#define _POSIX_C_SOURCE 200809L
#include "petta_search_machine.h"
#include "parser.h"
#include "symbol.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

typedef struct {
    SymbolId head;
    bool licensed;
    size_t remaining;
} Probe;

static uint64_t now_ns(void) {
    struct timespec time;
    assert(clock_gettime(CLOCK_MONOTONIC, &time) == 0);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + time.tv_nsec;
}

static size_t number(const char *text) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    assert(end && end != text && !*end && value <= SIZE_MAX);
    return (size_t)value;
}

static bool contains(void *context, SymbolId head, CettaExprLen arity) {
    return head == ((Probe *)context)->head && arity == 1u;
}

static bool reusable(void *context, Space *space, SymbolId head, CettaExprLen arity) {
    (void)space;
    return ((Probe *)context)->licensed && contains(context, head, arity);
}

static bool permit(void *context) {
    Probe *probe = context;
    if (!probe->remaining)
        return false;
    probe->remaining--;
    return true;
}

static Atom *parse(Arena *arena, const char *source) {
    Atom **forms = NULL;
    assert(parse_metta_text(source, arena, &forms) == 1);
    Atom *atom = forms[0];
    free(forms);
    return atom;
}

static void measure(Space *space, Arena *source, PettaMachineTable *table,
                    Probe *probe, size_t nodes, size_t seeds, size_t query_node,
                    size_t quantum, size_t repetition, uint64_t load_ns) {
    Arena answers;
    arena_init(&answers);
    PettaMachineHost host = {
        .context = probe,
        .tabled_relation_contains = contains,
        .tabled_relation_round_reusable = reusable,
        .permit_transition = quantum ? permit : NULL,
        .shared_table = table,
    };
    char text[96];
    snprintf(text, sizeof(text), "(node %zu)", query_node);
    Atom *query = parse(source, text);
    bool *seen = calloc(seeds ? seeds : 1u, sizeof(*seen));
    size_t *order = calloc(seeds ? seeds : 1u, sizeof(*order));
    assert(seen && order);
    PettaMachine machine;
    uint64_t start = now_ns();
    assert(petta_machine_init(&machine, space, &answers, query, NULL, &host));
    uint64_t prepare_ns = now_ns() - start;
    uint64_t first_ns = 0u, pauses = 0u, ordered_hash = UINT64_C(14695981039346656037);
    size_t length = 0u, dependency_peak = 0u;
    start = now_ns();
    for (;;) {
        probe->remaining = quantum ? quantum : SIZE_MAX;
        Atom *answer = NULL;
        Bindings environment;
        PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
        if (step == PETTA_MACHINE_STEP_ANSWER) {
            if (!length)
                first_ns = now_ns() - start;
            assert(answer->kind == ATOM_EXPR && answer->expr.len == 2u);
            assert(atom_is_symbol(answer->expr.elems[0], "seed"));
            Atom *value = answer->expr.elems[1];
            assert(value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT);
            assert(value->ground.ival >= 0 && (uint64_t)value->ground.ival < seeds);
            size_t index = (size_t)value->ground.ival;
            assert(length < seeds && !seen[index]);
            seen[index] = true;
            order[length] = index;
            ordered_hash = (ordered_hash ^ index) * UINT64_C(1099511628211);
            length++;
        }
        bindings_free(&environment);
        if (quantum) {
            PettaMachineTableStorage storage;
            assert(petta_machine_table_storage(table, &storage));
            if (storage.dependency_bytes > dependency_peak)
                dependency_peak = storage.dependency_bytes;
        }
        if (step == PETTA_MACHINE_STEP_EXHAUSTED)
            break;
        if (step == PETTA_MACHINE_STEP_SUSPENDED)
            pauses++;
        else
            assert(step == PETTA_MACHINE_STEP_ANSWER);
    }
    uint64_t all_ns = now_ns() - start;
    assert(length == seeds);
    PettaMachineStats stats;
    assert(petta_machine_stats(&machine, &stats));
    petta_machine_destroy(&machine);
    PettaMachineTableStorage retained;
    assert(petta_machine_table_storage(table, &retained));
    assert(retained.dependency_bytes == 0u);
    struct rusage usage;
    assert(getrusage(RUSAGE_SELF, &usage) == 0);
    printf("{\"nodes\":%zu,\"seeds\":%zu,\"licensed\":%s,\"query_node\":%zu,"
        "\"quantum\":%zu,\"repetition\":%zu,\"load_ns\":%" PRIu64 ","
        "\"prepare_ns\":%" PRIu64 ",\"first_ns\":%" PRIu64 ",\"all_ns\":%" PRIu64 ","
        "\"answers\":%zu,\"ordered_hash\":%" PRIu64 ",\"suspensions\":%" PRIu64 ","
        "\"rounds\":%" PRIu64 ",\"reused_rounds\":%" PRIu64 ","
        "\"dependency_sample_peak_bytes\":",
        nodes, seeds, probe->licensed ? "true" : "false", query_node,
        quantum, repetition, load_ns, prepare_ns, first_ns, all_ns, length,
        ordered_hash, pauses, stats.table_generator_rounds,
        stats.table_generator_rounds_reused);
    if (quantum)
        printf("%zu", dependency_peak);
    else
        fputs("null", stdout);
    printf(",\"table_control_bytes\":%zu,"
        "\"answer_vector_bytes\":%zu,\"table_arena_live_bytes\":%zu,"
        "\"table_arena_reserved_bytes\":%zu,\"process_peak_rss_kib\":%ld,\"answer_order\":[",
        retained.control_bytes, retained.answer_vector_bytes,
        retained.arena_live_bytes, retained.arena_reserved_bytes, usage.ru_maxrss);
    for (size_t i = 0u; i < length; i++)
        printf("%s%zu", i ? "," : "", order[i]);
    puts("]}");
    free(order);
    free(seen);
    arena_free(&answers);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s NODES SEEDS LICENSED REPETITIONS QUANTUM\n", argv[0]);
        return 2;
    }
    size_t nodes = number(argv[1]), seeds = number(argv[2]);
    size_t licensed = number(argv[3]), repetitions = number(argv[4]), quantum = number(argv[5]);
    assert(nodes > 1u && (seeds == 0u || seeds == 1u || seeds == nodes));
    assert(licensed <= 1u && repetitions);
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    Arena source;
    arena_init(&source);
    Space space;
    space_init(&space);
    uint64_t start = now_ns();
    for (size_t i = 0u; i < nodes; i++) {
        char text[128];
        snprintf(text, sizeof(text), "(= (node %zu) (node %zu))", i, (i + 1u) % nodes);
        space_add(&space, parse(&source, text));
        if (seeds == nodes || (seeds == 1u && !i)) {
            snprintf(text, sizeof(text), "(= (node %zu) (seed %zu))", i, i);
            space_add(&space, parse(&source, text));
        }
    }
    uint64_t load_ns = now_ns() - start;
    Probe probe = {symbol_intern_cstr(g_symbols, "node"), licensed != 0u, SIZE_MAX};
    for (size_t repetition = 0u; repetition < repetitions; repetition++) {
        PettaMachineTable *table = petta_machine_table_new();
        assert(table);
        measure(&space, &source, table, &probe, nodes, seeds, 0u, quantum, repetition, load_ns);
        measure(&space, &source, table, &probe, nodes, seeds, 1u, quantum, repetition, load_ns);
        petta_machine_table_free(table);
    }
    space_free(&space);
    arena_free(&source);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}
