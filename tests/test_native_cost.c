#include "native_cost.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static bool fail_event_storage;
static size_t event_storage_attempts;

static void *event_storage_realloc(void *pointer, size_t size) {
    event_storage_attempts++;
    if (fail_event_storage) {
        fail_event_storage = false;
        return NULL;
    }
    return realloc(pointer, size);
}

/* Compile the real implementation into this isolated unit so failures can
 * reach its private allocator and identity boundary without production hooks. */
#define realloc event_storage_realloc
#include "../src/native_cost.c"
#undef realloc

static void test_incomplete_history(void) {
    CettaNativeCostLedger ledger, other;
    cetta_native_cost_init(&ledger);
    cetta_native_cost_init(&other);
    cetta_native_cost_append(&ledger, CETTA_COST_CAPTURE, 3u, 1u);
    CettaNativeCostEvent prefix = ledger.events[0];
    uint64_t saved = atomic_exchange(&next_event_identity, UINT64_MAX);
    size_t attempts = event_storage_attempts;
    cetta_native_cost_append(&ledger, CETTA_COST_CAPTURE, 0u, 1u);
    assert(!ledger.total.trace_incomplete && ledger.total.work == 3u);
    assert(event_storage_attempts == attempts);
    cetta_native_cost_append(&ledger, CETTA_COST_HEAP_UPDATE, 5u, 2u);
    assert(ledger.total.trace_incomplete && ledger.total.work == 8u);
    assert(ledger.total.span == 8u && !ledger.total.overflow);
    assert(ledger.total.counts[CETTA_COST_HEAP_UPDATE] == 5u);
    assert(ledger.length == 1u && !memcmp(&prefix, ledger.events, sizeof(prefix)));
    CettaNativeCostEvent charge;
    CettaNativeCostSnapshot before = cetta_native_cost_read(&ledger, 3u, &charge);
    assert(before.work == 8u && before.trace_incomplete);
    assert(!charge.identity && ledger.total.work == 9u && ledger.length == 1u);
    /* Restoring the isolated test allocator cannot reconstruct lost history. */
    atomic_store(&next_event_identity, saved);
    cetta_native_cost_append(&ledger, CETTA_COST_PUBLISH, 4u, 4u);
    assert(ledger.total.trace_incomplete && ledger.total.work == 13u);
    assert(ledger.length == 1u && !memcmp(&prefix, ledger.events, sizeof(prefix)));
    assert(event_storage_attempts == attempts);
    cetta_native_cost_append(&ledger, CETTA_COST_QUEUE_VISIT, UINT64_MAX, 5u);
    assert(ledger.total.overflow && ledger.total.work == UINT64_MAX);
    cetta_native_cost_append(&ledger, CETTA_COST_QUEUE_VISIT, 0u, 5u);
    assert(ledger.total.overflow && ledger.total.trace_incomplete);

    /* Two empty, incomplete histories do not determine equal numeric work. */
    CettaNativeCostLedger left, right;
    cetta_native_cost_init(&left);
    cetta_native_cost_init(&right);
    saved = atomic_exchange(&next_event_identity, UINT64_MAX);
    cetta_native_cost_append(&left, CETTA_COST_CAPTURE, 2u, 1u);
    cetta_native_cost_append(&right, CETTA_COST_CAPTURE, 7u, 1u);
    atomic_store(&next_event_identity, saved);
    assert(!left.length && !right.length);
    assert(left.total.trace_incomplete && right.total.trace_incomplete);
    assert(left.total.work == 2u && right.total.work == 7u);
    CettaNativeCostScope scope;
    cetta_native_cost_scope_enter(&scope, &other, 8u);
    CettaNativeCostLedger *children[] = {&left, &right};
    assert(cetta_native_cost_join(children, 2u));
    cetta_native_cost_scope_leave(&scope);
    assert(other.total.work == 9u && other.total.span == 7u);
    assert(other.total.trace_incomplete && !other.length);
    assert(!cetta_native_cost_join(children, 2u));
    cetta_native_cost_free(&left);
    cetta_native_cost_free(&right);
    cetta_native_cost_free(&other);
    cetta_native_cost_free(&ledger);
    puts("PASS: refused event identities preserve numerical work, sticky incomplete history and join accounting");
}

static void test_refused_storage(void) {
    CettaNativeCostLedger ledger;
    cetta_native_cost_init(&ledger);
    size_t attempts = event_storage_attempts;
    fail_event_storage = true;
    cetta_native_cost_append(&ledger, CETTA_COST_CAPTURE, 0u, 1u);
    assert(fail_event_storage && event_storage_attempts == attempts);
    assert(!ledger.total.trace_incomplete && !ledger.total.work);
    cetta_native_cost_append(&ledger, CETTA_COST_CAPTURE, 3u, 1u);
    assert(!fail_event_storage && event_storage_attempts == attempts + 1u);
    assert(ledger.total.trace_incomplete && ledger.total.work == 3u);
    assert(!ledger.length && !ledger.events);
    fail_event_storage = true;
    cetta_native_cost_append(&ledger, CETTA_COST_HEAP_UPDATE, 5u, 2u);
    assert(fail_event_storage && event_storage_attempts == attempts + 1u);
    assert(ledger.total.work == 8u && ledger.total.span == 8u);
    assert(ledger.total.counts[CETTA_COST_HEAP_UPDATE] == 5u);
    fail_event_storage = false;
    cetta_native_cost_free(&ledger);

    cetta_native_cost_init(&ledger);
    cetta_native_cost_append(&ledger, CETTA_COST_CAPTURE, 2u, 1u);
    CettaNativeCostEvent prefix = ledger.events[0];
    /* Exercise failure after a retained prefix at the next capacity growth. */
    ledger.capacity = ledger.length;
    fail_event_storage = true;
    cetta_native_cost_append(&ledger, CETTA_COST_HEAP_UPDATE, 4u, 2u);
    assert(!fail_event_storage && ledger.total.trace_incomplete);
    assert(ledger.total.work == 6u && ledger.length == 1u);
    assert(!memcmp(&prefix, ledger.events, sizeof(prefix)));
    cetta_native_cost_append(&ledger, CETTA_COST_PUBLISH, 3u, 3u);
    assert(ledger.total.work == 9u && ledger.length == 1u);
    assert(!memcmp(&prefix, ledger.events, sizeof(prefix)));
    cetta_native_cost_free(&ledger);
    puts("PASS: refused receipt storage leaves ordered prefixes intact and continues numeric accounting");
}

static void test_scoped_reads(void) {
    CettaNativeCostLedger first, second, unrelated;
    cetta_native_cost_init(&first);
    cetta_native_cost_init(&second);
    cetta_native_cost_init(&unrelated);
    assert(current_scope == NULL);
    CettaNativeCostScope outer, middle, inner;
    cetta_native_cost_scope_enter(&outer, &first, 10u);
    cetta_native_cost_scope_enter(&middle, &second, 11u);
    cetta_native_cost_scope_enter(&inner, &first, 12u);
    uint64_t next = atomic_load(&next_event_identity);
    assert(next > 0u && next < UINT64_MAX - 4u);
    CettaNativeCostEvent charge;
    CettaNativeCostSnapshot before = cetta_native_cost_read(&first, 30u, &charge);
    assert(before.work == 0u && before.counts[CETTA_COST_READ] == 0u);
    assert(first.total.work == 1u && second.total.work == 1u);
    assert(unrelated.total.work == 0u && unrelated.length == 0u);
    assert(current_scope == &inner && inner.previous == &middle && middle.previous == &outer);
    assert(inner.occurrence == 12u && middle.occurrence == 11u && outer.occurrence == 10u);
    assert(charge.identity == next && atomic_load(&next_event_identity) == next + 1u);
    assert(charge.origin == first.identity && charge.occurrence == 30u);
    assert(first.length == 1u && second.length == 1u);
    assert(first.events[0].identity == charge.identity && second.events[0].identity == charge.identity);
    assert(first.events[0].origin == first.identity && second.events[0].origin == first.identity);

    CettaNativeCostSnapshot absent = cetta_native_cost_read(NULL, 31u, &charge);
    assert(absent.work == 0u && absent.trace_incomplete);
    assert(first.total.work == 2u && second.total.work == 2u);
    assert(charge.identity == next + 1u && charge.origin == 0u && charge.occurrence == 31u);
    assert(first.length == 2u && second.length == 2u);
    assert(first.events[1].identity == charge.identity && second.events[1].identity == charge.identity);
    assert(first.events[1].origin == 0u && second.events[1].origin == 0u);
    assert(current_scope == &inner && inner.occurrence == 12u);

    CettaNativeCostSnapshot later = cetta_native_cost_read(&first, 30u, &charge);
    assert(later.work == 2u && later.counts[CETTA_COST_READ] == 2u);
    assert(first.total.work == 3u && second.total.work == 3u);
    assert(charge.identity == next + 2u && atomic_load(&next_event_identity) == next + 3u);
    CettaNativeCostEvent prefix = first.events[2];
    assert(prefix.identity == charge.identity && prefix.repetitions == 1u);

    uint64_t saved = atomic_exchange(&next_event_identity, UINT64_MAX);
    size_t attempts = event_storage_attempts;
    CettaNativeCostSnapshot exhausted = cetta_native_cost_read(&first, 32u, &charge);
    assert(exhausted.work == 3u && !exhausted.trace_incomplete);
    assert(charge.identity == 0u && atomic_load(&next_event_identity) == UINT64_MAX);
    assert(first.total.work == 4u && second.total.work == 4u);
    assert(first.total.trace_incomplete && second.total.trace_incomplete);
    assert(first.length == 3u && second.length == 3u);
    assert(!memcmp(&first.events[2], &prefix, sizeof(prefix)));
    assert(event_storage_attempts == attempts && current_scope == &inner);
    atomic_store(&next_event_identity, saved);
    cetta_native_cost_scope_leave(&inner);
    cetta_native_cost_scope_leave(&middle);
    cetta_native_cost_scope_leave(&outer);
    assert(current_scope == NULL);

    attempts = event_storage_attempts;
    CettaNativeCostSnapshot alone = cetta_native_cost_read(NULL, 33u, &charge);
    assert(alone.work == 0u && alone.trace_incomplete);
    assert(charge.identity == saved && charge.origin == 0u);
    assert(atomic_load(&next_event_identity) == saved + 1u && current_scope == NULL);
    assert(event_storage_attempts == attempts && unrelated.total.work == 0u);
    cetta_native_cost_free(&unrelated);
    cetta_native_cost_free(&second);
    cetta_native_cost_free(&first);
}

int main(void) {
    test_scoped_reads();
    assert(CETTA_COST_QUANTUM == 0 && CETTA_COST_STORAGE_PROBE == 27);
    assert(CETTA_COST_COEFFICIENT_HANDLE == 28 && CETTA_COST_KIND_COUNT == 29);
    assert(!strcmp(cetta_native_cost_name(CETTA_COST_COEFFICIENT_HANDLE),
                   "coefficient-handle"));
    test_incomplete_history();
    test_refused_storage();
    CettaNativeCostLedger boundary;
    cetta_native_cost_init(&boundary);
    cetta_native_cost_append(&boundary, CETTA_COST_QUEUE_VISIT, UINT64_MAX, 1u);
    assert(boundary.total.work == UINT64_MAX && !boundary.total.overflow);
    assert(boundary.total.span == UINT64_MAX &&
           boundary.total.counts[CETTA_COST_QUEUE_VISIT] == UINT64_MAX);
    cetta_native_cost_append(&boundary, CETTA_COST_QUEUE_VISIT, 1u, 1u);
    assert(boundary.total.work == UINT64_MAX && boundary.total.overflow);
    cetta_native_cost_append(&boundary, CETTA_COST_QUEUE_VISIT, 0u, 1u);
    assert(boundary.total.work == UINT64_MAX && boundary.total.overflow);
    cetta_native_cost_append(&boundary, CETTA_COST_CAPTURE, 3u, 2u);
    assert(boundary.total.work == UINT64_MAX && boundary.total.overflow);
    assert(boundary.total.counts[CETTA_COST_CAPTURE] == 3u);
    cetta_native_cost_free(&boundary);
    CettaNativeCostLedger parent, child;
    assert(!cetta_native_cost_active());
    assert(cetta_native_cost_charge(CETTA_COST_CAPTURE, 2u) == 0u);
    assert(cetta_native_cost_name((CettaNativeCostKind)-1)[0] == 'i');
    cetta_native_cost_init(&parent);
    cetta_native_cost_init(&child);
    CettaNativeCostScope outer, inner, repeated;
    cetta_native_cost_scope_enter(&outer, &parent, 1u);
    assert(cetta_native_cost_active());
    cetta_native_cost_charge(CETTA_COST_CAPTURE, 2u);
    cetta_native_cost_scope_enter(&inner, &child, 3u);
    cetta_native_cost_charge(CETTA_COST_HEAP_LOOKUP, 4u);
    cetta_native_cost_scope_enter(&repeated, &child, 5u);
    cetta_native_cost_charge(CETTA_COST_PUBLISH, 1u);
    cetta_native_cost_scope_leave(&repeated);
    cetta_native_cost_scope_leave(&inner);
    assert(parent.total.work == 7u && child.total.work == 5u);
    assert(child.length == 2u && child.events[1].occurrence == 5u);
    assert(parent.events[1].identity == child.events[0].identity);
    assert(parent.events[2].identity == child.events[1].identity);
    assert(parent.events[1].origin == child.identity);
    assert(parent.events[1].occurrence == child.events[0].occurrence);
    /* Nested totals overlap: their sum double-counts both shared events. */
    assert(parent.total.work + child.total.work == 12u);
    assert(parent.total.work == 2u + child.total.work);
    CettaNativeCostSnapshot before = cetta_native_cost_read(&child, 8u, NULL);
    assert(before.work == 5u && before.counts[CETTA_COST_READ] == 0u);
    assert(child.total.work == 6u && child.events[2].kind == CETTA_COST_READ);
    assert(parent.total.work == 8u && parent.events[3].identity == child.events[2].identity);
    cetta_native_cost_charge(CETTA_COST_CAPTURE, 2u);
    cetta_native_cost_charge(CETTA_COST_CAPTURE, 3u);
    assert(parent.events[parent.length - 1u].units == 3u);
    assert(parent.events[parent.length - 2u].units == 2u);
    cetta_native_cost_charge(CETTA_COST_CAPTURE, 3u);
    assert(parent.events[parent.length - 1u].repetitions == 2u);
    cetta_native_cost_scope_leave(&outer);
    assert(!cetta_native_cost_active());
    cetta_native_cost_charge(CETTA_COST_PUBLISH, 50u);
    assert(parent.total.work == 16u);
    cetta_native_cost_append(&child, CETTA_COST_QUEUE_VISIT, UINT64_MAX, 9u);
    assert(child.total.work == UINT64_MAX && child.total.overflow);
    assert(!child.total.trace_incomplete);
    cetta_native_cost_append(&child, CETTA_COST_QUEUE_VISIT, 1u, 9u);
    assert(child.total.counts[CETTA_COST_QUEUE_VISIT] == UINT64_MAX);
    assert(child.events[child.length - 1u].units == 1u);
    CettaNativeCostEvent read_charge;
    uint64_t parent_before = parent.total.work;
    CettaNativeCostSnapshot outside = cetta_native_cost_read(&parent, 91u, &read_charge);
    assert(outside.work == parent_before && parent.total.work == parent_before + 1u);
    assert(read_charge.identity && read_charge.origin == parent.identity &&
           read_charge.occurrence == 91u && read_charge.units == 1u);
    cetta_native_cost_scope_enter(&outer, &parent, 92u);
    cetta_native_cost_scope_enter(&repeated, &parent, 93u);
    parent_before = parent.total.work;
    CettaNativeCostSnapshot reentered = cetta_native_cost_read(&parent, 94u, &read_charge);
    assert(reentered.work == parent_before && parent.total.work == parent_before + 1u);
    cetta_native_cost_scope_leave(&repeated);
    cetta_native_cost_scope_leave(&outer);
    cetta_native_cost_free(&child);
    cetta_native_cost_free(&parent);
    puts("PASS: native operation receipts, scope attribution, read boundary and overflow");
    return 0;
}
