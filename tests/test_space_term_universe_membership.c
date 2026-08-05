#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

void space_match_backend_init(Space *s) {
    memset(&s->match_backend, 0, sizeof(s->match_backend));
    s->match_backend.kind = SPACE_ENGINE_NATIVE;
}

void space_match_backend_free(Space *s) {
    (void)s;
}

bool space_match_backend_try_set(Space *s, SpaceEngine kind) {
    if (s)
        s->match_backend.kind = kind;
    return true;
}

bool space_match_backend_needs_atom_on_add(const Space *s, AtomId atom_id) {
    (void)s;
    (void)atom_id;
    return false;
}

void space_match_backend_note_add(Space *s, AtomId atom_id, Atom *atom,
                                  CettaIndex atom_idx) {
    (void)s;
    (void)atom_id;
    (void)atom;
    (void)atom_idx;
}

void space_match_backend_note_remove(Space *s) {
    (void)s;
}

CettaIndex space_match_backend_candidates64(Space *s, Atom *pattern,
                                            CettaIndex **out) {
    (void)s;
    (void)pattern;
    if (out)
        *out = NULL;
    return 0;
}

uint32_t space_match_backend_candidates(Space *s, Atom *pattern, uint32_t **out) {
    (void)s;
    (void)pattern;
    if (out)
        *out = NULL;
    return 0;
}

void space_match_backend_query(Space *s, Arena *a, Atom *query, SubstMatchSet *out) {
    (void)s;
    (void)a;
    (void)query;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
}

void space_match_backend_query_conjunction(Space *s, Arena *a, Atom **patterns,
                                           uint32_t npatterns,
                                           const Bindings *seed, BindingSet *out) {
    (void)s;
    (void)a;
    (void)patterns;
    (void)npatterns;
    (void)seed;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
}

const char *space_match_backend_name(const Space *s) {
    (void)s;
    return "stub";
}

bool space_match_backend_supports_direct_bindings(const Space *s) {
    (void)s;
    return false;
}

const char *space_match_backend_kind_name(SpaceEngine kind) {
    (void)kind;
    return "stub";
}

bool space_match_backend_kind_from_name(const char *name, SpaceEngine *out) {
    (void)name;
    if (out)
        *out = SPACE_ENGINE_NATIVE;
    return true;
}

const char *space_match_backend_unavailable_reason(SpaceEngine kind) {
    (void)kind;
    return NULL;
}

bool space_match_backend_attach_act_file(Space *s, const char *path,
                                         uint64_t *out_loaded) {
    (void)s;
    (void)path;
    if (out_loaded)
        *out_loaded = 0;
    return false;
}

bool space_match_backend_materialize_attached(Space *s, Arena *persistent_arena) {
    (void)s;
    (void)persistent_arena;
    return true;
}

bool space_match_backend_materialize_native_storage(Space *s,
                                                    Arena *persistent_arena) {
    (void)s;
    (void)persistent_arena;
    return true;
}

bool space_match_backend_store_atom_id_direct(Space *s, AtomId atom_id,
                                              Atom *atom) {
    (void)s;
    (void)atom_id;
    (void)atom;
    return false;
}

bool space_match_backend_store_atom_direct(Space *s, Atom *atom) {
    (void)s;
    (void)atom;
    return false;
}

SpaceBackendBatchResult space_match_backend_store_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_added) {
    (void)s;
    (void)atom_ids;
    (void)atom_count;
    if (out_added)
        *out_added = 0;
    return SPACE_BACKEND_BATCH_UNSUPPORTED;
}

bool space_match_backend_remove_atom_id_direct(Space *s, AtomId atom_id) {
    (void)s;
    (void)atom_id;
    return false;
}

bool space_match_backend_remove_atom_direct(Space *s, Atom *atom) {
    (void)s;
    (void)atom;
    return false;
}

SpaceBackendBatchResult space_match_backend_remove_atom_ids_batch_direct(
    Space *s, const AtomId *atom_ids, CettaCount atom_count,
    uint64_t *out_removed) {
    (void)s;
    (void)atom_ids;
    (void)atom_count;
    if (out_removed)
        *out_removed = 0;
    return SPACE_BACKEND_BATCH_UNSUPPORTED;
}

bool space_match_backend_truncate_direct(Space *s, uint32_t new_len) {
    (void)s;
    (void)new_len;
    return false;
}

bool space_match_backend_truncate_direct64(Space *s, uint64_t new_len) {
    if (new_len > UINT32_MAX)
        return false;
    return space_match_backend_truncate_direct(s, (uint32_t)new_len);
}

bool space_match_backend_load_sexpr_chunk(Space *s, Arena *persistent_arena,
                                          const uint8_t *text, size_t len,
                                          uint64_t *out_added) {
    (void)s;
    (void)persistent_arena;
    (void)text;
    (void)len;
    if (out_added)
        *out_added = 0;
    return false;
}

bool space_match_backend_remove_sexpr_chunk(Space *s, Arena *persistent_arena,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed) {
    (void)s;
    (void)persistent_arena;
    (void)text;
    (void)len;
    if (out_removed)
        *out_removed = 0;
    return false;
}

bool space_match_backend_step(Space *s, Arena *persistent_arena,
                              uint64_t steps, uint64_t *out_performed) {
    (void)s;
    (void)persistent_arena;
    (void)steps;
    if (out_performed)
        *out_performed = 0;
    return false;
}

bool space_match_backend_is_attached_compiled(const Space *s) {
    (void)s;
    return false;
}

bool space_match_backend_bridge_space(Space *s, CettaMorkSpaceHandle **out_bridge) {
    (void)s;
    if (out_bridge)
        *out_bridge = NULL;
    return false;
}

uint64_t space_match_backend_logical_len64(const Space *s) {
    return s ? s->len : 0;
}

static AtomId test_space_native_atom_id_at(const Space *s, uint64_t idx) {
    if (!s || !s->atom_ids || idx >= s->len)
        return CETTA_ATOM_ID_NONE;
    return cetta_atom_id_storage_load_bits(
        s->atom_ids +
            ((size_t)(s->start + idx) *
             cetta_atom_id_storage_width_bytes_from_bits(
                 s->atom_id_width_bits)),
        s->atom_id_width_bits);
}

AtomId space_match_backend_get_atom_id_at(const Space *s, uint32_t idx) {
    return test_space_native_atom_id_at(s, idx);
}

AtomId space_match_backend_get_atom_id_at64(const Space *s, uint64_t idx) {
    return test_space_native_atom_id_at(s, idx);
}

Atom *space_match_backend_get_at(const Space *s, uint32_t idx) {
    AtomId atom_id = space_match_backend_get_atom_id_at(s, idx);
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    return term_universe_get_atom(s->native.universe, atom_id);
}

Atom *space_match_backend_get_at64(const Space *s, uint64_t idx) {
    if (idx > UINT32_MAX)
        return NULL;
    return space_match_backend_get_at(s, (uint32_t)idx);
}

bool space_match_backend_mork_query_bindings_direct(
    CettaMorkSpaceHandle *bridge, Arena *a, Atom *query, BindingSet *out) {
    (void)bridge;
    (void)a;
    (void)query;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
    return false;
}

bool space_match_backend_mork_query_conjunction_direct(
    CettaMorkSpaceHandle *bridge, Arena *a, Atom **patterns,
    CettaExprLen npatterns, const Bindings *seed, BindingSet *out) {
    (void)bridge;
    (void)a;
    (void)patterns;
    (void)npatterns;
    (void)seed;
    if (out) {
        out->items = NULL;
        out->len = 0;
        out->cap = 0;
    }
    return false;
}

void space_match_backend_print_inventory(FILE *out) {
    (void)out;
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static Atom *make_pair(Arena *a, SymbolId pair_sym, int64_t lhs, int64_t rhs) {
    Atom *elems[3] = {
        atom_symbol_id(a, pair_sym),
        atom_int(a, lhs),
        atom_int(a, rhs),
    };
    return atom_expr(a, elems, 3);
}

static Atom *make_pair_var(Arena *a, SymbolId pair_sym) {
    Atom *elems[3] = {
        atom_symbol_id(a, pair_sym),
        atom_var_with_id(a, "x", 1),
        atom_int(a, 2),
    };
    return atom_expr(a, elems, 3);
}

static Atom *make_boxed_space(Arena *a, SymbolId box_sym, Space *space) {
    Atom *elems[2] = {
        atom_symbol_id(a, box_sym),
        atom_space(a, space),
    };
    return atom_expr(a, elems, 2);
}

static void reset_test_counters(void) {
    test_runtime_stats_reset_counters();
}

static uint64_t test_counter(CettaRuntimeCounter counter) {
    return test_runtime_stats_counter(counter);
}

static void assert_exact_match_one(Space *space, Atom *query, CettaIndex want) {
    uint32_t *matches = NULL;
    uint32_t nmatches = space_exact_match_indices(space, query, &matches);
    assert(nmatches == 1);
    assert(matches != NULL);
    assert(matches[0] == want);
    free(matches);
}

static void assert_exact_match_none(Space *space, Atom *query) {
    uint32_t *matches = NULL;
    uint32_t nmatches = space_exact_match_indices(space, query, &matches);
    assert(nmatches == 0);
    assert(matches == NULL);
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    Arena scratch_a;
    Arena scratch_b;
    Arena scratch_c;
    Arena scratch_d;
    Arena scratch_e;
    TermUniverse universe;
    Space left;
    Space right;

    init_test_symbols(&symbols);

    {
        Arena equation_persistent;
        Arena equation_scratch;
        TermUniverse equation_universe;
        Space equation_space;
        char name[64];

        arena_init(&equation_persistent);
        arena_init(&equation_scratch);
        term_universe_init(&equation_universe);
        term_universe_set_persistent_arena(&equation_universe,
                                           &equation_persistent);
        space_init_with_universe(&equation_space, &equation_universe);

        SymbolId source_head =
            symbol_intern_cstr(g_symbols, "equation-head-source");
        for (uint32_t i = 0; i < 255; i++) {
            snprintf(name, sizeof(name), "equation-head-filler-a-%u", i);
            (void)symbol_intern_cstr(g_symbols, name);
        }
        SymbolId colliding_head =
            symbol_intern_cstr(g_symbols, "equation-head-collision");
        for (uint32_t i = 0; i < 255; i++) {
            snprintf(name, sizeof(name), "equation-head-filler-b-%u", i);
            (void)symbol_intern_cstr(g_symbols, name);
        }
        SymbolId absent_colliding_head =
            symbol_intern_cstr(g_symbols, "equation-head-absent-collision");
        assert(colliding_head - source_head == 256);
        assert(absent_colliding_head - colliding_head == 256);

        Atom *source_lhs_elems[1] = {
            atom_symbol_id(&equation_scratch, source_head),
        };
        Atom *source_lhs = atom_expr(&equation_scratch, source_lhs_elems, 1);
        Atom *source_equation =
            atom_expr3(&equation_scratch,
                       atom_symbol_id(&equation_scratch,
                                      g_builtin_syms.equals),
                       source_lhs, atom_true(&equation_scratch));
        space_add(&equation_space, source_equation);
        assert(space_single_linear_equation(&equation_space, source_head) ==
               space_get_at64(&equation_space, 0));
        assert(space_equations_may_match_known_head(&equation_space,
                                                    source_head));
        assert(!space_equations_may_match_known_head(&equation_space,
                                                     colliding_head));
        assert(!space_equations_may_match_known_head(&equation_space,
                                                     absent_colliding_head));

        space_begin_secondary_index_deferral(&equation_space);
        Atom *second_source_equation =
            atom_expr3(&equation_scratch,
                       atom_symbol_id(&equation_scratch,
                                      g_builtin_syms.equals),
                       source_lhs, atom_false(&equation_scratch));
        space_add(&equation_space, second_source_equation);
        assert(space_single_linear_equation(&equation_space, source_head) ==
               NULL);
        space_end_secondary_index_deferral(&equation_space);
        assert(space_remove(&equation_space, second_source_equation));
        assert(space_single_linear_equation(&equation_space, source_head) ==
               space_get_at64(&equation_space, 0));

        Atom *wildcard_equation =
            atom_expr3(&equation_scratch,
                       atom_symbol_id(&equation_scratch,
                                      g_builtin_syms.equals),
                       atom_var_with_id(&equation_scratch, "wildcard", 1),
                       atom_true(&equation_scratch));
        space_add(&equation_space, wildcard_equation);
        assert(space_single_linear_equation(&equation_space, source_head) ==
               NULL);
        assert(space_remove(&equation_space, wildcard_equation));
        assert(space_single_linear_equation(&equation_space, source_head) ==
               space_get_at64(&equation_space, 0));

        Atom *colliding_lhs_elems[1] = {
            atom_symbol_id(&equation_scratch, colliding_head),
        };
        Atom *colliding_lhs =
            atom_expr(&equation_scratch, colliding_lhs_elems, 1);
        space_add(&equation_space,
                  atom_expr3(&equation_scratch,
                             atom_symbol_id(&equation_scratch,
                                            g_builtin_syms.equals),
                             colliding_lhs, atom_true(&equation_scratch)));
        assert(space_equations_may_match_known_head(&equation_space,
                                                    source_head));
        assert(space_equations_may_match_known_head(&equation_space,
                                                    colliding_head));
        assert(!space_equations_may_match_known_head(&equation_space,
                                                     absent_colliding_head));

        space_add(&equation_space, wildcard_equation);
        assert(space_equations_may_match_known_head(&equation_space,
                                                    absent_colliding_head));

        Atom *second_source_after_wildcard =
            atom_expr3(&equation_scratch,
                       atom_symbol_id(&equation_scratch,
                                      g_builtin_syms.equals),
                       source_lhs, atom_false(&equation_scratch));
        space_add(&equation_space, second_source_after_wildcard);

        SpaceEquationCursor source_cursor;
        SpaceEquationOccurrenceId cursor_id;
        assert(space_equation_cursor_init(
            &equation_space, source_head, &source_cursor));
        assert(space_equation_cursor_next(&source_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_ITEM);
        assert(cursor_id.logical_index == 0u);
        assert(space_equation_cursor_next(&source_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_ITEM);
        assert(cursor_id.logical_index == 2u);
        assert(space_equation_cursor_next(&source_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_ITEM);
        assert(cursor_id.logical_index == 3u);
        assert(space_equation_cursor_next(&source_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_END);

        SpaceEquationCursor collision_cursor;
        assert(space_equation_cursor_init(
            &equation_space, colliding_head, &collision_cursor));
        assert(space_equation_cursor_next(&collision_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_ITEM);
        assert(cursor_id.logical_index == 1u);
        assert(space_equation_cursor_next(&collision_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_ITEM);
        assert(cursor_id.logical_index == 2u);
        assert(space_equation_cursor_next(&collision_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_END);

        SpaceEquationCursor invalidated_cursor;
        assert(space_equation_cursor_init(
            &equation_space, source_head, &invalidated_cursor));
        space_add(&equation_space,
                  atom_symbol(&equation_scratch, "cursor-mutation"));
        assert(space_equation_cursor_next(
                   &invalidated_cursor, &cursor_id) ==
               SPACE_EQUATION_CURSOR_INVALIDATED);

        Space overlay_cursor_space;
        space_init_overlay(&overlay_cursor_space, &equation_space);
        space_add(&overlay_cursor_space, source_equation);
        SpaceEquationCursor overlay_cursor;
        assert(space_equation_cursor_init(
            &overlay_cursor_space, source_head, &overlay_cursor));
        CettaIndex previous_index = 0u;
        uint32_t overlay_matches = 0u;
        SpaceEquationCursorStep overlay_step;
        while ((overlay_step = space_equation_cursor_next(
                    &overlay_cursor, &cursor_id)) ==
               SPACE_EQUATION_CURSOR_ITEM) {
            if (overlay_matches > 0u)
                assert(cursor_id.logical_index > previous_index);
            previous_index = cursor_id.logical_index;
            overlay_matches++;
        }
        assert(overlay_step == SPACE_EQUATION_CURSOR_END);
        assert(overlay_matches == 4u);
        space_free(&overlay_cursor_space);

        Space occurrence_space;
        space_init_with_universe(&occurrence_space, &equation_universe);
        space_add(&occurrence_space, source_equation);
        space_add(&occurrence_space, source_equation);
        assert(space_length64(&occurrence_space) == 2);
        SpaceReadToken occurrence_read = space_read_token(&occurrence_space);
        uint64_t occurrence_instance =
            space_instance_id(&occurrence_space);
        assert(space_read_token_is_current(occurrence_read));
        SpaceEquationOccurrence first_occurrence;
        SpaceEquationOccurrence second_occurrence;
        SpaceEquationOccurrenceId first_id = {
            .read = occurrence_read,
            .logical_index = 0,
        };
        SpaceEquationOccurrenceId second_id = {
            .read = occurrence_read,
            .logical_index = 1,
        };
        assert(space_equation_occurrence_resolve(first_id,
                                                 &first_occurrence));
        assert(space_equation_occurrence_resolve(second_id,
                                                 &second_occurrence));
        assert(first_occurrence.id.logical_index !=
               second_occurrence.id.logical_index);
        assert(atom_eq(first_occurrence.equation,
                       second_occurrence.equation));
        assert(atom_eq(first_occurrence.lhs, source_lhs));
        assert(first_occurrence.rhs->kind == ATOM_GROUNDED);
        assert(first_occurrence.rhs->ground.gkind == GV_BOOL);
        assert(first_occurrence.rhs->ground.bval);

        space_add(&occurrence_space,
                  atom_symbol(&equation_scratch, "not-an-equation"));
        assert(!space_read_token_is_current(occurrence_read));
        assert(!space_equation_occurrence_resolve(first_id,
                                                  &first_occurrence));
        assert(first_occurrence.equation == NULL);
        SpaceReadToken current_read = space_read_token(&occurrence_space);
        SpaceEquationOccurrenceId non_equation_id = {
            .read = current_read,
            .logical_index = 2,
        };
        assert(!space_equation_occurrence_resolve(non_equation_id,
                                                  &first_occurrence));
        SpaceEquationOccurrenceId out_of_range_id = {
            .read = current_read,
            .logical_index = 3,
        };
        assert(!space_equation_occurrence_resolve(out_of_range_id,
                                                  &first_occurrence));
        space_free(&occurrence_space);
        space_init_with_universe(&occurrence_space, &equation_universe);
        assert(space_instance_id(&occurrence_space) != occurrence_instance);
        assert(!space_read_token_is_current(current_read));
        space_free(&occurrence_space);

        {
            Space prepared_space;
            space_init_with_universe(&prepared_space, &equation_universe);
            SymbolId prepared_head =
                symbol_intern_cstr(g_symbols, "prepared-equation-head");
            SymbolId result_head =
                symbol_intern_cstr(g_symbols, "prepared-equation-result");
            Atom *x = atom_var_with_id(&equation_scratch, "x", 7001u);
            Atom *y = atom_var_with_id(&equation_scratch, "y", 7002u);
            Atom *lhs_elems[3] = {
                atom_symbol_id(&equation_scratch, prepared_head), x, y,
            };
            Atom *rhs_elems[4] = {
                atom_symbol_id(&equation_scratch, result_head), x, x, y,
            };
            Atom *prepared_equation = atom_expr3(
                &equation_scratch,
                atom_symbol_id(&equation_scratch, g_builtin_syms.equals),
                atom_expr(&equation_scratch, lhs_elems, 3u),
                atom_expr(&equation_scratch, rhs_elems, 4u));
            space_add(&prepared_space, prepared_equation);

            SpacePreparedEquation plan;
            assert(space_prepare_single_equation(
                &prepared_space, prepared_head, &plan));
            assert(plan.arity == 2u);
            assert(cetta_gslt_prepared_equation_plan_admitted(
                plan.evidence));

            /* Aliased arguments and a register used twice on the RHS retain
             * one immutable value identity; no ownership is duplicated. */
            Atom *shared = atom_int(&equation_scratch, 73);
            Atom *call_elems[3] = {
                atom_symbol_id(&equation_scratch, prepared_head),
                shared, shared,
            };
            Atom *instantiated =
                space_prepared_equation_instantiate_ground(
                    &plan,
                    atom_expr(&equation_scratch, call_elems, 3u),
                    &equation_scratch);
            assert(instantiated && instantiated->kind == ATOM_EXPR);
            assert(instantiated->expr.len == 4u);
            assert(instantiated->expr.elems[1] == shared);
            assert(instantiated->expr.elems[2] == shared);
            assert(instantiated->expr.elems[3] == shared);

            /* A non-ground call stays on the general matcher path. */
            Atom *variable_call_elems[3] = {
                atom_symbol_id(&equation_scratch, prepared_head),
                atom_var_with_id(&equation_scratch, "query", 8001u),
                shared,
            };
            assert(!space_prepared_equation_instantiate_ground(
                &plan,
                atom_expr(&equation_scratch, variable_call_elems, 3u),
                &equation_scratch));

            /* The read token is part of admission: adding a second equation
             * invalidates the compiled singleton before its next use. */
            Atom *second_equation = atom_expr3(
                &equation_scratch,
                atom_symbol_id(&equation_scratch, g_builtin_syms.equals),
                atom_expr(&equation_scratch, lhs_elems, 3u),
                atom_false(&equation_scratch));
            space_add(&prepared_space, second_equation);
            assert(!space_prepared_equation_instantiate_ground(
                &plan,
                atom_expr(&equation_scratch, call_elems, 3u),
                &equation_scratch));
            assert(!space_prepare_single_equation(
                &prepared_space, prepared_head, &plan));
            space_free(&prepared_space);
        }

        {
            /* The generated register instruction uses simultaneous slot
             * assignment: aliases in the call and repeated RHS register use
             * cannot observe an already-overwritten accumulator. */
            Space register_space;
            space_init_with_universe(&register_space, &equation_universe);
            SymbolId loop_head =
                symbol_intern_cstr(g_symbols, "prepared-register-loop");
            Atom *n = atom_var_with_id(&equation_scratch, "n", 7101u);
            Atom *a = atom_var_with_id(&equation_scratch, "a", 7102u);
            Atom *b = atom_var_with_id(&equation_scratch, "b", 7103u);
            Atom *zero = atom_int(&equation_scratch, 0);
            Atom *one = atom_int(&equation_scratch, 1);
            Atom *guard_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_eq),
                n, zero,
            };
            Atom *minus_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_minus),
                n, one,
            };
            Atom *plus_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_plus),
                a, b,
            };
            Atom *tail_elems[4] = {
                atom_symbol_id(&equation_scratch, loop_head),
                atom_expr(&equation_scratch, minus_elems, 3u),
                b,
                atom_expr(&equation_scratch, plus_elems, 3u),
            };
            Atom *body_elems[4] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.if_text),
                atom_expr(&equation_scratch, guard_elems, 3u),
                a,
                atom_expr(&equation_scratch, tail_elems, 4u),
            };
            Atom *lhs_elems[4] = {
                atom_symbol_id(&equation_scratch, loop_head), n, a, b,
            };
            Atom *equation = atom_expr3(
                &equation_scratch,
                atom_symbol_id(&equation_scratch, g_builtin_syms.equals),
                atom_expr(&equation_scratch, lhs_elems, 4u),
                atom_expr(&equation_scratch, body_elems, 4u));
            space_add(&register_space, equation);

            SpacePreparedEquation plan;
            assert(space_prepare_single_equation(
                &register_space, loop_head, &plan));
            uint32_t runtime_evidence =
                plan.evidence | CETTA_GSLT_EVIDENCE_GROUND_CALL;
            assert(cetta_gslt_prepared_register_step_admitted(
                runtime_evidence));

            Atom *call_elems[4] = {
                atom_symbol_id(&equation_scratch, loop_head),
                atom_int(&equation_scratch, 10), zero, one,
            };
            Atom *result = NULL;
            assert(space_prepared_equation_run_register_loop(
                       &plan,
                       atom_expr(&equation_scratch, call_elems, 4u),
                       &equation_scratch, 64u, &result) ==
                   SPACE_PREPARED_REGISTER_VALUE);
            assert(result && result->kind == ATOM_GROUNDED &&
                   result->ground.gkind == GV_INT &&
                   result->ground.ival == 55);

            Atom *shared = atom_int(&equation_scratch, 7);
            Atom *alias_elems[4] = {
                atom_symbol_id(&equation_scratch, loop_head),
                one, shared, shared,
            };
            result = NULL;
            assert(space_prepared_equation_run_register_loop(
                       &plan,
                       atom_expr(&equation_scratch, alias_elems, 4u),
                       &equation_scratch, 8u, &result) ==
                   SPACE_PREPARED_REGISTER_VALUE);
            assert(result && result->kind == ATOM_GROUNDED &&
                   result->ground.gkind == GV_INT &&
                   result->ground.ival == 7);

            Atom *open_elems[4] = {
                atom_symbol_id(&equation_scratch, loop_head),
                atom_symbol(&equation_scratch, "not-an-integer"),
                zero, one,
            };
            result = NULL;
            assert(space_prepared_equation_run_register_loop(
                       &plan,
                       atom_expr(&equation_scratch, open_elems, 4u),
                       &equation_scratch, 8u, &result) ==
                   SPACE_PREPARED_REGISTER_NOT_APPLICABLE);

            result = NULL;
            assert(space_prepared_equation_run_register_loop(
                       &plan,
                       atom_expr(&equation_scratch, call_elems, 4u),
                       &equation_scratch, 1u, &result) ==
                   SPACE_PREPARED_REGISTER_TAIL_CALL);
            assert(result && result->kind == ATOM_EXPR &&
                   result->expr.len == 4u);
            space_add(&register_space, atom_expr3(
                &equation_scratch,
                atom_symbol_id(&equation_scratch, g_builtin_syms.equals),
                atom_expr(&equation_scratch, lhs_elems, 4u),
                atom_false(&equation_scratch)));
            Atom *stale_result = NULL;
            assert(space_prepared_equation_run_register_loop(
                       &plan, result, &equation_scratch, 8u,
                       &stale_result) ==
                   SPACE_PREPARED_REGISTER_NOT_APPLICABLE);
            assert(stale_result == NULL);
            space_free(&register_space);
        }

        {
            /* A generated pure-register program with two recursive calls is
             * evaluated by explicit machine frames, not the native C stack. */
            Space recursive_space;
            space_init_with_universe(&recursive_space, &equation_universe);
            SymbolId recursive_head =
                symbol_intern_cstr(g_symbols, "prepared-register-recursive-fib");
            Atom *n = atom_var_with_id(
                &equation_scratch, "recursive-n", 7201u);
            Atom *zero = atom_int(&equation_scratch, 0);
            Atom *one = atom_int(&equation_scratch, 1);
            Atom *two = atom_int(&equation_scratch, 2);
            Atom *guard_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_lt),
                n, two,
            };
            Atom *minus_one_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_minus),
                n, one,
            };
            Atom *minus_two_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_minus),
                n, two,
            };
            Atom *left_call_elems[2] = {
                atom_symbol_id(&equation_scratch, recursive_head),
                atom_expr(&equation_scratch, minus_one_elems, 3u),
            };
            Atom *right_call_elems[2] = {
                atom_symbol_id(&equation_scratch, recursive_head),
                atom_expr(&equation_scratch, minus_two_elems, 3u),
            };
            Atom *sum_elems[3] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.op_plus),
                atom_expr(&equation_scratch, left_call_elems, 2u),
                atom_expr(&equation_scratch, right_call_elems, 2u),
            };
            Atom *body_elems[4] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.if_text),
                atom_expr(&equation_scratch, guard_elems, 3u),
                n,
                atom_expr(&equation_scratch, sum_elems, 3u),
            };
            Atom *lhs_elems[2] = {
                atom_symbol_id(&equation_scratch, recursive_head), n,
            };
            Atom *equation = atom_expr3(
                &equation_scratch,
                atom_symbol_id(&equation_scratch, g_builtin_syms.equals),
                atom_expr(&equation_scratch, lhs_elems, 2u),
                atom_expr(&equation_scratch, body_elems, 4u));
            space_add(&recursive_space, equation);

            SpacePreparedEquation plan;
            assert(space_prepare_single_equation(
                &recursive_space, recursive_head, &plan));
            assert(cetta_gslt_prepared_register_recursion_admitted(
                plan.evidence | CETTA_GSLT_EVIDENCE_GROUND_CALL));

            Atom *call_elems[2] = {
                atom_symbol_id(&equation_scratch, recursive_head),
                atom_int(&equation_scratch, 10),
            };
            Atom *call = atom_expr(
                &equation_scratch, call_elems, 2u);
            Atom *result = NULL;
            assert(space_prepared_equation_run_register_recursion(
                       &plan, call, &equation_scratch, 512u, &result) ==
                   SPACE_PREPARED_REGISTER_VALUE);
            assert(result && result->kind == ATOM_GROUNDED &&
                   result->ground.gkind == GV_INT &&
                   result->ground.ival == 55);

            Atom *noninteger_elems[2] = {
                atom_symbol_id(&equation_scratch, recursive_head),
                atom_symbol(&equation_scratch, "not-an-integer"),
            };
            result = NULL;
            assert(space_prepared_equation_run_register_recursion(
                       &plan,
                       atom_expr(&equation_scratch, noninteger_elems, 2u),
                       &equation_scratch, 512u, &result) ==
                   SPACE_PREPARED_REGISTER_NOT_APPLICABLE);
            assert(result == NULL);

            result = NULL;
            assert(space_prepared_equation_run_register_recursion(
                       &plan, call, &equation_scratch, 1u, &result) ==
                   SPACE_PREPARED_REGISTER_NOT_APPLICABLE);
            assert(result == NULL);

            space_add(&recursive_space, atom_expr3(
                &equation_scratch,
                atom_symbol_id(&equation_scratch, g_builtin_syms.equals),
                atom_expr(&equation_scratch, lhs_elems, 2u), zero));
            result = NULL;
            assert(space_prepared_equation_run_register_recursion(
                       &plan, call, &equation_scratch, 512u, &result) ==
                   SPACE_PREPARED_REGISTER_NOT_APPLICABLE);
            assert(result == NULL);
            space_free(&recursive_space);
        }

        {
            /* An existential RHS variable is not a positional register and
             * must never be captured by the direct continuation. */
            Space existential_space;
            space_init_with_universe(&existential_space,
                                     &equation_universe);
            SymbolId head =
                symbol_intern_cstr(g_symbols, "prepared-existential-head");
            Atom *lhs_var =
                atom_var_with_id(&equation_scratch, "lhs", 9001u);
            Atom *rhs_var =
                atom_var_with_id(&equation_scratch, "rhs", 9002u);
            Atom *lhs_elems[2] = {
                atom_symbol_id(&equation_scratch, head), lhs_var,
            };
            space_add(
                &existential_space,
                atom_expr3(
                    &equation_scratch,
                    atom_symbol_id(&equation_scratch,
                                   g_builtin_syms.equals),
                    atom_expr(&equation_scratch, lhs_elems, 2u), rhs_var));
            SpacePreparedEquation rejected;
            assert(!space_prepare_single_equation(
                &existential_space, head, &rejected));
            space_free(&existential_space);
        }

        {
            /* User-defined relational effects are the least fixed point over
             * equation dependencies, not merely a scan for builtin heads in
             * the outer template. */
            Space effect_space;
            space_init_with_universe(&effect_space, &equation_universe);
            SymbolId leaf =
                symbol_intern_cstr(g_symbols, "effect-lfp-leaf");
            SymbolId middle =
                symbol_intern_cstr(g_symbols, "effect-lfp-middle");
            SymbolId root =
                symbol_intern_cstr(g_symbols, "effect-lfp-root");
            SymbolId pure_left =
                symbol_intern_cstr(g_symbols, "effect-lfp-pure-left");
            SymbolId pure_right =
                symbol_intern_cstr(g_symbols, "effect-lfp-pure-right");
            SymbolId higher =
                symbol_intern_cstr(g_symbols, "effect-lfp-higher");
            SymbolId mutable_leaf =
                symbol_intern_cstr(g_symbols, "effect-lfp-mutable-leaf");
            SymbolId mutable_root =
                symbol_intern_cstr(g_symbols, "effect-lfp-mutable-root");
            SymbolId inert =
                symbol_intern_cstr(g_symbols, "effect-lfp-inert-value");
            Atom *x = atom_var_with_id(
                &equation_scratch, "effect-x", 9101u);
            Atom *function = atom_var_with_id(
                &equation_scratch, "effect-function", 9102u);

#define EFFECT_CALL1(head_id, argument) \
    atom_expr2(&equation_scratch, \
               atom_symbol_id(&equation_scratch, (head_id)), (argument))
#define EFFECT_EQUATION1(head_id, argument, body) \
    atom_expr3(&equation_scratch, \
               atom_symbol_id(&equation_scratch, g_builtin_syms.equals), \
               EFFECT_CALL1((head_id), (argument)), (body))

            Atom *match_body_elems[4] = {
                atom_symbol_id(&equation_scratch, g_builtin_syms.match),
                atom_symbol_id(&equation_scratch, g_builtin_syms.self),
                x,
                x,
            };
            Atom *match_body = atom_expr(
                &equation_scratch, match_body_elems, 4u);
            space_add(&effect_space,
                      EFFECT_EQUATION1(leaf, x, match_body));
            space_add(&effect_space,
                      EFFECT_EQUATION1(middle, x,
                                       EFFECT_CALL1(leaf, x)));
            space_add(&effect_space,
                      EFFECT_EQUATION1(root, x,
                                       EFFECT_CALL1(middle, x)));
            assert(space_query_effect_for_head(
                       &effect_space, leaf, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);
            assert(space_query_effect_for_head(
                       &effect_space, middle, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);
            assert(space_query_effect_for_head(
                       &effect_space, root, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);

            /* A recursive component with no relational seed remains the
             * least element rather than becoming effectful merely for being
             * recursive. */
            space_add(&effect_space,
                      EFFECT_EQUATION1(pure_left, x,
                                       EFFECT_CALL1(pure_right, x)));
            space_add(&effect_space,
                      EFFECT_EQUATION1(pure_right, x,
                                       EFFECT_CALL1(pure_left, x)));
            assert(space_query_effect_for_head(
                       &effect_space, pure_left, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_PURE);
            assert(space_query_effect_for_head(
                       &effect_space, pure_right, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_PURE);

            /* Variable/higher-order heads fail closed. */
            Atom *higher_body_elems[2] = {function, x};
            Atom *higher_body = atom_expr(
                &equation_scratch, higher_body_elems, 2u);
            space_add(&effect_space,
                      EFFECT_EQUATION1(higher, function, higher_body));
            assert(space_query_effect_for_head(
                       &effect_space, higher, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD);

            /* A symbol with no equation is inert at this exact revision. */
            assert(space_query_effect_for_head(
                       &effect_space, inert, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL);

            /* Mutation must invalidate the transitive cached answer. */
            space_add(&effect_space,
                      EFFECT_EQUATION1(mutable_leaf, x,
                                       EFFECT_CALL1(inert, x)));
            space_add(&effect_space,
                      EFFECT_EQUATION1(mutable_root, x,
                                       EFFECT_CALL1(mutable_leaf, x)));
            assert(space_query_effect_for_head(
                       &effect_space, mutable_root, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_PURE);
            space_add(&effect_space,
                      EFFECT_EQUATION1(mutable_leaf, x, match_body));
            assert(space_query_effect_for_head(
                       &effect_space, mutable_root, NULL) ==
                   CETTA_GSLT_QUERY_EFFECT_RELATIONAL_QUERY);

#undef EFFECT_EQUATION1
#undef EFFECT_CALL1
            space_free(&effect_space);
        }

        space_free(&equation_space);
        term_universe_free(&equation_universe);
        arena_free(&equation_scratch);
        arena_free(&equation_persistent);
    }

    arena_init(&persistent);
    arena_init(&scratch_a);
    arena_init(&scratch_b);
    arena_init(&scratch_c);
    arena_init(&scratch_d);
    arena_init(&scratch_e);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&left, &universe);
    space_init_with_universe(&right, &universe);

    SymbolId pair_sym = symbol_intern_cstr(g_symbols, "pair");
    SymbolId box_sym = symbol_intern_cstr(g_symbols, "box");

    {
        Arena alpha_persistent;
        Arena alpha_scratch_a;
        Arena alpha_scratch_b;
        Arena alpha_scratch_c;
        TermUniverse alpha_universe;
        Space alpha_space;
        SymbolId rel_sym = symbol_intern_cstr(g_symbols, "alpha-key-rel");

        arena_init(&alpha_persistent);
        arena_init(&alpha_scratch_a);
        arena_init(&alpha_scratch_b);
        arena_init(&alpha_scratch_c);
        term_universe_init(&alpha_universe);
        term_universe_set_persistent_arena(&alpha_universe,
                                           &alpha_persistent);
        Atom *stored_elems[4] = {
            atom_symbol_id(&alpha_scratch_a, rel_sym),
            atom_var_with_id(&alpha_scratch_a, "x", 101u),
            atom_var_with_id(&alpha_scratch_a, "y", 102u),
            atom_var_with_id(&alpha_scratch_a, "x", 101u),
        };
        Atom *variant_elems[4] = {
            atom_symbol_id(&alpha_scratch_b, rel_sym),
            atom_var_with_id(&alpha_scratch_b, "a",
                             var_epoch_id(201u, 17u)),
            atom_var_with_id(&alpha_scratch_b, "b",
                             var_epoch_id(202u, 17u)),
            atom_var_with_id(&alpha_scratch_b, "a",
                             var_epoch_id(201u, 17u)),
        };
        Atom *different_partition_elems[4] = {
            atom_symbol_id(&alpha_scratch_c, rel_sym),
            atom_var_with_id(&alpha_scratch_c, "a",
                             var_epoch_id(301u, 29u)),
            atom_var_with_id(&alpha_scratch_c, "b",
                             var_epoch_id(302u, 29u)),
            atom_var_with_id(&alpha_scratch_c, "b",
                             var_epoch_id(302u, 29u)),
        };
        Atom *stored = atom_expr(&alpha_scratch_a, stored_elems, 4u);
        Atom *variant = atom_expr(&alpha_scratch_b, variant_elems, 4u);
        Atom *different_partition =
            atom_expr(&alpha_scratch_c, different_partition_elems, 4u);
        bool applicable = false;

        space_init_with_universe(&alpha_space, &alpha_universe);
        space_add(&alpha_space, stored);
        assert(space_contains_canonical(&alpha_space, variant, &applicable));
        assert(applicable);
        assert(!space_contains_canonical(&alpha_space, different_partition,
                                         &applicable));
        assert(applicable);
        assert(!alpha_space.native.id_present_dirty);
        space_begin_secondary_index_deferral(&alpha_space);
        assert(!alpha_space.native.id_present_dirty);
        space_add(&alpha_space, different_partition);
        assert(space_contains_canonical(&alpha_space, different_partition,
                                        &applicable));
        assert(applicable);
        space_end_secondary_index_deferral(&alpha_space);
        assert(space_remove(&alpha_space, variant));
        assert(!space_contains_canonical(&alpha_space, stored, &applicable));
        assert(applicable);
        assert(space_contains_canonical(&alpha_space, different_partition,
                                        &applicable));
        assert(applicable);

        Space alpha_overlay;
        space_init_overlay(&alpha_overlay, &alpha_space);
        assert(space_contains_canonical(&alpha_overlay,
                                        different_partition, &applicable));
        assert(applicable);
        assert(space_remove(&alpha_overlay, different_partition));
        assert(!space_contains_canonical(&alpha_overlay,
                                         different_partition, &applicable));
        assert(applicable);
        assert(space_contains_canonical(&alpha_space,
                                        different_partition, &applicable));
        space_add(&alpha_overlay, different_partition);
        assert(space_contains_canonical(&alpha_overlay,
                                        different_partition, &applicable));
        space_free(&alpha_overlay);

        /* A hidden base row creates a gap between overlay-logical and base-raw
         * indices.  Unique-alpha removal must retain which index domain the
         * match came from; otherwise raw index 1 is reinterpreted as visible
         * index 1 and the following row is removed instead. */
        Space alpha_hole_base;
        Space alpha_hole_overlay;
        Atom *hole = atom_symbol(
            &alpha_scratch_a, "alpha-overlay-hidden-prefix");
        space_init_with_universe(&alpha_hole_base, &alpha_universe);
        space_add(&alpha_hole_base, hole);
        space_add(&alpha_hole_base, stored);
        space_add(&alpha_hole_base, different_partition);
        space_init_overlay(&alpha_hole_overlay, &alpha_hole_base);
        assert(space_remove(&alpha_hole_overlay, hole));
        assert(space_remove(&alpha_hole_overlay, variant));
        assert(!space_contains_canonical(&alpha_hole_overlay, stored,
                                         &applicable));
        assert(applicable);
        assert(space_contains_canonical(&alpha_hole_overlay,
                                        different_partition, &applicable));
        assert(applicable);
        space_free(&alpha_hole_overlay);
        space_free(&alpha_hole_base);

        /* Mutable grounded payloads have pointer-backed, not structural,
           TermUniverse identity.  The canonical accelerator must decline this
           fragment so the alpha-scan oracle remains authoritative. */
        Space mutable_payload;
        Space unstable_alpha_space;
        space_init_with_universe(&mutable_payload, &alpha_universe);
        space_init_with_universe(&unstable_alpha_space, &alpha_universe);
        Atom *unstable_stored =
            atom_expr3(&alpha_scratch_a,
                       atom_symbol_id(&alpha_scratch_a, rel_sym),
                       atom_var_with_id(&alpha_scratch_a, "u", 401u),
                       atom_space(&alpha_scratch_a, &mutable_payload));
        Atom *unstable_variant =
            atom_expr3(&alpha_scratch_b,
                       atom_symbol_id(&alpha_scratch_b, rel_sym),
                       atom_var_with_id(&alpha_scratch_b, "v",
                                        var_epoch_id(501u, 31u)),
                       atom_space(&alpha_scratch_b, &mutable_payload));
        assert(atom_alpha_eq(unstable_stored, unstable_variant));
        space_add(&unstable_alpha_space, unstable_stored);
        applicable = true;
        assert(!space_contains_canonical(&unstable_alpha_space,
                                         unstable_variant, &applicable));
        assert(!applicable);
        assert(space_remove(&unstable_alpha_space, unstable_variant));
        assert(space_length64(&unstable_alpha_space) == 0u);
        space_free(&unstable_alpha_space);
        space_free(&mutable_payload);

        space_free(&alpha_space);
        term_universe_free(&alpha_universe);
        arena_free(&alpha_scratch_c);
        arena_free(&alpha_scratch_b);
        arena_free(&alpha_scratch_a);
        arena_free(&alpha_persistent);
    }

    Atom *pair_a = make_pair(&scratch_a, pair_sym, 1, 2);
    Atom *pair_b = make_pair(&scratch_b, pair_sym, 1, 2);
    space_add(&left, pair_a);
    assert(universe.len == 4);
    assert(left.atom_ids != NULL);
    assert(test_space_native_atom_id_at(&left, 0) != CETTA_ATOM_ID_NONE);
    AtomId pair_id = test_space_native_atom_id_at(&left, 0);
    assert(tu_hdr(&universe, pair_id) != NULL);
    assert(tu_kind(&universe, pair_id) == ATOM_EXPR);
    assert(tu_arity(&universe, pair_id) == 3);
    assert(tu_head_sym(&universe, pair_id) == pair_sym);
    AtomId pair_head_id = tu_child(&universe, pair_id, 0);
    AtomId pair_lhs_id = tu_child(&universe, pair_id, 1);
    AtomId pair_rhs_id = tu_child(&universe, pair_id, 2);
    assert(tu_kind(&universe, pair_head_id) == ATOM_SYMBOL);
    assert(tu_sym(&universe, pair_head_id) == pair_sym);
    assert(term_universe_get_atom(&universe, pair_lhs_id)->ground.ival == 1);
    assert(term_universe_get_atom(&universe, pair_rhs_id)->ground.ival == 2);
    assert(space_length64(&left) == 1);
    assert(space_match_backend_logical_len64(&left) == 1);
    assert(space_get_atom_id_at64(&left, 0) == pair_id);
    assert(space_match_backend_get_atom_id_at64(&left, 0) == pair_id);
    assert(space_get_at(&left, 0) ==
           term_universe_get_atom(&universe, pair_id));
    assert(space_get_at64(&left, 0) == space_get_at(&left, 0));
    assert(space_match_backend_get_at64(&left, 0) == space_get_at(&left, 0));
    assert(space_get_at64(&left, (CettaIndex)UINT32_MAX + 1u) == NULL);
    assert(space_match_backend_get_at64(&left, (uint64_t)UINT32_MAX + 1u) == NULL);

    reset_test_counters();
    left.exact_idx_dirty = true;
    assert_exact_match_one(&left, pair_a, 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    assert(term_universe_lookup_atom_id(&universe, pair_b) == pair_id);
    reset_test_counters();
    left.exact_idx_dirty = true;
    assert_exact_match_one(&left, pair_b, 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *absent_pair = make_pair(&scratch_d, pair_sym, 1, 3);
    assert(term_universe_lookup_atom_id(&universe, absent_pair) ==
           CETTA_ATOM_ID_NONE);
    reset_test_counters();
    left.exact_idx_dirty = true;
    assert_exact_match_none(&left, absent_pair);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT) == 0);

    Atom *var_pair = make_pair_var(&scratch_e, pair_sym);
    reset_test_counters();
    assert_exact_match_none(&left, var_pair);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP) == 0);

    space_add(&right, pair_b);
    assert(universe.len == 4);
    assert(test_space_native_atom_id_at(&right, 0) ==
           test_space_native_atom_id_at(&left, 0));
    assert(space_get_at(&right, 0) == space_get_at(&left, 0));

    Atom *boxed_space = make_boxed_space(&scratch_c, box_sym, &left);
    reset_test_counters();
    assert_exact_match_none(&left, boxed_space);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP) == 0);

    Atom *stored_boxed = space_store_atom(&left, &persistent, boxed_space);
    AtomId boxed_id = term_universe_store_atom_id(&universe, NULL, stored_boxed);
    assert(boxed_id != CETTA_ATOM_ID_NONE);
    assert(universe.len == 5);
    assert(tu_hdr(&universe, boxed_id) == NULL);
    space_add(&left, stored_boxed);
    assert(universe.len == 5);
    assert(test_space_native_atom_id_at(&left, 1) == boxed_id);
    assert(space_get_at(&left, 1) == stored_boxed);
    assert(space_length64(&left) == 2);
    assert(space_get_atom_id_at64(&left, 1) == boxed_id);

    Space *clone = space_heap_clone_shallow(&left);
    assert(clone != NULL);
    assert(clone->universe == &universe);
    assert(clone->atom_ids != NULL);
    assert(clone->len == left.len);
    assert(test_space_native_atom_id_at(clone, 0) ==
           test_space_native_atom_id_at(&left, 0));
    assert(test_space_native_atom_id_at(clone, 1) ==
           test_space_native_atom_id_at(&left, 1));
    assert(space_get_at(clone, 0) == space_get_at(&left, 0));
    assert(space_get_at(clone, 1) == space_get_at(&left, 1));
    assert(space_truncate64(clone, 1));
    assert(space_length64(clone) == 1);
    assert(space_get_atom_id_at64(clone, 0) ==
           test_space_native_atom_id_at(&left, 0));
    assert(!space_truncate64(clone, (CettaCount)UINT32_MAX + 1u));
    assert(!space_match_backend_truncate_direct64(clone, (uint64_t)UINT32_MAX + 1u));

    space_free(clone);
    free(clone);

    {
        Arena wide_persistent;
        Arena wide_scratch;
        TermUniverse wide_universe;
        Space wide_space;

        arena_init(&wide_persistent);
        arena_init(&wide_scratch);
        assert(term_universe_init_with_store_format(
            &wide_universe, TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1));
        term_universe_set_persistent_arena(&wide_universe, &wide_persistent);
        space_init_with_universe(&wide_space, &wide_universe);

        Atom *wide_pair = make_pair(&wide_scratch, pair_sym, 7, 8);
        AtomId wide_pair_id =
            term_universe_store_atom_id(&wide_universe, NULL, wide_pair);
        assert(wide_pair_id != CETTA_ATOM_ID_NONE);
        space_add_atom_id(&wide_space, wide_pair_id);
        assert(space_contains_atom_id(&wide_space, wide_pair_id));
        assert(space_get_atom_id_at64(&wide_space, 0) == wide_pair_id);

        Space *wide_clone = space_heap_clone_shallow(&wide_space);
        assert(wide_clone != NULL);
        assert(space_contains_atom_id(wide_clone, wide_pair_id));
        assert(space_remove_atom_id(wide_clone, wide_pair_id));
        assert(space_length64(wide_clone) == 0);
        space_free(wide_clone);
        free(wide_clone);

        space_free(&wide_space);
        term_universe_free(&wide_universe);
        arena_free(&wide_scratch);
        arena_free(&wide_persistent);
    }

    {
        enum { OVERLAY_BASE_COUNT = 512, OVERLAY_REMOVE_COUNT = 171 };
        Space overlay_base;
        Space overlay;
        AtomId ids[OVERLAY_BASE_COUNT];
        bool removed[OVERLAY_BASE_COUNT] = {false};

        space_init_with_universe(&overlay_base, &universe);
        for (uint32_t i = 0; i < OVERLAY_BASE_COUNT; i++) {
            Atom *value = atom_int(&scratch_d, 10000 + (int64_t)i);
            ids[i] = term_universe_store_atom_id(&universe, NULL, value);
            assert(ids[i] != CETTA_ATOM_ID_NONE);
            space_add_atom_id(&overlay_base, ids[i]);
        }
        space_init_overlay(&overlay, &overlay_base);
        uint64_t epoch_before = space_global_mutation_epoch();
        for (uint32_t i = 0; i < OVERLAY_REMOVE_COUNT; i++) {
            uint32_t raw = (i * 73u) % OVERLAY_BASE_COUNT;
            removed[raw] = true;
            assert(space_remove_atom_id(&overlay, ids[raw]));
        }
        assert(space_global_mutation_epoch() >=
               epoch_before + OVERLAY_REMOVE_COUNT);
        assert(space_length64(&overlay) ==
               OVERLAY_BASE_COUNT - OVERLAY_REMOVE_COUNT);
        assert(overlay.overlay_removed_base_len <= OVERLAY_REMOVE_COUNT);
        assert(overlay.overlay_base_visible_len -
                   overlay.overlay_removed_base_len ==
               space_length64(&overlay));
        for (CettaIndex i = 1; i < overlay.overlay_removed_base_len; i++) {
            assert(overlay.overlay_removed_base_indices[i - 1u] <
                   overlay.overlay_removed_base_indices[i]);
        }
        CettaIndex logical = 0;
        for (uint32_t raw = 0; raw < OVERLAY_BASE_COUNT; raw++) {
            if (removed[raw])
                continue;
            assert(space_get_atom_id_at64(&overlay, logical) == ids[raw]);
            logical++;
        }
        assert(logical == space_length64(&overlay));
        space_free(&overlay);
        space_free(&overlay_base);
    }

    {
        Space replace_target;
        Space replace_source;
        space_init_with_universe(&replace_target, &universe);
        space_init_with_universe(&replace_source, &universe);
        space_add(&replace_target, atom_int(&scratch_d, 20001));
        space_add(&replace_source, atom_int(&scratch_d, 20002));
        uint64_t target_instance = space_instance_id(&replace_target);
        uint64_t source_instance = space_instance_id(&replace_source);
        SpaceReadToken before_replace = space_read_token(&replace_target);
        SpaceReadToken source_before_replace = space_read_token(&replace_source);
        uint64_t epoch_before = space_global_mutation_epoch();
        space_replace_contents(&replace_target, &replace_source);
        assert(space_instance_id(&replace_target) == target_instance);
        assert(!space_read_token_is_current(before_replace));
        assert(space_instance_id(&replace_source) != 0u);
        assert(space_instance_id(&replace_source) != source_instance);
        assert(!space_read_token_is_current(source_before_replace));
        assert(space_global_mutation_epoch() == epoch_before + 1u);
        assert(space_length64(&replace_target) == 1u);
        assert(space_get_at64(&replace_target, 0)->ground.ival == 20002);
        /* The moved-from source is a complete empty Space, not a half-valid
           shell: it has a fresh lifetime and may be reused normally. */
        assert(space_length64(&replace_source) == 0u);
        space_add(&replace_source, atom_int(&scratch_d, 20003));
        assert(space_length64(&replace_source) == 1u);
        assert(space_get_at64(&replace_source, 0)->ground.ival == 20003);
        space_free(&replace_source);
        space_free(&replace_target);
    }

    space_free(&right);
    space_free(&left);
    term_universe_free(&universe);
    arena_free(&scratch_e);
    arena_free(&scratch_d);
    arena_free(&scratch_c);
    arena_free(&scratch_b);
    arena_free(&scratch_a);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: space term universe membership");
    return 0;
}
