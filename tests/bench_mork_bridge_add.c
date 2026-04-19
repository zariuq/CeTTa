#define _POSIX_C_SOURCE 200809L

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
#include "stats.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

typedef struct {
    uint8_t *data;
    size_t len;
} ByteSpan;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} ByteBuf;

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

static Atom *make_friend_atom(Arena *a, SymbolId friend_sym,
                              SymbolId sam_sym, int64_t value) {
    Atom *elems[3] = {
        atom_symbol_id(a, friend_sym),
        atom_symbol_id(a, sam_sym),
        atom_int(a, value),
    };
    return atom_expr(a, elems, 3);
}

static void lower_friend_atom(Arena *a, SymbolId friend_sym, SymbolId sam_sym,
                              int64_t value, uint8_t **out_bytes,
                              size_t *out_len) {
    const char *error = NULL;
    Atom *atom = make_friend_atom(a, friend_sym, sam_sym, value);
    if (!cetta_mm2_atom_to_bridge_expr_bytes(a, atom, out_bytes, out_len, &error)) {
        fprintf(stderr, "error: bridge expr-byte lowering failed: %s\n",
                error ? error : "unknown lowering error");
        exit(1);
    }
}

static ByteSpan *preencode_friend_exprs(size_t n, SymbolId friend_sym,
                                        SymbolId sam_sym, ByteBuf *packet_out) {
    Arena scratch;
    ByteSpan *exprs = calloc(n, sizeof(ByteSpan));
    if (!exprs) {
        perror("calloc");
        exit(1);
    }
    arena_init(&scratch);
    byte_buf_init(packet_out);
    for (size_t i = 0; i < n; i++) {
        ArenaMark mark = arena_mark(&scratch);
        lower_friend_atom(&scratch, friend_sym, sam_sym, (int64_t)i,
                          &exprs[i].data, &exprs[i].len);
        if (exprs[i].len > UINT32_MAX) {
            fprintf(stderr, "error: expr length exceeds batch packet limit\n");
            exit(1);
        }
        byte_buf_append_u32_be(packet_out, (uint32_t)exprs[i].len);
        byte_buf_append(packet_out, exprs[i].data, exprs[i].len);
        arena_reset(&scratch, mark);
    }
    arena_free(&scratch);
    return exprs;
}

static void free_preencoded_friend_exprs(ByteSpan *exprs, size_t n) {
    if (!exprs)
        return;
    for (size_t i = 0; i < n; i++)
        free(exprs[i].data);
    free(exprs);
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

static double run_singular_lowered(size_t n, int repeat, SymbolId friend_sym,
                                   SymbolId sam_sym) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        Arena scratch;
        CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
        if (!space)
            fail_bridge("cetta_mork_bridge_space_new");
        arena_init(&scratch);
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < n; i++) {
            ArenaMark mark = arena_mark(&scratch);
            uint8_t *expr_bytes = NULL;
            size_t expr_len = 0;
            lower_friend_atom(&scratch, friend_sym, sam_sym, (int64_t)i,
                              &expr_bytes, &expr_len);
            if (!cetta_mork_bridge_space_add_expr_bytes(space, expr_bytes, expr_len, NULL))
                fail_bridge("cetta_mork_bridge_space_add_expr_bytes");
            free(expr_bytes);
            arena_reset(&scratch, mark);
        }
        uint64_t finished = monotonic_ns();
        assert_space_size(space, (uint64_t)n, "singular-lowered size");
        cetta_mork_bridge_space_free(space);
        arena_free(&scratch);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_singular_preencoded(size_t n, int repeat,
                                      const ByteSpan *exprs) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
        if (!space)
            fail_bridge("cetta_mork_bridge_space_new");
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < n; i++) {
            if (!cetta_mork_bridge_space_add_expr_bytes(space,
                                                        exprs[i].data,
                                                        exprs[i].len,
                                                        NULL)) {
                fail_bridge("cetta_mork_bridge_space_add_expr_bytes");
            }
        }
        uint64_t finished = monotonic_ns();
        assert_space_size(space, (uint64_t)n, "singular-preencoded size");
        cetta_mork_bridge_space_free(space);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_batch_lowered(size_t n, int repeat, SymbolId friend_sym,
                                SymbolId sam_sym) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        Arena scratch;
        ByteBuf packet;
        CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
        if (!space)
            fail_bridge("cetta_mork_bridge_space_new");
        arena_init(&scratch);
        byte_buf_init(&packet);
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < n; i++) {
            ArenaMark mark = arena_mark(&scratch);
            uint8_t *expr_bytes = NULL;
            size_t expr_len = 0;
            lower_friend_atom(&scratch, friend_sym, sam_sym, (int64_t)i,
                              &expr_bytes, &expr_len);
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
        uint64_t finished = monotonic_ns();
        assert_space_size(space, (uint64_t)n, "batch-lowered size");
        byte_buf_free(&packet);
        cetta_mork_bridge_space_free(space);
        arena_free(&scratch);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_batch_preencoded(size_t n, int repeat, const ByteBuf *packet) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
        if (!space)
            fail_bridge("cetta_mork_bridge_space_new");
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_add_expr_bytes_batch(space,
                                                          packet->data,
                                                          packet->len,
                                                          NULL)) {
            fail_bridge("cetta_mork_bridge_space_add_expr_bytes_batch");
        }
        uint64_t finished = monotonic_ns();
        assert_space_size(space, (uint64_t)n, "batch-preencoded size");
        cetta_mork_bridge_space_free(space);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    SymbolId friend_sym;
    SymbolId sam_sym;
    size_t n = 100000;
    int repeat = 3;
    ByteSpan *exprs = NULL;
    ByteBuf packet;

    if (argc >= 2) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || parsed == 0) {
            fprintf(stderr, "usage: %s [count] [repeat]\n", argv[0]);
            return 2;
        }
        n = (size_t)parsed;
    }
    if (argc >= 3) {
        char *end = NULL;
        long parsed = strtol(argv[2], &end, 10);
        if (!end || *end != '\0' || parsed <= 0) {
            fprintf(stderr, "usage: %s [count] [repeat]\n", argv[0]);
            return 2;
        }
        repeat = (int)parsed;
    }

    init_symbols(&symbols);
    friend_sym = symbol_intern_cstr(g_symbols, "friend");
    sam_sym = symbol_intern_cstr(g_symbols, "sam");
    exprs = preencode_friend_exprs(n, friend_sym, sam_sym, &packet);

    printf("%-22s %8s %8s\n", "mode", "n", "avg");
    printf("%-22s %8s %8s\n", "----------------------", "--------", "--------");
    printf("%-22s %8zu %8.4f\n", "singular-lowered", n,
           run_singular_lowered(n, repeat, friend_sym, sam_sym));
    printf("%-22s %8zu %8.4f\n", "singular-preencoded", n,
           run_singular_preencoded(n, repeat, exprs));
    printf("%-22s %8zu %8.4f\n", "batch-lowered", n,
           run_batch_lowered(n, repeat, friend_sym, sam_sym));
    printf("%-22s %8zu %8.4f\n", "batch-preencoded", n,
           run_batch_preencoded(n, repeat, &packet));

    byte_buf_free(&packet);
    free_preencoded_friend_exprs(exprs, n);
    cleanup_symbols(&symbols);
    return 0;
}
