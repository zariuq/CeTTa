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
#include "symbol.h"

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
    if (!cetta_mm2_atom_to_bridge_expr_bytes(a, atom, out_bytes, out_len,
                                             &error)) {
        fprintf(stderr, "error: bridge expr-byte lowering failed: %s\n",
                error ? error : "unknown lowering error");
        exit(1);
    }
}

static CettaMorkSpaceHandle *build_friend_space(size_t n,
                                                SymbolId friend_sym,
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

static void prepare_branch_cursor(CettaMorkCursorHandle *cursor) {
    bool moved = false;
    if (!cetta_mork_bridge_cursor_reset(cursor))
        fail_bridge("cetta_mork_bridge_cursor_reset");
    if (!cetta_mork_bridge_cursor_descend_until(cursor, &moved))
        fail_bridge("cetta_mork_bridge_cursor_descend_until");
    if (!moved) {
        fprintf(stderr, "error: cursor branch preparation did not move\n");
        exit(1);
    }
}

static void prepare_leaf_cursor(CettaMorkCursorHandle *cursor) {
    bool moved = false;
    prepare_branch_cursor(cursor);
    if (!cetta_mork_bridge_cursor_next_val(cursor, &moved))
        fail_bridge("cetta_mork_bridge_cursor_next_val");
    if (!moved) {
        fprintf(stderr, "error: cursor leaf preparation did not move\n");
        exit(1);
    }
}

static double run_space_size_mode(CettaMorkSpaceHandle *space,
                                  size_t iters,
                                  int repeat,
                                  bool unique) {
    double total_seconds = 0.0;
    uint64_t sink = 0;
    for (int rep = 0; rep < repeat; rep++) {
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < iters; i++) {
            uint64_t value = 0;
            bool ok = unique
                          ? cetta_mork_bridge_space_unique_size(space, &value)
                          : cetta_mork_bridge_space_size(space, &value);
            if (!ok)
                fail_bridge(unique ? "cetta_mork_bridge_space_unique_size"
                                   : "cetta_mork_bridge_space_size");
            sink ^= value;
        }
        uint64_t finished = monotonic_ns();
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (sink == UINT64_MAX)
        fprintf(stderr, " ");
    return total_seconds / (double)repeat;
}

static double run_cursor_new_mode(CettaMorkSpaceHandle *space,
                                  size_t iters,
                                  int repeat) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < iters; i++) {
            CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
            if (!cursor)
                fail_bridge("cetta_mork_bridge_cursor_new");
            cetta_mork_bridge_cursor_free(cursor);
        }
        uint64_t finished = monotonic_ns();
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_cursor_bool_mode(CettaMorkSpaceHandle *space,
                                   size_t iters,
                                   int repeat,
                                   const char *ctx,
                                   bool (*call)(const CettaMorkCursorHandle *,
                                                bool *),
                                   bool expect,
                                   bool use_branch,
                                   bool use_leaf) {
    double total_seconds = 0.0;
    uint64_t sink = 0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
        if (!cursor)
            fail_bridge("cetta_mork_bridge_cursor_new");
        if (use_leaf) {
            prepare_leaf_cursor(cursor);
        } else if (use_branch) {
            prepare_branch_cursor(cursor);
        }
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < iters; i++) {
            bool value = false;
            if (!call(cursor, &value))
                fail_bridge(ctx);
            if (value != expect) {
                fprintf(stderr, "error: %s returned unexpected boolean %d\n",
                        ctx, (int)value);
                exit(1);
            }
            sink ^= (uint64_t)value;
        }
        uint64_t finished = monotonic_ns();
        cetta_mork_bridge_cursor_free(cursor);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (sink == UINT64_MAX)
        fprintf(stderr, " ");
    return total_seconds / (double)repeat;
}

static double run_cursor_u64_mode(CettaMorkSpaceHandle *space,
                                  size_t iters,
                                  int repeat,
                                  const char *ctx,
                                  bool (*call)(const CettaMorkCursorHandle *,
                                               uint64_t *),
                                  uint64_t expect,
                                  bool use_branch,
                                  bool use_leaf) {
    double total_seconds = 0.0;
    uint64_t sink = 0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
        if (!cursor)
            fail_bridge("cetta_mork_bridge_cursor_new");
        if (use_leaf) {
            prepare_leaf_cursor(cursor);
        } else if (use_branch) {
            prepare_branch_cursor(cursor);
        }
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < iters; i++) {
            uint64_t value = 0;
            if (!call(cursor, &value))
                fail_bridge(ctx);
            if (value != expect) {
                fprintf(stderr, "error: %s returned %" PRIu64 ", expected %" PRIu64 "\n",
                        ctx, value, expect);
                exit(1);
            }
            sink ^= value;
        }
        uint64_t finished = monotonic_ns();
        cetta_mork_bridge_cursor_free(cursor);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (sink == UINT64_MAX)
        fprintf(stderr, " ");
    return total_seconds / (double)repeat;
}

static double run_cursor_bytes_mode(CettaMorkSpaceHandle *space,
                                    size_t iters,
                                    int repeat,
                                    const char *ctx,
                                    bool (*call)(const CettaMorkCursorHandle *,
                                                 uint8_t **,
                                                 size_t *),
                                    bool use_branch,
                                    bool use_leaf) {
    double total_seconds = 0.0;
    uint64_t sink = 0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
        if (!cursor)
            fail_bridge("cetta_mork_bridge_cursor_new");
        if (use_leaf) {
            prepare_leaf_cursor(cursor);
        } else if (use_branch) {
            prepare_branch_cursor(cursor);
        }
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < iters; i++) {
            uint8_t *bytes = NULL;
            size_t len = 0;
            if (!call(cursor, &bytes, &len))
                fail_bridge(ctx);
            if (len == 0 || !bytes) {
                fprintf(stderr, "error: %s returned empty bytes\n", ctx);
                exit(1);
            }
            sink ^= (uint64_t)len;
            cetta_mork_bridge_bytes_free(bytes, len);
        }
        uint64_t finished = monotonic_ns();
        cetta_mork_bridge_cursor_free(cursor);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (sink == UINT64_MAX)
        fprintf(stderr, " ");
    return total_seconds / (double)repeat;
}

static double run_cursor_next_val_mode(CettaMorkSpaceHandle *space,
                                       size_t expected_vals,
                                       int repeat) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
        if (!cursor)
            fail_bridge("cetta_mork_bridge_cursor_new");
        prepare_branch_cursor(cursor);
        uint64_t started = monotonic_ns();
        for (size_t i = 0; i < expected_vals; i++) {
            bool moved = false;
            if (!cetta_mork_bridge_cursor_next_val(cursor, &moved))
                fail_bridge("cetta_mork_bridge_cursor_next_val");
            if (!moved) {
                fprintf(stderr, "error: cursor-next-val stopped early at %zu of %zu\n",
                        i, expected_vals);
                exit(1);
            }
        }
        uint64_t finished = monotonic_ns();
        {
            bool moved = true;
            if (!cetta_mork_bridge_cursor_next_val(cursor, &moved))
                fail_bridge("cetta_mork_bridge_cursor_next_val tail");
            if (moved) {
                fprintf(stderr, "error: cursor-next-val exceeded expected value count\n");
                exit(1);
            }
        }
        cetta_mork_bridge_cursor_free(cursor);
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
    CettaMorkSpaceHandle *space = NULL;

    if (argc >= 2) {
        char *end = NULL;
        unsigned long parsed = strtoul(argv[1], &end, 10);
        if (!end || *end != '\0' || parsed < 4) {
            fprintf(stderr, "usage: %s [count>=4] [repeat]\n", argv[0]);
            return 2;
        }
        n = (size_t)parsed;
    }
    if (argc >= 3) {
        char *end = NULL;
        long parsed = strtol(argv[2], &end, 10);
        if (!end || *end != '\0' || parsed <= 0) {
            fprintf(stderr, "usage: %s [count>=4] [repeat]\n", argv[0]);
            return 2;
        }
        repeat = (int)parsed;
    }

    init_symbols(&symbols);
    friend_sym = symbol_intern_cstr(g_symbols, "friend");
    sam_sym = symbol_intern_cstr(g_symbols, "sam");

    space = build_friend_space(n, friend_sym, sam_sym);
    assert_space_size(space, (uint64_t)n, "scalar-cursor bench size");

    printf("%-26s %10s %10s %10s\n",
           "mode", "rows", "iters", "avg_s");
    printf("%-26s %10s %10s %10s\n",
           "--------------------------", "----------", "----------", "----------");

    printf("%-26s %10zu %10zu %10.6f\n",
           "space-size",
           n,
           n,
           run_space_size_mode(space, n, repeat, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "space-unique-size",
           n,
           n,
           run_space_size_mode(space, n, repeat, true));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-new",
           n,
           n,
           run_cursor_new_mode(space, n, repeat));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-path-exists-root",
           n,
           n,
           run_cursor_bool_mode(space, n, repeat,
                                "cetta_mork_bridge_cursor_path_exists",
                                cetta_mork_bridge_cursor_path_exists,
                                true, false, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-is-val-root",
           n,
           n,
           run_cursor_bool_mode(space, n, repeat,
                                "cetta_mork_bridge_cursor_is_val",
                                cetta_mork_bridge_cursor_is_val,
                                false, false, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-child-count-root",
           n,
           n,
           run_cursor_u64_mode(space, n, repeat,
                               "cetta_mork_bridge_cursor_child_count",
                               cetta_mork_bridge_cursor_child_count,
                               1u, false, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-val-count-root",
           n,
           n,
           run_cursor_u64_mode(space, n, repeat,
                               "cetta_mork_bridge_cursor_val_count",
                               cetta_mork_bridge_cursor_val_count,
                               (uint64_t)n, false, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-depth-root",
           n,
           n,
           run_cursor_u64_mode(space, n, repeat,
                               "cetta_mork_bridge_cursor_depth",
                               cetta_mork_bridge_cursor_depth,
                               0u, false, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-child-bytes-root",
           n,
           n,
           run_cursor_bytes_mode(space, n, repeat,
                                 "cetta_mork_bridge_cursor_child_bytes",
                                 cetta_mork_bridge_cursor_child_bytes,
                                 false, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-path-bytes-branch",
           n,
           n,
           run_cursor_bytes_mode(space, n, repeat,
                                 "cetta_mork_bridge_cursor_path_bytes",
                                 cetta_mork_bridge_cursor_path_bytes,
                                 true, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-child-bytes-branch",
           n,
           n,
           run_cursor_bytes_mode(space, n, repeat,
                                 "cetta_mork_bridge_cursor_child_bytes",
                                 cetta_mork_bridge_cursor_child_bytes,
                                 true, false));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-path-bytes-leaf",
           n,
           n,
           run_cursor_bytes_mode(space, n, repeat,
                                 "cetta_mork_bridge_cursor_path_bytes",
                                 cetta_mork_bridge_cursor_path_bytes,
                                 false, true));
    printf("%-26s %10zu %10zu %10.6f\n",
           "cursor-next-val",
           n,
           n,
           run_cursor_next_val_mode(space, n, repeat));

    cetta_mork_bridge_space_free(space);
    cleanup_symbols(&symbols);
    return 0;
}
