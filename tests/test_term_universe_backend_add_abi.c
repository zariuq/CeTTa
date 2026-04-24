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
#include "subst_tree.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"
static uint8_t g_fake_bridge_space_storage;
static CettaMorkSpaceHandle *const g_fake_bridge_space =
    (CettaMorkSpaceHandle *)&g_fake_bridge_space_storage;
static uint8_t g_fake_bridge_cursor_storage;
static CettaMorkCursorHandle *const g_fake_bridge_cursor =
    (CettaMorkCursorHandle *)&g_fake_bridge_cursor_storage;
static bool g_bridge_accept_expr = false;
static char *g_bridge_texts[8];
static uint32_t g_bridge_text_count = 0;
static const uint8_t *g_bridge_value_bytes[8];
static size_t g_bridge_value_lens[8];
static uint32_t g_bridge_value_count = 0;
static uint32_t g_bridge_cursor_pos = UINT32_MAX;
static bool g_bridge_logical_rows_result = false;
static uint64_t g_bridge_logical_rows_added = 0;
static uint32_t g_bridge_logical_rows_calls = 0;

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

static void reset_bridge_capture(void) {
    for (uint32_t i = 0; i < g_bridge_text_count; i++) {
        free(g_bridge_texts[i]);
        g_bridge_texts[i] = NULL;
    }
    g_bridge_text_count = 0;
    g_bridge_accept_expr = false;
    memset(g_bridge_value_bytes, 0, sizeof(g_bridge_value_bytes));
    memset(g_bridge_value_lens, 0, sizeof(g_bridge_value_lens));
    g_bridge_value_count = 0;
    g_bridge_cursor_pos = UINT32_MAX;
    g_bridge_logical_rows_result = false;
    g_bridge_logical_rows_added = 0;
    g_bridge_logical_rows_calls = 0;
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
    if (space != g_fake_bridge_space)
        return false;
    g_bridge_cursor_pos = UINT32_MAX;
    return true;
}

bool cetta_mm2_atom_to_bridge_expr_bytes(Arena *a, Atom *atom,
                                         uint8_t **out_bytes,
                                         size_t *out_len,
                                         const char **out_error) {
    (void)a;
    (void)atom;
    if (out_bytes)
        *out_bytes = NULL;
    if (out_len)
        *out_len = 0;
    if (out_error)
        *out_error = "bridge expr-byte encoder stub disabled in unit test";
    return false;
}

bool cetta_mm2_atom_to_contextual_bridge_expr_bytes(
    Arena *a, Atom *atom, uint8_t **out_expr_bytes, size_t *out_expr_len,
    uint8_t **out_context_bytes, size_t *out_context_len,
    const char **out_error) {
    if (out_context_bytes)
        *out_context_bytes = NULL;
    if (out_context_len)
        *out_context_len = 0;
    return cetta_mm2_atom_to_bridge_expr_bytes(
        a, atom, out_expr_bytes, out_expr_len, out_error);
}

bool cetta_mm2_atom_id_to_bridge_expr_bytes(Arena *a,
                                            const TermUniverse *universe,
                                            AtomId atom_id,
                                            uint8_t **out_bytes,
                                            size_t *out_len,
                                            const char **out_error) {
    (void)a;
    (void)universe;
    (void)atom_id;
    if (out_bytes)
        *out_bytes = NULL;
    if (out_len)
        *out_len = 0;
    if (out_error)
        *out_error = "bridge expr-byte encoder stub disabled in unit test";
    return false;
}

bool cetta_mm2_atom_id_to_contextual_bridge_expr_bytes(Arena *a,
                                                       const TermUniverse *universe,
                                                       AtomId atom_id,
                                                       uint8_t **out_expr_bytes,
                                                       size_t *out_expr_len,
                                                       uint8_t **out_context_bytes,
                                                       size_t *out_context_len,
                                                       const char **out_error) {
    if (out_context_bytes)
        *out_context_bytes = NULL;
    if (out_context_len)
        *out_context_len = 0;
    return cetta_mm2_atom_id_to_bridge_expr_bytes(
        a, universe, atom_id, out_expr_bytes, out_expr_len, out_error);
}

bool cetta_mork_bridge_space_add_text(CettaMorkSpaceHandle *space,
                                      const char *text,
                                      uint64_t *out_added) {
    (void)space;
    (void)text;
    if (out_added)
        *out_added = 0;
    return false;
}

bool cetta_mork_bridge_space_add_expr_bytes(CettaMorkSpaceHandle *space,
                                            const uint8_t *expr_bytes,
                                            size_t len,
                                            uint64_t *out_added) {
    if (out_added)
        *out_added = 0;
    if (space != g_fake_bridge_space || !g_bridge_accept_expr || !expr_bytes)
        return false;
    if (g_bridge_text_count < (uint32_t)(sizeof(g_bridge_texts) / sizeof(g_bridge_texts[0]))) {
        size_t text_len = len * 2u + 1u;
        char *copy = malloc(text_len);
        assert(copy != NULL);
        for (size_t i = 0; i < len; i++)
            snprintf(copy + i * 2u, 3u, "%02x", expr_bytes[i]);
        g_bridge_texts[g_bridge_text_count] = copy;
        g_bridge_text_count++;
    }
    if (out_added)
        *out_added = 1;
    return true;
}

bool cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *expr_bytes,
    size_t len,
    const uint8_t *context_bytes,
    size_t context_len,
    uint64_t *out_added) {
    (void)context_bytes;
    (void)context_len;
    return cetta_mork_bridge_space_add_expr_bytes(space, expr_bytes, len,
                                                  out_added);
}

bool cetta_mork_bridge_space_add_expr_bytes_batch(CettaMorkSpaceHandle *space,
                                                  const uint8_t *packet,
                                                  size_t len,
                                                  uint64_t *out_added) {
    (void)space;
    (void)packet;
    (void)len;
    if (out_added)
        *out_added = 0;
    return false;
}

bool cetta_mork_bridge_space_add_logical_rows_from(
    CettaMorkSpaceHandle *dst, const CettaMorkSpaceHandle *src,
    uint64_t *out_added) {
    (void)dst;
    (void)src;
    if (out_added)
        *out_added = g_bridge_logical_rows_added;
    g_bridge_logical_rows_calls++;
    return g_bridge_logical_rows_result;
}

CettaMorkCursorHandle *cetta_mork_bridge_cursor_new(
    const CettaMorkSpaceHandle *space) {
    if (space != g_fake_bridge_space)
        return NULL;
    g_bridge_cursor_pos = UINT32_MAX;
    return g_fake_bridge_cursor;
}

void cetta_mork_bridge_cursor_free(CettaMorkCursorHandle *cursor) {
    (void)cursor;
}

bool cetta_mork_bridge_cursor_next_val(CettaMorkCursorHandle *cursor,
                                       bool *out_moved) {
    if (out_moved)
        *out_moved = false;
    if (cursor != g_fake_bridge_cursor)
        return false;
    if (g_bridge_cursor_pos == UINT32_MAX)
        g_bridge_cursor_pos = 0;
    else
        g_bridge_cursor_pos++;
    if (g_bridge_cursor_pos >= g_bridge_value_count) {
        if (out_moved)
            *out_moved = false;
        return true;
    }
    if (out_moved)
        *out_moved = true;
    return true;
}

bool cetta_mork_bridge_cursor_path_bytes(const CettaMorkCursorHandle *cursor,
                                         uint8_t **out_bytes,
                                         size_t *out_len) {
    if (out_bytes)
        *out_bytes = NULL;
    if (out_len)
        *out_len = 0;
    if (cursor != g_fake_bridge_cursor || g_bridge_cursor_pos >= g_bridge_value_count)
        return false;
    size_t len = g_bridge_value_lens[g_bridge_cursor_pos];
    uint8_t *copy = malloc(len ? len : 1u);
    assert(copy != NULL);
    if (len)
        memcpy(copy, g_bridge_value_bytes[g_bridge_cursor_pos], len);
    if (out_bytes)
        *out_bytes = copy;
    if (out_len)
        *out_len = len;
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

bool cetta_mork_bridge_space_remove_text(CettaMorkSpaceHandle *space,
                                         const char *text,
                                         uint64_t *out_removed) {
    (void)space;
    (void)text;
    if (out_removed)
        *out_removed = 0;
    return false;
}

bool cetta_mork_bridge_space_remove_expr_bytes(CettaMorkSpaceHandle *space,
                                               const uint8_t *expr_bytes,
                                               size_t len,
                                               uint64_t *out_removed) {
    (void)space;
    (void)expr_bytes;
    (void)len;
    if (out_removed)
        *out_removed = 0;
    return false;
}

bool cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *expr_bytes,
    size_t len,
    const uint8_t *context_bytes,
    size_t context_len,
    uint64_t *out_removed) {
    (void)context_bytes;
    (void)context_len;
    return cetta_mork_bridge_space_remove_expr_bytes(space, expr_bytes, len,
                                                     out_removed);
}

bool cetta_mork_bridge_space_contains_expr_bytes(const CettaMorkSpaceHandle *space,
                                                 const uint8_t *expr_bytes,
                                                 size_t len,
                                                 bool *out_found) {
    (void)space;
    (void)expr_bytes;
    (void)len;
    if (out_found)
        *out_found = false;
    return false;
}

bool cetta_mork_bridge_space_size(const CettaMorkSpaceHandle *space,
                                  uint64_t *out_size) {
    if (out_size)
        *out_size = 0;
    if (space != g_fake_bridge_space)
        return false;
    if (out_size)
        *out_size = g_bridge_value_count;
    return true;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_clone(
    const CettaMorkSpaceHandle *space) {
    (void)space;
    return NULL;
}

bool cetta_mork_bridge_space_dump_expr_rows(CettaMorkSpaceHandle *space,
                                            uint8_t **out_packet,
                                            size_t *out_len,
                                            uint32_t *out_rows) {
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    if (space != g_fake_bridge_space)
        return false;

    size_t packet_len = 0;
    for (uint32_t i = 0; i < g_bridge_value_count; i++) {
        packet_len += 4u + g_bridge_value_lens[i];
    }
    uint8_t *packet = malloc(packet_len ? packet_len : 1u);
    assert(packet != NULL);

    size_t off = 0;
    for (uint32_t i = 0; i < g_bridge_value_count; i++) {
        uint32_t len32 = (uint32_t)g_bridge_value_lens[i];
        packet[off++] = (uint8_t)(len32 >> 24);
        packet[off++] = (uint8_t)(len32 >> 16);
        packet[off++] = (uint8_t)(len32 >> 8);
        packet[off++] = (uint8_t)len32;
        if (len32) {
            memcpy(packet + off, g_bridge_value_bytes[i], len32);
            off += len32;
        }
    }

    if (out_packet)
        *out_packet = packet;
    if (out_len)
        *out_len = packet_len;
    if (out_rows)
        *out_rows = g_bridge_value_count;
    return true;
}

bool cetta_mork_bridge_space_dump_contextual_exact_rows(CettaMorkSpaceHandle *space,
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

bool cetta_mork_bridge_space_query_contextual_rows(CettaMorkSpaceHandle *space,
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
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_MORK)) {
        printf("SKIP: MORK imported backend unavailable in this build\n");
        space_free(&imported_space);
        return;
    }
    imported_space.match_backend.mork.bridge.built = true;
    imported_space.match_backend.mork.bridge.dirty = false;
    imported_space.match_backend.mork.bridge.bridge_active = false;

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
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_MORK)) {
        printf("SKIP: MORK imported backend unavailable in this build\n");
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
    reset_test_counters();
    assert(!space_match_backend_bridge_space(&imported_space, &bridge));
    assert(bridge == NULL);
    assert(g_bridge_text_count == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *stable_add = expr2(scratch, sym(scratch, "later"), sym(scratch, "ok"));
    AtomId stable_id = term_universe_store_atom_id(universe, NULL, stable_add);
    assert(stable_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, stable_id) != NULL);
    assert(!space_match_backend_needs_atom_on_add(&imported_space, stable_id));
    reset_bridge_capture();
    reset_test_counters();
    space_add(&imported_space, stable_add);
    assert(g_bridge_text_count == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    Atom *top_string = atom_string(scratch, "solo");
    reset_bridge_capture();
    reset_test_counters();
    space_add(&imported_space, top_string);
    assert(g_bridge_text_count == 0);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    SubstMatchSet string_matches;
    smset_init(&string_matches);
    space_subst_query(&imported_space, scratch, top_string, &string_matches);
    assert(string_matches.len == 1);
    smset_free(&string_matches);

    Atom *unstable_add =
        expr2(scratch, sym(scratch, "wrap-space"), atom_space(scratch, &imported_space));
    AtomId unstable_id = term_universe_store_atom_id(universe, NULL, unstable_add);
    assert(unstable_id != CETTA_ATOM_ID_NONE);
    assert(tu_hdr(universe, unstable_id) == NULL);

    SubstMatchSet rule_matches;
    smset_init(&rule_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, ":="),
                            expr2(scratch, sym(scratch, "I"), sym(scratch, "foo")),
                            var(scratch, "rhs", 7001)),
                      &rule_matches);
    assert(rule_matches.len == 1);
    smset_free(&rule_matches);
    assert(space_match_backend_needs_atom_on_add(&imported_space, unstable_id));

    reset_bridge_capture();
    assert(g_bridge_text_count == 0);
    space_free(&imported_space);
}

static void test_imported_chunk_remove_direct_id_boundary(TermUniverse *universe,
                                                          Arena *scratch) {
    Space imported_space;
    uint64_t added = 0;
    uint64_t removed = 0;
    const char *seed_text = "(keep a) (drop b)";
    const char *remove_text = "(drop b)";

    (void)scratch;
    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    reset_term_universe_witnesses(universe);
    assert(space_match_backend_load_sexpr_chunk(&imported_space, scratch,
                                                (const uint8_t *)seed_text,
                                                strlen(seed_text), &added));
    assert(added == 2);
    assert(space_length(&imported_space) == 2);
    CettaTermUniverseDiagnostics add_diag =
        snapshot_term_universe_witnesses(universe);
    assert(add_diag.legacy_top_down_stable_admissions == 0);
    assert(add_diag.lazy_decode_count == 0);
    assert(add_diag.legacy_hash_recompute_count == 0);

    reset_term_universe_witnesses(universe);
    assert(space_match_backend_remove_sexpr_chunk(&imported_space, scratch,
                                                  (const uint8_t *)remove_text,
                                                  strlen(remove_text), &removed));
    assert(removed == 1);
    assert(space_length(&imported_space) == 1);
    CettaTermUniverseDiagnostics remove_diag =
        snapshot_term_universe_witnesses(universe);
    assert(remove_diag.direct_constructor_leaf_hits > 0);
    assert(remove_diag.direct_constructor_expr_hits > 0);
    assert(remove_diag.legacy_top_down_stable_admissions == 0);
    assert(remove_diag.lazy_decode_count == 0);
    assert(remove_diag.legacy_hash_recompute_count == 0);

    SubstMatchSet keep_matches;
    smset_init(&keep_matches);
    space_subst_query(&imported_space, scratch,
                      expr2(scratch, sym(scratch, "keep"), sym(scratch, "a")),
                      &keep_matches);
    assert(keep_matches.len == 1);
    smset_free(&keep_matches);

    SubstMatchSet drop_matches;
    smset_init(&drop_matches);
    space_subst_query(&imported_space, scratch,
                      expr2(scratch, sym(scratch, "drop"), sym(scratch, "b")),
                      &drop_matches);
    assert(drop_matches.len == 0);
    smset_free(&drop_matches);
    space_free(&imported_space);
}

static void test_imported_chunk_switchback_regression(TermUniverse *universe,
                                                      Arena *scratch) {
    Space imported_space;
    uint64_t added = 0;
    uint64_t removed = 0;
    const char *seed_text = "(edge a b) (edge b c)";
    const char *grow_text = "(edge c d)";
    const char *remove_text = "(edge a b)";

    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    assert(space_match_backend_load_sexpr_chunk(&imported_space, scratch,
                                                (const uint8_t *)seed_text,
                                                strlen(seed_text), &added));
    assert(added == 2);
    assert(space_length(&imported_space) == 2);

    assert(space_match_backend_load_sexpr_chunk(&imported_space, scratch,
                                                (const uint8_t *)grow_text,
                                                strlen(grow_text), &added));
    assert(added == 1);
    assert(space_length(&imported_space) == 3);

    assert(space_match_backend_remove_sexpr_chunk(&imported_space, scratch,
                                                  (const uint8_t *)remove_text,
                                                  strlen(remove_text), &removed));
    assert(removed == 1);
    assert(space_length(&imported_space) == 2);

    SubstMatchSet keep_matches;
    smset_init(&keep_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "edge"),
                            sym(scratch, "b"), sym(scratch, "c")),
                      &keep_matches);
    assert(keep_matches.len == 1);
    smset_free(&keep_matches);

    assert(space_match_backend_try_set(&imported_space,
                                       SPACE_ENGINE_NATIVE_CANDIDATE_EXACT));
    assert(space_length(&imported_space) == 2);

    SubstMatchSet bc_matches;
    smset_init(&bc_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "edge"),
                            sym(scratch, "b"), sym(scratch, "c")),
                      &bc_matches);
    assert(bc_matches.len == 1);
    smset_free(&bc_matches);

    SubstMatchSet ab_matches;
    smset_init(&ab_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "edge"),
                            sym(scratch, "a"), sym(scratch, "b")),
                      &ab_matches);
    assert(ab_matches.len == 0);
    smset_free(&ab_matches);

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

static void test_subst_tree_live_branch_builder_witness(Arena *scratch) {
    SubstBucket bucket;
    SubstMatchSet matches;
    Atom *query = expr3(scratch, sym(scratch, "pair"),
                        sym(scratch, "A"), sym(scratch, "B"));

    stree_bucket_init(&bucket);
    smset_init(&matches);

    stree_bucket_insert(
        &bucket,
        expr3(scratch, sym(scratch, "pair"),
              var(scratch, "x", 4101), sym(scratch, "B")),
        0);
    stree_bucket_insert(
        &bucket,
        expr3(scratch, sym(scratch, "pair"),
              var(scratch, "y", 4102), sym(scratch, "C")),
        1);

    reset_test_counters();
    stree_query_bucket(&bucket, scratch, query, NULL, &matches);

    assert(matches.len == 1);
    assert(matches.items[0].atom_idx == 0);
    assert(matches.items[0].bindings.len == 1);
    assert(test_counter(CETTA_RUNTIME_COUNTER_BINDINGS_CLONE) == 1);
    assert(atom_is_symbol_id(matches.items[0].bindings.entries[0].val,
                             symbol_intern_cstr(g_symbols, "A")));

    smset_free(&matches);
    stree_bucket_free(&bucket);
}

static void test_parser_direct_add_boundary(TermUniverse *universe, Arena *scratch) {
    const char *stable_text = "(pair alpha 17) \"hello\" (ns.foo beta)";
    const char *var_text = "(pair $x $x)";
    Arena persistent;
    TermUniverse local_universe;
    AtomId *ids = NULL;
    AtomId *var_ids = NULL;
    Atom **atoms = NULL;
    Space space;

    (void)universe;
    arena_init(&persistent);
    term_universe_init(&local_universe);
    term_universe_set_persistent_arena(&local_universe, &persistent);
    universe = &local_universe;

    reset_term_universe_witnesses(universe);
    int n = parse_metta_text_ids(stable_text, universe, &ids);
    CettaTermUniverseDiagnostics direct_diag =
        snapshot_term_universe_witnesses(universe);
    assert(n == 3);
    assert(ids != NULL);
    assert(tu_kind(universe, ids[0]) == ATOM_EXPR);
    assert(tu_kind(universe, ids[1]) == ATOM_GROUNDED);
    assert(tu_kind(universe, ids[2]) == ATOM_EXPR);
    assert(tu_head_sym(universe, ids[2]) ==
           symbol_intern_cstr(g_symbols, "ns:foo"));
    assert(direct_diag.direct_constructor_leaf_hits > 0);
    assert(direct_diag.direct_constructor_expr_hits > 0);
    assert(direct_diag.legacy_top_down_stable_admissions == 0);
    assert(direct_diag.direct_lookup_misses > 0);
    assert(direct_diag.lazy_decode_count == 0);
    assert(direct_diag.legacy_hash_recompute_count == 0);

    int legacy_n = parse_metta_text(stable_text, scratch, &atoms);
    assert(legacy_n == n);
    term_universe_diag_reset(universe);
    for (int i = 0; i < n; i++) {
        assert(term_universe_store_atom_id(universe, NULL, atoms[i]) == ids[i]);
    }
    CettaTermUniverseDiagnostics legacy_diag =
        snapshot_term_universe_witnesses(universe);
    free(atoms);
    assert(legacy_diag.direct_constructor_leaf_hits > 0);
    assert(legacy_diag.direct_constructor_expr_hits > 0);
    assert(legacy_diag.legacy_top_down_stable_admissions == 3);
    assert(legacy_diag.direct_lookup_hits > 0);
    assert(legacy_diag.direct_lookup_misses == 0);
    assert(legacy_diag.lazy_decode_count == 0);
    assert(legacy_diag.legacy_hash_recompute_count == 0);

    space_init_with_universe(&space, universe);
    reset_term_universe_witnesses(universe);
    for (int i = 0; i < n; i++) {
        space_add_atom_id(&space, ids[i]);
    }
    CettaTermUniverseDiagnostics add_diag =
        snapshot_term_universe_witnesses(universe);
    assert(space_length(&space) == (uint32_t)n);
    assert(space_get_atom_id_at(&space, 0) == ids[0]);
    assert(space_get_atom_id_at(&space, 1) == ids[1]);
    assert(space_get_atom_id_at(&space, 2) == ids[2]);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);
    assert(add_diag.direct_constructor_leaf_hits == 0);
    assert(add_diag.direct_constructor_expr_hits == 0);
    assert(add_diag.legacy_top_down_stable_admissions == 0);
    assert(add_diag.direct_lookup_hits == 0);
    assert(add_diag.direct_lookup_misses == 0);
    assert(add_diag.lazy_decode_count == 0);
    assert(add_diag.legacy_hash_recompute_count == 0);
    space_free(&space);
    free(ids);

    n = parse_metta_text_ids(var_text, universe, &var_ids);
    assert(n == 1);
    assert(var_ids != NULL);
    assert(tu_kind(universe, var_ids[0]) == ATOM_EXPR);
    assert(tu_child(universe, var_ids[0], 1) ==
           tu_child(universe, var_ids[0], 2));
    free(var_ids);

    term_universe_free(universe);
    arena_free(&persistent);
}

static void test_bridge_structural_import_boundary(TermUniverse *universe,
                                                   Arena *scratch) {
    static const uint8_t pair_a_17[] = {
        3u,
        0xC4u, 'p', 'a', 'i', 'r',
        0xC1u, 'A',
        0xC2u, '1', '7',
    };
    static const uint8_t solo_string[] = {
        0xC6u, '"', 's', 'o', 'l', 'o', '"',
    };
    static const uint8_t pair_same_var[] = {
        3u,
        0xC4u, 'p', 'a', 'i', 'r',
        0xC0u,
        0x80u,
    };
    Space imported_space;
    uint64_t loaded = 0;

    (void)scratch;
    space_init_with_universe(&imported_space, universe);

    reset_bridge_capture();
    g_bridge_value_bytes[0] = pair_a_17;
    g_bridge_value_lens[0] = sizeof(pair_a_17);
    g_bridge_value_bytes[1] = solo_string;
    g_bridge_value_lens[1] = sizeof(solo_string);
    g_bridge_value_count = 2;
    reset_term_universe_witnesses(universe);
    {
        SpaceTransferEndpoint dst_endpoint = {
            .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
            .space = &imported_space,
        };
        SpaceTransferEndpoint src_endpoint = {
            .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
            .bridge = g_fake_bridge_space,
        };
        assert(space_match_backend_transfer_resolved_result(
                   dst_endpoint, src_endpoint, NULL, &loaded) ==
               SPACE_TRANSFER_OK);
    }
    assert(loaded == 2);
    assert(space_length(&imported_space) == 2);
    CettaTermUniverseDiagnostics import_diag =
        snapshot_term_universe_witnesses(universe);
    assert(import_diag.direct_constructor_leaf_hits > 0);
    assert(import_diag.direct_constructor_expr_hits > 0);
    assert(import_diag.legacy_top_down_stable_admissions == 0);
    assert(import_diag.lazy_decode_count == 0);
    assert(import_diag.legacy_hash_recompute_count == 0);

    SubstMatchSet pair_matches;
    smset_init(&pair_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "pair"),
                            sym(scratch, "A"), atom_int(scratch, 17)),
                      &pair_matches);
    assert(pair_matches.len == 1);
    smset_free(&pair_matches);

    SubstMatchSet string_matches;
    smset_init(&string_matches);
    space_subst_query(&imported_space, scratch, atom_string(scratch, "solo"),
                      &string_matches);
    assert(string_matches.len == 1);
    smset_free(&string_matches);

    reset_bridge_capture();
    g_bridge_value_bytes[0] = pair_same_var;
    g_bridge_value_lens[0] = sizeof(pair_same_var);
    g_bridge_value_count = 1;
    loaded = 1234;
    reset_term_universe_witnesses(universe);
    {
        SpaceTransferEndpoint dst_endpoint = {
            .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
            .space = &imported_space,
        };
        SpaceTransferEndpoint src_endpoint = {
            .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
            .bridge = g_fake_bridge_space,
        };
        assert(space_match_backend_transfer_resolved_result(
                   dst_endpoint, src_endpoint, NULL, &loaded) ==
               SPACE_TRANSFER_OK);
    }
    assert(loaded == 1);
    assert(space_length(&imported_space) == 3);
    import_diag = snapshot_term_universe_witnesses(universe);
    assert(import_diag.direct_constructor_leaf_hits > 0);
    assert(import_diag.direct_constructor_expr_hits > 0);
    assert(import_diag.legacy_top_down_stable_admissions == 0);
    assert(import_diag.lazy_decode_count == 0);

    SubstMatchSet same_var_matches;
    smset_init(&same_var_matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "pair"),
                            var(scratch, "same", 9301),
                            var(scratch, "same", 9301)),
                      &same_var_matches);
    assert(same_var_matches.len == 1);
    smset_free(&same_var_matches);

    space_free(&imported_space);
}

static void test_pathmap_no_universe_import_boundary(void) {
    static const uint8_t pair_a_17[] = {
        3u,
        0xC4u, 'p', 'a', 'i', 'r',
        0xC1u, 'A',
        0xC2u, '1', '7',
    };
    Space imported_space;
    uint64_t loaded = 1234;

    space_init(&imported_space);
    imported_space.universe = NULL;
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    reset_bridge_capture();
    g_bridge_value_bytes[0] = pair_a_17;
    g_bridge_value_lens[0] = sizeof(pair_a_17);
    g_bridge_value_count = 1;
    {
        SpaceTransferEndpoint dst_endpoint = {
            .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
            .space = &imported_space,
        };
        SpaceTransferEndpoint src_endpoint = {
            .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
            .bridge = g_fake_bridge_space,
        };
        assert(space_match_backend_transfer_resolved_result(
                   dst_endpoint, src_endpoint, NULL, &loaded) ==
               SPACE_TRANSFER_NEEDS_TEXT_FALLBACK);
    }
    assert(loaded == 0);
    assert(space_length(&imported_space) == 0);

    space_free(&imported_space);
}

static void test_space_to_space_bridge_unavailable_falls_back(TermUniverse *universe,
                                                              Arena *scratch) {
    Space dst;
    Space src;
    uint64_t added = 999;
    SpaceTransferEndpoint dst_endpoint;
    SpaceTransferEndpoint src_endpoint;

    space_init_with_universe(&dst, universe);
    space_init_with_universe(&src, universe);
    space_add(&src, expr2(scratch, sym(scratch, "safe-fallback"),
                          sym(scratch, "copied")));

    reset_bridge_capture();
    dst_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &dst,
    };
    src_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &src,
    };

    assert(space_match_backend_transfer_resolved_result(
               dst_endpoint, src_endpoint, NULL, &added) ==
           SPACE_TRANSFER_OK);
    assert(g_bridge_logical_rows_calls == 0);
    assert(added == 1);
    assert(space_length(&dst) == 1);

    space_free(&src);
    space_free(&dst);
}

static void test_space_to_space_bridge_attempt_failure_is_error(TermUniverse *universe) {
    Space dst;
    Space src;
    uint64_t added = 999;
    SpaceTransferEndpoint dst_endpoint;
    SpaceTransferEndpoint src_endpoint;

    space_init_with_universe(&dst, universe);
    space_init_with_universe(&src, universe);
    assert(space_match_backend_try_set(&dst, SPACE_ENGINE_PATHMAP));
    assert(space_match_backend_try_set(&src, SPACE_ENGINE_PATHMAP));

    reset_bridge_capture();
    g_bridge_logical_rows_result = false;
    g_bridge_logical_rows_added = 1;

    dst_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &dst,
    };
    src_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &src,
    };

    assert(space_match_backend_transfer_resolved_result(
               dst_endpoint, src_endpoint, NULL, &added) ==
           SPACE_TRANSFER_ERROR);
    assert(g_bridge_logical_rows_calls == 1);
    assert(added == 0);
    assert(space_length(&dst) == 0);

    space_free(&src);
    space_free(&dst);
}

static void test_space_clone_direct_id_boundary(TermUniverse *universe, Arena *scratch) {
    Space source;
    Space *clone = NULL;
    AtomId before_ids[3];

    space_init_with_universe(&source, universe);
    reset_term_universe_witnesses(universe);
    space_add(&source, expr2(scratch, sym(scratch, "clone"), sym(scratch, "alpha")));
    space_add(&source, atom_string(scratch, "beta"));
    space_add(&source, expr3(scratch, sym(scratch, "pair"),
                             sym(scratch, "left"), sym(scratch, "right")));
    before_ids[0] = space_get_atom_id_at(&source, 0);
    before_ids[1] = space_get_atom_id_at(&source, 1);
    before_ids[2] = space_get_atom_id_at(&source, 2);
    assert(before_ids[0] != CETTA_ATOM_ID_NONE);
    assert(before_ids[1] != CETTA_ATOM_ID_NONE);
    assert(before_ids[2] != CETTA_ATOM_ID_NONE);

    reset_term_universe_witnesses(universe);
    clone = space_heap_clone_shallow(&source);
    assert(clone != NULL);
    assert(clone->universe == universe);
    assert(space_length(clone) == 3);
    assert(space_get_atom_id_at(clone, 0) == before_ids[0]);
    assert(space_get_atom_id_at(clone, 1) == before_ids[1]);
    assert(space_get_atom_id_at(clone, 2) == before_ids[2]);
    assert(test_counter(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE) == 0);

    CettaTermUniverseDiagnostics diag =
        snapshot_term_universe_witnesses(universe);
    assert(diag.direct_constructor_leaf_hits == 0);
    assert(diag.direct_constructor_expr_hits == 0);
    assert(diag.legacy_top_down_stable_admissions == 0);
    assert(diag.direct_lookup_hits == 0);
    assert(diag.direct_lookup_misses == 0);
    assert(diag.lazy_decode_count == 0);
    assert(diag.legacy_hash_recompute_count == 0);

    space_free(clone);
    free(clone);
    space_free(&source);
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
    test_imported_chunk_remove_direct_id_boundary(&universe, &scratch);
    test_imported_chunk_switchback_regression(&universe, &scratch);
    test_byte_backed_rematch_delay(&universe, &scratch);
    test_subst_tree_live_branch_builder_witness(&scratch);
    test_parser_direct_add_boundary(&universe, &scratch);
    test_bridge_structural_import_boundary(&universe, &scratch);
    test_pathmap_no_universe_import_boundary();
    test_space_to_space_bridge_unavailable_falls_back(&universe, &scratch);
    test_space_to_space_bridge_attempt_failure_is_error(&universe);
    test_space_clone_direct_id_boundary(&universe, &scratch);

    term_universe_free(&universe);
    reset_bridge_capture();
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: term universe backend add abi");
    return 0;
}
