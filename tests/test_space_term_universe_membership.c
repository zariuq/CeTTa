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
        assert(space_equations_may_match_known_head(&equation_space,
                                                    source_head));
        assert(!space_equations_may_match_known_head(&equation_space,
                                                     colliding_head));
        assert(!space_equations_may_match_known_head(&equation_space,
                                                     absent_colliding_head));

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

        space_add(&equation_space,
                  atom_expr3(&equation_scratch,
                             atom_symbol_id(&equation_scratch,
                                            g_builtin_syms.equals),
                             atom_var_with_id(&equation_scratch, "wildcard", 1),
                             atom_true(&equation_scratch)));
        assert(space_equations_may_match_known_head(&equation_space,
                                                    absent_colliding_head));

        Space occurrence_space;
        space_init_with_universe(&occurrence_space, &equation_universe);
        space_add(&occurrence_space, source_equation);
        space_add(&occurrence_space, source_equation);
        assert(space_length64(&occurrence_space) == 2);
        SpaceReadToken occurrence_read = space_read_token(&occurrence_space);
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
