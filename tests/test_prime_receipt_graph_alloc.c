#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "prime_need.h"
#include "symbol.h"

/* Link wrapping exercises every real collector allocation, including the
 * shared frame worklist.  Fault injection is confined to this test binary. */
static bool probing;
static size_t fail_at;
static size_t allocation_count;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *memory, size_t size);

static bool refuse_allocation(void) {
    return probing && allocation_count++ == fail_at;
}

void *__wrap_malloc(size_t size) {
    return refuse_allocation() ? NULL : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
    return refuse_allocation() ? NULL : __real_calloc(count, size);
}

void *__wrap_realloc(void *memory, size_t size) {
    return refuse_allocation() ? NULL : __real_realloc(memory, size);
}

static PrimeNeedReceiptGraphStatus collect_with_fault(
    const PrimeNeedReceipt *receipt, PrimeNeedReceiptGraph *graph, size_t index) {
    fail_at = index;
    allocation_count = 0u;
    probing = true;
    PrimeNeedReceiptGraphStatus status =
        prime_need_receipt_graph_collect(receipt, graph);
    probing = false;
    return status;
}

static void check_production_support_refusal(Arena *arena) {
    PrimeNeedSnapshot root = {0}, base = {0}, running = {0}, produced = {0};
    uint64_t thunk = 0u;
    Atom *value = atom_int(arena, 43);
    assert(prime_need_snapshot_begin(&root));
    assert(prime_need_snapshot_allocate(arena, &root, value, &base, &thunk));
    PrimeNeedReadCapture *capture = prime_need_read_capture_new(&base, &base);
    assert(capture);
    PrimeNeedReadScope scope;
    prime_need_read_scope_enter(&scope, capture);
    PrimeNeedCellView cell;
    fail_at = 0u; allocation_count = 0u; probing = true;
    bool found = prime_need_snapshot_lookup(&base, thunk, &cell);
    probing = false;
    prime_need_read_scope_leave(&scope);
    assert(found && cell.cache_state == PRIME_NEED_CACHE_EMPTY && allocation_count == 1u);
    assert(!prime_need_read_capture_complete(capture));
    prime_need_read_capture_free(capture);

    capture = prime_need_read_capture_new(&base, &base);
    assert(capture);
    prime_need_read_scope_enter(&scope, capture);
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    assert(prime_need_snapshot_start_evaluation(arena, &base, thunk, evaluator, &running));
    assert(prime_need_snapshot_resolve_value(arena, &running, thunk, evaluator, value, &produced));
    prime_need_read_scope_leave(&scope);
    assert(prime_need_read_capture_complete(capture));
    for (size_t i = 0u; i < 2u; i++) {
        fail_at = i; allocation_count = 0u; probing = true;
        PrimeNeedReadCapture *copy = prime_need_read_capture_copy(capture);
        probing = false;
        assert(!copy && allocation_count == i + 1u);
        assert(prime_need_read_capture_complete(capture));
    }
    for (size_t i = 0u; i < 2u; i++) {
        PrimeNeedSnapshot out = root;
        fail_at = i; allocation_count = 0u; probing = true;
        bool imported = prime_need_snapshot_import_production(arena, capture, &produced,
            thunk, &base, &out);
        probing = false;
        assert(!imported && allocation_count == i + 1u &&
            memcmp(&out, &root, sizeof(out)) == 0);
    }
    PrimeNeedSnapshot imported;
    assert(prime_need_snapshot_import_production(arena, capture, &produced, thunk, &base, &imported));
    assert(prime_need_snapshot_lookup(&imported, thunk, &cell) && cell.cached == value);
    prime_need_read_capture_free(capture);
    puts("PASS: 5 production-support allocation refusals preserve lookups, evidence and destination");
}

static void check_capture_allocation_refusal(Arena *arena) {
    Atom *left=atom_int(arena,7),*right=atom_int(arena,7);
    for (unsigned i=0u;i<192u;i++) {
        left=atom_expr2(arena,left,left);
        right=atom_expr2(arena,right,right);
    }
    fail_at=SIZE_MAX;allocation_count=0u;probing=true;
    bool agrees=atom_capture_identical(left,right);
    probing=false;
    size_t allocations=allocation_count;
    assert(agrees && allocations>=4u);
    for (size_t i=0u;i<allocations;i++) {
        fail_at=i;allocation_count=0u;probing=true;
        agrees=atom_capture_identical(left,right);
        probing=false;
        assert(!agrees && allocation_count==i+1u);
        assert(atom_capture_identical(left,right));
    }
    printf("PASS: %zu captured-input allocation refusals preserve both source DAGs\n",allocations);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);
    check_capture_allocation_refusal(&arena);
    PrimeNeedReceipt receipt = {0};
    PrimeNeedReceiptGraph graph = {0};
    assert(collect_with_fault(&receipt, &graph, 0u) ==
           PRIME_NEED_RECEIPT_GRAPH_ABSENT);
    assert(allocation_count == 0u);
    assert(prime_need_receipt_begin(&arena, &receipt));
    assert(collect_with_fault(&receipt, &graph, 0u) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    assert(allocation_count == 0u && graph.session_id != 0u);
    assert(prime_need_receipt_use_equation_ids(
        &arena, &receipt, 1u, 1u, &receipt));
    assert(prime_need_receipt_graph_collect(&receipt, &graph) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    PrimeNeedReceiptGraph saved = graph;
    for (uint64_t i = 2u; i <= 80u; i++)
        assert(prime_need_receipt_use_equation_ids(
            &arena, &receipt, 1u, i, &receipt));
    PrimeNeedReceipt source = receipt;
    PrimeNeedReceiptGraph complete = {0};
    assert(collect_with_fault(&receipt, &complete, SIZE_MAX) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    size_t allocations = allocation_count;
    assert(allocations >= 4u && complete.length == 80u &&
           complete.event_count == 80u);
    for (size_t i = 0u; i < allocations; i++) {
        assert(collect_with_fault(&receipt, &graph, i) ==
               PRIME_NEED_RECEIPT_GRAPH_REFUSED);
        assert(allocation_count >= i + 1u);
        assert(graph.nodes == saved.nodes && graph.length == saved.length &&
               graph.event_count == saved.event_count &&
               graph.session_id == saved.session_id &&
               graph.root_id == saved.root_id);
        assert(receipt.top == source.top && receipt.owner == source.owner &&
               receipt.session_id == source.session_id);
        assert(prime_need_receipt_graph_validate(&graph) ==
               PRIME_NEED_RECEIPT_GRAPH_COMPLETE);
    }
    fail_at = 0u;
    allocation_count = 0u;
    probing = true;
    PrimeNeedReceiptGraphStatus status =
        prime_need_receipt_graph_validate(&complete);
    probing = false;
    assert(status == PRIME_NEED_RECEIPT_GRAPH_REFUSED && allocation_count == 1u);
    assert(complete.length == 80u && complete.event_count == 80u);
    PrimeNeedReceiptGraphNode producers[64] = {0};
    for (size_t i = 0u; i < 64u; i++)
        producers[i] = (PrimeNeedReceiptGraphNode){.has_event = true,
            .event = {.need_session_id = 3u, .thunk_id = 5u,
                .producer_update_id = i + 1u, .producer_completed = true}};
    PrimeNeedReceiptGraph producer_graph = {.nodes = producers, .length = 64u};
    bool retained[64] = {0}, untouched[64] = {0};
    fail_at = 0u;
    allocation_count = 0u;
    probing = true;
    bool indexed = prime_need_receipt_graph_producers(&producer_graph, retained);
    probing = false;
    assert(!indexed && allocation_count == 1u &&
           memcmp(retained, untouched, sizeof(retained)) == 0 &&
           producers[6].event.producer_update_id == 7u && producers[6].event.producer_completed);
    assert(prime_need_receipt_graph_producers(&producer_graph, retained));
    for (size_t i = 0u; i < 64u; i++) assert(retained[i]);
    assert(prime_need_receipt_graph_collect(&receipt, &graph) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 80u);
    prime_need_receipt_graph_free(&graph);
    prime_need_receipt_graph_free(&complete);
    check_production_support_refusal(&arena);
    prime_need_receipt_graph_free(&graph);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("PASS: %zu graph allocation refusals preserve the old view and receipt\n",
           allocations);
    return 0;
}
