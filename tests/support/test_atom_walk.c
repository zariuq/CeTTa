#include "atom_walk.h"
#include "term_domain.h"
#include <stdlib.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static int allocation_allowance = -1;
static bool allocation_armed;
static size_t allocation_count;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
static bool allocation_allowed(void) {
    if (!allocation_armed) return true;
    ++allocation_count;
    if (!allocation_allowance) return false;
    if (allocation_allowance > 0) --allocation_allowance;
    return true;
}
void *__wrap_malloc(size_t size) { return allocation_allowed() ? __real_malloc(size) : NULL; }
void *__wrap_calloc(size_t count, size_t size) { return allocation_allowed() ? __real_calloc(count, size) : NULL; }
void *__wrap_realloc(void *pointer, size_t size) { return allocation_allowed() ? __real_realloc(pointer, size) : NULL; }
static AtomWalk *new_walk(const Atom *root, bool unique) {
    allocation_armed = true;
    AtomWalk *walk = atom_walk_new(root, unique);
    allocation_armed = false;
    return walk;
}
static AtomWalkStep next_walk(AtomWalk *walk, AtomWalkEvent *event) {
    allocation_armed = true;
    AtomWalkStep step = atom_walk_next(walk, event);
    allocation_armed = false;
    return step;
}

typedef struct { size_t enters, leaves, shared, depth; } Counts;
typedef struct { AtomWalkStep step; AtomWalkEvent event; } TraceItem;
typedef struct { TraceItem *items; size_t len, cap; } Trace;

static bool trace_until_stop(AtomWalk *walk, Trace *trace) {
    for (size_t i = 0u; i < 100000u; ++i) {
        AtomWalkEvent event;
        AtomWalkStep step = next_walk(walk, &event);
        if (step == ATOM_WALK_RESOURCE) return true;
        if (step == ATOM_WALK_DONE) return false;
        assert(step == ATOM_WALK_ENTER || step == ATOM_WALK_LEAVE || step == ATOM_WALK_SHARED);
        if (trace->len == trace->cap) {
            size_t cap = trace->cap ? trace->cap * 2u : 32u;
            TraceItem *items = realloc(trace->items, cap * sizeof(*items)); assert(items);
            trace->items = items; trace->cap = cap;
        }
        trace->items[trace->len++] = (TraceItem){step, event};
    }
    assert(!"trace did not finish");
    return false;
}
static void allocation_sweep(const Atom *root) {
    allocation_count = 0u;
    AtomWalk *walk = new_walk(root, true); assert(walk);
    Trace expected = {0};
    assert(!trace_until_stop(walk, &expected));
    size_t allocations = allocation_count;
    atom_walk_free(walk);
    assert(allocations > 5u);
    for (size_t failure = 0u; failure < allocations; ++failure) {
        allocation_allowance = (int)failure;
        walk = new_walk(root, true);
        if (!walk) { assert(failure == 0u); continue; }
        Trace actual = {0};
        assert(trace_until_stop(walk, &actual));
        allocation_allowance = -1;
        assert(!trace_until_stop(walk, &actual));
        assert(actual.len == expected.len);
        for (size_t i = 0u; i < expected.len; ++i) {
            const TraceItem *a = &actual.items[i], *b = &expected.items[i];
            assert(a->step == b->step && a->event.node == b->event.node &&
                a->event.parent == b->event.parent && a->event.child_index == b->event.child_index &&
                a->event.depth == b->event.depth);
        }
        atom_walk_free(walk); free(actual.items);
    }
    allocation_allowance = -1;
    free(expected.items);
}
static void drain(AtomWalk *walk, Counts *counts) {
    for (size_t i = 0u; i < 1000000u; ++i) {
        AtomWalkEvent event;
        AtomWalkStep step = next_walk(walk, &event);
        if (step == ATOM_WALK_DONE) return;
        assert(step == ATOM_WALK_ENTER || step == ATOM_WALK_LEAVE || step == ATOM_WALK_SHARED);
        assert(event.node);
        if (event.parent) {
            assert(event.parent->kind == ATOM_EXPR);
            assert(event.child_index < event.parent->expr.len);
            assert(event.parent->expr.elems[event.child_index] == event.node);
        } else assert(!event.depth && !event.child_index);
        if (step == ATOM_WALK_ENTER) ++counts->enters;
        if (step == ATOM_WALK_LEAVE) ++counts->leaves;
        if (step == ATOM_WALK_SHARED) ++counts->shared;
        if (event.depth > counts->depth) counts->depth = event.depth;
    }
    assert(!"walk did not finish");
}
int main(void) {
    CettaTermDomain *domain = cetta_term_domain_new_default(false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    Arena arena;
    arena_init_detached(&arena);
    Atom *head = atom_symbol(&arena, "node");
    Atom *leaf = atom_int(&arena, 7);
    Atom *deep = leaf;
    const size_t depth = 12000u;
    for (size_t i = 0u; i < depth; ++i) deep = atom_expr2(&arena, head, deep);
    assert(deep);
    for (unsigned unique = 0u; unique < 2u; ++unique) {
        AtomWalk *walk = new_walk(deep, unique);
        assert(walk);
        Counts counts = {0}; drain(walk, &counts);
        assert(counts.enters == (unique ? depth + 2u : 2u * depth + 1u));
        assert(counts.leaves == counts.enters && counts.depth == depth);
        assert(counts.shared == (unique ? depth - 1u : 0u));
        atom_walk_free(walk);
    }
    Atom *sub = atom_expr2(&arena, head, leaf);
    Atom *dag = atom_expr3(&arena, head, sub, sub);
    for (unsigned unique = 0u; unique < 2u; ++unique) {
        AtomWalk *walk = new_walk(dag, unique);
        assert(walk);
        Counts counts = {0}; drain(walk, &counts);
        assert(counts.enters == (unique ? 4u : 8u));
        assert(counts.leaves == counts.enters && counts.shared == (unique ? 2u : 0u));
        atom_walk_free(walk);
    }
    puts("walks preserve occurrence order, parent coordinates and shared DAG distinctions beyond the old nesting cap");

    Atom *middle = atom_expr2(&arena, atom_int(&arena, 2), atom_int(&arena, 3));
    Atom *ordered = atom_expr3(&arena, atom_int(&arena, 1), middle, atom_int(&arena, 4));
    AtomWalk *order_walk = new_walk(ordered, false); assert(order_walk);
    int64_t expected_leaf = 1;
    for (;;) {
        AtomWalkEvent item;
        AtomWalkStep status = next_walk(order_walk, &item);
        if (status == ATOM_WALK_DONE) break;
        assert(status == ATOM_WALK_ENTER || status == ATOM_WALK_LEAVE);
        if (status == ATOM_WALK_ENTER && item.node->kind == ATOM_GROUNDED) {
            assert(item.node->ground.gkind == GV_INT && item.node->ground.ival == expected_leaf);
            ++expected_leaf;
        }
    }
    assert(expected_leaf == 5); atom_walk_free(order_walk);
    Atom **items = malloc(sizeof(*items) * 5000u); assert(items);
    for (size_t i = 0u; i < 5000u; ++i) items[i] = leaf;
    Atom *wide = atom_expr(&arena, items, 5000u); free(items); assert(wide);
    order_walk = new_walk(wide, false); assert(order_walk);
    Counts wide_counts = {0}; drain(order_walk, &wide_counts);
    assert(wide_counts.enters == 5001u && wide_counts.leaves == 5001u && wide_counts.depth == 1u);
    atom_walk_free(order_walk);
    puts("heterogeneous nesting keeps left-to-right order and wide nodes retain every occurrence");

    AtomWalk *walk = new_walk(deep, false);
    assert(walk);
    allocation_allowance = 0;
    size_t prefix = 0u;
    for (;;) {
        AtomWalkEvent event;
        AtomWalkStep step = next_walk(walk, &event);
        if (step == ATOM_WALK_RESOURCE) break;
        assert(step == ATOM_WALK_ENTER); ++prefix;
        /* Each scalar head leaves before the next nested child. */
        if (event.node == head) {
            assert(next_walk(walk, &event) == ATOM_WALK_LEAVE);
        }
    }
    allocation_allowance = -1;
    Counts suffix = {0}; drain(walk, &suffix);
    assert(prefix + suffix.enters == 2u * depth + 1u);
    atom_walk_free(walk);
    allocation_allowance = 0;
    assert(!new_walk(leaf, false));
    allocation_allowance = -1;
    puts("allocation refusal retains the pending traversal and resumes without losing an occurrence");
    Atom *sweep = leaf;
    for (size_t i = 0u; i < 130u; ++i) sweep = atom_expr2(&arena, head, sweep);
    allocation_sweep(sweep);
    puts("every cursor allocation failure resumes to the complete unmodified event trace");

    Atom *cycle = atom_expr(&arena, &leaf, 1u);
    assert(cycle); cycle->expr.elems[0] = cycle;
    AtomWalkEvent event;
    walk = new_walk(cycle, false); assert(walk);
    assert(next_walk(walk, &event) == ATOM_WALK_ENTER);
    assert(next_walk(walk, &event) == ATOM_WALK_CYCLE);
    atom_walk_free(walk);
    walk = new_walk(cycle, false); assert(walk);
    assert(next_walk(walk, &event) == ATOM_WALK_ENTER && atom_walk_prune(walk));
    assert(next_walk(walk, &event) == ATOM_WALK_LEAVE);
    assert(next_walk(walk, &event) == ATOM_WALK_DONE);
    atom_walk_free(walk);
    Atom *twice = atom_expr2(&arena, cycle, cycle);
    walk = new_walk(twice, false); assert(walk);
    assert(next_walk(walk, &event) == ATOM_WALK_ENTER);
    assert(next_walk(walk, &event) == ATOM_WALK_ENTER && event.node == cycle);
    assert(atom_walk_prune(walk));
    assert(next_walk(walk, &event) == ATOM_WALK_LEAVE);
    assert(next_walk(walk, &event) == ATOM_WALK_ENTER && event.node == cycle);
    assert(next_walk(walk, &event) == ATOM_WALK_CYCLE);
    atom_walk_free(walk);
    cycle->expr.elems[0] = leaf;
    walk = new_walk(NULL, false); assert(walk);
    assert(next_walk(walk, &event) == ATOM_WALK_INVALID);
    atom_walk_free(walk);
    puts("cyclic syntax is distinct from shared data; pruning one occurrence does not hide a later cycle");
    arena_free(&arena);
    cetta_term_domain_leave(&scope); cetta_term_domain_release(domain);
    puts("PASS: structural worklist traversal controls");
    return 0;
}
