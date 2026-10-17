#ifndef CETTA_PREPARED_UNARY_INDEX_V1_H
#define CETTA_PREPARED_UNARY_INDEX_V1_H

#include "src/atom.h"
#include <stdlib.h>

/* A local structural identity for symbol/unary-constructor addresses. The
 * key is (head, parent identity), not the hash of a recursively nested Atom.
 * Pointer memoization visits shared input once; structural keys also merge
 * equal addresses stored at different pointers. Neither table owns Atoms. */
typedef struct { SymbolId head; uint32_t parent, row; } CettaPreparedUnaryNodeV1;
typedef struct { const Atom *atom; uint32_t identity; bool pending; } CettaPreparedUnaryMemoV1;
typedef struct {
    CettaPreparedUnaryNodeV1 *nodes;
    uint32_t *keys;
    CettaPreparedUnaryMemoV1 *memo;
    const Atom **path;
    size_t len, capacity, key_capacity, memo_len, memo_capacity, path_capacity;
    bool unavailable;
} CettaPreparedUnaryIndexV1;

static inline bool cpui_unavailable(CettaPreparedUnaryIndexV1 *index) {
    index->unavailable = true; return false;
}

static inline size_t cpui_hash(uint64_t key) {
    key ^= key >> 30u; key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27u; key *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(key ^ (key >> 31u));
}
static inline void cpui_free(CettaPreparedUnaryIndexV1 *index) {
    free(index->nodes); free(index->keys); free(index->memo); free(index->path);
    *index = (CettaPreparedUnaryIndexV1){0};
}
static inline bool cpui_memo_grow(CettaPreparedUnaryIndexV1 *index) {
    if (index->memo_len + 1u <= index->memo_capacity / 2u) return true;
    size_t next = index->memo_capacity ? index->memo_capacity * 2u : 64u;
    if (next < index->memo_capacity || next > SIZE_MAX / sizeof(*index->memo)) return cpui_unavailable(index);
    CettaPreparedUnaryMemoV1 *memo = calloc(next, sizeof(*memo));
    if (!memo) return cpui_unavailable(index);
    for (size_t i = 0; i < index->memo_capacity; ++i) if (index->memo[i].atom) {
        size_t at = cpui_hash((uintptr_t)index->memo[i].atom) & (next - 1u);
        while (memo[at].atom) at = (at + 1u) & (next - 1u);
        memo[at] = index->memo[i];
    }
    free(index->memo); index->memo = memo; index->memo_capacity = next; return true;
}
static inline CettaPreparedUnaryMemoV1 *cpui_memo(CettaPreparedUnaryIndexV1 *index, const Atom *atom) {
    size_t at = cpui_hash((uintptr_t)atom) & (index->memo_capacity - 1u);
    while (index->memo[at].atom && index->memo[at].atom != atom) at = (at + 1u) & (index->memo_capacity - 1u);
    return &index->memo[at];
}
static inline bool cpui_intern(CettaPreparedUnaryIndexV1 *index, SymbolId head,
                               uint32_t parent, uint32_t *identity) {
    if (index->len + 1u > index->key_capacity / 2u) {
        size_t next = index->key_capacity ? index->key_capacity * 2u : 64u;
        if (next < index->key_capacity || next > SIZE_MAX / sizeof(*index->keys)) return cpui_unavailable(index);
        uint32_t *keys = calloc(next, sizeof(*keys));
        if (!keys) return cpui_unavailable(index);
        for (uint32_t i = 0; i < index->len; ++i) {
            CettaPreparedUnaryNodeV1 node = index->nodes[i];
            size_t at = cpui_hash(((uint64_t)node.head << 32u) ^ node.parent) & (next - 1u);
            while (keys[at]) at = (at + 1u) & (next - 1u);
            keys[at] = i + 1u;
        }
        free(index->keys); index->keys = keys; index->key_capacity = next;
    }
    size_t at = cpui_hash(((uint64_t)head << 32u) ^ parent) & (index->key_capacity - 1u);
    while (index->keys[at]) {
        uint32_t id = index->keys[at] - 1u;
        if (index->nodes[id].head == head && index->nodes[id].parent == parent) { *identity = id; return true; }
        at = (at + 1u) & (index->key_capacity - 1u);
    }
    if (index->len >= UINT32_MAX) return cpui_unavailable(index);
    if (index->len == index->capacity) {
        size_t next = index->capacity ? index->capacity * 2u : 32u;
        if (next < index->capacity || next > SIZE_MAX / sizeof(*index->nodes)) return cpui_unavailable(index);
        void *nodes = realloc(index->nodes, next * sizeof(*index->nodes));
        if (!nodes) return cpui_unavailable(index);
        index->nodes = nodes; index->capacity = next;
    }
    *identity = (uint32_t)index->len++;
    index->nodes[*identity] = (CettaPreparedUnaryNodeV1){head, parent, UINT32_MAX};
    index->keys[at] = *identity + 1u; return true;
}
static inline bool cpui_identity(CettaPreparedUnaryIndexV1 *index, const Atom *atom, uint32_t *identity) {
    size_t pending = 0;
    for (;;) {
        if (!atom || !cpui_memo_grow(index)) return false;
        CettaPreparedUnaryMemoV1 *memo = cpui_memo(index, atom);
        if (memo->atom) {
            if (memo->pending) return false;
            *identity = memo->identity; break;
        }
        if (atom->kind == ATOM_SYMBOL) {
            if (!cpui_intern(index, atom->sym_id, UINT32_MAX, identity)) return false;
            *memo = (CettaPreparedUnaryMemoV1){atom, *identity, false}; ++index->memo_len; break;
        }
        if (atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
            !atom->expr.elems[0] || atom->expr.elems[0]->kind != ATOM_SYMBOL) return false;
        if (pending == index->path_capacity) {
            size_t next = index->path_capacity ? index->path_capacity * 2u : 32u;
            if (next < index->path_capacity || next > SIZE_MAX / sizeof(*index->path)) return cpui_unavailable(index);
            void *path = realloc(index->path, next * sizeof(*index->path));
            if (!path) return cpui_unavailable(index);
            index->path = path; index->path_capacity = next;
        }
        *memo = (CettaPreparedUnaryMemoV1){atom, 0u, true}; ++index->memo_len;
        index->path[pending++] = atom; atom = atom->expr.elems[1];
    }
    while (pending) {
        atom = index->path[--pending];
        if (!cpui_intern(index, atom->expr.elems[0]->sym_id, *identity, identity)) return false;
        *cpui_memo(index, atom) = (CettaPreparedUnaryMemoV1){atom, *identity, false};
    }
    return true;
}
static inline bool cpui_bind(CettaPreparedUnaryIndexV1 *index, const Atom *atom, uint32_t row) {
    uint32_t identity;
    if (!cpui_identity(index, atom, &identity) || index->nodes[identity].row != UINT32_MAX) return false;
    index->nodes[identity].row = row; return true;
}
/* The identity of a reference, recording nothing new when it is one unary
 * step above an address already seen: such references are typically built
 * once each, and their structural key is interned already. */
static inline bool cpui_reference(CettaPreparedUnaryIndexV1 *index, const Atom *atom, uint32_t *identity) {
    if (atom && index->memo_capacity && atom->kind == ATOM_EXPR && atom->expr.len == 2u &&
        atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_SYMBOL && atom->expr.elems[1]) {
        CettaPreparedUnaryMemoV1 *own = cpui_memo(index, atom);
        if (own->atom) {
            if (own->pending) return false;
            *identity = own->identity; return true;
        }
        CettaPreparedUnaryMemoV1 *below = cpui_memo(index, atom->expr.elems[1]);
        if (below->atom && !below->pending)
            return cpui_intern(index, atom->expr.elems[0]->sym_id, below->identity, identity);
    }
    return cpui_identity(index, atom, identity);
}
static inline bool cpui_row(CettaPreparedUnaryIndexV1 *index, const Atom *atom, uint32_t *row) {
    uint32_t identity;
    if (!cpui_reference(index, atom, &identity) || index->nodes[identity].row == UINT32_MAX) return false;
    *row = index->nodes[identity].row; return true;
}
#endif
