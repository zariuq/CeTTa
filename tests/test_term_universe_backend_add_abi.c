#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mork_space_bridge_runtime.h"
#include "parser.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"

static uint64_t g_test_counters[CETTA_RUNTIME_COUNTER_COUNT];
static uint8_t g_fake_bridge_space_storage;
static CettaMorkSpaceHandle *const g_fake_bridge_space =
    (CettaMorkSpaceHandle *)&g_fake_bridge_space_storage;
static bool g_bridge_accept_indexed = false;
static char *g_bridge_texts[8];
static uint32_t g_bridge_indices[8];
static uint32_t g_bridge_text_count = 0;

void cetta_runtime_stats_add(CettaRuntimeCounter counter, uint64_t delta) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return;
    g_test_counters[counter] += delta;
}

static void reset_test_counters(void) {
    memset(g_test_counters, 0, sizeof(g_test_counters));
}

static uint64_t test_counter(CettaRuntimeCounter counter) {
    if ((uint32_t)counter >= CETTA_RUNTIME_COUNTER_COUNT)
        return 0;
    return g_test_counters[counter];
}

static void reset_bridge_capture(void) {
    for (uint32_t i = 0; i < g_bridge_text_count; i++) {
        free(g_bridge_texts[i]);
        g_bridge_texts[i] = NULL;
    }
    memset(g_bridge_indices, 0, sizeof(g_bridge_indices));
    g_bridge_text_count = 0;
    g_bridge_accept_indexed = false;
}

static char *copy_cstr(const char *text) {
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    assert(copy != NULL);
    memcpy(copy, text, len + 1);
    return copy;
}

Arena *eval_current_persistent_arena(void) {
    return NULL;
}

bool cetta_mork_bridge_is_available(void) {
    return false;
}

const char *cetta_mork_bridge_last_error(void) {
    return "bridge stubs disabled in unit test";
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_new(void) {
    return NULL;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_new_pathmap(void) {
    return g_fake_bridge_space;
}

void cetta_mork_bridge_space_free(CettaMorkSpaceHandle *space) {
    (void)space;
}

bool cetta_mork_bridge_space_clear(CettaMorkSpaceHandle *space) {
    return space == g_fake_bridge_space;
}

bool cetta_mork_bridge_space_add_indexed_text(CettaMorkSpaceHandle *space,
                                              uint32_t atom_idx,
                                              const char *text) {
    if (space != g_fake_bridge_space || !g_bridge_accept_indexed || !text)
        return false;
    if (g_bridge_text_count < (uint32_t)(sizeof(g_bridge_texts) / sizeof(g_bridge_texts[0]))) {
        g_bridge_texts[g_bridge_text_count] = copy_cstr(text);
        g_bridge_indices[g_bridge_text_count] = atom_idx;
        g_bridge_text_count++;
    }
    return true;
}

bool cetta_mork_bridge_space_add_sexpr(CettaMorkSpaceHandle *space,
                                       const uint8_t *text,
                                       size_t len,
                                       uint64_t *out_added) {
    (void)space;
    (void)text;
    (void)len;
    if (out_added)
        *out_added = 0;
    return false;
}

bool cetta_mork_bridge_space_remove_sexpr(CettaMorkSpaceHandle *space,
                                          const uint8_t *text,
                                          size_t len,
                                          uint64_t *out_removed) {
    (void)space;
    (void)text;
    (void)len;
    if (out_removed)
        *out_removed = 0;
    return false;
}

bool cetta_mork_bridge_space_dump(CettaMorkSpaceHandle *space,
                                  uint8_t **out_packet,
                                  size_t *out_len,
                                  uint32_t *out_rows) {
    (void)space;
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    return false;
}

bool cetta_mork_bridge_space_step(CettaMorkSpaceHandle *space,
                                  uint64_t steps,
                                  uint64_t *out_performed) {
    (void)space;
    (void)steps;
    if (out_performed)
        *out_performed = 0;
    return false;
}

bool cetta_mork_bridge_space_load_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_loaded) {
    (void)space;
    (void)path;
    (void)len;
    if (out_loaded)
        *out_loaded = 0;
    return false;
}

bool cetta_mork_bridge_space_compile_query_expr_text(CettaMorkSpaceHandle *space,
                                                     const char *pattern_text,
                                                     uint8_t **out_expr,
                                                     size_t *out_len) {
    (void)space;
    (void)pattern_text;
    if (out_expr)
        *out_expr = NULL;
    if (out_len)
        *out_len = 0;
    return false;
}

bool cetta_mork_bridge_space_query_candidates_prefix_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len,
    uint32_t **out_indices,
    uint32_t *out_count) {
    (void)space;
    (void)pattern_expr;
    (void)len;
    if (out_indices)
        *out_indices = NULL;
    if (out_count)
        *out_count = 0;
    return false;
}

bool cetta_mork_bridge_space_query_candidates_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len,
    uint32_t **out_indices,
    uint32_t *out_count) {
    (void)space;
    (void)pattern_expr;
    (void)len;
    if (out_indices)
        *out_indices = NULL;
    if (out_count)
        *out_count = 0;
    return false;
}

bool cetta_mork_bridge_space_query_bindings_query_only_v2(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len,
                                                          uint8_t **out_packet,
                                                          size_t *out_len,
                                                          uint32_t *out_rows) {
    (void)space;
    (void)pattern;
    (void)len;
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    return false;
}

bool cetta_mork_bridge_space_query_bindings_multi_ref_v3(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern,
                                                         size_t len,
                                                         uint8_t **out_packet,
                                                         size_t *out_len,
                                                         uint32_t *out_rows) {
    (void)space;
    (void)pattern;
    (void)len;
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    return false;
}

void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len) {
    (void)len;
    free(data);
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static Atom *sym(Arena *a, const char *name) {
    return atom_symbol(a, name);
}

static Atom *var(Arena *a, const char *name, VarId id) {
    return atom_var_with_spelling(a, symbol_intern_cstr(g_symbols, name), id);
}

static Atom *expr2(Arena *a, Atom *x0, Atom *x1) {
    Atom *items[2] = {x0, x1};
    return atom_expr(a, items, 2);
}

static Atom *expr3(Arena *a, Atom *x0, Atom *x1, Atom *x2) {
    Atom *items[3] = {x0, x1, x2};
    return atom_expr(a, items, 3);
}

static void test_native_add_boundary(TermUniverse *universe, Arena *scratch) {
    Space native_space;
    space_init_with_universe(&native_space, universe);
    assert(space_match_backend_try_set(&native_space, SPACE_ENGINE_NATIVE));

    for (uint32_t i = 0; i <= MATCH_TRIE_THRESHOLD; i++) {
        char name[32];
        snprintf(name, sizeof(name), "seed%u", i);
        space_add(&native_space, expr2(scratch, sym(scratch, "seed"), sym(scratch, name)));
    }

    SubstMatchSet initial_matches;
    smset_init(&initial_matches);
    space_subst_query(&native_space, scratch,
                      expr2(scratch, sym(scratch, "seed"), sym(scratch, "seed0")),
                      &initial_matches);
    assert(initial_matches.len == 1);
    smset_free(&initial_matches);

    Atom *stable_atom = expr2(scratch, sym(scratch, "stable-native"), sym(scratch, "ok"));
    AtomId stable_id = term_universe_store_atom_id(universe, NULL, stable_atom);
    assert(stable_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, stable_id) != NULL);
    assert(!space_match_backend_needs_atom_on_add(&native_space, stable_id));
    reset_test_counters();
    space_add(&native_space, stable_atom);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *unstable_atom =
        expr2(scratch, sym(scratch, "unstable-native"), atom_space(scratch, &native_space));
    AtomId unstable_id = term_universe_store_atom_id(universe, NULL, unstable_atom);
    assert(unstable_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, unstable_id) == NULL);
    assert(space_match_backend_needs_atom_on_add(&native_space, unstable_id));
    reset_test_counters();
    space_add(&native_space, unstable_atom);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    SubstMatchSet unstable_matches;
    smset_init(&unstable_matches);
    space_subst_query(&native_space, scratch, unstable_atom, &unstable_matches);
    assert(unstable_matches.len == 1);
    smset_free(&unstable_matches);

    space_free(&native_space);
}

static void test_imported_flat_add_boundary(TermUniverse *universe, Arena *scratch) {
    Space imported_space;
    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }
    imported_space.match_backend.imported.built = true;
    imported_space.match_backend.imported.dirty = false;
    imported_space.match_backend.imported.bridge_active = false;

    Atom *pair_aa = expr3(scratch, sym(scratch, "pair"), sym(scratch, "A"), sym(scratch, "A"));
    AtomId pair_aa_id = term_universe_store_atom_id(universe, NULL, pair_aa);
    assert(pair_aa_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, pair_aa_id) != NULL);
    assert(!space_match_backend_needs_atom_on_add(&imported_space, pair_aa_id));
    reset_test_counters();
    space_add(&imported_space, pair_aa);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *pair_vv = expr3(scratch, sym(scratch, "pair"),
                          var(scratch, "z", 77), var(scratch, "z", 77));
    AtomId pair_vv_id = term_universe_store_atom_id(universe, NULL, pair_vv);
    assert(pair_vv_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, pair_vv_id) != NULL);
    assert(!space_match_backend_needs_atom_on_add(&imported_space, pair_vv_id));
    reset_test_counters();
    space_add(&imported_space, pair_vv);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *wrap_space =
        expr2(scratch, sym(scratch, "wrap-space"), atom_space(scratch, &imported_space));
    AtomId wrap_space_id = term_universe_store_atom_id(universe, NULL, wrap_space);
    assert(wrap_space_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, wrap_space_id) == NULL);
    assert(space_match_backend_needs_atom_on_add(&imported_space, wrap_space_id));
    reset_test_counters();
    space_add(&imported_space, wrap_space);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *rewrite_rule =
        expr3(scratch, sym(scratch, ":="),
              expr2(scratch, sym(scratch, "I"), var(scratch, "x", 101)),
              var(scratch, "x", 101));
    AtomId rewrite_rule_id = term_universe_store_atom_id(universe, NULL, rewrite_rule);
    assert(rewrite_rule_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, rewrite_rule_id) != NULL);
    assert(!space_match_backend_needs_atom_on_add(&imported_space, rewrite_rule_id));
    reset_test_counters();
    space_add(&imported_space, rewrite_rule);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    SubstMatchSet pair_matches;
    smset_init(&pair_matches);
    reset_test_counters();
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "pair"),
                            var(scratch, "q", 5001), var(scratch, "q", 5001)),
                      &pair_matches);
    assert(pair_matches.len == 2);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    smset_free(&pair_matches);

    SubstMatchSet wrap_matches;
    smset_init(&wrap_matches);
    space_subst_query(&imported_space, scratch, wrap_space, &wrap_matches);
    assert(wrap_matches.len == 1);
    smset_free(&wrap_matches);

    SubstMatchSet rewrite_matches;
    smset_init(&rewrite_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, ":="),
                            expr2(scratch, sym(scratch, "I"), sym(scratch, "foo")),
                            var(scratch, "r", 5003)),
                      &rewrite_matches);
    assert(rewrite_matches.len == 1);
    smset_free(&rewrite_matches);

    space_free(&imported_space);
}

static void test_imported_bridge_add_boundary(TermUniverse *universe, Arena *scratch) {
    Space imported_space;
    CettaMorkSpaceHandle *bridge = NULL;
    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    Atom *rule =
        expr3(scratch, sym(scratch, ":="),
              expr2(scratch, sym(scratch, "I"), sym(scratch, "foo")),
              sym(scratch, "bar"));
    Atom *say =
        expr2(scratch, sym(scratch, "say"), atom_string(scratch, "line\nbreak"));
    space_add(&imported_space, rule);
    space_add(&imported_space, say);

    reset_bridge_capture();
    g_bridge_accept_indexed = true;
    reset_test_counters();
    assert(space_match_backend_bridge_space(&imported_space, &bridge));
    assert(bridge == g_fake_bridge_space);
    assert(g_bridge_text_count == 2);
    assert(g_bridge_indices[0] == 0);
    assert(g_bridge_indices[1] == 1);
    assert(strcmp(g_bridge_texts[0], "(:= (I foo) bar)") == 0);
    assert(strcmp(g_bridge_texts[1], "(say \"line\\nbreak\")") == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *stable_add = expr2(scratch, sym(scratch, "later"), sym(scratch, "ok"));
    AtomId stable_id = term_universe_store_atom_id(universe, NULL, stable_add);
    assert(stable_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, stable_id) != NULL);
    assert(!space_match_backend_needs_atom_on_add(&imported_space, stable_id));
    reset_bridge_capture();
    g_bridge_accept_indexed = true;
    reset_test_counters();
    space_add(&imported_space, stable_add);
    assert(g_bridge_text_count == 1);
    assert(g_bridge_indices[0] == 2);
    assert(strcmp(g_bridge_texts[0], "(later ok)") == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *top_string = atom_string(scratch, "solo");
    reset_bridge_capture();
    g_bridge_accept_indexed = true;
    reset_test_counters();
    space_add(&imported_space, top_string);
    assert(g_bridge_text_count == 1);
    assert(g_bridge_indices[0] == 3);
    assert(strcmp(g_bridge_texts[0], "\"solo\"") == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *unstable_add =
        expr2(scratch, sym(scratch, "wrap-space"), atom_space(scratch, &imported_space));
    AtomId unstable_id = term_universe_store_atom_id(universe, NULL, unstable_add);
    assert(unstable_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, unstable_id) == NULL);
    assert(space_match_backend_needs_atom_on_add(&imported_space, unstable_id));

    reset_bridge_capture();
    space_free(&imported_space);
}

static void test_byte_backed_rematch_delay(TermUniverse *universe, Arena *scratch) {
    Space space;
    Bindings seed;
    Bindings out;
    SubstMatch sm;
    Atom *candidate =
        expr3(scratch, sym(scratch, "pair"), sym(scratch, "A"), sym(scratch, "B"));
    Atom *query_var = var(scratch, "q", 9001);

    space_init_with_universe(&space, universe);
    space_add(&space, candidate);

    bindings_init(&seed);
    bindings_init(&sm.bindings);
    sm.atom_idx = 0;
    sm.epoch = 0;
    sm.exact = false;

    bindings_init(&out);
    reset_test_counters();
    assert(space_subst_match_with_seed(
        &space,
        expr3(scratch, sym(scratch, "pair"), sym(scratch, "A"), sym(scratch, "B")),
        &sm, &seed, scratch, &out));
    assert(out.len == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    bindings_free(&out);

    bindings_init(&out);
    reset_test_counters();
    assert(space_subst_match_with_seed(
        &space,
        expr3(scratch, sym(scratch, "pair"), query_var, sym(scratch, "B")),
        &sm, &seed, scratch, &out));
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    assert(bindings_lookup_var(&out, query_var) != NULL);
    assert(atom_is_symbol_id(bindings_lookup_var(&out, query_var),
                             symbol_intern_cstr(g_symbols, "A")));
    bindings_free(&out);

    bindings_free(&sm.bindings);
    bindings_free(&seed);
    space_free(&space);
}

static void test_parser_direct_add_boundary(TermUniverse *universe, Arena *scratch) {
    const char *stable_text = "(pair alpha 17) \"hello\" (ns.foo beta)";
    const char *var_text = "(pair $x $x)";
    AtomId *ids = NULL;
    AtomId *var_ids = NULL;
    Atom **atoms = NULL;
    Space space;

    int n = parse_metta_text_ids(stable_text, universe, &ids);
    assert(n == 3);
    assert(ids != NULL);
    assert(tu_kind(universe, ids[0]) == ATOM_EXPR);
    assert(tu_kind(universe, ids[1]) == ATOM_GROUNDED);
    assert(tu_kind(universe, ids[2]) == ATOM_EXPR);
    assert(tu_head_sym(universe, ids[2]) ==
           symbol_intern_cstr(g_symbols, "ns:foo"));

    int legacy_n = parse_metta_text(stable_text, scratch, &atoms);
    assert(legacy_n == n);
    for (int i = 0; i < n; i++) {
        assert(term_universe_store_atom_id(universe, NULL, atoms[i]) == ids[i]);
    }
    free(atoms);

    space_init_with_universe(&space, universe);
    reset_test_counters();
    for (int i = 0; i < n; i++) {
        space_add_atom_id(&space, ids[i]);
    }
    assert(space_length(&space) == (uint32_t)n);
    assert(space_get_atom_id_at(&space, 0) == ids[0]);
    assert(space_get_atom_id_at(&space, 1) == ids[1]);
    assert(space_get_atom_id_at(&space, 2) == ids[2]);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    space_free(&space);
    free(ids);

    n = parse_metta_text_ids(var_text, universe, &var_ids);
    assert(n == 1);
    assert(var_ids != NULL);
    assert(tu_kind(universe, var_ids[0]) == ATOM_EXPR);
    assert(tu_child(universe, var_ids[0], 1) ==
           tu_child(universe, var_ids[0], 2));
    free(var_ids);
}

int main(void) {
    SymbolTable symbols;
    Arena persistent;
    Arena scratch;
    TermUniverse universe;

    init_test_symbols(&symbols);
    arena_init(&persistent);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    test_native_add_boundary(&universe, &scratch);
    test_imported_flat_add_boundary(&universe, &scratch);
    test_imported_bridge_add_boundary(&universe, &scratch);
    test_byte_backed_rematch_delay(&universe, &scratch);
    test_parser_direct_add_boundary(&universe, &scratch);

    term_universe_free(&universe);
    reset_bridge_capture();
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: term universe backend add abi");
    return 0;
}
