#define _POSIX_C_SOURCE 200809L
#include "relational_plan.h"
#include "relational_cursor.h"
#include "parser.h"
#include "symbol.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

static uint64_t now_ns(void) {
    struct timespec time;
    assert(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}

static size_t heap_bytes(void) {
#ifdef __GLIBC__
    return mallinfo2().uordblks;
#else
    return 0u;
#endif
}

static uint64_t number(const char *text) {
    char *end = NULL;
    uint64_t value = strtoull(text, &end, 10);
    assert(end && end != text && !*end);
    return value;
}

int main(int argc, char **argv) {
    if ((argc != 4 && argc != 5) || (strcmp(argv[1], "dense") && strcmp(argv[1], "skew") &&
                     strcmp(argv[1], "guarded"))) {
        fprintf(stderr, "usage: %s dense|skew|guarded WIDTH REPETITIONS [NAVIGATION_QUANTUM]\n", argv[0]);
        return 2;
    }
    bool guarded = strcmp(argv[1], "guarded") == 0;
    bool dense = strcmp(argv[1], "skew") != 0;
    uint64_t width = number(argv[2]), repetitions = number(argv[3]);
    uint64_t quantum = argc == 5 ? number(argv[4]) : 0u;
    uint64_t expected = width;
    assert(width && width <= INT64_MAX && repetitions);
    assert(argc != 5 || quantum);
    if (dense) {
        if (!guarded)
            assert(!__builtin_mul_overflow(expected, width, &expected));
        assert(!__builtin_mul_overflow(expected, width, &expected));
    }
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    Arena persistent, scratch;
    arena_init(&persistent);
    arena_init(&scratch);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);
    space.kind = SPACE_KIND_STACK;
    uint64_t start = now_ns();
    const char *heads[] = {"r", "s", "t"};
    for (size_t f = 0u; f < 3u; f++) {
        uint64_t occurrence = 0u;
        for (uint64_t x = 0u; x < width; x++) {
            for (uint64_t y = 0u; y < (dense ? width : 1u); y++) {
                uint64_t left = dense ? x : f == 1u ? 0u : x;
                uint64_t right = dense ? y : f == 0u ? 0u : x;
                Atom *row[] = {atom_symbol(&scratch, heads[f]), atom_int(&scratch, left),
                    atom_int(&scratch, right), atom_int(&scratch, occurrence++)};
                space_add(&space, atom_expr(&scratch, row, 4u));
            }
        }
    }
    if (guarded) {
        uint64_t occurrence = 0u;
        for (uint64_t x = 0u; x < width; x++) {
            for (uint64_t y = 0u; y < width; y++) {
                Atom *row[] = {atom_symbol(&scratch, "u"), atom_int(&scratch, x),
                    atom_int(&scratch, y), atom_int(&scratch, (x + y) % width),
                    atom_int(&scratch, occurrence++)};
                space_add(&space, atom_expr(&scratch, row, 5u));
            }
        }
    }
    uint64_t load_ns = now_ns() - start;
    Atom **forms = NULL;
    const char *source = guarded
        ? "(, (r $x $y $or) (s $y $z $os) (t $z $x $ot) (u $x $y $z $ou))"
        : "(, (r $x $y $or) (s $y $z $os) (t $z $x $ot))";
    assert(parse_metta_text(source, &scratch, &forms) == 1);
    Atom *query = forms[0];
    free(forms);
    for (uint64_t attempt = 0u; attempt < repetitions; attempt++) {
        size_t before = heap_bytes();
        start = now_ns();
        CettaRelationalPlan *plan = NULL;
        assert(cetta_relational_plan_prepare(&space, &scratch, query->expr.elems + 1u,
            (size_t)query->expr.len - 1u, NULL, CETTA_RELATIONAL_ADMIT_FACTORIZED_COUNT, &plan));
        uint64_t prepared = now_ns();
        size_t retained = heap_bytes();
        const CettaRelationalPlanInfo info = *cetta_relational_plan_info(plan);
        uint64_t count = 0u;
        uint64_t navigation_steps = 0u, slices = 0u;
        CettaRelationalWork work;
        if (quantum) {
            CettaRelationalCountCursor *cursor = NULL;
            assert(cetta_relational_count_cursor_open(plan, &space, &cursor));
            for (;;) {
                uint64_t used = 0u;
                CettaRelationalCountStatus status = cetta_relational_count_cursor_advance(
                    cursor, &space, quantum, &used);
                assert(used <= quantum);
                assert(!__builtin_add_overflow(navigation_steps, used, &navigation_steps));
                slices++;
                if (status == CETTA_RELATIONAL_COUNT_COMPLETE)
                    break;
                assert(status == CETTA_RELATIONAL_COUNT_PAUSED);
            }
            assert(cetta_relational_count_cursor_result(cursor, &space, &count, &work));
            cetta_relational_count_cursor_free(cursor);
        } else {
            assert(cetta_relational_plan_count(plan, &space, &count, &work));
        }
        assert(count == expected);
        if (guarded) {
            assert(info.covering_factor == 3u);
            assert(work.candidate_prefixes == width + 2u * expected);
            assert(work.leaf_assignments == expected);
        }
        uint64_t counted = now_ns();
        cetta_relational_plan_free(plan);
        size_t freed = heap_bytes();
        struct rusage usage;
        assert(getrusage(RUSAGE_SELF, &usage) == 0);
        printf("{\"shape\":\"%s\",\"width\":%" PRIu64 ",\"attempt\":%" PRIu64
            ",\"rows\":%" PRIu64 ",\"count\":%" PRIu64 ",\"load_ns\":%" PRIu64
            ",\"prepare_ns\":%" PRIu64 ",\"count_ns\":%" PRIu64
            ",\"scanned_rows\":%" PRIu64 ",\"retained_payload\":%zu,\"preparation_payload_allowance\":%zu"
            ",\"execution_payload_allowance\":%zu,\"heap_before\":%zu,\"heap_prepared\":%zu,\"heap_freed\":%zu"
            ",\"peak_rss_kib\":%ld,\"key_seeks\":%" PRIu64 ",\"key_comparisons\":%" PRIu64
            ",\"prefixes\":%" PRIu64 ",\"leaf_assignments\":%" PRIu64
            ",\"navigation_quantum\":%" PRIu64 ",\"navigation_steps\":%" PRIu64 ",\"slices\":%" PRIu64 "}\n",
            argv[1], width, attempt, (uint64_t)space.native.len, count, load_ns,
            prepared - start, counted - prepared, info.scanned_rows, info.retained_bytes,
            info.preparation_peak_bytes, work.peak_bytes, before, retained, freed, usage.ru_maxrss,
            work.key_seeks, work.key_comparisons, work.candidate_prefixes, work.leaf_assignments,
            quantum, navigation_steps, slices);
    }
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}
