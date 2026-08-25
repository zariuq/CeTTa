#define _POSIX_C_SOURCE 200809L

#include "atom.h"
#include "library_io.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Atom *dispatch(CettaIoRuntime *runtime, Arena *arena,
                      const char *head_name, Atom **args, uint32_t nargs) {
    return cetta_io_dispatch(runtime, arena, atom_symbol(arena, head_name),
                             args, nargs);
}

static Atom *http_request(Arena *arena, const char *url) {
    Atom *items[7] = {
        atom_symbol(arena, "http:request"), atom_string(arena, "GET"),
        atom_string(arena, url), atom_expr(arena, NULL, 0u),
        atom_string(arena, ""), atom_int(arena, 5000), atom_int(arena, 64),
    };
    return atom_expr(arena, items, 7u);
}

static bool pending_id(Atom *atom, int64_t *id_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        !atom_is_symbol(atom->expr.elems[0], "io:pending") ||
        atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT)
        return false;
    *id_out = atom->expr.elems[1]->ground.ival;
    return true;
}

static bool idle(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 1u &&
           atom_is_symbol(atom->expr.elems[0], "io:idle");
}

static bool valid_event(Atom *atom, int64_t *id_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "io:event") ||
        atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT)
        return false;
    Atom *result = atom->expr.elems[2];
    if (!result || result->kind != ATOM_EXPR || result->expr.len != 3u ||
        !atom_is_symbol(result->expr.elems[0], "http:response") ||
        result->expr.elems[1]->kind != ATOM_GROUNDED ||
        result->expr.elems[1]->ground.gkind != GV_INT ||
        result->expr.elems[1]->ground.ival != 200 ||
        result->expr.elems[2]->kind != ATOM_GROUNDED ||
        result->expr.elems[2]->ground.gkind != GV_STRING ||
        strcmp(result->expr.elems[2]->ground.sval, "one") != 0)
        return false;
    *id_out = atom->expr.elems[1]->ground.ival;
    return true;
}

static uint64_t nanoseconds(struct timespec value) {
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s BASE_URL [REQUESTS]\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    unsigned long parsed = argc == 3 ? strtoul(argv[2], &end, 10) : 64u;
    if (parsed == 0u || parsed > 4096u ||
        (argc == 3 && (!end || *end != '\0'))) {
        fprintf(stderr, "REQUESTS must be in 1..4096\n");
        return 2;
    }
    size_t requests = (size_t)parsed;
    char url[1024];
    int written = snprintf(url, sizeof(url), "%s/one", argv[1]);
    if (written < 0 || (size_t)written >= sizeof(url)) return 2;

    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    CettaIoRuntime *runtime = cetta_io_runtime_new();
    bool *seen = calloc(requests + 1u, sizeof(*seen));
    if (!seen) return 2;

    struct timespec start;
    struct timespec end_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0u; index < requests; index++) {
        Atom *request = http_request(&arena, url);
        Atom *pending = dispatch(runtime, &arena, "__cetta_lib_io_submit",
                                 &request, 1u);
        int64_t id = 0;
        if (!pending_id(pending, &id) || id != (int64_t)index + 1) {
            fprintf(stderr, "submit failed at request %zu\n", index);
            return 1;
        }
    }

    size_t completed = 0u;
    size_t idle_polls = 0u;
    while (completed < requests && idle_polls < 100000u) {
        Atom *event = dispatch(runtime, &arena, "__cetta_lib_io_poll",
                               NULL, 0u);
        if (idle(event)) {
            struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000L};
            (void)nanosleep(&pause, NULL);
            idle_polls++;
            continue;
        }
        int64_t id = 0;
        if (!valid_event(event, &id) || id <= 0 ||
            (size_t)id > requests || seen[id]) {
            fputs("invalid, unknown, or replayed completion\n", stderr);
            atom_print(event, stderr);
            fputc('\n', stderr);
            return 1;
        }
        seen[id] = true;
        completed++;
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &end_time);
    uint64_t elapsed = nanoseconds(end_time) - nanoseconds(start);
    if (completed != requests ||
        !idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u))) {
        fprintf(stderr, "completion mismatch: %zu/%zu\n", completed,
                requests);
        return 1;
    }

    printf("(IoRuntimeBenchmark requests %zu completions %zu "
           "elapsed-ns %" PRIu64 " ns-per-completion %.3f)\n",
           requests, completed, elapsed, (double)elapsed / requests);
    free(seen);
    cetta_io_runtime_free(runtime);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
