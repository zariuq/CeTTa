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

static CettaMorkSpaceHandle *build_friend_space_range(size_t count,
                                                      int64_t start,
                                                      SymbolId friend_sym,
                                                      SymbolId sam_sym) {
    Arena scratch;
    ByteBuf packet;
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
    if (!space)
        fail_bridge("cetta_mork_bridge_space_new");
    arena_init(&scratch);
    byte_buf_init(&packet);
    for (size_t i = 0; i < count; i++) {
        ArenaMark mark = arena_mark(&scratch);
        uint8_t *expr_bytes = NULL;
        size_t expr_len = 0;
        lower_friend_atom(&scratch, friend_sym, sam_sym, start + (int64_t)i,
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

static double run_space_dump_packet_mode(CettaMorkSpaceHandle *space,
                                         uint64_t expected_rows,
                                         int repeat,
                                         size_t *out_packet_len) {
    double total_seconds = 0.0;
    size_t packet_len = 0;
    for (int rep = 0; rep < repeat; rep++) {
        uint8_t *packet = NULL;
        size_t len = 0;
        uint32_t rows = 0;
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_dump(space, &packet, &len, &rows))
            fail_bridge("cetta_mork_bridge_space_dump");
        uint64_t finished = monotonic_ns();
        if ((uint64_t)rows != expected_rows || len == 0 || !packet) {
            fprintf(stderr, "error: dump packet returned rows=%" PRIu32 ", len=%zu\n",
                    rows, len);
            exit(1);
        }
        packet_len = len;
        cetta_mork_bridge_bytes_free(packet, len);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    if (out_packet_len)
        *out_packet_len = packet_len;
    return total_seconds / (double)repeat;
}

static double run_act_dump_mode(CettaMorkSpaceHandle *space,
                                uint64_t expected_rows,
                                const char *path,
                                int repeat) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        uint64_t saved = 0;
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_dump_act_file(space,
                                                   (const uint8_t *)path,
                                                   strlen(path),
                                                   &saved)) {
            fail_bridge("cetta_mork_bridge_space_dump_act_file");
        }
        uint64_t finished = monotonic_ns();
        if (saved != expected_rows) {
            fprintf(stderr, "error: ACT dump saved %" PRIu64 ", expected %" PRIu64 "\n",
                    saved, expected_rows);
            exit(1);
        }
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_act_load_mode(const char *path,
                                uint64_t expected_rows,
                                int repeat) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        uint64_t loaded = 0;
        CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
        if (!space)
            fail_bridge("cetta_mork_bridge_space_new");
        uint64_t started = monotonic_ns();
        if (!cetta_mork_bridge_space_load_act_file(space,
                                                   (const uint8_t *)path,
                                                   strlen(path),
                                                   &loaded)) {
            fail_bridge("cetta_mork_bridge_space_load_act_file");
        }
        uint64_t finished = monotonic_ns();
        if (loaded != expected_rows) {
            fprintf(stderr, "error: ACT load loaded %" PRIu64 ", expected %" PRIu64 "\n",
                    loaded, expected_rows);
            exit(1);
        }
        assert_space_size(space, expected_rows, "ACT load size");
        cetta_mork_bridge_space_free(space);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_space_new_mode(const char *ctx,
                                 CettaMorkSpaceHandle *(*call)(
                                     const CettaMorkSpaceHandle *,
                                     const CettaMorkSpaceHandle *),
                                 const CettaMorkSpaceHandle *lhs,
                                 const CettaMorkSpaceHandle *rhs,
                                 uint64_t expected_rows,
                                 int repeat) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkSpaceHandle *out = NULL;
        uint64_t started = monotonic_ns();
        out = call(lhs, rhs);
        if (!out)
            fail_bridge(ctx);
        uint64_t finished = monotonic_ns();
        assert_space_size(out, expected_rows, ctx);
        cetta_mork_bridge_space_free(out);
        total_seconds += (double)(finished - started) / 1000000000.0;
    }
    return total_seconds / (double)repeat;
}

static double run_space_into_mode(const char *ctx,
                                  bool (*call)(CettaMorkSpaceHandle *,
                                               const CettaMorkSpaceHandle *),
                                  const CettaMorkSpaceHandle *lhs,
                                  const CettaMorkSpaceHandle *rhs,
                                  uint64_t expected_rows,
                                  int repeat) {
    double total_seconds = 0.0;
    for (int rep = 0; rep < repeat; rep++) {
        CettaMorkSpaceHandle *dst = cetta_mork_bridge_space_clone(lhs);
        if (!dst)
            fail_bridge("cetta_mork_bridge_space_clone");
        uint64_t started = monotonic_ns();
        if (!call(dst, rhs))
            fail_bridge(ctx);
        uint64_t finished = monotonic_ns();
        assert_space_size(dst, expected_rows, ctx);
        cetta_mork_bridge_space_free(dst);
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
    size_t overlap = 0;
    uint64_t lhs_rows = 0;
    uint64_t rhs_rows = 0;
    uint64_t join_rows = 0;
    uint64_t meet_rows = 0;
    uint64_t subtract_rows = 0;
    uint64_t restrict_rows = 0;
    size_t packet_len = 0;
    char act_path[256];
    CettaMorkSpaceHandle *lhs = NULL;
    CettaMorkSpaceHandle *rhs = NULL;
    CettaMorkSpaceHandle *verify = NULL;

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

    overlap = n / 2u;
    lhs_rows = (uint64_t)n;
    rhs_rows = (uint64_t)n;
    join_rows = (uint64_t)(n + n - overlap);
    meet_rows = (uint64_t)overlap;
    subtract_rows = (uint64_t)(n - overlap);
    restrict_rows = (uint64_t)overlap;

    init_symbols(&symbols);
    friend_sym = symbol_intern_cstr(g_symbols, "friend");
    sam_sym = symbol_intern_cstr(g_symbols, "sam");

    lhs = build_friend_space_range(n, 0, friend_sym, sam_sym);
    rhs = build_friend_space_range(n, (int64_t)overlap, friend_sym, sam_sym);
    assert_space_size(lhs, lhs_rows, "lhs size");
    assert_space_size(rhs, rhs_rows, "rhs size");

    snprintf(act_path, sizeof(act_path), "runtime/bench_mork_bridge_space_ops_%zu.act", n);
    if (!cetta_mork_bridge_space_dump_act_file(lhs,
                                               (const uint8_t *)act_path,
                                               strlen(act_path),
                                               NULL)) {
        fail_bridge("initial ACT dump");
    }
    verify = cetta_mork_bridge_space_new();
    if (!verify)
        fail_bridge("cetta_mork_bridge_space_new");
    if (!cetta_mork_bridge_space_load_act_file(verify,
                                               (const uint8_t *)act_path,
                                               strlen(act_path),
                                               NULL)) {
        fail_bridge("initial ACT load");
    }
    assert_space_size(verify, lhs_rows, "initial ACT load size");
    cetta_mork_bridge_space_free(verify);

    printf("%-22s %10s %10s %10s %10s %10s\n",
           "mode", "lhs_rows", "rhs_rows", "out_rows", "packet_b", "avg_s");
    printf("%-22s %10s %10s %10s %10s %10s\n",
           "----------------------", "----------", "----------",
           "----------", "----------", "----------");

    {
        double avg = run_space_dump_packet_mode(lhs, lhs_rows, repeat, &packet_len);
        printf("%-22s %10" PRIu64 " %10s %10" PRIu64 " %10zu %10.6f\n",
               "space-dump-packet",
               lhs_rows,
               "-",
               lhs_rows,
               packet_len,
               avg);
    }
    printf("%-22s %10" PRIu64 " %10s %10" PRIu64 " %10s %10.6f\n",
           "act-dump-file",
           lhs_rows,
           "-",
           lhs_rows,
           "-",
           run_act_dump_mode(lhs, lhs_rows, act_path, repeat));
    printf("%-22s %10s %10s %10" PRIu64 " %10s %10.6f\n",
           "act-load-file",
           "-",
           "-",
           lhs_rows,
           "-",
           run_act_load_mode(act_path, lhs_rows, repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-join-new",
           lhs_rows,
           rhs_rows,
           join_rows,
           "-",
           run_space_new_mode("cetta_mork_bridge_space_join",
                              cetta_mork_bridge_space_join,
                              lhs,
                              rhs,
                              join_rows,
                              repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-join-into",
           lhs_rows,
           rhs_rows,
           join_rows,
           "-",
           run_space_into_mode("cetta_mork_bridge_space_join_into",
                               cetta_mork_bridge_space_join_into,
                               lhs,
                               rhs,
                               join_rows,
                               repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-meet-new",
           lhs_rows,
           rhs_rows,
           meet_rows,
           "-",
           run_space_new_mode("cetta_mork_bridge_space_meet",
                              cetta_mork_bridge_space_meet,
                              lhs,
                              rhs,
                              meet_rows,
                              repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-meet-into",
           lhs_rows,
           rhs_rows,
           meet_rows,
           "-",
           run_space_into_mode("cetta_mork_bridge_space_meet_into",
                               cetta_mork_bridge_space_meet_into,
                               lhs,
                               rhs,
                               meet_rows,
                               repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-subtract-new",
           lhs_rows,
           rhs_rows,
           subtract_rows,
           "-",
           run_space_new_mode("cetta_mork_bridge_space_subtract",
                              cetta_mork_bridge_space_subtract,
                              lhs,
                              rhs,
                              subtract_rows,
                              repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-subtract-into",
           lhs_rows,
           rhs_rows,
           subtract_rows,
           "-",
           run_space_into_mode("cetta_mork_bridge_space_subtract_into",
                               cetta_mork_bridge_space_subtract_into,
                               lhs,
                               rhs,
                               subtract_rows,
                               repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-restrict-new",
           lhs_rows,
           rhs_rows,
           restrict_rows,
           "-",
           run_space_new_mode("cetta_mork_bridge_space_restrict",
                              cetta_mork_bridge_space_restrict,
                              lhs,
                              rhs,
                              restrict_rows,
                              repeat));
    printf("%-22s %10" PRIu64 " %10" PRIu64 " %10" PRIu64 " %10s %10.6f\n",
           "space-restrict-into",
           lhs_rows,
           rhs_rows,
           restrict_rows,
           "-",
           run_space_into_mode("cetta_mork_bridge_space_restrict_into",
                               cetta_mork_bridge_space_restrict_into,
                               lhs,
                               rhs,
                               restrict_rows,
                               repeat));

    cetta_mork_bridge_space_free(lhs);
    cetta_mork_bridge_space_free(rhs);
    cleanup_symbols(&symbols);
    return 0;
}
