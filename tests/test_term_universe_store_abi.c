#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cetta_stdlib.h"
#include "parser.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

static void reset_test_counters(void) {
    test_runtime_stats_reset_counters();
}

static uint64_t test_counter(CettaRuntimeCounter counter) {
    return test_runtime_stats_counter(counter);
}

static void reset_term_universe_witnesses(TermUniverse *universe) {
    reset_test_counters();
    term_universe_diag_reset(universe);
}

static CettaTermUniverseDiagnostics
snapshot_term_universe_witnesses(const TermUniverse *universe) {
    CettaTermUniverseDiagnostics out;
    term_universe_diag_snapshot(universe, &out);
    return out;
}

static void test_file_ingress_workload_witness(void) {
    const char *path = "tests/bench_matchjoin8_he.metta";
    Arena persistent;
    Arena scratch;
    TermUniverse universe;
    Space workload_space;
    AtomId *atom_ids = NULL;
    uint32_t loaded_atoms = 0;
    uint32_t bang_pairs = 0;

    arena_init(&persistent);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&workload_space, &universe);
    workload_space.kind = SPACE_KIND_HASH;

    reset_term_universe_witnesses(&universe);
    int n = parse_metta_file_ids(path, &universe, &atom_ids);
    assert(n > 0);
    for (int i = 0; i < n; i++) {
        if (tu_kind(&universe, atom_ids[i]) == ATOM_SYMBOL &&
            tu_sym(&universe, atom_ids[i]) == g_builtin_syms.bang &&
            i + 1 < n) {
            bang_pairs++;
            i++;
            continue;
        }
        space_add_atom_id(&workload_space, atom_ids[i]);
        loaded_atoms++;
    }

    CettaTermUniverseDiagnostics load_diag =
        snapshot_term_universe_witnesses(&universe);
    assert(space_length(&workload_space) == loaded_atoms);
    assert(loaded_atoms > 0);
    assert(bang_pairs == 1);
    assert(load_diag.direct_constructor_leaf_hits > 0);
    assert(load_diag.direct_constructor_expr_hits > 0);
    assert(load_diag.legacy_top_down_stable_admissions == 0);
    assert(load_diag.direct_lookup_misses > 0);
    assert(load_diag.lazy_decode_count == 0);
    assert(load_diag.legacy_hash_recompute_count == 0);

    reset_term_universe_witnesses(&universe);
    workload_space.eq_idx_dirty = true;
    SymbolId threehop8_sym = symbol_intern_cstr(g_symbols, "threehop8");
    Atom *query = atom_expr(&scratch,
                            (Atom *[]){atom_symbol_id(&scratch, threehop8_sym)}, 1);
    QueryResults eq_results;
    query_results_init(&eq_results);
    query_equations(&workload_space, query, &scratch, &eq_results);
    CettaTermUniverseDiagnostics query_diag =
        snapshot_term_universe_witnesses(&universe);
    assert(eq_results.len == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD) == 1);
    assert(query_diag.direct_constructor_leaf_hits == 0);
    assert(query_diag.direct_constructor_expr_hits == 0);
    assert(query_diag.legacy_top_down_stable_admissions == 0);
    assert(query_diag.lazy_decode_count == 0);
    assert(query_diag.legacy_hash_recompute_count == 0);
    query_results_free(&eq_results);

    free(atom_ids);
    space_free(&workload_space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
}

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
                                  uint32_t atom_idx) {
    (void)s;
    (void)atom_id;
    (void)atom;
    (void)atom_idx;
}

void space_match_backend_note_remove(Space *s) {
    (void)s;
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

uint32_t space_match_backend_logical_len(const Space *s) {
    return s ? s->len : 0;
}

AtomId space_match_backend_get_atom_id_at(const Space *s, uint32_t idx) {
    if (!s || idx >= s->len || !s->atom_ids)
        return CETTA_ATOM_ID_NONE;
    return s->atom_ids[s->start + idx];
}

Atom *space_match_backend_get_at(const Space *s, uint32_t idx) {
    AtomId atom_id = space_match_backend_get_atom_id_at(s, idx);
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    return term_universe_get_atom(s->native.universe, atom_id);
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
    uint32_t npatterns, const Bindings *seed, BindingSet *out) {
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

int main(void) {
    SymbolTable symbols;
    VarInternTable var_intern;
    Arena persistent;
    Arena scratch;
    TermUniverse universe;

    init_test_symbols(&symbols);
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;
    test_file_ingress_workload_witness();
    arena_init(&persistent);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    SymbolId pair_sym = symbol_intern_cstr(g_symbols, "pair");
    SymbolId var_sym = symbol_intern_cstr(g_symbols, "V");
    SymbolId box_sym = symbol_intern_cstr(g_symbols, "box");
    SymbolId alpha_sym = symbol_intern_cstr(g_symbols, "alpha");
    SymbolId tag_sym = symbol_intern_cstr(g_symbols, "Tag");

    Atom *sym = atom_symbol_id(&scratch, pair_sym);
    Atom *var = atom_var_with_spelling(&scratch, var_sym, 42);
    Atom *ival = atom_int(&scratch, 17);
    Atom *fval = atom_float(&scratch, 3.5);
    Atom *truth = atom_bool(&scratch, true);
    Atom *str = atom_string(&scratch, "hello");

    reset_test_counters();
    AtomId sym_id = term_universe_store_atom_id(&universe, NULL, sym);
    AtomId var_id = term_universe_store_atom_id(&universe, NULL, var);
    AtomId int_id = term_universe_store_atom_id(&universe, NULL, ival);
    AtomId float_id = term_universe_store_atom_id(&universe, NULL, fval);
    AtomId bool_id = term_universe_store_atom_id(&universe, NULL, truth);
    AtomId str_id = term_universe_store_atom_id(&universe, NULL, str);

    assert(sym_id != CETTA_ATOM_ID_NONE);
    assert(var_id != CETTA_ATOM_ID_NONE);
    assert(int_id != CETTA_ATOM_ID_NONE);
    assert(float_id != CETTA_ATOM_ID_NONE);
    assert(bool_id != CETTA_ATOM_ID_NONE);
    assert(str_id != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 6);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY) == 6);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_FALLBACK_ENTRY) == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BLOB_BYTES) > 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    assert(universe.entries[sym_id].decoded_cache == NULL);
    assert(universe.entries[str_id].decoded_cache == NULL);
    assert(tu_kind(&universe, sym_id) == ATOM_SYMBOL);
    assert(tu_kind(&universe, var_id) == ATOM_VAR);
    assert(tu_kind(&universe, int_id) == ATOM_GROUNDED);
    assert(tu_sym(&universe, sym_id) == pair_sym);
    assert(tu_sym(&universe, var_id) == var_sym);
    assert(tu_var_id(&universe, var_id) == 42);
    assert(!tu_has_vars(&universe, sym_id));
    assert(tu_has_vars(&universe, var_id));
    assert(tu_ground_kind(&universe, int_id) == GV_INT);
    assert(tu_ground_kind(&universe, float_id) == GV_FLOAT);
    assert(tu_ground_kind(&universe, bool_id) == GV_BOOL);
    assert(tu_ground_kind(&universe, str_id) == GV_STRING);
    assert(tu_int(&universe, int_id) == 17);
    assert(strcmp(tu_string_cstr(&universe, str_id), "hello") == 0);
    assert(strcmp(term_universe_atom_to_parseable_string(&scratch, &universe, str_id),
                  "\"hello\"") == 0);
    AtomId sym_id_direct = tu_intern_symbol(&universe, pair_sym);
    AtomId var_id_direct = tu_intern_var(&universe, var_sym, 42);
    AtomId int_id_direct = tu_intern_int(&universe, 17);
    AtomId float_id_direct = tu_intern_float(&universe, 3.5);
    AtomId bool_id_direct = tu_intern_bool(&universe, true);
    AtomId str_id_direct = tu_intern_string(&universe, "hello");
    assert(sym_id_direct == sym_id);
    assert(var_id_direct == var_id);
    assert(int_id_direct == int_id);
    assert(float_id_direct == float_id);
    assert(bool_id_direct == bool_id);
    assert(str_id_direct == str_id);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 6);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY) == 6);

    Atom *expr_elems[4] = {sym, ival, str, var};
    Atom *expr = atom_expr(&scratch, expr_elems, 4);
    AtomId expr_id = term_universe_store_atom_id(&universe, NULL, expr);
    assert(expr_id != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 7);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY) == 7);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_FALLBACK_ENTRY) == 0);
    assert(universe.entries[expr_id].decoded_cache == NULL);
    assert(tu_kind(&universe, expr_id) == ATOM_EXPR);
    assert(tu_arity(&universe, expr_id) == 4);
    assert(tu_head_sym(&universe, expr_id) == pair_sym);
    assert(tu_has_vars(&universe, expr_id));
    assert(tu_child(&universe, expr_id, 0) == sym_id);
    assert(tu_child(&universe, expr_id, 1) == int_id);
    assert(tu_child(&universe, expr_id, 2) == str_id);
    assert(tu_child(&universe, expr_id, 3) == var_id);
    AtomId expr_children[4] = {sym_id, int_id, str_id, var_id};
    AtomId expr_id_direct = tu_expr_from_ids(&universe, expr_children, 4);
    assert(expr_id_direct == expr_id);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 7);

    Atom *box_elems[2] = {
        atom_symbol_id(&scratch, box_sym),
        atom_space(&scratch, &universe),
    };
    Atom *boxed = atom_expr(&scratch, box_elems, 2);
    AtomId boxed_id = term_universe_store_atom_id(&universe, NULL, boxed);
    assert(boxed_id != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 8);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY) == 7);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_FALLBACK_ENTRY) == 1);
    assert(tu_hdr(&universe, boxed_id) == NULL);
    assert(universe.entries[boxed_id].decoded_cache != NULL);
    assert(term_universe_get_atom(&universe, boxed_id) ==
           universe.entries[boxed_id].decoded_cache);
    AtomId bad_children[2] = {sym_id, boxed_id};
    assert(tu_expr_from_ids(&universe, bad_children, 2) == CETTA_ATOM_ID_NONE);

    reset_test_counters();
    AtomId empty_direct = tu_expr_from_ids(&universe, NULL, 0);
    assert(empty_direct != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 1);
    Atom *empty_expr = atom_expr(&scratch, NULL, 0);
    AtomId empty_legacy = term_universe_store_atom_id(&universe, NULL, empty_expr);
    assert(empty_legacy == empty_direct);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 1);

    reset_test_counters();
    SymbolId direct_head_sym = symbol_intern_cstr(&symbols, "direct-first");
    AtomId direct_head_id = tu_intern_symbol(&universe, direct_head_sym);
    AtomId direct_expr_children[2] = {direct_head_id, int_id};
    AtomId direct_expr_id = tu_expr_from_ids(&universe, direct_expr_children, 2);
    assert(direct_head_id != CETTA_ATOM_ID_NONE);
    assert(direct_expr_id != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 2);
    Atom *direct_expr_elems[2] = {
        atom_symbol_id(&scratch, direct_head_sym),
        atom_int(&scratch, 17),
    };
    Atom *direct_expr = atom_expr(&scratch, direct_expr_elems, 2);
    AtomId direct_expr_legacy =
        term_universe_store_atom_id(&universe, NULL, direct_expr);
    assert(direct_expr_legacy == direct_expr_id);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 2);

    reset_term_universe_witnesses(&universe);
    SymbolId diag_head_sym = symbol_intern_cstr(&symbols, "diag-head");
    AtomId diag_head_id = tu_intern_symbol(&universe, diag_head_sym);
    AtomId diag_int_id = tu_intern_int(&universe, 4242);
    AtomId diag_children[2] = {diag_head_id, diag_int_id};
    AtomId diag_expr_id = tu_expr_from_ids(&universe, diag_children, 2);
    CettaTermUniverseDiagnostics diag =
        snapshot_term_universe_witnesses(&universe);
    assert(diag_head_id != CETTA_ATOM_ID_NONE);
    assert(diag_int_id != CETTA_ATOM_ID_NONE);
    assert(diag_expr_id != CETTA_ATOM_ID_NONE);
    assert(diag.direct_constructor_leaf_hits == 2);
    assert(diag.direct_constructor_expr_hits == 1);
    assert(diag.legacy_top_down_stable_admissions == 0);
    assert(diag.direct_lookup_hits == 0);
    assert(diag.direct_lookup_misses == 3);
    assert(diag.lazy_decode_count == 0);
    assert(diag.legacy_hash_recompute_count == 0);

    Atom *diag_expr_elems[2] = {
        atom_symbol_id(&scratch, diag_head_sym),
        atom_int(&scratch, 4242),
    };
    Atom *diag_expr = atom_expr(&scratch, diag_expr_elems, 2);
    AtomId diag_expr_legacy =
        term_universe_store_atom_id(&universe, NULL, diag_expr);
    diag = snapshot_term_universe_witnesses(&universe);
    assert(diag_expr_legacy == diag_expr_id);
    assert(diag.direct_constructor_leaf_hits == 4);
    assert(diag.direct_constructor_expr_hits == 2);
    assert(diag.legacy_top_down_stable_admissions == 1);
    assert(diag.direct_lookup_hits == 3);
    assert(diag.direct_lookup_misses == 3);
    assert(diag.lazy_decode_count == 0);
    assert(diag.legacy_hash_recompute_count == 0);

    Space stdlib_space;
    space_init_with_universe(&stdlib_space, &universe);
    reset_term_universe_witnesses(&universe);
    stdlib_load(&stdlib_space, &persistent);
    CettaTermUniverseDiagnostics stdlib_diag =
        snapshot_term_universe_witnesses(&universe);
    assert(space_length(&stdlib_space) > 0);
    assert(stdlib_diag.direct_constructor_leaf_hits > 0);
    assert(stdlib_diag.direct_constructor_expr_hits > 0);
    assert(stdlib_diag.legacy_top_down_stable_admissions == 0);
    assert(stdlib_diag.lazy_decode_count == 0);
    assert(stdlib_diag.legacy_hash_recompute_count == 0);

#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    {
        static const unsigned char scoped_blob[] = {
            'C','S','T','D',
            0x01,0x00,
            0x01,0x00,
            0x02,0x00,0x00,0x00,
            'x',0x00,
            0x02,0x00,
            0x03,0x02,0x00, 0x02,0x00,0x00, 0x02,0x00,0x00,
            0x03,0x02,0x00, 0x02,0x00,0x00, 0x02,0x00,0x00,
        };
        Space scoped_space;
        space_init_with_universe(&scoped_space, &universe);
        reset_term_universe_witnesses(&universe);
        assert(stdlib_load_blob_bytes(&scoped_space, &persistent,
                                      scoped_blob, sizeof(scoped_blob)));
        assert(space_length(&scoped_space) == 2);
        AtomId scoped0 = space_get_atom_id_at(&scoped_space, 0);
        AtomId scoped1 = space_get_atom_id_at(&scoped_space, 1);
        assert(scoped0 != CETTA_ATOM_ID_NONE);
        assert(scoped1 != CETTA_ATOM_ID_NONE);
        assert(scoped0 != scoped1);
        Atom *scoped_atom0 = space_get_at(&scoped_space, 0);
        Atom *scoped_atom1 = space_get_at(&scoped_space, 1);
        assert(scoped_atom0 && scoped_atom0->kind == ATOM_EXPR &&
               scoped_atom0->expr.len == 2);
        assert(scoped_atom1 && scoped_atom1->kind == ATOM_EXPR &&
               scoped_atom1->expr.len == 2);
        assert(scoped_atom0->expr.elems[0]->kind == ATOM_VAR);
        assert(scoped_atom0->expr.elems[1]->kind == ATOM_VAR);
        assert(scoped_atom1->expr.elems[0]->kind == ATOM_VAR);
        assert(scoped_atom1->expr.elems[1]->kind == ATOM_VAR);
        assert(scoped_atom0->expr.elems[0]->var_id ==
               scoped_atom0->expr.elems[1]->var_id);
        assert(scoped_atom1->expr.elems[0]->var_id ==
               scoped_atom1->expr.elems[1]->var_id);
        assert(scoped_atom0->expr.elems[0]->var_id !=
               scoped_atom1->expr.elems[0]->var_id);
        space_free(&scoped_space);
    }
#endif

    term_universe_diag_reset(&universe);
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE, 0);
    Atom *sym_load = term_universe_get_atom(&universe, sym_id);
    assert(sym_load == universe.entries[sym_id].decoded_cache);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 1);
    assert(term_universe_get_atom(&universe, sym_id) == sym_load);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 1);
    diag = snapshot_term_universe_witnesses(&universe);
    assert(diag.lazy_decode_count == 1);

    Atom *var_load = term_universe_get_atom(&universe, var_id);
    Atom *int_load = term_universe_get_atom(&universe, int_id);
    Atom *str_load = term_universe_get_atom(&universe, str_id);
    assert(var_load != NULL);
    assert(int_load != NULL);
    assert(str_load != NULL);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 4);

    Atom *expr_load = term_universe_get_atom(&universe, expr_id);
    assert(expr_load == universe.entries[expr_id].decoded_cache);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 5);
    assert(term_universe_get_atom(&universe, expr_id) == expr_load);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 5);
    diag = snapshot_term_universe_witnesses(&universe);
    assert(diag.lazy_decode_count == 5);
    assert(expr_load->expr.elems[0] == sym_load);
    assert(expr_load->expr.elems[1] == int_load);
    assert(expr_load->expr.elems[2] == str_load);
    assert(expr_load->expr.elems[3] == var_load);

    Atom *epoch_var = atom_var_with_spelling(&scratch, var_sym, var_epoch_id(42, 7));
    Atom *epoch_items[3] = {sym, epoch_var, epoch_var};
    Atom *epoch_expr = atom_expr(&scratch, epoch_items, 3);
    AtomId epoch_id = term_universe_store_atom_id(&universe, NULL, epoch_expr);
    assert(epoch_id != CETTA_ATOM_ID_NONE);
    Atom *epoch_loaded = term_universe_get_atom(&universe, epoch_id);
    assert(epoch_loaded != NULL);
    assert(epoch_loaded->kind == ATOM_EXPR);
    assert(epoch_loaded->expr.len == 3);
    assert(epoch_loaded->expr.elems[1]->kind == ATOM_VAR);
    assert(var_epoch_suffix(epoch_loaded->expr.elems[1]->var_id) == 0);
    assert(epoch_loaded->expr.elems[1]->var_id == epoch_loaded->expr.elems[2]->var_id);
    uint32_t epoch_canonical_base = var_base_id(epoch_loaded->expr.elems[1]->var_id);
    assert(epoch_canonical_base != 0);
    assert(epoch_canonical_base != 42);
    Atom *epoch_copy = term_universe_copy_atom_epoch(&universe, &scratch, epoch_id, 99);
    assert(epoch_copy != NULL);
    assert(var_epoch_suffix(epoch_copy->expr.elems[1]->var_id) == 99);
    assert(var_base_id(epoch_copy->expr.elems[1]->var_id) == epoch_canonical_base);

    Space space;
    space_init_with_universe(&space, &universe);
    Atom *fresh = atom_symbol(&scratch, "fresh-space-add");
    reset_test_counters();
    space_add(&space, fresh);
    AtomId fresh_id = space_get_atom_id_at(&space, 0);
    assert(fresh_id != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    assert(universe.entries[fresh_id].decoded_cache == NULL);
    Atom *fresh_loaded = space_get_at(&space, 0);
    assert(fresh_loaded != NULL);
    assert(fresh_loaded == universe.entries[fresh_id].decoded_cache);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 1);

    Space hash_space;
    space_init_with_universe(&hash_space, &universe);
    hash_space.kind = SPACE_KIND_HASH;
    reset_test_counters();
    Atom *seen_head = atom_symbol(&scratch, "seen");
    Atom *seen_alpha = atom_symbol(&scratch, "alpha");
    Atom *seen_items[2] = {seen_head, seen_alpha};
    Atom *seen_atom = atom_expr(&scratch, seen_items, 2);
    space_add(&hash_space, seen_atom);
    AtomId seen_id = space_get_atom_id_at(&hash_space, 0);
    assert(seen_id != CETTA_ATOM_ID_NONE);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY) == 3);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    term_universe_diag_reset(&universe);
    AtomId lookup_id = term_universe_lookup_atom_id(&universe, seen_atom);
    CettaTermUniverseDiagnostics lookup_diag =
        snapshot_term_universe_witnesses(&universe);
    assert(lookup_id == seen_id);
    assert(term_universe_atom_id_eq(&universe, seen_id, seen_atom));
    assert(tu_hash32(&universe, seen_id) == atom_hash(seen_atom));
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    assert(lookup_diag.lazy_decode_count == 0);
    assert(lookup_diag.legacy_hash_recompute_count == 1);

    Space admit_space;
    space_init_with_universe(&admit_space, &universe);
    Atom *seen_cached = term_universe_get_atom(&universe, seen_id);
    assert(seen_cached != NULL);
    reset_term_universe_witnesses(&universe);
    assert(space_admit_atom(&admit_space, NULL, seen_cached));
    CettaTermUniverseDiagnostics admit_diag =
        snapshot_term_universe_witnesses(&universe);
    assert(space_get_atom_id_at(&admit_space, 0) == seen_id);
    assert(admit_diag.direct_constructor_leaf_hits == 0);
    assert(admit_diag.direct_constructor_expr_hits == 0);
    assert(admit_diag.legacy_top_down_stable_admissions == 0);
    assert(admit_diag.direct_lookup_hits == 0);
    assert(admit_diag.direct_lookup_misses == 0);
    assert(admit_diag.lazy_decode_count == 0);
    assert(admit_diag.legacy_hash_recompute_count == 0);

    DiscNode *disc = disc_node_new();
    assert(disc_insert_id(disc, &universe, seen_id, 77));
    uint32_t *disc_matches = NULL;
    uint32_t disc_nmatches = 0;
    uint32_t disc_cap = 0;
    disc_lookup(disc, seen_atom, &disc_matches, &disc_nmatches, &disc_cap);
    assert(disc_nmatches == 1);
    assert(disc_cap >= disc_nmatches);
    assert(disc_matches != NULL);
    assert(disc_matches[0] == 77);
    free(disc_matches);
    disc_node_free(disc);

    SubstTree stree;
    stree_init(&stree);
    assert(stree_insert_id(&stree, &universe, seen_id, 88));
    SubstMatchSet direct_matches;
    smset_init(&direct_matches);
    stree_query_bucket(&stree.buckets[stree_head_hash(tu_head_sym(&universe, seen_id))],
                       &scratch, seen_atom, NULL, &direct_matches);
    assert(direct_matches.len == 1);
    assert(direct_matches.items[0].atom_idx == 88);
    smset_free(&direct_matches);
    stree_free(&stree);

    hash_space.exact_idx_dirty = true;
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE, 0);
    uint32_t *matches = NULL;
    uint32_t nmatches = space_exact_match_indices(&hash_space, seen_atom, &matches);
    assert(nmatches == 1);
    assert(matches != NULL);
    assert(matches[0] == 0);
    free(matches);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Space typed_space;
    space_init_with_universe(&typed_space, &universe);
    typed_space.kind = SPACE_KIND_HASH;
    Atom *annotation_items[3] = {
        atom_symbol_id(&scratch, g_builtin_syms.colon),
        atom_symbol_id(&scratch, alpha_sym),
        atom_symbol_id(&scratch, tag_sym),
    };
    Atom *annotation = atom_expr(&scratch, annotation_items, 3);
    reset_test_counters();
    space_add(&typed_space, annotation);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    typed_space.ty_idx_dirty = true;
    reset_test_counters();
    Atom **typed_results = NULL;
    uint32_t ntyped = get_atom_types(&typed_space, &scratch,
                                     atom_symbol_id(&scratch, alpha_sym),
                                     &typed_results);
    assert(ntyped == 1);
    assert(typed_results != NULL);
    assert(atom_is_symbol_id(typed_results[0], tag_sym));
    free(typed_results);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TY_INDEX_REBUILD) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    SymbolId foo_sym = symbol_intern_cstr(&symbols, "foo");
    SymbolId a_sym = symbol_intern_cstr(&symbols, "A");
    SymbolId b_sym = symbol_intern_cstr(&symbols, "B");
    Space equation_space;
    space_init_with_universe(&equation_space, &universe);
    equation_space.kind = SPACE_KIND_HASH;
    Atom *lhs_items[2] = {
        atom_symbol_id(&scratch, foo_sym),
        atom_symbol_id(&scratch, a_sym),
    };
    Atom *lhs_atom = atom_expr(&scratch, lhs_items, 2);
    Atom *equation_items[3] = {
        atom_symbol_id(&scratch, g_builtin_syms.equals),
        lhs_atom,
        atom_symbol_id(&scratch, b_sym),
    };
    Atom *equation = atom_expr(&scratch, equation_items, 3);
    reset_test_counters();
    space_add(&equation_space, equation);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    equation_space.eq_idx_dirty = true;
    reset_test_counters();
    assert(space_equations_may_match_known_head(&equation_space, foo_sym));
    assert(test_counter(CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD) == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    QueryResults eq_results;
    query_results_init(&eq_results);
    reset_test_counters();
    query_equations(&equation_space, lhs_atom, &scratch, &eq_results);
    assert(eq_results.len == 1);
    assert(atom_is_symbol_id(eq_results.items[0].result, b_sym));
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    query_results_free(&eq_results);

    DiscNode *fallback_disc = disc_node_new();
    assert(!disc_insert_id(fallback_disc, &universe, boxed_id, 99));
    disc_node_free(fallback_disc);

    SubstTree fallback_stree;
    stree_init(&fallback_stree);
    assert(!stree_insert_id(&fallback_stree, &universe, boxed_id, 99));
    stree_free(&fallback_stree);

    space_free(&equation_space);
    space_free(&typed_space);
    space_free(&admit_space);
    space_free(&hash_space);
    space_free(&space);
    space_free(&stdlib_space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_var_intern = NULL;
    var_intern_free(&var_intern);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: term universe store abi");
    return 0;
}
