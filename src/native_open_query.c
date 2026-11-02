#include "native_query.h"
#include "term_molecule.h"

#include <stdlib.h>
#include <string.h>

/*
 * Joins over facts holding variables (native_query.h, for a query whose
 * legs read such facts).  The legs are read in the order given, through the
 * spaces' occurrence cursors, as the ordinary match reads them: each fact
 * under a fresh frame, so that it is renamed apart, and unified with its
 * pattern by ids (term_molecule.h); a fact a relation keeps as a skeleton
 * and holes is unified as those.  Each solution's effect instances are made
 * by ids as alpha keys, into a relation of deep facts as their holes, and
 * the run makes them when the loops end.  It is admitted only where that
 * changes nothing a leg reads: no instance of an addition meets a pattern
 * read in its target.
 */

enum { NATIVE_OPEN_LEGS = 8u, NATIVE_OPEN_EFFECTS = 4u };

typedef struct {
    AtomId *ids;
    size_t len, cap;
} NativeOpenIds;

typedef struct {
    /* The data's head, and when its target keeps a relation of deep facts
     * for it, that relation's spine, copied: instances fitting it are made
     * as holes. */
    SymbolId head;
    bool by_holes;
    CettaSkeleton spine;
    NativeOpenIds holes;
    size_t hole_facts;
    NativeOpenIds keys;
} NativeOpenEffect;

typedef struct {
    const CettaNativeQuery *query;
    TermUniverse *universe;
    CettaMolStore mol;
    /* The frame of the query's own variables, those of its patterns and
     * effects. */
    uint32_t frame;
    AtomId patterns[NATIVE_OPEN_LEGS];
    AtomId data[NATIVE_OPEN_EFFECTS];
    NativeOpenEffect effects[NATIVE_OPEN_EFFECTS];
    uint64_t answers;
    bool declined;
} NativeOpenRun;

static bool native_open_push(NativeOpenIds *ids, const AtomId *items,
                             size_t count) {
    if (ids->len + count > ids->cap) {
        size_t cap = ids->cap ? ids->cap : 64u;
        while (cap < ids->len + count)
            cap *= 2u;
        AtomId *grown = realloc(ids->ids, cap * sizeof(*grown));
        if (!grown)
            return false;
        ids->ids = grown;
        ids->cap = cap;
    }
    memcpy(ids->ids + ids->len, items, count * sizeof(*items));
    ids->len += count;
    return true;
}

/* A solution: each effect's instance made, then one answer. */
static bool native_open_solution(NativeOpenRun *run) {
    size_t effect_count = run->query->consumer == CETTA_NATIVE_QUERY_EFFECTS
        ? run->query->effect_count : 0u;
    for (size_t e = 0u; e < effect_count; e++) {
        NativeOpenEffect *made = &run->effects[e];
        CettaMol data = {run->data[e], run->frame};
        if (made->by_holes) {
            AtomId holes[CETTA_SKELETON_NODES];
            CettaMolResult cut =
                cetta_mol_key_holes(&run->mol, data, &made->spine, holes);
            if (cut == CETTA_MOL_OK) {
                if (!native_open_push(&made->holes, holes, made->spine.holes))
                    return false;
                made->hole_facts++;
                continue;
            }
            if (cut == CETTA_MOL_DECLINE) {
                run->declined = true;
                return false;
            }
        }
        CettaMolNumbering numbering = {.len = 0u};
        AtomId key = CETTA_ATOM_ID_NONE;
        CettaMolResult keyed = cetta_mol_key(&run->mol, data, &numbering, &key);
        if (keyed != CETTA_MOL_OK) {
            run->declined = true;
            return false;
        }
        if (!native_open_push(&made->keys, &key, 1u))
            return false;
    }
    run->answers++;
    return true;
}

static bool native_open_leg(NativeOpenRun *run, size_t leg) {
    const CettaNativeQuery *query = run->query;
    if (leg == query->pattern_count)
        return native_open_solution(run);
    Space *space = query->sources ? query->sources[leg] : query->source;
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!space_occurrence_cursor_init(space, query->patterns[leg], &cursor) ||
        space_occurrence_cursor_universe(&cursor) != run->universe) {
        space_occurrence_cursor_release(&cursor);
        run->declined = true;
        return false;
    }
    CettaMol pattern = {run->patterns[leg], run->frame};
    /* The pattern aligned with the spine of the kept facts, found when the
     * first of them is met. */
    const CettaSkeleton *aligned_spine = NULL;
    CettaMolAligned aligned;
    CettaMolResult alignment = CETTA_MOL_DECLINE;
    bool ok = true;
    while (ok) {
        AtomId row = CETTA_ATOM_ID_NONE;
        size_t kept = SIZE_MAX;
        SpaceOccurrenceCursorStep step =
            space_occurrence_cursor_next_kept(&cursor, &row, &kept);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        /* A row the loops cannot read by its id declines the run. */
        if (step != SPACE_OCCURRENCE_CURSOR_ITEM ||
            (row == CETTA_ATOM_ID_NONE && kept == SIZE_MAX)) {
            run->declined = true;
            ok = false;
            break;
        }
        CettaMolMark mark = cetta_mol_mark(&run->mol);
        uint32_t frame = cetta_mol_frame(&run->mol);
        CettaMolResult unified;
        if (kept != SIZE_MAX) {
            const CettaSkeleton *spine = NULL;
            const AtomId *holes = NULL;
            space_occurrence_cursor_kept_fact(&cursor, kept, &spine, &holes);
            if (spine != aligned_spine) {
                aligned_spine = spine;
                alignment = cetta_mol_align(&run->mol, pattern, spine,
                                            &aligned);
            }
            unified = alignment == CETTA_MOL_OK
                ? cetta_mol_unify_aligned(&run->mol, &aligned, holes, frame)
                : alignment == CETTA_MOL_FAIL
                    ? CETTA_MOL_FAIL
                    : cetta_mol_unify_kept(&run->mol, pattern, spine, holes,
                                           frame);
        } else {
            unified = cetta_mol_unify(&run->mol, pattern,
                                      (CettaMol){row, frame});
        }
        if (unified == CETTA_MOL_DECLINE) {
            run->declined = true;
            ok = false;
        } else if (unified == CETTA_MOL_OK) {
            ok = native_open_leg(run, leg + 1u);
        }
        cetta_mol_undo(&run->mol, mark);
    }
    space_occurrence_cursor_release(&cursor);
    return ok;
}

/* Whether an instance of DATA may meet PATTERN, each read apart with every
 * variable free; a walk that cannot tell says it may. */
static bool native_open_may_meet(NativeOpenRun *run, AtomId data,
                                 AtomId pattern) {
    CettaMolMark mark = cetta_mol_mark(&run->mol);
    uint32_t left = cetta_mol_frame(&run->mol);
    uint32_t right = cetta_mol_frame(&run->mol);
    CettaMolResult met = cetta_mol_unify(&run->mol, (CettaMol){data, left},
                                         (CettaMol){pattern, right});
    cetta_mol_undo(&run->mol, mark);
    return met != CETTA_MOL_FAIL;
}

static void native_open_free(NativeOpenRun *run) {
    for (size_t e = 0u; e < NATIVE_OPEN_EFFECTS; e++) {
        free(run->effects[e].holes.ids);
        free(run->effects[e].keys.ids);
    }
    cetta_mol_store_free(&run->mol);
}

CettaNativeQueryStatus cetta_native_open_query_run(
    const CettaNativeQuery *query, Arena *arena,
    CettaNativeQueryResult *result) {
    bool effects = query->consumer == CETTA_NATIVE_QUERY_EFFECTS;
    size_t effect_count = effects ? query->effect_count : 0u;
    TermUniverse *universe = query->source->native.universe;
    if (query->rows || query->guard_space || query->filter_count ||
        query->inert_head_count ||
        query->distinct_key || query->pattern_count > NATIVE_OPEN_LEGS ||
        effect_count > NATIVE_OPEN_EFFECTS ||
        (!query->count_only && atom_has_vars(query->template_atom)) ||
        (effects && !query->apply))
        return CETTA_NATIVE_QUERY_DECLINED;
    for (size_t i = 0u; i < query->pattern_count; i++) {
        Space *read = query->sources ? query->sources[i] : query->source;
        if (!read || read->native.universe != universe ||
            query->patterns[i]->kind != ATOM_EXPR)
            return CETTA_NATIVE_QUERY_DECLINED;
    }
    /* Additions into sets, which hold an instance as its alpha key. */
    for (size_t e = 0u; e < effect_count; e++) {
        const CettaNativeQueryEffect *made = &query->effects[e];
        if (made->removal || made->refused_head_count != 0u ||
            !made->target || !made->data ||
            made->target->native.universe != universe ||
            !space_is_set(made->target))
            return CETTA_NATIVE_QUERY_DECLINED;
    }
    NativeOpenRun run = {.query = query, .universe = universe};
    cetta_mol_store_init(&run.mol, universe);
    run.frame = cetta_mol_frame(&run.mol);
    /* The patterns and the effects' data are stored as the elements of one
     * term, whose variables the universe numbers together: a variable is
     * one cell of the query's frame wherever it occurs. */
    Atom *parts[1u + NATIVE_OPEN_LEGS + NATIVE_OPEN_EFFECTS];
    size_t part_count = 0u;
    parts[part_count++] = atom_symbol_id(arena, g_builtin_syms.match);
    for (size_t i = 0u; i < query->pattern_count; i++)
        parts[part_count++] = query->patterns[i];
    for (size_t e = 0u; e < effect_count; e++)
        parts[part_count++] = query->effects[e].data;
    Atom *joined = parts[0]
        ? atom_expr(arena, parts, (CettaExprLen)part_count) : NULL;
    AtomId stored = joined
        ? term_universe_store_atom_id(universe, arena, joined)
        : CETTA_ATOM_ID_NONE;
    bool admitted = stored != CETTA_ATOM_ID_NONE &&
        tu_arity(universe, stored) == (CettaExprLen)part_count &&
        cetta_mol_touch(&run.mol, (CettaMol){stored, run.frame}) ==
            CETTA_MOL_OK;
    for (size_t i = 0u; admitted && i < query->pattern_count; i++)
        run.patterns[i] = tu_child(universe, stored, (CettaExprIndex)(1u + i));
    for (size_t e = 0u; admitted && e < effect_count; e++) {
        Atom *data = query->effects[e].data;
        run.data[e] = tu_child(universe, stored,
                               (CettaExprIndex)(1u + query->pattern_count + e));
        NativeOpenEffect *made = &run.effects[e];
        made->head = data->kind == ATOM_EXPR && data->expr.len > 0u &&
                data->expr.elems[0]->kind == ATOM_SYMBOL
            ? atom_symbol_current_id(data->expr.elems[0]) : SYMBOL_ID_NONE;
        made->by_holes = admitted && made->head != SYMBOL_ID_NONE &&
            query->apply_holes &&
            space_dense_deep_spine(query->effects[e].target, made->head,
                                   &made->spine) &&
            cetta_skeleton_len(&made->spine) == data->expr.len;
        /* No instance meets a pattern its target is read by, through any
         * view of it. */
        Space *target = space_mutation_target(query->effects[e].target);
        for (size_t i = 0u; admitted && i < query->pattern_count; i++) {
            Space *read = query->sources ? query->sources[i] : query->source;
            if (space_mutation_target(read) == target &&
                native_open_may_meet(&run, run.data[e], run.patterns[i]))
                admitted = false;
        }
    }
    if (!admitted) {
        native_open_free(&run);
        return CETTA_NATIVE_QUERY_DECLINED;
    }
    bool done = native_open_leg(&run, 0u);
    /* The effects, made once the loops have read everything. */
    bool made_effects = false;
    for (size_t e = 0u; done && e < effect_count; e++) {
        const CettaNativeQueryEffect *effect = &query->effects[e];
        NativeOpenEffect *made = &run.effects[e];
        if (made->keys.len) {
            made_effects = true;
            done = query->apply(query->apply_context, effect, made->keys.ids,
                                made->keys.len);
        }
        if (done && made->hole_facts) {
            made_effects = true;
            done = query->apply_holes(query->apply_context, effect, made->head,
                                      &made->spine, made->holes.ids,
                                      made->hole_facts);
        }
    }
    uint64_t answers = run.answers;
    bool declined = run.declined;
    native_open_free(&run);
    if (!done)
        return made_effects && !declined ? CETTA_NATIVE_QUERY_FAILED
                                         : CETTA_NATIVE_QUERY_DECLINED;
    if (!query->count_only) {
        Atom *collection = cetta_expr_len_mul_fits_size((CettaExprLen)answers,
                                                        sizeof(Atom *))
            ? atom_expr_builder_begin(arena, (CettaExprLen)answers) : NULL;
        if (!collection)
            return made_effects ? CETTA_NATIVE_QUERY_FAILED
                                : CETTA_NATIVE_QUERY_DECLINED;
        for (uint64_t index = 0u; index < answers; index++)
            collection->expr.elems[index] = query->template_atom;
        result->collection = atom_expr_builder_finish(arena, collection);
        if (!result->collection)
            return made_effects ? CETTA_NATIVE_QUERY_FAILED
                                : CETTA_NATIVE_QUERY_DECLINED;
    }
    result->answers = answers;
    return CETTA_NATIVE_QUERY_DONE;
}
