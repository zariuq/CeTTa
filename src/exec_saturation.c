#include "exec_saturation.h"

#include "equation_presentation.h"
#include "match.h"
#include "stats.h"
#include "symbol.h"
#include "transform_core.h"
#include "u64_index.h"
#include "var_index.h"

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
    const char *type;
} ExecSaturationPresentation;

static const ExecSaturationPresentation EXEC_SATURATION_PRESENTATION[] = {
    {"exec:saturate", EXEC_SATURATE_EQUATIONS, 1u, "(-> Atom Atom %Undefined%)"},
    {"exec:round", EXEC_ROUND_EQUATIONS, 1u, "(-> Atom Atom %Undefined%)"},
    {"exec:size", EXEC_SIZE_EQUATIONS, 1u, "(-> Atom %Undefined%)"},
    {"exec:emit", EXEC_EMIT_EQUATIONS, 1u, "(-> Atom Atom %Undefined%)"},
    {"exec:publish", EXEC_PUBLISH_EQUATIONS, 1u, "(-> Atom Atom %Undefined%)"},
    {"exec", NULL, 0u, "(-> Atom Atom Atom Atom)"},
};

/* Equations, declarations and imported boundaries of the presentation. */
static _Thread_local struct {
    SpaceTheoryToken program;
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

bool cetta_exec_saturation_presentation_admitted(Space *program) {
    if (!program || exec_saturation_reference())
        return false;
    SpaceTheoryToken token = space_theory_token(program);
    if (space_theory_token_eq(g_exec_saturation_presentation.program, token))
        return g_exec_saturation_presentation.admitted;
    Arena scratch;
    arena_init(&scratch);
    bool admitted = true;
    size_t count = sizeof(EXEC_SATURATION_PRESENTATION) /
                   sizeof(EXEC_SATURATION_PRESENTATION[0]);
    for (size_t i = 0u; admitted && i < count; i++) {
        const ExecSaturationPresentation *part =
            &EXEC_SATURATION_PRESENTATION[i];
        admitted = part->count == 0u || cetta_equation_presentation_exact(
            &scratch, program, part->head, part->sources, part->count);
        Atom **types = NULL;
        uint32_t ntypes = space_get_declared_types(
            program, &scratch, atom_symbol(&scratch, part->head), &types);
        Atom *expected = cetta_presentation_parse_one(&scratch, part->type);
        admitted = admitted && expected && ntypes == 1u &&
            atom_alpha_eq(types[0], expected);
        free(types);
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
    g_exec_saturation_presentation.program = token;
    g_exec_saturation_presentation.admitted = admitted;
    return admitted;
}

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

bool cetta_exec_directive_read(Atom *directive, CettaExecDirective *rule) {
    if (!directive || directive->kind != ATOM_EXPR ||
        directive->expr.len != 4u ||
        !atom_is_symbol(directive->expr.elems[0], "exec") ||
        !exec_saturation_tuple(directive->expr.elems[2], &rule->patterns,
                               &rule->npatterns) ||
        !exec_saturation_tuple(directive->expr.elems[3], &rule->templates,
                               &rule->ntemplates))
        return false;
    rule->key = directive->expr.elems[1];
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
    Atom **variables;
    uint32_t length;
    size_t capacity;
    CettaVarIndex index;
    bool reading;
    bool failed;
} ExecSaturationLicense;

/* A deliberately recognizable first-order data signature. Private views,
 * executable constructors and mutable/opaque leaves retain their existing
 * realization. Every produced variable must occur in a read. */
static bool exec_saturation_refuses_node(const Atom *atom, void *context) {
    ExecSaturationLicense *license = context;
    if (atom->kind == ATOM_EXPR) {
        if (atom_is_carrier(atom) || atom_petta_value_representation(atom) != PETTA_VALUE_ORDINARY)
            return true;
        return atom->expr.len &&
            (atom->expr.elems[0]->kind != ATOM_SYMBOL ||
             symbol_id_is_builtin(atom->expr.elems[0]->sym_id));
    }
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind != GV_INT && atom->ground.gkind != GV_BOOL &&
            atom->ground.gkind != GV_STRING;
    if (atom->kind != ATOM_VAR)
        return false;
    if (cetta_var_index_find_atom(&license->index, license->variables,
                                  license->length, atom->var_id) != UINT32_MAX)
        return false;
    if (!license->reading)
        return true;
    if (license->length == UINT32_MAX) {
        license->failed = true;
        return true;
    }
    if (license->length == license->capacity) {
        size_t capacity = license->capacity ? license->capacity * 2u : 16u;
        if (capacity < license->capacity || capacity > SIZE_MAX / sizeof(Atom *)) {
            license->failed = true;
            return true;
        }
        Atom **variables = realloc(license->variables, capacity * sizeof(*variables));
        if (!variables) { license->failed = true; return true; }
        license->variables = variables;
        license->capacity = capacity;
    }
    license->variables[license->length++] = (Atom *)atom;
    if (!cetta_var_index_note_atom(&license->index, license->variables, license->length)) {
        license->failed = true;
        return true;
    }
    return false;
}

static bool exec_saturation_licensed_term(ExecSaturationLicense *license, Atom *term) {
    bool complete = false;
    bool refused = atom_graph_any_checked(term, exec_saturation_refuses_node,
                                           NULL, license, SIZE_MAX, &complete);
    return complete && !refused && !license->failed;
}

/* SetStoreSyntax.Propagates and a ground seed, on a fixed native set world.
 * The existing no-take/no-tell exec grammar supplies positive reads only.
 * This is an admission recognizer, not a proof of the C implementation. */
static bool exec_saturation_set_license(Space *facts,
                                        const CettaExecDirective *rules, CettaExprLen nrules) {
    if (!space_is_set(facts) || facts->module_own_view || facts->overlay_base || facts->imported_len || facts->dep_count ||
        facts->match_backend.kind != SPACE_ENGINE_NATIVE || !space_contains_only_exact_atoms(facts))
        return false;
    ExecSaturationLicense license = {0};
    bool accepted = true;
    CettaCount length = space_length64(facts);
    for (CettaIndex i = 0u; accepted && i < length; i++) {
        Atom *row = space_get_at64(facts, i);
        accepted = row && !atom_has_vars(row) && exec_saturation_licensed_term(&license, row);
    }
    for (CettaExprLen i = 0u; accepted && i < nrules; i++) {
        license.length = 0u;
        license.reading = true;
        for (CettaExprLen j = 0u; accepted && j < rules[i].npatterns; j++)
            accepted = exec_saturation_licensed_term(&license, rules[i].patterns[j]);
        license.reading = false;
        for (CettaExprLen j = 0u; accepted && j < rules[i].ntemplates; j++)
            accepted = exec_saturation_licensed_term(&license, rules[i].templates[j]);
    }
    cetta_var_index_free(&license.index);
    free(license.variables);
    return accepted;
}

static SymbolId exec_saturation_head(Atom *term) {
    if (term->kind == ATOM_SYMBOL) return term->sym_id;
    if (term->kind == ATOM_EXPR && term->expr.len && term->expr.elems[0]->kind == ATOM_SYMBOL)
        return term->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

/* A disjoint constructor head cannot be a new premise. Unknown heads and
 * empty-premise rules are always retained; the actual delta queries still
 * enumerate every hot instance, including its duplicate derivations. */
static bool exec_saturation_hot_rule(const CettaExecDirective *rule,
                                     const CettaU64Index *heads) {
    if (!rule->npatterns) return true;
    for (CettaExprLen j = 0u; j < rule->npatterns; j++) {
        SymbolId head = exec_saturation_head(rule->patterns[j]);
        uint32_t ignored;
        if (head == SYMBOL_ID_NONE || cetta_u64_index_find(heads, head, &ignored))
            return true;
    }
    return false;
}

bool cetta_exec_saturation_admitted(Space *program, Atom *call, Space *facts) {
    if (!program || !facts || facts == program || !call ||
        call->kind != ATOM_EXPR || call->expr.len != 3u ||
        !atom_is_symbol_id(call->expr.elems[0], g_builtin_syms.exec_saturate) ||
        !call->expr.elems[2] || call->expr.elems[2]->kind != ATOM_EXPR ||
        !cetta_exec_saturation_presentation_admitted(program))
        return false;
    Atom *directives = call->expr.elems[2];
    for (CettaExprLen i = 0u; i < directives->expr.len; i++) {
        CettaExecDirective rule;
        if (!cetta_exec_directive_read(directives->expr.elems[i], &rule))
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

bool cetta_exec_directive_rows(Space *facts, Space *delta, Arena *a,
                               const CettaExecDirective *rule,
                               CettaExecRowVisit visit, void *context) {
    if (rule->npatterns == 0u) {
        Bindings unit;
        bindings_init(&unit);
        bool completed = visit(context, rule, &unit);
        bindings_free(&unit);
        return completed;
    }
    if (!delta) {
        BindingSet rows;
        binding_set_init(&rows);
        space_query_conjunction(facts, a, rule->patterns, rule->npatterns,
                                NULL, &rows);
        bool ok = true;
        for (CettaCount r = 0u; ok && r < rows.len; r++)
            ok = visit(context, rule, &rows.items[r]);
        binding_set_free(&rows);
        return ok;
    }
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
                ok = visit(context, rule, &seeds.items[s]);
                continue;
            }
            BindingSet rows;
            binding_set_init(&rows);
            space_query_conjunction(facts, a, rest, k, &seeds.items[s],
                                    &rows);
            for (CettaCount r = 0u; ok && r < rows.len; r++)
                ok = visit(context, rule, &rows.items[r]);
            binding_set_free(&rows);
        }
        binding_set_free(&seeds);
    }
    return ok;
}

typedef struct {
    Arena *arena;
    ExecSaturationAtoms *proposals;
} ExecSaturationProposer;

/* Every template instance under one row of a directive's query. */
static bool exec_saturation_propose(void *context,
                                    const CettaExecDirective *rule,
                                    Bindings *row) {
    ExecSaturationProposer *proposer = context;
    for (CettaExprLen i = 0u; i < rule->ntemplates; i++) {
        Atom *instance = bindings_apply(row, proposer->arena,
                                        rule->templates[i]);
        if (!instance || !exec_saturation_push(proposer->proposals, instance))
            return false;
    }
    return true;
}

CettaExecSaturationOutcome cetta_exec_saturate(
    Space *program, Arena *a, Atom *call, Space *facts,
    const CettaExecSaturationHost *host, Atom **answer_out) {
    Atom *space_ref = call && call->kind == ATOM_EXPR &&
            call->expr.len == 3u ? call->expr.elems[1] : NULL;
    Atom *directives = space_ref ? call->expr.elems[2] : NULL;
    if (!a || !host || !host->add_nodup || !answer_out ||
        !cetta_exec_saturation_admitted(program, call, facts))
        return CETTA_EXEC_SATURATION_DECLINED;
    CettaExprLen nrules = directives->expr.len;
    CettaExecDirective *rules = nrules
        ? arena_alloc(a, sizeof(*rules) * (size_t)nrules) : NULL;
    if (nrules && !rules)
        return CETTA_EXEC_SATURATION_DECLINED;
    for (CettaExprLen i = 0u; i < nrules; i++)
        if (!cetta_exec_directive_read(directives->expr.elems[i], &rules[i]))
            return CETTA_EXEC_SATURATION_DECLINED;
    bool licensed = host->set_membership_publication &&
        cetta_transform_engine() == CETTA_TRANSFORM_ENGINE_TCALC &&
        exec_saturation_set_license(facts, rules, nrules);
    if (getenv("CETTA_TRANSFORM_TRACE"))
        fprintf(stderr, "[transform] saturate (engine %s) (fast-path %s) (observer completed-set)\n",
                licensed ? "tcalc" : "direct", licensed ? "bulk-set semi-naive" : "none");
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
        ExecSaturationProposer proposer = {a, &proposals};
        CettaU64Index heads = {0};
        bool indexed = licensed && !first;
        for (size_t i = 0u; indexed && i < added.len; i++) {
            SymbolId head = exec_saturation_head(added.items[i]);
            if (head == SYMBOL_ID_NONE) { indexed = false; break; }
            CettaGsltU32IndexInsertResultV1 inserted = cetta_u64_index_insert(&heads, head, 0u);
            indexed = inserted == CETTA_GSLT_U32_INDEX_INSERTED_V1 ||
                inserted == CETTA_GSLT_U32_INDEX_DUPLICATE_V1;
        }
        bool ok = true;
        uint64_t cold = 0u;
        for (CettaExprLen i = 0u; ok && i < nrules; i++) {
            if (indexed && !exec_saturation_hot_rule(&rules[i], &heads)) {
                cold++;
                continue;
            }
            ok = cetta_exec_directive_rows(facts, first ? NULL : &delta, a,
                                           &rules[i], exec_saturation_propose,
                                           &proposer);
        }
        cetta_u64_index_free(&heads);
        if (licensed && getenv("CETTA_TRANSFORM_TRACE"))
            fprintf(stderr, "[transform] saturation round (cold-rules %llu)\n", (unsigned long long)cold);
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
