#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "atom.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "parser.h"
#include "symbol.h"

#define IMPORTED_MORK_QUERY_ONLY_V2_MAGIC 0x43544252u
#define IMPORTED_MORK_QUERY_ONLY_V2_VERSION 2u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY 0x0001u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES 0x0002u
#define IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS 0x0010u
#define IMPORTED_MORK_TAG_VARREF_MASK 0xC0u
#define IMPORTED_MORK_TAG_VARREF_PREFIX 0x80u
#define IMPORTED_MORK_TAG_SYMBOL_PREFIX 0xC0u
#define IMPORTED_MORK_TAG_NEWVAR 0xC0u
#define IMPORTED_MORK_WIDE_TAG_ARITY 0x00u
#define IMPORTED_MORK_WIDE_TAG_SYMBOL 0x01u
#define IMPORTED_MORK_WIDE_TAG_NEWVAR 0x02u
#define IMPORTED_MORK_WIDE_TAG_VARREF 0x03u

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} ByteBuf;

typedef struct {
    uint64_t rows;
    uint64_t refs;
    uint64_t bindings;
    uint64_t expr_bytes;
} QueryPacketSummary;

typedef struct {
    const char *name;
    const char *query;
    uint64_t expected_rows;
    uint64_t expected_bindings;
} QueryScenario;

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void fail_bridge(const char *ctx) {
    fprintf(stderr, "error: %s: %s\n", ctx, cetta_mork_bridge_last_error());
    exit(1);
}

static void byte_buf_init(ByteBuf *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void byte_buf_free(ByteBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void byte_buf_reserve(ByteBuf *buf, size_t needed) {
    if (needed <= buf->cap)
        return;
    size_t new_cap = buf->cap ? buf->cap : 256;
    while (new_cap < needed)
        new_cap *= 2;
    uint8_t *grown = realloc(buf->data, new_cap);
    if (!grown) {
        perror("realloc");
        exit(1);
    }
    buf->data = grown;
    buf->cap = new_cap;
}

static void byte_buf_append(ByteBuf *buf, const uint8_t *src, size_t len) {
    byte_buf_reserve(buf, buf->len + len);
    memcpy(buf->data + buf->len, src, len);
    buf->len += len;
}

static void byte_buf_append_u32_be(ByteBuf *buf, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)((value >> 24) & 0xffu),
        (uint8_t)((value >> 16) & 0xffu),
        (uint8_t)((value >> 8) & 0xffu),
        (uint8_t)(value & 0xffu),
    };
    byte_buf_append(buf, bytes, sizeof(bytes));
}

static void init_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
}

static void cleanup_symbols(SymbolTable *symbols) {
    g_var_intern = NULL;
    g_hashcons = NULL;
    g_symbols = NULL;
    symbol_table_free(symbols);
}

static Atom *make_row_atom(Arena *a,
                           SymbolId row_sym,
                           SymbolId payload_sym,
                           SymbolId name_sym,
                           SymbolId age_sym,
                           SymbolId sam_sym,
                           int64_t value) {
    Atom *name_elems[2] = {
        atom_symbol_id(a, name_sym),
        atom_symbol_id(a, sam_sym),
    };
    Atom *age_elems[2] = {
        atom_symbol_id(a, age_sym),
        atom_int(a, value),
    };
    Atom *payload_elems[3] = {
        atom_symbol_id(a, payload_sym),
        atom_expr(a, name_elems, 2),
        atom_expr(a, age_elems, 2),
    };
    Atom *row_elems[3] = {
        atom_symbol_id(a, row_sym),
        atom_int(a, value),
        atom_expr(a, payload_elems, 3),
    };
    return atom_expr(a, row_elems, 3);
}

static void lower_row_atom(Arena *a,
                           SymbolId row_sym,
                           SymbolId payload_sym,
                           SymbolId name_sym,
                           SymbolId age_sym,
                           SymbolId sam_sym,
                           int64_t value,
                           uint8_t **out_bytes,
                           size_t *out_len) {
    const char *error = NULL;
    Atom *atom = make_row_atom(a, row_sym, payload_sym, name_sym, age_sym,
                               sam_sym, value);
    if (!cetta_mm2_atom_to_bridge_expr_bytes(a, atom, out_bytes, out_len, &error)) {
        fprintf(stderr, "error: bridge expr-byte lowering failed: %s\n",
                error ? error : "unknown lowering error");
        exit(1);
    }
}

static CettaMorkSpaceHandle *build_query_space(size_t n,
                                               SymbolId row_sym,
                                               SymbolId payload_sym,
                                               SymbolId name_sym,
                                               SymbolId age_sym,
                                               SymbolId sam_sym) {
    Arena scratch;
    ByteBuf packet;
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
    if (!space)
        fail_bridge("cetta_mork_bridge_space_new");
    arena_init(&scratch);
    byte_buf_init(&packet);
    for (size_t i = 0; i < n; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint8_t *expr_bytes = NULL;
        size_t expr_len = 0;
        lower_row_atom(&scratch, row_sym, payload_sym, name_sym, age_sym,
                       sam_sym, (int64_t)i, &expr_bytes, &expr_len);
        if (expr_len > UINT32_MAX) {
            fprintf(stderr, "error: expr length exceeds batch packet limit\n");
            exit(1);
        }
        byte_buf_append_u32_be(&packet, (uint32_t)expr_len);
        byte_buf_append(&packet, expr_bytes, expr_len);
        free(expr_bytes);
        arena_reset(&scratch, mark);
    }
    if (!cetta_mork_bridge_space_add_expr_bytes_batch(space,
                                                      packet.data,
                                                      packet.len,
                                                      NULL)) {
        fail_bridge("cetta_mork_bridge_space_add_expr_bytes_batch");
    }
    byte_buf_free(&packet);
    arena_free(&scratch);
    return space;
}

static void assert_space_size(CettaMorkSpaceHandle *space, uint64_t expected,
                              const char *ctx) {
    uint64_t got = 0;
    if (!cetta_mork_bridge_space_size(space, &got))
        fail_bridge(ctx);
    if (got != expected) {
        fprintf(stderr, "error: %s: expected size %" PRIu64 ", got %" PRIu64 "\n",
                ctx, expected, got);
        exit(1);
    }
}

static bool read_u32_be(const uint8_t *packet, size_t len, size_t *off,
                        uint32_t *out) {
    if (*off + 4 > len)
        return false;
    *out = ((uint32_t)packet[*off] << 24) |
           ((uint32_t)packet[*off + 1] << 16) |
           ((uint32_t)packet[*off + 2] << 8) |
           (uint32_t)packet[*off + 3];
    *off += 4;
    return true;
}

static bool read_u16_be(const uint8_t *packet, size_t len, size_t *off,
                        uint16_t *out) {
    if (*off + 2 > len)
        return false;
    *out = (uint16_t)(((uint16_t)packet[*off] << 8) |
                      (uint16_t)packet[*off + 1]);
    *off += 2;
    return true;
}

static Atom *parse_token_bytes(Arena *a,
                               const uint8_t *bytes,
                               uint32_t len,
                               bool exact_len_reliable,
                               bool *ok) {
    if (!ok || !*ok) {
        if (ok)
            *ok = false;
        return NULL;
    }
    char *tok = arena_alloc(a, (size_t)len + 1);
    memcpy(tok, bytes, len);
    tok[len] = '\0';
    if (!exact_len_reliable && len == 63) {
        *ok = false;
        return NULL;
    }
    if (len > 0 && (tok[0] == '"' || tok[len - 1] == '"')) {
        size_t pos = 0;
        Atom *parsed = parse_sexpr(a, tok, &pos);
        if (parsed && pos == len &&
            parsed->kind == ATOM_GROUNDED &&
            parsed->ground.gkind == GV_STRING)
            return parsed;
        *ok = false;
        return NULL;
    }
    if (strcmp(tok, "True") == 0)
        return atom_bool(a, true);
    if (strcmp(tok, "False") == 0)
        return atom_bool(a, false);
    if (strcmp(tok, "PI") == 0)
        return atom_float(a, 3.14159265358979323846);
    if (strcmp(tok, "EXP") == 0)
        return atom_float(a, 2.71828182845904523536);

    char *endp = NULL;
    errno = 0;
    long long ival = strtoll(tok, &endp, 10);
    if (*endp == '\0' && errno == 0)
        return atom_int(a, (int64_t)ival);

    if (strchr(tok, '.')) {
        char *fendp = NULL;
        errno = 0;
        double fval = strtod(tok, &fendp);
        if (*fendp == '\0' && errno == 0)
            return atom_float(a, fval);
    }

    const char *canonical = parser_canonicalize_namespace_token(a, tok);
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols, canonical));
}

static Atom *decode_value_raw_v2_rec(Arena *a,
                                     const uint8_t *expr,
                                     size_t len,
                                     size_t *off,
                                     bool *ok) {
    if (!expr || !off || !ok || !*ok || *off >= len) {
        if (ok)
            *ok = false;
        return NULL;
    }

    uint8_t tag = expr[*off];
    if (tag == IMPORTED_MORK_TAG_NEWVAR ||
        (tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_VARREF_PREFIX) {
        *ok = false;
        (*off)++;
        return NULL;
    }

    if ((tag & IMPORTED_MORK_TAG_VARREF_MASK) == IMPORTED_MORK_TAG_SYMBOL_PREFIX) {
        uint32_t sym_len = (uint32_t)(tag & 0x3Fu);
        if (sym_len == 0 || *off + 1u + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = parse_token_bytes(a, expr + *off + 1u, sym_len, false, ok);
        if (!*ok)
            return NULL;
        *off += 1u + sym_len;
        return atom;
    }

    uint32_t arity = (uint32_t)(tag & 0x3Fu);
    (*off)++;
    Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
    for (uint32_t i = 0; i < arity; i++) {
        elems[i] = decode_value_raw_v2_rec(a, expr, len, off, ok);
        if (!*ok)
            return NULL;
    }
    return atom_expr(a, elems, arity);
}

static Atom *decode_value_wide_v2_rec(Arena *a,
                                      const uint8_t *expr,
                                      size_t len,
                                      size_t *off,
                                      bool *ok) {
    if (!expr || !off || !ok || !*ok || *off >= len) {
        if (ok)
            *ok = false;
        return NULL;
    }

    uint8_t tag = expr[(*off)++];
    switch (tag) {
    case IMPORTED_MORK_WIDE_TAG_NEWVAR:
        *ok = false;
        return NULL;
    case IMPORTED_MORK_WIDE_TAG_VARREF:
        if (*off >= len) {
            *ok = false;
            return NULL;
        }
        (*off)++;
        *ok = false;
        return NULL;
    case IMPORTED_MORK_WIDE_TAG_SYMBOL: {
        uint32_t sym_len = 0;
        if (!read_u32_be(expr, len, off, &sym_len) ||
            sym_len == 0 || *off + sym_len > len) {
            *ok = false;
            return NULL;
        }
        Atom *atom = parse_token_bytes(a, expr + *off, sym_len, true, ok);
        if (!*ok)
            return NULL;
        *off += sym_len;
        return atom;
    }
    case IMPORTED_MORK_WIDE_TAG_ARITY: {
        uint32_t arity = 0;
        if (!read_u32_be(expr, len, off, &arity)) {
            *ok = false;
            return NULL;
        }
        Atom **elems = arity ? arena_alloc(a, sizeof(Atom *) * arity) : NULL;
        for (uint32_t i = 0; i < arity; i++) {
            elems[i] = decode_value_wide_v2_rec(a, expr, len, off, ok);
            if (!*ok)
                return NULL;
        }
        return atom_expr(a, elems, arity);
    }
    default:
        *ok = false;
        return NULL;
    }
}

static Atom *decode_value_raw_v2(Arena *a,
                                 const uint8_t *expr,
                                 uint32_t expr_len,
                                 uint8_t value_env,
                                 uint8_t value_flags,
                                 bool wide_tokens,
                                 bool *ok) {
    if (!ok || !*ok || !expr || expr_len == 0) {
        if (ok)
            *ok = false;
        return NULL;
    }
    if (!(value_flags & 0x01u)) {
        *ok = false;
        return NULL;
    }
    (void)value_env;

    size_t off = 0;
    Atom *value = wide_tokens
        ? decode_value_wide_v2_rec(a, expr, expr_len, &off, ok)
        : decode_value_raw_v2_rec(a, expr, expr_len, &off, ok);
    if (!*ok || off != expr_len) {
        *ok = false;
        return NULL;
    }
    return value;
}

static bool scan_v2_packet(const uint8_t *packet,
                           size_t packet_len,
                           uint64_t expected_rows,
                           uint64_t expected_bindings,
                           bool decode_values,
                           QueryPacketSummary *summary) {
    size_t off = 0;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t parsed_rows = 0;
    QueryPacketSummary local = {0};
    Arena decode_scratch;
    bool wide_tokens = false;

    if (decode_values)
        arena_init(&decode_scratch);

    if (!read_u32_be(packet, packet_len, &off, &magic) ||
        !read_u16_be(packet, packet_len, &off, &version) ||
        !read_u16_be(packet, packet_len, &off, &flags) ||
        !read_u32_be(packet, packet_len, &off, &parsed_rows)) {
        if (decode_values)
            arena_free(&decode_scratch);
        return false;
    }

    if (magic != IMPORTED_MORK_QUERY_ONLY_V2_MAGIC ||
        version != IMPORTED_MORK_QUERY_ONLY_V2_VERSION ||
        parsed_rows != expected_rows) {
        if (decode_values)
            arena_free(&decode_scratch);
        return false;
    }
    wide_tokens =
        (flags & IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS) != 0;
    if ((flags & ~(IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES |
                   IMPORTED_MORK_QUERY_ONLY_V2_FLAG_WIDE_TOKENS)) != 0 ||
        (flags & (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
                  IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) !=
            (IMPORTED_MORK_QUERY_ONLY_V2_FLAG_QUERY_KEYS_ONLY |
             IMPORTED_MORK_QUERY_ONLY_V2_FLAG_RAW_EXPR_BYTES)) {
        if (decode_values)
            arena_free(&decode_scratch);
        return false;
    }

    local.rows = parsed_rows;
    for (uint32_t row = 0; row < parsed_rows; row++) {
        uint32_t ref_count = 0;
        uint32_t binding_count = 0;
        if (!read_u32_be(packet, packet_len, &off, &ref_count) ||
            off + (size_t)ref_count * 4u > packet_len) {
            if (decode_values)
                arena_free(&decode_scratch);
            return false;
        }
        off += (size_t)ref_count * 4u;
        local.refs += ref_count;

        if (!read_u32_be(packet, packet_len, &off, &binding_count)) {
            if (decode_values)
                arena_free(&decode_scratch);
            return false;
        }
        local.bindings += binding_count;

        ArenaMark row_mark = {0};
        if (decode_values)
            row_mark = arena_mark(&decode_scratch);

        for (uint32_t bi = 0; bi < binding_count; bi++) {
            uint16_t query_slot = 0;
            uint8_t value_env = 0;
            uint8_t value_flags = 0;
            uint32_t expr_len = 0;
            if (!read_u16_be(packet, packet_len, &off, &query_slot) ||
                off + 2 > packet_len) {
                if (decode_values)
                    arena_free(&decode_scratch);
                return false;
            }
            value_env = packet[off++];
            value_flags = packet[off++];
            if (!read_u32_be(packet, packet_len, &off, &expr_len) ||
                off + expr_len > packet_len) {
                if (decode_values)
                    arena_free(&decode_scratch);
                return false;
            }
            (void)query_slot;
            local.expr_bytes += expr_len;
            if (decode_values) {
                bool ok = true;
                Atom *value = decode_value_raw_v2(&decode_scratch,
                                                 packet + off,
                                                 expr_len,
                                                 value_env,
                                                 value_flags,
                                                 wide_tokens,
                                                 &ok);
                if (!ok || !value) {
                    arena_free(&decode_scratch);
                    return false;
                }
            }
            off += expr_len;
        }

        if (decode_values)
            arena_reset(&decode_scratch, row_mark);
    }

    if (decode_values)
        arena_free(&decode_scratch);

    if (off != packet_len || local.bindings != expected_bindings)
        return false;
    if (summary)
        *summary = local;
    return true;
}

static double run_v1_packet_only(CettaMorkSpaceHandle *space,
                                 const QueryScenario *scenario,
                                 int repeat,
                                 size_t *out_packet_len) {
    double total_seconds = 0.0;
    size_t packet_len_sample = 0;
    for (int rep = 0; rep < repeat; rep++) {
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint32_t rows = 0;
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_query_bindings_text(space,
                                                         scenario->query,
                                                         &packet,
                                                         &packet_len,
                                                         &rows)) {
            fail_bridge("cetta_mork_bridge_space_query_bindings_text");
        }
        uint64_t finished = monotonic_ns();
        if (rows != scenario->expected_rows) {
            fprintf(stderr, "error: v1 packet rows mismatch for %s: expected %" PRIu64 ", got %" PRIu32 "\n",
                    scenario->name, scenario->expected_rows, rows);
            exit(1);
        }
        if (rep == 0)
            packet_len_sample = packet_len;
        cetta_mork_bridge_bytes_free(packet, packet_len);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (out_packet_len)
        *out_packet_len = packet_len_sample;
    return total_seconds / (double)repeat;
}

static double run_v2_packet_only(CettaMorkSpaceHandle *space,
                                 const QueryScenario *scenario,
                                 int repeat,
                                 size_t *out_packet_len) {
    double total_seconds = 0.0;
    size_t packet_len_sample = 0;
    for (int rep = 0; rep < repeat; rep++) {
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint32_t rows = 0;
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_query_bindings_query_only_v2(space,
                                                                  (const uint8_t *)scenario->query,
                                                                  strlen(scenario->query),
                                                                  &packet,
                                                                  &packet_len,
                                                                  &rows)) {
            fail_bridge("cetta_mork_bridge_space_query_bindings_query_only_v2");
        }
        uint64_t finished = monotonic_ns();
        if (rows != scenario->expected_rows) {
            fprintf(stderr, "error: v2 packet rows mismatch for %s: expected %" PRIu64 ", got %" PRIu32 "\n",
                    scenario->name, scenario->expected_rows, rows);
            exit(1);
        }
        if (rep == 0)
            packet_len_sample = packet_len;
        cetta_mork_bridge_bytes_free(packet, packet_len);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (out_packet_len)
        *out_packet_len = packet_len_sample;
    return total_seconds / (double)repeat;
}

static double run_v2_scan_mode(CettaMorkSpaceHandle *space,
                               const QueryScenario *scenario,
                               int repeat,
                               bool decode_values,
                               size_t *out_packet_len,
                               QueryPacketSummary *out_summary) {
    double total_seconds = 0.0;
    size_t packet_len_sample = 0;
    QueryPacketSummary summary_sample = {0};
    for (int rep = 0; rep < repeat; rep++) {
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint32_t rows = 0;
        QueryPacketSummary summary = {0};
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_query_bindings_query_only_v2(space,
                                                                  (const uint8_t *)scenario->query,
                                                                  strlen(scenario->query),
                                                                  &packet,
                                                                  &packet_len,
                                                                  &rows)) {
            fail_bridge("cetta_mork_bridge_space_query_bindings_query_only_v2");
        }
        if (!scan_v2_packet(packet,
                            packet_len,
                            scenario->expected_rows,
                            scenario->expected_bindings,
                            decode_values,
                            &summary)) {
            fprintf(stderr, "error: v2 %s decode failed for %s\n",
                    decode_values ? "decode" : "scan", scenario->name);
            exit(1);
        }
        uint64_t finished = monotonic_ns();
        if (rows != scenario->expected_rows) {
            fprintf(stderr, "error: v2 %s rows mismatch for %s: expected %" PRIu64 ", got %" PRIu32 "\n",
                    decode_values ? "decode" : "scan",
                    scenario->name, scenario->expected_rows, rows);
            exit(1);
        }
        if (rep == 0) {
            packet_len_sample = packet_len;
            summary_sample = summary;
        }
        cetta_mork_bridge_bytes_free(packet, packet_len);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (out_packet_len)
        *out_packet_len = packet_len_sample;
    if (out_summary)
        *out_summary = summary_sample;
    return total_seconds / (double)repeat;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    SymbolId row_sym;
    SymbolId payload_sym;
    SymbolId name_sym;
    SymbolId age_sym;
    SymbolId sam_sym;
    size_t n = 100000;
    int repeat = 3;
    CettaMorkSpaceHandle *space = NULL;
    char exact_hit_query[64];
    char exact_miss_query[64];
    QueryScenario scenarios[3];

    if (argc >= 2) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || parsed <= 42) {
            fprintf(stderr, "usage: %s [count>42] [repeat]\n", argv[0]);
            return 2;
        }
        n = (size_t)parsed;
    }
    if (argc >= 3) {
        char *end = NULL;
        long parsed = strtol(argv[2], &end, 10);
        if (!end || *end != '\0' || parsed <= 0) {
            fprintf(stderr, "usage: %s [count>42] [repeat]\n", argv[0]);
            return 2;
        }
        repeat = (int)parsed;
    }

    init_symbols(&symbols);
    row_sym = symbol_intern_cstr(g_symbols, "row");
    payload_sym = symbol_intern_cstr(g_symbols, "payload");
    name_sym = symbol_intern_cstr(g_symbols, "name");
    age_sym = symbol_intern_cstr(g_symbols, "age");
    sam_sym = symbol_intern_cstr(g_symbols, "sam");

    space = build_query_space(n, row_sym, payload_sym, name_sym, age_sym, sam_sym);
    assert_space_size(space, (uint64_t)n, "query bench size");

    snprintf(exact_hit_query, sizeof(exact_hit_query), "(row 42 $p)");
    snprintf(exact_miss_query, sizeof(exact_miss_query), "(row %" PRIu64 " $p)",
             (uint64_t)n + 7u);

    scenarios[0] = (QueryScenario){
        .name = "exact-hit",
        .query = exact_hit_query,
        .expected_rows = 1,
        .expected_bindings = 1,
    };
    scenarios[1] = (QueryScenario){
        .name = "exact-miss",
        .query = exact_miss_query,
        .expected_rows = 0,
        .expected_bindings = 0,
    };
    scenarios[2] = (QueryScenario){
        .name = "full-scan",
        .query = "(row $k $p)",
        .expected_rows = (uint64_t)n,
        .expected_bindings = (uint64_t)n * 2u,
    };

    printf("%-20s %-12s %8s %10s %10s %10s\n",
           "mode", "scenario", "rows", "bindings", "packet_b", "avg_s");
    printf("%-20s %-12s %8s %10s %10s %10s\n",
           "--------------------", "------------", "--------", "----------",
           "----------", "----------");

    for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
        size_t packet_len = 0;
        QueryPacketSummary summary = {0};
        double avg = 0.0;

        avg = run_v1_packet_only(space, &scenarios[i], repeat, &packet_len);
        printf("%-20s %-12s %8" PRIu64 " %10" PRIu64 " %10zu %10.6f\n",
               "v1-text-packet",
               scenarios[i].name,
               scenarios[i].expected_rows,
               scenarios[i].expected_bindings,
               packet_len,
               avg);

        avg = run_v2_packet_only(space, &scenarios[i], repeat, &packet_len);
        printf("%-20s %-12s %8" PRIu64 " %10" PRIu64 " %10zu %10.6f\n",
               "v2-packet-only",
               scenarios[i].name,
               scenarios[i].expected_rows,
               scenarios[i].expected_bindings,
               packet_len,
               avg);

        avg = run_v2_scan_mode(space, &scenarios[i], repeat, false,
                               &packet_len, &summary);
        printf("%-20s %-12s %8" PRIu64 " %10" PRIu64 " %10zu %10.6f\n",
               "v2-scan",
               scenarios[i].name,
               scenarios[i].expected_rows,
               scenarios[i].expected_bindings,
               packet_len,
               avg);

        avg = run_v2_scan_mode(space, &scenarios[i], repeat, true,
                               &packet_len, &summary);
        printf("%-20s %-12s %8" PRIu64 " %10" PRIu64 " %10zu %10.6f\n",
               "v2-decode",
               scenarios[i].name,
               summary.rows,
               summary.bindings,
               packet_len,
               avg);
    }

    cetta_mork_bridge_space_free(space);
    cleanup_symbols(&symbols);
    return 0;
}
