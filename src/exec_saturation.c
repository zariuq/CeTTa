#include "exec_saturation.h"

#include "equation_presentation.h"
#include "match.h"
#include "stats.h"
#include "symbol.h"

#include <stdlib.h>
#include <string.h>

/* The library presentation this realization replaces (lib/lib_exec.metta). */
static const char *const EXEC_SATURATE_EQUATIONS[] = {
    "(= (exec:saturate $facts $directives)"
    "   (let* (($before (exec:size $facts))"
    "          ($_round (exec:round $facts $directives)))"
    "      (if (== $before (exec:size $facts))"
    "          ()"
    "          (exec:saturate $facts $directives))))",
};
static const char *const EXEC_ROUND_EQUATIONS[] = {
    "(= (exec:round $facts $directives)"
    "   (let $pending (new-space)"
    "      (let $_planned"
    "            (collapse"
    "               (let (exec $key $query $templates) (superpose $directives)"
    "                  (let (, $outputs) (decons-atom $templates)"
    "                     (match $facts $query (exec:emit $pending $outputs)))))"
    "         (exec:publish $facts $pending))))",
};
static const char *const EXEC_SIZE_EQUATIONS[] = {
    "(= (exec:size $facts)"
    "   (let $atoms (collapse (get-atoms $facts)) (size-atom $atoms)))",
};
static const char *const EXEC_EMIT_EQUATIONS[] = {
    "(= (exec:emit $pending $items)"
    "   (if-equal $items () ()"
    "      (let ($head $tail) (decons-atom $items)"
    "         (let $_ (add-atom-nodup $pending $head)"
    "            (exec:emit $pending $tail)))))",
};
static const char *const EXEC_PUBLISH_EQUATIONS[] = {
    "(= (exec:publish $facts $pending)"
    "   (collapse (match $pending $row (add-atom-nodup $facts $row))))",
};

typedef struct {
    const char *head;
    const char *const *sources;
    size_t count;
} ExecSaturationPresentation;

static const ExecSaturationPresentation EXEC_SATURATION_PRESENTATION[] = {
    {"exec:saturate", EXEC_SATURATE_EQUATIONS, 1u},
    {"exec:round", EXEC_ROUND_EQUATIONS, 1u},
    {"exec:size", EXEC_SIZE_EQUATIONS, 1u},
    {"exec:emit", EXEC_EMIT_EQUATIONS, 1u},
    {"exec:publish", EXEC_PUBLISH_EQUATIONS, 1u},
};

/* Admission of the presentation, per program space and revision. */
static _Thread_local struct {
    const Space *space;
    uint64_t instance_id;
    uint64_t revision;
    bool admitted;
} g_exec_saturation_presentation;

/* CETTA_EXEC_SATURATION_REFERENCE=1 runs the authored rounds, so the two can
 * be compared. */
static bool exec_saturation_reference(void) {
    static int reference = -1;
    if (reference < 0) {
        const char *setting = getenv("CETTA_EXEC_SATURATION_REFERENCE");
        reference = setting && strcmp(setting, "1") == 0;
    }
    return reference != 0;
}

static bool exec_saturation_presentation_admitted(Space *program) {
    if (exec_saturation_reference())
        return false;
    uint64_t instance_id = space_instance_id(program);
    uint64_t revision = space_revision(program);
    if (g_exec_saturation_presentation.space == program &&
        g_exec_saturation_presentation.instance_id == instance_id &&
        g_exec_saturation_presentation.revision == revision)
        return g_exec_saturation_presentation.admitted;
    Arena scratch;
    arena_init(&scratch);
    bool admitted = true;
    size_t count = sizeof(EXEC_SATURATION_PRESENTATION) /
                   sizeof(EXEC_SATURATION_PRESENTATION[0]);
    for (size_t i = 0u; admitted && i < count; i++) {
        const ExecSaturationPresentation *part =
            &EXEC_SATURATION_PRESENTATION[i];
        admitted = cetta_equation_presentation_exact(
            &scratch, program, part->head, part->sources, part->count);
    }
    /* Directives are data: no equation may rewrite one. */
    SpaceEquationCursor cursor;
    SymbolId exec = symbol_intern_cstr(g_symbols, "exec");
    Atom *directive = cetta_presentation_parse_one(
        &scratch, "(exec $key $query $templates)");
    if (admitted && (!directive || exec == SYMBOL_ID_NONE ||
                     !space_equation_cursor_init(program, exec, &cursor)))
        admitted = false;
    while (admitted) {
        SpaceEquationOccurrenceId occurrence_id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &occurrence_id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        SpaceEquationOccurrence occurrence;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_equation_occurrence_resolve(occurrence_id, &occurrence) ||
            cetta_patterns_overlap(occurrence.lhs, directive))
            admitted = false;
    }
    arena_free(&scratch);
    g_exec_saturation_presentation.space = program;
    g_exec_saturation_presentation.instance_id = instance_id;
    g_exec_saturation_presentation.revision = revision;
    g_exec_saturation_presentation.admitted = admitted;
    return admitted;
}

typedef struct {
    Atom **patterns;
    CettaExprLen npatterns;
    Atom **templates;
    CettaExprLen ntemplates;
} ExecSaturationRule;

static bool exec_saturation_is_connective(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len >= 1u &&
           (atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.comma) ||
            atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.pipe));
}

/* `(, x1 ... xn)`: its elements, or false. */
static bool exec_saturation_tuple(Atom *tuple, Atom ***items_out,
                                  CettaExprLen *len_out) {
    if (!tuple || tuple->kind != ATOM_EXPR || tuple->expr.len < 1u ||
        !atom_is_symbol_id(tuple->expr.elems[0], g_builtin_syms.comma))
        return false;
    *items_out = tuple->expr.elems + 1u;
    *len_out = tuple->expr.len - 1u;
    return true;
}

/* A directive the authored round reads as data: `(exec KEY (, P...) (, T...))`
 * with a flat query of one or more patterns, each read by the space's own
 * conjunction query as `match` reads it, and templates whose heads are
 * constants other than `=` and `:`, so a published fact is never a
 * definition. */
static bool exec_saturation_rule(Atom *directive, ExecSaturationRule *rule) {
    if (!directive || directive->kind != ATOM_EXPR ||
        directive->expr.len != 4u ||
        !atom_is_symbol(directive->expr.elems[0], "exec") ||
        !exec_saturation_tuple(directive->expr.elems[2], &rule->patterns,
                               &rule->npatterns) ||
        rule->npatterns == 0u ||
        !exec_saturation_tuple(directive->expr.elems[3], &rule->templates,
                               &rule->ntemplates))
        return false;
    for (CettaExprLen i = 0u; i < rule->npatterns; i++) {
        Atom *pattern = rule->patterns[i];
        if (!pattern || pattern->kind == ATOM_VAR ||
            exec_saturation_is_connective(pattern))
            return false;
    }
    for (CettaExprLen i = 0u; i < rule->ntemplates; i++) {
        Atom *head = rule->templates[i] &&
                rule->templates[i]->kind == ATOM_EXPR &&
                rule->templates[i]->expr.len > 0u
            ? rule->templates[i]->expr.elems[0] : rule->templates[i];
        if (!head || atom_has_vars(head) ||
            atom_is_symbol(head, "=") || atom_is_symbol(head, ":"))
            return false;
    }
    return true;
}

typedef struct {
    Atom **items;
    size_t len, cap;
} ExecSaturationAtoms;

static bool exec_saturation_push(ExecSaturationAtoms *list, Atom *atom) {
    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2u : 64u;
        Atom **items = realloc(list->items, cap * sizeof(*items));
        if (!items)
            return false;
        list->items = items;
        list->cap = cap;
    }
    list->items[list->len++] = atom;
    return true;
}

/* Every template instance under one row of a directive's query. */
static bool exec_saturation_propose(Arena *a, const ExecSaturationRule *rule,
                                    Bindings *row,
                                    ExecSaturationAtoms *proposals) {
    for (CettaExprLen i = 0u; i < rule->ntemplates; i++) {
        Atom *instance = bindings_apply(row, a, rule->templates[i]);
        if (!instance || !exec_saturation_push(proposals, instance))
            return false;
    }
    return true;
}

/* Rows of the whole query, the first round's. */
static bool exec_saturation_round_all(Space *facts, Arena *a,
                                      const ExecSaturationRule *rule,
                                      ExecSaturationAtoms *proposals) {
    BindingSet rows;
    binding_set_init(&rows);
    space_query_conjunction(facts, a, rule->patterns, rule->npatterns,
                            NULL, &rows);
    bool ok = true;
    for (CettaCount r = 0u; ok && r < rows.len; r++)
        ok = exec_saturation_propose(a, rule, &rows.items[r], proposals);
    binding_set_free(&rows);
    return ok;
}

/* Rows that read at least one fact of `delta`: each query position in turn
 * reads the delta, and the other patterns the whole space.  A row reading
 * several new facts is proposed once per such position; publication without
 * duplicates absorbs the repeats. */
static bool exec_saturation_round_delta(Space *facts, Space *delta, Arena *a,
                                        const ExecSaturationRule *rule,
                                        ExecSaturationAtoms *proposals) {
    Atom **rest = rule->npatterns > 1u
        ? arena_alloc(a, sizeof(*rest) * (size_t)(rule->npatterns - 1u))
        : NULL;
    if (rule->npatterns > 1u && !rest)
        return false;
    bool ok = true;
    for (CettaExprLen i = 0u; ok && i < rule->npatterns; i++) {
        BindingSet seeds;
        binding_set_init(&seeds);
        space_query_conjunction(delta, a, &rule->patterns[i], 1u, NULL,
                                &seeds);
        CettaExprLen k = 0u;
        for (CettaExprLen j = 0u; j < rule->npatterns; j++)
            if (j != i)
                rest[k++] = rule->patterns[j];
        for (CettaCount s = 0u; ok && s < seeds.len; s++) {
            if (k == 0u) {
                ok = exec_saturation_propose(a, rule, &seeds.items[s],
                                             proposals);
                continue;
            }
            BindingSet rows;
            binding_set_init(&rows);
            space_query_conjunction(facts, a, rest, k, &seeds.items[s],
                                    &rows);
            for (CettaCount r = 0u; ok && r < rows.len; r++)
                ok = exec_saturation_propose(a, rule, &rows.items[r],
                                             proposals);
            binding_set_free(&rows);
        }
        binding_set_free(&seeds);
    }
    return ok;
}

CettaExecSaturationOutcome cetta_exec_saturate(
    Space *program, Arena *a, Atom *call, Space *facts,
    const CettaExecSaturationHost *host, Atom **answer_out) {
    Atom *space_ref = call && call->kind == ATOM_EXPR &&
            call->expr.len == 3u ? call->expr.elems[1] : NULL;
    Atom *directives = space_ref ? call->expr.elems[2] : NULL;
    if (!program || !a || !facts || facts == program || !space_ref ||
        !directives || directives->kind != ATOM_EXPR || !host ||
        !host->add_nodup || !answer_out ||
        !exec_saturation_presentation_admitted(program))
        return CETTA_EXEC_SATURATION_DECLINED;
    CettaExprLen nrules = directives->expr.len;
    ExecSaturationRule *rules = nrules
        ? arena_alloc(a, sizeof(*rules) * (size_t)nrules) : NULL;
    if (nrules && !rules)
        return CETTA_EXEC_SATURATION_DECLINED;
    for (CettaExprLen i = 0u; i < nrules; i++)
        if (!exec_saturation_rule(directives->expr.elems[i], &rules[i]))
            return CETTA_EXEC_SATURATION_DECLINED;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EXEC_SATURATION_ADMITTED);

    ExecSaturationAtoms proposals = {0}, added = {0};
    Space delta;
    bool delta_live = false;
    bool first = true;
    CettaExecSaturationOutcome outcome = CETTA_EXEC_SATURATION_DONE;
    *answer_out = NULL;
    for (;;) {
        proposals.len = 0u;
        if (!first) {
            space_init_with_universe(&delta, facts->native.universe);
            delta_live = true;
            for (size_t i = 0u; i < added.len; i++)
                space_add(&delta, added.items[i]);
        }
        bool ok = true;
        for (CettaExprLen i = 0u; ok && i < nrules; i++)
            ok = first
                ? exec_saturation_round_all(facts, a, &rules[i], &proposals)
                : exec_saturation_round_delta(facts, &delta, a, &rules[i],
                                              &proposals);
        if (delta_live) {
            space_free(&delta);
            delta_live = false;
        }
        if (!ok) {
            *answer_out = atom_error(a, call,
                                     atom_symbol(a, "ExecSaturationFailed"));
            outcome = CETTA_EXEC_SATURATION_ERROR;
            break;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EXEC_SATURATION_ROUND);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_EXEC_SATURATION_PROPOSAL,
            (uint64_t)proposals.len);
        /* The authored round collects each publication's answer and reads
         * none of them, so a refused addition adds nothing and saturation
         * goes on. */
        added.len = 0u;
        for (size_t i = 0u; i < proposals.len; i++) {
            Atom *stored = NULL;
            (void)host->add_nodup(host->context, space_ref,
                                  proposals.items[i], &stored);
            if (stored && !exec_saturation_push(&added, stored)) {
                *answer_out = atom_error(
                    a, call, atom_symbol(a, "ExecSaturationFailed"));
                outcome = CETTA_EXEC_SATURATION_ERROR;
                break;
            }
        }
        if (outcome != CETTA_EXEC_SATURATION_DONE || added.len == 0u)
            break;
        first = false;
    }
    free(proposals.items);
    free(added.items);
    if (outcome == CETTA_EXEC_SATURATION_DONE)
        *answer_out = atom_unit(a);
    return outcome;
}
