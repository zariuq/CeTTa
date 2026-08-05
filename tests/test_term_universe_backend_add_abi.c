#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mm2_lower.h"
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
static uint8_t g_fake_query_cursor_storage;
static CettaMorkQueryCursorHandle *const g_fake_query_cursor =
    (CettaMorkQueryCursorHandle *)&g_fake_query_cursor_storage;
static bool g_bridge_accept_expr = false;
static char *g_bridge_texts[8];
static uint32_t g_bridge_text_count = 0;
static const uint8_t *g_bridge_value_bytes[8];
static size_t g_bridge_value_lens[8];
static uint32_t g_bridge_value_count = 0;
static uint64_t g_bridge_size_override = UINT64_MAX;
static uint64_t g_bridge_dump_expr_rows_override = UINT64_MAX;
static uint32_t g_bridge_cursor_pos = UINT32_MAX;
static bool g_bridge_expr_cursor_enabled = true;
static bool g_query_cursor_enabled = false;
static uint32_t g_query_cursor_pos = UINT32_MAX;
static uint32_t g_query_cursor_new_calls = 0;
static bool g_bridge_logical_rows_result = false;
static uint64_t g_bridge_logical_rows_added = 0;
static uint32_t g_bridge_logical_rows_calls = 0;
static const uint8_t *g_bridge_contextual_query_packet = NULL;
static size_t g_bridge_contextual_query_packet_len = 0;
static uint64_t g_bridge_contextual_query_rows = 0;
static const uint8_t *g_bridge_contextual_exact_packet = NULL;
static size_t g_bridge_contextual_exact_packet_len = 0;
static uint64_t g_bridge_contextual_exact_rows = 0;

static bool test_copy_packet_override(const uint8_t *packet,
                                      size_t packet_len,
                                      uint64_t rows,
                                      uint8_t **out_packet,
                                      size_t *out_len,
                                      uint64_t *out_rows);

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
    g_bridge_size_override = UINT64_MAX;
    g_bridge_dump_expr_rows_override = UINT64_MAX;
    g_bridge_cursor_pos = UINT32_MAX;
    g_bridge_expr_cursor_enabled = true;
    g_query_cursor_enabled = false;
    g_query_cursor_pos = UINT32_MAX;
    g_query_cursor_new_calls = 0;
    g_bridge_logical_rows_result = false;
    g_bridge_logical_rows_added = 0;
    g_bridge_logical_rows_calls = 0;
    g_bridge_contextual_query_packet = NULL;
    g_bridge_contextual_query_packet_len = 0;
    g_bridge_contextual_query_rows = 0;
    g_bridge_contextual_exact_packet = NULL;
    g_bridge_contextual_exact_packet_len = 0;
    g_bridge_contextual_exact_rows = 0;
    space_match_backend_diag_reset();
}

Arena *eval_current_persistent_arena(void) {
    return NULL;
}

bool cetta_mork_bridge_is_available(void) {
    return false;
}

bool cetta_mork_bridge_supports_expr_bytes_batch_add(void) {
    return false;
}

bool cetta_mork_bridge_supports_expr_bytes_batch_remove(void) {
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

bool cetta_mm2_atom_ids_to_bridge_expr_bytes_batch(
    Arena *a, const TermUniverse *universe, const AtomId *atom_ids,
    CettaCount atom_count, uint8_t **out_packet, size_t *out_packet_len,
    const char **out_error) {
    (void)a;
    (void)universe;
    (void)atom_ids;
    (void)atom_count;
    if (out_packet)
        *out_packet = NULL;
    if (out_packet_len)
        *out_packet_len = 0;
    if (out_error)
        *out_error = "bridge batch encoder stub disabled in unit test";
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

bool cetta_mm2_atom_to_bridge_expr_packet(Arena *a, Atom *atom,
                                          uint8_t **out_packet,
                                          size_t *out_len,
                                          const char **out_error) {
    return cetta_mm2_atom_to_bridge_expr_bytes(
        a, atom, out_packet, out_len, out_error);
}

bool cetta_mm2_atom_to_contextual_bridge_expr_packet(
    Arena *a, Atom *atom, uint8_t **out_packet, size_t *out_packet_len,
    uint8_t **out_context_bytes, size_t *out_context_len,
    const char **out_error) {
    return cetta_mm2_atom_to_contextual_bridge_expr_bytes(
        a, atom, out_packet, out_packet_len,
        out_context_bytes, out_context_len, out_error);
}

bool cetta_mm2_atom_id_to_bridge_expr_packet(
    Arena *a, const TermUniverse *universe, AtomId atom_id,
    uint8_t **out_packet, size_t *out_len, const char **out_error) {
    return cetta_mm2_atom_id_to_bridge_expr_bytes(
        a, universe, atom_id, out_packet, out_len, out_error);
}

bool cetta_mm2_atom_id_to_contextual_bridge_expr_packet(
    Arena *a, const TermUniverse *universe, AtomId atom_id,
    uint8_t **out_packet, size_t *out_packet_len,
    uint8_t **out_context_bytes, size_t *out_context_len,
    const char **out_error) {
    return cetta_mm2_atom_id_to_contextual_bridge_expr_bytes(
        a, universe, atom_id, out_packet, out_packet_len,
        out_context_bytes, out_context_len, out_error);
}

bool cetta_mork_bridge_space_normalize_expr_packet(
    CettaMorkSpaceHandle *space, const uint8_t *packet, size_t len,
    uint8_t **out_expr_bytes, size_t *out_expr_len) {
    (void)space;
    (void)packet;
    (void)len;
    if (out_expr_bytes)
        *out_expr_bytes = NULL;
    if (out_expr_len)
        *out_expr_len = 0;
    return false;
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
    if (space != g_fake_bridge_space || !g_bridge_expr_cursor_enabled)
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

bool cetta_mork_bridge_cursor_next_expr_rows(CettaMorkCursorHandle *cursor,
                                             uint64_t max_rows,
                                             uint64_t max_bytes,
                                             uint8_t **out_packet,
                                             size_t *out_len,
                                             uint64_t *out_rows) {
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    if (cursor != g_fake_bridge_cursor || max_rows == 0 || max_bytes == 0)
        return false;
    if (g_bridge_dump_expr_rows_override != UINT64_MAX) {
        uint8_t *packet = malloc(1u);
        assert(packet != NULL);
        if (out_packet)
            *out_packet = packet;
        else
            free(packet);
        if (out_rows)
            *out_rows = g_bridge_dump_expr_rows_override;
        return true;
    }

    if (g_bridge_cursor_pos == UINT32_MAX)
        g_bridge_cursor_pos = 0;
    if (g_bridge_cursor_pos >= g_bridge_value_count)
        return true;

    uint64_t rows64 = (uint64_t)g_bridge_value_count - g_bridge_cursor_pos;
    if (rows64 > max_rows)
        rows64 = max_rows;
    uint32_t rows = (uint32_t)rows64;
    size_t packet_len = 0;
    for (uint32_t i = 0; i < rows; i++)
        packet_len += 4u + g_bridge_value_lens[g_bridge_cursor_pos + i];

    uint8_t *packet = malloc(packet_len ? packet_len : 1u);
    assert(packet != NULL);
    size_t off = 0;
    for (uint32_t i = 0; i < rows; i++) {
        uint32_t len32 = (uint32_t)g_bridge_value_lens[g_bridge_cursor_pos + i];
        packet[off++] = (uint8_t)(len32 >> 24);
        packet[off++] = (uint8_t)(len32 >> 16);
        packet[off++] = (uint8_t)(len32 >> 8);
        packet[off++] = (uint8_t)len32;
        if (len32) {
            memcpy(packet + off, g_bridge_value_bytes[g_bridge_cursor_pos + i], len32);
            off += len32;
        }
    }
    g_bridge_cursor_pos += rows;
    if (out_packet)
        *out_packet = packet;
    else
        free(packet);
    if (out_len)
        *out_len = packet_len;
    if (out_rows)
        *out_rows = rows;
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

bool cetta_mork_bridge_space_remove_expr_bytes_batch(
    CettaMorkSpaceHandle *space, const uint8_t *packet, size_t len,
    uint64_t *out_removed) {
    (void)space;
    (void)packet;
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
        *out_size = g_bridge_size_override == UINT64_MAX
            ? (uint64_t)g_bridge_value_count
            : g_bridge_size_override;
    return true;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_clone(
    const CettaMorkSpaceHandle *space) {
    return space == g_fake_bridge_space ? g_fake_bridge_space : NULL;
}

bool cetta_mork_bridge_space_dump_expr_rows(CettaMorkSpaceHandle *space,
                                            uint8_t **out_packet,
                                            size_t *out_len,
                                            uint64_t *out_rows) {
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    if (space != g_fake_bridge_space)
        return false;
    if (g_bridge_dump_expr_rows_override != UINT64_MAX) {
        uint8_t *packet = malloc(1u);
        assert(packet != NULL);
        if (out_packet)
            *out_packet = packet;
        else
            free(packet);
        if (out_rows)
            *out_rows = g_bridge_dump_expr_rows_override;
        return true;
    }

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
                                               uint64_t *out_rows) {
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    if (space == g_fake_bridge_space && g_bridge_contextual_exact_packet) {
        return test_copy_packet_override(
            g_bridge_contextual_exact_packet,
            g_bridge_contextual_exact_packet_len,
            g_bridge_contextual_exact_rows,
            out_packet, out_len, out_rows);
    }
    return false;
}

bool cetta_mork_bridge_space_dump(CettaMorkSpaceHandle *space,
                                  uint8_t **out_packet,
                                  size_t *out_len,
                                  uint64_t *out_rows) {
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
                                                          uint64_t *out_rows) {
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
                                                         uint64_t *out_rows) {
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

static void test_store_u16_be(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void test_store_u32_be(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static void test_store_u64_be(uint8_t *dst, uint64_t value) {
    dst[0] = (uint8_t)(value >> 56);
    dst[1] = (uint8_t)(value >> 48);
    dst[2] = (uint8_t)(value >> 40);
    dst[3] = (uint8_t)(value >> 32);
    dst[4] = (uint8_t)(value >> 24);
    dst[5] = (uint8_t)(value >> 16);
    dst[6] = (uint8_t)(value >> 8);
    dst[7] = (uint8_t)value;
}

static bool test_copy_packet_override(const uint8_t *packet,
                                      size_t packet_len,
                                      uint64_t rows,
                                      uint8_t **out_packet,
                                      size_t *out_len,
                                      uint64_t *out_rows) {
    uint8_t *copy = NULL;
    if (!packet && packet_len != 0)
        return false;
    copy = malloc(packet_len ? packet_len : 1u);
    assert(copy != NULL);
    if (packet_len)
        memcpy(copy, packet, packet_len);
    if (out_packet)
        *out_packet = copy;
    else
        free(copy);
    if (out_len)
        *out_len = packet_len;
    if (out_rows)
        *out_rows = rows;
    return true;
}

bool cetta_mork_bridge_query_cursor_new_query_only_v2(
    CettaMorkSpaceHandle *space, const uint8_t *pattern, size_t len,
    CettaMorkQueryCursorHandle **out_cursor) {
    (void)pattern;
    (void)len;
    if (out_cursor)
        *out_cursor = NULL;
    if (space != g_fake_bridge_space || !g_query_cursor_enabled || !out_cursor)
        return false;
    g_query_cursor_new_calls++;
    g_query_cursor_pos = 0;
    *out_cursor = g_fake_query_cursor;
    return true;
}

bool cetta_mork_bridge_query_cursor_new_multi_ref_v3(
    CettaMorkSpaceHandle *space, const uint8_t *pattern, size_t len,
    CettaMorkQueryCursorHandle **out_cursor) {
    (void)space;
    (void)pattern;
    (void)len;
    if (out_cursor)
        *out_cursor = NULL;
    return false;
}

bool cetta_mork_bridge_query_cursor_new_indexed_multi_ref_v4(
    CettaMorkSpaceHandle *space, const uint8_t *pattern, size_t len,
    CettaMorkQueryCursorHandle **out_cursor) {
    (void)space;
    (void)pattern;
    (void)len;
    if (out_cursor)
        *out_cursor = NULL;
    return false;
}

bool cetta_mork_bridge_query_cursor_new_indexed_semi_naive_multi_ref_v4(
    CettaMorkSpaceHandle *known, CettaMorkSpaceHandle *old,
    CettaMorkSpaceHandle *delta, const uint8_t *pattern, size_t len,
    CettaMorkQueryCursorHandle **out_cursor) {
    (void)known;
    (void)old;
    (void)delta;
    (void)pattern;
    (void)len;
    if (out_cursor)
        *out_cursor = NULL;
    return false;
}

bool cetta_mork_bridge_query_cursor_indexed_stat(
    const CettaMorkQueryCursorHandle *cursor,
    CettaMorkIndexedCursorStat stat, uint64_t *out_value) {
    (void)cursor;
    (void)stat;
    if (out_value)
        *out_value = 0;
    return false;
}

bool cetta_mork_bridge_query_cursor_count_remaining(
    CettaMorkQueryCursorHandle *cursor, uint64_t *out_count) {
    (void)cursor;
    if (out_count)
        *out_count = 0;
    return false;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_monotone_delta(
    const CettaMorkSpaceHandle *later,
    const CettaMorkSpaceHandle *earlier) {
    (void)later;
    (void)earlier;
    return NULL;
}

void cetta_mork_bridge_query_cursor_free(CettaMorkQueryCursorHandle *cursor) {
    (void)cursor;
}

bool cetta_mork_bridge_query_cursor_next(CettaMorkQueryCursorHandle *cursor,
                                         uint64_t max_rows,
                                         uint64_t max_bytes,
                                         uint8_t **out_packet,
                                         size_t *out_len,
                                         uint64_t *out_rows) {
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    if (cursor != g_fake_query_cursor || !g_query_cursor_enabled ||
        max_rows == 0 || max_bytes < 24 || !out_packet || !out_len || !out_rows)
        return false;
    if (g_query_cursor_pos++ > 0)
        return true;

    uint8_t *packet = malloc(24);
    assert(packet != NULL);
    test_store_u32_be(packet, 0x43544252u);
    test_store_u16_be(packet + 4, 5u);
    test_store_u16_be(packet + 6, 0x0013u);
    test_store_u64_be(packet + 8, 1u);
    test_store_u32_be(packet + 16, 0u);
    test_store_u32_be(packet + 20, 0u);
    *out_packet = packet;
    *out_len = 24;
    *out_rows = 1;
    return true;
}

bool cetta_mork_bridge_space_query_contextual_rows(CettaMorkSpaceHandle *space,
                                                   const uint8_t *pattern,
                                                   size_t len,
                                                   uint8_t **out_packet,
                                                   size_t *out_len,
                                                   uint64_t *out_rows) {
    (void)pattern;
    (void)len;
    if (out_packet)
        *out_packet = NULL;
    if (out_len)
        *out_len = 0;
    if (out_rows)
        *out_rows = 0;
    if (space == g_fake_bridge_space && g_bridge_contextual_query_packet) {
        return test_copy_packet_override(
            g_bridge_contextual_query_packet,
            g_bridge_contextual_query_packet_len,
            g_bridge_contextual_query_rows,
            out_packet, out_len, out_rows);
    }
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

static Atom *large_var_query(Arena *a,
                             const char *head_name,
                             uint32_t first_var,
                             uint32_t var_count) {
    const uint32_t chunk_size = 256u;
    uint32_t nchunks = (var_count + chunk_size - 1u) / chunk_size;
    CettaExprLen arity = (CettaExprLen)nchunks + 1u;
    Atom **items = arena_alloc(a, sizeof(Atom *) * arity);
    items[0] = sym(a, head_name);
    for (uint32_t chunk = 0; chunk < nchunks; chunk++) {
        uint32_t chunk_first = first_var + chunk * chunk_size;
        uint32_t chunk_count = chunk_size;
        CettaExprLen chunk_arity;
        Atom **chunk_items;
        if (chunk_first + chunk_count > first_var + var_count)
            chunk_count = (first_var + var_count) - chunk_first;
        chunk_arity = (CettaExprLen)chunk_count + 1u;
        chunk_items = arena_alloc(a, sizeof(Atom *) * chunk_arity);
        chunk_items[0] = sym(a, "chunk");
        for (uint32_t i = 0; i < chunk_count; i++) {
            char name[64];
            uint32_t ordinal = chunk_first + i;
            snprintf(name, sizeof(name), "slot%u", ordinal);
            chunk_items[i + 1u] = var(a, name, (VarId)ordinal + 1u);
        }
        items[chunk + 1u] = atom_expr(a, chunk_items, chunk_arity);
    }
    return atom_expr(a, items, arity);
}

static bool count_bindings_visit(const Bindings *bindings, void *ctx) {
    (void)bindings;
    (*(uint32_t *)ctx)++;
    return true;
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
    assert(space_length64(&imported_space) == 2);
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
    assert(space_length64(&imported_space) == 1);
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
    assert(space_length64(&imported_space) == 2);

    assert(space_match_backend_load_sexpr_chunk(&imported_space, scratch,
                                                (const uint8_t *)grow_text,
                                                strlen(grow_text), &added));
    assert(added == 1);
    assert(space_length64(&imported_space) == 3);

    assert(space_match_backend_remove_sexpr_chunk(&imported_space, scratch,
                                                  (const uint8_t *)remove_text,
                                                  strlen(remove_text), &removed));
    assert(removed == 1);
    assert(space_length64(&imported_space) == 2);

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
    assert(space_length64(&imported_space) == 2);

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

static void test_subst_tree_adversarial_int_fanout(Arena *scratch) {
    enum { KEY_COUNT = 64 };
    SubstBucket bucket;
    int64_t keys[KEY_COUNT];

    stree_bucket_init(&bucket);
    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        int64_t lane = (int64_t)i - (KEY_COUNT / 2);
        keys[i] = lane * (INT64_C(1) << 32);
        stree_bucket_insert(&bucket, atom_int(scratch, keys[i]), i);
    }
    assert(bucket.root != NULL);
    assert(bucket.root->int_hashed);
    assert(bucket.root->int_ht.count == KEY_COUNT);

    /* The old low-32-bit hash placed every one of these keys in one 64-entry
       probe run.  Full-width avalanche must keep the deterministic witness
       well below the adaptive-promotion threshold. */
    uint32_t cap = bucket.root->int_ht.mask + 1u;
    uint32_t run = 0;
    uint32_t max_run = 0;
    for (uint32_t i = 0; i < cap; i++) {
        if (bucket.root->int_ht.entries[i].child) {
            run++;
            if (run > max_run)
                max_run = run;
        } else {
            run = 0;
        }
    }
    assert(max_run < SNODE_HASH_THRESHOLD);

    for (uint32_t i = 0; i < KEY_COUNT; i++) {
        SubstMatchSet matches;
        smset_init(&matches);
        stree_query_bucket(&bucket, scratch, atom_int(scratch, keys[i]),
                           NULL, &matches);
        assert(matches.len == 1);
        assert(matches.items[0].atom_idx == i);
        smset_free(&matches);
    }
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
    assert(space_length64(&space) == (uint32_t)n);
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
    assert(space_length64(&imported_space) == 2);
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
    assert(space_length64(&imported_space) == 3);
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
    assert(space_length64(&imported_space) == 0);

    space_free(&imported_space);
}

static void test_bridge_logical_len64_over_u32_boundary(TermUniverse *universe,
                                                        Arena *scratch) {
    Space imported_space;
    uint64_t huge_len = (uint64_t)UINT32_MAX + 17u;
    Space *clone = NULL;
    Atom *query = NULL;
    uint32_t visited = 0;

    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    reset_bridge_capture();
    g_bridge_size_override = huge_len;
    imported_space.match_backend.pathmap.bridge.bridge_active = true;
    imported_space.match_backend.pathmap.bridge.bridge_space = g_fake_bridge_space;
    imported_space.match_backend.pathmap.bridge.projection_valid = false;

    assert(space_match_backend_native_materialization_limit() > UINT32_MAX);
    assert(space_match_backend_packet_materialization_limit() > UINT32_MAX);
    assert(space_match_backend_logical_len64(&imported_space) == huge_len);
    assert(space_length64(&imported_space) == huge_len);
    {
        uint32_t narrow_len = 7;
        space_match_backend_clear_error();
        assert(!space_length_u32_checked(&imported_space, &narrow_len));
        assert(narrow_len == 0);
        assert(space_match_backend_last_error_code() ==
               SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
        assert(strcmp(space_match_backend_last_error(), "NativeSpaceTooLarge") == 0);
    }
    space_match_backend_clear_error();
    clone = space_heap_clone_shallow(&imported_space);
    assert(clone == NULL);
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
    assert(strcmp(space_match_backend_last_error(), "NativeSpaceTooLarge") == 0);

    g_query_cursor_enabled = true;
    query = expr3(scratch, sym(scratch, "edge"), var(scratch, "x", 1),
                  var(scratch, "y", 2));
    assert(space_match_backend_visit_bindings_direct(
        &imported_space, scratch, query, count_bindings_visit, &visited));
    assert(visited == 1);
    assert(g_query_cursor_pos == 2);

    space_free(&imported_space);
    reset_bridge_capture();
}

static void test_bridge_materialize_packet_over_capacity_reports_packet_too_large(
    TermUniverse *universe) {
    Space imported_space;
    uint64_t packet_limit = space_match_backend_packet_materialization_limit();

    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    reset_bridge_capture();
    assert(packet_limit < UINT64_MAX);
    g_bridge_dump_expr_rows_override = packet_limit + 1u;
    imported_space.match_backend.pathmap.bridge.bridge_active = true;
    imported_space.match_backend.pathmap.bridge.bridge_space = g_fake_bridge_space;
    imported_space.match_backend.pathmap.bridge.projection_valid = false;

    space_match_backend_clear_error();
    assert(!space_match_backend_materialize_native_storage(&imported_space, NULL));
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE);
    assert(strcmp(space_match_backend_last_error(), "PacketTooLarge") == 0);

    space_free(&imported_space);
    reset_bridge_capture();
}

static void
test_bridge_contextual_query_context_count_ceiling_falls_back_materialized(
    Arena *scratch) {
    uint8_t packet[20];
    static const uint8_t ctx_query_foo[] = {
        2u,
        0xC9u, 'c', 't', 'x', '-', 'q', 'u', 'e', 'r', 'y',
        0xC3u, 'f', 'o', 'o',
    };
    uint32_t visited = 0;
    Atom *query = expr2(scratch, sym(scratch, "ctx-query"), var(scratch, "x", 17));

    test_store_u32_be(packet, 0x43544252u);
    test_store_u16_be(packet + 4, 5u);
    test_store_u16_be(packet + 6, 0u);
    test_store_u64_be(packet + 8, 1u);
    test_store_u32_be(packet + 16, 2u);

    reset_bridge_capture();
    space_match_backend_diag_set_packet_materialization_limit_override(1u);
    g_bridge_contextual_query_packet = packet;
    g_bridge_contextual_query_packet_len = sizeof(packet);
    g_bridge_contextual_query_rows = 1u;
    g_bridge_value_bytes[0] = ctx_query_foo;
    g_bridge_value_lens[0] = sizeof(ctx_query_foo);
    g_bridge_value_count = 1u;
    space_match_backend_clear_error();

    assert(space_match_backend_mork_visit_bindings_direct(
        g_fake_bridge_space, scratch, query, count_bindings_visit, &visited));
    assert(visited == 1);
    assert(g_query_cursor_new_calls == 0);
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_NONE);
}

static void
test_bridge_contextual_exact_entry_count_ceiling_falls_back_expr_dump(
    TermUniverse *universe) {
    uint8_t packet[28];
    static const uint8_t wide_pair_a_17[] = {
        0x00u, 0x00u, 0x00u, 0x00u, 0x03u,
        0x01u, 0x00u, 0x00u, 0x00u, 0x04u, 'p', 'a', 'i', 'r',
        0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 'A',
        0x01u, 0x00u, 0x00u, 0x00u, 0x02u, '1', '7',
    };
    Space imported_space;
    Arena check;

    test_store_u32_be(packet, 0x43544252u);
    test_store_u16_be(packet + 4, 5u);
    test_store_u16_be(packet + 6, 0u);
    test_store_u64_be(packet + 8, 1u);
    test_store_u32_be(packet + 16, 1u);
    test_store_u32_be(packet + 20, 7u);
    test_store_u32_be(packet + 24, 2u);

    space_init_with_universe(&imported_space, universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        return;
    }

    reset_bridge_capture();
    space_match_backend_diag_set_packet_materialization_limit_override(1u);
    g_bridge_contextual_exact_packet = packet;
    g_bridge_contextual_exact_packet_len = sizeof(packet);
    g_bridge_contextual_exact_rows = 1u;
    g_bridge_expr_cursor_enabled = false;
    g_bridge_value_bytes[0] = wide_pair_a_17;
    g_bridge_value_lens[0] = sizeof(wide_pair_a_17);
    g_bridge_value_count = 1u;
    imported_space.match_backend.pathmap.bridge.bridge_active = true;
    imported_space.match_backend.pathmap.bridge.bridge_space = g_fake_bridge_space;
    imported_space.match_backend.pathmap.bridge.projection_valid = false;

    arena_init(&check);
    space_match_backend_clear_error();
    assert(space_match_backend_materialize_native_storage(&imported_space, NULL));
    assert(space_length64(&imported_space) == 1u);
    assert(space_get_at64(&imported_space, 0u) != NULL);
    assert(atom_eq(space_get_at64(&imported_space, 0u),
                   expr3(&check, sym(&check, "pair"),
                         sym(&check, "A"), atom_int(&check, 17))));
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_NONE);

    arena_free(&check);
    space_free(&imported_space);
    reset_bridge_capture();
}

static void test_bridge_import_falls_back_to_expr_row_dump(
    TermUniverse *universe,
    Arena *scratch) {
    static const uint8_t wide_pair_b_23[] = {
        0x00u, 0x00u, 0x00u, 0x00u, 0x03u,
        0x01u, 0x00u, 0x00u, 0x00u, 0x04u, 'p', 'a', 'i', 'r',
        0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 'B',
        0x01u, 0x00u, 0x00u, 0x00u, 0x02u, '2', '3',
    };
    Space imported_space;
    uint64_t loaded = 0;
    SpaceTransferEndpoint dst_endpoint;
    SpaceTransferEndpoint src_endpoint;

    space_init_with_universe(&imported_space, universe);

    reset_bridge_capture();
    g_bridge_expr_cursor_enabled = false;
    g_bridge_value_bytes[0] = wide_pair_b_23;
    g_bridge_value_lens[0] = sizeof(wide_pair_b_23);
    g_bridge_value_count = 1u;

    dst_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &imported_space,
    };
    src_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
        .bridge = g_fake_bridge_space,
    };

    assert(space_match_backend_transfer_resolved_result(
               dst_endpoint, src_endpoint, NULL, &loaded) ==
           SPACE_TRANSFER_OK);
    assert(loaded == 1u);
    assert(space_length64(&imported_space) == 1u);

    SubstMatchSet matches;
    smset_init(&matches);
    space_subst_query(&imported_space, scratch,
                      expr3(scratch, sym(scratch, "pair"),
                            sym(scratch, "B"), atom_int(scratch, 23)),
                      &matches);
    assert(matches.len == 1u);
    smset_free(&matches);

    space_free(&imported_space);
    reset_bridge_capture();
}

static void test_bridge_projection_storage_migrates_with_universe(void) {
    SymbolTable symbols;
    Arena persistent;
    Arena scratch;
    TermUniverse universe;
    Space imported_space;
    AtomId imported_id = CETTA_ATOM_ID_NONE;
    Atom *expected = NULL;
    uint8_t *snapshot = NULL;

    init_test_symbols(&symbols);
    arena_init(&persistent);
    arena_init(&scratch);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_diag_set_atom_id_capacity_override(&universe, 8u);

    space_init_with_universe(&imported_space, &universe);
    if (!space_match_backend_try_set(&imported_space, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&imported_space);
        term_universe_free(&universe);
        arena_free(&scratch);
        arena_free(&persistent);
        g_symbols = NULL;
        symbol_table_free(&symbols);
        return;
    }

    {
        Atom *items[3] = {
            sym(&scratch, "pair"),
            sym(&scratch, "A"),
            atom_int(&scratch, 17),
        };
        expected = atom_expr(&scratch, items, 3);
    }
    imported_id = term_universe_store_atom_id(&universe, NULL, expected);
    assert(imported_id != CETTA_ATOM_ID_NONE);
    snapshot = malloc(sizeof(uint32_t));
    assert(snapshot != NULL);
    assert(cetta_atom_id_storage_store_bits(snapshot, 32u, imported_id));

    reset_bridge_capture();
    imported_space.match_backend.pathmap.bridge.bridge_active = true;
    imported_space.match_backend.pathmap.bridge.bridge_space = g_fake_bridge_space;
    imported_space.match_backend.pathmap.bridge.projected_atom_ids = snapshot;
    imported_space.match_backend.pathmap.bridge.projected_len = 1u;
    imported_space.match_backend.pathmap.bridge.projected_atom_id_width_bits = 32u;
    imported_space.match_backend.pathmap.bridge.projection_valid = true;

    assert(imported_space.match_backend.pathmap.bridge.projection_valid);
    assert(imported_space.match_backend.pathmap.bridge.projected_atom_ids != NULL);
    assert(imported_space.match_backend.pathmap.bridge.projected_len == 1u);
    assert(imported_space.match_backend.pathmap.bridge.projected_atom_id_width_bits ==
           32u);
    assert(cetta_atom_id_storage_load_bits(
               imported_space.match_backend.pathmap.bridge.projected_atom_ids,
               imported_space.match_backend.pathmap.bridge.projected_atom_id_width_bits) ==
           imported_id);
    assert(space_get_atom_id_at64(&imported_space, 0u) == imported_id);
    assert(space_get_at64(&imported_space, 0u) != NULL);
    assert(atom_eq(space_get_at64(&imported_space, 0u), expected));
    while (term_universe_store_format(&universe) ==
           TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1) {
        static int64_t seed = 1000;
        AtomId extra = term_universe_store_atom_id(&universe, NULL,
                                                   atom_int(&scratch, seed++));
        assert(extra != CETTA_ATOM_ID_NONE);
    }

    assert(term_universe_store_format(&universe) ==
           TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1);
    assert(imported_space.match_backend.pathmap.bridge.projected_atom_id_width_bits ==
           64u);
    assert(cetta_atom_id_storage_load_bits(
               imported_space.match_backend.pathmap.bridge.projected_atom_ids,
               imported_space.match_backend.pathmap.bridge.projected_atom_id_width_bits) ==
           imported_id);
    assert(space_get_atom_id_at64(&imported_space, 0u) == imported_id);
    assert(space_match_backend_materialize_native_storage(&imported_space, &scratch));
    assert(imported_space.native.atom_id_width_bits == 64u);
    assert(imported_space.native.len == 1u);
    assert(space_get_atom_id_at64(&imported_space, 0u) == imported_id);
    assert(space_get_at64(&imported_space, 0u) != NULL);
    assert(atom_eq(space_get_at64(&imported_space, 0u), expected));

    space_free(&imported_space);
    term_universe_free(&universe);
    reset_bridge_capture();
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
}

static void test_binding_set_growth_has_named_ceiling(void) {
    BindingSet set;
    Bindings empty;
    Bindings moved;

    binding_set_init(&set);
    bindings_init(&empty);
    assert(CETTA_BINDING_SET_MAX_ROWS == UINT64_MAX);
    assert(binding_set_push(&set, &empty));
    assert(set.len == 1);
    binding_set_free(&set);
    bindings_free(&empty);

    binding_set_init(&set);
    bindings_init(&empty);
    set.len = CETTA_BINDING_SET_MAX_ROWS;
    set.cap = CETTA_BINDING_SET_MAX_ROWS;
    assert(!binding_set_push(&set, &empty));
    set.len = 0;
    set.cap = 0;
    binding_set_free(&set);
    bindings_free(&empty);

    binding_set_init(&set);
    bindings_init(&moved);
    set.len = CETTA_BINDING_SET_MAX_ROWS;
    set.cap = CETTA_BINDING_SET_MAX_ROWS;
    assert(!binding_set_push_move(&set, &moved));
    set.len = 0;
    set.cap = 0;
    binding_set_free(&set);
    bindings_free(&moved);
}

static void
test_space_to_space_large_logical_transfer_fallback_reports_native_space_too_large(
    TermUniverse *universe) {
    Space dst;
    Space src;
    uint64_t added = 999u;
    uint64_t huge_len = (uint64_t)UINT32_MAX + 17u;
    SpaceTransferEndpoint dst_endpoint;
    SpaceTransferEndpoint src_endpoint;

    space_init_with_universe(&dst, universe);
    space_init_with_universe(&src, universe);
    if (!space_match_backend_try_set(&src, SPACE_ENGINE_PATHMAP)) {
        printf("SKIP: PATHMAP unavailable in this build\n");
        space_free(&src);
        space_free(&dst);
        return;
    }

    reset_bridge_capture();
    g_bridge_size_override = huge_len;
    src.match_backend.pathmap.bridge.bridge_active = true;
    src.match_backend.pathmap.bridge.bridge_space = g_fake_bridge_space;
    src.match_backend.pathmap.bridge.projection_valid = false;

    dst_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &dst,
    };
    src_endpoint = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .space = &src,
    };

    space_match_backend_clear_error();
    assert(space_match_backend_transfer_resolved_result(
               dst_endpoint, src_endpoint, NULL, &added) ==
           SPACE_TRANSFER_ERROR);
    assert(added == 0);
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
    assert(strcmp(space_match_backend_last_error(), "NativeSpaceTooLarge") == 0);

    space_free(&src);
    space_free(&dst);
    reset_bridge_capture();
}

static void test_mork_direct_query_slot_ceiling_falls_back_materialized(
    Arena *scratch
) {
    static const uint8_t direct_slot_row[] = {
        2u,
        0xD4u, 'd', 'i', 'r', 'e', 'c', 't', '-', 's', 'l', 'o', 't', '-',
        'o', 'v', 'e', 'r', 'f', 'l', 'o', 'w',
        5u,
        0xC5u, 'c', 'h', 'u', 'n', 'k',
        0xC1u, 'A',
        0xC1u, 'B',
        0xC1u, 'C',
        0xC1u, 'D',
    };
    uint32_t visited = 0;
    Atom *query = large_var_query(scratch, "direct-slot-overflow", 0u, 4u);

    reset_bridge_capture();
    g_bridge_value_bytes[0] = direct_slot_row;
    g_bridge_value_lens[0] = sizeof(direct_slot_row);
    g_bridge_value_count = 1u;
    space_match_backend_diag_set_contextual_query_slot_limit_override(3u);
    space_match_backend_clear_error();

    assert(space_match_backend_mork_visit_bindings_direct(
        g_fake_bridge_space, scratch, query, count_bindings_visit, &visited));
    assert(visited == 1);
    assert(g_query_cursor_new_calls == 0);
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_NONE);
}

static void test_mork_conjunction_slot_overflow_falls_back_iterative(
    Arena *scratch
) {
    uint32_t visited = 0;
    Atom *patterns[2] = {
        large_var_query(scratch, "iter-slot-left", 0u, 3u),
        large_var_query(scratch, "iter-slot-right", 3u, 3u),
    };

    reset_bridge_capture();
    g_query_cursor_enabled = true;
    space_match_backend_diag_set_contextual_query_slot_limit_override(4u);
    space_match_backend_clear_error();

    assert(space_match_backend_mork_visit_conjunction_direct(
        g_fake_bridge_space, scratch, patterns, 2u, NULL,
        count_bindings_visit, &visited));
    assert(visited == 1);
    assert(g_query_cursor_new_calls == 2);
    assert(space_match_backend_last_error_code() ==
           SPACE_MATCH_BACKEND_ERROR_NONE);
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
    assert(space_length64(&dst) == 1);

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
    if (!space_match_backend_try_set(&dst, SPACE_ENGINE_PATHMAP) ||
        !space_match_backend_try_set(&src, SPACE_ENGINE_PATHMAP)) {
        space_free(&src);
        space_free(&dst);
        return;
    }

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
    assert(space_length64(&dst) == 0);

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
    assert(space_length64(clone) == 3);
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

static void test_subst_match_normalize_compacted_duplicate_runs(Arena *scratch) {
    SubstMatchSet matches;
    Bindings a;
    Bindings b;
    Atom *a_var = atom_var_with_id(scratch, "normalize-a", 91001u);
    Atom *b_var = atom_var_with_id(scratch, "normalize-b", 91002u);

    bindings_init(&a);
    bindings_init(&b);
    assert(bindings_add_var(&a, a_var, atom_int(scratch, 1)));
    assert(bindings_add_var(&b, b_var, atom_int(scratch, 2)));

    smset_init(&matches);
    matches.items = cetta_malloc(sizeof(*matches.items) * 4u);
    matches.cap = 4u;
    matches.len = 4u;
    for (CettaIndex i = 0; i < matches.len; i++) {
        const Bindings *source = i < 2u ? &a : &b;
        matches.items[i].atom_idx = i < 2u ? 10u : 20u;
        matches.items[i].epoch = i < 2u ? 101u : 202u;
        matches.items[i].exact = false;
        assert(bindings_clone(&matches.items[i].bindings, source));
    }

    space_match_backend_diag_normalize_subst_matches(&matches);
    assert(matches.len == 2u);
    assert(matches.items[0].atom_idx == 10u);
    assert(matches.items[1].atom_idx == 20u);
    assert(bindings_eq(&matches.items[0].bindings, &a));
    assert(bindings_eq(&matches.items[1].bindings, &b));

    smset_free(&matches);
    bindings_free(&b);
    bindings_free(&a);
}

static void test_leaf_patch_refusal_is_transactional(Arena *scratch) {
    const uint32_t epoch = 73u;
    Atom *x = atom_var_with_id(scratch, "leaf-x", 92001u);
    Atom *y = atom_var_with_id(scratch, "leaf-y", 92002u);
    Atom *epoch_y =
        atom_var_like(scratch, y, var_epoch_id(y->var_id, epoch));
    Atom *lhs = expr3(scratch, sym(scratch, "leaf-f"), x, y);
    Atom *query = expr3(scratch, sym(scratch, "leaf-f"),
                        atom_int(scratch, 1), atom_int(scratch, 2));
    Bindings base;

    bindings_init(&base);
    /* Keep an unresolved structural equality which permits x := 1 but rejects
       y := 2.  The fast path must not leak the successful first binding into
       the general-matcher fallback when the second binding is rejected. */
    assert(bindings_add_constraint(
        &base,
        expr2(scratch, sym(scratch, "leaf-wrap"), epoch_y),
        expr2(scratch, sym(scratch, "leaf-wrap"), atom_int(scratch, 99))));
    assert(base.len == 0u);
    assert(base.eq_len == 1u);

    assert(!match_atoms_epoch_positional_linear(
        query, lhs, &base, scratch, epoch));
    assert(base.len == 0u);
    assert(base.eq_len == 1u);
    assert(bindings_lookup_id(&base, var_epoch_id(x->var_id, epoch)) == NULL);

    bindings_free(&base);
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
    test_subst_tree_adversarial_int_fanout(&scratch);
    test_parser_direct_add_boundary(&universe, &scratch);
    test_bridge_structural_import_boundary(&universe, &scratch);
    test_pathmap_no_universe_import_boundary();
    test_bridge_logical_len64_over_u32_boundary(&universe, &scratch);
    test_bridge_materialize_packet_over_capacity_reports_packet_too_large(&universe);
    test_bridge_contextual_query_context_count_ceiling_falls_back_materialized(
        &scratch);
    test_bridge_contextual_exact_entry_count_ceiling_falls_back_expr_dump(
        &universe);
    test_bridge_import_falls_back_to_expr_row_dump(&universe, &scratch);
    test_bridge_projection_storage_migrates_with_universe();
    test_binding_set_growth_has_named_ceiling();
    test_space_to_space_large_logical_transfer_fallback_reports_native_space_too_large(
        &universe);
    test_mork_direct_query_slot_ceiling_falls_back_materialized(&scratch);
    test_mork_conjunction_slot_overflow_falls_back_iterative(&scratch);
    test_space_to_space_bridge_unavailable_falls_back(&universe, &scratch);
    test_space_to_space_bridge_attempt_failure_is_error(&universe);
    test_space_clone_direct_id_boundary(&universe, &scratch);
    test_subst_match_normalize_compacted_duplicate_runs(&scratch);
    test_leaf_patch_refusal_is_transactional(&scratch);

    term_universe_free(&universe);
    reset_bridge_capture();
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    puts("PASS: term universe backend add abi");
    return 0;
}
