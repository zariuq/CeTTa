#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);
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
    assert(prime_need_receipt_graph_collect(&receipt, &graph) ==
           PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 80u);
    prime_need_receipt_graph_free(&graph);
    prime_need_receipt_graph_free(&complete);
    prime_need_receipt_graph_free(&graph);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("PASS: %zu graph allocation refusals preserve the old view and receipt\n",
           allocations);
    return 0;
}
