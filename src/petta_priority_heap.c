#include "petta_priority_heap.h"

#include "petta_semantics.h"
#include "stats.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Atom *value;
    Atom *key;
    Atom *children;
} PettaPairingNode;

static Atom *petta_priority_heap_compound_body(Atom *atom) {
    Atom *body = NULL;
    return atom_petta_prolog_compound_body(atom, &body)
        ? body : atom;
}

static bool petta_priority_heap_node(
        Atom *atom, PettaPairingNode *node) {
    atom = petta_priority_heap_compound_body(atom);
    if (!atom || !node || atom->kind != ATOM_EXPR ||
        atom->expr.len != 4u ||
        !atom_is_symbol(atom->expr.elems[0], "t") ||
        !atom->expr.elems[1] || !atom->expr.elems[2] ||
        !atom->expr.elems[3] ||
        atom->expr.elems[3]->kind != ATOM_EXPR ||
        atom_is_list_rest(atom->expr.elems[3])) {
        return false;
    }
    *node = (PettaPairingNode){
        .value = atom->expr.elems[1],
        .key = atom->expr.elems[2],
        .children = atom->expr.elems[3],
    };
    return true;
}

static bool petta_priority_heap_carrier(
        Atom *atom, Atom **root, int64_t *size) {
    atom = petta_priority_heap_compound_body(atom);
    if (!atom || !root || !size || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "heap") ||
        !atom->expr.elems[1] || !atom->expr.elems[2] ||
        atom->expr.elems[2]->kind != ATOM_GROUNDED ||
        atom->expr.elems[2]->ground.gkind != GV_INT) {
        return false;
    }
    *root = atom->expr.elems[1];
    *size = atom->expr.elems[2]->ground.ival;
    return true;
}

static Atom *petta_priority_heap_expression4(
        Arena *arena, Atom *a, Atom *b, Atom *c, Atom *d) {
    Atom **items = arena_alloc(arena, 4u * sizeof(*items));
    if (!items)
        return NULL;
    items[0] = a;
    items[1] = b;
    items[2] = c;
    items[3] = d;
    return atom_expr(arena, items, 4u);
}

static Atom *petta_priority_heap_compound3(
        Arena *arena, Atom *a, Atom *b, Atom *c) {
    Atom *body = atom_expr3(arena, a, b, c);
    return body ? atom_petta_prolog_compound(arena, body) : NULL;
}

static Atom *petta_priority_heap_compound4(
        Arena *arena, Atom *a, Atom *b, Atom *c, Atom *d) {
    Atom *body = petta_priority_heap_expression4(arena, a, b, c, d);
    return body ? atom_petta_prolog_compound(arena, body) : NULL;
}

static Atom *petta_priority_heap_prepend_child(
        Arena *arena, Atom *root, Atom *child) {
    PettaPairingNode node;
    if (!petta_priority_heap_node(root, &node) ||
        node.children->expr.len == UINT32_MAX)
        return NULL;
    CettaExprLen length = node.children->expr.len;
    Atom **children = arena_alloc(
        arena, ((size_t)length + 1u) * sizeof(*children));
    if (!children)
        return NULL;
    children[0] = child;
    if (length)
        memcpy(children + 1u, node.children->expr.elems,
               (size_t)length * sizeof(*children));
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PETTA_PRIORITY_HEAP_CHILD_COPY,
        (uint64_t)length);
    Atom *list = atom_expr(arena, children, length + 1u);
    return list
        ? petta_priority_heap_compound4(
              arena, atom_symbol(arena, "t"), node.value, node.key, list)
        : NULL;
}

static Atom *petta_priority_heap_meld(
        Arena *arena, Atom *left, Atom *right, bool *supported) {
    if (atom_is_symbol(left, "nil"))
        return right;
    if (atom_is_symbol(right, "nil"))
        return left;
    PettaPairingNode lnode, rnode;
    if (!petta_priority_heap_node(left, &lnode) ||
        !petta_priority_heap_node(right, &rnode)) {
        *supported = false;
        return NULL;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_PRIORITY_HEAP_MELD);
    int order = 0;
    if (!petta_semantics_term_compare(lnode.key, rnode.key, &order)) {
        *supported = false;
        return NULL;
    }
    /* SWI's meld uses Px @< Py. Equal keys therefore select the newly
     * inserted/right root, giving the observable LIFO tie order. */
    return order < 0
        ? petta_priority_heap_prepend_child(arena, left, right)
        : petta_priority_heap_prepend_child(arena, right, left);
}

static Atom *petta_priority_heap_pair_children(
        Arena *arena, Atom *children, bool *supported) {
    if (!children || children->kind != ATOM_EXPR ||
        atom_is_list_rest(children)) {
        *supported = false;
        return NULL;
    }
    size_t count = children->expr.len;
    if (count == 0u)
        return atom_symbol(arena, "nil");
    if (count == 1u)
        return children->expr.elems[0];
    size_t pairs = (count + 1u) / 2u;
    Atom **merged = arena_alloc(arena, pairs * sizeof(*merged));
    if (!merged)
        return NULL;
    size_t source = 0u;
    size_t output = 0u;
    while (source + 1u < count) {
        merged[output] = petta_priority_heap_meld(
            arena, children->expr.elems[source],
            children->expr.elems[source + 1u], supported);
        if (!merged[output])
            return NULL;
        output++;
        source += 2u;
    }
    if (source < count)
        merged[output++] = children->expr.elems[source];
    Atom *result = merged[output - 1u];
    while (output-- > 1u) {
        result = petta_priority_heap_meld(
            arena, merged[output - 1u], result, supported);
        if (!result)
            return NULL;
    }
    return result;
}

static bool petta_priority_heap_ground_finite(Atom *atom) {
    return atom && !atom_has_vars(atom) &&
        !atom_structural_has_rational(atom);
}

static PettaPriorityHeapStatus petta_priority_heap_empty(
        Arena *arena, PettaPriorityHeapAnswer *answer) {
    Atom *heap = atom_symbol(arena, "heap");
    Atom *nil = atom_symbol(arena, "nil");
    Atom *zero = atom_int(arena, 0);
    answer->result = heap && nil && zero
        ? petta_priority_heap_compound3(arena, heap, nil, zero) : NULL;
    return answer->result
        ? PETTA_PRIORITY_HEAP_ANSWER : PETTA_PRIORITY_HEAP_CAPACITY;
}

static PettaPriorityHeapStatus petta_priority_heap_add(
        Arena *arena, Atom *heap, Atom *key, Atom *value,
        PettaPriorityHeapAnswer *answer) {
    Atom *root = NULL;
    int64_t size = 0;
    if (!petta_priority_heap_carrier(heap, &root, &size) || size < 0 ||
        size == INT64_MAX || !petta_priority_heap_ground_finite(heap) ||
        !petta_priority_heap_ground_finite(key) ||
        !petta_priority_heap_ground_finite(value)) {
        return PETTA_PRIORITY_HEAP_DECLINED;
    }
    Atom *empty = atom_expr(arena, NULL, 0u);
    Atom *node = empty
        ? petta_priority_heap_compound4(
              arena, atom_symbol(arena, "t"), value, key, empty)
        : NULL;
    if (!node)
        return PETTA_PRIORITY_HEAP_CAPACITY;
    bool supported = true;
    Atom *combined = petta_priority_heap_meld(
        arena, root, node, &supported);
    if (!combined)
        return supported
            ? PETTA_PRIORITY_HEAP_CAPACITY : PETTA_PRIORITY_HEAP_DECLINED;
    answer->result = petta_priority_heap_compound3(
        arena, atom_symbol(arena, "heap"), combined,
        atom_int(arena, size + 1));
    return answer->result
        ? PETTA_PRIORITY_HEAP_ANSWER : PETTA_PRIORITY_HEAP_CAPACITY;
}

static PettaPriorityHeapStatus petta_priority_heap_take(
        Arena *arena, Atom *heap, PettaPriorityHeapAnswer *answer) {
    Atom *root = NULL;
    int64_t size = 0;
    if (!petta_priority_heap_carrier(heap, &root, &size) || size < 0 ||
        !petta_priority_heap_ground_finite(heap)) {
        return PETTA_PRIORITY_HEAP_DECLINED;
    }
    if (atom_is_symbol(root, "nil"))
        return size == 0
            ? PETTA_PRIORITY_HEAP_NO_ANSWER : PETTA_PRIORITY_HEAP_DECLINED;
    PettaPairingNode node;
    if (size == 0 || !petta_priority_heap_node(root, &node))
        return PETTA_PRIORITY_HEAP_DECLINED;
    bool supported = true;
    Atom *remaining = petta_priority_heap_pair_children(
        arena, node.children, &supported);
    if (!remaining)
        return supported
            ? PETTA_PRIORITY_HEAP_CAPACITY : PETTA_PRIORITY_HEAP_DECLINED;
    answer->key = node.key;
    answer->value = node.value;
    answer->result = petta_priority_heap_compound3(
        arena, atom_symbol(arena, "heap"), remaining,
        atom_int(arena, size - 1));
    return answer->result
        ? PETTA_PRIORITY_HEAP_ANSWER : PETTA_PRIORITY_HEAP_CAPACITY;
}

static PettaPriorityHeapStatus petta_priority_heap_size(
        Arena *arena, Atom *heap, PettaPriorityHeapAnswer *answer) {
    Atom *root = NULL;
    int64_t size = 0;
    if (!petta_priority_heap_carrier(heap, &root, &size))
        return PETTA_PRIORITY_HEAP_DECLINED;
    answer->result = atom_int(arena, size);
    return answer->result
        ? PETTA_PRIORITY_HEAP_ANSWER : PETTA_PRIORITY_HEAP_CAPACITY;
}

PettaPriorityHeapStatus petta_priority_heap_call(
        Arena *arena, Atom *resolved_call,
        PettaPriorityHeapAnswer *answer) {
    if (!arena || !resolved_call || !answer ||
        resolved_call->kind != ATOM_EXPR ||
        resolved_call->expr.len == 0u ||
        !resolved_call->expr.elems[0] ||
        resolved_call->expr.elems[0]->kind != ATOM_SYMBOL) {
        return PETTA_PRIORITY_HEAP_DECLINED;
    }
    memset(answer, 0, sizeof(*answer));
    Atom *head = resolved_call->expr.elems[0];
    if (atom_is_symbol(head, "empty_heap") &&
        resolved_call->expr.len == 1u) {
        return petta_priority_heap_empty(arena, answer);
    }
    if (atom_is_symbol(head, "add_to_heap") &&
        resolved_call->expr.len == 4u) {
        return petta_priority_heap_add(
            arena, resolved_call->expr.elems[1],
            resolved_call->expr.elems[2], resolved_call->expr.elems[3],
            answer);
    }
    if (atom_is_symbol(head, "get_from_heap") &&
        resolved_call->expr.len == 4u) {
        return petta_priority_heap_take(
            arena, resolved_call->expr.elems[1], answer);
    }
    if (atom_is_symbol(head, "heap_size") &&
        resolved_call->expr.len == 2u) {
        return petta_priority_heap_size(
            arena, resolved_call->expr.elems[1], answer);
    }
    return PETTA_PRIORITY_HEAP_DECLINED;
}

PettaPriorityHeapStatus petta_priority_heap_validate(Atom *heap,
        bool (*interrupted)(void), bool *valid) {
    typedef struct { Atom *root, *parent; } Pending;
    *valid = false;
    Atom *root = NULL;
    int64_t size = 0;
    if (!petta_priority_heap_carrier(heap, &root, &size) || size < 0 ||
        !petta_priority_heap_ground_finite(heap)) return PETTA_PRIORITY_HEAP_ANSWER;
    if (atom_is_symbol(root, "nil")) {
        *valid = size == 0;
        return PETTA_PRIORITY_HEAP_ANSWER;
    }
    size_t capacity = 16, length = 1;
    uint64_t visited = 0;
    Pending *pending = malloc(capacity * sizeof(*pending));
    if (!pending) return PETTA_PRIORITY_HEAP_CAPACITY;
    pending[0] = (Pending){root, NULL};
    while (length) {
        if (interrupted && interrupted()) break;
        Pending next = pending[--length];
        PettaPairingNode node;
        int order = 0;
        if (++visited > (uint64_t)size || !petta_priority_heap_node(next.root, &node) ||
            (next.parent && (!petta_semantics_term_compare(next.parent, node.key, &order) || order > 0)))
            break;
        size_t children = node.children->expr.len;
        if (children > SIZE_MAX - length || length + children > SIZE_MAX / sizeof(*pending)) {
            free(pending); return PETTA_PRIORITY_HEAP_CAPACITY;
        }
        size_t needed = length + children;
        if ((uint64_t)needed > (uint64_t)size - visited) break;
        if (needed > capacity) {
            size_t grown = capacity > SIZE_MAX / 2 ? needed : capacity * 2;
            if (grown < needed) grown = needed;
            if (grown > SIZE_MAX / sizeof(*pending)) grown = needed;
            Pending *more = realloc(pending, grown * sizeof(*pending));
            if (!more) { free(pending); return PETTA_PRIORITY_HEAP_CAPACITY; }
            pending = more; capacity = grown;
        }
        for (size_t i = 0; i < children; i++)
            pending[length++] = (Pending){node.children->expr.elems[i], node.key};
        if (!length) *valid = visited == (uint64_t)size;
    }
    free(pending);
    return PETTA_PRIORITY_HEAP_ANSWER;
}
