#include "term_canon.h"
#include <time.h>
#include "space.h"
#include "fixpoint.h"
#include "symbol_index.h"
#include "space_path_index.h"
#include "space_type_evidence.h"
#include "shared_transition.h"
#include "grounded.h"
#include "search_machine.h"
#include "rule_binding.h"
#include "stats.h"
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include "eval.h"
#include "exact_integer.h"
#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static _Thread_local SpaceMatchBackendError g_space_match_backend_error =
    SPACE_MATCH_BACKEND_ERROR_NONE;
static _Thread_local uint64_t
    g_space_match_backend_packet_materialization_limit_override = 0;
static _Thread_local uint64_t
    g_space_match_backend_contextual_query_slot_limit_override = 0;
static _Thread_local CettaCount g_query_results_capacity_limit_override = 0;
static _Atomic uint64_t g_space_next_instance_id = 1u;
static _Atomic uint64_t g_space_next_prefix_epoch = 1u;
_Atomic uint64_t cetta_space_global_mutation_epoch = 0u;

static _Thread_local bool g_declared_type_index_configured = false;
static _Thread_local bool g_declared_type_index_enabled = true;
static _Thread_local bool
    g_stable_occurrence_transport_configured = false;
static _Thread_local bool
    g_stable_occurrence_transport_enabled = true;

/* The native index is the production path.  Its opt-out exists solely so a
 * separate qualification process can compare it with the observationally
 * identical logical-space scan. */
static bool declared_type_index_enabled(void) {
    if (!g_declared_type_index_configured) {
        const char *setting = getenv("CETTA_TYPE_SPACE_INDEX");
        g_declared_type_index_enabled =
            !setting || !(strcmp(setting, "0") == 0 ||
                          strcasecmp(setting, "false") == 0 ||
                          strcasecmp(setting, "off") == 0 ||
                          strcasecmp(setting, "no") == 0);
        g_declared_type_index_configured = true;
    }
    return g_declared_type_index_enabled;
}

/* The reference switch is a qualification seam only.  Both realizations
 * implement the same stable ordered contraction; production chooses the
 * coordinate-transport realization whenever its backend admits it. */
static bool stable_occurrence_transport_enabled(void) {
    if (!g_stable_occurrence_transport_configured) {
        g_stable_occurrence_transport_enabled =
            getenv("CETTA_SPACE_STABLE_OCCURRENCE_TRANSPORT_REFERENCE") ==
            NULL;
        g_stable_occurrence_transport_configured = true;
    }
    return g_stable_occurrence_transport_enabled;
}

#ifndef CETTA_DISC_INT_HASH_REFERENCE
#define CETTA_DISC_INT_HASH_REFERENCE 0
#endif

#define DISC_HASH_THRESHOLD 16u

static uint64_t space_fresh_instance_id(void) {
    uint64_t id = atomic_fetch_add_explicit(
        &g_space_next_instance_id, 1u, memory_order_relaxed);
    if (id == 0u || id == UINT64_MAX) {
        fputs("CeTTa: exhausted process-local Space identities\n", stderr);
        abort();
    }
    return id;
}

static uint64_t space_fresh_prefix_epoch(void) {
    uint64_t epoch = atomic_fetch_add_explicit(
        &g_space_next_prefix_epoch, 1u, memory_order_relaxed);
    if (epoch == 0u || epoch == UINT64_MAX) {
        fputs("CeTTa: exhausted process-local Space prefix epochs\n",
              stderr);
        abort();
    }
    return epoch;
}

static void space_advance_global_mutation_epoch(void) {
    uint64_t prior = atomic_fetch_add_explicit(
        &cetta_space_global_mutation_epoch, 1u, memory_order_relaxed);
    if (prior == UINT64_MAX) {
        fputs("CeTTa: exhausted global Space mutation epoch\n", stderr);
        abort();
    }
}

static uint64_t space_match_backend_atom_id_materialization_limit(void) {
    return (uint64_t)(SIZE_MAX / sizeof(AtomId));
}

const char *space_match_backend_error_name(SpaceMatchBackendError code) {
    switch (code) {
    case SPACE_MATCH_BACKEND_ERROR_NONE:
        return "None";
    case SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE:
        return "NativeSpaceTooLarge";
    case SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE:
        return "PacketTooLarge";
    }
    return "UnknownSpaceMatchBackendError";
}

void space_match_backend_clear_error(void) {
    g_space_match_backend_error = SPACE_MATCH_BACKEND_ERROR_NONE;
}

void space_match_backend_set_error(SpaceMatchBackendError code) {
    g_space_match_backend_error = code;
}

SpaceMatchBackendError space_match_backend_last_error_code(void) {
    return g_space_match_backend_error;
}

const char *space_match_backend_last_error(void) {
    return space_match_backend_error_name(g_space_match_backend_error);
}

uint64_t space_match_backend_native_materialization_limit(void) {
    return space_match_backend_atom_id_materialization_limit();
}

uint64_t space_match_backend_packet_materialization_limit(void) {
    if (g_space_match_backend_packet_materialization_limit_override != 0)
        return g_space_match_backend_packet_materialization_limit_override;
    return space_match_backend_atom_id_materialization_limit();
}

uint64_t space_match_backend_contextual_query_slot_limit(void) {
    if (g_space_match_backend_contextual_query_slot_limit_override != 0)
        return g_space_match_backend_contextual_query_slot_limit_override;
    return UINT16_MAX;
}

void space_match_backend_diag_set_contextual_query_slot_limit_override(
    uint64_t limit) {
    g_space_match_backend_contextual_query_slot_limit_override = limit;
}

void space_match_backend_diag_set_packet_materialization_limit_override(
    uint64_t limit) {
    g_space_match_backend_packet_materialization_limit_override = limit;
}

void space_match_backend_diag_reset(void) {
    g_space_match_backend_packet_materialization_limit_override = 0;
    g_space_match_backend_contextual_query_slot_limit_override = 0;
}

static uint64_t space_match_backend_limit_for_error(SpaceMatchBackendError error) {
    switch (error) {
    case SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE:
        return space_match_backend_native_materialization_limit();
    case SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE:
        return space_match_backend_packet_materialization_limit();
    case SPACE_MATCH_BACKEND_ERROR_NONE:
        return UINT64_MAX;
    }
    return UINT64_MAX;
}

bool space_match_backend_u32_bound_checked(uint64_t value,
                                           SpaceMatchBackendError error,
                                           uint32_t *out_value) {
    uint64_t limit = space_match_backend_limit_for_error(error);
    if (out_value)
        *out_value = 0;
    if (limit > UINT32_MAX)
        limit = UINT32_MAX;
    if (value > limit) {
        space_match_backend_set_error(error);
        return false;
    }
    if (out_value)
        *out_value = (uint32_t)value;
    space_match_backend_clear_error();
    return true;
}

/* ── Discrimination Trie ────────────────────────────────────────────────── */

typedef struct {
    SymbolId key;
    DiscNode *child;
} DiscSymBranch;

typedef DiscSymBranch DiscSymHashEntry;

typedef struct {
    DiscSymHashEntry *entries;
    uint32_t mask;
    uint32_t count;
} DiscSymHashTable;

typedef struct {
    int64_t key;
    DiscNode *child;
} DiscIntBranch;

typedef struct {
    DiscIntBranch *entries;
    uint32_t mask;
    uint32_t count;
} DiscIntHashTable;

/* The branches of one scalar kind.  An integer is keyed by its value; a
 * string by its cached structural hash, so different strings with equal
 * hashes share a branch, which the matcher tells apart. */
typedef struct {
    DiscIntBranch *list;
    uint32_t count;
    uint32_t cap;
    DiscIntHashTable ht;
    bool hashed;
} DiscScalarBranches;

typedef struct {
    CettaExprLen arity;
    DiscNode *child;
} DiscExprBranch;

typedef struct {
    DiscSymBranch *sym;
    uint32_t nsym;
    uint32_t csym;
    DiscSymHashTable sym_ht;
    bool sym_hashed;
    DiscNode *var_child;
    DiscExprBranch *expr;
    uint32_t nexpr;
    uint32_t cexpr;
    DiscScalarBranches ints;
    DiscScalarBranches strs;
} DiscBranchSet;

typedef enum {
    DISC_EDGE_EMPTY = 0,
    DISC_EDGE_SYMBOL,
    DISC_EDGE_VARIABLE,
    DISC_EDGE_EXPRESSION,
    DISC_EDGE_INTEGER,
    DISC_EDGE_STRING,
    DISC_EDGE_MANY,
} DiscEdgeKind;

typedef union {
    SymbolId symbol;
    CettaExprLen arity;
    /* An integer's value, or a string's content hash. */
    int64_t integer;
} DiscEdgeKey;

/* A trie node.  The coordinates spell a term in prefix order, each
 * expression with its arity, so no whole term is a prefix of another: a
 * position where a term ends holds the rows stored there and never an
 * outgoing coordinate, and every other position has outgoing coordinates
 * and holds no rows.  A leaf position is made without room for an edge,
 * half an inner node's size; `edge` is read only where `edge_kind` names
 * one. */
struct DiscNode {
    /* A DiscEdgeKind; DISC_EDGE_EMPTY at a leaf. */
    uint8_t edge_kind;
    /* 0: no row was ever stored here; 1: the inline slot, even after its
     * row is removed; k > 1: an array of 2^k rows. */
    uint8_t leaf_capacity;
    /* Whether the node was made with room for an edge. */
    bool inner;
    uint32_t leaf_count;
    union {
        CettaIndex one;
        CettaIndex *many;
        /* A recycled node's successor on its free list. */
        DiscNode *next_free;
    } leaf;
    /* An inner node's outgoing coordinates.  Most inner positions have one,
     * kept inline; a second distinct coordinate makes the branch set. */
    union {
        struct {
            DiscEdgeKey key;
            DiscNode *child;
        } one;
        DiscBranchSet *many;
    } edge;
};

/* A leaf node is the part of a node before its edge. */
#define DISC_LEAF_NODE_BYTES offsetof(DiscNode, edge)

_Static_assert(DISC_LEAF_NODE_BYTES == 16u && sizeof(DiscNode) == 32u,
               "trie nodes keep their sizes");

/* The rows a node's leaf store holds before it grows. */
static inline CettaIndex disc_leaf_capacity(const DiscNode *node) {
    return node->leaf_capacity > 1u
        ? (CettaIndex)1u << node->leaf_capacity
        : (CettaIndex)node->leaf_capacity;
}

/* A trie's storage, owned by its root.  Nodes, branch sets and arrays of
 * up to DISC_POOL_SMALL bytes are carved from chunks; a larger array is
 * allocated alone, behind a header holding its place in the pool's list of
 * such blocks, so it grows and is released in place.  A trie grows and is
 * freed whole, never a node at a time: freeing it releases its chunks and
 * blocks without visiting a node.  A small array that grows leaves its old
 * space in its chunk, less in all than the array's final size. */
enum {
    DISC_POOL_SMALL = 256u,
    DISC_POOL_CHUNK = 64u * 1024u,
};

typedef struct DiscChunk {
    struct DiscChunk *next;
    size_t used;
    size_t cap;
    size_t reserved;
    unsigned char data[];
} DiscChunk;

typedef struct {
    size_t index;
    size_t reserved;
} DiscBlock;

_Static_assert(offsetof(DiscChunk, data) % 16u == 0u,
               "trie chunks keep 16-byte alignment");
_Static_assert(sizeof(DiscBlock) % 16u == 0u,
               "trie blocks keep 16-byte alignment");

typedef struct {
    DiscNode **nodes;
    size_t len;
    size_t capacity;
} DiscNodeVector;

typedef struct {
    DiscChunk *chunks;
    DiscBlock **blocks;
    size_t block_len;
    size_t block_cap;
    /* Materialized only by coordinate transport.  Each populated node is
     * registered once, and stays registered when its leaf becomes empty. */
    DiscNodeVector coordinate_leaves;
    bool coordinate_leaves_ready;
    bool approximate;
    /* Nodes a removal unlinked, inner and leaf apart, taken before new
     * storage is carved. */
    DiscNode *free_inner;
    DiscNode *free_leaves;
} DiscPool;

typedef struct {
    DiscPool pool;
    DiscNode node;
} DiscRoot;

static DiscPool *disc_root_pool(DiscNode *root) {
    return &((DiscRoot *)(void *)((char *)root - offsetof(DiscRoot, node)))
                ->pool;
}

/* Storage of `bytes` allocated alone, behind its block header. */
static void *disc_pool_alloc_alone(DiscPool *pool, size_t bytes) {
    if (pool->block_len == pool->block_cap) {
        size_t cap = pool->block_cap ? pool->block_cap * 2u : 16u;
        pool->blocks = cetta_realloc(pool->blocks,
                                     sizeof(*pool->blocks) * cap);
        pool->block_cap = cap;
    }
    DiscBlock *block = cetta_malloc(sizeof(*block) + bytes);
    block->index = pool->block_len;
    pool->blocks[pool->block_len++] = block;
    return block + 1;
}

static void *disc_pool_alloc(DiscPool *pool, size_t bytes) {
    if (bytes <= DISC_POOL_SMALL) {
        bytes = (bytes + 15u) & ~(size_t)15u;
        DiscChunk *chunk = pool->chunks;
        if (!chunk || chunk->cap - chunk->used < bytes) {
            chunk = cetta_malloc(sizeof(*chunk) + DISC_POOL_CHUNK);
            chunk->next = pool->chunks;
            chunk->used = 0u;
            chunk->cap = DISC_POOL_CHUNK;
            pool->chunks = chunk;
        }
        void *out = chunk->data + chunk->used;
        chunk->used += bytes;
        return out;
    }
    return disc_pool_alloc_alone(pool, bytes);
}

/* An array of `old_bytes` grown to `new_bytes`, its contents kept. */
static void *disc_pool_grow(DiscPool *pool, void *old, size_t old_bytes,
                            size_t new_bytes) {
    if (old && old_bytes > DISC_POOL_SMALL) {
        DiscBlock *block = (DiscBlock *)old - 1;
        size_t index = block->index;
        block = cetta_realloc(block, sizeof(*block) + new_bytes);
        pool->blocks[index] = block;
        return block + 1;
    }
    void *next = disc_pool_alloc(pool, new_bytes);
    if (old && old_bytes > 0u)
        memcpy(next, old, old_bytes);
    return next;
}

/* An array of `bytes` the trie no longer uses. */
static void disc_pool_release(DiscPool *pool, void *ptr, size_t bytes) {
    if (!ptr || bytes <= DISC_POOL_SMALL)
        return;
    DiscBlock *block = (DiscBlock *)ptr - 1;
    DiscBlock *last = pool->blocks[--pool->block_len];
    pool->blocks[block->index] = last;
    last->index = block->index;
    free(block);
}

static DiscNode *disc_pool_node(DiscPool *pool, bool inner) {
    DiscNode **free_list = inner ? &pool->free_inner : &pool->free_leaves;
    size_t bytes = inner ? sizeof(DiscNode) : DISC_LEAF_NODE_BYTES;
    DiscNode *node = *free_list;
    if (node)
        *free_list = node->leaf.next_free;
    else
#if defined(__SANITIZE_ADDRESS__)
        /* Each node its own allocation, so a read past a leaf node's end
         * is reported. */
        node = disc_pool_alloc_alone(pool, bytes);
#else
        node = disc_pool_alloc(pool, bytes);
#endif
    memset(node, 0, bytes);
    node->inner = inner;
    return node;
}

/* Keep a node a removal unlinked for the next node the trie makes.  One
 * with a branch set keeps its storage.  The registry of populated nodes
 * is dropped, to be rebuilt from the trie when next read, so no node is in
 * it twice or under a path it has left. */
static void disc_pool_recycle(DiscPool *pool, DiscNode *node) {
    if (node->edge_kind == DISC_EDGE_MANY)
        return;
    if (node->leaf_capacity > 1u)
        disc_pool_release(pool, node->leaf.many,
                          sizeof(CettaIndex) *
                              (size_t)disc_leaf_capacity(node));
    pool->coordinate_leaves.len = 0u;
    pool->coordinate_leaves_ready = false;
    DiscNode **free_list = node->inner ? &pool->free_inner
                                       : &pool->free_leaves;
    node->edge_kind = DISC_EDGE_EMPTY;
    node->leaf.next_free = *free_list;
    *free_list = node;
}

static bool disc_edge_key_eq(
        DiscEdgeKind kind, DiscEdgeKey left, DiscEdgeKey right) {
    switch (kind) {
    case DISC_EDGE_SYMBOL:
        return left.symbol == right.symbol;
    case DISC_EDGE_VARIABLE:
        return true;
    case DISC_EDGE_EXPRESSION:
        return left.arity == right.arity;
    case DISC_EDGE_INTEGER:
    case DISC_EDGE_STRING:
        return left.integer == right.integer;
    case DISC_EDGE_EMPTY:
    case DISC_EDGE_MANY:
        return false;
    }
    return false;
}

static DiscNode *disc_singleton_find(
        const DiscNode *node, DiscEdgeKind kind, DiscEdgeKey key) {
    if (!node || node->edge_kind != kind ||
        !disc_edge_key_eq(kind, node->edge.one.key, key)) {
        return NULL;
    }
    return node->edge.one.child;
}

static DiscNode *disc_singleton_insert(
        DiscPool *pool, DiscNode *node, DiscEdgeKind kind, DiscEdgeKey key,
        bool inner) {
    if (!node->inner) {
        fputs("CeTTa: discrimination trie extended past a whole term\n",
              stderr);
        abort();
    }
    DiscNode *child = disc_pool_node(pool, inner);
    node->edge_kind = kind;
    node->edge.one.key = key;
    node->edge.one.child = child;
    return child;
}

/* A trie's root, with the pool that holds the trie. */
DiscNode *disc_node_new(void) {
    DiscRoot *root = cetta_malloc(sizeof(*root));
    memset(root, 0, sizeof(*root));
    root->node.inner = true;
    return &root->node;
}

/* Free a trie from its root: its pool's chunks and blocks, then the root. */
void disc_node_free(DiscNode *n) {
    if (!n)
        return;
    DiscPool *pool = disc_root_pool(n);
    for (size_t i = 0u; i < pool->block_len; i++)
        free(pool->blocks[i]);
    free(pool->blocks);
    free(pool->coordinate_leaves.nodes);
    DiscChunk *chunk = pool->chunks;
    while (chunk) {
        DiscChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    free((char *)n - offsetof(DiscRoot, node));
}

static CettaIndex disc_leaf_at(const DiscNode *node, CettaIndex index) {
    return node->leaf_capacity > 1u
        ? node->leaf.many[index]
        : node->leaf.one;
}

static void disc_leaf_set(
        DiscNode *node, CettaIndex index, CettaIndex value) {
    if (node->leaf_capacity > 1u)
        node->leaf.many[index] = value;
    else
        node->leaf.one = value;
}

static void disc_node_vector_push(DiscNodeVector *vector, DiscNode *node) {
    if (!node)
        return;
    if (vector->len == vector->capacity) {
        size_t limit = SIZE_MAX / sizeof(*vector->nodes);
        if (vector->capacity == limit) {
            fputs("CeTTa: discrimination leaf registry capacity exhausted\n",
                  stderr);
            abort();
        }
        size_t capacity = vector->capacity ?
            (vector->capacity > limit / 2u ? limit : vector->capacity * 2u) : 16u;
        vector->nodes = cetta_realloc(vector->nodes,
                                     capacity * sizeof(*vector->nodes));
        vector->capacity = capacity;
    }
    vector->nodes[vector->len++] = node;
}

/* Build once with an explicit worklist.  Later transports touch only leaf
 * stores; neither term depth nor unused hash slots enter their visit count.
 * New leaves join this registry in disc_add_leaf.  The owning pool releases
 * it together with the trie, including detached snapshot tries. */
static void disc_prepare_coordinate_leaves(DiscPool *pool, DiscNode *root) {
    DiscNodeVector pending = {0};
    disc_node_vector_push(&pending, root);
    while (pending.len) {
        DiscNode *node = pending.nodes[--pending.len];
        if (node->leaf_capacity != 0u)
            disc_node_vector_push(&pool->coordinate_leaves, node);
        if (node->edge_kind != DISC_EDGE_MANY) {
            if (node->edge_kind != DISC_EDGE_EMPTY)
                disc_node_vector_push(&pending, node->edge.one.child);
            continue;
        }
        DiscBranchSet *branches = node->edge.many;
        if (branches->sym_hashed) {
            uint32_t cap = branches->sym_ht.mask + 1u;
            for (uint32_t i = 0u; i < cap; i++) {
                if (branches->sym_ht.entries[i].key != SYMBOL_ID_NONE) {
                    disc_node_vector_push(&pending,
                        branches->sym_ht.entries[i].child);
                }
            }
        } else {
            for (uint32_t i = 0u; i < branches->nsym; i++) {
                disc_node_vector_push(&pending, branches->sym[i].child);
            }
        }
        disc_node_vector_push(&pending, branches->var_child);
        for (uint32_t i = 0u; i < branches->nexpr; i++) {
            disc_node_vector_push(&pending, branches->expr[i].child);
        }
        const DiscScalarBranches *scalars[] = {&branches->ints, &branches->strs};
        for (size_t kind = 0u; kind < 2u; kind++) {
            const DiscScalarBranches *each = scalars[kind];
            uint32_t cap = each->hashed ? each->ht.mask + 1u : each->count;
            for (uint32_t i = 0u; i < cap; i++) {
                /* A hashed table's empty slots hold no child. */
                DiscNode *child = each->hashed
                    ? each->ht.entries[i].child : each->list[i].child;
                if (child)
                    disc_node_vector_push(&pending, child);
            }
        }
    }
    free(pending.nodes);
    pool->coordinate_leaves_ready = true;
}

CettaCount disc_transport_stable_coordinates(
        DiscNode *root, const CettaIndex *source_to_target,
        CettaCount source_len) {
    if (!root || (!source_to_target && source_len != 0u))
        return 0u;
    DiscPool *pool = disc_root_pool(root);
    if (!pool->coordinate_leaves_ready)
        disc_prepare_coordinate_leaves(pool, root);
    CettaCount removed = 0u;
    for (size_t i = 0u; i < pool->coordinate_leaves.len; i++) {
        DiscNode *node = pool->coordinate_leaves.nodes[i];
        CettaIndex write = 0u;
        for (CettaIndex read = 0u; read < node->leaf_count; read++) {
            CettaIndex source = disc_leaf_at(node, read);
            if (source >= source_len) {
                fputs("CeTTa: clean discrimination index contains an invalid "
                      "occurrence coordinate\n", stderr);
                abort();
            }
            CettaIndex target = source_to_target[source];
            if (target == SPACE_OCCURRENCE_COORDINATE_REMOVED) {
                removed++;
                continue;
            }
            disc_leaf_set(node, write++, target);
        }
        node->leaf_count = write;
    }
    return removed;
}

static void disc_add_leaf(DiscPool *pool, DiscNode *n, CettaIndex idx) {
    if (n->leaf_capacity == 0u) {
        if (pool->coordinate_leaves_ready)
            disc_node_vector_push(&pool->coordinate_leaves, n);
        n->leaf_capacity = 1u;
    }
    if (n->leaf_count == 0u && n->leaf_capacity == 1u) {
        n->leaf.one = idx;
        n->leaf_count = 1u;
        return;
    }
    if (n->leaf_count == UINT32_MAX) {
        fputs("CeTTa: discrimination leaf holds too many rows\n", stderr);
        abort();
    }
    if (n->leaf_capacity == 1u) {
        CettaIndex first = n->leaf.one;
        n->leaf_capacity = 2u;
        n->leaf.many = disc_pool_alloc(
            pool, sizeof(CettaIndex) * (size_t)disc_leaf_capacity(n));
        n->leaf.many[0] = first;
    } else if (n->leaf_count >= disc_leaf_capacity(n)) {
        size_t old_bytes = sizeof(CettaIndex) * (size_t)disc_leaf_capacity(n);
        n->leaf_capacity++;
        n->leaf.many = disc_pool_grow(
            pool, n->leaf.many, old_bytes,
            sizeof(CettaIndex) * (size_t)disc_leaf_capacity(n));
    }
    n->leaf.many[n->leaf_count++] = idx;
}

static inline uint32_t disc_sym_hash(SymbolId key) {
    return (uint32_t)((uint64_t)key * 2654435761u);
}

static void disc_sym_ht_init(DiscPool *pool, DiscSymHashTable *ht,
                             uint32_t min_cap) {
    uint32_t cap = 32;
    while (cap < min_cap * 2)
        cap *= 2;
    ht->entries = disc_pool_alloc(pool, sizeof(DiscSymHashEntry) * cap);
    ht->mask = cap - 1;
    ht->count = 0;
    for (uint32_t i = 0; i < cap; i++)
        ht->entries[i].key = SYMBOL_ID_NONE;
}

static DiscNode *disc_sym_ht_get(const DiscSymHashTable *ht, SymbolId key) {
    uint32_t idx = disc_sym_hash(key) & ht->mask;
    for (;;) {
        if (ht->entries[idx].key == key)
            return ht->entries[idx].child;
        if (ht->entries[idx].key == SYMBOL_ID_NONE)
            return NULL;
        idx = (idx + 1) & ht->mask;
    }
}

static void disc_sym_ht_put(DiscPool *pool, DiscSymHashTable *ht,
                            SymbolId key, DiscNode *child) {
    if (ht->count * 10 > (ht->mask + 1) * 7) {
        uint32_t old_cap = ht->mask + 1;
        DiscSymHashEntry *old = ht->entries;
        uint32_t new_cap = old_cap * 2;
        ht->entries = disc_pool_alloc(
            pool, sizeof(DiscSymHashEntry) * new_cap);
        ht->mask = new_cap - 1;
        ht->count = 0;
        for (uint32_t i = 0; i < new_cap; i++)
            ht->entries[i].key = SYMBOL_ID_NONE;
        for (uint32_t i = 0; i < old_cap; i++) {
            if (old[i].key != SYMBOL_ID_NONE)
                disc_sym_ht_put(pool, ht, old[i].key, old[i].child);
        }
        disc_pool_release(pool, old, sizeof(DiscSymHashEntry) * old_cap);
    }
    uint32_t idx = disc_sym_hash(key) & ht->mask;
    while (ht->entries[idx].key != SYMBOL_ID_NONE)
        idx = (idx + 1) & ht->mask;
    ht->entries[idx].key = key;
    ht->entries[idx].child = child;
    ht->count++;
}

static DiscBranchSet *disc_promote_singleton(DiscPool *pool,
                                             DiscNode *node) {
    DiscEdgeKind kind = node->edge_kind;
    DiscEdgeKey key = node->edge.one.key;
    DiscNode *child = node->edge.one.child;
    DiscBranchSet *branches = disc_pool_alloc(pool, sizeof(*branches));
    memset(branches, 0, sizeof(*branches));
    switch (kind) {
    case DISC_EDGE_SYMBOL:
        branches->csym = 4u;
        branches->sym = disc_pool_alloc(
            pool, sizeof(*branches->sym) * branches->csym);
        branches->sym[0] = (DiscSymBranch){
            .key = key.symbol,
            .child = child,
        };
        branches->nsym = 1u;
        break;
    case DISC_EDGE_VARIABLE:
        branches->var_child = child;
        break;
    case DISC_EDGE_EXPRESSION:
        branches->cexpr = 4u;
        branches->expr = disc_pool_alloc(
            pool, sizeof(*branches->expr) * branches->cexpr);
        branches->expr[0] = (DiscExprBranch){
            .arity = key.arity,
            .child = child,
        };
        branches->nexpr = 1u;
        break;
    case DISC_EDGE_INTEGER:
    case DISC_EDGE_STRING: {
        DiscScalarBranches *scalars = kind == DISC_EDGE_STRING
            ? &branches->strs : &branches->ints;
        scalars->cap = 4u;
        scalars->list = disc_pool_alloc(
            pool, sizeof(*scalars->list) * scalars->cap);
        scalars->list[0] = (DiscIntBranch){
            .key = key.integer,
            .child = child,
        };
        scalars->count = 1u;
        break;
    }
    case DISC_EDGE_EMPTY:
    case DISC_EDGE_MANY:
        break;
    }
    node->edge_kind = DISC_EDGE_MANY;
    node->edge.many = branches;
    return branches;
}

static void disc_sym_promote(DiscPool *pool, DiscBranchSet *branches) {
    DiscSymBranch *old_sym = branches->sym;
    uint32_t count = branches->nsym;
    disc_sym_ht_init(pool, &branches->sym_ht, count + 16u);
    for (uint32_t i = 0; i < count; i++)
        disc_sym_ht_put(
            pool, &branches->sym_ht, old_sym[i].key, old_sym[i].child);
    disc_pool_release(pool, old_sym,
                      sizeof(*old_sym) * (size_t)branches->csym);
    branches->sym = NULL;
    branches->csym = 0u;
    branches->sym_hashed = true;
}

static DiscNode *disc_get_sym(DiscPool *pool, DiscNode *n, SymbolId key,
                              bool inner) {
    DiscEdgeKey edge_key = {.symbol = key};
    if (n->edge_kind == DISC_EDGE_EMPTY)
        return disc_singleton_insert(pool, n, DISC_EDGE_SYMBOL, edge_key,
                                     inner);
    DiscNode *single = disc_singleton_find(
        n, DISC_EDGE_SYMBOL, edge_key);
    if (single)
        return single;
    DiscBranchSet *branches = n->edge_kind == DISC_EDGE_MANY
        ? n->edge.many
        : disc_promote_singleton(pool, n);
    if (branches->sym_hashed) {
        DiscNode *existing = disc_sym_ht_get(&branches->sym_ht, key);
        if (existing) return existing;
        DiscNode *child = disc_pool_node(pool, inner);
        disc_sym_ht_put(pool, &branches->sym_ht, key, child);
        branches->nsym++;
        return child;
    }
    for (uint32_t i = 0; i < branches->nsym; i++)
        if (branches->sym[i].key == key) return branches->sym[i].child;
    if (branches->nsym >= branches->csym) {
        size_t old_bytes = sizeof(branches->sym[0]) * branches->csym;
        branches->csym = branches->csym ? branches->csym * 2u : 4u;
        branches->sym = disc_pool_grow(
            pool, branches->sym, old_bytes,
            sizeof(branches->sym[0]) * branches->csym);
    }
    DiscNode *child = disc_pool_node(pool, inner);
    branches->sym[branches->nsym].key = key;
    branches->sym[branches->nsym].child = child;
    branches->nsym++;
    if (branches->nsym > DISC_HASH_THRESHOLD)
        disc_sym_promote(pool, branches);
    return child;
}

static DiscNode *disc_get_var(DiscPool *pool, DiscNode *n, bool inner) {
    DiscEdgeKey edge_key = {0};
    if (n->edge_kind == DISC_EDGE_EMPTY)
        return disc_singleton_insert(pool, n, DISC_EDGE_VARIABLE, edge_key,
                                     inner);
    DiscNode *single = disc_singleton_find(
        n, DISC_EDGE_VARIABLE, edge_key);
    if (single)
        return single;
    DiscBranchSet *branches = n->edge_kind == DISC_EDGE_MANY
        ? n->edge.many
        : disc_promote_singleton(pool, n);
    if (!branches->var_child)
        branches->var_child = disc_pool_node(pool, inner);
    return branches->var_child;
}

static DiscNode *disc_get_expr(DiscPool *pool, DiscNode *n,
                               CettaExprLen arity, bool inner) {
    DiscEdgeKey edge_key = {.arity = arity};
    if (n->edge_kind == DISC_EDGE_EMPTY)
        return disc_singleton_insert(pool, n, DISC_EDGE_EXPRESSION, edge_key,
                                     inner);
    DiscNode *single = disc_singleton_find(
        n, DISC_EDGE_EXPRESSION, edge_key);
    if (single)
        return single;
    DiscBranchSet *branches = n->edge_kind == DISC_EDGE_MANY
        ? n->edge.many
        : disc_promote_singleton(pool, n);
    for (uint32_t i = 0; i < branches->nexpr; i++)
        if (branches->expr[i].arity == arity)
            return branches->expr[i].child;
    if (branches->nexpr >= branches->cexpr) {
        size_t old_bytes = sizeof(branches->expr[0]) * branches->cexpr;
        branches->cexpr = branches->cexpr ? branches->cexpr * 2u : 4u;
        branches->expr = disc_pool_grow(
            pool, branches->expr, old_bytes,
            sizeof(branches->expr[0]) * branches->cexpr);
    }
    DiscNode *child = disc_pool_node(pool, inner);
    branches->expr[branches->nexpr].arity = arity;
    branches->expr[branches->nexpr].child = child;
    branches->nexpr++;
    return child;
}

static inline uint32_t disc_int_hash(int64_t key) {
    uint64_t mixed = (uint64_t)key;
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31u;
    return (uint32_t)(mixed ^ (mixed >> 32u));
}

static void disc_int_ht_init(DiscPool *pool, DiscIntHashTable *ht,
                             uint32_t min_cap) {
    uint32_t cap = 32u;
    while (cap < min_cap * 2u)
        cap *= 2u;
    ht->entries = disc_pool_alloc(pool, sizeof(*ht->entries) * (size_t)cap);
    memset(ht->entries, 0, sizeof(*ht->entries) * (size_t)cap);
    ht->mask = cap - 1u;
    ht->count = 0u;
}

static DiscNode *disc_int_ht_get(
        const DiscIntHashTable *ht, int64_t key) {
    uint32_t index = disc_int_hash(key) & ht->mask;
    for (;;) {
        const DiscIntBranch *entry = &ht->entries[index];
        if (!entry->child)
            return NULL;
        if (entry->key == key)
            return entry->child;
        index = (index + 1u) & ht->mask;
    }
}

static void disc_int_ht_put(
        DiscPool *pool, DiscIntHashTable *ht, int64_t key, DiscNode *child) {
    if (ht->count * 10u > (ht->mask + 1u) * 7u) {
        uint32_t old_cap = ht->mask + 1u;
        DiscIntBranch *old = ht->entries;
        uint32_t new_cap = old_cap * 2u;
        ht->entries = disc_pool_alloc(
            pool, sizeof(*ht->entries) * (size_t)new_cap);
        memset(ht->entries, 0,
               sizeof(*ht->entries) * (size_t)new_cap);
        ht->mask = new_cap - 1u;
        ht->count = 0u;
        for (uint32_t i = 0u; i < old_cap; i++) {
            if (old[i].child)
                disc_int_ht_put(pool, ht, old[i].key, old[i].child);
        }
        disc_pool_release(pool, old, sizeof(*old) * (size_t)old_cap);
    }
    uint32_t index = disc_int_hash(key) & ht->mask;
    while (ht->entries[index].child)
        index = (index + 1u) & ht->mask;
    ht->entries[index] = (DiscIntBranch){
        .key = key,
        .child = child,
    };
    ht->count++;
}

static void disc_scalar_promote(DiscPool *pool, DiscScalarBranches *scalars) {
    DiscIntBranch *old = scalars->list;
    uint32_t count = scalars->count;
    disc_int_ht_init(pool, &scalars->ht, count + 16u);
    for (uint32_t i = 0u; i < count; i++)
        disc_int_ht_put(pool, &scalars->ht, old[i].key, old[i].child);
    disc_pool_release(pool, old, sizeof(*old) * (size_t)scalars->cap);
    scalars->list = NULL;
    scalars->cap = 0u;
    scalars->hashed = true;
}

/* The child under scalar key `key` of edge kind `kind`, made if absent. */
static DiscNode *disc_get_scalar(DiscPool *pool, DiscNode *n,
                                 DiscEdgeKind kind, int64_t key, bool inner) {
    DiscEdgeKey edge_key = {.integer = key};
    if (n->edge_kind == DISC_EDGE_EMPTY)
        return disc_singleton_insert(pool, n, kind, edge_key, inner);
    DiscNode *single = disc_singleton_find(n, kind, edge_key);
    if (single)
        return single;
    DiscBranchSet *branches = n->edge_kind == DISC_EDGE_MANY
        ? n->edge.many
        : disc_promote_singleton(pool, n);
    DiscScalarBranches *scalars = kind == DISC_EDGE_STRING
        ? &branches->strs : &branches->ints;
    if (scalars->hashed) {
        DiscNode *existing = disc_int_ht_get(&scalars->ht, key);
        if (existing)
            return existing;
        DiscNode *child = disc_pool_node(pool, inner);
        disc_int_ht_put(pool, &scalars->ht, key, child);
        scalars->count++;
        return child;
    }
    for (uint32_t i = 0; i < scalars->count; i++)
        if (scalars->list[i].key == key)
            return scalars->list[i].child;
    if (scalars->count >= scalars->cap) {
        size_t old_bytes = sizeof(scalars->list[0]) * scalars->cap;
        scalars->cap = scalars->cap ? scalars->cap * 2u : 4u;
        scalars->list = disc_pool_grow(
            pool, scalars->list, old_bytes,
            sizeof(scalars->list[0]) * scalars->cap);
    }
    DiscNode *child = disc_pool_node(pool, inner);
    scalars->list[scalars->count].key = key;
    scalars->list[scalars->count].child = child;
    scalars->count++;
    if (scalars->count > DISC_HASH_THRESHOLD &&
        !CETTA_DISC_INT_HASH_REFERENCE) {
        disc_scalar_promote(pool, scalars);
    }
    return child;
}

static DiscNode *disc_get_int(DiscPool *pool, DiscNode *n, int64_t val,
                              bool inner) {
    return disc_get_scalar(pool, n, DISC_EDGE_INTEGER, val, inner);
}

/* A string's key: its leaf's structural hash, computed once over its bytes
 * (embedded NUL included) and cached, equal for the atom and its universe
 * record (cetta-ours 23726ac5).  Rebuilding an index reads it instead of
 * hashing every stored string again; collisions only widen candidates,
 * which the matcher tells apart. */
static int64_t disc_string_key(Atom *atom) {
    return (int64_t)atom_hash(atom);
}

static int64_t disc_string_key_id(const TermUniverse *universe,
                                  AtomId atom_id) {
    return (int64_t)tu_hash32(universe, atom_id);
}

/* Limit expansion of expression occurrences, not merely depth: a wide DAG
 * can unfold exponentially even at small depth. Cut subtrees are complete
 * wildcard coordinates. Ordinary residual matching remains authoritative;
 * exact count/projection shortcuts decline approximate pools. */
static DiscNode *disc_insert_atom(DiscPool *pool, DiscNode *node, Atom *a,
                                  unsigned depth, CettaIndexExpansionBudget *budget,
                                  bool last) {
    /* The coordinate made here ends the whole term when it is the last of
     * a last subterm: then its node is a leaf. */
    bool inner = !last;
    if (a->kind == ATOM_EXPR) {
        if (!cetta_index_expand_expression(budget, depth, a->expr.len)) {
            pool->approximate = true;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_DISC_TRUNCATED_SUBTREE);
            return disc_get_var(pool, node, inner);
        }
    }
    switch (a->kind) {
    case ATOM_SYMBOL: return disc_get_sym(pool, node, a->sym_id, inner);
    case ATOM_VAR:    return disc_get_var(pool, node, inner);
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT)
            return disc_get_int(pool, node, a->ground.ival, inner);
        if (a->ground.gkind == GV_STRING)
            return disc_get_scalar(
                pool, node, DISC_EDGE_STRING,
                disc_string_key(a), inner);
        /* Other grounded values sit on the variable branch. */
        return disc_get_var(pool, node, inner);
    case ATOM_EXPR: {
        /* A list pattern meets lists of every length at least its prefix's,
         * so it is keyed as a variable. */
        if (atom_is_list_rest(a))
            return disc_get_var(pool, node, inner);
        DiscNode *cur = disc_get_expr(pool, node, a->expr.len,
                                      inner || a->expr.len != 0u);
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            cur = disc_insert_atom(pool, cur, a->expr.elems[i], depth + 1u,
                                   budget, last && i + 1u == a->expr.len);
        return cur;
    }
    }
    return node;
}

static bool disc_insert_atom_id(DiscPool *pool, DiscNode *node,
                                const TermUniverse *universe,
                                AtomId atom_id, DiscNode **out_leaf, unsigned depth,
                                CettaIndexExpansionBudget *budget, bool last) {
    if (!node || !universe || atom_id == CETTA_ATOM_ID_NONE ||
        !tu_hdr(universe, atom_id) || !out_leaf) {
        return false;
    }
    bool inner = !last;

    if (tu_kind(universe, atom_id) == ATOM_EXPR) {
        if (!cetta_index_expand_expression(budget, depth, tu_arity(universe, atom_id))) {
            pool->approximate = true;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_DISC_TRUNCATED_SUBTREE);
            *out_leaf = disc_get_var(pool, node, inner);
            return true;
        }
    }

    switch (tu_kind(universe, atom_id)) {
    case ATOM_SYMBOL:
        *out_leaf = disc_get_sym(pool, node, tu_sym(universe, atom_id),
                                inner);
        return true;
    case ATOM_VAR:
        *out_leaf = disc_get_var(pool, node, inner);
        return true;
    case ATOM_GROUNDED:
        if (tu_ground_kind(universe, atom_id) == GV_INT) {
            *out_leaf = disc_get_int(pool, node, tu_int(universe, atom_id),
                                    inner);
        } else if (tu_ground_kind(universe, atom_id) == GV_STRING) {
            *out_leaf = disc_get_scalar(
                pool, node, DISC_EDGE_STRING,
                disc_string_key_id(universe, atom_id), inner);
        } else {
            *out_leaf = disc_get_var(pool, node, inner);
        }
        return true;
    case ATOM_EXPR: {
        if (tu_arity(universe, atom_id) > 0u &&
            tu_internal_tag(universe, tu_child(universe, atom_id, 0u)) ==
                CETTA_INTERNAL_TAG_LIST_REST) {
            *out_leaf = disc_get_var(pool, node, inner);
            return true;
        }
        CettaExprLen arity = tu_arity(universe, atom_id);
        DiscNode *cur = disc_get_expr(pool, node, arity,
                                      inner || arity != 0u);
        for (CettaExprIndex i = 0; i < arity; i++) {
            AtomId child_id = tu_child(universe, atom_id, i);
            if (!disc_insert_atom_id(pool, cur, universe, child_id, &cur,
                                    depth + 1u, budget,
                                    last && i + 1u == arity))
                return false;
        }
        *out_leaf = cur;
        return true;
    }
    }
    return false;
}

void disc_insert(DiscNode *root, Atom *lhs, CettaIndex eq_idx) {
    DiscPool *pool = disc_root_pool(root);
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    DiscNode *leaf = disc_insert_atom(pool, root, lhs, 0u, &budget, true);
    disc_add_leaf(pool, leaf, eq_idx);
}

bool disc_insert_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, CettaIndex eq_idx) {
    DiscPool *pool = disc_root_pool(root);
    DiscNode *leaf = NULL;
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    if (!disc_insert_atom_id(pool, root, universe, atom_id, &leaf, 0u,
                             &budget, true))
        return false;
    disc_add_leaf(pool, leaf, eq_idx);
    return true;
}


static const DiscBranchSet *disc_many_branches(const DiscNode *node) {
    return node && node->edge_kind == DISC_EDGE_MANY
        ? node->edge.many
        : NULL;
}

static DiscNode *disc_find_symbol_branch(
        const DiscNode *node, SymbolId key) {
    DiscEdgeKey edge_key = {.symbol = key};
    DiscNode *single = disc_singleton_find(
        node, DISC_EDGE_SYMBOL, edge_key);
    if (single)
        return single;
    const DiscBranchSet *branches = disc_many_branches(node);
    if (!branches)
        return NULL;
    if (branches->sym_hashed)
        return disc_sym_ht_get(&branches->sym_ht, key);
    for (uint32_t i = 0u; i < branches->nsym; i++) {
        if (branches->sym[i].key == key)
            return branches->sym[i].child;
    }
    return NULL;
}

static DiscNode *disc_find_scalar_branch(
        const DiscNode *node, DiscEdgeKind kind, int64_t key) {
    DiscEdgeKey edge_key = {.integer = key};
    DiscNode *single = disc_singleton_find(node, kind, edge_key);
    if (single)
        return single;
    const DiscBranchSet *branches = disc_many_branches(node);
    if (!branches)
        return NULL;
    const DiscScalarBranches *scalars = kind == DISC_EDGE_STRING
        ? &branches->strs : &branches->ints;
    if (scalars->hashed)
        return disc_int_ht_get(&scalars->ht, key);
    for (uint32_t i = 0u; i < scalars->count; i++) {
        if (scalars->list[i].key == key)
            return scalars->list[i].child;
    }
    return NULL;
}

static DiscNode *disc_find_integer_branch(
        const DiscNode *node, int64_t value) {
    return disc_find_scalar_branch(node, DISC_EDGE_INTEGER, value);
}

static DiscNode *disc_find_expression_branch(
        const DiscNode *node, CettaExprLen arity) {
    DiscEdgeKey edge_key = {.arity = arity};
    DiscNode *single = disc_singleton_find(
        node, DISC_EDGE_EXPRESSION, edge_key);
    if (single)
        return single;
    const DiscBranchSet *branches = disc_many_branches(node);
    if (!branches)
        return NULL;
    for (uint32_t i = 0u; i < branches->nexpr; i++) {
        if (branches->expr[i].arity == arity)
            return branches->expr[i].child;
    }
    return NULL;
}


static DiscNode *disc_find_variable_branch(const DiscNode *node) {
    DiscEdgeKey edge_key = {0};
    DiscNode *single = disc_singleton_find(
        node, DISC_EDGE_VARIABLE, edge_key);
    if (single)
        return single;
    const DiscBranchSet *branches = disc_many_branches(node);
    return branches ? branches->var_child : NULL;
}

/* ── Removal of one coordinate ── */

typedef struct {
    DiscNode *node;
    DiscEdgeKind kind;
    DiscEdgeKey key;
} DiscPathStep;

typedef struct {
    DiscPathStep *steps;
    size_t len, cap;
} DiscPath;

static DiscNode *disc_path_step(DiscPath *path, DiscNode *node,
                                DiscEdgeKind kind, DiscEdgeKey key) {
    DiscNode *child = NULL;
    switch (kind) {
    case DISC_EDGE_SYMBOL:
        child = disc_find_symbol_branch(node, key.symbol);
        break;
    case DISC_EDGE_INTEGER:
    case DISC_EDGE_STRING:
        child = disc_find_scalar_branch(node, kind, key.integer);
        break;
    case DISC_EDGE_EXPRESSION:
        child = disc_find_expression_branch(node, key.arity);
        break;
    case DISC_EDGE_VARIABLE:
        child = disc_find_variable_branch(node);
        break;
    case DISC_EDGE_EMPTY:
    case DISC_EDGE_MANY:
        break;
    }
    if (!child)
        return NULL;
    if (path->len == path->cap) {
        path->cap = path->cap ? path->cap * 2u : 16u;
        path->steps = cetta_realloc(path->steps,
                                    path->cap * sizeof(*path->steps));
    }
    path->steps[path->len++] = (DiscPathStep){
        .node = node, .kind = kind, .key = key};
    return child;
}

/* The walk of disc_insert_atom_id, finding instead of creating, with the
 * edges it takes. */
static DiscNode *disc_find_atom_id_path(
        DiscNode *node, const TermUniverse *universe, AtomId atom_id,
        unsigned depth, CettaIndexExpansionBudget *budget, DiscPath *path) {
    if (!node || !universe || atom_id == CETTA_ATOM_ID_NONE ||
        !tu_hdr(universe, atom_id))
        return NULL;
    if (tu_kind(universe, atom_id) == ATOM_EXPR &&
        !cetta_index_expand_expression(budget, depth,
                                       tu_arity(universe, atom_id)))
        return disc_path_step(path, node, DISC_EDGE_VARIABLE,
                              (DiscEdgeKey){0});
    switch (tu_kind(universe, atom_id)) {
    case ATOM_SYMBOL:
        return disc_path_step(path, node, DISC_EDGE_SYMBOL,
                              (DiscEdgeKey){.symbol = tu_sym(universe, atom_id)});
    case ATOM_VAR:
        return disc_path_step(path, node, DISC_EDGE_VARIABLE,
                              (DiscEdgeKey){0});
    case ATOM_GROUNDED:
        if (tu_ground_kind(universe, atom_id) == GV_INT)
            return disc_path_step(
                path, node, DISC_EDGE_INTEGER,
                (DiscEdgeKey){.integer = tu_int(universe, atom_id)});
        if (tu_ground_kind(universe, atom_id) == GV_STRING)
            return disc_path_step(
                path, node, DISC_EDGE_STRING,
                (DiscEdgeKey){.integer = disc_string_key_id(
                    universe, atom_id)});
        return disc_path_step(path, node, DISC_EDGE_VARIABLE,
                              (DiscEdgeKey){0});
    case ATOM_EXPR: {
        if (tu_arity(universe, atom_id) > 0u &&
            tu_internal_tag(universe, tu_child(universe, atom_id, 0u)) ==
                CETTA_INTERNAL_TAG_LIST_REST)
            return disc_path_step(path, node, DISC_EDGE_VARIABLE,
                                  (DiscEdgeKey){0});
        DiscNode *cur = disc_path_step(
            path, node, DISC_EDGE_EXPRESSION,
            (DiscEdgeKey){.arity = tu_arity(universe, atom_id)});
        for (CettaExprIndex i = 0; cur && i < tu_arity(universe, atom_id); i++)
            cur = disc_find_atom_id_path(cur, universe,
                                         tu_child(universe, atom_id, i),
                                         depth + 1u, budget, path);
        return cur;
    }
    }
    return NULL;
}

/* Linear-probing deletion: later entries of the probe run move back. */
static void disc_int_ht_delete(DiscIntHashTable *ht, int64_t key) {
    uint32_t hole = disc_int_hash(key) & ht->mask;
    while (ht->entries[hole].child && ht->entries[hole].key != key)
        hole = (hole + 1u) & ht->mask;
    if (!ht->entries[hole].child)
        return;
    for (uint32_t next = (hole + 1u) & ht->mask; ht->entries[next].child;
         next = (next + 1u) & ht->mask) {
        uint32_t home = disc_int_hash(ht->entries[next].key) & ht->mask;
        bool stays = hole <= next ? (home > hole && home <= next)
                                  : (home > hole || home <= next);
        if (!stays) {
            ht->entries[hole] = ht->entries[next];
            hole = next;
        }
    }
    ht->entries[hole] = (DiscIntBranch){0};
    ht->count--;
}

static void disc_sym_ht_delete(DiscSymHashTable *ht, SymbolId key) {
    uint32_t hole = disc_sym_hash(key) & ht->mask;
    while (ht->entries[hole].key != SYMBOL_ID_NONE &&
           ht->entries[hole].key != key)
        hole = (hole + 1u) & ht->mask;
    if (ht->entries[hole].key == SYMBOL_ID_NONE)
        return;
    for (uint32_t next = (hole + 1u) & ht->mask;
         ht->entries[next].key != SYMBOL_ID_NONE;
         next = (next + 1u) & ht->mask) {
        uint32_t home = disc_sym_hash(ht->entries[next].key) & ht->mask;
        bool stays = hole <= next ? (home > hole && home <= next)
                                  : (home > hole || home <= next);
        if (!stays) {
            ht->entries[hole] = ht->entries[next];
            hole = next;
        }
    }
    ht->entries[hole].key = SYMBOL_ID_NONE;
    ht->entries[hole].child = NULL;
    ht->count--;
}

/* Take the edge of one step out of its node. */
static void disc_unlink(const DiscPathStep *step) {
    DiscNode *node = step->node;
    if (node->edge_kind == step->kind &&
        disc_edge_key_eq(step->kind, node->edge.one.key, step->key)) {
        node->edge_kind = DISC_EDGE_EMPTY;
        return;
    }
    DiscBranchSet *branches = node->edge_kind == DISC_EDGE_MANY
        ? node->edge.many : NULL;
    if (!branches)
        return;
    switch (step->kind) {
    case DISC_EDGE_SYMBOL:
        if (branches->sym_hashed) {
            disc_sym_ht_delete(&branches->sym_ht, step->key.symbol);
            branches->nsym--;
            break;
        }
        for (uint32_t i = 0u; i < branches->nsym; i++) {
            if (branches->sym[i].key != step->key.symbol)
                continue;
            memmove(&branches->sym[i], &branches->sym[i + 1u],
                    (branches->nsym - i - 1u) * sizeof(branches->sym[0]));
            branches->nsym--;
            break;
        }
        break;
    case DISC_EDGE_INTEGER:
    case DISC_EDGE_STRING: {
        DiscScalarBranches *scalars = step->kind == DISC_EDGE_STRING
            ? &branches->strs : &branches->ints;
        if (scalars->hashed) {
            disc_int_ht_delete(&scalars->ht, step->key.integer);
            scalars->count--;
            break;
        }
        for (uint32_t i = 0u; i < scalars->count; i++) {
            if (scalars->list[i].key != step->key.integer)
                continue;
            memmove(&scalars->list[i], &scalars->list[i + 1u],
                    (scalars->count - i - 1u) * sizeof(scalars->list[0]));
            scalars->count--;
            break;
        }
        break;
    }
    case DISC_EDGE_EXPRESSION:
        for (uint32_t i = 0u; i < branches->nexpr; i++) {
            if (branches->expr[i].arity != step->key.arity)
                continue;
            memmove(&branches->expr[i], &branches->expr[i + 1u],
                    (branches->nexpr - i - 1u) * sizeof(branches->expr[0]));
            branches->nexpr--;
            break;
        }
        break;
    case DISC_EDGE_VARIABLE:
        branches->var_child = NULL;
        break;
    case DISC_EDGE_EMPTY:
    case DISC_EDGE_MANY:
        break;
    }
}

static bool disc_node_vacant(const DiscNode *node) {
    if (node->leaf_count != 0u)
        return false;
    if (node->edge_kind == DISC_EDGE_EMPTY)
        return true;
    const DiscBranchSet *branches = disc_many_branches(node);
    return branches && branches->nsym == 0u && branches->ints.count == 0u &&
           branches->strs.count == 0u && branches->nexpr == 0u &&
           !branches->var_child;
}

/* Drop one coordinate from the leaf an id was inserted at, and the nodes
 * that hold nothing more, so later lookups do not walk them.  False when the
 * coordinate is not there; the trie then still holds it and readers skip
 * it.  Unlinked nodes stay in the pool until the trie is rebuilt. */
bool disc_remove_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, CettaIndex coordinate) {
    DiscPath path = {0};
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    DiscNode *leaf = disc_find_atom_id_path(root, universe, atom_id, 0u,
                                            &budget, &path);
    bool removed = false;
    for (CettaIndex read = 0u; leaf && read < leaf->leaf_count; read++) {
        if (disc_leaf_at(leaf, read) != coordinate)
            continue;
        for (CettaIndex next = read + 1u; next < leaf->leaf_count; next++)
            disc_leaf_set(leaf, next - 1u, disc_leaf_at(leaf, next));
        leaf->leaf_count--;
        removed = true;
        break;
    }
    if (removed) {
        DiscPool *pool = disc_root_pool(root);
        DiscNode *child = leaf;
        for (size_t step = path.len; step > 0u; step--) {
            if (!disc_node_vacant(child))
                break;
            disc_unlink(&path.steps[step - 1u]);
            disc_pool_recycle(pool, child);
            child = path.steps[step - 1u].node;
        }
    }
    free(path.steps);
    return removed;
}

/* ── Discrimination Trie Lookup (node-set based) ──────────────────────── */

/* A dynamic set of trie nodes — used during lookup to track all reachable
   positions in the trie after matching a query atom.  The depth-first
   flattening during insertion means that:
     symbol/var/int → 1 trie step
     expression(arity) → 1 step (arity branch) + arity recursive sub-terms
   Lookup must mirror this structure exactly. */

typedef struct {
    DiscNode **nodes;
    uint32_t n, c;
} DiscNodeSet;

static void dns_init(DiscNodeSet *s) { s->nodes = NULL; s->n = 0; s->c = 0; }
static void dns_free(DiscNodeSet *s) { free(s->nodes); s->nodes = NULL; s->n = 0; s->c = 0; }

static __attribute__((noinline)) void dns_grow(DiscNodeSet *s) {
    s->c = s->c ? s->c * 2 : 8;
    s->nodes = cetta_realloc(s->nodes, sizeof(DiscNode *) * s->c);
}

static inline __attribute__((always_inline)) void dns_push(DiscNodeSet *s, DiscNode *node) {
    if (!node) return;
    if (s->n >= s->c)
        dns_grow(s);
    s->nodes[s->n++] = node;
}

/* Keep the first occurrence of each node, in traversal order.  A query can
 * reach one node along two routes (a symbol and the variable branch beside
 * it), while each stored row sits in exactly one leaf, so distinct nodes
 * hold distinct rows. */
static void dns_unique(DiscNodeSet *s) {
    if (s->n <= 1u)
        return;
    uint32_t kept = 0u;
    if (s->n <= 16u) {
        for (uint32_t i = 0u; i < s->n; i++) {
            bool seen = false;
            for (uint32_t j = 0u; j < kept && !seen; j++)
                seen = s->nodes[j] == s->nodes[i];
            if (!seen)
                s->nodes[kept++] = s->nodes[i];
        }
        s->n = kept;
        return;
    }
    uint32_t cap = 32u;
    while (cap < s->n * 2u)
        cap <<= 1u;
    DiscNode **slots = cetta_malloc(sizeof(*slots) * cap);
    memset(slots, 0, sizeof(*slots) * cap);
    for (uint32_t i = 0u; i < s->n; i++) {
        DiscNode *node = s->nodes[i];
        uint64_t h = (uint64_t)(uintptr_t)node * UINT64_C(0x9e3779b97f4a7c15);
        uint32_t slot = (uint32_t)(h >> 32) & (cap - 1u);
        while (slots[slot] && slots[slot] != node)
            slot = (slot + 1u) & (cap - 1u);
        if (slots[slot])
            continue;
        slots[slot] = node;
        s->nodes[kept++] = node;
    }
    free(slots);
    s->n = kept;
}

/* Forward declarations for mutual recursion */
static void disc_step(DiscNode *node, Atom *q, DiscNodeSet *next);
static void disc_skip_term(DiscNode *node, DiscNodeSet *next);

/* Traverse a borrowed expression coordinate vector without manufacturing an
 * Atom header.  This is the storage-level realization of a projected term
 * view: the trie observes the same coordinates as an expression Atom, while
 * ownership and materialization remain with the caller. */
static void disc_step_expression_coordinates(
        DiscNode *node, Atom *const *coordinates,
        CettaExprLen coordinate_count, DiscNodeSet *next) {
    if (!node || !next || (coordinate_count > 0u && !coordinates))
        return;
    DiscNode *expression = disc_find_expression_branch(
        node, coordinate_count);
    if (expression) {
        DiscNodeSet current;
        dns_init(&current);
        dns_push(&current, expression);
        for (CettaExprIndex coordinate = 0u;
             coordinate < coordinate_count; coordinate++) {
            DiscNodeSet following;
            dns_init(&following);
            for (uint32_t candidate = 0u;
                 candidate < current.n; candidate++) {
                disc_step(
                    current.nodes[candidate], coordinates[coordinate],
                    &following);
            }
            dns_free(&current);
            current = following;
        }
        for (uint32_t candidate = 0u;
             candidate < current.n; candidate++) {
            dns_push(next, current.nodes[candidate]);
        }
        dns_free(&current);
    }
    /* A variable stored at this position matches the complete expression. */
    dns_push(next, disc_find_variable_branch(node));
}

/* Skip one complete term from the trie.  A query variable can match any
   indexed term, so we must advance past the entire depth-first encoding
   of whatever term appears at this position. */
static void disc_skip_term(DiscNode *node, DiscNodeSet *next) {
    if (!node)
        return;
    if (node->edge_kind != DISC_EDGE_MANY) {
        if (node->edge_kind == DISC_EDGE_EMPTY)
            return;
        if (node->edge_kind != DISC_EDGE_EXPRESSION) {
            dns_push(next, node->edge.one.child);
            return;
        }
        DiscNodeSet cur;
        dns_init(&cur);
        dns_push(&cur, node->edge.one.child);
        for (CettaExprIndex ci = 0u;
             ci < node->edge.one.key.arity; ci++) {
            DiscNodeSet tmp;
            dns_init(&tmp);
            for (uint32_t ni = 0; ni < cur.n; ni++)
                disc_skip_term(cur.nodes[ni], &tmp);
            dns_free(&cur);
            cur = tmp;
        }
        /* After skipping all sub-terms, cur holds the continuations */
        for (uint32_t ni = 0; ni < cur.n; ni++)
            dns_push(next, cur.nodes[ni]);
        dns_free(&cur);
        return;
    }

    const DiscBranchSet *branches = node->edge.many;
    /* Symbol, variable, integer and string branches each consume one trie
     * step. */
    if (branches->sym_hashed) {
        uint32_t cap = branches->sym_ht.mask + 1u;
        for (uint32_t i = 0u; i < cap; i++) {
            if (branches->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                dns_push(next, branches->sym_ht.entries[i].child);
        }
    } else {
        for (uint32_t i = 0u; i < branches->nsym; i++)
            dns_push(next, branches->sym[i].child);
    }
    dns_push(next, branches->var_child);
    const DiscScalarBranches *scalars[] = {&branches->ints, &branches->strs};
    for (size_t kind = 0u; kind < 2u; kind++) {
        const DiscScalarBranches *each = scalars[kind];
        uint32_t cap = each->hashed ? each->ht.mask + 1u : each->count;
        for (uint32_t i = 0u; i < cap; i++)
            dns_push(next, each->hashed ? each->ht.entries[i].child
                                        : each->list[i].child);
    }
    /* Expression branches consume their arity tag and complete subterms. */
    for (uint32_t i = 0u; i < branches->nexpr; i++) {
        DiscNodeSet cur;
        dns_init(&cur);
        dns_push(&cur, branches->expr[i].child);
        for (CettaExprIndex ci = 0u;
             ci < branches->expr[i].arity; ci++) {
            DiscNodeSet tmp;
            dns_init(&tmp);
            for (uint32_t ni = 0u; ni < cur.n; ni++)
                disc_skip_term(cur.nodes[ni], &tmp);
            dns_free(&cur);
            cur = tmp;
        }
        for (uint32_t ni = 0u; ni < cur.n; ni++)
            dns_push(next, cur.nodes[ni]);
        dns_free(&cur);
    }
}

/* Advance through one query atom, collecting all reachable next-nodes.
   Mirrors the depth-first structure of disc_insert_atom exactly. */
/* Float facts sit on the variable branch.  Integer facts sit on integer
 * branches.  An HE float query follows every integer key that promotes
 * equal to it; below 2^53 that key is unique. */
static void disc_push_he_float_ints(DiscNode *node, double value,
                                   DiscNodeSet *next) {
    int64_t exact = 0;
    CettaHeFloatIntBranches mode =
        cetta_he_float_int_branches(value, &exact);
    if (!node || !next || mode == CETTA_HE_FLOAT_INTS_NONE)
        return;
    if (mode == CETTA_HE_FLOAT_INTS_ONE) {
        dns_push(next, disc_find_integer_branch(node, exact));
        return;
    }
    if (node->edge_kind == DISC_EDGE_INTEGER &&
        cetta_he_promoted_kind_equal(
            GV_FLOAT, 0, value, GV_INT,
            node->edge.one.key.integer, 0.0)) {
        dns_push(next, node->edge.one.child);
        return;
    }
    const DiscBranchSet *branches = disc_many_branches(node);
    if (!branches)
        return;
    if (branches->ints.hashed) {
        uint32_t cap = branches->ints.ht.mask + 1u;
        for (uint32_t i = 0u; i < cap; i++) {
            const DiscIntBranch *entry = &branches->ints.ht.entries[i];
            if (!entry->child)
                continue;
            if (cetta_he_promoted_kind_equal(
                    GV_FLOAT, 0, value, GV_INT, entry->key, 0.0))
                dns_push(next, entry->child);
        }
        return;
    }
    for (uint32_t i = 0u; i < branches->ints.count; i++) {
        if (cetta_he_promoted_kind_equal(
                GV_FLOAT, 0, value, GV_INT, branches->ints.list[i].key, 0.0))
            dns_push(next, branches->ints.list[i].child);
    }
}

static void disc_step(DiscNode *node, Atom *q, DiscNodeSet *next) {
    if (!node) return;
    switch (q->kind) {
    case ATOM_SYMBOL:
        dns_push(next, disc_find_symbol_branch(node, q->sym_id));
        /* A variable in the indexed LHS matches any query symbol */
        dns_push(next, disc_find_variable_branch(node));
        break;

    case ATOM_VAR:
        /* Query variable matches any indexed term — skip one complete term */
        disc_skip_term(node, next);
        break;

    case ATOM_GROUNDED:
        if (q->ground.gkind == GV_INT)
            dns_push(next, disc_find_integer_branch(
                node, q->ground.ival));
        else if (q->ground.gkind == GV_FLOAT)
            disc_push_he_float_ints(node, q->ground.fval, next);
        else if (q->ground.gkind == GV_STRING)
            dns_push(next, disc_find_scalar_branch(
                node, DISC_EDGE_STRING,
                disc_string_key(q)));
        /* A variable in the indexed LHS matches any grounded value */
        dns_push(next, disc_find_variable_branch(node));
        break;

    case ATOM_EXPR:
        /* A list pattern in a query may meet any indexed term there. */
        if (atom_is_list_rest(q)) {
            disc_skip_term(node, next);
            break;
        }
        disc_step_expression_coordinates(
            node, q->expr.elems, q->expr.len, next);
        break;
    }
}

static void disc_collect_leaves(
        const DiscNodeSet *final, CettaIndex **out,
        CettaIndex *nout, CettaIndex *cout) {
    for (uint32_t i = 0u; i < final->n; i++) {
        DiscNode *node = final->nodes[i];
        for (CettaIndex j = 0u; j < node->leaf_count; j++) {
            if (*nout >= *cout) {
                *cout = *cout ? *cout * 2u : 16u;
                *out = cetta_realloc(*out, sizeof(CettaIndex) * *cout);
            }
            (*out)[(*nout)++] = disc_leaf_at(node, j);
        }
    }
}

void disc_lookup(DiscNode *root, Atom *query, CettaIndex **out,
                 CettaIndex *nout, CettaIndex *cout) {
    *out = NULL; *nout = 0; *cout = 0;
    /* disc_step from root through the query atom */
    DiscNodeSet final;
    dns_init(&final);
    disc_step(root, query, &final);
    dns_unique(&final);
    disc_collect_leaves(&final, out, nout, cout);
    dns_free(&final);
}

/* While current, a pinned view borrows its Space's rows and trie and
 * `space` names that Space.  Detaching copies the rows and their removal
 * marks into the view, takes the trie, and clears `space`: the view is then
 * the only source of its cursors, and the Space is free to rewrite its own
 * rows.  The shared transition domain protects references and the
 * attached-to-detached transition together; atomic reference counts alone
 * would not protect the borrowed rows and trie during mutation. */
struct SpacePinnedOccurrences {
    uint32_t references;
    Space *space;
    uint8_t *atom_ids;
    uint8_t atom_id_width_bits;
    CettaCount len;
    DiscNode *match_trie;
    TermUniverse *universe;
    /* The rows marked removed when the view detached, as the Space keeps
     * its marks: a bitmap and a Fenwick tree of the live rows of each
     * 64-row word of [0, mark_rows). */
    uint64_t *marks;
    CettaIndex *mark_tree;
    CettaIndex mark_rows;
    CettaIndex marked;
    /* The head indexes its cursors read, taken when it detached. */
    CettaSpaceHeadIndex *head_index;
};

static inline CettaIndex space_mark_lowbit(CettaIndex i) {
    return i & (~i + 1u);
}

/* Each byte of the result counts the set bits of that byte of `bits`. */
static inline uint64_t space_marks_byte_counts(uint64_t bits) {
    bits -= (bits >> 1) & UINT64_C(0x5555555555555555);
    bits = (bits & UINT64_C(0x3333333333333333)) +
        ((bits >> 2) & UINT64_C(0x3333333333333333));
    return (bits + (bits >> 4)) & UINT64_C(0x0F0F0F0F0F0F0F0F);
}

/* The set bits of a word: the instruction where the target has one, else
 * the byte counts summed, not the C library's call. */
static inline CettaIndex space_marks_bits(uint64_t bits) {
#if defined(__POPCNT__)
    return (CettaIndex)__builtin_popcountll(bits);
#else
    return (CettaIndex)((space_marks_byte_counts(bits) *
                         UINT64_C(0x0101010101010101)) >> 56);
#endif
}

/* The rows of word `word` that [0, rows) covers. */
static inline uint64_t space_marks_word_rows(CettaIndex rows,
                                             CettaIndex word) {
    CettaIndex first = word * 64u;
    if (rows >= first + 64u)
        return ~UINT64_C(0);
    return rows > first ? (UINT64_C(1) << (rows - first)) - 1u : 0u;
}

/* The live rows before `row`, which is at most `rows`: the tree's count of
 * the words before its word, and the unmarked bits below it in its word. */
static CettaIndex space_marks_live_before(const uint64_t *marks,
                                          const CettaIndex *tree,
                                          CettaIndex row) {
    CettaIndex live = 0u;
    for (CettaIndex i = row >> 6; i > 0u; i -= space_mark_lowbit(i))
        live += tree[i];
    if (row & 63u)
        live += space_marks_bits(
            ~marks[row >> 6] & ((UINT64_C(1) << (row & 63u)) - 1u));
    return live;
}

/* The position of the `rank`th (from 1) set bit of `bits`, which holds at
 * least `rank`: the byte holding it from the bytes' running counts, then
 * the bit within that byte. */
static unsigned space_marks_select_bit(uint64_t bits, CettaIndex rank) {
    /* Byte i of `running` counts the set bits of bytes 0 to i. */
    uint64_t running = space_marks_byte_counts(bits) *
        UINT64_C(0x0101010101010101);
    unsigned shift = 0u;
    while (shift < 56u && ((running >> shift) & 0xFFu) < rank)
        shift += 8u;
    if (shift)
        rank -= (running >> (shift - 8u)) & 0xFFu;
    bits >>= shift;
    while (--rank > 0u)
        bits &= bits - 1u;
    return shift + (unsigned)__builtin_ctzll(bits);
}

/* The removal marks over a view's rows: its Space's while the view is
 * current, which no removal changes before detaching it, and the ones it
 * copied when it detached. */
typedef struct {
    const uint64_t *marks;
    const CettaIndex *tree;
    CettaIndex rows;
    CettaIndex marked;
} SpaceViewMarks;

static SpaceViewMarks space_view_marks(const SpacePinnedOccurrences *view) {
    if (view->space) {
        const SpaceNativeStorage *n = &view->space->native;
        return (SpaceViewMarks){n->marks, n->mark_tree, n->mark_rows,
                                n->marked};
    }
    return (SpaceViewMarks){view->marks, view->mark_tree, view->mark_rows,
                            view->marked};
}

static inline bool space_view_row_marked(const SpaceViewMarks *m,
                                         CettaIndex row) {
    return m->marked != 0u && row < m->rows &&
           ((m->marks[row >> 6] >> (row & 63u)) & 1u) != 0u;
}

/* A live row's logical index: the live rows before it. */
static CettaIndex space_view_logical(const SpaceViewMarks *m, CettaIndex row) {
    if (m->marked == 0u)
        return row;
    if (row >= m->rows)
        return m->rows - m->marked + (row - m->rows);
    return space_marks_live_before(m->marks, m->tree, row);
}

/* The row holding live logical index `logical`. */
static CettaIndex space_view_row(const SpaceViewMarks *m, CettaIndex logical) {
    if (m->marked == 0u)
        return logical;
    CettaIndex live_in_tree = m->rows - m->marked;
    if (logical >= live_in_tree)
        return m->rows + (logical - live_in_tree);
    /* The word holding it: the most words whose live rows fall short of
     * it.  Then its rank among that word's live rows. */
    CettaIndex words = (m->rows + 63u) >> 6;
    /* The largest power of two not above the words. */
    CettaIndex step = (CettaIndex)1u << (63 - __builtin_clzll(words));
    CettaIndex word = 0u;
    CettaIndex remaining = logical + 1u;
    for (; step; step >>= 1u) {
        if (word + step <= words && m->tree[word + step] < remaining) {
            word += step;
            remaining -= m->tree[word];
        }
    }
    uint64_t live = ~m->marks[word] & space_marks_word_rows(m->rows, word);
    return word * 64u + space_marks_select_bit(live, remaining);
}

static SpacePinnedOccurrences *space_pinned_occurrences_acquire(Space *s) {
    SpaceMatchNativeState *st = &s->match_backend.native;
    if (!st->pinned) {
        st->pinned = cetta_malloc(sizeof(*st->pinned));
        *st->pinned = (SpacePinnedOccurrences){.space = s};
    }
    st->pinned->references++;
    space_match_native_pin_trie(s);
    return st->pinned;
}

static SpacePinnedOccurrences *space_pinned_occurrences_share(
        SpacePinnedOccurrences *view) {
    view->references++;
    if (view->space)
        space_match_native_pin_trie(view->space);
    return view;
}

/* A detached view's rows, as its readers see them: the rows its own marks
 * did not remove.  The view holds them, one reference a row, while it
 * lives. */
static void space_pinned_occurrences_roots(const TermUniverse *universe,
                                           TermUniverseRootVisit visit,
                                           void *visit_context, void *ctx) {
    const SpacePinnedOccurrences *view = ctx;
    size_t width = cetta_atom_id_storage_width_bytes_from_bits(
        view->atom_id_width_bits);
    (void)universe;
    for (CettaIndex row = 0u; view->atom_ids && row < view->len; row++) {
        if (row < view->mark_rows && view->marks &&
            ((view->marks[row >> 6] >> (row & 63u)) & 1u) != 0u)
            continue;
        visit(visit_context,
              cetta_atom_id_storage_load_bits(
                  view->atom_ids + (size_t)row * width,
                  view->atom_id_width_bits));
    }
}

static void space_pinned_occurrences_release(SpacePinnedOccurrences *view) {
    if (!view)
        return;
    if (view->space)
        space_match_native_unpin_trie(view->space);
    if (--view->references != 0u)
        return;
    if (view->space) {
        view->space->match_backend.native.pinned = NULL;
    } else {
        term_universe_remove_root_provider(
            view->universe, space_pinned_occurrences_roots, view);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_SPACE_RETIRED_OCCURRENCE_RELEASE_BYTES,
            (size_t)view->len *
                cetta_atom_id_storage_width_bytes_from_bits(view->atom_id_width_bits));
        free(view->atom_ids);
        free(view->marks);
        free(view->mark_tree);
        space_head_index_free(view->head_index);
        disc_node_free(view->match_trie);
    }
    free(view);
}

void space_pinned_occurrences_detach(Space *s) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    SpaceMatchNativeState *st = s ? &s->match_backend.native : NULL;
    SpacePinnedOccurrences *view = st ? st->pinned : NULL;
    if (!view)
        return;
    /* The copy is of the rows as they stand, with the rows marked removed
     * and their marks, so the Space need not contract them first. */
    size_t width = cetta_atom_id_storage_width_bytes_from_bits(
        s->native.atom_id_width_bits);
    size_t first = s->kind == SPACE_KIND_QUEUE ? (size_t)s->native.start : 0u;
    if (width != 0u && (size_t)s->native.len > SIZE_MAX / width) {
        fputs("CeTTa: pinned occurrence view too large to detach\n", stderr);
        abort();
    }
    size_t bytes = (size_t)s->native.len * width;
    view->atom_ids = bytes ? cetta_malloc(bytes) : NULL;
    if (bytes)
        memcpy(view->atom_ids, s->native.atom_ids + first * width, bytes);
    view->atom_id_width_bits = s->native.atom_id_width_bits;
    view->len = s->native.len;
    view->universe = s->native.universe;
    if (s->native.marked != 0u) {
        size_t words = ((size_t)s->native.mark_rows + 63u) / 64u;
        size_t nodes = words + 1u;
        view->marks = cetta_malloc(words * sizeof(*view->marks));
        view->mark_tree = cetta_malloc(nodes * sizeof(*view->mark_tree));
        memcpy(view->marks, s->native.marks, words * sizeof(*view->marks));
        memcpy(view->mark_tree, s->native.mark_tree,
               nodes * sizeof(*view->mark_tree));
        view->mark_rows = s->native.mark_rows;
        view->marked = s->native.marked;
    }
    view->match_trie = st->match_trie;
    view->head_index = space_head_index_take(s);
    view->space = NULL;
    /* A detached view lives while a reader walks it, rarely across a
     * collection, so it lists its rows when one comes rather than counting
     * them when it detaches and again when it goes. */
    (void)term_universe_add_root_provider(
        view->universe, space_pinned_occurrences_roots, view);
    st->match_trie = NULL;
    st->match_trie_dirty = false;
    st->match_trie_stale_occurrences = 0u;
    st->match_trie_pins = 0u;
    st->pinned = NULL;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_SPACE_PINNED_OCCURRENCES_DETACH);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_SPACE_PINNED_OCCURRENCES_DETACHED_ROW,
        view->len);
}

/* The rows a cursor reads: its Space's while the view is current, the
 * captured copy once detached. */
static CettaCount space_pinned_occurrences_len(
        const SpacePinnedOccurrences *view) {
    return view->space ? view->space->native.len : view->len;
}

void space_occurrence_cursor_init_empty(SpaceOccurrenceCursor *cursor) {
    if (cursor)
        memset(cursor, 0, sizeof(*cursor));
}

static CettaIndex space_occurrence_cursor_head(
        const SpaceOccurrenceCursor *cursor, uint32_t node, CettaIndex leaf) {
    return (leaf << cursor->heap_node_bits) | node;
}

static void space_occurrence_cursor_heap_push(
        SpaceOccurrenceCursor *cursor, CettaIndex head) {
    uint32_t index = cursor->heap_len++;
    while (index > 0u) {
        uint32_t parent = (index - 1u) / 2u;
        if (head >= cursor->heap[parent])
            break;
        cursor->heap[index] = cursor->heap[parent];
        index = parent;
    }
    cursor->heap[index] = head;
}

static void space_occurrence_cursor_heap_sift(
        SpaceOccurrenceCursor *cursor, uint32_t index, CettaIndex head) {
    while (index < cursor->heap_len / 2u) {
        uint32_t left = index * 2u + 1u;
        uint32_t right = left + 1u;
        uint32_t child = (right < cursor->heap_len &&
                          cursor->heap[right] < cursor->heap[left])
            ? right : left;
        if (cursor->heap[child] >= head)
            break;
        cursor->heap[index] = cursor->heap[child];
        index = child;
    }
    cursor->heap[index] = head;
}

static CettaIndex space_occurrence_cursor_heap_pop(SpaceOccurrenceCursor *cursor) {
    CettaIndex top = cursor->heap[0];
    if (--cursor->heap_len > 0u)
        space_occurrence_cursor_heap_sift(
            cursor, 0u, cursor->heap[cursor->heap_len]);
    return top;
}

/* Build a frontier from current positions in linear time. Pulling reads only
 * the least heads; a cancelled prefix never visits the remaining leaf bag.
 * Allocation failure retains the same declaration-order node scan. */
static void space_occurrence_cursor_prepare_heap(SpaceOccurrenceCursor *cursor) {
    size_t bytes;
    if (cursor->full_scan || cursor->flat_mode || cursor->head_mode ||
        cursor->node_len <= 1u ||
        __builtin_mul_overflow((size_t)cursor->node_len,
                              sizeof(*cursor->heap), &bytes))
        return;
    uint32_t bits = 0u;
    for (uint32_t top = cursor->node_len - 1u; top > 0u; top >>= 1u)
        bits++;
    /* Integer comparison orders occurrences first and branch ordinals second.
     * Wider coordinate ranges retain the position-preserving node scan. */
    if (cursor->ceiling > 0u &&
        cursor->ceiling - 1u > (UINT64_MAX >> bits))
        return;
    cursor->heap = cetta_malloc(bytes);
    if (!cursor->heap)
        return;
    cursor->heap_node_bits = bits;
    for (uint32_t index = 0u; index < cursor->node_len; index++) {
        DiscNode *node = cursor->nodes[index];
        CettaIndex position = cursor->leaf_pos[index];
        if (!node || position >= node->leaf_count)
            continue;
        if (disc_leaf_at(node, position) >= cursor->ceiling) {
            cursor->leaf_pos[index] = node->leaf_count;
            continue;
        }
        cursor->heap[cursor->heap_len++] = space_occurrence_cursor_head(
            cursor, index, disc_leaf_at(node, position));
    }
    for (uint32_t index = cursor->heap_len / 2u; index > 0u; index--)
        space_occurrence_cursor_heap_sift(cursor, index - 1u,
                                         cursor->heap[index - 1u]);
}

static bool space_occurrence_cursor_heap_next(
        SpaceOccurrenceCursor *cursor, CettaCount view_len, CettaIndex *out) {
    CettaIndex mask = (UINT64_C(1) << cursor->heap_node_bits) - 1u;
    while (cursor->heap_len > 0u) {
        CettaIndex best = cursor->heap[0] >> cursor->heap_node_bits;
        uint32_t deferred_len = 0u;
        while (cursor->heap_len > 0u &&
                (cursor->heap[0] >> cursor->heap_node_bits) == best) {
            uint32_t head = (uint32_t)(space_occurrence_cursor_heap_pop(cursor) & mask);
            DiscNode *node = cursor->nodes[head];
            CettaIndex position = ++cursor->leaf_pos[head];
            if (position >= node->leaf_count)
                continue;
            if (disc_leaf_at(node, position) >= cursor->ceiling) {
                cursor->leaf_pos[head] = node->leaf_count;
                continue;
            }
            /* Popping frees at least one slot. Queue successor heads at the
             * high end, then insert from low to high so no unread head moves. */
            cursor->heap[cursor->node_len - ++deferred_len] =
                space_occurrence_cursor_head(
                    cursor, head, disc_leaf_at(node, position));
        }
        for (uint32_t index = cursor->node_len - deferred_len;
             index < cursor->node_len; index++)
            space_occurrence_cursor_heap_push(cursor, cursor->heap[index]);
        if (best < view_len) {
            *out = best;
            return true;
        }
    }
    return false;
}

/* A clean native index stores each source occurrence in exactly one leaf.
 * After deduplicating selected leaf nodes, their lengths therefore certify
 * full coverage without reading or copying the occurrence bag. Stale leaf
 * coordinates do not carry that certificate. */
static bool space_occurrence_cursor_frontier_is_full(
        const Space *space, const DiscNodeSet *frontier, uint32_t length) {
    const SpaceMatchNativeState *state = &space->match_backend.native;
    if (state->match_trie_dirty || state->match_trie_stale_occurrences != 0u)
        return false;
    /* The trie holds the live rows only. */
    CettaCount live = space->native.len - space->native.marked;
    CettaCount selected = 0u;
    for (uint32_t index = 0u; index < length; index++) {
        CettaCount count = frontier->nodes[index]->leaf_count;
        if (count > live - selected)
            return false;
        selected += count;
    }
    return selected == live;
}

/* Access preparation may contract marked storage. Capture its resulting
 * coordinates and pin them together, after the access path is ready. */
static bool space_occurrence_cursor_capture(
        Space *space, SpaceOccurrenceCursor *cursor) {
    cursor->read = space_read_token(space);
    cursor->prefix_epoch = space->prefix_epoch;
    cursor->ceiling = space->native.len;
    cursor->occurrences = space_pinned_occurrences_acquire(space);
    cursor->pinned = true;
    return space_read_token_prefix_intact(cursor->read);
}

/* ── Facts a relation kept, lent by occurrence cursors ─────────────────── */

/* The facts of a space's relation that a pattern selects, taken when an
 * occurrence cursor begins and lent after its rows: their columns, and each
 * fact's record once a cursor has lent it.  It lists its ids to the
 * collector while it lives, so a fact lent stays while a cursor sharing it
 * lives, and the columns stay though the relation goes. */
typedef struct SpaceKeptOccurrences {
    uint32_t references;
    uint32_t arity;
    TermUniverse *universe;
    AtomId head_id;
    size_t len, cap;
    AtomId *columns;
    AtomId *records;
} SpaceKeptOccurrences;

static void space_kept_occurrences_roots(const TermUniverse *universe,
                                         TermUniverseRootVisit visit,
                                         void *visit_context, void *ctx) {
    const SpaceKeptOccurrences *kept = ctx;
    (void)universe;
    visit(visit_context, kept->head_id);
    for (size_t i = 0u; i < kept->len * kept->arity; i++)
        visit(visit_context, kept->columns[i]);
    for (size_t i = 0u; kept->records && i < kept->len; i++) {
        if (kept->records[i] != CETTA_ATOM_ID_NONE)
            visit(visit_context, kept->records[i]);
    }
}

static void space_kept_occurrences_release(SpaceKeptOccurrences *kept) {
    if (!kept || --kept->references != 0u)
        return;
    term_universe_remove_root_provider(kept->universe,
                                       space_kept_occurrences_roots, kept);
    free(kept->columns);
    free(kept->records);
    free(kept);
}

static bool space_kept_occurrences_push(void *context,
                                        const CettaDenseId *tuple) {
    SpaceKeptOccurrences *kept = context;
    if (kept->len == kept->cap) {
        size_t cap = kept->cap ? kept->cap * 2u : 16u;
        AtomId *columns = realloc(kept->columns,
                                  cap * kept->arity * sizeof(*columns));
        if (!columns)
            return false;
        kept->columns = columns;
        kept->cap = cap;
    }
    for (uint32_t c = 0u; c < kept->arity; c++)
        kept->columns[kept->len * kept->arity + c] = (AtomId)tuple[c];
    kept->len++;
    return true;
}

/* The facts of the space's relation that PATTERN selects, for an occurrence
 * cursor beginning now.  NULL when no relation of the space answers for the
 * pattern: its readers give the relation back to the rows first.  A column
 * that is an expression selects none of the relation's facts, whose columns
 * are atoms; a number other than an integer may equal one by value, which
 * the rows decide. */
static SpaceKeptOccurrences *space_dense_occurrences(Space *s,
                                                     const Atom *pattern) {
    if (!s || space_dense_facts(s) == 0u || !pattern ||
        pattern->kind != ATOM_EXPR || pattern->expr.len < 2u ||
        pattern->expr.len > CETTA_TUPLE_MAX_ARITY + 1u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    SpaceDenseForm form;
    if (!space_dense_form(s, pattern->expr.elems[0]->sym_id, &form) ||
        (CettaExprLen)form.arity + 1u != pattern->expr.len)
        return NULL;
    CettaDenseId fixed[CETTA_TUPLE_MAX_ARITY];
    bool none = false;
    for (uint32_t c = 0u; c < form.arity; c++) {
        Atom *column = pattern->expr.elems[c + 1u];
        fixed[c] = CETTA_DENSE_ANY;
        if (column->kind == ATOM_VAR)
            continue;
        if (column->kind == ATOM_EXPR) {
            none = true;
            continue;
        }
        if (column->kind != ATOM_SYMBOL &&
            (column->kind != ATOM_GROUNDED ||
             (column->ground.gkind != GV_INT &&
              column->ground.gkind != GV_BOOL &&
              column->ground.gkind != GV_STRING)))
            return NULL;
        AtomId id = term_universe_lookup_atom_id(s->native.universe, column);
        if (id == CETTA_ATOM_ID_NONE)
            none = true;
        else
            fixed[c] = id;
    }
    SpaceKeptOccurrences *kept = calloc(1u, sizeof(*kept));
    if (!kept)
        return NULL;
    kept->references = 1u;
    kept->arity = form.arity;
    kept->universe = s->native.universe;
    kept->head_id = form.head_id;
    bool ok = none || space_dense_form_each(form, fixed,
                                            space_kept_occurrences_push, kept);
    if (ok && kept->len != 0u) {
        kept->records = malloc(kept->len * sizeof(*kept->records));
        ok = kept->records != NULL;
        for (size_t i = 0u; ok && i < kept->len; i++)
            kept->records[i] = CETTA_ATOM_ID_NONE;
    }
    if (!ok || !term_universe_add_root_provider(
                   kept->universe, space_kept_occurrences_roots, kept)) {
        free(kept->columns);
        free(kept->records);
        free(kept);
        return NULL;
    }
    return kept;
}

/* The record of fact I, made when it is first lent. */
static AtomId space_kept_occurrences_id(SpaceKeptOccurrences *kept, size_t i) {
    if (kept->records[i] == CETTA_ATOM_ID_NONE) {
        AtomId kids[CETTA_TUPLE_MAX_ARITY + 1u];
        kids[0] = kept->head_id;
        memcpy(&kids[1], &kept->columns[i * kept->arity],
               kept->arity * sizeof(*kids));
        kept->records[i] = tu_expr_from_ids(kept->universe, kids,
                                            kept->arity + 1u);
    }
    return kept->records[i];
}

static bool space_occurrence_cursor_init_rows(Space *s, Atom *pattern,
                                              SpaceOccurrenceCursor *cursor);

/* The rows the pattern may match, then the facts the space's relation for
 * it kept: the relation stays, and only the facts lent get records. */
bool space_occurrence_cursor_init(Space *s, Atom *pattern,
                                  SpaceOccurrenceCursor *cursor) {
    SpaceKeptOccurrences *kept = space_dense_occurrences(s, pattern);
    if (!kept)
        return space_occurrence_cursor_init_rows(s, pattern, cursor);
    space_dense_reading_begin();
    bool ok = space_occurrence_cursor_init_rows(s, pattern, cursor);
    space_dense_reading_end();
    if (!ok) {
        space_kept_occurrences_release(kept);
        return false;
    }
    cursor->kept = kept;
    return true;
}

static bool space_occurrence_cursor_init_rows(Space *s, Atom *pattern,
                                              SpaceOccurrenceCursor *cursor) {
    space_dense_settle_for_pattern(s, pattern);
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    space_occurrence_cursor_init_empty(cursor);
    if (!s || !pattern || !cursor || s->overlay_base || s->dep_count != 0u ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    space_linearize(s);
    cursor->space = s;
    if (pattern->kind == ATOM_VAR ||
        space_match_reads_rows(s, s->native.len)) {
        cursor->full_scan = true;
        return space_occurrence_cursor_capture(s, cursor);
    }
    if (space_match_native_exact_occurrences(
            s, pattern, &cursor->flat, &cursor->flat_len)) {
        cursor->flat_mode = true;
        return space_occurrence_cursor_capture(s, cursor);
    }
    /* A head over variables selects by its head alone: its rows, read in
     * order as they are needed, where the trie would merge every leaf below
     * the head before the first match. */
    if (space_head_index_open(s, pattern, &cursor->head)) {
        cursor->head_start = cursor->head;
        cursor->head_mode = true;
        return space_occurrence_cursor_capture(s, cursor);
    }
    /* Rows removed by marks stay where they are and the cursor skips them.
     * The cold scan and a position index being built read rows by logical
     * position, so a marked Space takes the trie instead. */
    bool marked = space_has_marks(s);
    bool leads = space_path_index_leads(pattern);
    bool indexed_first = !marked && leads && space_path_index_may_select(pattern) &&
        space_path_index_candidates(s, pattern, &cursor->flat, &cursor->flat_len);
    if (indexed_first)
        space_match_native_note_indexed_query(s);
    if (indexed_first ||
        (!marked && space_match_native_try_cold_candidates(
            s, pattern, &cursor->flat, &cursor->flat_len)) ||
        (!marked && !leads && space_path_index_may_select(pattern) &&
         space_path_index_candidates(s, pattern, &cursor->flat, &cursor->flat_len)) ||
        (!marked && !s->match_backend.native.match_trie &&
         space_path_index_prefix_candidates(s, pattern, true, &cursor->flat,
                                            &cursor->flat_len))) {
        cursor->flat_mode = true;
        return space_occurrence_cursor_capture(s, cursor);
    }
    space_match_native_ensure_trie(s);
    DiscNode *root = s->match_backend.native.match_trie;
    if (!root)
        return false;
    cursor->row_order = space_is_ordered(s);
    DiscNodeSet final;
    dns_init(&final);
    disc_step(root, pattern, &final);
    dns_unique(&final);
    uint32_t unique = final.n;
    if (space_occurrence_cursor_frontier_is_full(s, &final, unique)) {
        dns_free(&final);
        cursor->full_scan = true;
        return space_occurrence_cursor_capture(s, cursor);
    }
    cursor->node_len = unique;
    if (unique > 0u) {
        cursor->nodes = cetta_malloc(sizeof(*cursor->nodes) * unique);
        cursor->leaf_pos = cetta_malloc(sizeof(*cursor->leaf_pos) * unique);
        if (!cursor->nodes || !cursor->leaf_pos) {
            free(cursor->nodes);
            free(cursor->leaf_pos);
            dns_free(&final);
            space_occurrence_cursor_init_empty(cursor);
            return false;
        }
        memcpy(cursor->nodes, final.nodes, sizeof(*cursor->nodes) * unique);
        memset(cursor->leaf_pos, 0, sizeof(*cursor->leaf_pos) * unique);
    }
    dns_free(&final);
    if (!space_occurrence_cursor_capture(s, cursor))
        return false;
    if (cursor->row_order)
        space_occurrence_cursor_prepare_heap(cursor);
    return true;
}

/* Both clone modes own their traversal arrays and share only the pinned rows.
 * Rewinding is permitted only while that view is still the whole live prefix. */
static bool space_occurrence_cursor_copy(
        const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst,
        bool rewind) {
    if (!dst || src == dst)
        return false;
    space_occurrence_cursor_init_empty(dst);
    if (!src || !src->pinned || !src->space || !src->occurrences)
        return false;
    Space *live = src->occurrences->space;
    if ((live &&
         (src->read.instance_id != space_instance_id(live) ||
          src->prefix_epoch != live->prefix_epoch)) ||
        (rewind && (!live || src->ceiling != live->native.len)) ||
        src->ceiling > space_pinned_occurrences_len(src->occurrences) ||
        (src->node_len > 0u && (!src->nodes || !src->leaf_pos)) ||
        (src->flat_mode && src->flat_len > 0u && !src->flat) ||
        src->heap_len > src->node_len) {
        return false;
    }

    *dst = *src;
    dst->nodes = NULL;
    dst->leaf_pos = NULL;
    dst->flat = NULL;
    dst->heap = NULL;
    dst->heap_len = 0u;
    dst->occurrences = NULL;
    dst->pinned = false;
    if (dst->kept)
        dst->kept->references++;
    if (rewind) {
        dst->full_next = 0u;
        dst->flat_next = 0u;
        dst->head = src->head_start;
        dst->has_last = false;
        dst->kept_next = 0u;
        dst->kept_turn = false;
    }
    if (src->flat_mode && src->flat_len > 0u) {
        dst->flat = cetta_malloc(sizeof(*dst->flat) * (size_t)src->flat_len);
        if (!dst->flat)
            goto failed;
        memcpy(dst->flat, src->flat,
               sizeof(*dst->flat) * (size_t)src->flat_len);
    }
    if (src->node_len > 0u) {
        dst->nodes = cetta_malloc(sizeof(*dst->nodes) * src->node_len);
        dst->leaf_pos = cetta_malloc(sizeof(*dst->leaf_pos) * src->node_len);
        if (!dst->nodes || !dst->leaf_pos)
            goto failed;
        memcpy(dst->nodes, src->nodes, sizeof(*dst->nodes) * src->node_len);
        if (rewind)
            memset(dst->leaf_pos, 0, sizeof(*dst->leaf_pos) * src->node_len);
        else
            memcpy(dst->leaf_pos, src->leaf_pos,
                   sizeof(*dst->leaf_pos) * src->node_len);
    }
    if (rewind) {
        dst->node_next = 0u;
        if (src->row_order)
            space_occurrence_cursor_prepare_heap(dst);
    } else if (src->heap) {
        dst->heap = cetta_malloc(sizeof(*dst->heap) * (size_t)src->node_len);
        if (dst->heap) {
            dst->heap_len = src->heap_len;
            memcpy(dst->heap, src->heap, sizeof(*dst->heap) * src->heap_len);
        }
        /* Without the optional heap, the position-preserving scan has the
         * same next occurrence and terminal result. */
    }
    dst->occurrences = space_pinned_occurrences_share(src->occurrences);
    dst->pinned = true;
    return true;

failed:
    space_occurrence_cursor_release(dst);
    return false;
}

bool space_occurrence_cursor_clone_unstarted(
        const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    return space_occurrence_cursor_copy(src, dst, true);
}

bool space_occurrence_cursor_clone(
        const SpaceOccurrenceCursor *src, SpaceOccurrenceCursor *dst) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    return space_occurrence_cursor_copy(src, dst, false);
}

/* Yield `row` as its logical index, unless the view's marks removed it. */
static bool space_occurrence_cursor_yield(
        SpaceOccurrenceCursor *cursor, const SpaceViewMarks *marks,
        CettaIndex row, CettaIndex *logical_index_out) {
    if (space_view_row_marked(marks, row))
        return false;
    cursor->has_last = true;
    cursor->last_row = row;
    cursor->last_logical = space_view_logical(marks, row);
    if (logical_index_out)
        *logical_index_out = cursor->last_logical;
    return true;
}

/* Inlined into both readers, the atom's and the id's, as it was into the
 * one reader there was. */
static inline __attribute__((always_inline)) SpaceOccurrenceCursorStep
space_occurrence_cursor_next_row(
    SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out) {
    if (logical_index_out)
        *logical_index_out = 0u;
    if (!cursor || !cursor->space || !cursor->pinned ||
        !cursor->occurrences)
        return SPACE_OCCURRENCE_CURSOR_INVALIDATED;
    const SpacePinnedOccurrences *occurrences = cursor->occurrences;
    if (occurrences->space &&
        (cursor->read.instance_id != space_instance_id(occurrences->space) ||
         cursor->prefix_epoch != occurrences->space->prefix_epoch))
        return SPACE_OCCURRENCE_CURSOR_INVALIDATED;
    CettaCount len = space_pinned_occurrences_len(occurrences);
    SpaceViewMarks marks = space_view_marks(occurrences);
    if (cursor->head_mode) {
        CettaIndex row = 0u;
        CettaIndex ceiling = cursor->ceiling < len ? cursor->ceiling : len;
        while (space_head_index_next(&cursor->head, ceiling,
                                     marks.marked ? marks.marks : NULL,
                                     marks.rows, &row)) {
            if (space_occurrence_cursor_yield(cursor, &marks, row,
                                              logical_index_out))
                return SPACE_OCCURRENCE_CURSOR_ITEM;
        }
        return SPACE_OCCURRENCE_CURSOR_END;
    }
    if (cursor->full_scan) {
        while (cursor->full_next < cursor->ceiling &&
               cursor->full_next < len) {
            if (space_occurrence_cursor_yield(cursor, &marks,
                    cursor->full_next++, logical_index_out))
                return SPACE_OCCURRENCE_CURSOR_ITEM;
        }
        return SPACE_OCCURRENCE_CURSOR_END;
    }
    if (cursor->flat_mode) {
        while (cursor->flat_next < cursor->flat_len) {
            if (space_occurrence_cursor_yield(cursor, &marks,
                    cursor->flat[cursor->flat_next++], logical_index_out))
                return SPACE_OCCURRENCE_CURSOR_ITEM;
        }
        return SPACE_OCCURRENCE_CURSOR_END;
    }
    if (cursor->heap) {
        CettaIndex index;
        while (space_occurrence_cursor_heap_next(cursor, len, &index)) {
            if (space_occurrence_cursor_yield(cursor, &marks, index,
                                              logical_index_out))
                return SPACE_OCCURRENCE_CURSOR_ITEM;
        }
        return SPACE_OCCURRENCE_CURSOR_END;
    }
    /* A space answers a bag: unless the space keeps an order, its nodes
     * are read one after another, each in row order. */
    if (!cursor->row_order) {
        while (cursor->node_next < cursor->node_len) {
            uint32_t i = cursor->node_next;
            DiscNode *node = cursor->nodes[i];
            if (!node || cursor->leaf_pos[i] >= node->leaf_count) {
                cursor->node_next++;
                continue;
            }
            CettaIndex index = disc_leaf_at(node, cursor->leaf_pos[i]++);
            if (index >= cursor->ceiling) {
                cursor->leaf_pos[i] = node->leaf_count;
                continue;
            }
            if (index < len &&
                space_occurrence_cursor_yield(cursor, &marks, index,
                                              logical_index_out))
                return SPACE_OCCURRENCE_CURSOR_ITEM;
        }
        return SPACE_OCCURRENCE_CURSOR_END;
    }
    for (;;) {
        CettaIndex best = UINT64_MAX;
        bool found = false;
        for (uint32_t i = 0u; i < cursor->node_len; i++) {
            DiscNode *node = cursor->nodes[i];
            if (!node)
                continue;
            while (cursor->leaf_pos[i] < node->leaf_count) {
                CettaIndex index = disc_leaf_at(node, cursor->leaf_pos[i]);
                if (index >= cursor->ceiling) {
                    cursor->leaf_pos[i] = node->leaf_count;
                    break;
                }
                if (!found || index < best) {
                    best = index;
                    found = true;
                }
                break;
            }
        }
        if (!found)
            return SPACE_OCCURRENCE_CURSOR_END;
        for (uint32_t i = 0u; i < cursor->node_len; i++) {
            DiscNode *node = cursor->nodes[i];
            if (!node || cursor->leaf_pos[i] >= node->leaf_count)
                continue;
            if (disc_leaf_at(node, cursor->leaf_pos[i]) == best)
                cursor->leaf_pos[i]++;
        }
        if (best >= len ||
            !space_occurrence_cursor_yield(cursor, &marks, best,
                                           logical_index_out))
            continue;
        return SPACE_OCCURRENCE_CURSOR_ITEM;
    }
}

/* Physical rows belong to the same pinned view on either side of detach.
 * A selected row needs no second logical-to-physical index lookup. */
static AtomId space_pinned_occurrences_id_at_row(
        const SpacePinnedOccurrences *occurrences, CettaIndex row) {
    const SpaceNativeStorage *live = occurrences->space
        ? &occurrences->space->native : NULL;
    uint8_t bits = live ? live->atom_id_width_bits : occurrences->atom_id_width_bits;
    CettaCount length = live ? live->len : occurrences->len;
    const uint8_t *storage = live ? live->atom_ids : occurrences->atom_ids;
    size_t width = cetta_atom_id_storage_width_bytes_from_bits(bits);
    if (row >= length || !storage || width == 0u)
        return CETTA_ATOM_ID_NONE;
    return cetta_atom_id_storage_load_bits(
        storage + (size_t)row * width, bits);
}

static TermUniverse *space_pinned_occurrences_universe(
        const SpacePinnedOccurrences *occurrences) {
    return occurrences->space ? occurrences->space->native.universe
                              : occurrences->universe;
}

static Atom *space_pinned_occurrences_atom_at_row(
        const SpacePinnedOccurrences *occurrences, CettaIndex row) {
    AtomId atom_id = space_pinned_occurrences_id_at_row(occurrences, row);
    return atom_id != CETTA_ATOM_ID_NONE
        ? term_universe_get_atom(
              space_pinned_occurrences_universe(occurrences), atom_id)
        : NULL;
}

/* The next row, then the next fact the relation kept. */
static inline __attribute__((always_inline)) SpaceOccurrenceCursorStep
space_occurrence_cursor_next_locked(
    SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out) {
    if (!cursor || !cursor->kept_turn) {
        SpaceOccurrenceCursorStep step =
            space_occurrence_cursor_next_row(cursor, logical_index_out);
        if (step != SPACE_OCCURRENCE_CURSOR_END || !cursor->kept)
            return step;
        cursor->kept_turn = true;
        cursor->has_last = false;
    }
    if (cursor->kept_next >= cursor->kept->len)
        return SPACE_OCCURRENCE_CURSOR_END;
    cursor->kept_last = cursor->kept_next++;
    if (logical_index_out)
        *logical_index_out = cursor->ceiling + (CettaIndex)cursor->kept_last;
    return SPACE_OCCURRENCE_CURSOR_ITEM;
}

SpaceOccurrenceCursorStep space_occurrence_cursor_next(
        SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out, Atom **atom_out) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    if (atom_out)
        *atom_out = NULL;
    SpaceOccurrenceCursorStep step =
        space_occurrence_cursor_next_locked(cursor, logical_index_out);
    if (step == SPACE_OCCURRENCE_CURSOR_ITEM && atom_out) {
        if (cursor->kept_turn) {
            AtomId id = space_kept_occurrences_id(cursor->kept,
                                                  cursor->kept_last);
            *atom_out = id != CETTA_ATOM_ID_NONE
                ? term_universe_get_atom(cursor->kept->universe, id) : NULL;
        } else {
            *atom_out = space_pinned_occurrences_atom_at_row(
                cursor->occurrences, cursor->last_row);
        }
    }
    return step;
}

SpaceOccurrenceCursorStep space_occurrence_cursor_next_id(
        SpaceOccurrenceCursor *cursor, CettaIndex *logical_index_out,
        AtomId *id_out) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    if (id_out)
        *id_out = CETTA_ATOM_ID_NONE;
    SpaceOccurrenceCursorStep step =
        space_occurrence_cursor_next_locked(cursor, logical_index_out);
    if (step == SPACE_OCCURRENCE_CURSOR_ITEM && id_out)
        *id_out = cursor->kept_turn
            ? space_kept_occurrences_id(cursor->kept, cursor->kept_last)
            : space_pinned_occurrences_id_at_row(cursor->occurrences,
                                                 cursor->last_row);
    return step;
}

TermUniverse *space_occurrence_cursor_universe(
        const SpaceOccurrenceCursor *cursor) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    return cursor && cursor->occurrences
        ? space_pinned_occurrences_universe(cursor->occurrences) : NULL;
}

Atom *space_occurrence_cursor_atom(const SpaceOccurrenceCursor *cursor,
                                   CettaIndex index) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    const SpacePinnedOccurrences *occurrences =
        cursor ? cursor->occurrences : NULL;
    if (!occurrences)
        return NULL;
    /* Past the ceiling are the facts the relation kept. */
    if (cursor->kept && index >= cursor->ceiling) {
        size_t i = (size_t)(index - cursor->ceiling);
        AtomId id = i < cursor->kept->len
            ? space_kept_occurrences_id(cursor->kept, i) : CETTA_ATOM_ID_NONE;
        return id != CETTA_ATOM_ID_NONE
            ? term_universe_get_atom(cursor->kept->universe, id) : NULL;
    }
    if (cursor->has_last && cursor->last_logical == index)
        return space_pinned_occurrences_atom_at_row(occurrences, cursor->last_row);
    if (occurrences->space)
        return space_get_at64(occurrences->space, index);
    /* The detached copy holds the rows themselves, marked ones too. */
    SpaceViewMarks marks = space_view_marks(occurrences);
    return space_pinned_occurrences_atom_at_row(
        occurrences, space_view_row(&marks, index));
}

void space_occurrence_cursor_release(SpaceOccurrenceCursor *cursor) {
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    if (!cursor)
        return;
    if (cursor->pinned)
        space_pinned_occurrences_release(cursor->occurrences);
    space_kept_occurrences_release(cursor->kept);
    free(cursor->nodes);
    free(cursor->leaf_pos);
    free(cursor->flat);
    free(cursor->heap);
    space_occurrence_cursor_init_empty(cursor);
}

void disc_lookup_expression_coordinates(
        DiscNode *root, Atom *const *coordinates,
        CettaExprLen coordinate_count, CettaIndex **out,
        CettaIndex *nout, CettaIndex *cout) {
    *out = NULL;
    *nout = 0u;
    *cout = 0u;
    DiscNodeSet final;
    dns_init(&final);
    disc_step_expression_coordinates(
        root, coordinates, coordinate_count, &final);
    dns_unique(&final);
    disc_collect_leaves(&final, out, nout, cout);
    dns_free(&final);
}

static bool disc_follow_rigid_exact_path(
    const DiscNode *node, const Atom *query,
    const DiscNode **out_continuation);

static bool disc_follow_rigid_exact_expression_coordinates(
        const DiscNode *node, Atom *const *coordinates,
        CettaExprLen coordinate_count,
        const DiscNode **out_continuation) {
    if (out_continuation)
        *out_continuation = NULL;
    if (!node || !out_continuation ||
        disc_find_variable_branch(node) ||
        (coordinate_count > 0u && !coordinates)) {
        return false;
    }
    const DiscNode *continuation = disc_find_expression_branch(
        node, coordinate_count);
    if (!continuation)
        return true;
    for (CettaExprIndex i = 0u; i < coordinate_count; i++) {
        const DiscNode *next = NULL;
        if (!disc_follow_rigid_exact_path(
                continuation, coordinates[i], &next)) {
            return false;
        }
        if (!next)
            return true;
        continuation = next;
    }
    *out_continuation = continuation;
    return true;
}

/* Follow the unique rigid encoding used by disc_insert_atom.  NULL in
 * out_continuation denotes a proven empty path; false denotes a query for
 * which the trie cannot by itself account for every matching occurrence. */
static bool disc_follow_rigid_exact_path(
        const DiscNode *node, const Atom *query,
        const DiscNode **out_continuation) {
    if (out_continuation)
        *out_continuation = NULL;
    if (!node || !query || !out_continuation ||
        disc_find_variable_branch(node))
        return false;

    switch (query->kind) {
    case ATOM_SYMBOL:
        *out_continuation = disc_find_symbol_branch(
            node, query->sym_id);
        return true;
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        if (query->ground.gkind != GV_INT)
            return false;
        *out_continuation = disc_find_integer_branch(
            node, query->ground.ival);
        return true;
    case ATOM_EXPR: {
        return disc_follow_rigid_exact_expression_coordinates(
            node, query->expr.elems, query->expr.len,
            out_continuation);
    }
    }
    return false;
}

bool disc_count_rigid_exact_expression_coordinates(
        const DiscNode *root, Atom *const *coordinates,
        CettaExprLen coordinate_count, CettaIndex *out_count) {
    if (root && disc_root_pool((DiscNode *)root)->approximate)
        return false;
    if (out_count)
        *out_count = 0u;
    if (!root || !out_count ||
        (coordinate_count > 0u && !coordinates)) {
        return false;
    }
    const DiscNode *leaf = NULL;
    if (!disc_follow_rigid_exact_expression_coordinates(
            root, coordinates, coordinate_count, &leaf)) {
        return false;
    }
    *out_count = leaf ? leaf->leaf_count : 0u;
    return true;
}

typedef struct {
    const DiscNode *node;
    CettaExprLen steps;
    uint64_t sym_index, int_index, str_index;
    bool variable_done;
} DiscWildcardFoldFrame;

static const DiscNode *disc_wildcard_fold_next(
        DiscWildcardFoldFrame *frame, const DiscBranchSet *branches) {
    uint64_t sym_cap = branches->sym_hashed
        ? (uint64_t)branches->sym_ht.mask + 1u : branches->nsym;
    while (frame->sym_index < sym_cap) {
        uint64_t index = frame->sym_index++;
        if (branches->sym_hashed) {
            if (branches->sym_ht.entries[index].key != SYMBOL_ID_NONE)
                return branches->sym_ht.entries[index].child;
        } else {
            return branches->sym[index].child;
        }
    }
    if (!frame->variable_done) {
        frame->variable_done = true;
        if (branches->var_child)
            return branches->var_child;
    }
    uint64_t int_cap = branches->ints.hashed
        ? (uint64_t)branches->ints.ht.mask + 1u : branches->ints.count;
    while (frame->int_index < int_cap) {
        uint64_t index = frame->int_index++;
        const DiscNode *child = branches->ints.hashed
            ? branches->ints.ht.entries[index].child
            : branches->ints.list[index].child;
        if (child)
            return child;
    }
    uint64_t str_cap = branches->strs.hashed
        ? (uint64_t)branches->strs.ht.mask + 1u : branches->strs.count;
    while (frame->str_index < str_cap) {
        uint64_t index = frame->str_index++;
        const DiscNode *child = branches->strs.hashed
            ? branches->strs.ht.entries[index].child
            : branches->strs.list[index].child;
        if (child)
            return child;
    }
    return NULL;
}

/* The count observer needs multiplicities, not candidate coordinates. Keep
 * only the unfinished branch walks; a singleton path advances in place. */
static bool disc_sum_wildcard_leaves(
        const DiscNode *node, CettaExprLen steps, uint64_t *sum) {
    if (!node || !sum)
        return false;
    DiscWildcardFoldFrame inline_frames[8];
    DiscWildcardFoldFrame *frames = inline_frames;
    size_t len = 1u, capacity = 8u;
    frames[0] = (DiscWildcardFoldFrame){.node = node, .steps = steps};
    bool supported = true;
    while (len) {
        DiscWildcardFoldFrame *frame = &frames[len - 1u];
        node = frame->node;
        if (!node) {
            supported = false;
            break;
        }
        if (!frame->steps) {
            if (*sum > UINT64_MAX - node->leaf_count) {
                supported = false;
                break;
            }
            *sum += node->leaf_count;
            len--;
            continue;
        }
        if (node->edge_kind == DISC_EDGE_EMPTY) {
            len--;
            continue;
        }
        if (node->edge_kind == DISC_EDGE_EXPRESSION) {
            supported = false;
            break;
        }
        if (node->edge_kind != DISC_EDGE_MANY) {
            frame->node = node->edge.one.child;
            frame->steps--;
            continue;
        }
        const DiscBranchSet *branches = node->edge.many;
        if (!branches || branches->nexpr) {
            supported = false;
            break;
        }
        const DiscNode *child = disc_wildcard_fold_next(frame, branches);
        if (!child) {
            len--;
            continue;
        }
        CettaExprLen child_steps = frame->steps - 1u;
        if (len == capacity) {
            if (capacity > SIZE_MAX / sizeof(*frames) / 2u) {
                supported = false;
                break;
            }
            capacity *= 2u;
            if (frames == inline_frames) {
                frames = cetta_malloc(capacity * sizeof(*frames));
                memcpy(frames, inline_frames, len * sizeof(*frames));
            } else {
                frames = cetta_realloc(frames, capacity * sizeof(*frames));
            }
        }
        frames[len++] = (DiscWildcardFoldFrame){
            .node = child, .steps = child_steps};
    }
    if (frames != inline_frames)
        free(frames);
    return supported;
}

/* A free tail is a run of distinct variables.  A repeated variable is an
 * equality constraint, and summing every leaf under that tail would drop it. */
static bool disc_tail_is_free_linear(
    const Atom *pattern, CettaExprIndex index,
    const VarId *bind_ids, size_t bind_len) {
    for (CettaExprIndex rest = index; rest < pattern->expr.len; rest++) {
        const Atom *later = pattern->expr.elems[rest];
        if (!later || later->kind != ATOM_VAR)
            return false;
        for (size_t bind = 0u; bind < bind_len; bind++) {
            if (bind_ids && var_id_eq(bind_ids[bind], later->var_id))
                return false;
        }
        for (CettaExprIndex earlier = 0u; earlier < rest; earlier++) {
            const Atom *previous = pattern->expr.elems[earlier];
            if (previous && previous->kind == ATOM_VAR &&
                var_id_eq(previous->var_id, later->var_id))
                return false;
        }
    }
    return true;
}

static bool disc_i128_add(__int128 left, __int128 right, __int128 *out) {
    const __int128 limit =
        (((__int128)INT64_MAX) << 64) | (__int128)UINT64_MAX;
    const __int128 floor = -limit - 1;
    if (right > 0 && left > limit - right)
        return false;
    if (right < 0 && left < floor - right)
        return false;
    *out = left + right;
    return true;
}

static bool disc_project_node_column(
    Space *space, const DiscNode *node, size_t column,
    uint64_t *count, __int128 *sum, int64_t *product, bool *product_ok) {
    if (!node)
        return true;
    for (CettaIndex leaf = 0u; leaf < node->leaf_count; leaf++) {
        Atom *fact = space_get_at64(space, disc_leaf_at(node, leaf));
        if (!fact || fact->kind != ATOM_EXPR ||
            column >= (size_t)fact->expr.len || atom_has_vars(fact))
            return false;
        Atom *cell = fact->expr.elems[column];
        if (!cell || cell->kind != ATOM_GROUNDED ||
            cell->ground.gkind != GV_INT)
            return false;
        __int128 next_sum = 0;
        if (!disc_i128_add(*sum, (__int128)cell->ground.ival, &next_sum))
            return false;
        *sum = next_sum;
        if (*count == UINT64_MAX)
            return false;
        (*count)++;
        if (*product_ok) {
            __int128 next = (__int128)(*product) * (__int128)cell->ground.ival;
            if (next > (__int128)INT64_MAX || next < (__int128)INT64_MIN)
                *product_ok = false;
            else
                *product = (int64_t)next;
        }
    }
    return true;
}

static bool disc_project_depth_column(
    Space *space, const DiscNode *node, unsigned steps, size_t column,
    uint64_t *count, __int128 *sum, int64_t *product, bool *product_ok) {
    if (!node)
        return true;
    if (steps == 0u)
        return disc_project_node_column(
            space, node, column, count, sum, product, product_ok);
    if (node->edge_kind == DISC_EDGE_EMPTY)
        return true;
    if (node->edge_kind == DISC_EDGE_EXPRESSION)
        return false;
    if (node->edge_kind != DISC_EDGE_MANY) {
        return disc_project_depth_column(
            space, node->edge.one.child, steps - 1u, column,
            count, sum, product, product_ok);
    }
    const DiscBranchSet *branches = node->edge.many;
    if (!branches || branches->nexpr != 0u || branches->var_child)
        return false;
    if (branches->sym_hashed) {
        uint32_t cap = branches->sym_ht.mask + 1u;
        for (uint32_t index = 0u; index < cap; index++) {
            if (branches->sym_ht.entries[index].key == SYMBOL_ID_NONE)
                continue;
            if (!disc_project_depth_column(
                    space, branches->sym_ht.entries[index].child,
                    steps - 1u, column, count, sum, product, product_ok))
                return false;
        }
    } else {
        for (uint32_t index = 0u; index < branches->nsym; index++) {
            if (!disc_project_depth_column(
                    space, branches->sym[index].child, steps - 1u, column,
                    count, sum, product, product_ok))
                return false;
        }
    }
    const DiscScalarBranches *scalars[] = {&branches->ints, &branches->strs};
    for (size_t kind = 0u; kind < 2u; kind++) {
        const DiscScalarBranches *each = scalars[kind];
        uint32_t cap = each->hashed ? each->ht.mask + 1u : each->count;
        for (uint32_t index = 0u; index < cap; index++) {
            const DiscNode *child = each->hashed
                ? each->ht.entries[index].child : each->list[index].child;
            if (child && !disc_project_depth_column(
                    space, child, steps - 1u, column, count, sum, product,
                    product_ok))
                return false;
        }
    }
    return true;
}

bool disc_project_flat_bound_column(
    Space *space, const DiscNode *root, const Atom *pattern,
    const VarId *bind_ids, Atom *const *bind_values, size_t bind_len,
    size_t column, uint64_t *count_out, __int128 *sum_out,
    int64_t *product_out, bool *product_ok) {
    if (root && disc_root_pool((DiscNode *)root)->approximate)
        return false;
    if (count_out)
        *count_out = 0u;
    if (sum_out)
        *sum_out = 0;
    if (product_out)
        *product_out = 1;
    if (product_ok)
        *product_ok = true;
    if (!space || !root || !pattern || !count_out || !sum_out ||
        !product_out || !product_ok ||
        pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
        column >= (size_t)pattern->expr.len ||
        (bind_len > 0u && (!bind_ids || !bind_values)))
        return false;
    const DiscNode *node = disc_find_expression_branch(
        root, pattern->expr.len);
    if (!node)
        return true;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        const Atom *item = pattern->expr.elems[index];
        const Atom *ground = item;
        if (!item)
            return false;
        if (item->kind == ATOM_VAR) {
            ground = NULL;
            for (size_t bind = 0u; bind < bind_len; bind++) {
                if (var_id_eq(bind_ids[bind], item->var_id)) {
                    ground = bind_values[bind];
                    break;
                }
            }
        }
        if (!ground || ground->kind == ATOM_VAR) {
            if (!disc_tail_is_free_linear(
                    pattern, index, bind_ids, bind_len))
                return false;
            return disc_project_depth_column(
                space, node, (unsigned)(pattern->expr.len - index), column,
                count_out, sum_out, product_out, product_ok);
        }
        if (disc_find_variable_branch(node))
            return false;
        if (ground->kind == ATOM_SYMBOL)
            node = disc_find_symbol_branch(node, ground->sym_id);
        else if (ground->kind == ATOM_GROUNDED &&
                 ground->ground.gkind == GV_INT)
            node = disc_find_integer_branch(node, ground->ground.ival);
        else
            return false;
        if (!node)
            return true;
    }
    return disc_project_node_column(
        space, node, column, count_out, sum_out, product_out, product_ok);
}

bool disc_count_flat_bound_vars(
    const DiscNode *root, const Atom *pattern,
    const VarId *bind_ids, Atom *const *bind_values, size_t bind_len,
    CettaIndex *out_count) {
    if (root && disc_root_pool((DiscNode *)root)->approximate)
        return false;
    if (out_count)
        *out_count = 0u;
    if (!root || !pattern || !out_count ||
        pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
        (bind_len > 0u && (!bind_ids || !bind_values)) ||
        disc_find_variable_branch(root))
        return false;
    const DiscNode *node = disc_find_expression_branch(
        root, pattern->expr.len);
    if (!node)
        return true;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        const Atom *item = pattern->expr.elems[index];
        const Atom *ground = item;
        if (!item)
            return false;
        if (item->kind == ATOM_VAR) {
            ground = NULL;
            for (size_t bind = 0u; bind < bind_len; bind++) {
                if (var_id_eq(bind_ids[bind], item->var_id)) {
                    ground = bind_values[bind];
                    break;
                }
            }
        }
        if (!ground || ground->kind == ATOM_VAR) {
            if (!disc_tail_is_free_linear(
                    pattern, index, bind_ids, bind_len))
                return false;
            uint64_t sum = 0u;
            if (!disc_sum_wildcard_leaves(
                    node,
                    pattern->expr.len - index, &sum))
                return false;
            *out_count = (CettaIndex)sum;
            return true;
        }
        if (disc_find_variable_branch(node))
            return false;
        if (ground->kind == ATOM_SYMBOL)
            node = disc_find_symbol_branch(node, ground->sym_id);
        else if (ground->kind == ATOM_GROUNDED &&
                 ground->ground.gkind == GV_INT)
            node = disc_find_integer_branch(node, ground->ground.ival);
        else
            return false;
        if (!node)
            return true;
    }
    *out_count = node->leaf_count;
    return true;
}

bool disc_count_flat_trailing_wildcards(
    const DiscNode *root, const Atom *pattern, CettaIndex *out_count) {
    if (!pattern || pattern->kind != ATOM_EXPR) {
        if (out_count)
            *out_count = 0u;
        return false;
    }
    return disc_count_flat_expression_coordinates(
        root, pattern->expr.elems, pattern->expr.len, out_count);
}

bool disc_count_flat_expression_coordinates(
        const DiscNode *root, Atom *const *coordinates,
        CettaExprLen coordinate_count, CettaIndex *out_count) {
    if (root && disc_root_pool((DiscNode *)root)->approximate)
        return false;
    if (out_count)
        *out_count = 0u;
    if (!root || !coordinates || !out_count || coordinate_count == 0u ||
        disc_find_variable_branch(root))
        return false;
    CettaExprIndex first_var = coordinate_count;
    for (CettaExprIndex index = 0u; index < coordinate_count; index++) {
        const Atom *item = coordinates[index];
        if (!item)
            return false;
        if (item->kind == ATOM_VAR) {
            first_var = index;
            break;
        }
        if (item->kind == ATOM_SYMBOL)
            continue;
        if (item->kind == ATOM_GROUNDED &&
            item->ground.gkind == GV_INT)
            continue;
        return false;
    }
    for (CettaExprIndex index = first_var;
         index < coordinate_count; index++) {
        if (!coordinates[index] || coordinates[index]->kind != ATOM_VAR)
            return false;
        for (CettaExprIndex earlier = first_var; earlier < index; earlier++) {
            if (var_id_eq(coordinates[earlier]->var_id, coordinates[index]->var_id))
                return false;
        }
    }
    const DiscNode *node = disc_find_expression_branch(
        root, coordinate_count);
    if (!node)
        return true;
    for (CettaExprIndex index = 0u; index < first_var; index++) {
        if (disc_find_variable_branch(node))
            return false;
        const Atom *item = coordinates[index];
        if (item->kind == ATOM_SYMBOL)
            node = disc_find_symbol_branch(node, item->sym_id);
        else
            node = disc_find_integer_branch(node, item->ground.ival);
        if (!node)
            return true;
    }
    uint64_t sum = 0u;
    CettaExprLen wildcards = coordinate_count - first_var;
    if (!disc_sum_wildcard_leaves(node, wildcards, &sum))
        return false;
    *out_count = (CettaIndex)sum;
    return true;
}

bool disc_count_rigid_exact_path(
        const DiscNode *root, const Atom *query,
        CettaIndex *out_count) {
    if (root && disc_root_pool((DiscNode *)root)->approximate)
        return false;
    if (out_count)
        *out_count = 0u;
    if (!root || !query || !out_count)
        return false;
    const DiscNode *leaf = NULL;
    if (!disc_follow_rigid_exact_path(root, query, &leaf))
        return false;
    *out_count = leaf ? leaf->leaf_count : 0u;
    return true;
}

/* ── Equation Index ─────────────────────────────────────────────────────── */

static bool __attribute__((unused)) atom_is_eq_subst_safe(Atom *atom);
static bool atom_id_is_eq_subst_safe(const Space *s, AtomId atom_id);
static SymbolId eq_head_symbol(Atom *lhs);
static SymbolId eq_head_symbol_id(const Space *s, AtomId lhs_id);

static void eq_bucket_init(EqBucket *b) {
    b->atom_indices = NULL;
    b->atom_ids = NULL;
    b->len = 0;
    b->cap = 0;
    b->trie = NULL;
    stree_bucket_init(&b->subst);
    b->head = SYMBOL_ID_NONE;
    b->mixed_heads = false;
    b->subst_safe = true;
}

static void eq_bucket_note_head(EqBucket *b, SymbolId head) {
    if (!b || head == SYMBOL_ID_NONE)
        return;
    if (b->head == SYMBOL_ID_NONE) {
        b->head = head;
    } else if (b->head != head) {
        b->mixed_heads = true;
    }
}

static void eq_bucket_add(EqBucket *b, Atom *lhs, CettaIndex atom_idx,
                          AtomId equation_id) {
    CettaIndex idx = b->len;
    SymbolId head = eq_head_symbol(lhs);
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(CettaIndex) * b->cap);
        b->atom_ids = cetta_realloc(b->atom_ids, sizeof(AtomId) * b->cap);
    }
    b->atom_indices[b->len] = atom_idx;
    b->atom_ids[b->len] = equation_id;
    b->len++;
    eq_bucket_note_head(b, head);
    /* Add to discrimination trie */
    if (!b->trie) b->trie = disc_node_new();
    disc_insert(b->trie, lhs, idx);
    stree_bucket_insert(&b->subst, lhs, idx);
    b->subst_safe = b->subst_safe && atom_is_eq_subst_safe(lhs);
}

static void eq_bucket_add_id(EqBucket *b, const Space *s, AtomId lhs_id,
                             CettaIndex atom_idx, AtomId equation_id) {
    SymbolId head = eq_head_symbol_id(s, lhs_id);
    if (!b || !s || !s->native.universe || lhs_id == CETTA_ATOM_ID_NONE ||
        !tu_hdr(s->native.universe, lhs_id)) {
        Atom *lhs = (s && s->native.universe)
                        ? term_universe_get_atom(s->native.universe, lhs_id)
                        : NULL;
        if (lhs)
            eq_bucket_add(b, lhs, atom_idx, equation_id);
        return;
    }
    CettaIndex idx = b->len;
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(CettaIndex) * b->cap);
        b->atom_ids = cetta_realloc(b->atom_ids, sizeof(AtomId) * b->cap);
    }
    b->atom_indices[b->len] = atom_idx;
    b->atom_ids[b->len] = equation_id;
    b->len++;
    eq_bucket_note_head(b, head);
    if (!b->trie)
        b->trie = disc_node_new();
    if (!disc_insert_id(b->trie, s->native.universe, lhs_id, idx)) {
        Atom *lhs = term_universe_get_atom(s->native.universe, lhs_id);
        if (lhs)
            disc_insert(b->trie, lhs, idx);
    }
    if (!stree_bucket_insert_id(&b->subst, s->native.universe, lhs_id, idx)) {
        Atom *lhs = term_universe_get_atom(s->native.universe, lhs_id);
        if (lhs)
            stree_bucket_insert(&b->subst, lhs, idx);
    }
    b->subst_safe = b->subst_safe && atom_id_is_eq_subst_safe(s, lhs_id);
}

static void eq_bucket_free(EqBucket *b) {
    free(b->atom_indices);
    free(b->atom_ids);
    disc_node_free(b->trie); b->trie = NULL;
    stree_bucket_free(&b->subst);
    b->atom_indices = NULL;
    b->atom_ids = NULL;
    b->len = 0;
    b->cap = 0;
    b->head = SYMBOL_ID_NONE;
    b->mixed_heads = false;
    b->subst_safe = true;
}

static uint32_t symbol_hash(SymbolId id) {
    /*
     * Bucket placement must be a function of the symbol, not of when it was
     * interned.  Otherwise loading an unrelated language first renumbers
     * SymbolIds and changes equation/type-index collision work in every other
     * language.  The symbol table already records a spelling-stable 64-bit
     * hash; avalanche its folded value only at the bucket boundary.
     */
    uint64_t stable = symbol_hash_value(g_symbols, id);
    uint32_t mixed = (uint32_t)stable ^ (uint32_t)(stable >> 32);
    mixed ^= mixed >> 16;
    mixed *= 0x85ebca6bu;
    mixed ^= mixed >> 13;
    mixed *= 0xc2b2ae35u;
    mixed ^= mixed >> 16;
    return mixed % EQ_INDEX_BUCKETS;
}

static bool atom_is_eq_subst_safe_within(Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        return atom->ground.gkind == GV_INT ||
               atom->ground.gkind == GV_BIGINT ||
               atom->ground.gkind == GV_RATIONAL ||
               atom->ground.gkind == GV_STRING;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!atom_is_eq_subst_safe_within(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool __attribute__((unused)) atom_is_eq_subst_safe(Atom *atom) {
    return stree_query_within_expansion_budget(atom) && atom_is_eq_subst_safe_within(atom);
}

static bool atom_id_is_eq_subst_safe_within(const Space *s, AtomId atom_id,
        unsigned depth, CettaIndexExpansionBudget *budget) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
    if (!hdr) {
        Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
        return atom ? atom_is_eq_subst_safe(atom) : false;
    }
    if (tu_kind(s->native.universe, atom_id) == ATOM_EXPR &&
        !cetta_index_expand_expression(budget, depth, tu_arity(s->native.universe, atom_id)))
        return false;
    switch (tu_kind(s->native.universe, atom_id)) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        return tu_ground_kind(s->native.universe, atom_id) == GV_INT ||
               tu_ground_kind(s->native.universe, atom_id) == GV_BIGINT ||
               tu_ground_kind(s->native.universe, atom_id) == GV_RATIONAL ||
               tu_ground_kind(s->native.universe, atom_id) == GV_STRING;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < tu_arity(s->native.universe, atom_id); i++) {
            if (!atom_id_is_eq_subst_safe_within(s, tu_child(s->native.universe, atom_id, i),
                                                    depth + 1u, budget))
                return false;
        }
        return true;
    }
    return false;
}

static bool atom_id_is_eq_subst_safe(const Space *s, AtomId atom_id) {
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    return atom_id_is_eq_subst_safe_within(s, atom_id, 0u, &budget);
}

/* Get the head symbol of an equation LHS for indexing.
   Returns NULL if the head is not a symbol (variable, complex expr). */
static SymbolId eq_head_symbol(Atom *lhs) {
    if (lhs->kind == ATOM_SYMBOL) return lhs->sym_id;
    if (lhs->kind == ATOM_EXPR && lhs->expr.len > 0 &&
        lhs->expr.elems[0]->kind == ATOM_SYMBOL)
        return lhs->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

/* A known symbol-headed call can match an exact equation head or a genuinely
 * open head variable.  A structured callable head such as `((closure x) y)`
 * cannot match `(named y)` and must not become a wildcard candidate for every
 * named call merely because both lack a top-level head symbol. */
static bool eq_lhs_may_match_known_head(Atom *lhs, SymbolId head) {
    if (!lhs || head == SYMBOL_ID_NONE)
        return false;
    SymbolId exact = eq_head_symbol(lhs);
    if (exact != SYMBOL_ID_NONE)
        return exact == head;
    if (lhs->kind == ATOM_VAR)
        return true;
    return lhs->kind == ATOM_EXPR && lhs->expr.len > 0u &&
           lhs->expr.elems[0]->kind == ATOM_VAR;
}

#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
static bool eq_lhs_has_structured_head(const Atom *lhs) {
    return lhs && lhs->kind == ATOM_EXPR && lhs->expr.len > 0u &&
           lhs->expr.elems[0]->kind != ATOM_SYMBOL &&
           lhs->expr.elems[0]->kind != ATOM_VAR;
}
#endif

static SymbolId eq_head_symbol_id(const Space *s, AtomId lhs_id) {
    if (!s || !s->native.universe || lhs_id == CETTA_ATOM_ID_NONE)
        return SYMBOL_ID_NONE;
    return tu_head_sym(s->native.universe, lhs_id);
}

/* Exact-head maps place a symbol by its id: a probe compares heads, so the
 * work does not depend on which other symbols collide. */
static uint32_t symbol_id_mix(SymbolId head) {
    uint32_t mixed = (uint32_t)head;
    mixed ^= mixed >> 16;
    mixed *= 0x7feb352du;
    mixed ^= mixed >> 15;
    mixed *= 0x846ca68bu;
    mixed ^= mixed >> 16;
    return mixed;
}

static void eq_head_set_init(EqHeadSet *set) {
    set->slots = NULL;
    set->len = 0u;
    set->cap = 0u;
}

static void eq_head_set_free(EqHeadSet *set) {
    free(set->slots);
    eq_head_set_init(set);
}

static bool eq_head_set_contains(const EqHeadSet *set, SymbolId head) {
    if (!set || !set->slots || set->cap == 0u || head == SYMBOL_ID_NONE)
        return false;
    CettaIndex mask = set->cap - 1u;
    CettaIndex slot = (CettaIndex)symbol_id_mix(head) & mask;
    for (;;) {
        SymbolId present = set->slots[slot];
        if (present == SYMBOL_ID_NONE)
            return false;
        if (present == head)
            return true;
        slot = (slot + 1u) & mask;
    }
}

static void eq_head_set_insert_raw(EqHeadSet *set, SymbolId head) {
    CettaIndex mask = set->cap - 1u;
    CettaIndex slot = (CettaIndex)symbol_id_mix(head) & mask;
    while (set->slots[slot] != SYMBOL_ID_NONE &&
           set->slots[slot] != head) {
        slot = (slot + 1u) & mask;
    }
    if (set->slots[slot] == head)
        return;
    set->slots[slot] = head;
    set->len++;
}

static void eq_head_set_reserve(EqHeadSet *set, CettaIndex capacity) {
    CettaIndex new_cap = set->cap ? set->cap : 16u;
    while (new_cap < capacity)
        new_cap *= 2u;
    if (new_cap == set->cap)
        return;

    SymbolId *old_slots = set->slots;
    CettaIndex old_cap = set->cap;
    set->slots = cetta_malloc(sizeof(*set->slots) * new_cap);
    set->cap = new_cap;
    set->len = 0u;
    for (CettaIndex i = 0u; i < new_cap; i++)
        set->slots[i] = SYMBOL_ID_NONE;
    for (CettaIndex i = 0u; i < old_cap; i++) {
        if (old_slots[i] != SYMBOL_ID_NONE)
            eq_head_set_insert_raw(set, old_slots[i]);
    }
    free(old_slots);
}

static void eq_head_set_add(EqHeadSet *set, SymbolId head) {
    if (!set || head == SYMBOL_ID_NONE || eq_head_set_contains(set, head))
        return;
    /* Keep linear-probing occupancy below 3/4. */
    if (set->cap == 0u || (set->len + 1u) * 4u >= set->cap * 3u)
        eq_head_set_reserve(set, set->cap ? set->cap * 2u : 16u);
    eq_head_set_insert_raw(set, head);
}

/* ── Head chains ────────────────────────────────────────────────────────── */

#define SPACE_HEAD_CHAIN_END UINT32_MAX

static void space_head_chains_init(SpaceHeadChains *rows) {
    *rows = (SpaceHeadChains){
        .headless = {
            .head = SYMBOL_ID_NONE,
            .first = SPACE_HEAD_CHAIN_END,
            .last = SPACE_HEAD_CHAIN_END,
        },
    };
}

static void space_head_chains_free(SpaceHeadChains *rows) {
    free(rows->atom_indices);
    free(rows->atom_ids);
    free(rows->next);
    free(rows->chains);
    space_head_chains_init(rows);
}

static const SpaceHeadChain *space_head_chains_find(const SpaceHeadChains *rows,
                                                    SymbolId head) {
    if (head == SYMBOL_ID_NONE)
        return &rows->headless;
    if (rows->chain_cap == 0u)
        return NULL;
    uint32_t mask = rows->chain_cap - 1u;
    for (uint32_t slot = symbol_id_mix(head) & mask;;
         slot = (slot + 1u) & mask) {
        const SpaceHeadChain *chain = &rows->chains[slot];
        if (chain->head == head)
            return chain;
        if (chain->head == SYMBOL_ID_NONE)
            return NULL;
    }
}

static SpaceHeadChain *space_head_chain_slot(SpaceHeadChain *chains,
                                             uint32_t cap, SymbolId head) {
    uint32_t mask = cap - 1u;
    uint32_t slot = symbol_id_mix(head) & mask;
    while (chains[slot].head != head && chains[slot].head != SYMBOL_ID_NONE)
        slot = (slot + 1u) & mask;
    return &chains[slot];
}

static SpaceHeadChain *space_head_chain_insert(SpaceHeadChains *rows,
                                               SymbolId head) {
    if (head == SYMBOL_ID_NONE)
        return &rows->headless;
    if (((uint64_t)rows->chain_len + 1u) * 4u > (uint64_t)rows->chain_cap * 3u) {
        if (rows->chain_cap > UINT32_MAX / 2u) {
            fputs("CeTTa: exhausted head-chain heads\n", stderr);
            abort();
        }
        uint32_t cap = rows->chain_cap ? rows->chain_cap * 2u : 64u;
        SpaceHeadChain *chains = cetta_malloc(sizeof(*chains) * cap);
        memset(chains, 0, sizeof(*chains) * cap);
        for (uint32_t i = 0u; i < rows->chain_cap; i++) {
            if (rows->chains[i].head != SYMBOL_ID_NONE)
                *space_head_chain_slot(chains, cap, rows->chains[i].head) =
                    rows->chains[i];
        }
        free(rows->chains);
        rows->chains = chains;
        rows->chain_cap = cap;
    }
    SpaceHeadChain *chain = space_head_chain_slot(rows->chains, rows->chain_cap, head);
    if (chain->head == SYMBOL_ID_NONE) {
        *chain = (SpaceHeadChain){
            .head = head, .first = SPACE_HEAD_CHAIN_END, .last = SPACE_HEAD_CHAIN_END,
        };
        rows->chain_len++;
    }
    return chain;
}

/* Rows arrive in logical order, so each chain is oldest first. */
static void space_head_chains_append(SpaceHeadChains *rows, SymbolId head,
                                     CettaIndex atom_idx, AtomId atom_id) {
    if (rows->len >= rows->cap) {
        if (rows->cap >= SPACE_HEAD_CHAIN_END / 2u) {
            fputs("CeTTa: exhausted head-chain rows\n", stderr);
            abort();
        }
        CettaIndex cap = rows->cap ? rows->cap * 2u : 16u;
        rows->atom_indices =
            cetta_realloc(rows->atom_indices, sizeof(CettaIndex) * cap);
        rows->atom_ids = cetta_realloc(rows->atom_ids, sizeof(AtomId) * cap);
        rows->next = cetta_realloc(rows->next, sizeof(uint32_t) * cap);
        rows->cap = cap;
    }
    uint32_t row = (uint32_t)rows->len++;
    rows->atom_indices[row] = atom_idx;
    rows->atom_ids[row] = atom_id;
    rows->next[row] = SPACE_HEAD_CHAIN_END;
    SpaceHeadChain *chain = space_head_chain_insert(rows, head);
    if (chain->last == SPACE_HEAD_CHAIN_END)
        chain->first = row;
    else
        rows->next[chain->last] = row;
    chain->last = row;
    chain->count++;
}

static void eq_index_init(EqIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        eq_bucket_init(&idx->buckets[i]);
    eq_bucket_init(&idx->wildcard);
    space_head_chains_init(&idx->head_rows);
    eq_head_set_init(&idx->directional_heads);
    idx->directional_wildcard = false;
    idx->directional_present = false;
}

static void eq_index_free(EqIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        eq_bucket_free(&idx->buckets[i]);
    eq_bucket_free(&idx->wildcard);
    space_head_chains_free(&idx->head_rows);
    eq_head_set_free(&idx->directional_heads);
}

static void eq_index_note_directional_row(EqIndex *idx, const Space *s,
                                          AtomId atom_id) {
    const TermUniverse *universe = s->native.universe;
    if (!universe || atom_id == CETTA_ATOM_ID_NONE) return;
    if (tu_hdr(universe, atom_id) &&
        (tu_kind(universe, atom_id) != ATOM_EXPR ||
         tu_arity(universe, atom_id) != 3u ||
         tu_head_sym(universe, atom_id) != g_builtin_syms.equals_percent))
        return;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    CettaRuleDescriptor rule;
    if (!cetta_rule_view(atom, CETTA_RULE_SYNTAX_DIRECTIONAL, &rule)) return;
    idx->directional_present = true;
    SymbolId head = eq_head_symbol(rule.head);
    if (head != SYMBOL_ID_NONE)
        eq_head_set_add(&idx->directional_heads, head);
    else
        idx->directional_wildcard = true;
}

static void eq_index_add(EqIndex *idx, Atom *lhs, CettaIndex atom_idx,
                         AtomId equation_id) {
    SymbolId head = eq_head_symbol(lhs);
    if (head != SYMBOL_ID_NONE) {
        space_head_chains_append(&idx->head_rows, head, atom_idx, equation_id);
        eq_bucket_add(&idx->buckets[symbol_hash(head)], lhs, atom_idx,
                      equation_id);
    } else {
        eq_bucket_add(&idx->wildcard, lhs, atom_idx, equation_id);
    }
}

static void eq_index_add_id(EqIndex *idx, const Space *s, AtomId lhs_id,
                            CettaIndex atom_idx, AtomId equation_id) {
    SymbolId head = eq_head_symbol_id(s, lhs_id);
    if (head != SYMBOL_ID_NONE) {
        space_head_chains_append(&idx->head_rows, head, atom_idx, equation_id);
        eq_bucket_add_id(&idx->buckets[symbol_hash(head)], s, lhs_id, atom_idx,
                         equation_id);
    } else {
        eq_bucket_add_id(&idx->wildcard, s, lhs_id, atom_idx, equation_id);
    }
}

/* ── Type Annotation Index ──────────────────────────────────────────────── */

/* A declaration subject's head: the symbol itself, or the symbol heading an
 * expression.  Other subjects are headless. */
static SymbolId ty_ann_subject_head(const Atom *subject) {
    if (!subject)
        return SYMBOL_ID_NONE;
    if (subject->kind == ATOM_SYMBOL)
        return subject->sym_id;
    if (subject->kind == ATOM_EXPR && subject->expr.len > 0 &&
        subject->expr.elems[0]->kind == ATOM_SYMBOL)
        return subject->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

static SymbolId ty_ann_subject_head_id(const Space *s, AtomId subject_id) {
    if (!s || !s->native.universe || subject_id == CETTA_ATOM_ID_NONE)
        return SYMBOL_ID_NONE;
    if (tu_hdr(s->native.universe, subject_id)) {
        if (tu_kind(s->native.universe, subject_id) == ATOM_SYMBOL)
            return tu_sym(s->native.universe, subject_id);
        if (tu_kind(s->native.universe, subject_id) == ATOM_EXPR &&
            tu_arity(s->native.universe, subject_id) > 0)
            return tu_head_sym(s->native.universe, subject_id);
        return SYMBOL_ID_NONE;
    }
    return ty_ann_subject_head(
        term_universe_get_atom(s->native.universe, subject_id));
}

static void ty_ann_index_init(TypeAnnIndex *idx) {
    space_head_chains_init(&idx->rows);
    idx->has_open_rows = false;
    idx->has_non_symbol_subjects = false;
}
static void ty_ann_index_free(TypeAnnIndex *idx) {
    space_head_chains_free(&idx->rows);
    ty_ann_index_init(idx);
}
static void ty_ann_index_add(TypeAnnIndex *idx, Atom *ann_atom,
                             CettaIndex atom_idx, AtomId annotation_id) {
    if (!ann_atom || ann_atom->kind != ATOM_SYMBOL)
        idx->has_non_symbol_subjects = true;
    if (!ann_atom || atom_structural_has_rational(ann_atom) ||
        (ann_atom->kind == ATOM_EXPR &&
         atom_petta_value_representation(ann_atom) != PETTA_VALUE_ORDINARY))
        idx->has_open_rows = true;
    space_head_chains_append(&idx->rows, ty_ann_subject_head(ann_atom),
                             atom_idx, annotation_id);
}

static void ty_ann_index_add_id(TypeAnnIndex *idx, const Space *s,
                                AtomId subject_id, CettaIndex atom_idx,
                                AtomId annotation_id) {
    if (!s || !s->native.universe || subject_id == CETTA_ATOM_ID_NONE ||
        tu_kind(s->native.universe, subject_id) != ATOM_SYMBOL)
        idx->has_non_symbol_subjects = true;
    if (!s || !s->native.universe || subject_id == CETTA_ATOM_ID_NONE) {
        idx->has_open_rows = true;
    } else if (tu_hdr(s->native.universe, subject_id)) {
        if (tu_kind(s->native.universe, subject_id) == ATOM_EXPR &&
            tu_petta_value_representation(s->native.universe, subject_id) !=
                PETTA_VALUE_ORDINARY)
            idx->has_open_rows = true;
    } else {
        Atom *subject = term_universe_get_atom(s->native.universe, subject_id);
        if (!subject || atom_structural_has_rational(subject) ||
            (subject->kind == ATOM_EXPR &&
             atom_petta_value_representation(subject) != PETTA_VALUE_ORDINARY))
            idx->has_open_rows = true;
    }
    space_head_chains_append(&idx->rows, ty_ann_subject_head_id(s, subject_id),
                             atom_idx, annotation_id);
}

static void ty_ann_index_note_row(TypeAnnIndex *idx, const Space *s,
                                   AtomId row_id) {
    if (idx->has_open_rows)
        return;
    const TermUniverse *universe = s->native.universe;
    const CettaTermHdr *header = tu_hdr(universe, row_id);
    if (header) {
        /* These structural tests share the already validated record header. */
        AtomKind kind = (AtomKind)header->tag;
        if (kind == ATOM_VAR ||
            (kind == ATOM_EXPR &&
             (header->aux32 & CETTA_TERM_HDR_DATA_MASK) != 0u &&
             (header->sym_or_head == SYMBOL_ID_NONE ||
              tu_petta_value_representation(universe, row_id) !=
                  PETTA_VALUE_ORDINARY)))
            idx->has_open_rows = true;
        return;
    }
    Atom *row = term_universe_get_atom(universe, row_id);
    if (!row || row->kind == ATOM_VAR || atom_structural_has_rational(row) ||
        (row->kind == ATOM_EXPR && row->expr.len != 0u &&
         (atom_head_symbol_id(row) == SYMBOL_ID_NONE ||
          atom_petta_value_representation(row) != PETTA_VALUE_ORDINARY)))
        idx->has_open_rows = true;
}

static void exact_index_init(ExactAtomIndex *idx) {
    *idx = (ExactAtomIndex){0};
}

static void exact_index_free(ExactAtomIndex *idx) {
    free(idx->slots);
    free(idx->inexact_heads);
    exact_index_init(idx);
}

static inline size_t exact_index_home(uint64_t key, size_t mask) {
    uint64_t h = key * UINT64_C(0x9E3779B97F4A7C15);
    return (size_t)(h ^ (h >> 32)) & mask;
}

static inline bool exact_slot_vacant(const ExactAtomIndex *idx, size_t at) {
    return idx->wide
        ? ((const ExactAtomSlot *)idx->slots)[at].id == CETTA_ATOM_ID_NONE
        : ((const ExactAtomNarrowSlot *)idx->slots)[at].id == UINT32_MAX;
}

static inline AtomId exact_slot_id(const ExactAtomIndex *idx, size_t at) {
    return idx->wide ? ((const ExactAtomSlot *)idx->slots)[at].id
                     : ((const ExactAtomNarrowSlot *)idx->slots)[at].id;
}

static inline CettaIndex exact_slot_row(const ExactAtomIndex *idx, size_t at) {
    return idx->wide ? ((const ExactAtomSlot *)idx->slots)[at].row
                     : ((const ExactAtomNarrowSlot *)idx->slots)[at].row;
}

/* A narrow table holds the pair only when it fits (exact_slot_fits_narrow). */
static inline void exact_slot_put(ExactAtomIndex *idx, size_t at, AtomId id,
                                  CettaIndex row) {
    if (idx->wide)
        ((ExactAtomSlot *)idx->slots)[at] = (ExactAtomSlot){id, row};
    else
        ((ExactAtomNarrowSlot *)idx->slots)[at] =
            (ExactAtomNarrowSlot){(uint32_t)id, (uint32_t)row};
}

static inline void exact_slot_vacate(ExactAtomIndex *idx, size_t at) {
    if (idx->wide)
        ((ExactAtomSlot *)idx->slots)[at].id = CETTA_ATOM_ID_NONE;
    else
        ((ExactAtomNarrowSlot *)idx->slots)[at].id = UINT32_MAX;
}

static inline bool exact_slot_fits_narrow(AtomId id, CettaIndex row) {
    return id < UINT32_MAX && row < UINT32_MAX;
}

static void exact_index_place(ExactAtomIndex *idx, AtomId id, CettaIndex row) {
    size_t at = exact_index_home(id, idx->mask);
    while (!exact_slot_vacant(idx, at))
        at = (at + 1u) & idx->mask;
    exact_slot_put(idx, at, id, row);
    idx->used++;
}

/* Room for `count` rows within the table's load bound (seven tenths), wide
 * once any row needs it. */
static void exact_index_reserve(ExactAtomIndex *idx, size_t count, bool wide) {
    size_t capacity = idx->slots ? idx->mask + 1u : 0u;
    wide = wide || idx->wide;
    if (idx->slots && wide == idx->wide && count <= capacity / 10u * 7u)
        return;
    size_t width = wide ? sizeof(ExactAtomSlot) : sizeof(ExactAtomNarrowSlot);
    size_t next = capacity ? capacity : 16u;
    while (count > next / 10u * 7u) {
        if (next > SIZE_MAX / 2u / width)
            cetta_oom(SIZE_MAX);
        next *= 2u;
    }
    ExactAtomIndex grown = {
        .slots = cetta_malloc(next * width), .mask = next - 1u, .wide = wide};
    memset(grown.slots, 0xff, next * width);
    for (size_t at = 0u; at < capacity; at++) {
        if (!exact_slot_vacant(idx, at))
            exact_index_place(&grown, exact_slot_id(idx, at),
                              exact_slot_row(idx, at));
    }
    free(idx->slots);
    idx->slots = grown.slots;
    idx->mask = grown.mask;
    idx->used = grown.used;
    idx->wide = wide;
}

static void exact_index_add_row(ExactAtomIndex *idx, AtomId id,
                                CettaIndex row) {
    exact_index_reserve(idx, idx->used + 1u, !exact_slot_fits_narrow(id, row));
    exact_index_place(idx, id, row);
}

/* Remove one row, closing the gap by moving later slots of its run back
 * (no tombstones); false when the row is not there. */
static bool exact_index_remove_row(ExactAtomIndex *idx, AtomId id,
                                   CettaIndex row) {
    if (!idx->slots)
        return false;
    size_t mask = idx->mask;
    size_t at = exact_index_home(id, mask);
    for (;; at = (at + 1u) & mask) {
        if (exact_slot_vacant(idx, at))
            return false;
        if (exact_slot_id(idx, at) == id && exact_slot_row(idx, at) == row)
            break;
    }
    size_t gap = at;
    for (size_t next = (gap + 1u) & mask; !exact_slot_vacant(idx, next);
         next = (next + 1u) & mask) {
        size_t home = exact_index_home(exact_slot_id(idx, next), mask);
        /* The slot may fill the gap only if its home lies cyclically at or
         * before the gap. */
        bool stays = gap <= next ? (gap < home && home <= next)
                                 : (gap < home || home <= next);
        if (stays)
            continue;
        exact_slot_put(idx, gap, exact_slot_id(idx, next),
                       exact_slot_row(idx, next));
        gap = next;
    }
    exact_slot_vacate(idx, gap);
    idx->used--;
    return true;
}

static int exact_index_row_compare(const void *left, const void *right) {
    CettaIndex a = *(const CettaIndex *)left;
    CettaIndex b = *(const CettaIndex *)right;
    return (a > b) - (a < b);
}

/* The rows holding `id`, ascending, in a fresh array (NULL when none). */
static CettaIndex exact_index_rows(const ExactAtomIndex *idx, AtomId id,
                                   CettaIndex **out) {
    *out = NULL;
    if (!idx->slots)
        return 0u;
    CettaIndex count = 0u, cap = 0u;
    bool ascending = true;
    for (size_t at = exact_index_home(id, idx->mask);
         !exact_slot_vacant(idx, at); at = (at + 1u) & idx->mask) {
        if (exact_slot_id(idx, at) != id)
            continue;
        if (count == cap) {
            cap = cap ? cap * 2u : 4u;
            *out = cetta_realloc(*out, (size_t)cap * sizeof(**out));
        }
        CettaIndex row = exact_slot_row(idx, at);
        if (count && (*out)[count - 1u] > row)
            ascending = false;
        (*out)[count++] = row;
    }
    if (!ascending)
        qsort(*out, (size_t)count, sizeof(**out), exact_index_row_compare);
    return count;
}

static bool exact_index_holds_id(const ExactAtomIndex *idx, AtomId id) {
    if (!idx->slots)
        return false;
    for (size_t at = exact_index_home(id, idx->mask);
         !exact_slot_vacant(idx, at); at = (at + 1u) & idx->mask) {
        if (exact_slot_id(idx, at) == id)
            return true;
    }
    return false;
}

/* Carry every row through a contraction's monotone renumbering, which keeps
 * each id's rows in order and never widens a row; false when the index holds
 * a removed row and must be rebuilt. */
static bool exact_index_transport(ExactAtomIndex *idx,
                                  const CettaIndex *source_to_target,
                                  CettaCount source_len) {
    for (size_t at = 0u; idx->slots && at <= idx->mask; at++) {
        if (exact_slot_vacant(idx, at))
            continue;
        CettaIndex row = exact_slot_row(idx, at);
        if (row >= source_len ||
            source_to_target[row] == SPACE_OCCURRENCE_COORDINATE_REMOVED)
            return false;
        exact_slot_put(idx, at, exact_slot_id(idx, at), source_to_target[row]);
    }
    return true;
}

static ExactIndexInexactHead *exact_index_inexact_head(
        ExactAtomIndex *idx, SymbolId head, bool insert) {
    if (!idx->inexact_heads) {
        if (!insert)
            return NULL;
        idx->inexact_heads = cetta_malloc(16u * sizeof(*idx->inexact_heads));
        memset(idx->inexact_heads, 0, 16u * sizeof(*idx->inexact_heads));
        idx->inexact_head_mask = 15u;
    } else if (insert && (idx->inexact_head_used + 1u) * 10u >
                             (idx->inexact_head_mask + 1u) * 7u) {
        size_t next = (idx->inexact_head_mask + 1u) * 2u;
        ExactIndexInexactHead *heads = cetta_malloc(next * sizeof(*heads));
        memset(heads, 0, next * sizeof(*heads));
        for (size_t i = 0u; i <= idx->inexact_head_mask; i++) {
            ExactIndexInexactHead entry = idx->inexact_heads[i];
            if (entry.head == SYMBOL_ID_NONE)
                continue;
            size_t at = exact_index_home(entry.head, next - 1u);
            while (heads[at].head != SYMBOL_ID_NONE)
                at = (at + 1u) & (next - 1u);
            heads[at] = entry;
        }
        free(idx->inexact_heads);
        idx->inexact_heads = heads;
        idx->inexact_head_mask = next - 1u;
    }
    size_t mask = idx->inexact_head_mask;
    for (size_t at = exact_index_home(head, mask);;
         at = (at + 1u) & mask) {
        if (idx->inexact_heads[at].head == head)
            return &idx->inexact_heads[at];
        if (idx->inexact_heads[at].head == SYMBOL_ID_NONE) {
            if (!insert)
                return NULL;
            idx->inexact_heads[at].head = head;
            idx->inexact_head_used++;
            return &idx->inexact_heads[at];
        }
    }
}

/* Dense AtomId presence bitset -- doctrine-1 exact-membership at the store seam.
   Interned AtomIds are sequential, so a grown bitset gives O(1) unclusterable
   exact-contains, replacing the fixed-bucket structural walk on the hot path. */
static bool id_present_contains(const SpaceNativeStorage *ns, AtomId id) {
    uint64_t bit = (uint64_t)id;
    return ns->id_present && bit < ns->id_present_bits &&
           ((ns->id_present[bit >> 3] >> (bit & 7u)) & 1u);
}

static bool id_present_set(SpaceNativeStorage *ns, AtomId id) {
    uint64_t bit = (uint64_t)id;
    if (bit >= ns->id_present_bits) {
        uint64_t new_bits = ns->id_present_bits ? ns->id_present_bits : 4096u;
        while (new_bits <= bit) {
            if (new_bits > UINT64_MAX / 2u)
                return false;
            new_bits <<= 1;
        }
        uint64_t new_bytes = new_bits >> 3;
        uint64_t old_bytes = ns->id_present_bits >> 3;
        if (new_bytes > SIZE_MAX)
            return false;
        uint8_t *grown =
            (uint8_t *)cetta_realloc(ns->id_present, (size_t)new_bytes);
        if (!grown)
            return false;
        memset(grown + (size_t)old_bytes, 0,
               (size_t)(new_bytes - old_bytes));
        ns->id_present = grown;
        ns->id_present_bits = new_bits;
    }
    ns->id_present[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
    return true;
}

/* A key the bitset sets that is no row's id: the bitset holds it. */
static bool id_present_key_hold(SpaceNativeStorage *ns, AtomId key) {
    if (ns->id_present_key_len == ns->id_present_key_cap) {
        CettaIndex cap = ns->id_present_key_cap ? ns->id_present_key_cap * 2u
                                                : 16u;
        if (cap > SIZE_MAX / sizeof(AtomId))
            return false;
        AtomId *grown = cetta_realloc(ns->id_present_keys,
                                      (size_t)cap * sizeof(AtomId));
        if (!grown)
            return false;
        ns->id_present_keys = grown;
        ns->id_present_key_cap = cap;
    }
    ns->id_present_keys[ns->id_present_key_len++] = key;
    term_universe_hold(ns->universe, key);
    return true;
}

static void id_present_keys_release(SpaceNativeStorage *ns) {
    for (CettaIndex i = 0u; i < ns->id_present_key_len; i++)
        term_universe_release(ns->universe, ns->id_present_keys[i]);
    ns->id_present_key_len = 0u;
}

static void id_present_clear(SpaceNativeStorage *ns) {
    if (ns->id_present)
        memset(ns->id_present, 0, (size_t)(ns->id_present_bits >> 3));
    id_present_keys_release(ns);
}

static void id_present_free(SpaceNativeStorage *ns) {
    free(ns->id_present);
    ns->id_present = NULL;
    ns->id_present_bits = 0;
    id_present_keys_release(ns);
    free(ns->id_present_keys);
    ns->id_present_keys = NULL;
    ns->id_present_key_cap = 0u;
}

/* Reusable per-thread scratch for canonicalization/interning fallback, reset
   (not freed) per call so the hot dedup path pays no arena init/free churn.
   What it builds lives until that reset, so it interns nothing in the
   process's hash-cons table, which would keep it for the process. */
static _Thread_local Arena g_canon_scratch;
static _Thread_local bool g_canon_scratch_ready = false;
static Arena *canon_scratch(void) {
    if (!g_canon_scratch_ready) {
        arena_init_detached(&g_canon_scratch);
        g_canon_scratch_ready = true;
    }
    return &g_canon_scratch;
}

/* Alpha-canonical AtomId of a STORED atom (maintenance).  Ground atoms are their
   own canonical form (their exact id); non-ground atoms canonicalize their vars
   to first-occurrence ordinals and intern that form, so every alpha-variant of a
   theorem maps to one id.  The presence bitset then answers alpha-aware
   containment as an O(1) id predicate. */
static AtomId space_canonical_id_for_stored(Space *s, AtomId atom_id) {
    const CettaTermHdr *hdr =
        s && s->native.universe
            ? tu_hdr(s->native.universe, atom_id)
            : NULL;
    /* A byte-backed ground record is already its own alpha-canonical key.
       Reading that fact from the stored summary avoids materializing a
       persistent Atom merely to rediscover that it has no variables. */
    if (hdr && !tu_has_vars(s->native.universe, atom_id))
        return atom_id;
    /* An open record's key is read from it: nothing is decoded. */
    AtomId key = hdr ? term_universe_alpha_key_id(s->native.universe, atom_id)
                     : CETTA_ATOM_ID_NONE;
    if (key != CETTA_ATOM_ID_NONE)
        return key;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    if (!atom)
        return CETTA_ATOM_ID_NONE;
    if (!atom_has_vars(atom))
        return atom_id;
    Arena *scratch = canon_scratch();
    ArenaMark mark = arena_mark(scratch);
    Atom *canonical =
        term_universe_alpha_canonicalize_atom(scratch, atom);
    AtomId cid = canonical
        ? term_universe_store_atom_id(s->native.universe, scratch, canonical)
        : CETTA_ATOM_ID_NONE;
    arena_reset(scratch, mark);
    return cid;
}

/* Alpha-canonical AtomId of a QUERY atom (contains).  Find-only, so a query miss
   stays a miss without growing the universe. */
static AtomId space_canonical_id_for_query(Space *s, Atom *atom) {
    if (!s || !atom || !s->native.universe)
        return CETTA_ATOM_ID_NONE;
    if (!atom_has_vars(atom))
        return term_universe_lookup_atom_id(s->native.universe, atom);
    /* The key is looked up part by part, without a canonical copy. */
    bool declined = false;
    AtomId key = term_universe_alpha_key_lookup(s->native.universe, atom,
                                                &declined);
    if (!declined)
        return key;
    Arena *scratch = canon_scratch();
    ArenaMark mark = arena_mark(scratch);
    Atom *canon = term_universe_alpha_canonicalize_atom(scratch, atom);
    AtomId cid = canon
        ? term_universe_lookup_atom_id(s->native.universe, canon)
        : CETTA_ATOM_ID_NONE;
    arena_reset(scratch, mark);
    return cid;
}

static bool atom_has_variables(const Atom *atom) {
    if (!atom) return false;
    if ((atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u)
        return atom_has_vars(atom);
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (atom_has_variables(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool atom_is_exact_indexable(const Atom *atom) {
    if (!atom) return false;
    if ((atom->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) != 0u &&
        !atom_has_vars(atom) && !atom_structural_may_have_internal_tag(atom))
        return true;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_STRUCTURAL:
        case GV_SEAL:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return false;
        case GV_INTERNAL_TAG:
            return cetta_internal_tag_is_list(atom->ground.ival);
        }
        return false;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!atom_is_exact_indexable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool atom_id_is_exact_indexable(const Space *s, AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
    if (!hdr)
        return atom_is_exact_indexable(term_universe_get_atom(s->native.universe, atom_id));
    switch (tu_kind(s->native.universe, atom_id)) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_STRUCTURAL:
        case GV_SEAL:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return false;
        case GV_INTERNAL_TAG:
            return cetta_internal_tag_is_list(
                tu_internal_tag(s->native.universe, atom_id));
        }
        return false;
    case ATOM_EXPR:
        return !tu_has_vars(s->native.universe, atom_id);
    }
    return false;
}

/* Account a row in the exact index: an exact row under its AtomId, any other
 * under its head symbol, or among the rows without one (a variable, a value,
 * a list with a rest, an expression headed by anything but a symbol). */
static void exact_index_note_row(Space *s, AtomId atom_id, CettaIndex row) {
    ExactAtomIndex *idx = &s->native.exact_idx;
    if (atom_id_is_exact_indexable(s, atom_id)) {
        exact_index_add_row(idx, atom_id, row);
        return;
    }
    SymbolId head = tu_head_sym(s->native.universe, atom_id);
    if (head == SYMBOL_ID_NONE)
        idx->inexact_unheaded++;
    else
        exact_index_inexact_head(idx, head, true)->rows++;
}

/* Withdraw a row from the exact index; false when the index does not hold
 * it, and must then be rebuilt. */
static bool exact_index_forget_row(Space *s, AtomId atom_id, CettaIndex row) {
    ExactAtomIndex *idx = &s->native.exact_idx;
    if (atom_id_is_exact_indexable(s, atom_id))
        return exact_index_remove_row(idx, atom_id, row);
    SymbolId head = tu_head_sym(s->native.universe, atom_id);
    if (head == SYMBOL_ID_NONE) {
        if (idx->inexact_unheaded == 0u)
            return false;
        idx->inexact_unheaded--;
        return true;
    }
    ExactIndexInexactHead *entry = exact_index_inexact_head(idx, head, false);
    if (!entry || entry->rows == 0u)
        return false;
    entry->rows--;
    return true;
}

static void eq_index_rebuild(Space *s);
static void ty_ann_index_rebuild(Space *s);
static void exact_index_rebuild(Space *s);
static void recompute_has_non_exact_atoms(Space *s);

static TermUniverse g_space_default_universe = {0};
static Arena g_space_default_arena = {0};
static bool g_space_default_universe_ready = false;

static TermUniverse *space_default_universe(void) {
    if (!g_space_default_universe_ready) {
        arena_init(&g_space_default_arena);
        arena_set_runtime_kind(&g_space_default_arena,
                               CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
        term_universe_init(&g_space_default_universe);
        term_universe_set_persistent_arena(&g_space_default_universe,
                                           &g_space_default_arena);
        g_space_default_universe_ready = true;
    }
    return &g_space_default_universe;
}

static bool space_tracks_atom_ids(const Space *s) {
    return s && s->native.universe != NULL;
}

static ImportedBridgeState *space_imported_bridge_state(Space *s) {
    if (!s)
        return NULL;
    switch (s->match_backend.kind) {
    case SPACE_ENGINE_PATHMAP:
        return &s->match_backend.pathmap.bridge;
    case SPACE_ENGINE_MORK:
        return &s->match_backend.mork.bridge;
    default:
        return NULL;
    }
}

static uint32_t space_atom_id_width_bits_for_format(
    TermUniverseStoreFormat format) {
    return term_universe_store_format_atom_id_width_bits(format);
}

static uint32_t space_atom_id_width_bits_for_universe(
    const TermUniverse *universe) {
    return space_atom_id_width_bits_for_format(term_universe_store_format(universe));
}

static size_t space_atom_id_width_bytes_bits(uint32_t bits) {
    return cetta_atom_id_storage_width_bytes_from_bits(bits);
}

static AtomId space_atom_id_storage_load_at(const uint8_t *storage,
                                            uint32_t bits,
                                            CettaIndex idx) {
    size_t width = space_atom_id_width_bytes_bits(bits);
    if (!storage || width == 0)
        return CETTA_ATOM_ID_NONE;
    return cetta_atom_id_storage_load_bits(storage + ((size_t)idx * width),
                                           bits);
}

static bool space_atom_id_storage_store_at(uint8_t *storage,
                                           uint32_t bits,
                                           CettaIndex idx,
                                           AtomId atom_id) {
    size_t width = space_atom_id_width_bytes_bits(bits);
    if (!storage || width == 0)
        return false;
    return cetta_atom_id_storage_store_bits(
        storage + ((size_t)idx * width), bits, atom_id);
}

static bool space_raw_atom_id_storage_rewrite(uint8_t **storage,
                                              uint8_t *storage_bits,
                                              CettaIndex start,
                                              CettaIndex len,
                                              CettaIndex cap,
                                              uint32_t new_bits) {
    uint32_t old_bits = 0;
    uint8_t *next = NULL;
    size_t new_width = space_atom_id_width_bytes_bits(new_bits);
    if (!storage || !storage_bits)
        return false;
    old_bits = *storage_bits;
    if (old_bits == new_bits)
        return true;
    if (new_width == 0)
        return false;
    if (cap == 0 || !*storage) {
        free(*storage);
        *storage = NULL;
        *storage_bits = (uint8_t)new_bits;
        return true;
    }
    if ((size_t)cap > SIZE_MAX / new_width)
        return false;
    next = cetta_malloc((size_t)cap * new_width);
    if (!next)
        return false;
    memset(next, 0, (size_t)cap * new_width);
    for (CettaIndex i = start; i < start + len; i++) {
        AtomId atom_id = space_atom_id_storage_load_at(*storage, old_bits, i);
        if (!space_atom_id_storage_store_at(next, new_bits, i, atom_id)) {
            free(next);
            return false;
        }
    }
    free(*storage);
    *storage = next;
    *storage_bits = (uint8_t)new_bits;
    return true;
}

static bool space_sync_atom_id_storage_width_bits(Space *s,
                                                  uint32_t want_bits) {
    if (!s || !space_tracks_atom_ids(s))
        return true;
    if (want_bits == 0)
        return false;
    if (s->native.atom_id_width_bits == 0) {
        s->native.atom_id_width_bits = (uint8_t)want_bits;
        return true;
    }
    return space_raw_atom_id_storage_rewrite(
        &s->native.atom_ids, &s->native.atom_id_width_bits, s->native.start,
        s->native.len, s->native.cap, want_bits);
}

static bool space_sync_atom_id_storage_width(Space *s) {
    return space_sync_atom_id_storage_width_bits(
        s, s && s->native.universe
               ? space_atom_id_width_bits_for_universe(s->native.universe)
               : 0);
}

static bool space_sync_projected_atom_id_storage_width_bits(Space *s,
                                                            uint32_t want_bits) {
    ImportedBridgeState *st = NULL;
    if (!s)
        return true;
    st = space_imported_bridge_state(s);
    if (!st)
        return true;
    if (want_bits == 0)
        return false;
    if (st->projected_atom_id_width_bits == 0) {
        st->projected_atom_id_width_bits = (uint8_t)want_bits;
        return true;
    }
    return space_raw_atom_id_storage_rewrite(
        &st->projected_atom_ids, &st->projected_atom_id_width_bits, 0,
        st->projected_len, st->projected_len, want_bits);
}

static bool space_term_universe_store_format_observer(
    TermUniverse *universe,
    TermUniverseStoreFormat old_format,
    TermUniverseStoreFormat new_format,
    void *ctx) {
    Space *space = ctx;
    uint32_t want_bits = space_atom_id_width_bits_for_format(new_format);
    (void)old_format;
    if (!space || space->native.universe != universe)
        return true;
    if (!space_sync_atom_id_storage_width_bits(space, want_bits))
        return false;
    return space_sync_projected_atom_id_storage_width_bits(space, want_bits);
}

static void space_visit_stored_ids(const uint8_t *ids, uint8_t width_bits,
                                   CettaIndex start, CettaIndex len,
                                   TermUniverseRootVisit visit,
                                   void *visit_context) {
    if (!ids)
        return;
    for (CettaIndex i = start; i < start + len; i++)
        visit(visit_context, space_atom_id_storage_load_at(ids, width_bits, i));
}

static void space_visit_flat_bucket_ids(const ImportedFlatBucket *bucket,
                                        TermUniverseRootVisit visit,
                                        void *visit_context) {
    for (CettaIndex e = 0u; bucket->entries && e < bucket->len; e++) {
        const ImportedFlatEntry *entry = &bucket->entries[e];
        for (CettaIndex t = 0u; entry->tokens && t < entry->len; t++) {
            if (entry->tokens[t].origin_id != CETTA_ATOM_ID_NONE)
                visit(visit_context, entry->tokens[t].origin_id);
        }
    }
}

static void space_visit_head_chain_ids(const SpaceHeadChains *chains,
                                       TermUniverseRootVisit visit,
                                       void *visit_context) {
    for (CettaIndex i = 0u; chains->atom_ids && i < chains->len; i++)
        visit(visit_context, chains->atom_ids[i]);
}

static bool space_marks_row_marked(const SpaceNativeStorage *n,
                                   CettaIndex row);

/* The ids a space counts: one reference a live row, and one a key its
 * presence bitset holds beyond its rows' own ids.  A row marked removed
 * holds nothing: no reader meets it, and contracting it reads no more than
 * its id.  A dirty bitset is cleared before it is read, so only its held keys
 * matter. */
static void space_dense_list(const Space *s, TermUniverseRootVisit visit,
                             void *visit_context);

static void space_term_universe_counted_roots(const TermUniverse *universe,
                                              TermUniverseRootVisit visit,
                                              void *visit_context, void *ctx) {
    Space *space = ctx;
    if (!space || space->native.universe != universe)
        return;
    const SpaceNativeStorage *native = &space->native;
    if (native->marked == 0u) {
        space_visit_stored_ids(native->atom_ids, native->atom_id_width_bits,
                               native->start, native->len, visit,
                               visit_context);
    } else {
        for (CettaIndex row = 0u; native->atom_ids && row < native->len;
             row++) {
            if (!space_marks_row_marked(native, row))
                visit(visit_context,
                      space_atom_id_storage_load_at(
                          native->atom_ids, native->atom_id_width_bits,
                          native->start + row));
        }
    }
    for (CettaIndex i = 0u; i < native->id_present_key_len; i++)
        visit(visit_context, native->id_present_keys[i]);
    space_dense_list(space, visit, visit_context);
}

/* The other ids a space keeps between calls, listed at each collection: a
 * backend's projected rows and flat tokens, and the ids its equation and
 * declaration indexes captured. */
static void space_term_universe_roots(const TermUniverse *universe,
                                      TermUniverseRootVisit visit,
                                      void *visit_context, void *ctx) {
    Space *space = ctx;
    if (!space || space->native.universe != universe)
        return;
    ImportedBridgeState *st = space_imported_bridge_state(space);
    if (st) {
        space_visit_stored_ids(st->projected_atom_ids,
                               st->projected_atom_id_width_bits, 0u,
                               st->projected_len, visit, visit_context);
        for (size_t b = 0u; b < STREE_BUCKETS; b++)
            space_visit_flat_bucket_ids(&st->buckets[b], visit,
                                        visit_context);
        space_visit_flat_bucket_ids(&st->wildcard, visit, visit_context);
    }
    const EqIndex *eq = &space->native.eq_idx;
    for (size_t b = 0u; b <= EQ_INDEX_BUCKETS; b++) {
        const EqBucket *bucket =
            b < EQ_INDEX_BUCKETS ? &eq->buckets[b] : &eq->wildcard;
        for (CettaIndex i = 0u; bucket->atom_ids && i < bucket->len; i++)
            visit(visit_context, bucket->atom_ids[i]);
    }
    space_visit_head_chain_ids(&eq->head_rows, visit, visit_context);
    space_visit_head_chain_ids(&space->native.ty_idx.rows, visit,
                               visit_context);
}

static void space_attach_to_universe(Space *s, TermUniverse *universe) {
    if (!s || !universe)
        return;
    (void)term_universe_add_store_format_observer(
        universe, space_term_universe_store_format_observer, s);
    (void)term_universe_add_counted_root_provider(
        universe, space_term_universe_counted_roots, s);
    (void)term_universe_add_root_provider(
        universe, space_term_universe_roots, s);
}

static void space_detach_from_universe(Space *s) {
    if (!s || !s->native.universe)
        return;
    term_universe_remove_store_format_observer(
        s->native.universe, space_term_universe_store_format_observer, s);
    term_universe_remove_root_provider(
        s->native.universe, space_term_universe_counted_roots, s);
    term_universe_remove_root_provider(
        s->native.universe, space_term_universe_roots, s);
}

static void space_note_atom_id_storage_peak(const Space *s) {
    size_t width = 0;
    if (!s || !space_tracks_atom_ids(s))
        return;
    width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
    (void)width;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_SPACE_ATOM_ID_LIVE_BYTES_PEAK,
        (uint64_t)s->native.len * (uint64_t)width);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_SPACE_ATOM_ID_CAPACITY_BYTES_PEAK,
        (uint64_t)s->native.cap * (uint64_t)width);
}

static bool space_has_overlay_base(const Space *s) {
    return s && s->overlay_base;
}

typedef enum {
    SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY = 0,
    /* Bits, merged by union: an `=` occurrence, an unclassifiable
     * occurrence (advances every projection clock conservatively), and a
     * `:` declaration, which lies outside the equation-occurrence projection
     * but inside the program projection read by callability and arity. */
    SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT = 1u << 0,
    SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE = 1u << 1,
    SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION = 1u << 2,
    /* With DECLARATION: the declaration is a `(:< sub super)` row. */
    SPACE_MUTATION_EQUATION_PROJECTION_SUBTYPE = 1u << 3,
} SpaceMutationEquationProjection;

typedef enum {
    SPACE_MUTATION_PREFIX_APPEND_ONLY = 0,
    SPACE_MUTATION_PREFIX_REWRITTEN,
} SpaceMutationPrefixEffect;

static void space_mark_indexes_dirty(Space *s);
static void space_note_released(Space *s, CettaCount rows);
static void space_release_row(Space *s, AtomId atom_id);
static void space_release_rows(Space *s, CettaIndex from, CettaIndex to);
static void space_publish_mutation(
    Space *s, SpaceMutationEquationProjection equation_projection,
    SpaceMutationPrefixEffect prefix_effect);
static void space_replace_contents_classified(
    Space *dst, Space *src,
    SpaceMutationEquationProjection equation_projection);
static SpaceMutationEquationProjection
space_local_removal_equation_projection(const Space *s, CettaIndex raw_idx);
static SpaceMutationEquationProjection
space_atom_equation_projection(const Space *s, AtomId atom_id, Atom *atom);
static bool space_contract_occurrence_mask(
        Space *s, const uint8_t *remove_mask, CettaCount mask_len,
        CettaCount *out_removed, bool settling);

static CettaCount space_local_length64(const Space *s) {
    return s ? (CettaCount)space_match_backend_logical_len64(s) : 0;
}

static AtomId space_local_get_atom_id_at64(const Space *s, CettaIndex idx) {
    return s ? space_match_backend_get_atom_id_at64(s, idx) : CETTA_ATOM_ID_NONE;
}

static Atom *space_local_get_at64(const Space *s, CettaIndex idx) {
    return s ? space_match_backend_get_at64(s, idx) : NULL;
}

static CettaIndex space_overlay_removed_lower_bound(const Space *s,
                                                    CettaIndex idx) {
    CettaIndex lo = 0;
    CettaIndex hi = space_has_overlay_base(s)
        ? s->overlay_removed_base_len : 0;
    while (lo < hi) {
        CettaIndex mid = lo + (hi - lo) / 2u;
        if (s->overlay_removed_base_indices[mid] < idx)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

static CettaIndex space_overlay_removed_upper_bound(const Space *s,
                                                    CettaIndex idx) {
    CettaIndex lo = 0;
    CettaIndex hi = space_has_overlay_base(s)
        ? s->overlay_removed_base_len : 0;
    while (lo < hi) {
        CettaIndex mid = lo + (hi - lo) / 2u;
        if (s->overlay_removed_base_indices[mid] <= idx)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

static bool space_overlay_base_index_removed(const Space *s, CettaIndex idx) {
    if (!space_has_overlay_base(s))
        return false;
    CettaIndex slot = space_overlay_removed_lower_bound(s, idx);
    return slot < s->overlay_removed_base_len &&
           s->overlay_removed_base_indices[slot] == idx;
}

static void space_overlay_clear_removed_at_or_after(Space *s, CettaIndex limit) {
    if (!space_has_overlay_base(s))
        return;
    s->overlay_removed_base_len =
        space_overlay_removed_lower_bound(s, limit);
}

static void space_overlay_trim_base_tail(Space *s) {
    if (!space_has_overlay_base(s))
        return;
    while (s->overlay_base_visible_len > 0 &&
           s->overlay_removed_base_len > 0) {
        CettaIndex tail = s->overlay_base_visible_len - 1u;
        CettaIndex last = s->overlay_removed_base_len - 1u;
        if (s->overlay_removed_base_indices[last] != tail)
            break;
        s->overlay_removed_base_len = last;
        s->overlay_base_visible_len = tail;
    }
}

static bool space_overlay_mark_base_removed(Space *s, CettaIndex idx) {
    if (!space_has_overlay_base(s) || idx >= s->overlay_base_visible_len)
        return false;
    CettaIndex slot = space_overlay_removed_lower_bound(s, idx);
    if (slot < s->overlay_removed_base_len &&
        s->overlay_removed_base_indices[slot] == idx) {
        return true;
    }
    if (s->overlay_removed_base_len == s->overlay_removed_base_cap) {
        if (s->overlay_removed_base_cap > UINT64_MAX / 2u)
            return false;
        CettaIndex next_cap =
            s->overlay_removed_base_cap ? s->overlay_removed_base_cap * 2u : 8u;
        if (next_cap > SIZE_MAX / sizeof(CettaIndex))
            return false;
        CettaIndex *next = cetta_realloc(
            s->overlay_removed_base_indices, sizeof(CettaIndex) * (size_t)next_cap);
        if (!next)
            return false;
        s->overlay_removed_base_indices = next;
        s->overlay_removed_base_cap = next_cap;
    }
    if (slot < s->overlay_removed_base_len) {
        memmove(&s->overlay_removed_base_indices[slot + 1u],
                &s->overlay_removed_base_indices[slot],
                (size_t)(s->overlay_removed_base_len - slot) *
                    sizeof(CettaIndex));
    }
    s->overlay_removed_base_indices[slot] = idx;
    s->overlay_removed_base_len++;
    if (idx + 1u == s->overlay_base_visible_len)
        space_overlay_trim_base_tail(s);
    space_mark_indexes_dirty(s);
    return true;
}

static CettaCount space_overlay_visible_base_count(const Space *s) {
    if (!space_has_overlay_base(s))
        return 0;
    if (s->module_own_view)
        return space_length64(s->overlay_base) -
               space_imported_length(s->overlay_base);
    return s->overlay_base_visible_len - s->overlay_removed_base_len;
}

static bool space_overlay_visible_base_raw_index(const Space *s,
                                                 CettaCount visible_index,
                                                 CettaIndex *out_raw) {
    if (!space_has_overlay_base(s) || !out_raw)
        return false;
    CettaCount visible_count = space_overlay_visible_base_count(s);
    if (visible_index >= visible_count)
        return false;
    if (s->module_own_view) {
        *out_raw = space_imported_length(s->overlay_base) + visible_index;
        return true;
    }
    CettaCount target = visible_index + 1u;
    CettaIndex lo = 0;
    CettaIndex hi = s->overlay_base_visible_len;
    while (lo < hi) {
        CettaIndex mid = lo + (hi - lo) / 2u;
        CettaIndex removed_through =
            space_overlay_removed_upper_bound(s, mid);
        CettaCount visible_through =
            (CettaCount)(mid + 1u - removed_through);
        if (visible_through >= target)
            hi = mid;
        else
            lo = mid + 1u;
    }
    if (lo >= s->overlay_base_visible_len ||
        space_overlay_base_index_removed(s, lo)) {
        return false;
    }
    *out_raw = lo;
    return true;
}

static bool space_overlay_remove_local_raw_index(Space *s, CettaIndex raw_idx) {
    space_settle_marks(s);
    if (!s)
        return false;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    if (raw_idx >= s->native.len)
        return false;
    SpaceMutationEquationProjection equation_projection =
        space_local_removal_equation_projection(s, raw_idx);
    AtomId removed_id = space_atom_id_storage_load_at(
        s->native.atom_ids, s->native.atom_id_width_bits, raw_idx);
    if (space_is_ordered(s)) {
        size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        memmove(s->native.atom_ids + ((size_t)raw_idx * width),
                s->native.atom_ids + ((size_t)(raw_idx + 1u) * width),
                (size_t)(s->native.len - raw_idx - 1u) * width);
        s->native.len--;
    } else {
        AtomId tail_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, s->native.len - 1u);
        s->native.len--;
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits, raw_idx, tail_id);
    }
    space_release_row(s, removed_id);
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_publish_mutation(
        s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

static void space_reserve_linear(Space *s, CettaIndex min_cap) {
    size_t width = 0;
    if (s->native.cap >= min_cap)
        return;
    if (!space_sync_atom_id_storage_width(s))
        return;
    CettaIndex new_cap = s->native.cap ? s->native.cap : 64;
    while (new_cap < min_cap)
        new_cap *= 2;
    width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
    s->native.atom_ids = cetta_realloc(
        s->native.atom_ids, width * (size_t)new_cap);
    s->native.cap = new_cap;
    space_note_atom_id_storage_peak(s);
}

/* Rows a space removes release the references they held: the universe
 * counts them as its estimate of the garbage made since its last collection. */
static void space_note_released(Space *s, CettaCount rows) {
    if (s && rows)
        term_universe_collection_note_released(s->native.universe, rows);
}

/* A row appended holds its atom, one reference counted by the universe. */
static inline void space_hold_row(Space *s, AtomId atom_id) {
    term_universe_hold(s->native.universe, atom_id);
}

/* A row removed drops its reference. */
static void space_release_row(Space *s, AtomId atom_id) {
    space_note_released(s, 1u);
    term_universe_release(s->native.universe, atom_id);
}

/* Drop the references of rows [from, to), those not marked: a marked row
 * dropped its own when it was marked. */
static void space_release_rows(Space *s, CettaIndex from, CettaIndex to) {
    TermUniverse *universe = s->native.universe;
    if (from < to)
        space_note_released(s, to - from);
    if (!universe || !universe->counting || !s->native.atom_ids)
        return;
    for (CettaIndex row = from; row < to; row++) {
        if (s->native.marked != 0u && space_marks_row_marked(&s->native, row))
            continue;
        term_universe_release_counted(
            universe, space_atom_id_storage_load_at(
                          s->native.atom_ids, s->native.atom_id_width_bits,
                          s->native.start + row));
    }
}

void space_rows_hold(Space *s) {
    TermUniverse *universe = s ? s->native.universe : NULL;
    if (!universe || !universe->counting || !s->native.atom_ids)
        return;
    for (CettaIndex row = 0u; row < s->native.len; row++) {
        if (s->native.marked != 0u && space_marks_row_marked(&s->native, row))
            continue;
        term_universe_hold_counted(
            universe, space_atom_id_storage_load_at(
                          s->native.atom_ids, s->native.atom_id_width_bits,
                          s->native.start + row));
    }
}

void space_rows_release(Space *s) {
    if (s)
        space_release_rows(s, 0u, s->native.len);
}

/* ── Relations kept as bits or tables ───────────────────────────────────── */

/*
 * A multiset or set space may keep the ground facts (h c1 ... ck) of a head
 * h out of its rows: the facts of k columns, 1 <= k <= CETTA_TUPLE_MAX_ARITY,
 * whose columns are atoms equal only to themselves.  The head's relation is
 * bits when k is two (space_dense.h) and a table of tuples otherwise
 * (space_tuples.h).  The space is then its rows and, for each such head, its
 * relation.  A copy of a fact the relation holds already is a row, so the
 * space is the same bag; a removal takes a row copy first and a fact after,
 * which leaves the same bag either way.
 *
 * The relations answer adds, removals, membership, the length and the native
 * query's legs.  Every other reader gives a head's facts back to the rows
 * first (space_dense_settle): positional reads past the rows, and patterns
 * whose head is the head or is not a symbol.  The facts then stay rows.
 *
 * A head is counted while its facts are rows; once they are enough, its
 * later facts go to its relation, while that costs less than rows would.  A
 * head's relation has the columns of the first fact counted; facts of the
 * head with other columns stay rows.  CETTA_SPACE_DENSE=eager keeps a head's
 * relation from its first fact, so a run reads relations wherever a head
 * qualifies; =off keeps every fact a row.
 */
enum {
    SPACE_DENSE_COUNTING = 0,
    SPACE_DENSE_KEPT,
    SPACE_DENSE_ROWS,
};

#define SPACE_DENSE_THRESHOLD 4096u
/* What a fact costs as a row, in bytes, against which its relation is
 * weighed: its record, entry, intern slot, row and index entries. */
#define SPACE_DENSE_ROW_BYTES 96u

typedef struct {
    SymbolId head;
    AtomId head_id;
    uint8_t state;
    /* The columns of the facts the relation holds: the first counted
     * fact's. */
    uint8_t arity;
    /* Counting: the head's facts added as rows.  Kept: at least as many as
     * its facts kept as rows, those left at promotion and any added since;
     * zero when none is. */
    CettaCount rows;
    /* The head's facts in a batch about to be added (space_dense_prepare_adds). */
    CettaCount batch;
    /* Kept: the relation, bits for two columns and a table otherwise. */
    CettaDenseRelation bits;
    CettaTupleTable table;
} SpaceDenseHead;

struct SpaceDense {
    SpaceDenseHead *heads;
    uint32_t len, cap;
    /* Head symbol -> position plus one, open-addressed. */
    uint32_t *index;
    uint32_t index_mask;
    /* Giving a relation back to rows, which changes no fact of the space:
     * the rows it adds publish nothing. */
    bool settling;
    /* A row whose head is open (a variable, a term without a symbol head, or
     * one stored without a record) has been in the space since its relations
     * began: such a row may stand for a fact of any head. */
    bool open_rows;
};

/* Readers of relations on this thread: the guards leave the relations alone
 * while one reads them and asks the rows for the rest. */
static _Thread_local uint32_t g_space_dense_reading = 0u;

/* The source of dense epochs: each change of form takes a fresh one, so no
 * value repeats across spaces or their relations' lives. */
static _Atomic uint64_t g_space_dense_epochs = 0u;

static void space_dense_touch(Space *s) {
    s->native.dense_epoch = atomic_fetch_add_explicit(
        &g_space_dense_epochs, 1u, memory_order_relaxed) + 1u;
}

/* CETTA_SPACE_DENSE_TRACE=1 prints each head that takes a relation and each
 * that gives one back. */
static bool space_dense_trace(void) {
    static int trace = -1;
    if (trace < 0) {
        const char *flag = getenv("CETTA_SPACE_DENSE_TRACE");
        trace = flag && *flag && strcmp(flag, "0") != 0;
    }
    return trace != 0;
}

/* CETTA_SPACE_DENSE: off (or 0) keeps every fact a row; eager gives a head
 * its relation from its first fact; anything else, or nothing, from
 * SPACE_DENSE_THRESHOLD facts. */
enum { SPACE_DENSE_MODE_OFF, SPACE_DENSE_MODE_ON, SPACE_DENSE_MODE_EAGER };

static int space_dense_mode(void) {
    static int mode = -1;
    if (mode < 0) {
        const char *flag = getenv("CETTA_SPACE_DENSE");
        mode = !flag ? SPACE_DENSE_MODE_ON
             : strcmp(flag, "off") == 0 || strcmp(flag, "0") == 0
                 ? SPACE_DENSE_MODE_OFF
             : strcmp(flag, "eager") == 0 ? SPACE_DENSE_MODE_EAGER
             : SPACE_DENSE_MODE_ON;
    }
    return mode;
}

static bool space_dense_off(void) {
    return space_dense_mode() == SPACE_DENSE_MODE_OFF;
}

/* The facts a head takes its relation at. */
static uint64_t space_dense_threshold(void) {
    return space_dense_mode() == SPACE_DENSE_MODE_EAGER ? 1u
                                                         : SPACE_DENSE_THRESHOLD;
}

/* Whether the space may keep relations: a native multiset or set space of
 * its own rows only, outside a concurrent-evaluation scope. */
static bool space_dense_admits(const Space *s) {
    return s && (s->kind == SPACE_KIND_ATOM || s->kind == SPACE_KIND_SET) &&
           !s->module_own_view && !space_has_overlay_base(s) &&
           s->imported_len == 0u && !space_has_dependencies(s) &&
           (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
            s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) &&
           s->native.universe && space_tracks_atom_ids(s) &&
           !cetta_shared_transition_scope_active() && !space_dense_off();
}

/* A column a relation can hold: an atom equal only to itself, so its id is
 * its identity. */
static bool space_dense_column(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr)
        return false;
    if (hdr->tag == ATOM_SYMBOL)
        return true;
    return hdr->tag == ATOM_GROUNDED &&
           (hdr->subtag == GV_INT || hdr->subtag == GV_BOOL ||
            hdr->subtag == GV_STRING);
}

/* Whether an expression of LEN elements has the columns of a fact some
 * relation could hold. */
static inline bool space_dense_fact_len(uint32_t len) {
    return len >= 2u && len <= CETTA_TUPLE_MAX_ARITY + 1u;
}

/* Whether the row ID is a fact a relation could hold: a ground
 * (h c1 ... ck) of a head other than the program's (=, =%, :, <:), each
 * column one a relation holds.  Its columns go to COLUMNS and their number
 * to *ARITY. */
static bool space_dense_fact(const Space *s, AtomId id, SymbolId *head,
                             AtomId *head_id, AtomId *columns,
                             uint32_t *arity) {
    const TermUniverse *universe = s->native.universe;
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr || hdr->tag != ATOM_EXPR ||
        !space_dense_fact_len(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) ||
        hdr->sym_or_head == SYMBOL_ID_NONE)
        return false;
    SymbolId h = hdr->sym_or_head;
    if (h == g_builtin_syms.equals || h == g_builtin_syms.equals_percent ||
        h == g_builtin_syms.colon || h == g_builtin_syms.subtype)
        return false;
    uint32_t k = (hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) - 1u;
    for (uint32_t c = 0u; c < k; c++) {
        columns[c] = tu_child_from_header(universe, hdr, c + 1u);
        if (!space_dense_column(universe, columns[c]))
            return false;
    }
    *head = h;
    *head_id = tu_child_from_header(universe, hdr, 0u);
    *arity = k;
    return true;
}

static inline uint32_t space_dense_head_hash(SymbolId head) {
    return (uint32_t)(((uint64_t)head * UINT64_C(0x9E3779B97F4A7C15)) >> 32);
}

static SpaceDenseHead *space_dense_find(const struct SpaceDense *dense,
                                        SymbolId head) {
    if (!dense || !dense->index)
        return NULL;
    for (uint32_t i = space_dense_head_hash(head) & dense->index_mask;;
         i = (i + 1u) & dense->index_mask) {
        uint32_t slot = dense->index[i];
        if (slot == 0u)
            return NULL;
        if (dense->heads[slot - 1u].head == head)
            return &dense->heads[slot - 1u];
    }
}

/* ── A head's relation, whichever its form ── */

static inline bool space_dense_bits(const SpaceDenseHead *head) {
    return head->arity == 2u;
}

static uint64_t space_dense_kept_facts(const SpaceDenseHead *head) {
    return space_dense_bits(head) ? cetta_dense_relation_pairs(&head->bits)
                                  : cetta_tuple_table_tuples(&head->table);
}

/* The ids the relation numbers, in order of numbering. */
static uint32_t space_dense_kept_numbered(const SpaceDenseHead *head) {
    return space_dense_bits(head) ? head->bits.len : head->table.len;
}

static AtomId space_dense_kept_id(const SpaceDenseHead *head, uint32_t n) {
    return (AtomId)(space_dense_bits(head) ? head->bits.ids[n]
                                           : head->table.ids[n]);
}

static uint64_t space_dense_kept_bytes(const SpaceDenseHead *head) {
    return space_dense_bits(head) ? cetta_dense_relation_bytes(&head->bits)
                                  : cetta_tuple_table_bytes(&head->table);
}

static void space_dense_kept_free(SpaceDenseHead *head) {
    cetta_dense_relation_free(&head->bits);
    cetta_tuple_table_free(&head->table);
}

/* A pair visit of bits seen as a visit of tuples. */
typedef struct {
    CettaTupleVisit visit;
    void *context;
} SpaceDensePairVisit;

static bool space_dense_pair_as_tuple(void *context, CettaDenseId a,
                                      CettaDenseId b) {
    const SpaceDensePairVisit *pair = context;
    CettaDenseId tuple[2] = {a, b};
    return pair->visit(pair->context, tuple);
}

bool space_dense_form_each(SpaceDenseForm form, const CettaDenseId *fixed,
                           CettaTupleVisit visit, void *context) {
    if (form.bits) {
        SpaceDensePairVisit pair = {.visit = visit, .context = context};
        return cetta_dense_relation_each(form.bits,
            fixed ? fixed[0] : CETTA_DENSE_ANY,
            fixed ? fixed[1] : CETTA_DENSE_ANY,
            space_dense_pair_as_tuple, &pair);
    }
    return cetta_tuple_table_each(form.table, fixed, visit, context);
}

uint64_t space_dense_form_count(SpaceDenseForm form,
                                const CettaDenseId *fixed) {
    if (form.bits) {
        CettaDenseId a = fixed ? fixed[0] : CETTA_DENSE_ANY;
        CettaDenseId b = fixed ? fixed[1] : CETTA_DENSE_ANY;
        if (a == CETTA_DENSE_ANY && b == CETTA_DENSE_ANY)
            return cetta_dense_relation_pairs(form.bits);
        if (a != CETTA_DENSE_ANY && b != CETTA_DENSE_ANY)
            return cetta_dense_relation_contains(form.bits, a, b) ? 1u : 0u;
        return cetta_dense_relation_count(form.bits, a, b);
    }
    return cetta_tuple_table_count(form.table, fixed);
}

uint64_t space_dense_form_facts(SpaceDenseForm form) {
    return form.bits ? cetta_dense_relation_pairs(form.bits)
                     : cetta_tuple_table_tuples(form.table);
}

/* Whether a row's head is open: it may stand for a fact of any head. */
static bool space_dense_row_head_open(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    return !hdr || hdr->tag == ATOM_VAR ||
           (hdr->tag == ATOM_EXPR && hdr->sym_or_head == SYMBOL_ID_NONE);
}

/* The head's entry, made when it is new with relations of ARITY columns;
 * NULL without room, or when its relation has other columns. */
static SpaceDenseHead *space_dense_head(Space *s, SymbolId head,
                                        AtomId head_id, uint32_t arity) {
    struct SpaceDense *dense = s->native.dense;
    if (!dense) {
        dense = calloc(1u, sizeof(*dense));
        if (!dense)
            return NULL;
        for (CettaIndex row = 0u; !dense->open_rows && row < s->native.len; row++)
            dense->open_rows = space_dense_row_head_open(
                s->native.universe, space_get_atom_id_at64(s, row));
        s->native.dense = dense;
    }
    SpaceDenseHead *found = space_dense_find(dense, head);
    if (found)
        return found->arity == arity ? found : NULL;
    if (!dense->index || (uint64_t)(dense->len + 1u) * 2u >
                             (uint64_t)dense->index_mask + 1u) {
        uint32_t size = dense->index ? (dense->index_mask + 1u) * 2u : 16u;
        uint32_t *index = calloc(size, sizeof(*index));
        if (!index)
            return NULL;
        for (uint32_t n = 0u; n < dense->len; n++) {
            uint32_t i = space_dense_head_hash(dense->heads[n].head) &
                         (size - 1u);
            while (index[i] != 0u)
                i = (i + 1u) & (size - 1u);
            index[i] = n + 1u;
        }
        free(dense->index);
        dense->index = index;
        dense->index_mask = size - 1u;
    }
    if (dense->len == dense->cap) {
        uint32_t cap = dense->cap ? dense->cap * 2u : 8u;
        SpaceDenseHead *heads = realloc(dense->heads, cap * sizeof(*heads));
        if (!heads)
            return NULL;
        dense->heads = heads;
        dense->cap = cap;
    }
    SpaceDenseHead *entry = &dense->heads[dense->len];
    *entry = (SpaceDenseHead){.head = head, .head_id = head_id,
                              .arity = (uint8_t)arity};
    cetta_dense_relation_init(&entry->bits);
    if (!cetta_tuple_table_init(&entry->table, arity))
        return NULL;
    uint32_t i = space_dense_head_hash(head) & dense->index_mask;
    while (dense->index[i] != 0u)
        i = (i + 1u) & dense->index_mask;
    dense->index[i] = ++dense->len;
    space_dense_touch(s);
    return entry;
}

static void space_dense_release_id(void *context, CettaDenseId id) {
    term_universe_release(((Space *)context)->native.universe, (AtomId)id);
}

static void space_dense_kept_release(Space *s, const SpaceDenseHead *head) {
    if (space_dense_bits(head))
        cetta_dense_relation_each_numbered(&head->bits, space_dense_release_id,
                                           s);
    else
        cetta_tuple_table_each_numbered(&head->table, space_dense_release_id,
                                        s);
}

/* Hold the ids numbered from FROM on, as the relation's references. */
static void space_dense_hold_numbered(Space *s, const SpaceDenseHead *head,
                                      uint32_t from) {
    for (uint32_t n = from; n < space_dense_kept_numbered(head); n++)
        space_hold_row(s, space_dense_kept_id(head, n));
}

/* The ids a space's relations hold: each head's own and its numbered
 * columns. */
static void space_dense_list(const Space *s, TermUniverseRootVisit visit,
                             void *visit_context) {
    const struct SpaceDense *dense = s->native.dense;
    for (uint32_t i = 0u; dense && i < dense->len; i++) {
        const SpaceDenseHead *head = &dense->heads[i];
        if (head->state != SPACE_DENSE_KEPT)
            continue;
        visit(visit_context, head->head_id);
        for (uint32_t n = 0u; n < space_dense_kept_numbered(head); n++)
            visit(visit_context, space_dense_kept_id(head, n));
    }
}

static void space_add_stored_id(Space *s, AtomId atom_id, Atom *backend_atom);

typedef struct {
    Space *space;
    AtomId head_id;
    uint32_t arity;
} SpaceDenseSettle;

static bool space_dense_settle_tuple(void *context, const CettaDenseId *tuple) {
    SpaceDenseSettle *settle = context;
    AtomId kids[CETTA_TUPLE_MAX_ARITY + 1u];
    kids[0] = settle->head_id;
    for (uint32_t c = 0u; c < settle->arity; c++)
        kids[c + 1u] = (AtomId)tuple[c];
    AtomId id = tu_expr_from_ids(settle->space->native.universe, kids,
                                 settle->arity + 1u);
    if (id == CETTA_ATOM_ID_NONE) {
        fputs("CeTTa: no room to give a relation back to rows\n", stderr);
        abort();
    }
    space_add_stored_id(settle->space, id, NULL);
    return true;
}

/* The relation of a head it keeps, as readers see it. */
static SpaceDenseForm space_dense_head_form(SpaceDenseHead *head) {
    return space_dense_bits(head)
        ? (SpaceDenseForm){.arity = 2u, .head_id = head->head_id,
                           .bits = &head->bits}
        : (SpaceDenseForm){.arity = head->arity, .head_id = head->head_id,
                           .table = &head->table};
}

/* Give HEAD's facts back to the rows; the head stays rows from now on. */
static void space_dense_settle_head(Space *s, SpaceDenseHead *head) {
    if (head->state == SPACE_DENSE_ROWS)
        return;
    bool kept = head->state == SPACE_DENSE_KEPT;
    head->state = SPACE_DENSE_ROWS;
    if (!kept)
        return;
    space_dense_touch(s);
    struct SpaceDense *dense = s->native.dense;
    uint64_t facts = space_dense_kept_facts(head);
    if (space_dense_trace())
        fprintf(stderr, "space relations: %s gives %" PRIu64 " facts back to rows\n",
                symbol_bytes(g_symbols, head->head), facts);
    s->native.dense_facts -= facts;
    /* The facts become rows, which take their own references, before the
     * relation drops its own.  The space holds the same facts at the same
     * positions, so the rows publish no mutation: its revision, and every
     * observation of it, stand. */
    SpaceDenseSettle settle = {.space = s, .head_id = head->head_id,
                               .arity = head->arity};
    dense->settling = true;
    (void)space_dense_form_each(space_dense_head_form(head), NULL,
                                space_dense_settle_tuple, &settle);
    dense->settling = false;
    space_dense_kept_release(s, head);
    term_universe_release(s->native.universe, head->head_id);
    space_dense_kept_free(head);
    head->rows = 0u;
}

void space_dense_settle(Space *s, SymbolId head) {
    if (!s || s->native.dense_facts == 0u || g_space_dense_reading != 0u)
        return;
    struct SpaceDense *dense = s->native.dense;
    if (head != SYMBOL_ID_NONE) {
        SpaceDenseHead *entry = space_dense_find(dense, head);
        if (entry && entry->state == SPACE_DENSE_KEPT)
            space_dense_settle_head(s, entry);
        return;
    }
    for (uint32_t i = 0u; i < dense->len; i++) {
        if (dense->heads[i].state == SPACE_DENSE_KEPT)
            space_dense_settle_head(s, &dense->heads[i]);
    }
}

void space_dense_settle_for_pattern(Space *s, const Atom *pattern) {
    if (!s || s->native.dense_facts == 0u)
        return;
    if (pattern && pattern->kind == ATOM_EXPR && pattern->expr.len != 0u &&
        pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
        space_dense_settle_for_head(s, pattern->expr.elems[0]->sym_id,
                                    pattern->expr.len);
        return;
    }
    /* A symbol reads no fact a relation holds. */
    if (pattern && pattern->kind == ATOM_SYMBOL)
        return;
    space_dense_settle(s, SYMBOL_ID_NONE);
}

void space_dense_settle_for_head(Space *s, SymbolId head, CettaExprLen len) {
    if (!s || s->native.dense_facts == 0u)
        return;
    /* A pattern of other columns than the head's relation reads none of
     * its facts. */
    const SpaceDenseHead *entry = space_dense_find(s->native.dense, head);
    if (entry && entry->state == SPACE_DENSE_KEPT &&
        (CettaExprLen)entry->arity + 1u == len)
        space_dense_settle(s, head);
}

void space_dense_settle_for_patterns(Space *s, Atom *const *patterns,
                                     CettaExprLen count) {
    for (CettaExprLen i = 0u; patterns && i < count &&
             space_dense_facts(s) != 0u; i++)
        space_dense_settle_for_pattern(s, patterns[i]);
}

bool space_dense_form(const Space *s, SymbolId head, SpaceDenseForm *form) {
    SpaceDenseHead *entry =
        s ? space_dense_find(s->native.dense, head) : NULL;
    if (!entry || entry->state != SPACE_DENSE_KEPT ||
        cetta_shared_transition_scope_active())
        return false;
    *form = space_dense_head_form(entry);
    return true;
}

bool space_dense_meets_patterns(const Space *s, Atom *const *patterns,
                                size_t count) {
    for (size_t i = 0u; patterns && i < count && space_dense_facts(s) != 0u;
         i++) {
        const Atom *pattern = patterns[i];
        SpaceDenseForm form;
        if (pattern && pattern->kind == ATOM_EXPR && pattern->expr.len != 0u &&
            pattern->expr.elems[0]->kind == ATOM_SYMBOL &&
            space_dense_form(s, pattern->expr.elems[0]->sym_id, &form) &&
            (CettaExprLen)form.arity + 1u == pattern->expr.len)
            return true;
    }
    return false;
}

bool space_dense_patterns_miss(const Space *s, Atom *const *patterns,
                               size_t count) {
    if (space_dense_facts(s) == 0u)
        return true;
    /* A concurrent scope reads no relation, so it cannot tell. */
    if (cetta_shared_transition_scope_active())
        return false;
    for (size_t i = 0u; i < count; i++) {
        const Atom *pattern = patterns ? patterns[i] : NULL;
        if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
            pattern->expr.elems[0]->kind != ATOM_SYMBOL)
            return false;
        SpaceDenseForm form;
        if (space_dense_form(s, pattern->expr.elems[0]->sym_id, &form) &&
            (CettaExprLen)form.arity + 1u == pattern->expr.len)
            return false;
    }
    return true;
}

bool space_dense_count_columns(const Space *s, Atom *const *columns,
                               CettaExprLen len, CettaCount *count) {
    *count = 0u;
    if (cetta_shared_transition_scope_active() || !space_dense_fact_len(len))
        return false;
    const struct SpaceDense *dense = s ? s->native.dense : NULL;
    CettaDenseId fixed[CETTA_TUPLE_MAX_ARITY];
    bool held = dense && s->native.dense_facts != 0u;
    for (CettaExprLen c = 0u; c < len; c++) {
        Atom *column = columns[c];
        if (c != 0u)
            fixed[c - 1u] = CETTA_DENSE_ANY;
        if (column->kind == ATOM_VAR)
            continue;
        /* The relations compare columns by identity; a number other than an
         * integer may equal one by value, which the rows decide. */
        if (column->kind == ATOM_GROUNDED && column->ground.gkind != GV_INT &&
            column->ground.gkind != GV_BOOL &&
            column->ground.gkind != GV_STRING)
            return false;
        if (c == 0u || !held)
            continue;
        /* An atom the universe lacks is in no fact. */
        AtomId id = term_universe_lookup_atom_id(s->native.universe, column);
        if (id == CETTA_ATOM_ID_NONE)
            held = false;
        else
            fixed[c - 1u] = id;
    }
    for (uint32_t i = 0u; held && i < dense->len; i++) {
        SpaceDenseHead *head = &dense->heads[i];
        if (head->state != SPACE_DENSE_KEPT ||
            (CettaExprLen)head->arity + 1u != len ||
            (columns[0]->kind != ATOM_VAR &&
             (columns[0]->kind != ATOM_SYMBOL ||
              columns[0]->sym_id != head->head)))
            continue;
        *count += space_dense_form_count(space_dense_head_form(head), fixed);
    }
    return true;
}

const CettaDenseRelation *space_dense_relation(const Space *s,
                                               SymbolId head) {
    SpaceDenseForm form;
    return space_dense_form(s, head, &form) ? form.bits : NULL;
}

/* Whether the relation costs less than its facts would as rows. */
static bool space_dense_worth(const SpaceDenseHead *head) {
    return space_dense_kept_bytes(head) <=
           (space_dense_kept_facts(head) + 64u) * SPACE_DENSE_ROW_BYTES;
}

/* HEAD's facts go to its relation from now on, which holds its id.  Its
 * rows stay rows: moving them would shift the positions a reader walking
 * the rows holds, and the space is the same bag either way.  Its count of
 * rows stays, as the rows it keeps. */
static void space_dense_promote(Space *s, SpaceDenseHead *head) {
    if (space_dense_trace())
        fprintf(stderr, "space relations: %s takes %s of %u columns after %"
                PRIu64 " rows\n", symbol_bytes(g_symbols, head->head),
                space_dense_bits(head) ? "bits" : "a table",
                (unsigned)head->arity, (uint64_t)head->rows);
    head->state = SPACE_DENSE_KEPT;
    space_hold_row(s, head->head_id);
    space_dense_touch(s);
}

/* A fact added as a row: one more row of its head if the head keeps a
 * relation; a row whose head is open is noted as such. */
static void space_dense_note_row(Space *s, AtomId atom_id) {
    if (__builtin_expect(!s->native.dense, 1) || !s->native.universe)
        return;
    if (space_dense_row_head_open(s->native.universe, atom_id)) {
        s->native.dense->open_rows = true;
        return;
    }
    const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
    if (!hdr || hdr->tag != ATOM_EXPR ||
        !space_dense_fact_len(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) ||
        hdr->sym_or_head == SYMBOL_ID_NONE)
        return;
    SpaceDenseHead *head = space_dense_find(s->native.dense, hdr->sym_or_head);
    if (head && head->state == SPACE_DENSE_KEPT && head->rows != UINT64_MAX)
        head->rows++;
}

bool space_dense_head_rows_absent(Space *s, SymbolId head) {
    if (!s || !s->native.dense || s->native.dense->open_rows)
        return false;
    const SpaceDenseHead *entry = space_dense_find(s->native.dense, head);
    return entry && entry->state == SPACE_DENSE_KEPT && entry->rows == 0u;
}

static bool space_set_holds(Space *s, AtomId atom_id, Atom *atom);

/* HEAD's relation takes the fact of COLUMNS: true when the add is done,
 * false when the fact is to be a row (a multiset's second copy, or no
 * room, which gives the relation back). */
static bool space_dense_take(Space *s, SpaceDenseHead *head,
                             const AtomId *columns) {
    uint32_t numbered = space_dense_kept_numbered(head);
    bool oom = false;
    bool added;
    if (space_dense_bits(head)) {
        added = cetta_dense_relation_add(&head->bits, columns[0], columns[1],
                                         &oom);
    } else {
        CettaDenseId tuple[CETTA_TUPLE_MAX_ARITY];
        for (uint32_t c = 0u; c < head->arity; c++)
            tuple[c] = columns[c];
        added = cetta_tuple_table_add(&head->table, tuple, &oom);
    }
    space_dense_hold_numbered(s, head, numbered);
    if (oom) {
        space_dense_settle_head(s, head);
        return false;
    }
    /* A set holds the fact once; a multiset keeps the copy as a row. */
    if (!added)
        return s->kind == SPACE_KIND_SET;
    s->native.dense_facts++;
    space_publish_mutation(s, SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY,
                           SPACE_MUTATION_PREFIX_APPEND_ONLY);
    /* A relation whose columns outgrow its facts is worth more as rows. */
    if (space_dense_kept_numbered(head) != numbered && !space_dense_worth(head))
        space_dense_settle_head(s, head);
    return true;
}

void space_dense_prepare_adds(Space *s, const AtomId *ids, CettaCount count) {
    s = space_mutation_target(s);
    if (!s || !ids || count < space_dense_threshold() || !space_dense_admits(s))
        return;
    /* A batch is mostly runs of one head: the first fact of a run says
     * whether its head could take a relation, and the rest count by their
     * headers.  Promotion is policy only: a fact no relation can hold stays
     * a row whatever its head's state. */
    const TermUniverse *universe = s->native.universe;
    SpaceDenseHead *head = NULL;
    SymbolId run = SYMBOL_ID_NONE;
    for (CettaCount i = 0u; i < count; i++) {
        const CettaTermHdr *hdr = tu_hdr(universe, ids[i]);
        if (!hdr || hdr->tag != ATOM_EXPR ||
            !space_dense_fact_len(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK))
            continue;
        if (hdr->sym_or_head != run) {
            run = hdr->sym_or_head;
            SymbolId head_sym;
            AtomId head_id;
            AtomId columns[CETTA_TUPLE_MAX_ARITY];
            uint32_t arity = 0u;
            head = space_dense_fact(s, ids[i], &head_sym, &head_id, columns,
                                    &arity)
                ? space_dense_head(s, head_sym, head_id, arity) : NULL;
            /* Counting holds nothing: the head's id is a fact's of the
             * batch. */
            if (head && head->state == SPACE_DENSE_COUNTING)
                head->head_id = head_id;
        }
        if (head && head->state == SPACE_DENSE_COUNTING)
            head->batch++;
    }
    struct SpaceDense *dense = s->native.dense;
    for (uint32_t i = 0u; dense && i < dense->len; i++) {
        SpaceDenseHead *entry = &dense->heads[i];
        if (entry->batch != 0u && entry->state == SPACE_DENSE_COUNTING &&
            entry->rows + entry->batch >= space_dense_threshold())
            space_dense_promote(s, entry);
        entry->batch = 0u;
    }
}

/* Route an added row: true when a relation took the fact (or a set holds it
 * already), false when it is to be a row. */
static bool space_dense_route_add(Space *s, AtomId atom_id) {
    /* A row is always a right answer, so the fact's header is asked first:
     * most facts no relation could hold, or of the head last found to keep
     * rows, need no more. */
    const CettaTermHdr *hdr = s->native.universe
        ? tu_hdr(s->native.universe, atom_id) : NULL;
    if (!hdr || hdr->tag != ATOM_EXPR ||
        !space_dense_fact_len(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK))
        return false;
    SymbolId h = hdr->sym_or_head;
    if (h == SYMBOL_ID_NONE || h == s->native.dense_rows_head ||
        h == g_builtin_syms.equals || h == g_builtin_syms.equals_percent ||
        h == g_builtin_syms.colon || h == g_builtin_syms.subtype ||
        !space_dense_admits(s))
        return false;
    SymbolId head_sym;
    AtomId head_id;
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    uint32_t arity = 0u;
    if (!space_dense_fact(s, atom_id, &head_sym, &head_id, columns, &arity)) {
        /* An expression in a column: no relation can hold the fact. */
        s->native.dense_rows_head = h;
        return false;
    }
    SpaceDenseHead *head = space_dense_head(s, head_sym, head_id, arity);
    if (!head)
        return false;
    if (head->state == SPACE_DENSE_ROWS) {
        s->native.dense_rows_head = head_sym;
        return false;
    }
    if (head->state == SPACE_DENSE_COUNTING) {
        if (head->rows + 1u < space_dense_threshold()) {
            head->rows++;
            return false;
        }
        /* Counting holds nothing: the head's id is taken from this fact,
         * whose record holds it now.  The fact goes to the relation, so it
         * is not one of the rows the head keeps. */
        head->head_id = head_id;
        space_dense_promote(s, head);
        if (head->state != SPACE_DENSE_KEPT) {
            head->rows++;
            return false;
        }
    }
    /* A set holds a fact once, whether as a row or in its relation.  Asking
     * the rows may read the whole space, so the head is looked at again
     * after. */
    if (s->kind == SPACE_KIND_SET && space_set_holds(s, atom_id, NULL))
        return true;
    if (head->state != SPACE_DENSE_KEPT)
        return false;
    return space_dense_take(s, head, columns);
}

/* A column a relation can hold, given as an atom: as space_dense_column. */
static bool space_dense_column_atom(const Atom *atom) {
    return atom->kind == ATOM_SYMBOL ||
           (atom->kind == ATOM_GROUNDED &&
            (atom->ground.gkind == GV_INT || atom->ground.gkind == GV_BOOL ||
             atom->ground.gkind == GV_STRING));
}

/* Add ATOM by its parts when the space keeps its relation: a fact a
 * relation keeps needs no record of its own.  True when the relation took
 * it, or a set holds it already; false leaves the add to a record and the
 * rows. */
static bool space_dense_add_parts(Space *s, Atom *atom) {
    if (__builtin_expect(!s->native.dense, 1) || !atom ||
        atom->kind != ATOM_EXPR || !space_dense_fact_len(atom->expr.len) ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom->expr.elems[0]->sym_id == s->native.dense_rows_head)
        return false;
    SpaceDenseHead *head = space_dense_find(s->native.dense,
                                            atom->expr.elems[0]->sym_id);
    if (!head || head->state != SPACE_DENSE_KEPT ||
        (CettaExprLen)head->arity + 1u != atom->expr.len ||
        !space_dense_admits(s))
        return false;
    for (uint32_t c = 0u; c < head->arity; c++)
        if (!space_dense_column_atom(atom->expr.elems[c + 1u]))
            return false;
    TermUniverse *universe = s->native.universe;
    /* A row of a set holding the fact has a record of it. */
    if (s->kind == SPACE_KIND_SET) {
        AtomId row = term_universe_lookup_atom_id(universe, atom);
        if (row != CETTA_ATOM_ID_NONE && space_set_holds(s, row, NULL))
            return true;
        if (head->state != SPACE_DENSE_KEPT)
            return false;
    }
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t c = 0u; c < head->arity; c++) {
        columns[c] = term_universe_store_atom_id(universe, NULL,
                                                 atom->expr.elems[c + 1u]);
        if (columns[c] == CETTA_ATOM_ID_NONE)
            return false;
    }
    return space_dense_take(s, head, columns);
}

bool space_dense_may_hold(const Space *s, SymbolId head) {
    if (!space_dense_admits(s) || head == SYMBOL_ID_NONE ||
        head == g_builtin_syms.equals || head == g_builtin_syms.equals_percent ||
        head == g_builtin_syms.colon || head == g_builtin_syms.subtype)
        return false;
    const SpaceDenseHead *entry = space_dense_find(s->native.dense, head);
    return !entry || entry->state != SPACE_DENSE_ROWS;
}

bool space_add_dense_tuples(Space *s, SymbolId head, AtomId head_id,
                            uint32_t arity, const AtomId *tuples,
                            CettaCount count) {
    Space *target = space_mutation_target(s);
    if (!target || (count && !tuples) || !space_tracks_atom_ids(target) ||
        !space_dense_fact_len(arity + 1u))
        return false;
    TermUniverse *universe = target->native.universe;
    /* Facts enough to take a relation take it from the first. */
    SpaceDenseHead *counting = count >= space_dense_threshold() &&
            space_dense_admits(target)
        ? space_dense_head(target, head, head_id, arity) : NULL;
    if (counting && counting->state == SPACE_DENSE_COUNTING) {
        counting->head_id = head_id;
        space_dense_promote(target, counting);
    }
    for (CettaCount i = 0u; i < count; i++) {
        AtomId kids[CETTA_TUPLE_MAX_ARITY + 1u];
        kids[0] = head_id;
        bool columns = true;
        for (uint32_t c = 0u; c < arity; c++) {
            kids[c + 1u] = tuples[(size_t)arity * i + c];
            columns = columns && space_dense_column(universe, kids[c + 1u]);
        }
        /* Until the head takes its relation, the facts are rows, which
         * count toward it (space_dense_route_add). */
        SpaceDenseHead *entry = target->native.dense
            ? space_dense_find(target->native.dense, head) : NULL;
        if (entry && entry->state == SPACE_DENSE_KEPT &&
            entry->arity == arity && space_dense_admits(target) && columns) {
            /* A row of a set holding the fact has a record of it. */
            if (target->kind == SPACE_KIND_SET) {
                AtomId row = tu_expr_lookup_ids(universe, kids, arity + 1u);
                if (row != CETTA_ATOM_ID_NONE &&
                    space_set_holds(target, row, NULL))
                    continue;
            }
            if (entry->state == SPACE_DENSE_KEPT &&
                space_dense_take(target, entry, &kids[1]))
                continue;
        }
        AtomId id = tu_expr_from_ids(universe, kids, arity + 1u);
        if (id == CETTA_ATOM_ID_NONE)
            return false;
        space_add_stored_id(target, id, NULL);
    }
    return true;
}

/* Whether the relation of HEAD holds the fact of COLUMNS. */
static bool space_dense_kept_contains(const SpaceDenseHead *head,
                                      const AtomId *columns) {
    if (space_dense_bits(head))
        return cetta_dense_relation_contains(&head->bits, columns[0],
                                             columns[1]);
    CettaDenseId tuple[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t c = 0u; c < head->arity; c++)
        tuple[c] = columns[c];
    return cetta_tuple_table_contains(&head->table, tuple);
}

/* Take the fact of COLUMNS from HEAD's relation: true when it held it. */
static bool space_dense_kept_remove(SpaceDenseHead *head,
                                    const AtomId *columns) {
    if (space_dense_bits(head))
        return cetta_dense_relation_remove(&head->bits, columns[0],
                                           columns[1]);
    CettaDenseId tuple[CETTA_TUPLE_MAX_ARITY];
    for (uint32_t c = 0u; c < head->arity; c++)
        tuple[c] = columns[c];
    return cetta_tuple_table_remove(&head->table, tuple);
}

/* Whether the space holds the fact ATOM_ID in a relation. */
static bool space_dense_holds(const Space *s, AtomId atom_id) {
    if (!s || s->native.dense_facts == 0u)
        return false;
    SymbolId head_sym;
    AtomId head_id;
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    uint32_t arity = 0u;
    if (!space_dense_fact(s, atom_id, &head_sym, &head_id, columns, &arity))
        return false;
    const SpaceDenseHead *head = space_dense_find(s->native.dense, head_sym);
    return head && head->state == SPACE_DENSE_KEPT && head->arity == arity &&
           space_dense_kept_contains(head, columns);
}

/* The relation and columns of the ground fact (HEAD a1 ... an) given by its
 * arguments: a fact a relation holds may have no record of its own, but its
 * columns are numbered, so held.  False when no relation could hold it. */
static bool space_dense_parts(const Space *s, SymbolId head,
                              Atom *const *arguments, CettaExprLen count,
                              SpaceDenseHead **head_out, AtomId *columns) {
    if (!s || space_dense_facts(s) == 0u || head == SYMBOL_ID_NONE ||
        !arguments || !space_dense_fact_len(count + 1u))
        return false;
    SpaceDenseHead *entry = space_dense_find(s->native.dense, head);
    if (!entry || entry->state != SPACE_DENSE_KEPT ||
        (CettaExprLen)entry->arity != count)
        return false;
    for (uint32_t c = 0u; c < entry->arity; c++) {
        Atom *column = arguments[c];
        if (!column || atom_has_vars(column))
            return false;
        columns[c] = term_universe_lookup_atom_id(s->native.universe, column);
        if (columns[c] == CETTA_ATOM_ID_NONE)
            return false;
    }
    *head_out = entry;
    return true;
}

static bool space_dense_atom_parts(const Space *s, Atom *atom,
                                   SpaceDenseHead **head_out,
                                   AtomId *columns) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len != 0u &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           space_dense_parts(s, atom->expr.elems[0]->sym_id,
                             atom->expr.elems + 1, atom->expr.len - 1u,
                             head_out, columns);
}

/* Whether a relation holds ATOM. */
static bool space_dense_holds_atom(const Space *s, Atom *atom) {
    SpaceDenseHead *head = NULL;
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    return space_dense_atom_parts(s, atom, &head, columns) &&
           space_dense_kept_contains(head, columns);
}

/* Take the ground fact ATOM from its relation: 1 when it held it, 0
 * otherwise. */
CettaCount space_dense_remove_ground(Space *s, Atom *atom) {
    SpaceDenseHead *head = NULL;
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    if (!space_dense_atom_parts(s, atom, &head, columns) ||
        !space_dense_kept_remove(head, columns))
        return 0u;
    s->native.dense_facts--;
    space_note_released(s, 1u);
    space_publish_mutation(s, SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY,
                           SPACE_MUTATION_PREFIX_REWRITTEN);
    return 1u;
}

/* Remove ATOM_ID from its relation: true when it held it.  A copy kept as
 * a row is just as good to remove; either leaves the same bag. */
static bool space_dense_route_remove(Space *s, AtomId atom_id) {
    if (s->native.dense_facts == 0u)
        return false;
    SymbolId head_sym;
    AtomId head_id;
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    uint32_t arity = 0u;
    if (!space_dense_fact(s, atom_id, &head_sym, &head_id, columns, &arity))
        return false;
    SpaceDenseHead *head = space_dense_find(s->native.dense, head_sym);
    if (!head || head->state != SPACE_DENSE_KEPT || head->arity != arity ||
        !space_dense_kept_remove(head, columns))
        return false;
    s->native.dense_facts--;
    space_note_released(s, 1u);
    space_publish_mutation(s, SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY,
                           SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

void space_dense_reading_begin(void) {
    g_space_dense_reading++;
}

void space_dense_reading_end(void) {
    g_space_dense_reading--;
}

/* Free the relations with the space: their references go. */
static void space_dense_free(Space *s) {
    struct SpaceDense *dense = s->native.dense;
    if (!dense)
        return;
    for (uint32_t i = 0u; i < dense->len; i++) {
        SpaceDenseHead *head = &dense->heads[i];
        if (head->state == SPACE_DENSE_KEPT) {
            space_dense_kept_release(s, head);
            term_universe_release(s->native.universe, head->head_id);
        }
        space_dense_kept_free(head);
    }
    free(dense->heads);
    free(dense->index);
    free(dense);
    s->native.dense = NULL;
    s->native.dense_facts = 0u;
    space_dense_touch(s);
}

/* ── Removal marks ───────────────────────────────────────────────────────── */

/* A removed data row is marked instead of contracted at once.  The marks
 * cover rows [0, mark_rows); rows appended later are live and enter the
 * Fenwick tree when a mark reaches them.  mark_tree is 1-based over the
 * bitmap's 64-row words and counts each word's live rows, so it takes a
 * word where one counter per row would take 64. */

static CettaIndex space_marks_tree_prefix(const SpaceNativeStorage *n,
                                          CettaIndex rows) {
    return space_marks_live_before(n->marks, n->mark_tree, rows);
}

/* Add `delta` live rows to word `word`. */
static void space_marks_tree_add(SpaceNativeStorage *n, CettaIndex word,
                                 CettaIndex delta) {
    CettaIndex words = (n->mark_rows + 63u) >> 6;
    for (CettaIndex i = word + 1u; i <= words; i += space_mark_lowbit(i))
        n->mark_tree[i] += delta;
}

static bool space_marks_row_marked(const SpaceNativeStorage *n,
                                   CettaIndex row) {
    return row < n->mark_rows &&
           ((n->marks[row >> 6] >> (row & 63u)) & 1u) != 0u;
}

static void space_marks_reserve(Space *s, CettaIndex rows) {
    SpaceNativeStorage *n = &s->native;
    if (rows <= n->mark_cap)
        return;
    CettaIndex cap = n->mark_cap ? n->mark_cap : 64u;
    while (cap < rows)
        cap *= 2u;
    size_t old_words = ((size_t)n->mark_cap + 63u) / 64u;
    size_t words = ((size_t)cap + 63u) / 64u;
    n->marks = cetta_realloc(n->marks, words * sizeof(*n->marks));
    memset(n->marks + old_words, 0, (words - old_words) * sizeof(*n->marks));
    n->mark_tree = cetta_realloc(n->mark_tree,
                                 (words + 1u) * sizeof(*n->mark_tree));
    n->mark_cap = cap;
}

/* Bring rows [mark_rows, rows) into the tree as live rows: their bits are
 * clear, so each word they reach gains them. */
static void space_marks_cover(Space *s, CettaIndex rows) {
    SpaceNativeStorage *n = &s->native;
    if (rows <= n->mark_rows)
        return;
    space_marks_reserve(s, rows);
    CettaIndex old_words = (n->mark_rows + 63u) >> 6;
    CettaIndex words = (rows + 63u) >> 6;
    if (words - old_words > 64u) {
        /* Rebuild: each word's own count, then fold it into its parent. */
        for (CettaIndex i = 1u; i <= words; i++)
            n->mark_tree[i] = space_marks_bits(
                ~n->marks[i - 1u] & space_marks_word_rows(rows, i - 1u));
        for (CettaIndex i = 1u; i <= words; i++) {
            CettaIndex parent = i + space_mark_lowbit(i);
            if (parent <= words)
                n->mark_tree[parent] += n->mark_tree[i];
        }
        n->mark_rows = rows;
        return;
    }
    /* The rows that fall in the last word already in the tree. */
    if (old_words != 0u && (n->mark_rows & 63u) != 0u) {
        CettaIndex end = rows < old_words * 64u ? rows : old_words * 64u;
        space_marks_tree_add(n, old_words - 1u, end - n->mark_rows);
    }
    /* Each new word enters with its rows: its own count, plus the counts
     * of the words its node spans before it. */
    for (CettaIndex i = old_words + 1u; i <= words; i++) {
        CettaIndex own = space_marks_bits(
            ~n->marks[i - 1u] & space_marks_word_rows(rows, i - 1u));
        n->mark_tree[i] = own + space_marks_live_before(
                                    n->marks, n->mark_tree, (i - 1u) * 64u) -
            space_marks_live_before(n->marks, n->mark_tree,
                                    (i - space_mark_lowbit(i)) * 64u);
    }
    n->mark_rows = rows;
}

static void space_marks_reset(Space *s) {
    SpaceNativeStorage *n = &s->native;
    if (n->marks && n->mark_rows)
        memset(n->marks, 0,
               (((size_t)n->mark_rows + 63u) / 64u) * sizeof(*n->marks));
    n->marked = 0u;
    n->mark_rows = 0u;
}

static void space_marks_free(Space *s) {
    SpaceNativeStorage *n = &s->native;
    free(n->marks);
    free(n->mark_tree);
    n->marks = NULL;
    n->mark_tree = NULL;
    n->marked = 0u;
    n->mark_rows = 0u;
    n->mark_cap = 0u;
}

static void space_marks_mark(Space *s, CettaIndex row) {
    SpaceNativeStorage *n = &s->native;
    space_marks_cover(s, row + 1u);
    n->marks[row >> 6] |= (uint64_t)1u << (row & 63u);
    space_marks_tree_add(n, row >> 6, (CettaIndex)-1);
    n->marked++;
}

/* The row holding live logical position `logical`. */
CettaIndex space_marks_select(const Space *s, CettaIndex logical) {
    const SpaceNativeStorage *n = &s->native;
    SpaceViewMarks marks = {n->marks, n->mark_tree, n->mark_rows, n->marked};
    return space_view_row(&marks, logical);
}

/* Keep the live rows of an ascending row list, as logical positions. */
CettaIndex space_marks_physical_to_logical(const Space *s, CettaIndex *rows,
                                           CettaIndex count) {
    const SpaceNativeStorage *n = &s->native;
    if (n->marked == 0u || !rows)
        return count;
    CettaIndex kept = 0u;
    CettaIndex live_in_tree = n->mark_rows - n->marked;
    for (CettaIndex i = 0u; i < count; i++) {
        CettaIndex row = rows[i];
        if (space_marks_row_marked(n, row))
            continue;
        rows[kept++] = row < n->mark_rows
            ? space_marks_tree_prefix(n, row)
            : live_in_tree + (row - n->mark_rows);
    }
    return kept;
}

/* Contract every marked row away in one pass and carry the indexes once. */
void space_settle_marks_slow(Space *s) {
    if (!space_has_marks(s))
        return;
    CETTA_SCOPED_SHARED_TRANSITION(transition);
    /* Pinned readers keep the rows and marks they began with. */
    space_pinned_occurrences_detach(s);
    SpaceNativeStorage *n = &s->native;
    CettaIndex len = n->len;
    uint8_t *mask = cetta_malloc(len ? (size_t)len : 1u);
    for (CettaIndex row = 0u; row < len; row++)
        mask[row] = space_marks_row_marked(n, row) ? 1u : 0u;
    CettaIndex marked = n->marked;
    space_marks_reset(s);
    CettaCount removed = 0u;
    bool contracted = space_contract_occurrence_mask(
        s, mask, len, &removed, true);
    if (!contracted || removed != marked) {
        fprintf(stderr, "CeTTa: marked rows could not be contracted (ok=%d removed=%llu marked=%llu len=%llu live_after=%llu)\n",
                (int)contracted, (unsigned long long)removed, (unsigned long long)marked,
                (unsigned long long)len, (unsigned long long)space_length64(s));
        abort();
    }
    free(mask);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_MARK_SETTLE);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_SPACE_MARK_SETTLE_ROW, len);
}

/* Remove the rows at ascending live logical positions by marking them.
 * False, with nothing removed, when this space or a row is not admitted:
 * the caller then contracts at once.  Admitted are plain native spaces
 * whose removed rows are data, outside the equation and declaration
 * projections. */
bool space_remove_logical_positions(Space *s, const CettaIndex *positions,
                                    CettaCount count) {
    if (!s || (count && !positions))
        return false;
    if (count == 0u)
        return true;
    if (space_has_overlay_base(s) || s->module_own_view ||
        (s->kind != SPACE_KIND_ATOM && s->kind != SPACE_KIND_SET) ||
        s->imported_len != 0u ||
        space_has_dependencies(s) || !stable_occurrence_transport_enabled() ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !s->native.atom_ids || s->native.start != 0u)
        return false;
    /* A position past the rows names a fact kept in a relation: the
     * relations become rows, at the positions their facts had. */
    if (space_dense_facts(s) != 0u &&
        positions[count - 1u] >= space_length64(s) - space_dense_facts(s))
        space_dense_settle(s, SYMBOL_ID_NONE);
    /* Pinned readers keep the rows and marks they began with. */
    space_pinned_occurrences_detach(s);
    CettaCount live = space_length64(s);
    CettaIndex *rows = cetta_malloc((size_t)count * sizeof(*rows));
    for (CettaCount i = 0u; i < count; i++) {
        if (positions[i] >= live || (i && positions[i] <= positions[i - 1u])) {
            free(rows);
            return false;
        }
        rows[i] = space_marks_select(s, positions[i]);
        AtomId atom_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, rows[i]);
        if (space_atom_equation_projection(s, atom_id, NULL) !=
                SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY) {
            free(rows);
            return false;
        }
    }
    SpaceMatchNativeState *native_state = &s->match_backend.native;
    for (CettaCount i = 0u; i < count; i++) {
        space_marks_mark(s, rows[i]);
        /* The row leaves the match trie and the exact index now, so lookups
         * do not meet it; the position indexes keep it until the marks
         * settle, and their reader drops marked rows. */
        AtomId atom_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, rows[i]);
        term_universe_release(s->native.universe, atom_id);
        if (native_state->match_trie && !native_state->match_trie_dirty)
            (void)disc_remove_id(native_state->match_trie,
                                 s->native.universe, atom_id, rows[i]);
        if (!s->native.exact_idx_dirty &&
            !exact_index_forget_row(s, atom_id, rows[i]))
            s->native.exact_idx_dirty = true;
        space_path_indexes_note_mark(s, rows[i], atom_id);
    }
    free(rows);
    s->native.has_non_exact_atoms_dirty = true;
    s->native.id_present_dirty = true;
    uint64_t followed_prefix = s->prefix_epoch;
    space_publish_mutation(s, SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY,
                           SPACE_MUTATION_PREFIX_REWRITTEN);
    space_path_indexes_adopt_prefix_epoch(s, followed_prefix);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_SPACE_MARKED_ROW, count);
    space_note_released(s, count);
    /* Settle when marked rows reach the live ones: a pass then reads at most
     * twice the rows it removes. */
    if (s->native.marked >= s->native.len - s->native.marked)
        space_settle_marks(s);
    return true;
}

void space_linearize(Space *s) {
    size_t width = 0;
    if (!space_is_queue(s) || s->native.start == 0)
        return;
    if (s->native.len > 0) {
        width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        memmove(s->native.atom_ids,
                s->native.atom_ids + ((size_t)s->native.start * width),
                width * (size_t)s->native.len);
    }
    s->native.start = 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUEUE_COMPACT);
}

static void space_queue_reserve_tail(Space *s, CettaIndex extra) {
    CettaIndex tail = s->native.start + s->native.len;
    if (tail + extra <= s->native.cap)
        return;
    if (s->native.start > 0) {
        space_linearize(s);
        tail = s->native.len;
        if (tail + extra <= s->native.cap)
            return;
    }
    space_reserve_linear(s, s->native.len + extra);
}

const char *space_kind_name(SpaceKind kind) {
    switch (kind) {
    case SPACE_KIND_ATOM:
        return "atom";
    case SPACE_KIND_STACK:
        return "stack";
    case SPACE_KIND_QUEUE:
        return "queue";
    case SPACE_KIND_HASH:
        return "hash";
    case SPACE_KIND_SET:
        return "set";
    }
    return "unknown";
}

bool space_kind_from_name(const char *name, SpaceKind *out) {
    if (!name || !out) return false;
    if (strcmp(name, "atom") == 0) {
        *out = SPACE_KIND_ATOM;
        return true;
    }
    if (strcmp(name, "stack") == 0) {
        *out = SPACE_KIND_STACK;
        return true;
    }
    if (strcmp(name, "queue") == 0) {
        *out = SPACE_KIND_QUEUE;
        return true;
    }
    if (strcmp(name, "hash") == 0) {
        *out = SPACE_KIND_HASH;
        return true;
    }
    if (strcmp(name, "set") == 0) {
        *out = SPACE_KIND_SET;
        return true;
    }
    return false;
}

/* Make S strict or full.  A strict space other than a hash space keeps no
 * exact index: no add notes a row in one, and no reader builds one. */
void space_set_strict(Space *s, bool strict) {
    if (!s)
        return;
    s->strict = strict;
    if (!space_keeps_exact_index(s))
        s->native.exact_idx_dirty = true;
}

bool space_is_ordered(const Space *s) {
    return s && (s->kind == SPACE_KIND_STACK || s->kind == SPACE_KIND_QUEUE);
}

bool space_is_stack(const Space *s) {
    return s && s->kind == SPACE_KIND_STACK;
}

bool space_is_queue(const Space *s) {
    return s && s->kind == SPACE_KIND_QUEUE;
}

bool space_is_hash(const Space *s) {
    return s && s->kind == SPACE_KIND_HASH;
}

bool space_is_set(const Space *s) {
    return s && s->kind == SPACE_KIND_SET;
}

static void space_mark_secondary_indexes_dirty(Space *s) {
    if (!s)
        return;
    s->native.eq_idx_dirty = true;
    s->native.ty_idx_dirty = true;
    s->native.exact_idx_dirty = true;
    s->native.has_non_exact_atoms_dirty = true;
}

void space_mark_derived_state_dirty(Space *s) {
    if (!s)
        return;
    space_mark_secondary_indexes_dirty(s);
    /* The presence bitset is add-monotone: incremental add paths keep it current
       and do NOT route through here, so any caller of this helper (removal, bulk
       replace, backend-direct store, external mutation) may have
       invalidated a bit -- flag a recompute-on-next-membership-query. */
    s->native.id_present_dirty = true;
}

static void space_mark_indexes_dirty(Space *s) {
    space_mark_derived_state_dirty(s);
}

static void space_clear_native_logical_view(Space *s) {
    if (!s)
        return;
    /* Pinned readers keep the rows and marks they began with. */
    space_pinned_occurrences_detach(s);
    space_release_rows(s, 0u, s->native.len);
    space_marks_reset(s);
    free(s->native.atom_ids);
    s->native.atom_ids = NULL;
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.has_non_exact_atoms = false;
}

void space_discard_native_logical_view(Space *s) {
    if (!s)
        return;
    space_clear_native_logical_view(s);
    space_mark_derived_state_dirty(s);
}

void space_discard_native_logical_view_preserving_dispatch(Space *s) {
    if (!s)
        return;
    space_clear_native_logical_view(s);
    s->native.exact_idx_dirty = true;
    s->native.has_non_exact_atoms_dirty = true;
    s->native.id_present_dirty = true;
}

static bool space_atom_form_is(Atom *atom, SymbolId head,
                               CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity &&
           atom_is_symbol_id(atom->expr.elems[0], head);
}

static bool space_atom_id_form_is(const Space *s, AtomId atom_id,
                                  Atom *atom, SymbolId head,
                                  CettaExprLen arity) {
    if (s && s->native.universe && atom_id != CETTA_ATOM_ID_NONE) {
        const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
        if (hdr) {
            return tu_kind(s->native.universe, atom_id) == ATOM_EXPR &&
                   tu_arity(s->native.universe, atom_id) == arity &&
                   tu_head_sym(s->native.universe, atom_id) == head;
        }
        if (!atom)
            atom = term_universe_get_atom(s->native.universe, atom_id);
    }
    return space_atom_form_is(atom, head, arity);
}

static SpaceMutationEquationProjection
space_atom_equation_projection(const Space *s, AtomId atom_id, Atom *atom) {
    if (s && s->native.universe && atom_id != CETTA_ATOM_ID_NONE) {
        const CettaTermHdr *header = tu_hdr(s->native.universe, atom_id);
        if (header) {
            if (tu_kind(s->native.universe, atom_id) == ATOM_EXPR &&
                tu_arity(s->native.universe, atom_id) == 3u) {
                SymbolId head = tu_head_sym(s->native.universe, atom_id);
                if (head == g_builtin_syms.equals || head == g_builtin_syms.equals_percent)
                    return SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT;
                if (head == g_builtin_syms.colon)
                    return SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION;
                if (head == g_builtin_syms.subtype)
                    return SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
                        SPACE_MUTATION_EQUATION_PROJECTION_SUBTYPE;
            }
            return SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY;
        }
        if (!atom)
            atom = term_universe_get_atom(s->native.universe, atom_id);
    }
    if (atom) {
        if (space_atom_form_is(atom, g_builtin_syms.equals, 3u) ||
            space_atom_form_is(atom, g_builtin_syms.equals_percent, 3u))
            return SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT;
        if (space_atom_form_is(atom, g_builtin_syms.colon, 3u))
            return SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION;
        if (space_atom_form_is(atom, g_builtin_syms.subtype, 3u))
            return SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
                SPACE_MUTATION_EQUATION_PROJECTION_SUBTYPE;
        return SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY;
    }
    return SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE;
}

static SpaceMutationEquationProjection space_merge_equation_projection(
        SpaceMutationEquationProjection left,
        SpaceMutationEquationProjection right) {
    return (SpaceMutationEquationProjection)(left | right);
}

static SpaceMutationEquationProjection
space_local_removal_equation_projection(const Space *s, CettaIndex raw_idx) {
    if (!s || raw_idx >= s->native.len)
        return SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE;
    AtomId removed_id = space_local_get_atom_id_at64(s, raw_idx);
    SpaceMutationEquationProjection result =
        space_atom_equation_projection(s, removed_id, NULL);
    if (!space_is_ordered(s) && raw_idx + 1u < s->native.len) {
        CettaIndex tail = s->native.len - 1u;
        result = space_merge_equation_projection(
            result,
            space_atom_equation_projection(
                s, space_local_get_atom_id_at64(s, tail),
                NULL));
    }
    return result;
}

typedef struct {
    bool equations;
    bool declarations;
} SpaceDispatchRetention;

static bool space_transport_dispatch_bucket(
        CettaIndex *positions, CettaCount count,
        CettaCount source_len, CettaCount target_len,
        const CettaIndex *source_to_target, CettaIndex erased) {
    for (CettaIndex i = 0u; i < count; i++) {
        CettaIndex source = positions[i];
        if (source >= source_len || (!source_to_target && source == erased))
            return false;
        CettaIndex target = source_to_target ? source_to_target[source]
            : source - (source > erased);
        if (target >= target_len)
            return false;
        positions[i] = target;
    }
    return true;
}

/* Each index is retained only when its own row projection is unchanged. For a
 * single unordered erasure the classification includes the relocated tail.
 * Type exclusion also needs the absence of open rows; deletion preserves that
 * negative certificate, but a positive flag requires a rebuild to update it.
 * Only logical coordinates move. Bucket-local trie positions, ordered
 * payloads and declaration-subject metadata remain unchanged. */
static SpaceDispatchRetention space_transport_dispatch_coordinates(
        Space *s, SpaceMutationEquationProjection projection,
        const SpaceStableOccurrenceTransport *transport, CettaIndex erased) {
    SpaceDispatchRetention retained = {0};
    if (space_has_dependencies(s) || space_has_overlay_base(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return retained;
    retained.equations = !s->native.eq_idx_dirty &&
        !(projection & (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
                        SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE));
    retained.declarations = !s->native.ty_idx_dirty && !s->native.ty_idx.has_open_rows &&
        !(projection & (SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
                        SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE));
    if ((!retained.equations && !retained.declarations) ||
        (!transport && !space_is_ordered(s)))
        return retained;
    CettaCount source_len = transport ? transport->source_len : s->native.len;
    CettaCount target_len = transport ? transport->target_len : source_len - 1u;
    const CettaIndex *coordinates = transport ? transport->source_to_target : NULL;
    if (retained.equations) {
        for (uint32_t i = 0u; i <= EQ_INDEX_BUCKETS; i++) {
            EqBucket *bucket = i == EQ_INDEX_BUCKETS
                ? &s->native.eq_idx.wildcard : &s->native.eq_idx.buckets[i];
            if (!space_transport_dispatch_bucket(
                    bucket->atom_indices, bucket->len, source_len, target_len,
                    coordinates, erased)) {
                retained.equations = false;
                break;
            }
        }
        if (retained.equations &&
            !space_transport_dispatch_bucket(
                s->native.eq_idx.head_rows.atom_indices,
                s->native.eq_idx.head_rows.len, source_len, target_len,
                coordinates, erased))
            retained.equations = false;
    }
    if (retained.declarations &&
        !space_transport_dispatch_bucket(
            s->native.ty_idx.rows.atom_indices, s->native.ty_idx.rows.len, source_len,
            target_len, coordinates, erased))
        retained.declarations = false;
    return retained;
}

static SpaceMutationEquationProjection
space_logical_removed_range_equation_projection(
        const Space *s, CettaIndex begin, CettaCount end) {
    if (!s || begin > end || end > space_length64(s))
        return SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE;
    SpaceMutationEquationProjection result =
        SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY;
    for (CettaIndex index = begin; index < end; index++) {
        result = space_merge_equation_projection(
            result,
            space_atom_equation_projection(
                s, space_get_atom_id_at64(s, index),
                space_get_at64(s, index)));
        if ((result & SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE) ||
            ((result & SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT) &&
             (result & SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION)))
            break;
    }
    return result;
}

bool space_atom_id_requires_authored_order(const Space *s,
                                           AtomId atom_id,
                                           Atom *atom) {
    return space_atom_id_form_is(
               s, atom_id, atom, g_builtin_syms.equals, 3u) ||
           space_atom_id_form_is(
               s, atom_id, atom, g_builtin_syms.equals_percent, 3u) ||
           space_atom_id_form_is(
               s, atom_id, atom, g_builtin_syms.colon, 3u);
}

/* The counted PathMap remains the occurrence authority. Its full tuple view
 * is disposable, while these two dispatch descriptors are stable across a
 * mutation that cannot add or remove their row class. */
static void space_mark_backend_primary_atom_mutation(
    Space *s, AtomId atom_id, Atom *atom) {
    if (!s)
        return;
    if (space_atom_id_form_is(s, atom_id, atom, g_builtin_syms.equals, 3u) ||
        space_atom_id_form_is(s, atom_id, atom, g_builtin_syms.equals_percent, 3u))
        s->native.eq_idx_dirty = true;
    if (space_atom_id_form_is(s, atom_id, atom, g_builtin_syms.colon, 3u))
        s->native.ty_idx_dirty = true;
    s->native.exact_idx_dirty = true;
    s->native.has_non_exact_atoms_dirty = true;
    s->native.id_present_dirty = true;
}

/* A backend-primary PathMap can retain the exact source AtomId occurrence
 * sequence when the bridge mutation was expressed in those same ids.  This
 * is only an observation shadow: the counted bridge remains authoritative.
 * Opaque bridge/text mutations invalidate the shadow in the backend instead. */
static void space_append_backend_primary_shadow_id(Space *s,
                                                   AtomId atom_id) {
    ImportedBridgeState *st;
    CettaIndex atom_idx;
    if (!s || atom_id == CETTA_ATOM_ID_NONE ||
        s->match_backend.kind != SPACE_ENGINE_PATHMAP)
        return;
    st = &s->match_backend.pathmap.bridge;
    if (!st->native_shadow_synced)
        return;
    atom_idx = s->native.len;
    space_reserve_linear(s, s->native.len + 1u);
    (void)space_atom_id_storage_store_at(
        s->native.atom_ids, s->native.atom_id_width_bits,
        atom_idx, atom_id);
    s->native.len++;
    space_hold_row(s, atom_id);
    space_match_backend_note_native_shadow_add(s, atom_id, atom_idx);
    space_note_atom_id_storage_peak(s);
}

static bool space_store_via_backend_primary(Space *s, AtomId atom_id,
                                            Atom *atom,
                                            bool *out_transition_failed) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (out_transition_failed)
        *out_transition_failed = false;
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        space_atom_id_requires_authored_order(s, atom_id, atom)) {
        if (!space_match_backend_require_logical_order(s, NULL) &&
            out_transition_failed)
            *out_transition_failed = true;
        return false;
    }
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_id_is_exact_indexable(s, atom_id);
    if (!space_match_backend_store_atom_id_direct(s, atom_id, atom))
        return false;
    space_append_backend_primary_shadow_id(s, atom_id);
    space_mark_backend_primary_atom_mutation(s, atom_id, atom);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms =
            had_non_exact_atom || !atom_exact_indexable;
        s->native.has_non_exact_atoms_dirty = false;
    }
    space_publish_mutation(
        s, space_atom_equation_projection(s, atom_id, atom),
        SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

static bool space_store_atom_via_backend_primary(
    Space *s, Atom *atom, bool *out_transition_failed) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (out_transition_failed)
        *out_transition_failed = false;
    if (!s || !atom)
        return false;
    /* A PathMap-only space should not pay for a complete native AtomId view
       before any native candidate consumer asks for one.  Once that derived
       view exists, keep using the AtomId route so later inserts extend its
       indexes incrementally instead of invalidating and rebuilding them. */
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        space_tracks_atom_ids(s) && term_universe_atom_is_stable(atom) &&
        s->native.len > 0u &&
        s->match_backend.pathmap.bridge.native_shadow_synced)
        return false;
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        (space_atom_form_is(atom, g_builtin_syms.equals, 3u) ||
         space_atom_form_is(atom, g_builtin_syms.colon, 3u))) {
        if (!space_match_backend_require_logical_order(s, NULL) &&
            out_transition_failed)
            *out_transition_failed = true;
        return false;
    }
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_is_exact_indexable(atom);
    if (!space_match_backend_store_atom_direct(s, atom))
        return false;
    space_mark_backend_primary_atom_mutation(
        s, CETTA_ATOM_ID_NONE, atom);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms =
            had_non_exact_atom || !atom_exact_indexable;
        s->native.has_non_exact_atoms_dirty = false;
    }
    space_publish_mutation(
        s, space_atom_equation_projection(
               s, CETTA_ATOM_ID_NONE, atom),
        SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

static bool space_remove_via_backend_primary(Space *s, AtomId atom_id) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_id_is_exact_indexable(s, atom_id);
    if (!space_match_backend_remove_atom_id_direct(s, atom_id))
        return false;
    space_mark_backend_primary_atom_mutation(s, atom_id, NULL);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms = had_non_exact_atom;
        s->native.has_non_exact_atoms_dirty =
            had_non_exact_atom && !atom_exact_indexable;
    }
    space_publish_mutation(
        s, space_atom_equation_projection(s, atom_id, NULL),
        SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

static bool space_remove_atom_via_backend_primary(Space *s, Atom *atom) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (!s || !atom)
        return false;
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_is_exact_indexable(atom);
    if (!space_match_backend_remove_atom_direct(s, atom))
        return false;
    space_mark_backend_primary_atom_mutation(
        s, CETTA_ATOM_ID_NONE, atom);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms = had_non_exact_atom;
        s->native.has_non_exact_atoms_dirty =
            had_non_exact_atom && !atom_exact_indexable;
    }
    space_publish_mutation(
        s, space_atom_equation_projection(
               s, CETTA_ATOM_ID_NONE, atom),
        SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

static bool space_truncate_via_backend_primary64(Space *s, CettaCount new_len) {
    if (!s)
        return false;
    CettaCount old_len = space_length64(s);
    if (new_len > old_len)
        return false;
    SpaceMutationEquationProjection equation_projection =
        space_logical_removed_range_equation_projection(
            s, new_len, old_len);
    if (!space_match_backend_truncate_direct64(s, new_len))
        return false;
    space_mark_indexes_dirty(s);
    space_publish_mutation(
        s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

static bool space_truncate_via_backend_primary(Space *s, uint32_t new_len) {
    return space_truncate_via_backend_primary64(s, new_len);
}

static void ensure_eq_index(Space *s) {
    if (s && s->native.eq_idx_dirty) {
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return;
        eq_index_rebuild(s);
    }
}

static void ensure_ty_ann_index(Space *s) {
    if (s && s->native.ty_idx_dirty) {
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return;
        ty_ann_index_rebuild(s);
    }
}

static void ensure_exact_index(Space *s) {
    if (s && s->native.exact_idx_dirty && space_keeps_exact_index(s))
        exact_index_rebuild(s);
}

static bool ensure_has_non_exact_atoms(Space *s) {
    if (!s)
        return false;
    if (s->native.has_non_exact_atoms_dirty) {
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return false;
        recompute_has_non_exact_atoms(s);
    }
    return !s->native.has_non_exact_atoms_dirty;
}

void space_begin_secondary_index_deferral(Space *s) {
    if (!s)
        return;
    s->native.secondary_index_deferral_depth++;
    /* Deferral postpones only the query-oriented secondary indexes.  It does
       not mutate membership, so invalidating the alpha-presence projection
       here would turn every deferral window into an unnecessary full rescan. */
    space_mark_secondary_indexes_dirty(s);
}

void space_end_secondary_index_deferral(Space *s) {
    if (!s || s->native.secondary_index_deferral_depth == 0)
        return;
    s->native.secondary_index_deferral_depth--;
}

static bool space_sync_exact_membership_from_backend(Space *s) {
    if (!s || !space_engine_uses_pathmap(s->match_backend.kind))
        return true;
    if (!s->native.exact_idx_dirty)
        return true;
    Arena scratch;
    arena_init(&scratch);
    bool ok = space_match_backend_materialize_native_storage(s, &scratch);
    arena_free(&scratch);
    return ok;
}

static void space_native_storage_init_empty(Space *s, TermUniverse *universe) {
    TermUniverse *resolved = NULL;
    if (!s)
        return;
    resolved = universe ? universe : space_default_universe();
    s->native.atom_ids = NULL;
    s->native.atom_id_width_bits =
        (uint8_t)space_atom_id_width_bits_for_universe(resolved);
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.universe = resolved;
    eq_index_init(&s->native.eq_idx);
    ty_ann_index_init(&s->native.ty_idx);
    exact_index_init(&s->native.exact_idx);
    s->native.id_present = NULL;
    s->native.id_present_bits = 0;
    s->native.id_present_synced_len = 0;
    s->native.id_present_dirty = false;
    s->native.id_present_keys = NULL;
    s->native.id_present_key_len = 0u;
    s->native.id_present_key_cap = 0u;
    s->native.dense = NULL;
    s->native.dense_facts = 0u;
    s->native.dense_epoch = 0u;
    s->native.dense_rows_head = SYMBOL_ID_NONE;
    s->native.eq_idx_dirty = false;
    s->native.ty_idx_dirty = false;
    /* The exact index is built by its first reader and kept current from
     * then on: a space nobody reads exactly pays nothing for it. */
    s->native.exact_idx_dirty = true;
    s->native.has_non_exact_atoms = false;
    s->native.has_non_exact_atoms_dirty = false;
    s->native.secondary_index_deferral_depth = 0;
    s->native.marks = NULL;
    s->native.mark_tree = NULL;
    s->native.marked = 0;
    s->native.mark_rows = 0;
    s->native.mark_cap = 0;
    s->overlay_base = NULL;
    s->module_own_view = false;
    s->overlay_base_visible_len = 0;
    s->overlay_removed_base_indices = NULL;
    s->overlay_removed_base_len = 0;
    s->overlay_removed_base_cap = 0;
}

static void space_move_storage_and_backend(Space *dst, Space *src) {
    if (!dst || !src)
        return;
    dst->native = src->native;
    dst->kind = src->kind;
    dst->strict = src->strict;
    dst->module_own_view = src->module_own_view;
    dst->match_backend = src->match_backend;
    dst->overlay_base = src->overlay_base;
    dst->overlay_base_visible_len = src->overlay_base_visible_len;
    dst->overlay_removed_base_indices = src->overlay_removed_base_indices;
    dst->overlay_removed_base_len = src->overlay_removed_base_len;
    dst->overlay_removed_base_cap = src->overlay_removed_base_cap;
    dst->payload_owner_epoch = src->payload_owner_epoch;
    dst->payload_export_owner_epoch = src->payload_export_owner_epoch;
}

static void space_reset_moved_from(Space *s) {
    if (!s)
        return;
    s->kind = SPACE_KIND_ATOM;
    s->strict = false;
    /* Moving the payload ends the old source lifetime.  Re-establish every
       invariant of an empty live Space so callers may safely reuse or free it:
       a fresh identity prevents stale read tokens from reviving, and the
       universe observer keeps AtomId storage width synchronized. */
    s->instance_id = space_fresh_instance_id();
    s->revision = 0;
    s->equation_revision = 0;
    s->declaration_revision = 0;
    s->declares_subtypes = false;
    s->prefix_epoch = space_fresh_prefix_epoch();
    space_native_storage_init_empty(s, space_default_universe());
    space_match_backend_init(s);
    space_attach_to_universe(s, s->native.universe);
    s->payload_owner_epoch = 0;
    s->payload_export_owner_epoch = 0;
    s->imported_len = 0;
    s->module_dependency_epoch = 0u;
}

/* ── Space ──────────────────────────────────────────────────────────────── */

/* Heap bytes the C library has handed out, for the resource ledger's
 * account of what initializing a space allocates; zero where the library
 * does not report it. */
static uint64_t space_ledger_heap_in_use(void) {
#if CETTA_BUILD_WITH_RUNTIME_STATS && defined(__GLIBC__)
    if (cetta_runtime_stats_is_enabled())
        return (uint64_t)mallinfo2().uordblks;
#endif
    return 0u;
}

void space_init_with_universe(Space *s, TermUniverse *universe) {
    if (!s)
        return;
    s->ledger_counted = cetta_runtime_stats_is_enabled();
    uint64_t heap_before = s->ledger_counted ? space_ledger_heap_in_use() : 0u;
    s->kind = SPACE_KIND_ATOM;
    s->strict = false;
    space_native_storage_init_empty(s, universe);
    s->instance_id = space_fresh_instance_id();
    s->revision = 0;
    s->equation_revision = 0;
    s->declaration_revision = 0;
    s->declares_subtypes = false;
    s->prefix_epoch = space_fresh_prefix_epoch();
    space_match_backend_init(s);
    space_attach_to_universe(s, s->native.universe);
    s->payload_owner_epoch = 0;
    s->payload_export_owner_epoch = 0;
    s->imported_len = 0;
    s->deps = NULL;
    s->dep_count = 0u;
    s->dep_cap = 0u;
    s->importers = NULL;
    s->importer_count = 0u;
    s->importer_cap = 0u;
    s->module_dependency_epoch = 0u;
    if (s->ledger_counted) {
        uint64_t heap_after = space_ledger_heap_in_use();
        cetta_runtime_stats_gauge_add(CETTA_RUNTIME_COUNTER_SPACE_LIVE,
                                      CETTA_RUNTIME_COUNTER_SPACE_LIVE_PEAK, 1);
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES,
            CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES_PEAK,
            (int64_t)sizeof(Space));
        if (heap_after > heap_before)
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_SPACE_INIT_HEAP_BYTES,
                                    heap_after - heap_before);
    }
}

static bool space_handle_list_push(Space ***items, uint32_t *count,
                                   uint32_t *cap, Space *item) {
    if (*count == *cap) {
        uint32_t next = *cap ? *cap * 2u : 4u;
        if (next <= *cap)
            return false;
        *items = cetta_realloc(*items, sizeof(**items) * (size_t)next);
        *cap = next;
    }
    (*items)[(*count)++] = item;
    return true;
}

static void space_handle_list_remove(Space **items, uint32_t *count,
                                     const Space *item) {
    uint32_t write = 0u;
    for (uint32_t read = 0u; read < *count; read++) {
        if (items[read] != item)
            items[write++] = items[read];
    }
    *count = write;
}

/* A module the importer reads through changed: the importer's composed view
 * changed after its own atoms.  Advance its revisions without publishing a
 * mutation of its own atoms, so no cascade runs through further importers;
 * those link the module directly, since imports flatten.  A rewrite of the
 * module, or a change of the importer's links, rewrites the composed view
 * and so ends the importer's prefix epoch as well. */
static void space_note_dependency_mutation(
        Space *importer, SpaceMutationEquationProjection equation_projection,
        bool view_rewritten) {
    if (!importer)
        return;
    if (importer->revision == UINT64_MAX ||
        ((equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)) &&
         importer->declaration_revision == UINT64_MAX) ||
        ((equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)) &&
         importer->equation_revision == UINT64_MAX)) {
        fputs("CeTTa: exhausted imported Space revision counter\n", stderr);
        abort();
    }
    importer->revision++;
    importer->module_dependency_epoch = space_fresh_prefix_epoch();
    if (view_rewritten)
        importer->prefix_epoch = space_fresh_prefix_epoch();
    if (equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE |
             SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION))
        space_execution_analysis_note_mutation(importer);
    if ((equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)))
        importer->declaration_revision++;
    if ((equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)))
        importer->equation_revision++;
}

static bool space_add_dependency_link(Space *importer, Space *dependency) {
    if (!importer || !dependency || importer == dependency)
        return false;
    for (uint32_t i = 0u; i < importer->dep_count; i++) {
        if (importer->deps[i] == dependency)
            return true;
    }
    if (!space_handle_list_push(&importer->deps, &importer->dep_count,
                                &importer->dep_cap, dependency))
        return false;
    if (!space_handle_list_push(&dependency->importers,
                                &dependency->importer_count,
                                &dependency->importer_cap, importer)) {
        importer->dep_count--;
        return false;
    }
    space_note_dependency_mutation(
        importer, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE, true);
    space_advance_global_mutation_epoch();
    return true;
}

bool space_add_dependency(Space *importer, Space *dependency) {
    /* A space that composes others keeps no relations. */
    space_dense_settle(importer, SYMBOL_ID_NONE);
    return importer && !importer->module_own_view &&
           space_add_dependency_link(importer, dependency);
}

/* A dependency contributes its own atoms: those after its imported prefix. */
static CettaIndex space_dependency_own_begin(const Space *dependency) {
    CettaIndex imported = space_imported_length(dependency);
    CettaCount len = space_length64(dependency);
    return imported < len ? imported : len;
}

CettaCount space_view_length64(const Space *s) {
    CettaCount len = space_length64(s);
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies; d++) {
        const Space *dependency = space_dependency_at(s, d);
        len += space_length64(dependency) -
               space_dependency_own_begin(dependency);
    }
    return len;
}

Atom *space_view_get_at64(const Space *s, CettaIndex index) {
    CettaCount len = space_length64(s);
    if (index < len)
        return space_get_at64(s, index);
    index -= len;
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies; d++) {
        const Space *dependency = space_dependency_at(s, d);
        CettaIndex begin = space_dependency_own_begin(dependency);
        CettaCount own = space_length64(dependency) - begin;
        if (index < own)
            return space_get_at64(dependency, begin + index);
        index -= own;
    }
    return NULL;
}

/* False only when the row's stored header shows that it is not a rule
 * `(= lhs rhs)` or `(=% lhs rhs)`; a row without a header may be one. */
bool space_row_may_be_rule(const Space *s, CettaIndex idx) {
    if (!s || space_has_overlay_base(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return true;
    /* Past the rows are facts kept in relations, data and never rules: they
     * stay in their relations for the asking. */
    if (space_dense_facts(s) != 0u &&
        idx >= space_length64(s) - space_dense_facts(s))
        return false;
    const CettaTermHdr *header =
        tu_hdr(s->native.universe, space_get_atom_id_at64(s, idx));
    if (!header)
        return true;
    if (header->tag != ATOM_EXPR ||
        (header->aux32 & CETTA_TERM_HDR_DATA_MASK) != 3u)
        return false;
    return header->sym_or_head == SYMBOL_ID_NONE ||
        header->sym_or_head == g_builtin_syms.equals ||
        header->sym_or_head == g_builtin_syms.equals_percent;
}

bool space_view_row_may_be_rule(const Space *s, CettaIndex index) {
    CettaCount len = space_length64(s);
    if (index < len)
        return space_row_may_be_rule(s, index);
    index -= len;
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies; d++) {
        const Space *dependency = space_dependency_at(s, d);
        CettaIndex begin = space_dependency_own_begin(dependency);
        CettaCount own = space_length64(dependency) - begin;
        if (index < own)
            return space_row_may_be_rule(dependency, begin + index);
        index -= own;
    }
    return true;
}

bool space_copy_module_view(Space *dst, const Space *src) {
    if (!dst || !src)
        return false;
    CettaIndex imported = space_imported_length(src);
    if (imported != 0u && !space_has_overlay_base(dst) &&
        dst->kind == SPACE_KIND_ATOM &&
        imported <= space_length64(dst)) {
        bool same_prefix = true;
        for (CettaIndex i = 0u; i < imported && same_prefix; i++)
            same_prefix = space_get_atom_id_at64(dst, i) ==
                          space_get_atom_id_at64(src, i);
        if (same_prefix)
            dst->imported_len = imported;
    }
    uint32_t dependencies = space_dependency_count(src);
    for (uint32_t d = 0u; d < dependencies; d++) {
        if (!space_add_dependency(dst, space_dependency_at(src, d)))
            return false;
    }
    return true;
}

/* Remove every link of `s` in both directions. */
static void space_dependencies_unlink(Space *s) {
    if (s->importer_count != 0u)
        space_advance_global_mutation_epoch();
    for (uint32_t i = 0u; i < s->dep_count; i++)
        space_handle_list_remove(s->deps[i]->importers,
                                 &s->deps[i]->importer_count, s);
    for (uint32_t i = 0u; i < s->importer_count; i++) {
        Space *importer = s->importers[i];
        bool selected = importer->module_own_view && importer->overlay_base == s;
        space_handle_list_remove(importer->deps, &importer->dep_count, s);
        /* Watches are acquired in nested-storage order. A disappearing
           selection falls back to the remaining enclosing storage, or
           detaches when the final owner ends. Never expose watches as
           query dependencies of a detached own-storage view. */
        if (selected)
            importer->overlay_base = importer->dep_count
                ? importer->deps[importer->dep_count - 1u] : NULL;
        space_note_dependency_mutation(
            importer, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE, true);
    }
    free(s->deps);
    free(s->importers);
    s->deps = NULL;
    s->importers = NULL;
    s->dep_count = s->dep_cap = 0u;
    s->importer_count = s->importer_cap = 0u;
}

bool space_mark_imported_prefix(Space *s) {
    if (!s || space_has_overlay_base(s) || s->kind != SPACE_KIND_ATOM)
        return false;
    space_dense_settle(s, SYMBOL_ID_NONE);
    s->imported_len = space_length64(s);
    return true;
}

CettaIndex space_imported_length(const Space *s) {
    /* An overlay's logical prefix is its base's, down to the root space:
       no overlay can remove imported atoms from under it. */
    while (s && s->overlay_base) {
        if (s->module_own_view)
            return 0u;
        s = s->overlay_base;
    }
    return s ? s->imported_len : 0u;
}

static uint32_t space_grounded_types(SymbolId symbol, Atom *const **types) {
    if (types)
        *types = NULL;
    return space_session_grounded_symbol_types && symbol != SYMBOL_ID_NONE
        ? space_session_grounded_symbol_types(symbol, types) : 0u;
}

/* An atom occurring among the imported atoms and nowhere among the space's
 * own stays when it is removed through the space. */
/* The rows holding `atom_id`, read from the exact index when it is current
 * for that atom (false when it cannot answer): whether one lies before
 * `from`, and the first at or after it. */
static bool space_exact_rows(const Space *s, AtomId atom_id, CettaIndex from,
                             bool *before_from, bool *any_from,
                             CettaIndex *first_from) {
    if (!s || s->native.exact_idx_dirty || space_has_overlay_base(s) ||
        s->native.marked != 0u ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !atom_id_is_exact_indexable(s, atom_id))
        return false;
    CettaIndex *rows = NULL;
    CettaIndex count = exact_index_rows(&s->native.exact_idx, atom_id, &rows);
    *before_from = false;
    *any_from = false;
    for (CettaIndex k = 0u; k < count; k++) {
        if (rows[k] < from) {
            *before_from = true;
        } else {
            *first_from = rows[k];
            *any_from = true;
            break;
        }
    }
    free(rows);
    return true;
}

/* An unordered removal moves the last row into the gap: the removed row
 * leaves the index and the moved row takes the gap's position.  False when
 * an entry is missing; the caller then leaves the index to be rebuilt. */
static bool exact_index_note_swap_remove(Space *s, CettaIndex row,
                                         AtomId removed, CettaIndex moved_from,
                                         AtomId moved) {
    if (!exact_index_forget_row(s, removed, row))
        return false;
    if (moved_from != row && atom_id_is_exact_indexable(s, moved)) {
        if (!exact_index_remove_row(&s->native.exact_idx, moved, moved_from))
            return false;
        exact_index_add_row(&s->native.exact_idx, moved, row);
    }
    return true;
}

static bool space_atom_id_imported_only(const Space *s, AtomId atom_id) {
    CettaIndex imported = space_imported_length(s);
    if (imported == 0u || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    bool before = false, after = false;
    CettaIndex first = 0u;
    if (space_exact_rows(s, atom_id, imported, &before, &after, &first))
        return before && !after;
    bool in_imported = false;
    for (CettaIndex i = 0u; i < imported && !in_imported; i++)
        in_imported = space_get_atom_id_at64(s, i) == atom_id;
    if (!in_imported)
        return false;
    CettaCount logical_len = space_length64(s);
    for (CettaIndex i = imported; i < logical_len; i++) {
        if (space_get_atom_id_at64(s, i) == atom_id)
            return false;
    }
    return true;
}

void space_init_overlay(Space *s, const Space *base) {
    space_init_with_universe(s, base ? base->native.universe : NULL);
    if (!base)
        return;
    s->kind = base->kind;
    space_set_strict(s, base->strict);
    s->overlay_base = base;
    s->overlay_base_visible_len = space_length64(base);
}

void space_init(Space *s) {
    space_init_with_universe(s, NULL);
}

bool space_init_module_own_view(Space *s, Space *module) {
    if (!s || !module || s == module || module->module_own_view)
        return false;
    space_init_overlay(s, module);
    /* The link is a lifetime/revision observation, hidden from queries.
     * Existing reverse notifications carry storage mutations to the view. */
    if (!space_add_dependency(s, module)) {
        space_free(s);
        return false;
    }
    s->module_own_view = true;
    return true;
}

bool space_module_own_view_select(Space *view, Space *storage) {
    if (!view || !view->module_own_view || !storage || storage->module_own_view ||
        !space_add_dependency_link(view, storage))
        return false;
    if (view->overlay_base != storage) {
        view->overlay_base = storage;
        space_note_dependency_mutation(
            view, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE, true);
        space_advance_global_mutation_epoch();
    }
    return true;
}

void space_module_own_view_release(Space *view, Space *storage) {
    if (!view || !view->module_own_view || !storage)
        return;
    bool selected = view->overlay_base == storage;
    space_handle_list_remove(view->deps, &view->dep_count, storage);
    space_handle_list_remove(storage->importers, &storage->importer_count, view);
    if (selected)
        view->overlay_base = view->dep_count
            ? view->deps[view->dep_count - 1u] : NULL;
    space_note_dependency_mutation(
        view, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE, true);
    space_advance_global_mutation_epoch();
}

void space_free(Space *s) {
    if (!s)
        return;
    if (s->ledger_counted) {
        s->ledger_counted = false;
        cetta_runtime_stats_gauge_add(CETTA_RUNTIME_COUNTER_SPACE_LIVE,
                                      CETTA_RUNTIME_COUNTER_SPACE_LIVE_PEAK, -1);
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES,
            CETTA_RUNTIME_COUNTER_SPACE_STRUCT_BYTES_PEAK,
            -(int64_t)sizeof(Space));
    }
    /* A read token may retain this address after the object lifetime ends.
       Retire thread-local execution-analysis entries while the identity is
       still readable; cache lookup also validates against its live query
       Space before consulting a token. */
    space_execution_analysis_note_mutation(s);
    space_pinned_occurrences_detach(s);
    space_dependencies_unlink(s);
    space_detach_from_universe(s);
    space_release_rows(s, 0u, s->native.len);
    space_dense_free(s);
    id_present_keys_release(&s->native);
    space_marks_free(s);
    free(s->native.atom_ids);
    s->native.atom_ids = NULL;
    s->native.atom_id_width_bits = 0;
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.universe = space_default_universe();
    s->instance_id = 0;
    s->revision = 0;
    s->equation_revision = 0;
    s->declaration_revision = 0;
    s->prefix_epoch = 0;
    eq_index_free(&s->native.eq_idx);
    ty_ann_index_free(&s->native.ty_idx);
    exact_index_free(&s->native.exact_idx);
    id_present_free(&s->native);
    space_match_backend_free(s);
    s->native.has_non_exact_atoms = false;
    s->native.has_non_exact_atoms_dirty = false;
    s->native.secondary_index_deferral_depth = 0;
    free(s->overlay_removed_base_indices);
    s->overlay_base = NULL;
    s->overlay_base_visible_len = 0;
    s->overlay_removed_base_indices = NULL;
    s->overlay_removed_base_len = 0;
    s->overlay_removed_base_cap = 0;
    s->payload_owner_epoch = 0;
    s->payload_export_owner_epoch = 0;
}

/* A space stores syntax: copies made to store an atom drop HE's evaluated
 * marks (atom_deep_copy_strip_he_marks_enter). */
static void space_store_scope_leave(bool *entered) {
    if (*entered)
        atom_deep_copy_strip_he_marks_leave();
}

#define SPACE_STORE_SCOPE() \
    __attribute__((cleanup(space_store_scope_leave))) bool space_store_scope = \
        (atom_deep_copy_strip_he_marks_enter(), true)

Atom *space_store_atom(Space *s, Arena *fallback, Atom *atom) {
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard shared_transition = {0};
    cetta_shared_transition_guard_enter(&shared_transition);
    SPACE_STORE_SCOPE();
    return term_universe_store_atom(s ? s->native.universe : NULL, fallback, atom);
}

static bool is_equation_atom(Atom *a, Atom **lhs_out, Atom **rhs_out) {
    CettaRuleDescriptor rule;
    if (!cetta_rule_view(a, CETTA_RULE_SYNTAX_ORDINARY, &rule)) return false;
    *lhs_out = rule.head;
    *rhs_out = rule.body;
    return true;
}

static uint64_t space_projection_dependency_epoch(const Space *s,
                                                 bool include_modules);

SpaceReadToken space_read_token(const Space *s) {
    return (SpaceReadToken){
        .space = s,
        .instance_id = space_instance_id(s),
        .revision = space_revision(s),
        .prefix_epoch = s ? s->prefix_epoch : 0u,
        .base_prefix_epoch = space_projection_dependency_epoch(s, false),
        .base_dependency_epoch = space_projection_dependency_epoch(s, true),
    };
}

bool space_read_token_is_current(SpaceReadToken token) {
    return token.space && token.instance_id != 0u &&
           token.instance_id == space_instance_id(token.space) &&
           token.revision == space_revision(token.space) &&
           token.base_dependency_epoch != UINT64_MAX &&
           token.base_dependency_epoch ==
               space_projection_dependency_epoch(token.space, true);
}

bool space_read_token_prefix_intact(SpaceReadToken token) {
    return token.space && token.instance_id != 0u &&
           token.instance_id == space_instance_id(token.space) &&
           token.prefix_epoch == token.space->prefix_epoch &&
           token.base_prefix_epoch != UINT64_MAX &&
           token.base_prefix_epoch ==
               space_projection_dependency_epoch(token.space, false);
}

bool space_read_token_matches_live_space(SpaceReadToken token,
                                         const Space *live_space) {
    return live_space && token.space == live_space &&
           token.instance_id != 0u &&
           token.instance_id == space_instance_id(live_space) &&
           token.revision == space_revision(live_space) &&
           token.base_dependency_epoch != UINT64_MAX &&
           token.base_dependency_epoch ==
               space_projection_dependency_epoch(live_space, true);
}

static uint64_t space_projection_dependency_epoch(const Space *s,
                                                 bool include_modules) {
    const Space *slow = s ? s->overlay_base : NULL;
    const Space *fast = slow;
    while (fast && fast->overlay_base) {
        slow = slow->overlay_base;
        fast = fast->overlay_base->overlay_base;
        if (slow == fast)
            return UINT64_MAX;
    }

    uint64_t latest = 0u;
    for (const Space *dependency = s ? s->overlay_base : NULL;
         dependency; dependency = dependency->overlay_base) {
        if (dependency->prefix_epoch > latest)
            latest = dependency->prefix_epoch;
        if (include_modules && dependency->module_dependency_epoch > latest)
            latest = dependency->module_dependency_epoch;
    }
    return latest;
}

SpaceProgramToken space_program_token(const Space *s) {
    return (SpaceProgramToken){
        .space = s,
        .instance_id = space_instance_id(s),
        .equation_revision = space_equation_revision(s),
        .declaration_revision = space_declaration_revision(s),
        .base_dependency_epoch =
            space_projection_dependency_epoch(s, true),
    };
}

SpaceEquationToken space_equation_token(const Space *s) {
    return (SpaceEquationToken){
        .space = s,
        .instance_id = space_instance_id(s),
        .equation_revision = space_equation_revision(s),
        .projection_dependency = s ? s->overlay_base : NULL,
        .projection_dependency_epoch =
            space_projection_dependency_epoch(s, true),
    };
}

uint64_t space_overlay_dependency_epoch(const Space *s) {
    return space_projection_dependency_epoch(s, true);
}

bool space_program_token_matches_live_space(
        SpaceProgramToken token, const Space *live_space) {
    return live_space && token.space == live_space &&
           token.instance_id != 0u &&
           token.base_dependency_epoch != UINT64_MAX &&
           space_program_token_eq(
               token, space_program_token(live_space));
}

bool space_equation_token_is_current(SpaceEquationToken token) {
    return token.space && token.instance_id != 0u &&
           token.instance_id == space_instance_id(token.space) &&
           token.equation_revision ==
               space_equation_revision(token.space) &&
           token.projection_dependency == token.space->overlay_base &&
           token.projection_dependency_epoch != UINT64_MAX &&
           token.projection_dependency_epoch ==
               space_projection_dependency_epoch(token.space, true);
}

bool space_equation_token_matches_live_space(
        SpaceEquationToken token, const Space *live_space) {
    return live_space && token.space == live_space &&
           token.instance_id != 0u &&
           token.instance_id == space_instance_id(live_space) &&
           token.equation_revision ==
               space_equation_revision(live_space) &&
           token.projection_dependency == live_space->overlay_base &&
           token.projection_dependency_epoch != UINT64_MAX &&
           token.projection_dependency_epoch ==
               space_projection_dependency_epoch(live_space, true);
}

/* An index entry without its atom id: read its row.  While rows are
 * marked, an index row is not a logical position. */
static __attribute__((noinline)) AtomId space_indexed_occurrence_row_atom_id(
    const Space *s, const CettaIndex *indices, CettaIndex position) {
    if (!s || !indices)
        return CETTA_ATOM_ID_NONE;
    if (space_has_marks(s))
        return indices[position] < s->native.len
            ? space_atom_id_storage_load_at(s->native.atom_ids,
                                            s->native.atom_id_width_bits,
                                            indices[position])
            : CETTA_ATOM_ID_NONE;
    if (indices[position] >= space_length64(s))
        return CETTA_ATOM_ID_NONE;
    return space_get_atom_id_at64(s, indices[position]);
}

static AtomId space_indexed_occurrence_atom_id(
    const Space *s, const CettaIndex *indices, const AtomId *atom_ids,
    CettaIndex position) {
    if (atom_ids && atom_ids[position] != CETTA_ATOM_ID_NONE)
        return atom_ids[position];
    return space_indexed_occurrence_row_atom_id(s, indices, position);
}

static Atom *space_indexed_occurrence_atom(
    Space *s, const CettaIndex *indices, const AtomId *atom_ids,
    CettaIndex position) {
    AtomId atom_id = space_indexed_occurrence_atom_id(
        s, indices, atom_ids, position);
    return atom_id != CETTA_ATOM_ID_NONE
        ? term_universe_get_atom(s ? s->native.universe : NULL, atom_id)
        : NULL;
}

bool space_rule_occurrence_resolve(SpaceEquationOccurrenceId id,
    unsigned admitted_forms, SpaceEquationOccurrence *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!out || !space_read_token_prefix_intact(id.read)) {
        return false;
    }
    Atom *equation = id.has_equation_id
        ? term_universe_get_atom(
              id.read.space->native.universe, id.equation_id)
        : (id.logical_index < space_length64(id.read.space)
               ? space_get_at64(id.read.space, id.logical_index)
               : NULL);
    CettaRuleDescriptor rule;
    if (!cetta_rule_view(equation, admitted_forms, &rule) ||
        !space_read_token_prefix_intact(id.read)) {
        return false;
    }
    out->id = id;
    out->equation = equation;
    out->lhs = rule.head;
    out->rhs = rule.body;
    return true;
}

bool space_equation_occurrence_resolve(SpaceEquationOccurrenceId id,
                                       SpaceEquationOccurrence *out) {
    return space_rule_occurrence_resolve(id, CETTA_RULE_SYNTAX_ORDINARY, out);
}

static bool space_equation_cursor_index_matches(
    const SpaceEquationCursor *cursor, const EqBucket *bucket,
    CettaIndex position, bool wildcard) {
    if (!cursor || !cursor->member.space || !bucket ||
        position >= bucket->len) {
        return false;
    }
    AtomId equation_id = space_indexed_occurrence_atom_id(
        cursor->member.space, bucket->atom_indices, bucket->atom_ids,
        position);
    Atom *equation = equation_id != CETTA_ATOM_ID_NONE
        ? term_universe_get_atom(
              cursor->member.space->native.universe, equation_id)
        : NULL;
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs))
        return false;
    SymbolId lhs_head = eq_head_symbol(lhs);
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
    if (wildcard && lhs_head == SYMBOL_ID_NONE &&
        eq_lhs_has_structured_head(lhs)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_CURSOR_STRUCTURED_DISJOINT);
    }
#endif
    return wildcard
        ? lhs_head == SYMBOL_ID_NONE &&
              eq_lhs_may_match_known_head(lhs, cursor->head)
        : lhs_head == cursor->head;
}

static bool space_equation_cursor_peek_bucket(
    SpaceEquationCursor *cursor, const EqBucket *bucket,
    CettaIndex *position, bool wildcard, CettaIndex *logical_index,
    AtomId *equation_id) {
    if (!cursor || !bucket || !position || !logical_index || !equation_id)
        return false;
    while (*position < bucket->len) {
        CettaIndex candidate = bucket->atom_indices[*position];
        if (candidate >= cursor->ceiling ||
            (cursor->imported != 0u &&
             (candidate < cursor->imported) != (cursor->phase == 1u))) {
            (*position)++;
            continue;
        }
        if (space_equation_cursor_index_matches(
                cursor, bucket, *position, wildcard)) {
            *logical_index = candidate;
            *equation_id = space_indexed_occurrence_atom_id(
                cursor->member.space, bucket->atom_indices,
                bucket->atom_ids, *position);
            return true;
        }
        (*position)++;
    }
    return false;
}

/* Start reading `s` as the cursor's next member: its own equations, then,
 * unless `own_only`, its imported ones. */
static void space_equation_cursor_enter(SpaceEquationCursor *cursor,
                                        Space *s, bool own_only) {
    if (!space_has_overlay_base(s))
        ensure_eq_index(s);
    bool extended_projection = cursor->head == SYMBOL_ID_NONE ||
        (cursor->admitted_forms != CETTA_RULE_SYNTAX_ORDINARY &&
        (!(cursor->admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) ||
         space_has_overlay_base(s) || s->native.eq_idx.directional_present));
    cursor->member = space_read_token(s);
    cursor->own_only = own_only;
    /* Equation indexes hold rows; a marked data row before an equation
     * moves its logical position, not its row. */
    cursor->ceiling = space_has_marks(s) ? s->native.len : space_length64(s);
    cursor->overlay = space_has_overlay_base(s) || extended_projection;
    cursor->imported = space_imported_length(s);
    if (cursor->imported > cursor->ceiling)
        cursor->imported = 0u;
    /* A member holding imported atoms only has no own equations: a
       dependency then contributes nothing, and the space itself reads its
       imported ones in one pass. */
    if (cursor->imported != 0u && cursor->imported == cursor->ceiling) {
        if (own_only)
            cursor->ceiling = 0u;
        cursor->imported = 0u;
    }
    cursor->phase = 0u;
    cursor->exact_position = 0u;
    cursor->wildcard_position = 0u;
    cursor->overlay_position = cursor->overlay ? cursor->imported : 0u;
}

bool space_equation_cursor_init(Space *s, SymbolId head,
                                SpaceEquationCursor *cursor) {
    if (head == SYMBOL_ID_NONE) return false;
    return space_rule_cursor_init(s, head, CETTA_RULE_SYNTAX_ORDINARY, cursor);
}

bool space_rule_cursor_init(Space *s, SymbolId head, unsigned admitted_forms,
                            SpaceEquationCursor *cursor) {
    if (cursor)
        memset(cursor, 0, sizeof(*cursor));
    if (!s || !cursor || !admitted_forms ||
        (admitted_forms & ~(CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL)))
        return false;
    cursor->read = space_read_token(s);
    cursor->head = head;
    cursor->admitted_forms = admitted_forms;
    space_equation_cursor_enter(cursor, s, false);
    return space_read_token_prefix_intact(cursor->read);
}

/* The next equation of the member being read, or END when it is done. */
static SpaceEquationCursorStep space_equation_cursor_member_next(
    SpaceEquationCursor *cursor, SpaceEquationOccurrenceId *out) {
    /* The space itself was checked by the caller; a dependency is checked
       here. */
    if (cursor->dependency != 0u &&
        !space_read_token_prefix_intact(cursor->member))
        return SPACE_EQUATION_CURSOR_INVALIDATED;
    if (cursor->ceiling == 0u)
        return SPACE_EQUATION_CURSOR_END;
    Space *s = (Space *)cursor->member.space;
    if (cursor->overlay) {
        CettaCount logical_len = space_length64(s);
        if (logical_len > cursor->ceiling)
            logical_len = cursor->ceiling;
    overlay_phase:
        if (cursor->phase == 1u && logical_len > cursor->imported)
            logical_len = cursor->imported;
        while (cursor->overlay_position < logical_len) {
            CettaIndex logical_index = cursor->overlay_position++;
            Atom *equation = space_get_at64(s, logical_index);
            CettaRuleDescriptor rule;
            if (!cetta_rule_view(equation, cursor->admitted_forms, &rule))
                continue;
            Atom *lhs = rule.head;
            if (cursor->head != SYMBOL_ID_NONE && !eq_lhs_may_match_known_head(lhs, cursor->head)) {
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
                if (eq_lhs_has_structured_head(lhs)) {
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_CURSOR_STRUCTURED_DISJOINT);
                }
#endif
                continue;
            }
            out->read = cursor->member;
            out->logical_index = logical_index;
            out->equation_id = CETTA_ATOM_ID_NONE;
            out->has_equation_id = false;
            cetta_runtime_stats_inc(
                eq_head_symbol(lhs) == cursor->head
                    ? CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_CURSOR_EXACT_ITEM
                    : CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_CURSOR_OPEN_ITEM);
            return SPACE_EQUATION_CURSOR_ITEM;
        }
        if (cursor->imported != 0u && !cursor->own_only &&
            cursor->phase == 0u) {
            cursor->phase = 1u;
            cursor->overlay_position = 0u;
            goto overlay_phase;
        }
        return SPACE_EQUATION_CURSOR_END;
    }

    EqBucket *exact =
        &s->native.eq_idx.buckets[symbol_hash(cursor->head)];
    EqBucket *wildcard = &s->native.eq_idx.wildcard;
    CettaIndex exact_index = 0u;
    CettaIndex wildcard_index = 0u;
    AtomId exact_id = CETTA_ATOM_ID_NONE;
    AtomId wildcard_id = CETTA_ATOM_ID_NONE;
    bool has_exact = space_equation_cursor_peek_bucket(
        cursor, exact, &cursor->exact_position, false, &exact_index,
        &exact_id);
    bool has_wildcard = space_equation_cursor_peek_bucket(
        cursor, wildcard, &cursor->wildcard_position, true,
        &wildcard_index, &wildcard_id);
    if (!has_exact && !has_wildcard &&
        cursor->imported != 0u && !cursor->own_only &&
        cursor->phase == 0u) {
        cursor->phase = 1u;
        cursor->exact_position = 0u;
        cursor->wildcard_position = 0u;
        has_exact = space_equation_cursor_peek_bucket(
            cursor, exact, &cursor->exact_position, false, &exact_index,
            &exact_id);
        has_wildcard = space_equation_cursor_peek_bucket(
            cursor, wildcard, &cursor->wildcard_position, true,
            &wildcard_index, &wildcard_id);
    }
    if (!has_exact && !has_wildcard)
        return SPACE_EQUATION_CURSOR_END;

    CettaIndex logical_index;
    AtomId equation_id;
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
    bool exact_item;
#endif
    if (has_exact && (!has_wildcard || exact_index < wildcard_index)) {
        logical_index = exact_index;
        equation_id = exact_id;
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
        exact_item = true;
#endif
        cursor->exact_position++;
    } else if (has_wildcard &&
               (!has_exact || wildcard_index < exact_index)) {
        logical_index = wildcard_index;
        equation_id = wildcard_id;
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
        exact_item = false;
#endif
        cursor->wildcard_position++;
    } else {
        logical_index = exact_index;
        equation_id = exact_id;
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
        exact_item = true;
#endif
        cursor->exact_position++;
        cursor->wildcard_position++;
    }
    out->read = cursor->member;
    out->logical_index = logical_index;
    out->equation_id = equation_id;
    out->has_equation_id = equation_id != CETTA_ATOM_ID_NONE;
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
    cetta_runtime_stats_inc(
        exact_item
            ? CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_CURSOR_EXACT_ITEM
            : CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_CURSOR_OPEN_ITEM);
#endif
    return SPACE_EQUATION_CURSOR_ITEM;
}

SpaceEquationCursorStep space_equation_cursor_next(
    SpaceEquationCursor *cursor, SpaceEquationOccurrenceId *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!cursor || !out || !space_read_token_prefix_intact(cursor->read))
        return SPACE_EQUATION_CURSOR_INVALIDATED;
    for (;;) {
        SpaceEquationCursorStep step =
            space_equation_cursor_member_next(cursor, out);
        if (step != SPACE_EQUATION_CURSOR_END)
            return step;
        /* The space's own and imported equations are done: each module it
           imports follows with its own, in import order. */
        Space *dependency = space_dependency_at(
            cursor->read.space, cursor->dependency);
        if (!dependency)
            return SPACE_EQUATION_CURSOR_END;
        cursor->dependency++;
        space_equation_cursor_enter(cursor, dependency, true);
    }
}

/* ── Revision-pinned execution-contract analysis ──────────────────────── */

typedef struct {
    SymbolId head;
    CettaGsltQueryEffect effect;
    bool defined;
    bool occupied;
} SpaceEffectCacheEntry;

typedef struct {
    SpaceProgramToken program;
    /* The language the analysis read the program in: HE reads its macro
     * calls as their expansions. */
    CettaLanguageId language;
    SpaceEffectCacheEntry *entries;
    size_t len;
    size_t cap;
    bool occupied;
} SpaceEffectCacheShard;

#define SPACE_EFFECT_CACHE_SHARDS 8u

typedef struct {
    SpaceEffectCacheShard shards[SPACE_EFFECT_CACHE_SHARDS];
    size_t next_victim;
} SpaceEffectCache;

static _Thread_local SpaceEffectCache g_space_effect_cache;

void space_execution_analysis_cache_free_for_current_thread(void) {
    for (size_t index = 0u;
         index < SPACE_EFFECT_CACHE_SHARDS; index++) {
        free(g_space_effect_cache.shards[index].entries);
    }
    memset(&g_space_effect_cache, 0, sizeof(g_space_effect_cache));
}

/* A call whose head a variable supplies runs whatever head the variable
 * holds when it runs.  The analysis keeps it apart from the effects of the
 * heads it can name: an executor that admits each such call when it runs, by
 * the head it then has, reads only those; every other reader counts the call
 * as one that may read and write anything.  The bit is the analysis's own
 * and never leaves it. */
#define SPACE_EFFECT_DYNAMIC_CALL ((CettaGsltQueryEffect)(1u << 7))

typedef struct {
    SymbolId head;
    SymbolId *dependencies;
    size_t dependency_len;
    size_t dependency_cap;
    CettaGsltQueryEffect base_effect;
    CettaGsltQueryEffect effect;
    bool defined;
    bool scanned;
} SpaceEffectNode;

typedef struct {
    Space *space;
    SpaceProgramToken program;
    SpaceEffectNode *nodes;
    size_t len;
    size_t cap;
    CettaSymbolIndex by_head;
    bool failed;
} SpaceEffectGraph;

typedef struct {
    SymbolId head;
    CettaGsltQueryEffect effect;
    bool occupied;
} SpaceGeneratedEffectCacheEntry;

enum { SPACE_GENERATED_EFFECT_CACHE_CAP = 256u };
static _Thread_local SpaceGeneratedEffectCacheEntry
    g_space_generated_effect_cache[SPACE_GENERATED_EFFECT_CACHE_CAP];
static _Thread_local uint64_t g_space_generated_effect_table_instance;

static CettaGsltQueryEffect space_generated_query_head_effect(
    SymbolId head) {
    uint64_t instance = symbol_table_instance_id(g_symbols);
    if (g_space_generated_effect_table_instance != instance) {
        memset(g_space_generated_effect_cache, 0,
               sizeof(g_space_generated_effect_cache));
        g_space_generated_effect_table_instance = instance;
    }
    size_t slot = ((size_t)head * (size_t)UINT32_C(2654435761)) &
        (SPACE_GENERATED_EFFECT_CACHE_CAP - 1u);
    SpaceGeneratedEffectCacheEntry *cached =
        &g_space_generated_effect_cache[slot];
    if (cached->occupied && cached->head == head)
        return cached->effect;
    CettaGsltQueryEffect result = CETTA_GSLT_QUERY_EFFECT_PURE;
#define SPACE_QUERY_EFFECT_HEAD(field, effect) \
    if (head == g_builtin_syms.field) { result = (effect); goto done; }
    CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(SPACE_QUERY_EFFECT_HEAD)
#undef SPACE_QUERY_EFFECT_HEAD
done:
    *cached = (SpaceGeneratedEffectCacheEntry){
        .head = head, .effect = result, .occupied = true,
    };
    return result;
}


static uint64_t space_effect_cache_hash(SymbolId head) {
    uint64_t key = (uint64_t)head * UINT64_C(0x9e3779b97f4a7c15);
    key ^= key >> 30u;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27u;
    key *= UINT64_C(0x94d049bb133111eb);
    return key ^ (key >> 31u);
}

static bool space_effect_cache_rehash(
    SpaceEffectCacheShard *shard, size_t requested_cap) {
    SpaceEffectCacheEntry *old_entries = shard->entries;
    size_t old_cap = shard->cap;
    size_t next = 128u;
    while (next < requested_cap) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(*old_entries))
        return false;
    SpaceEffectCacheEntry *entries = calloc(next, sizeof(*entries));
    if (!entries)
        return false;

    shard->entries = entries;
    shard->len = 0u;
    shard->cap = next;
    for (size_t index = 0u; index < old_cap; index++) {
        SpaceEffectCacheEntry entry = old_entries[index];
        if (!entry.occupied)
            continue;
        size_t slot = (size_t)space_effect_cache_hash(entry.head) &
            (next - 1u);
        while (entries[slot].occupied)
            slot = (slot + 1u) & (next - 1u);
        entries[slot] = entry;
        shard->len++;
    }
    free(old_entries);
    return true;
}

static CettaLanguageId space_effect_language(void) {
    return eval_current_language_id ? eval_current_language_id()
                                    : (CettaLanguageId)0;
}

static SpaceEffectCacheShard *space_effect_cache_shard(
    Space *space, bool create) {
    SpaceProgramToken program = space_program_token(space);
    CettaLanguageId language = space_effect_language();
    SpaceEffectCacheShard *reusable = NULL;
    for (size_t index = 0u;
         index < SPACE_EFFECT_CACHE_SHARDS; index++) {
        SpaceEffectCacheShard *shard =
            &g_space_effect_cache.shards[index];
        if (shard->occupied &&
            space_program_token_eq(shard->program, program) &&
            shard->language == language &&
            space_program_token_matches_live_space(
                shard->program, space)) {
            return shard;
        }
        if (!reusable &&
            ((!shard->occupied && shard->program.space == space &&
              shard->program.instance_id == program.instance_id) ||
             (!shard->occupied && shard->program.space == NULL))) {
            reusable = shard;
        }
        if (shard->occupied && shard->program.space == space &&
            shard->program.instance_id == program.instance_id) {
            reusable = shard;
        }
    }
    if (!create)
        return NULL;

    if (!reusable) {
        for (size_t index = 0u;
             index < SPACE_EFFECT_CACHE_SHARDS; index++) {
            if (!g_space_effect_cache.shards[index].occupied) {
                reusable = &g_space_effect_cache.shards[index];
                break;
            }
        }
    }
    if (!reusable) {
        reusable = &g_space_effect_cache.shards[
            g_space_effect_cache.next_victim];
        g_space_effect_cache.next_victim =
            (g_space_effect_cache.next_victim + 1u) %
            SPACE_EFFECT_CACHE_SHARDS;
    }
    if (reusable->entries && reusable->cap > 0u) {
        memset(reusable->entries, 0,
               reusable->cap * sizeof(*reusable->entries));
    }
    reusable->program = program;
    reusable->language = language;
    reusable->len = 0u;
    reusable->occupied = true;
    return reusable;
}

static const SpaceEffectCacheEntry *space_effect_cache_lookup(
    Space *space, SymbolId head) {
    SpaceEffectCacheShard *shard =
        space_effect_cache_shard(space, false);
    if (!shard || !shard->entries || shard->cap == 0u)
        return NULL;
    size_t slot = (size_t)space_effect_cache_hash(head) &
        (shard->cap - 1u);
    for (;;) {
        SpaceEffectCacheEntry *entry = &shard->entries[slot];
        if (!entry->occupied)
            return NULL;
        if (entry->head == head)
            return entry;
        slot = (slot + 1u) & (shard->cap - 1u);
    }
}

static void space_effect_cache_store(
    Space *space, SymbolId head, CettaGsltQueryEffect effect,
    bool defined) {
    if (!space || head == SYMBOL_ID_NONE)
        return;
    SpaceEffectCacheShard *shard =
        space_effect_cache_shard(space, true);
    if (!shard)
        return;
    if (shard->cap == 0u || (shard->len + 1u) * 4u >=
            shard->cap * 3u) {
        size_t requested = shard->cap == 0u
            ? 128u
            : shard->cap * 2u;
        if (requested < shard->cap ||
            !space_effect_cache_rehash(shard, requested)) {
            return;
        }
    }

    size_t slot = (size_t)space_effect_cache_hash(head) &
        (shard->cap - 1u);
    for (;;) {
        SpaceEffectCacheEntry *entry = &shard->entries[slot];
        if (!entry->occupied) {
            *entry = (SpaceEffectCacheEntry){
                .head = head,
                .effect = effect,
                .defined = defined,
                .occupied = true,
            };
            shard->len++;
            return;
        }
        if (entry->head == head) {
            entry->effect = effect;
            entry->defined = defined;
            return;
        }
        slot = (slot + 1u) & (shard->cap - 1u);
    }
}

void space_execution_analysis_note_mutation(Space *space) {
    if (!space)
        return;
    uint64_t instance = space_instance_id(space);
    for (size_t index = 0u;
         index < SPACE_EFFECT_CACHE_SHARDS; index++) {
        SpaceEffectCacheShard *shard =
            &g_space_effect_cache.shards[index];
        if (shard->occupied && shard->program.space == space &&
            shard->program.instance_id == instance) {
            shard->occupied = false;
            shard->len = 0u;
        }
    }
}

static bool space_effect_graph_reserve(SpaceEffectGraph *graph,
                                       size_t need) {
    if (need <= graph->cap)
        return true;
    size_t next = graph->cap ? graph->cap * 2u : 8u;
    while (next < need) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(*graph->nodes))
        return false;
    SpaceEffectNode *nodes = realloc(
        graph->nodes, sizeof(*nodes) * next);
    if (!nodes)
        return false;
    memset(nodes + graph->cap, 0,
           sizeof(*nodes) * (next - graph->cap));
    graph->nodes = nodes;
    graph->cap = next;
    return true;
}

static bool space_effect_graph_find_or_add(
    SpaceEffectGraph *graph, SymbolId head, size_t *out_index) {
    size_t known = cetta_symbol_index_find(&graph->by_head, head);
    if (known != SIZE_MAX) {
        *out_index = known;
        return true;
    }
    if (!space_effect_graph_reserve(graph, graph->len + 1u) ||
        !cetta_symbol_index_put(&graph->by_head, head, graph->len))
        return false;
    size_t index = graph->len++;
    graph->nodes[index].head = head;
    graph->nodes[index].base_effect = CETTA_GSLT_QUERY_EFFECT_PURE;
    graph->nodes[index].effect = CETTA_GSLT_QUERY_EFFECT_PURE;
    *out_index = index;
    return true;
}

static bool space_effect_node_add_dependency(
    SpaceEffectGraph *graph, size_t node_index, SymbolId dependency) {
    size_t dependency_index = 0u;
    if (!space_effect_graph_find_or_add(
            graph, dependency, &dependency_index)) {
        return false;
    }
    SpaceEffectNode *node = &graph->nodes[node_index];
    for (size_t i = 0u; i < node->dependency_len; i++)
        if (node->dependencies[i] == dependency)
            return true;
    if (node->dependency_len == node->dependency_cap) {
        size_t next = node->dependency_cap
            ? node->dependency_cap * 2u : 4u;
        if (next > SIZE_MAX / sizeof(*node->dependencies))
            return false;
        SymbolId *dependencies = realloc(
            node->dependencies, sizeof(*dependencies) * next);
        if (!dependencies)
            return false;
        node->dependencies = dependencies;
        node->dependency_cap = next;
    }
    node->dependencies[node->dependency_len++] =
        graph->nodes[dependency_index].head;
    return true;
}

/* One term the walk analyzes.  A matched term is (part of) an operand an
 * instruction compares with a pattern: its structure is not a call, while
 * the calls in it count, as a binding may carry them where they are
 * evaluated. */
typedef struct {
    Atom *term;
    bool matched;
} SpaceEffectScanItem;

/* Push one term to analyze onto the walk's stack, growing it off the
 * inline array when full. */
static bool space_effect_stack_push(
    SpaceEffectScanItem **stack, size_t *len, size_t *cap,
    SpaceEffectScanItem *inline_stack, Atom *term, bool matched) {
    if (*len == *cap) {
        if (*cap > SIZE_MAX / 2u || *cap * 2u > SIZE_MAX / sizeof(**stack))
            return false;
        size_t next_cap = *cap * 2u;
        SpaceEffectScanItem *next = *stack == inline_stack
            ? malloc(sizeof(*next) * next_cap)
            : realloc(*stack, sizeof(*next) * next_cap);
        if (!next)
            return false;
        if (*stack == inline_stack)
            memcpy(next, inline_stack, sizeof(*next) * *len);
        *stack = next;
        *cap = next_cap;
    }
    (*stack)[(*len)++] = (SpaceEffectScanItem){
        .term = term,
        .matched = matched,
    };
    return true;
}

/* HE reads a call of a macro definition as what it evaluates: the
 * arguments evaluated before the call, and the body its expansion runs
 * (he_macro_expansion.h).  The evaluator provides the expansion; a space
 * linked without it reads every call as written. */
bool he_macro_effect_expansion(Space *space, Arena *arena, Atom *call,
                               Atom **evaluated, uint32_t capacity,
                               uint32_t *evaluated_count, Atom **body_out)
    __attribute__((weak));

enum {
    SPACE_EFFECT_MAX_EXPANSIONS = 64u,
    SPACE_EFFECT_MAX_EXPANSION_ARITY = 64u,
};

static bool space_effect_scan_rhs(
    SpaceEffectGraph *graph, size_t node_index, Atom *root) {
    SpaceEffectScanItem inline_stack[32];
    SpaceEffectScanItem *stack = inline_stack;
    size_t len = 0u;
    size_t cap = sizeof(inline_stack) / sizeof(inline_stack[0]);
    Arena expansions;
    bool expansions_live = false;
    uint32_t expansion_count = 0u;
    bool ok = true;
    bool he_language = space_effect_language() == CETTA_LANGUAGE_HE;

    if (!root)
        return false;
    stack[len++] = (SpaceEffectScanItem){.term = root, .matched = false};
    while (len > 0u) {
        SpaceEffectScanItem item = stack[--len];
        Atom *current = item.term;
        if (!current || current->kind != ATOM_EXPR ||
            current->expr.len == 0u) {
            continue;
        }
        CettaGsltFoldControl control = {0};
        bool has_control = false;
        Atom *head = current->expr.elems[0];
        if (item.matched && (!head || head->kind != ATOM_SYMBOL)) {
            for (CettaExprIndex i = 0u; ok && i < current->expr.len; i++) {
                Atom *child = current->expr.elems[i];
                if (child && child->kind == ATOM_EXPR)
                    ok = space_effect_stack_push(
                        &stack, &len, &cap, inline_stack, child, true);
            }
            if (!ok)
                break;
            continue;
        }
        if (head && head->kind == ATOM_SYMBOL && he_macro_effect_expansion &&
            expansion_count < SPACE_EFFECT_MAX_EXPANSIONS) {
            if (!expansions_live) {
                arena_init_detached(&expansions);
                expansions_live = true;
            }
            Atom *evaluated[SPACE_EFFECT_MAX_EXPANSION_ARITY];
            uint32_t evaluated_count = 0u;
            Atom *body = NULL;
            if (he_macro_effect_expansion(
                    graph->space, &expansions, current, evaluated,
                    SPACE_EFFECT_MAX_EXPANSION_ARITY, &evaluated_count,
                    &body)) {
                expansion_count++;
                for (uint32_t i = 0u; ok && i < evaluated_count; i++)
                    ok = space_effect_stack_push(
                        &stack, &len, &cap, inline_stack, evaluated[i],
                        false);
                if (ok)
                    ok = space_effect_stack_push(
                        &stack, &len, &cap, inline_stack, body, false);
                if (!ok)
                    break;
                continue;
            }
        }
        /* A tuple headed by a value or an expression reduces only through
         * an equation without a head symbol; where there is none, its
         * evaluation is its elements' (its head among them).  A variable
         * head may name any head once it holds a value. */
        if (head && head->kind != ATOM_SYMBOL && head->kind != ATOM_VAR &&
            !space_equations_may_match_unnamed_head(graph->space)) {
            for (CettaExprIndex i = 0u; ok && i < current->expr.len; i++) {
                Atom *child = current->expr.elems[i];
                if (child && child->kind == ATOM_EXPR)
                    ok = space_effect_stack_push(
                        &stack, &len, &cap, inline_stack, child, false);
            }
            if (!ok)
                break;
            continue;
        }
        if (head && head->kind == ATOM_VAR) {
            graph->nodes[node_index].base_effect =
                cetta_gslt_query_effect_join(
                    graph->nodes[node_index].base_effect,
                    SPACE_EFFECT_DYNAMIC_CALL);
        } else if (!head || head->kind != ATOM_SYMBOL) {
            graph->nodes[node_index].base_effect =
                cetta_gslt_query_effect_join(
                    graph->nodes[node_index].base_effect,
                    CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD);
        } else {
            has_control = cetta_gslt_fold_control_program(
                head->sym_id, current->expr.len - 1u, &control, &g_builtin_syms);
            bool grounded = is_grounded_op(head->sym_id);
            bool defined = space_equations_may_match_known_head(
                graph->space, head->sym_id);
            CettaGsltQueryEffect direct =
                space_generated_query_head_effect(head->sym_id);
            graph->nodes[node_index].base_effect =
                cetta_gslt_query_effect_join(
                    graph->nodes[node_index].base_effect, direct);
            /* A bind sequence (let*) is evaluated as nested binds, never
             * through the equations a library gives its head. */
            bool sequence = has_control &&
                control == CETTA_GSLT_FOLD_CONTROL_BIND_SEQUENCE;
            if (direct == CETTA_GSLT_QUERY_EFFECT_PURE &&
                !grounded && defined && !sequence &&
                !space_effect_node_add_dependency(
                    graph, node_index, head->sym_id)) {
                ok = false;
                break;
            }
            if (cetta_gslt_query_effect_children_opaque(
                    head->sym_id, &g_builtin_syms)) {
                continue;
            }
            /* An expression whose head names nothing still has its
             * elements evaluated: they are walked as any call's operands. */
        }
        for (CettaExprIndex i = 1u; i < current->expr.len; i++) {
            CettaGsltControlOperandRole role;
            bool has_role = has_control &&
                cetta_gslt_fold_control_operand_role(
                    control, i - 1u, &role);
            /* A case's or a switch's branches are pattern pairs as a bind
             * sequence's bindings are: only their values are evaluated. */
            if (!has_role && head && head->kind == ATOM_SYMBOL &&
                cetta_gslt_query_effect_operand_pattern_pairs(
                    head->sym_id, current->expr.len - 1u, i - 1u,
                    he_language, &g_builtin_syms)) {
                role = CETTA_GSLT_CONTROL_OPERAND_PATTERN_PAIRS;
                has_role = true;
            }
            if (has_role && role == CETTA_GSLT_CONTROL_OPERAND_PATTERN)
                continue;
            Atom *child = current->expr.elems[i];
            if (head && head->kind == ATOM_SYMBOL && child &&
                child->kind == ATOM_EXPR &&
                cetta_gslt_query_effect_operand_matched(
                    head->sym_id, current->expr.len - 1u, i - 1u,
                    he_language, &g_builtin_syms)) {
                if (!space_effect_stack_push(&stack, &len, &cap,
                                             inline_stack, child, true)) {
                    ok = false;
                    break;
                }
                continue;
            }
            /* Pattern pairs evaluate only their values; a malformed pair
             * leaves the form to the evaluator, so its effect is unknown. */
            if (has_role && role == CETTA_GSLT_CONTROL_OPERAND_PATTERN_PAIRS) {
                if (!child || child->kind != ATOM_EXPR) {
                    graph->nodes[node_index].base_effect =
                        cetta_gslt_query_effect_join(
                            graph->nodes[node_index].base_effect,
                            CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD);
                    continue;
                }
                for (CettaExprIndex k = 0u; k < child->expr.len; k++) {
                    Atom *pair = child->expr.elems[k];
                    if (!pair || pair->kind != ATOM_EXPR ||
                        pair->expr.len != 2u) {
                        graph->nodes[node_index].base_effect =
                            cetta_gslt_query_effect_join(
                                graph->nodes[node_index].base_effect,
                                CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD);
                        continue;
                    }
                    Atom *value = pair->expr.elems[1];
                    if (value && value->kind == ATOM_EXPR &&
                        !space_effect_stack_push(&stack, &len, &cap,
                                                 inline_stack, value,
                                                 false)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
                continue;
            }
            /* unquote runs the syntax its quoted operand holds. */
            if (head && head->kind == ATOM_SYMBOL &&
                head->sym_id == g_builtin_syms.unquote && child &&
                child->kind == ATOM_EXPR && child->expr.len == 2u &&
                atom_is_symbol_id(child->expr.elems[0],
                                  g_builtin_syms.quote))
                child = child->expr.elems[1];
            if (!child || child->kind != ATOM_EXPR)
                continue;
            if (!space_effect_stack_push(&stack, &len, &cap,
                                         inline_stack, child, false)) {
                ok = false;
                break;
            }
        }
        if (!ok)
            break;
    }
    if (stack != inline_stack)
        free(stack);
    if (expansions_live)
        arena_free(&expansions);
    return ok;
}

static bool space_effect_scan_node(
    SpaceEffectGraph *graph, size_t node_index) {
    SymbolId head = graph->nodes[node_index].head;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(graph->space, head, &cursor))
        return false;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step != SPACE_EQUATION_CURSOR_ITEM)
            return false;
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(id, &occurrence))
            return false;
        graph->nodes[node_index].defined = true;
        if (!space_effect_scan_rhs(
                graph, node_index, occurrence.rhs)) {
            return false;
        }
    }
    graph->nodes[node_index].scanned = true;
    return space_program_token_is_current(graph->program);
}

static void space_effect_graph_free(SpaceEffectGraph *graph) {
    if (!graph)
        return;
    for (size_t i = 0u; i < graph->len; i++)
        free(graph->nodes[i].dependencies);
    free(graph->nodes);
    cetta_symbol_index_free(&graph->by_head);
    memset(graph, 0, sizeof(*graph));
}

static bool space_effect_graph_solve(SpaceEffectGraph *graph) {
    for (size_t scan = 0u; scan < graph->len; scan++) {
        if (!space_effect_scan_node(graph, scan))
            return false;
    }
    /* A head's effect joins its base effect with its dependencies'.  The
     * effects are sets of bits joined by union, so each bit is its own
     * least fixed point (fixpoint.h): a head has it when some head it
     * reaches has it in its base. */
    size_t len = graph->len, edges = 0u;
    for (size_t i = 0u; i < len; i++)
        edges += graph->nodes[i].dependency_len;
    size_t *first = malloc((len + 1u) * sizeof(*first));
    size_t *targets = malloc((edges ? edges : 1u) * sizeof(*targets));
    bool *in = malloc((len ? len : 1u) * sizeof(*in));
    bool ok = first && targets && in;
    size_t used = 0u;
    unsigned bits = 0u;
    for (size_t i = 0u; ok && i < len; i++) {
        first[i] = used;
        bits |= (unsigned)graph->nodes[i].base_effect;
        graph->nodes[i].effect = graph->nodes[i].base_effect;
        for (size_t d = 0u; d < graph->nodes[i].dependency_len; d++) {
            size_t target = cetta_symbol_index_find(
                &graph->by_head, graph->nodes[i].dependencies[d]);
            if (target != SIZE_MAX)
                targets[used++] = target;
        }
    }
    CettaFixpointGraph dependencies = {len, first, targets};
    if (ok)
        first[len] = used;
    for (unsigned bit = 1u; ok && bit && bit <= bits; bit <<= 1u) {
        if (!(bits & bit))
            continue;
        for (size_t i = 0u; i < len; i++)
            in[i] = ((unsigned)graph->nodes[i].base_effect & bit) != 0u;
        ok = cetta_fixpoint_least(&dependencies, in);
        for (size_t i = 0u; ok && i < len; i++)
            if (in[i])
                graph->nodes[i].effect = cetta_gslt_query_effect_join(
                    graph->nodes[i].effect, (CettaGsltQueryEffect)bit);
    }
    free(first);
    free(targets);
    free(in);
    return ok && space_program_token_is_current(graph->program);
}

static CettaGsltQueryEffect space_query_effect_for_head_analyzed(
    Space *space, SymbolId head, bool *out_defined) {
    if (out_defined)
        *out_defined = false;
    if (!space || head == SYMBOL_ID_NONE)
        return CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD;

    CettaGsltQueryEffect direct =
        space_generated_query_head_effect(head);
    if (direct != CETTA_GSLT_QUERY_EFFECT_PURE) {
        if (out_defined)
            *out_defined = true;
        return direct;
    }
    if (is_grounded_op(head))
        return CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL;

    const SpaceEffectCacheEntry *cached =
        space_effect_cache_lookup(space, head);
    if (cached) {
        if (out_defined)
            *out_defined = cached->defined;
        return cached->effect;
    }
    if (!space_equations_may_match_known_head(space, head)) {
        space_effect_cache_store(
            space, head, CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL, false);
        return CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL;
    }

    SpaceEffectGraph graph = {
        .space = space,
        .program = space_program_token(space),
    };
    size_t root_index = 0u;
    if (!space_effect_graph_find_or_add(&graph, head, &root_index) ||
        !space_effect_graph_solve(&graph)) {
        space_effect_graph_free(&graph);
        return CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD;
    }

    CettaGsltQueryEffect result = graph.nodes[root_index].effect;
    bool defined = graph.nodes[root_index].defined;
    for (size_t i = 0u; i < graph.len; i++) {
        space_effect_cache_store(
            space, graph.nodes[i].head, graph.nodes[i].effect,
            graph.nodes[i].defined);
    }
    if (out_defined)
        *out_defined = defined;
    space_effect_graph_free(&graph);
    return result;
}

CettaGsltQueryEffect space_query_effect_for_head(
    Space *space, SymbolId head, bool *out_defined) {
    CettaGsltQueryEffect effect =
        space_query_effect_for_head_analyzed(space, head, out_defined);
    return (effect & SPACE_EFFECT_DYNAMIC_CALL)
        ? cetta_gslt_query_effect_join(
              (CettaGsltQueryEffect)(effect & ~SPACE_EFFECT_DYNAMIC_CALL),
              CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD)
        : effect;
}

CettaGsltQueryEffect space_query_static_effect_for_head(
    Space *space, SymbolId head, bool *out_defined, bool *out_dynamic) {
    CettaGsltQueryEffect effect =
        space_query_effect_for_head_analyzed(space, head, out_defined);
    if (out_dynamic)
        *out_dynamic = (effect & SPACE_EFFECT_DYNAMIC_CALL) != 0u;
    return (CettaGsltQueryEffect)(effect & ~SPACE_EFFECT_DYNAMIC_CALL);
}

static bool space_equation_child_ids_at_id(const Space *s, AtomId atom_id,
                                           AtomId *lhs_id_out, AtomId *rhs_id_out) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!tu_hdr(s->native.universe, atom_id) ||
        tu_kind(s->native.universe, atom_id) != ATOM_EXPR ||
        tu_arity(s->native.universe, atom_id) != 3 ||
        tu_head_sym(s->native.universe, atom_id) != g_builtin_syms.equals)
        return false;
    AtomId lhs_id = tu_child(s->native.universe, atom_id, 1);
    AtomId rhs_id = tu_child(s->native.universe, atom_id, 2);
    if (lhs_id == CETTA_ATOM_ID_NONE || rhs_id == CETTA_ATOM_ID_NONE)
        return false;
    *lhs_id_out = lhs_id;
    *rhs_id_out = rhs_id;
    return true;
}

static bool space_equation_children_at_id(const Space *s, AtomId atom_id,
                                          Atom **lhs_out, Atom **rhs_out) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    AtomId lhs_id = CETTA_ATOM_ID_NONE;
    AtomId rhs_id = CETTA_ATOM_ID_NONE;
    if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
        Atom *lhs = term_universe_get_atom(s->native.universe, lhs_id);
        Atom *rhs = term_universe_get_atom(s->native.universe, rhs_id);
        if (lhs && rhs) {
            *lhs_out = lhs;
            *rhs_out = rhs;
            return true;
        }
    }
    if (tu_hdr(s->native.universe, atom_id))
        return false;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    return atom ? is_equation_atom(atom, lhs_out, rhs_out) : false;
}

static bool space_type_annotation_child_ids_at_id(const Space *s,
                                                  AtomId atom_id,
                                                  AtomId *subject_id_out,
                                                  AtomId *type_id_out) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!tu_hdr(s->native.universe, atom_id) ||
        tu_kind(s->native.universe, atom_id) != ATOM_EXPR ||
        tu_arity(s->native.universe, atom_id) != 3 ||
        tu_head_sym(s->native.universe, atom_id) != g_builtin_syms.colon)
        return false;
    AtomId subject_id = tu_child(s->native.universe, atom_id, 1);
    AtomId type_id = tu_child(s->native.universe, atom_id, 2);
    if (subject_id == CETTA_ATOM_ID_NONE || type_id == CETTA_ATOM_ID_NONE)
        return false;
    *subject_id_out = subject_id;
    *type_id_out = type_id;
    return true;
}

static Atom *space_type_annotation_subject_at_id(const Space *s,
                                                 AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    AtomId subject_id = CETTA_ATOM_ID_NONE;
    AtomId type_id = CETTA_ATOM_ID_NONE;
    if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
        if (subject_id != CETTA_ATOM_ID_NONE)
            return term_universe_get_atom(s->native.universe, subject_id);
    }
    if (tu_hdr(s->native.universe, atom_id))
        return NULL;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 3 &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.colon))
        return atom->expr.elems[1];
    return NULL;
}

static void eq_index_rebuild(Space *s) {
    space_settle_marks(s);
    space_linearize(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD);
    eq_index_free(&s->native.eq_idx);
    eq_index_init(&s->native.eq_idx);
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        eq_index_note_directional_row(&s->native.eq_idx, s, atom_id);
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
            eq_index_add_id(&s->native.eq_idx, s, lhs_id, i, atom_id);
            continue;
        }
        Atom *lhs, *rhs;
        if (space_equation_children_at_id(s, atom_id, &lhs, &rhs))
            eq_index_add(&s->native.eq_idx, lhs, i, atom_id);
    }
    s->native.eq_idx_dirty = false;
}

static void ty_ann_index_rebuild(Space *s) {
    space_settle_marks(s);
    space_linearize(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TY_INDEX_REBUILD);
    ty_ann_index_free(&s->native.ty_idx);
    ty_ann_index_init(&s->native.ty_idx);
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        ty_ann_index_note_row(&s->native.ty_idx, s, atom_id);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
            ty_ann_index_add_id(&s->native.ty_idx, s, subject_id, i,
                                atom_id);
            continue;
        }
        Atom *subject = space_type_annotation_subject_at_id(s, atom_id);
        if (subject)
            ty_ann_index_add(&s->native.ty_idx, subject, i, atom_id);
    }
    s->native.ty_idx_dirty = false;
}

/* Lazily bring the presence bitset up to date with membership.  id_present is a
   projection of the stored atom ids keyed on ALPHA-canonical ids of EVERY atom
   (ground and non-ground), giving add-atom-nodup dedup an O(1) alpha-aware id
   predicate.  Maintenance is DEFERRED to query time (this function) rather than
   done per-add, for two reasons: (1) pure file ingress never forces a lazy blob
   decode -- the canonicalizing read only happens when membership is actually
   queried; (2) a forward-chaining deferral window (which defers the exact-index
   buckets) never triggers a full O(N) rebuild per contains.  A removal / bulk
   mutation sets id_present_dirty -> clear + full resync; otherwise the bitset is
   append-only and we sync just the [synced_len, len) suffix (O(1) amortized in a
   chaining loop, so native add-atom-nodup stays O(N), not O(N^2)).  Uses the
   logical atom-id accessor, so no linearization is required.  Failure leaves
   the projection dirty and forces callers onto the exact/alpha scan; an
   incomplete accelerator must never create a false negative. */
static bool id_present_sync(Space *s) {
    if (!s)
        return false;
    /* The bitset covers the rows; the facts kept as bits answer for
     * themselves. */
    CettaCount logical_len = space_length64(s) - space_dense_facts(s);
    if (s->native.id_present_dirty ||
        s->native.id_present_synced_len > logical_len) {
        id_present_clear(&s->native);
        s->native.id_present_synced_len = 0;
    }
    for (CettaIndex i = s->native.id_present_synced_len;
         i < logical_len; i++) {
        AtomId stored_id = space_get_atom_id_at64(s, i);
        if (!tu_hdr(s->native.universe, stored_id)) {
            Atom *stored =
                term_universe_get_atom(s->native.universe, stored_id);
            /* Pointer-backed runtime values do not have lawful structural
               intern keys.  Leave them to the exact/alpha scan; stable queries
               can still use the projection built from the remaining rows. */
            if (stored && !term_universe_atom_is_stable(stored))
                continue;
        }
        AtomId cid = space_canonical_id_for_stored(s, stored_id);
        if (cid == CETTA_ATOM_ID_NONE ||
            !id_present_set(&s->native, cid) ||
            (cid != stored_id && !id_present_key_hold(&s->native, cid))) {
            id_present_clear(&s->native);
            s->native.id_present_synced_len = 0;
            s->native.id_present_dirty = true;
            return false;
        }
    }
    s->native.id_present_synced_len = logical_len;
    s->native.id_present_dirty = false;
    return true;
}

static void exact_index_rebuild(Space *s) {
    space_settle_marks(s);
    space_linearize(s);
    exact_index_free(&s->native.exact_idx);
    if (s->native.len)
        exact_index_reserve(&s->native.exact_idx, (size_t)s->native.len,
                            s->native.len >= UINT32_MAX);
    for (CettaIndex i = 0; i < s->native.len; i++)
        exact_index_note_row(s, space_get_atom_id_at64(s, i), i);
    /* id_present is now decoupled from the bucket index -- it is synced lazily at
       membership-query time (id_present_sync), so the bucket rebuild does not
       touch it.  space_linearize preserves logical order, so id_present_synced_len
       stays valid across a bucket rebuild. */
    s->native.exact_idx_dirty = false;
}

static void recompute_has_non_exact_atoms(Space *s) {
    space_settle_marks(s);
    if (!s) return;
    s->native.has_non_exact_atoms = false;
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        if (!atom_id_is_exact_indexable(s, atom_id)) {
            s->native.has_non_exact_atoms = true;
            break;
        }
    }
    s->native.has_non_exact_atoms_dirty = false;
}

/* A module changed: every space importing it reads the change. */
static __attribute__((noinline, cold)) void space_notify_importers(
        Space *s, SpaceMutationEquationProjection equation_projection,
        SpaceMutationPrefixEffect prefix_effect) {
    for (uint32_t i = 0u; i < s->importer_count; i++)
        space_note_dependency_mutation(
            s->importers[i], equation_projection,
            prefix_effect == SPACE_MUTATION_PREFIX_REWRITTEN);
}

static void space_publish_mutation(
        Space *s, SpaceMutationEquationProjection equation_projection,
        SpaceMutationPrefixEffect prefix_effect) {
    if (!s)
        return;
    if (__builtin_expect(s->native.dense != NULL, 0) &&
        s->native.dense->settling)
        return;
    if (s->revision == UINT64_MAX) {
        fputs("CeTTa: exhausted Space revision counter\n", stderr);
        abort();
    }
    s->revision++;
    if (prefix_effect == SPACE_MUTATION_PREFIX_REWRITTEN) {
        s->prefix_epoch = space_fresh_prefix_epoch();
    } else if (prefix_effect != SPACE_MUTATION_PREFIX_APPEND_ONLY) {
        abort();
    }
    if (equation_projection &
            ~(SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
              SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE |
              SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
              SPACE_MUTATION_EQUATION_PROJECTION_SUBTYPE)) {
        abort();
    }
    if (equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_SUBTYPE |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE))
        s->declares_subtypes = true;
    if (equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE |
             SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION)) {
        space_execution_analysis_note_mutation(s);
    }
    /* The publication counters describe the equation-occurrence projection:
     * a declaration-only mutation is data-only for that projection. */
    if (equation_projection & SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_MUTATION_PUBLISH_EQUATION);
    } else if (equation_projection &
               SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_MUTATION_PUBLISH_OPAQUE);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_MUTATION_PUBLISH_DATA_ONLY);
    }
    if (equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)) {
        if (s->declaration_revision == UINT64_MAX) {
            fputs("CeTTa: exhausted Space declaration revision counter\n",
                  stderr);
            abort();
        }
        s->declaration_revision++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_DECLARATION_REVISION_BUMP);
    }
    if (equation_projection &
            (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
             SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)) {
        if (s->equation_revision == UINT64_MAX) {
            fputs("CeTTa: exhausted Space equation revision counter\n", stderr);
            abort();
        }
        s->equation_revision++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_SPACE_EQUATION_REVISION_BUMP);
    }
    if (__builtin_expect(s->importer_count != 0u, 0))
        space_notify_importers(s, equation_projection, prefix_effect);
    space_advance_global_mutation_epoch();
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_REVISION_BUMP);
}

void space_note_external_backend_mutation(Space *s) {
    if (!s)
        return;
    space_mark_derived_state_dirty(s);
    space_publish_mutation(
        s, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE,
        SPACE_MUTATION_PREFIX_REWRITTEN);
}

static bool space_defer_incremental_secondary_indices(const Space *s) {
    return s &&
           ((s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
             !s->match_backend.pathmap.bridge.preserve_logical_order) ||
            s->native.secondary_index_deferral_depth > 0);
}

/* Whether a set holds the atom stored as `atom_id`, or an alpha-variant of
 * it.  A ground atom is held when its interned id is: the exact index says so
 * while a removal leaves the presence bitset to a full resync, the bitset
 * otherwise.  An open atom is compared through its canonical form. */
/* Whether a set holds a row whose alpha key is `key`, when its presence
 * bitset, keyed by alpha keys, can say: false leaves the caller to compare
 * atoms. */
static bool space_set_holds_key(Space *s, AtomId key, bool *held) {
    TermUniverse *universe = s->native.universe;
    if (key == CETTA_ATOM_ID_NONE || !universe ||
        !space_tracks_atom_ids(s) || !tu_hdr(universe, key) ||
        space_has_dependencies(s) || space_has_overlay_base(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        !id_present_sync(s))
        return false;
    *held = id_present_contains(&s->native, key);
    return true;
}

static bool space_set_holds(Space *s, AtomId atom_id, Atom *atom) {
    TermUniverse *universe = s->native.universe;
    if (atom_id != CETTA_ATOM_ID_NONE && space_tracks_atom_ids(s) &&
        universe && tu_hdr(universe, atom_id) &&
        !space_has_dependencies(s) && !space_has_overlay_base(s) &&
        (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
         s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)) {
        if (!tu_has_vars(universe, atom_id)) {
            if (s->native.id_present_dirty && !s->native.exact_idx_dirty)
                return exact_index_holds_id(&s->native.exact_idx, atom_id);
            if (id_present_sync(s))
                return id_present_contains(&s->native, atom_id);
        } else {
            /* An open row is held, as itself or as a variant, when its
             * key is; the key is read from its record. */
            bool held = false;
            if (space_set_holds_key(
                    s, term_universe_alpha_key_id(universe, atom_id), &held))
                return held;
        }
    }
    if (!atom && space_tracks_atom_ids(s) && atom_id != CETTA_ATOM_ID_NONE)
        atom = term_universe_get_atom(universe, atom_id);
    return atom && space_contains_for_add_nodup(s, atom);
}

static void space_add_stored_id_as(Space *s, AtomId atom_id,
                                   Atom *backend_atom, bool set_checked);

static void space_add_stored_id(Space *s, AtomId atom_id, Atom *backend_atom) {
    space_add_stored_id_as(s, atom_id, backend_atom, false);
}

/* `set_checked`: the caller has just found, by the presence bitset, that
 * the set holds no copy of the atom and no alpha-variant, and `atom_id` is
 * the atom's own presence key (its alpha key). */
static void space_add_stored_id_as(Space *s, AtomId atom_id,
                                   Atom *backend_atom, bool set_checked) {
    if (space_dense_route_add(s, atom_id))
        return;
    space_dense_note_row(s, atom_id);
    /* A set adds an atom only when it holds no copy of it, nor an
     * alpha-variant. */
    if (s->kind == SPACE_KIND_SET && !set_checked &&
        space_set_holds(s, atom_id, backend_atom))
        return;
    bool queue_gap = space_is_queue(s) && s->native.start != 0;
    bool backend_needs_atom =
        !queue_gap && space_match_backend_needs_atom_on_add(s, atom_id);
    bool defer_secondary =
        space_defer_incremental_secondary_indices(s);
    /* The stored atom, which holds no evaluated mark, stands for one that
     * may: the space keeps syntax, also in its equation index. */
    if ((backend_needs_atom || atom_may_hold_he_mark(backend_atom)) &&
        space_tracks_atom_ids(s) && atom_id != CETTA_ATOM_ID_NONE) {
        Atom *stored_backend_atom =
            term_universe_get_atom(s->native.universe, atom_id);
        if (stored_backend_atom) {
            backend_atom = stored_backend_atom;
        } else if (!backend_atom) {
            return;
        }
    }
    CettaIndex idx = s->native.len;
    if (space_is_queue(s)) {
        space_queue_reserve_tail(s, 1);
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits,
            s->native.start + s->native.len, atom_id);
    } else {
        space_reserve_linear(s, s->native.len + 1);
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits, s->native.len,
            atom_id);
    }
    s->native.len++;
    space_hold_row(s, atom_id);
    /* A row stored as its own presence key, the bitset synced up to it:
     * the bitset takes it now, and no later sync reads its key again. */
    if (set_checked && !s->native.id_present_dirty &&
        space_dense_facts(s) == 0u &&
        s->native.id_present_synced_len + 1u == s->native.len &&
        id_present_set(&s->native, atom_id))
        s->native.id_present_synced_len = s->native.len;
    space_note_atom_id_storage_peak(s);
    space_publish_mutation(
        s, space_atom_equation_projection(s, atom_id, backend_atom),
        SPACE_MUTATION_PREFIX_APPEND_ONLY);
    if (queue_gap) {
        space_mark_indexes_dirty(s);
        space_match_backend_note_remove(s);
        return;
    }
    if (defer_secondary) {
        s->native.eq_idx_dirty = true;
        s->native.ty_idx_dirty = true;
        s->native.exact_idx_dirty = true;
        s->native.has_non_exact_atoms_dirty = true;
        /* id_present is synced lazily at query time (append-only suffix via
           id_present_synced_len), so a deferred add needs no per-add work here --
           this is what keeps a forward-chaining loop O(1) per contains AND keeps
           pure ingress free of lazy blob decodes. */
    } else {
        /* Index equations by head symbol */
        if (!s->native.eq_idx_dirty) {
            eq_index_note_directional_row(&s->native.eq_idx, s, atom_id);
            AtomId lhs_id = CETTA_ATOM_ID_NONE;
            AtomId rhs_id = CETTA_ATOM_ID_NONE;
            if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
                eq_index_add_id(&s->native.eq_idx, s, lhs_id, idx, atom_id);
            } else {
                Atom *lhs, *rhs;
                if (space_equation_children_at_id(s, atom_id, &lhs, &rhs))
                    eq_index_add(&s->native.eq_idx, lhs, idx, atom_id);
            }
        }
        /* Index type annotations (: atom type) */
        if (!s->native.ty_idx_dirty) {
            ty_ann_index_note_row(&s->native.ty_idx, s, atom_id);
            AtomId subject_id = CETTA_ATOM_ID_NONE;
            AtomId type_id = CETTA_ATOM_ID_NONE;
            if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
                ty_ann_index_add_id(&s->native.ty_idx, s, subject_id, idx,
                                    atom_id);
            } else {
                Atom *subject = space_type_annotation_subject_at_id(s, atom_id);
                if (subject)
                    ty_ann_index_add(&s->native.ty_idx, subject, idx, atom_id);
            }
        }
        if (!s->native.exact_idx_dirty)
            exact_index_note_row(s, atom_id, idx);
        /* id_present is synced lazily at membership-query time (id_present_sync),
           not per-add, so pure ingress never forces a canonicalizing blob decode. */
        if (!s->native.has_non_exact_atoms_dirty &&
            !atom_id_is_exact_indexable(s, atom_id))
            s->native.has_non_exact_atoms = true;
    }
    /* Match backend owns its own incremental indexing policy. */
    if (backend_needs_atom)
        cetta_provenance_assert_not_transient(backend_atom,
                                             "space.backend.note_add");
    space_match_backend_note_add(s, atom_id,
                                 backend_needs_atom ? backend_atom : NULL, idx);
}

void space_add_atom_id(Space *s, AtomId atom_id) {
    s = space_mutation_target(s);
    bool transition_failed = false;
    if (!s || !space_tracks_atom_ids(s) || atom_id == CETTA_ATOM_ID_NONE)
        return;
    if (space_store_via_backend_primary(
            s, atom_id, NULL, &transition_failed))
        return;
    if (transition_failed)
        return;
    space_add_stored_id(s, atom_id, NULL);
}

/* Before a mutation that publishes a prefix rewrite: pinned readers keep
 * the rows and marks they began with, so none is left reading rows that
 * move, and the marks are contracted. */
static void space_prepare_prefix_rewrite(Space *s) {
    space_pinned_occurrences_detach(s);
    space_settle_marks(s);
}

static bool space_add_atom_ids_batch_bag(Space *s, const AtomId *atom_ids,
                                         CettaCount atom_count);

/* A set adds the atoms of a batch it holds no copy of, counting the batch's
 * own earlier atoms.  When every atom is ground, its interned id decides:
 * the presence bitset answers for the rows held, a table local to the batch
 * for the batch, and the new atoms are appended as a bag appends a batch.
 * Otherwise each atom is checked as it is added. */
static bool space_set_add_ids_batch(Space *s, const AtomId *atom_ids,
                                    CettaCount atom_count) {
    TermUniverse *universe = s->native.universe;
    bool by_id = atom_count > 1u && space_tracks_atom_ids(s) && universe &&
        !space_has_dependencies(s) && !space_has_overlay_base(s) &&
        (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
         s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) &&
        atom_count <= SIZE_MAX / 4u / sizeof(AtomId);
    for (CettaCount i = 0u; by_id && i < atom_count; i++) {
        by_id = atom_ids[i] != CETTA_ATOM_ID_NONE &&
            tu_hdr(universe, atom_ids[i]) &&
            !tu_has_vars(universe, atom_ids[i]);
    }
    if (by_id && s->native.id_present_dirty && !s->native.exact_idx_dirty)
        by_id = false;
    if (!by_id || !id_present_sync(s)) {
        for (CettaCount i = 0u; i < atom_count; i++)
            space_add_atom_id(s, atom_ids[i]);
        return true;
    }
    size_t capacity = 16u;
    while (capacity < (size_t)atom_count * 2u)
        capacity <<= 1u;
    AtomId *seen = cetta_malloc(sizeof(*seen) * capacity);
    AtomId *fresh = cetta_malloc(sizeof(*fresh) * (size_t)atom_count);
    for (size_t i = 0u; i < capacity; i++)
        seen[i] = CETTA_ATOM_ID_NONE;
    CettaCount fresh_len = 0u;
    for (CettaCount i = 0u; i < atom_count; i++) {
        AtomId id = atom_ids[i];
        if (id_present_contains(&s->native, id))
            continue;
        uint64_t h = (uint64_t)id * UINT64_C(0x9e3779b97f4a7c15);
        size_t slot = (size_t)(h >> 32) & (capacity - 1u);
        while (seen[slot] != CETTA_ATOM_ID_NONE && seen[slot] != id)
            slot = (slot + 1u) & (capacity - 1u);
        if (seen[slot] == id)
            continue;
        seen[slot] = id;
        fresh[fresh_len++] = id;
    }
    bool added = space_add_atom_ids_batch_bag(s, fresh, fresh_len);
    free(seen);
    free(fresh);
    return added;
}

bool space_add_atom_ids_batch(Space *s, const AtomId *atom_ids,
                              CettaCount atom_count) {
    if (s && s->kind == SPACE_KIND_SET)
        return space_set_add_ids_batch(s, atom_ids, atom_count);
    return space_add_atom_ids_batch_bag(s, atom_ids, atom_count);
}

static bool space_add_atom_ids_batch_rows(Space *s, const AtomId *atom_ids,
                                          CettaCount atom_count);

/* The facts of a batch the relations take leave it; the rest are rows. */
static bool space_add_atom_ids_batch_bag(Space *s, const AtomId *atom_ids,
                                         CettaCount atom_count) {
    Space *target = space_mutation_target(s);
    if (!target || atom_count == 0u || !atom_ids ||
        !space_dense_admits(target))
        return space_add_atom_ids_batch_rows(s, atom_ids, atom_count);
    space_dense_prepare_adds(target, atom_ids, atom_count);
    AtomId *rows = cetta_malloc((size_t)atom_count * sizeof(*rows));
    CettaCount row_count = 0u;
    for (CettaCount i = 0u; i < atom_count; i++) {
        if (!space_dense_route_add(target, atom_ids[i]))
            rows[row_count++] = atom_ids[i];
    }
    bool added = row_count == 0u ||
        space_add_atom_ids_batch_rows(s, rows, row_count);
    free(rows);
    return added;
}

static bool space_add_atom_ids_batch_rows(Space *s, const AtomId *atom_ids,
                                          CettaCount atom_count) {
    Space *origin = s;
    uint64_t added = 0;
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool batch_has_non_exact_atom = false;
    SpaceMutationEquationProjection equation_projection =
        SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY;
    SpaceBackendBatchResult batch_result;

    s = space_mutation_target(s);

    if (!s || (!atom_ids && atom_count != 0))
        return false;
    if (atom_count == 0)
        return true;
    for (CettaCount i = 0; i < atom_count; i++)
        space_dense_note_row(s, atom_ids[i]);
    for (CettaCount i = 0; i < atom_count; i++) {
        equation_projection = space_merge_equation_projection(
            equation_projection,
            space_atom_equation_projection(s, atom_ids[i], NULL));
    }

    /* A data-only native batch is one append publication. The authoritative
     * ordered occurrence array retains duplicates, while secondary indexes
     * may be rebuilt when queried. Existing pinned cursors keep their ceiling.
     * Rows are appended after any marked ones, as a single addition appends:
     * only the other routes rewrite the prefix, and settle the marks first. */
    bool appends = !space_has_overlay_base(s) && !space_is_queue(s) &&
        s->match_backend.kind == SPACE_ENGINE_NATIVE &&
        equation_projection == SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY &&
        space_tracks_atom_ids(s);
    if (!appends)
        space_prepare_prefix_rewrite(origin);
    if (appends) {
        if (atom_count > UINT64_MAX - s->native.len ||
            !space_sync_atom_id_storage_width(s))
            return false;
        CettaIndex next_len = s->native.len + atom_count;
        size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        if (!width || next_len > SIZE_MAX / width)
            return false;
        space_reserve_linear(s, next_len);
        CettaIndex start = s->native.len;
        for (CettaCount i = 0u; i < atom_count; i++) {
            if (!space_atom_id_storage_store_at(s->native.atom_ids,
                    s->native.atom_id_width_bits, start + i, atom_ids[i]))
                return false;
            if (!s->native.ty_idx_dirty)
                ty_ann_index_note_row(&s->native.ty_idx, s, atom_ids[i]);
        }
        s->native.len = next_len;
        for (CettaCount i = 0u; i < atom_count; i++)
            space_hold_row(s, atom_ids[i]);
        /* A current exact index stays current, a row at a time; a dirty
         * one would cost its next reader the whole space. */
        if (!s->native.exact_idx_dirty) {
            for (CettaCount i = 0u; i < atom_count; i++)
                exact_index_note_row(s, atom_ids[i], start + i);
        }
        s->native.has_non_exact_atoms_dirty = true;
        /* Every index kept current a row at a time hears of the rows: one
         * left behind is rebuilt whole by its next reader. */
        SpaceMatchNativeState *native = &s->match_backend.native;
        if ((native->match_trie && !native->match_trie_dirty) ||
            (native->stree && !native->stree_dirty) ||
            native->path_indexes || native->head_index) {
            for (CettaCount i = 0u; i < atom_count; i++)
                space_match_backend_note_add(s, atom_ids[i], NULL, start + i);
        }
        space_note_atom_id_storage_peak(s);
        space_publish_mutation(s, equation_projection, SPACE_MUTATION_PREFIX_APPEND_ONLY);
        return true;
    }

    if (!space_has_overlay_base(s) &&
        s->match_backend.kind == SPACE_ENGINE_PATHMAP) {
        bool requires_authored_order = false;
        for (CettaCount i = 0; i < atom_count; i++) {
            if (space_atom_id_requires_authored_order(
                    s, atom_ids[i], NULL)) {
                requires_authored_order = true;
                break;
            }
        }
        if (requires_authored_order) {
            if (!space_match_backend_require_logical_order(s, NULL))
                return false;
            for (CettaCount i = 0; i < atom_count; i++)
                space_add_stored_id(s, atom_ids[i], NULL);
            return space_term_universe_last_error_code(s) ==
                   TERM_UNIVERSE_ERROR_NONE;
        }
    }

    keep_pathmap_exact_metadata =
        !space_has_overlay_base(s) &&
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    if (keep_pathmap_exact_metadata) {
        for (CettaCount i = 0; i < atom_count; i++) {
            if (!atom_id_is_exact_indexable(s, atom_ids[i])) {
                batch_has_non_exact_atom = true;
                break;
            }
        }
    }

    batch_result = space_has_overlay_base(s)
        ? SPACE_BACKEND_BATCH_UNSUPPORTED
        : space_match_backend_store_atom_ids_batch_direct(
              s, atom_ids, atom_count, &added);
    if (batch_result == SPACE_BACKEND_BATCH_ERROR)
        return false;
    if (batch_result == SPACE_BACKEND_BATCH_APPLIED) {
        if (added != (uint64_t)atom_count)
            return false;
        for (CettaCount i = 0; i < atom_count; i++) {
            space_append_backend_primary_shadow_id(s, atom_ids[i]);
            space_mark_backend_primary_atom_mutation(
                s, atom_ids[i], NULL);
        }
        if (keep_pathmap_exact_metadata) {
            s->native.has_non_exact_atoms =
                had_non_exact_atom || batch_has_non_exact_atom;
            s->native.has_non_exact_atoms_dirty = false;
        }
        space_publish_mutation(
            s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
        return true;
    }

    /* Oracle replay keeps ordered multiplicity and overlay behavior for every
       fragment the backend declines, including variables and wide symbols. */
    for (CettaCount i = 0; i < atom_count; i++)
        space_add_atom_id(s, atom_ids[i]);
    return space_term_universe_last_error_code(s) == TERM_UNIVERSE_ERROR_NONE;
}

void space_add(Space *s, Atom *atom) {
    s = space_mutation_target(s);
    if (!s)
        return;
    SPACE_STORE_SCOPE();
    AtomId atom_id = CETTA_ATOM_ID_NONE;
    bool transition_failed = false;
    if (space_store_atom_via_backend_primary(
            s, atom, &transition_failed))
        return;
    if (transition_failed)
        return;
    if (space_tracks_atom_ids(s) && atom) {
        if (space_dense_add_parts(s, atom))
            return;
        atom_id = term_universe_store_atom_id(s->native.universe, NULL, atom);
        if (atom_id == CETTA_ATOM_ID_NONE)
            return;
        if (space_store_via_backend_primary(
                s, atom_id, atom, &transition_failed))
            return;
        if (transition_failed)
            return;
    }
    space_add_stored_id(s, atom_id, atom);
}

static bool space_term_universe_failed(const Space *s) {
    TermUniverse *universe = s ? s->native.universe : NULL;
    return term_universe_last_error_code(universe) != TERM_UNIVERSE_ERROR_NONE;
}

TermUniverseError space_term_universe_last_error_code(const Space *s) {
    TermUniverse *universe = s ? s->native.universe : NULL;
    return term_universe_last_error_code(universe);
}


/* The route space_admit_atom_impl takes for an open row of a PeTTa
 * set keeping its own records: no backend stores the row directly, its
 * relations never take a row with variables, and the row is kept as
 * its alpha key when the key forms. */
bool space_keys_open_row(Space *s, Atom *atom) {
    s = space_mutation_target(s);
    return s && atom && atom_has_vars(atom) && s->kind == SPACE_KIND_SET &&
        space_effect_language() == CETTA_LANGUAGE_PETTA &&
        !(s->match_backend.ops && s->match_backend.ops->store_atom_direct) &&
        space_tracks_atom_ids(s) && term_universe_alpha_key_forms(atom);
}

static bool space_admit_atom_impl(Space *s, Arena *fallback,
                                  const Arena *source_arena, Atom *atom) {
    s = space_mutation_target(s);
    bool transition_failed = false;
    if (!s || !atom)
        return false;
    SPACE_STORE_SCOPE();

    if (space_store_atom_via_backend_primary(
            s, atom, &transition_failed))
        return true;
    if (transition_failed)
        return false;

    if (space_tracks_atom_ids(s)) {
        if (space_dense_add_parts(s, atom))
            return true;
        /* Deduplication by design: a set identifies a row with its
         * variants, and PeTTa never shows a stored variable's name, so the
         * row is kept as its alpha key, its own key: one record a variant,
         * as a trie keyed by first occurrences keeps one path.  The key is
         * interned first: a set holding a variant holds the key, which then
         * exists already, so nothing grows, and its presence decides. */
        bool open_row = s->kind == SPACE_KIND_SET && atom_has_vars(atom);
        bool set_checked = false;
        AtomId atom_id = CETTA_ATOM_ID_NONE;
        if (open_row && space_effect_language() == CETTA_LANGUAGE_PETTA) {
            atom_id = term_universe_store_alpha_key(s->native.universe, atom);
            bool held = false;
            if (space_set_holds_key(s, atom_id, &held)) {
                if (held)
                    return true;
                set_checked = true;
            }
        }
        /* Otherwise a set already holding an alpha-variant of an open atom
         * adds nothing, and the variant, whose variables are fresh, is not
         * interned. */
        if (open_row && atom_id == CETTA_ATOM_ID_NONE &&
            space_set_holds(s, CETTA_ATOM_ID_NONE, atom))
            return true;
        if (atom_id == CETTA_ATOM_ID_NONE)
            atom_id = source_arena
                ? term_universe_store_atom_id_from_source_arena(
                      s->native.universe, fallback, source_arena, atom)
                : term_universe_store_atom_id(
                      s->native.universe, fallback, atom);
        if (atom_id != CETTA_ATOM_ID_NONE) {
            if (space_store_via_backend_primary(
                    s, atom_id, atom, &transition_failed))
                return true;
            if (transition_failed)
                return false;
            space_add_stored_id_as(s, atom_id, atom, set_checked);
            return true;
        }
        if (space_term_universe_failed(s))
            return false;
        Atom *stored = space_store_atom(s, fallback, atom);
        if (!stored)
            return false;
        AtomId stored_id =
            term_universe_lookup_atom_id(s->native.universe, stored);
        if (stored_id == CETTA_ATOM_ID_NONE)
            return false;
        space_add_atom_id(s, stored_id);
        return true;
    }

    Atom *stored = space_store_atom(s, fallback, atom);
    if (!stored)
        return false;
    CettaIndex len_before = s->native.len;
    space_add(s, stored);
    return s->native.len == len_before + 1;
}

bool space_admit_atom(Space *s, Arena *fallback, Atom *atom) {
    return space_admit_atom_impl(s, fallback, NULL, atom);
}

bool space_admit_atom_from_source_arena(
    Space *s, Arena *fallback, const Arena *source_arena, Atom *atom) {
    return space_admit_atom_impl(s, fallback, source_arena, atom);
}

bool space_admit_atoms_batch_from_source_arena(
    Space *s, Arena *fallback, const Arena *source_arena,
    Atom *const *atoms, CettaCount atom_count) {
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))
    CettaSharedTransitionGuard shared_transition = {0};
    cetta_shared_transition_guard_enter(&shared_transition);
    s = space_mutation_target(s);
    if (!s || (!atoms && atom_count != 0u) ||
        atom_count > SIZE_MAX / sizeof(AtomId)) {
        return false;
    }
    if (atom_count == 0u)
        return true;

    /* Engines without canonical ids retain the exact singular path.  The
     * id-backed engines can prepare the whole occurrence block without
     * publishing it, then use their existing bag-preserving batch store. */
    if (!space_tracks_atom_ids(s)) {
        for (CettaCount i = 0u; i < atom_count; i++) {
            if (!space_admit_atom_impl(
                    s, fallback, source_arena, atoms[i])) {
                return false;
            }
        }
        return true;
    }

    SPACE_STORE_SCOPE();
    AtomId *ids = cetta_malloc(sizeof(*ids) * (size_t)atom_count);
    if (!ids)
        return false;
    bool ready = true;
    for (CettaCount i = 0u; i < atom_count; i++) {
        if (!atoms[i]) {
            ready = false;
            break;
        }
        ids[i] = source_arena
            ? term_universe_store_atom_id_from_source_arena(
                  s->native.universe, fallback, source_arena, atoms[i])
            : term_universe_store_atom_id(
                  s->native.universe, fallback, atoms[i]);
        if (ids[i] == CETTA_ATOM_ID_NONE) {
            ready = false;
            break;
        }
    }
    bool stored = ready && space_add_atom_ids_batch(s, ids, atom_count);
    free(ids);
    return stored;
}

Space *space_heap_clone_shallow(Space *src) {
    Space *clone;
    CettaCount logical_len = 0;
    uint32_t narrow_len = 0;
    if (!src)
        return NULL;
    logical_len = space_length64(src);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_SPACE_SHALLOW_CLONE_CALL);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_SPACE_SHALLOW_CLONE_ROW,
        logical_len);
    if (src->native.len != logical_len) {
        if (!space_length_u32_checked(src, &narrow_len))
            return NULL;
        logical_len = narrow_len;
    }
    clone = cetta_malloc(sizeof(Space));
    if (!clone)
        return NULL;
    space_init_with_universe(clone, src ? src->native.universe : NULL);
    clone->kind = src->kind;
    space_set_strict(clone, src->strict);
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (CettaIndex i = 0; i < logical_len; i++) {
        AtomId atom_id = space_get_atom_id_at64(src, i);
        Atom *source_atom = NULL;
        if (atom_id != CETTA_ATOM_ID_NONE) {
            space_add_atom_id(clone, atom_id);
            continue;
        }
        source_atom = space_get_at64(src, i);
        if (!source_atom) {
            space_match_backend_set_error(
                SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
            space_free(clone);
            free(clone);
            return NULL;
        }
        if (!space_admit_atom(clone, NULL, source_atom))
            space_add(clone, source_atom);
    }
    /* A copy reads through the same modules as its source. */
    if (!space_copy_module_view(clone, src)) {
        space_free(clone);
        free(clone);
        return NULL;
    }
    return clone;
}

static void space_replace_contents_classified(
        Space *dst, Space *src,
        SpaceMutationEquationProjection equation_projection) {
    space_dense_settle(dst, SYMBOL_ID_NONE);
    space_dense_settle(src, SYMBOL_ID_NONE);
    space_settle_marks(dst);
    space_settle_marks(src);
    if (!dst || !src || dst == src)
        return;
    uint64_t dst_instance_id = dst->instance_id;
    uint64_t old_revision = dst->revision;
    uint64_t src_revision = src->revision;
    uint64_t old_equation_revision = dst->equation_revision;
    uint64_t src_equation_revision = src->equation_revision;
    uint64_t old_declaration_revision = dst->declaration_revision;
    uint64_t src_declaration_revision = src->declaration_revision;
    /* Imported atoms travel with the contents: a source that has them passes
       them on, and a rebuild that starts with the destination's imported
       atoms, as a rebuild in logical order does, keeps them. */
    CettaIndex imported = src->imported_len;
    if (imported == 0u) {
        imported = dst->imported_len;
        if (imported > space_length64(src))
            imported = 0u;
        for (CettaIndex i = 0u; i < imported; i++) {
            if (space_get_atom_id_at64(dst, i) !=
                space_get_atom_id_at64(src, i)) {
                imported = 0u;
                break;
            }
        }
    }
    /* The links are the space's own: dst keeps its dependencies and its
       importers, and gains the dependencies src added, so a committed
       transaction keeps the imports made inside it, while a rebuild from
       a fresh copy loses none.  The moved-from source's links end. */
    Space **deps = dst->deps, **importers = dst->importers;
    uint32_t dep_count = dst->dep_count, dep_cap = dst->dep_cap;
    uint32_t importer_count = dst->importer_count;
    uint32_t importer_cap = dst->importer_cap;
    dst->deps = dst->importers = NULL;
    dst->dep_count = dst->dep_cap = 0u;
    dst->importer_count = dst->importer_cap = 0u;
    space_free(dst);
    space_move_storage_and_backend(dst, src);
    dst->imported_len = imported;
    dst->deps = deps;
    dst->dep_count = dep_count;
    dst->dep_cap = dep_cap;
    dst->importers = importers;
    dst->importer_count = importer_count;
    dst->importer_cap = importer_cap;
    for (uint32_t i = 0u; i < src->dep_count; i++)
        (void)space_add_dependency(dst, src->deps[i]);
    space_dependencies_unlink(src);
    space_detach_from_universe(src);
    space_attach_to_universe(dst, dst->native.universe);
    dst->instance_id = dst_instance_id;
    dst->revision = old_revision > src_revision ? old_revision : src_revision;
    dst->equation_revision =
        (equation_projection &
         (SPACE_MUTATION_EQUATION_PROJECTION_RELEVANT |
          SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)) == 0
            ? old_equation_revision
            : old_equation_revision > src_equation_revision
                ? old_equation_revision : src_equation_revision;
    dst->declaration_revision =
        (equation_projection &
         (SPACE_MUTATION_EQUATION_PROJECTION_DECLARATION |
          SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE)) == 0
            ? old_declaration_revision
            : old_declaration_revision > src_declaration_revision
                ? old_declaration_revision : src_declaration_revision;
    space_publish_mutation(
        dst, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    space_reset_moved_from(src);
}

void space_replace_contents(Space *dst, Space *src) {
    space_replace_contents_classified(
        dst, src, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE);
}

/* ── Query Results ──────────────────────────────────────────────────────── */

void query_results_init(QueryResults *qr) {
    memset(qr->inline_items, 0, sizeof(qr->inline_items));
    qr->items = qr->inline_items;
    qr->len = 0;
    qr->cap = 1;
}

static CettaCount query_results_capacity_limit(void) {
    if (g_query_results_capacity_limit_override != 0)
        return g_query_results_capacity_limit_override;
    return (CettaCount)(SIZE_MAX / sizeof(QueryResult));
}

void query_results_diag_set_capacity_limit_override(CettaCount limit) {
    g_query_results_capacity_limit_override = limit;
}

static void query_results_fatal_capacity(CettaCount len, CettaCount limit,
                                         size_t bytes) {
    fprintf(stderr,
            "fatal: query result capacity exhausted "
            "(len=%" PRIu64 " limit=%" PRIu64 " next-bytes=%zu)\n",
            (uint64_t)len, (uint64_t)limit, bytes);
    abort();
}

static bool query_results_ensure_one(QueryResults *qr) {
    CettaCount limit;
    CettaCount need;
    CettaCount next;
    if (!qr)
        return false;
    limit = query_results_capacity_limit();
    if (qr->len >= limit)
        query_results_fatal_capacity(qr->len, limit, 0u);
    if (qr->len < qr->cap)
        return true;
    need = qr->len + 1u;
    next = qr->cap ? qr->cap * 2u : 8u;
    if (next <= qr->cap || next < need)
        next = need;
    if (next > limit)
        next = limit;
    if (next < need)
        query_results_fatal_capacity(qr->len, limit,
                                     sizeof(QueryResult) * (size_t)need);
    if (qr->items == qr->inline_items) {
        QueryResult *heap = cetta_malloc(sizeof(QueryResult) * (size_t)next);
        if (qr->len > 0)
            memcpy(heap, qr->items, sizeof(QueryResult) * (size_t)qr->len);
        qr->items = heap;
    } else {
        qr->items = cetta_realloc(qr->items, sizeof(QueryResult) * (size_t)next);
    }
    qr->cap = next;
    return true;
}

bool query_results_push(QueryResults *qr, Atom *result, Bindings *b) {
    if (!query_results_ensure_one(qr))
        return false;
    qr->items[qr->len].result = result;
    if (!bindings_clone(&qr->items[qr->len].bindings, b))
        return false;
    qr->len++;
    return true;
}

bool query_results_push_move(QueryResults *qr, Atom *result, Bindings *b) {
    if (!query_results_ensure_one(qr))
        return false;
    qr->items[qr->len].result = result;
    bindings_move(&qr->items[qr->len].bindings, b);
    qr->len++;
    return true;
}

CettaCount query_results_visit(const QueryResults *qr, QueryResultVisitor visitor,
                               void *ctx) {
    CettaCount visited = 0;
    if (!qr || !visitor)
        return 0;
    for (CettaIndex i = 0; i < qr->len; i++) {
        visited++;
        if (!visitor(qr->items[i].result, &qr->items[i].bindings, ctx))
            break;
    }
    return visited;
}

void query_results_free(QueryResults *qr) {
    for (CettaIndex i = 0; i < qr->len; i++)
        bindings_free(&qr->items[i].bindings);
    if (qr->items != qr->inline_items)
        free(qr->items);
    memset(qr->inline_items, 0, sizeof(qr->inline_items));
    qr->items = qr->inline_items;
    qr->len = 0;
    qr->cap = 1;
}

typedef struct {
    VarId var_id;
    SymbolId spelling;
    Atom *name_key;
} QueryVisibleVar;

#define QUERY_VISIBLE_INLINE_CAP 8

typedef struct {
    QueryVisibleVar inline_items[QUERY_VISIBLE_INLINE_CAP];
    QueryVisibleVar *items;
    CettaExprLen len;
    CettaExprLen cap;
} QueryVisibleVarSet;

static void query_visible_var_set_init(QueryVisibleVarSet *set) {
    set->items = set->inline_items;
    set->len = 0;
    set->cap = QUERY_VISIBLE_INLINE_CAP;
}

static void query_visible_var_set_free(QueryVisibleVarSet *set) {
    if (set->items != set->inline_items)
        free(set->items);
    set->items = set->inline_items;
    set->len = 0;
    set->cap = QUERY_VISIBLE_INLINE_CAP;
}

static bool query_visible_var_set_reserve(QueryVisibleVarSet *set,
                                          CettaExprLen needed) {
    if (needed <= set->cap)
        return true;
    CettaExprLen next_cap = set->cap ? set->cap : QUERY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(QueryVisibleVar)))
        return false;
    QueryVisibleVar *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(QueryVisibleVar) * (size_t)next_cap)
        : cetta_realloc(set->items, sizeof(QueryVisibleVar) * (size_t)next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(QueryVisibleVar) * (size_t)set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool query_visible_var_set_add(QueryVisibleVarSet *set, Atom *var) {
    if (!set || !var || var->kind != ATOM_VAR)
        return false;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_VISIBLE_DEDUP_SCAN,
                            set->len);
    for (CettaExprIndex i = 0; i < set->len; i++) {
        if (var_id_eq(set->items[i].var_id, var->var_id))
            return true;
    }
    if (!query_visible_var_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].var_id = var->var_id;
    set->items[set->len].spelling = var->sym_id;
    set->items[set->len].name_key = var->name_key;
    set->len++;
    return true;
}

static bool query_visible_var_set_contains(const QueryVisibleVarSet *set,
                                           VarId var_id) {
    for (CettaExprIndex i = 0; set && i < set->len; i++) {
        if (var_id_eq(set->items[i].var_id, var_id))
            return true;
    }
    return false;
}

static bool collect_query_visible_vars_rec(Atom *atom, QueryVisibleVarSet *set) {
    if (!atom || !atom_has_vars(atom))
        return true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_VISIBLE_NODE_VISIT);
    if (atom->kind == ATOM_VAR)
        return query_visible_var_set_add(set, atom);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (!collect_query_visible_vars_rec(atom->expr.elems[i], set))
            return false;
    }
    return true;
}

static bool atom_refs_only_query_visible_vars(Atom *atom,
                                              const QueryVisibleVarSet *visible) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR)
        return query_visible_var_set_contains(visible, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (!atom_refs_only_query_visible_vars(atom->expr.elems[i], visible))
            return false;
    }
    return true;
}

static const QueryVisibleVar *query_visible_alias_for_var(
    Atom *atom,
    const QueryVisibleVarSet *visible,
    const Bindings *full) {
    const QueryVisibleVar *found = NULL;
    if (!atom || atom->kind != ATOM_VAR || !visible || !full)
        return NULL;
    for (CettaExprIndex i = 0; i < visible->len; i++) {
        BindingValue exact = bindings_lookup_value_id((Bindings *)full, visible->items[i].var_id);
        if (!exact.skeleton || exact.skeleton->kind != ATOM_VAR)
            continue;
        if (!var_id_eq(binding_value_variable_id(exact), atom->var_id))
            continue;
        if (var_id_eq(atom->var_id, visible->items[i].var_id))
            return NULL;
        if (found && !var_id_eq(found->var_id, visible->items[i].var_id))
            return NULL;
        found = &visible->items[i];
    }
    return found;
}

static Atom *rewrite_query_visible_aliases(Arena *a, Atom *atom,
                                           const QueryVisibleVarSet *visible,
                                           const Bindings *full) {
    if (!atom || !visible || visible->len == 0 || !atom_has_vars(atom))
        return atom;
    if (atom->kind == ATOM_VAR) {
        const QueryVisibleVar *alias =
            query_visible_alias_for_var(atom, visible, full);
        return alias
            ? atom_var_with_presentation(
                  a, alias->spelling, alias->name_key, alias->var_id)
            : atom;
    }
    if (atom->kind != ATOM_EXPR)
        return atom;

    Atom **rewritten = NULL;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        Atom *child = atom->expr.elems[i];
        Atom *next = rewrite_query_visible_aliases(a, child, visible, full);
        if (!rewritten && next != child) {
            rewritten = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            for (CettaExprIndex j = 0; j < i; j++)
                rewritten[j] = atom->expr.elems[j];
        }
        if (rewritten)
            rewritten[i] = next;
    }
    return rewritten ? atom_expr(a, rewritten, atom->expr.len) : atom;
}

static Atom *bindings_resolve_query_visible_var(Arena *a, const Bindings *full,
                                                const QueryVisibleVar *wanted) {
    BindingValue exact = bindings_lookup_value_id((Bindings *)full, wanted->var_id);
    if (exact.skeleton)
        return bindings_apply_value_without_id(full, a, wanted->var_id, exact);

    Atom *slot_var = atom_var_with_presentation(
        a, wanted->spelling, wanted->name_key, wanted->var_id);
    if (!slot_var)
        return NULL;
    Atom *resolved = bindings_apply_if_vars(full, a, slot_var);
    if (resolved != slot_var)
        return resolved;

    /* Do NOT fall back to same-base/same-spelling lookup. That fallback
       incorrectly captures outer-scope variables that happen to share the same
       spelling as inner-scope variables during equation evaluation. The exact
       var_id lookup above and bindings_apply_if_vars are the only correct
       resolution paths. */
    return slot_var;
}

static bool project_query_visible_bindings(Arena *a,
                                           const QueryVisibleVarSet *visible,
                                           const Bindings *full,
                                           Bindings *projected) {
    bindings_init(projected);

    if (visible->len == 0) {
        return true;
    }

    for (uint32_t i = 0; i < visible->len; i++) {
        Atom *resolved =
            bindings_resolve_query_visible_var(a, full, &visible->items[i]);
        if (!resolved) {
            bindings_free(projected);
            return false;
        }
        resolved = rewrite_query_visible_aliases(a, resolved, visible, full);
        if (resolved->kind == ATOM_VAR &&
            var_id_eq(resolved->var_id, visible->items[i].var_id)) {
            continue;
        }
        Atom *visible_var = atom_var_with_presentation(
            a, visible->items[i].spelling, visible->items[i].name_key,
            visible->items[i].var_id);
        if (!visible_var ||
            !bindings_add_var(projected, visible_var, resolved)) {
            bindings_free(projected);
            return false;
        }
    }

    for (uint32_t i = 0; i < full->eq_len; i++) {
        Atom *lhs = bindings_apply_value(full, a, full->constraints[i].lhs);
        Atom *rhs = bindings_apply_value(full, a, full->constraints[i].rhs);
        lhs = rewrite_query_visible_aliases(a, lhs, visible, full);
        rhs = rewrite_query_visible_aliases(a, rhs, visible, full);
        if (!atom_refs_only_query_visible_vars(lhs, visible) ||
            !atom_refs_only_query_visible_vars(rhs, visible)) {
            continue;
        }
        if (!bindings_add_constraint(projected, lhs, rhs)) {
            bindings_free(projected);
            return false;
        }
    }

    return true;
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

/* is_equation_atom defined above in Space section */

/* ── Space Registry ─────────────────────────────────────────────────────── */

void registry_init(Registry *r) {
    if (!r) return;
    r->states = NULL;
    r->entries = r->inline_entries;
    r->len = 0;
    r->word_len = 0;
    r->cap = (uint32_t)(sizeof(r->inline_entries) /
                        sizeof(r->inline_entries[0]));
    r->name_keys = name_key_table_new(32u);
    r->index_slots = r->inline_index_slots;
    r->index_cap = (uint32_t)(sizeof(r->inline_index_slots) /
                              sizeof(r->inline_index_slots[0]));
    memset(r->inline_index_slots, 0, sizeof(r->inline_index_slots));
}

static void registry_states_free(struct RegistryStates *states);

void registry_free(Registry *r) {
    if (!r) return;
    registry_states_free(r->states);
    r->states = NULL;
    if (r->entries && r->entries != r->inline_entries)
        free(r->entries);
    if (r->index_slots && r->index_slots != r->inline_index_slots)
        free(r->index_slots);
    name_key_table_delete(r->name_keys);
    r->entries = r->inline_entries;
    r->len = 0;
    r->word_len = 0;
    r->cap = (uint32_t)(sizeof(r->inline_entries) /
                        sizeof(r->inline_entries[0]));
    r->name_keys = NULL;
    r->index_slots = r->inline_index_slots;
    r->index_cap = (uint32_t)(sizeof(r->inline_index_slots) /
                              sizeof(r->inline_index_slots[0]));
    memset(r->inline_index_slots, 0, sizeof(r->inline_index_slots));
}

static uint32_t registry_index_hash(SymbolId key, NameId name_id) {
    uint32_t x = key != SYMBOL_ID_NONE
        ? ((uint32_t)key ^ UINT32_C(0x243f6a88))
        : ((uint32_t)name_id ^ UINT32_C(0x9e3779b9));
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    x ^= x >> 16;
    return x;
}

static bool registry_entry_matches(const RegistryEntry *entry,
                                   SymbolId key, NameId name_id) {
    return entry && entry->key == key && entry->name_id == name_id;
}

static uint32_t registry_index_find(const Registry *r, SymbolId key,
                                    NameId name_id) {
    if (!r || !r->index_slots || r->index_cap == 0u)
        return UINT32_MAX;
    uint32_t mask = r->index_cap - 1u;
    uint32_t slot = registry_index_hash(key, name_id) & mask;
    for (uint32_t probes = 0u; probes < r->index_cap; probes++) {
        uint32_t encoded = r->index_slots[slot];
        if (encoded == 0u) return UINT32_MAX;
        uint32_t index = encoded - 1u;
        if (index < r->len &&
            registry_entry_matches(&r->entries[index], key, name_id))
            return index;
        slot = (slot + 1u) & mask;
    }
    return UINT32_MAX;
}

static void registry_index_insert(Registry *r, uint32_t entry_index) {
    RegistryEntry *entry = &r->entries[entry_index];
    uint32_t mask = r->index_cap - 1u;
    uint32_t slot = registry_index_hash(entry->key, entry->name_id) & mask;
    while (r->index_slots[slot] != 0u)
        slot = (slot + 1u) & mask;
    r->index_slots[slot] = entry_index + 1u;
}

static bool registry_index_ensure_capacity(Registry *r,
                                           uint32_t needed_entries) {
    if (!r) return false;
    uint32_t next_cap = r->index_cap ? r->index_cap : 32u;
    while ((uint64_t)needed_entries * 10u > (uint64_t)next_cap * 7u) {
        if (next_cap > UINT32_MAX / 2u) return false;
        next_cap *= 2u;
    }
    if (r->index_slots && r->index_cap == next_cap) return true;
    uint32_t *next = cetta_malloc(sizeof(*next) * (size_t)next_cap);
    memset(next, 0, sizeof(*next) * (size_t)next_cap);
    if (r->index_slots && r->index_slots != r->inline_index_slots)
        free(r->index_slots);
    r->index_slots = next;
    r->index_cap = next_cap;
    for (uint32_t i = 0u; i < r->len; i++)
        registry_index_insert(r, i);
    return true;
}

static bool registry_ensure_capacity(Registry *r, uint32_t min_needed) {
    uint32_t next_cap;
    RegistryEntry *next;
    if (!r)
        return false;
    if (!r->entries) {
        r->entries = r->inline_entries;
        r->cap = (uint32_t)(sizeof(r->inline_entries) /
                            sizeof(r->inline_entries[0]));
    }
    if (r->cap >= min_needed)
        return true;
    next_cap = r->cap ? r->cap : 16u;
    while (next_cap < min_needed) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if (next_cap != 0u &&
        SIZE_MAX / (size_t)next_cap < sizeof(RegistryEntry))
        return false;
    if (r->entries == r->inline_entries) {
        next = cetta_malloc(sizeof(RegistryEntry) * (size_t)next_cap);
        if (r->len > 0)
            memcpy(next, r->entries, sizeof(RegistryEntry) * (size_t)r->len);
    } else {
        next = cetta_realloc(r->entries,
                             sizeof(RegistryEntry) * (size_t)next_cap);
    }
    r->entries = next;
    r->cap = next_cap;
    return true;
}

void registry_bind_id(Registry *r, SymbolId key, Atom *value) {
    if (!r || key == SYMBOL_ID_NONE) return;
    uint32_t existing = registry_index_find(r, key, NAME_ID_NONE);
    if (existing != UINT32_MAX) {
        r->entries[existing].value = value;
        return;
    }
    if (!registry_ensure_capacity(r, r->len + 1u) ||
        !registry_index_ensure_capacity(r, r->len + 1u))
        return;
    r->entries[r->len].key = key;
    r->entries[r->len].name_id = NAME_ID_NONE;
    r->entries[r->len].value = value;
    registry_index_insert(r, r->len);
    r->len++;
    if (symbol_bytes(g_symbols, key)[0] != '&')
        r->word_len++;
}

Atom *registry_lookup_id(Registry *r, SymbolId key) {
    if (!r || key == SYMBOL_ID_NONE) return NULL;
    uint32_t index = registry_index_find(r, key, NAME_ID_NONE);
    return index == UINT32_MAX ? NULL : r->entries[index].value;
}

/* The states' names and current values, with the arena holding each value.
   A replaced value's arena is emptied and kept for the next change, so a
   change takes no new arena: an arena's identity is a finite resource. */
struct RegistryStates {
    Registry names;
    Arena **storage;
    uint32_t storage_cap;
    Arena *spare;
};

enum { REGISTRY_STATE_BLOCK_CAPACITY = 256u };

static Arena *registry_state_arena_take(struct RegistryStates *states) {
    Arena *arena = states->spare;
    states->spare = NULL;
    if (arena)
        return arena;
    arena = cetta_malloc(sizeof(*arena));
    arena_init_detached(arena);
    arena_set_block_capacity(arena, REGISTRY_STATE_BLOCK_CAPACITY);
    return arena;
}

static void registry_state_arena_drop(Arena *arena) {
    if (!arena)
        return;
    arena_free(arena);
    free(arena);
}

/* Release what `arena` holds, keeping one block, as the next change's. */
static void registry_state_arena_give_back(struct RegistryStates *states,
                                           Arena *arena) {
    if (!arena)
        return;
    if (states->spare) {
        registry_state_arena_drop(arena);
        return;
    }
    arena_reset(arena, (ArenaMark){0});
    arena_release_spare(arena, REGISTRY_STATE_BLOCK_CAPACITY);
    states->spare = arena;
}

static void registry_states_free(struct RegistryStates *states) {
    if (!states)
        return;
    for (uint32_t index = 0u; index < states->names.len; index++)
        registry_state_arena_drop(states->storage[index]);
    registry_state_arena_drop(states->spare);
    free(states->storage);
    registry_free(&states->names);
    free(states);
}

Atom *registry_state_lookup(const Registry *r, SymbolId key) {
    if (!r || !r->states || key == SYMBOL_ID_NONE)
        return NULL;
    uint32_t index =
        registry_index_find(&r->states->names, key, NAME_ID_NONE);
    return index == UINT32_MAX ? NULL
                               : r->states->names.entries[index].value;
}

bool registry_state_set(Registry *r, SymbolId key, Atom *value) {
    if (!r || key == SYMBOL_ID_NONE || !value)
        return false;
    struct RegistryStates *states = r->states;
    if (!states) {
        states = cetta_malloc(sizeof(*states));
        memset(states, 0, sizeof(*states));
        registry_init(&states->names);
        r->states = states;
    }
    Arena *arena = registry_state_arena_take(states);
    Atom *stored = atom_deep_copy(arena, value);
    if (!stored) {
        registry_state_arena_give_back(states, arena);
        return false;
    }
    uint32_t index = registry_index_find(&states->names, key, NAME_ID_NONE);
    if (index != UINT32_MAX) {
        Arena *replaced = states->storage[index];
        states->names.entries[index].value = stored;
        states->storage[index] = arena;
        registry_state_arena_give_back(states, replaced);
        return true;
    }
    if (states->names.len >= states->storage_cap) {
        uint32_t cap = states->storage_cap ? states->storage_cap * 2u : 8u;
        states->storage =
            cetta_realloc(states->storage, sizeof(*states->storage) * cap);
        states->storage_cap = cap;
    }
    registry_bind_id(&states->names, key, stored);
    index = registry_index_find(&states->names, key, NAME_ID_NONE);
    if (index == UINT32_MAX || index >= states->storage_cap) {
        registry_state_arena_give_back(states, arena);
        return false;
    }
    states->storage[index] = arena;
    return true;
}

uint32_t registry_state_count(const Registry *r) {
    return r && r->states ? r->states->names.len : 0u;
}

bool registry_state_entry(const Registry *r, uint32_t index,
                          SymbolId *key_out, Atom **value_out) {
    if (!r || !r->states || index >= r->states->names.len)
        return false;
    *key_out = r->states->names.entries[index].key;
    *value_out = r->states->names.entries[index].value;
    return true;
}

void registry_bind(Registry *r, const char *name, Atom *value) {
    registry_bind_id(r, symbol_intern_cstr(g_symbols, name), value);
}

Atom *registry_lookup(Registry *r, const char *name) {
    return registry_lookup_id(r, symbol_intern_cstr(g_symbols, name));
}

bool registry_bind_name(Registry *r, Atom *name_key, Atom *value) {
    if (!r || !r->name_keys || !name_key) return false;
    NameId id = name_key_intern(r->name_keys, name_key);
    if (id == NAME_ID_NONE) return false;
    uint32_t existing = registry_index_find(r, SYMBOL_ID_NONE, id);
    if (existing != UINT32_MAX) {
        r->entries[existing].value = value;
        return true;
    }
    if (!registry_ensure_capacity(r, r->len + 1u) ||
        !registry_index_ensure_capacity(r, r->len + 1u))
        return false;
    r->entries[r->len].key = SYMBOL_ID_NONE;
    r->entries[r->len].name_id = id;
    r->entries[r->len].value = value;
    registry_index_insert(r, r->len);
    r->len++;
    return true;
}

Atom *registry_lookup_name(Registry *r, Atom *name_key) {
    if (!r || !r->name_keys || !name_key) return NULL;
    NameId id = name_key_find(r->name_keys, name_key);
    if (id == NAME_ID_NONE) return NULL;
    uint32_t index = registry_index_find(r, SYMBOL_ID_NONE, id);
    return index == UINT32_MAX ? NULL : r->entries[index].value;
}

const Atom *registry_entry_name_key(const Registry *r, uint32_t index) {
    if (!r || index >= r->len || !r->name_keys ||
        r->entries[index].key != SYMBOL_ID_NONE ||
        r->entries[index].name_id == NAME_ID_NONE) {
        return NULL;
    }
    return name_key_lookup(r->name_keys, r->entries[index].name_id);
}

bool registry_ref_name_key(Atom *ref, Atom **name_key_out) {
    if (name_key_out) *name_key_out = NULL;
    if (!ref || ref->kind != ATOM_EXPR || ref->expr.len != 2u ||
        !atom_is_symbol(ref->expr.elems[0], "resolve-name")) {
        return false;
    }
    Atom *quoted = ref->expr.elems[1];
    if (!quoted || quoted->kind != ATOM_EXPR || quoted->expr.len != 2u ||
        !atom_is_symbol_id(quoted->expr.elems[0], g_builtin_syms.quote)) {
        return false;
    }
    if (name_key_out) *name_key_out = quoted->expr.elems[1];
    return true;
}

Atom *registry_lookup_ref(Registry *r, Atom *ref) {
    if (!r || !ref) return NULL;
    if (ref->kind == ATOM_SYMBOL)
        return registry_lookup_id(r, ref->sym_id);
    Atom *name_key = NULL;
    return registry_ref_name_key(ref, &name_key)
        ? registry_lookup_name(r, name_key)
        : NULL;
}

Space *resolve_space(Registry *r, Atom *ref) {
    /* Grounded space atom → direct pointer */
    if (ref->kind == ATOM_GROUNDED && ref->ground.gkind == GV_SPACE)
        return (Space *)ref->ground.ptr;
    /* Symbol like &self → registry lookup */
    if (ref->kind == ATOM_SYMBOL || ref->kind == ATOM_EXPR) {
        Atom *val = registry_lookup_ref(r, ref);
        if (val && val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE)
            return (Space *)val->ground.ptr;
    }
    return NULL;
}

bool space_remove(Space *s, Atom *atom) {
    space_settle_marks(s);
    s = space_mutation_target(s);
    if (!s)
        return false;
    if (space_dense_remove_ground(s, atom) != 0u)
        return true;
    if (space_has_overlay_base(s)) {
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaIndex alpha_index = 0;
        CettaCount alpha_count = 0;
        bool alpha_in_base = false;
        for (CettaCount i = 0; i < base_visible; i++) {
            CettaIndex raw = 0;
            Atom *candidate = NULL;
            if (!space_overlay_visible_base_raw_index(s, i, &raw) ||
                raw < space_imported_length(s->overlay_base))
                continue;
            candidate = space_get_at64(s->overlay_base, raw);
            if (candidate && atom_eq(candidate, atom)) {
                SpaceMutationEquationProjection equation_projection =
                    space_atom_equation_projection(
                        s->overlay_base,
                        space_get_atom_id_at64(s->overlay_base, raw),
                        candidate);
                if (!space_overlay_mark_base_removed(s, raw))
                    return false;
                space_publish_mutation(
                    s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
                return true;
            }
        }
        for (CettaIndex i = 0; i < space_local_length64(s); i++) {
            Atom *candidate = space_local_get_at64(s, i);
            if (candidate && atom_eq(candidate, atom))
                return space_overlay_remove_local_raw_index(s, i);
        }
        for (CettaCount i = 0; i < base_visible; i++) {
            CettaIndex raw = 0;
            Atom *candidate = NULL;
            if (!space_overlay_visible_base_raw_index(s, i, &raw) ||
                raw < space_imported_length(s->overlay_base))
                continue;
            candidate = space_get_at64(s->overlay_base, raw);
            if (candidate && atom_alpha_eq(candidate, atom)) {
                alpha_index = raw;
                alpha_in_base = true;
                alpha_count++;
            }
        }
        for (CettaIndex i = 0; i < space_local_length64(s); i++) {
            Atom *candidate = space_local_get_at64(s, i);
            if (candidate && atom_alpha_eq(candidate, atom)) {
                alpha_index = i;
                alpha_in_base = false;
                alpha_count++;
            }
        }
        if (alpha_count != 1u)
            return false;
        if (alpha_in_base) {
            AtomId removed_id = space_get_atom_id_at64(
                s->overlay_base, alpha_index);
            Atom *removed_atom = space_get_at64(
                s->overlay_base, alpha_index);
            SpaceMutationEquationProjection equation_projection =
                space_atom_equation_projection(
                    s->overlay_base, removed_id, removed_atom);
            if (!space_overlay_mark_base_removed(s, alpha_index))
                return false;
            space_publish_mutation(
                s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
            return true;
        }
        return space_overlay_remove_local_raw_index(s, alpha_index);
    }
    if (s->native.universe && atom) {
        AtomId atom_id = term_universe_lookup_atom_id(s->native.universe, atom);
        if (atom_id != CETTA_ATOM_ID_NONE &&
            !space_atom_id_imported_only(s, atom_id) &&
            space_remove_via_backend_primary(s, atom_id)) {
            return true;
        }
    }
    if (space_imported_length(s) == 0u &&
        space_remove_atom_via_backend_primary(s, atom))
        return true;
    if (!atom)
        return false;
    /* A live PathMap bridge is the primary store and must not be bypassed by
       mutating its native projection.  Once authored order pins the logical
       sequence, or when the bridge has explicitly fallen back as unavailable,
       the native shadow is authoritative; preserve native
       exact-then-unique-alpha removal semantics there. */
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->match_backend.pathmap.bridge.bridge_unavailable &&
        !s->match_backend.pathmap.bridge.preserve_logical_order)
        return false;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    bool found = false;
    CettaIndex remove_idx = 0;
    CettaIndex own_begin = s->imported_len;
    for (CettaIndex i = own_begin; i < s->native.len; i++) {
        Atom *candidate = space_get_at64(s, i);
        if (!candidate)
            continue;
        if (atom_eq(candidate, atom)) {
            remove_idx = i;
            found = true;
            break;
        }
    }
    if (!found) {
        CettaIndex alpha_idx = 0;
        CettaCount alpha_count = 0;
        for (CettaIndex i = own_begin; i < s->native.len; i++) {
            Atom *candidate = space_get_at64(s, i);
            if (!candidate)
                continue;
            if (atom_alpha_eq(candidate, atom)) {
                alpha_idx = i;
                alpha_count++;
            }
        }
        if (alpha_count == 1) {
            remove_idx = alpha_idx;
            found = true;
        }
    }
    if (!found)
        return false;

    SpaceMutationEquationProjection equation_projection =
        space_local_removal_equation_projection(s, remove_idx);
    SpaceDispatchRetention retained = space_transport_dispatch_coordinates(
        s, equation_projection, NULL, remove_idx);
    space_pinned_occurrences_detach(s);

    AtomId swap_removed_id = CETTA_ATOM_ID_NONE, swap_moved_id = CETTA_ATOM_ID_NONE;
    CettaIndex swap_moved_from = 0u;
    if (space_is_ordered(s)) {
        size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        AtomId removed_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, remove_idx);
        memmove(s->native.atom_ids + ((size_t)remove_idx * width),
                s->native.atom_ids + ((size_t)(remove_idx + 1u) * width),
                (size_t)(s->native.len - remove_idx - 1u) * width);
        s->native.len--;
        space_release_row(s, removed_id);
    } else {
        AtomId removed_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, remove_idx);
        CettaIndex moved_from = s->native.len - 1u;
        AtomId tail_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, moved_from);
        s->native.len--;
        space_release_row(s, removed_id);
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits, remove_idx,
            tail_id); /* swap with last */
        swap_removed_id = removed_id;
        swap_moved_from = moved_from;
        swap_moved_id = tail_id;
    }
    space_mark_indexes_dirty(s);
    s->native.eq_idx_dirty = !retained.equations;
    s->native.ty_idx_dirty = !retained.declarations;
    uint64_t followed_prefix = s->prefix_epoch;
    if (swap_removed_id != CETTA_ATOM_ID_NONE)
        space_match_backend_note_swap_remove(s, remove_idx, swap_removed_id,
                                             swap_moved_from, swap_moved_id);
    else
        space_match_backend_note_remove(s);
    space_publish_mutation(
        s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    if (swap_removed_id != CETTA_ATOM_ID_NONE)
        space_path_indexes_adopt_prefix_epoch(s, followed_prefix);
    return true;
}

bool space_contains_atom_id(const Space *s, AtomId atom_id) {
    CettaCount logical_len = 0;
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (space_dense_holds(s, atom_id))
        return true;
    logical_len = space_length64(s) - space_dense_facts(s);
    for (CettaIndex i = 0; i < logical_len; i++) {
        if (space_get_atom_id_at64(s, i) == atom_id)
            return true;
    }
    return false;
}

bool space_holds_atom_id_exact(Space *s, AtomId atom_id) {
    CETTA_SCOPED_SHARED_TRANSITION(holds);
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (space_dense_holds(s, atom_id))
        return true;
    if (!space_tracks_atom_ids(s) || space_has_dependencies(s) ||
        space_has_overlay_base(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return space_contains_atom_id(s, atom_id);
    /* A set answers from its presence index, kept current by the rows
     * added since; another space from its exact index, rebuilt here when
     * stale and a rebuild is allowed, as exact_index_head_exact does. */
    if (s->kind == SPACE_KIND_SET)
        return space_set_holds(s, atom_id, NULL);
    if (s->native.exact_idx_dirty && space_keeps_exact_index(s) &&
        s->native.secondary_index_deferral_depth == 0u &&
        s->native.marked == 0u)
        exact_index_rebuild(s);
    return s->native.exact_idx_dirty
        ? space_contains_atom_id(s, atom_id)
        : exact_index_holds_id(&s->native.exact_idx, atom_id);
}

static bool space_remove_atom_id_rows(Space *s, AtomId atom_id);

bool space_remove_atom_id(Space *s, AtomId atom_id) {
    Space *target = space_mutation_target(s);
    if (target && atom_id != CETTA_ATOM_ID_NONE &&
        space_dense_route_remove(target, atom_id))
        return true;
    return space_remove_atom_id_rows(s, atom_id);
}

static bool space_remove_atom_id_rows(Space *s, AtomId atom_id) {
    space_settle_marks(s);
    s = space_mutation_target(s);
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount base_visible = space_overlay_visible_base_count(s);
        for (CettaCount i = 0; i < base_visible; i++) {
            CettaIndex raw = 0;
            if (!space_overlay_visible_base_raw_index(s, i, &raw) ||
                raw < space_imported_length(s->overlay_base))
                continue;
            if (space_get_atom_id_at64(s->overlay_base, raw) != atom_id)
                continue;
            SpaceMutationEquationProjection equation_projection =
                space_atom_equation_projection(
                    s->overlay_base, atom_id,
                    space_get_at64(s->overlay_base, raw));
            if (!space_overlay_mark_base_removed(s, raw))
                return false;
            space_publish_mutation(
                s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
            return true;
        }
        for (CettaIndex i = 0; i < space_local_length64(s); i++) {
            if (space_local_get_atom_id_at64(s, i) == atom_id)
                return space_overlay_remove_local_raw_index(s, i);
        }
        return false;
    }
    if (space_atom_id_imported_only(s, atom_id))
        return false;
    if (s->match_backend.native.match_trie_pins == 0u &&
        space_remove_via_backend_primary(s, atom_id))
        return true;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    /* An unordered space finds the first local row through its exact index,
     * which a removal keeps current; a stale one is rebuilt once here. */
    CettaIndex start = s->imported_len;
    bool before = false, found = false;
    CettaIndex first = 0u;
    if (!space_is_ordered(s))
        ensure_exact_index(s);
    if (!space_is_ordered(s) &&
        space_exact_rows(s, atom_id, s->imported_len, &before, &found, &first)) {
        if (!found)
            return false;
        start = first;
    }
    for (CettaIndex i = start; i < s->native.len; i++) {
        if (space_get_atom_id_at64(s, i) != atom_id)
            continue;
        SpaceMutationEquationProjection equation_projection =
            space_local_removal_equation_projection(s, i);
        SpaceDispatchRetention retained = space_transport_dispatch_coordinates(
            s, equation_projection, NULL, i);
        space_pinned_occurrences_detach(s);
        if (space_is_ordered(s)) {
            size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
            memmove(s->native.atom_ids + ((size_t)i * width),
                    s->native.atom_ids + ((size_t)(i + 1u) * width),
                    (size_t)(s->native.len - i - 1u) * width);
            s->native.len--;
            space_release_row(s, atom_id);
            space_mark_indexes_dirty(s);
            s->native.eq_idx_dirty = !retained.equations;
            s->native.ty_idx_dirty = !retained.declarations;
            space_match_backend_note_remove(s);
        } else {
            CettaIndex moved_from = s->native.len - 1u;
            AtomId tail_id = space_atom_id_storage_load_at(
                s->native.atom_ids, s->native.atom_id_width_bits, moved_from);
            s->native.len--;
            space_release_row(s, atom_id);
            (void)space_atom_id_storage_store_at(
                s->native.atom_ids, s->native.atom_id_width_bits, i,
                tail_id);
            bool exact_current = !s->native.exact_idx_dirty;
            space_mark_indexes_dirty(s);
            s->native.eq_idx_dirty = !retained.equations;
            s->native.ty_idx_dirty = !retained.declarations;
            if (exact_current)
                s->native.exact_idx_dirty = !exact_index_note_swap_remove(
                    s, i, atom_id, moved_from, tail_id);
            uint64_t followed_prefix = s->prefix_epoch;
            space_match_backend_note_swap_remove(s, i, atom_id, moved_from,
                                                 tail_id);
            space_publish_mutation(
                s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
            space_path_indexes_adopt_prefix_epoch(s, followed_prefix);
            return true;
        }
        space_publish_mutation(
            s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
        return true;
    }
    return false;
}

bool space_remove_atom_ids_batch(Space *s, const AtomId *atom_ids,
                                 CettaCount atom_count,
                                 CettaCount *out_removed) {
    space_prepare_prefix_rewrite(s);
    uint64_t removed = 0;
    SpaceBackendBatchResult batch_result;

    s = space_mutation_target(s);

    if (out_removed)
        *out_removed = 0;
    if (!s || (!atom_ids && atom_count != 0))
        return false;
    if (atom_count == 0)
        return true;

    batch_result = space_has_overlay_base(s) || space_imported_length(s) != 0u
        ? SPACE_BACKEND_BATCH_UNSUPPORTED
        : space_match_backend_remove_atom_ids_batch_direct(
              s, atom_ids, atom_count, &removed);
    if (batch_result == SPACE_BACKEND_BATCH_ERROR)
        return false;
    if (batch_result == SPACE_BACKEND_BATCH_APPLIED) {
        if (removed != 0) {
            for (CettaCount i = 0; i < atom_count; i++)
                space_mark_backend_primary_atom_mutation(
                    s, atom_ids[i], NULL);
            /* The direct backend reports only a count, not the exact authored
               occurrences or their transport.  Preserve soundness until that
               interface supplies a projection receipt. */
            space_publish_mutation(
                s, SPACE_MUTATION_EQUATION_PROJECTION_OPAQUE,
                SPACE_MUTATION_PREFIX_REWRITTEN);
        }
        if (out_removed)
            *out_removed = (CettaCount)removed;
        return true;
    }

    for (CettaCount i = 0; i < atom_count; i++) {
        if (space_remove_atom_id(s, atom_ids[i]))
            removed++;
    }
    if (out_removed)
        *out_removed = (CettaCount)removed;
    return true;
}

/* A stable contraction.  Settling marks contracts rows the logical view
 * already omits: it carries the indexes and publishes nothing. */
static bool space_contract_occurrence_mask(
        Space *s, const uint8_t *remove_mask, CettaCount mask_len,
        CettaCount *out_removed, bool settling) {
    if (!settling)
        space_settle_marks(s);
    if (out_removed)
        *out_removed = 0u;
    if (!s || (!remove_mask && mask_len != 0u))
        return false;

    CettaCount logical_len = space_length64(s);
    /* Facts kept in relations follow the rows.  Settling marks covers the
     * rows alone; a mask over the whole sequence gives the relations to the
     * rows first, at the positions their facts had. */
    if (space_dense_facts(s) != 0u) {
        if (!settling && mask_len == logical_len) {
            space_dense_settle(s, SYMBOL_ID_NONE);
            logical_len = space_length64(s);
        }
        logical_len -= space_dense_facts(s);
    }
    if (mask_len != logical_len)
        return false;
    if (s->module_own_view) {
        Space *owner = space_mutation_target(s);
        if (!owner)
            return false;
        CettaCount owner_len = space_length64(owner);
        if (owner_len > SIZE_MAX)
            return false;
        uint8_t *owner_mask = calloc((size_t)owner_len, 1u);
        if (!owner_mask && owner_len)
            return false;
        if (mask_len)
            memcpy(owner_mask + space_imported_length(owner),
                   remove_mask, (size_t)mask_len);
        bool ok = space_remove_occurrence_mask_stable(
            owner, owner_mask, owner_len, out_removed);
        free(owner_mask);
        return ok;
    }
    CettaIndex imported = space_imported_length(s);
    for (CettaIndex index = 0u; index < imported && index < mask_len; index++) {
        if (remove_mask[index] != 0u)
            return false;
    }

    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_MASK_CONTRACTION);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_MASK_SOURCE_ROW,
        logical_len);

    CettaCount removed = 0u;
    SpaceMutationEquationProjection equation_projection =
        SPACE_MUTATION_EQUATION_PROJECTION_DATA_ONLY;
    for (CettaIndex index = 0u; index < logical_len; index++) {
        if (remove_mask[index] == 0u)
            continue;
        removed++;
        /* Settling contracts rows marking admitted as data only, and whose
         * terms a collection may since have reclaimed: they are not read. */
        if (settling)
            continue;
        equation_projection = space_merge_equation_projection(
            equation_projection,
            space_atom_equation_projection(
                s, space_get_atom_id_at64(s, index),
                space_has_overlay_base(s) ? space_get_at64(s, index) : NULL));
    }
    if (removed == 0u)
        return true;
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_MASK_REMOVED_OCCURRENCE,
        removed);

    if (!space_has_overlay_base(s))
        space_pinned_occurrences_detach(s);

    /* A complete contraction has no surviving occurrence coordinates to
     * transport. Replace the owned storage instead of retaining its old row
     * capacity and indexes. Detached readers retain their previous rows/trie;
     * replacement keeps instance/dependency authority and advances revisions. */
    if (removed == logical_len && !space_has_overlay_base(s) && !settling) {
        Space empty;
        space_init_with_universe(&empty, s->native.universe);
        empty.kind = s->kind;
        space_set_strict(&empty, s->strict);
        bool ok = space_match_backend_try_set(&empty, s->match_backend.kind) &&
            space_match_backend_require_logical_order(&empty, NULL);
        if (ok) {
            empty.payload_owner_epoch = s->payload_owner_epoch;
            empty.payload_export_owner_epoch = s->payload_export_owner_epoch;
            space_replace_contents_classified(s, &empty, equation_projection);
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_PRIVATE_RESET);
            if (out_removed)
                *out_removed = removed;
        }
        space_free(&empty);
        return ok;
    }

    if (stable_occurrence_transport_enabled() && !space_has_overlay_base(s) &&
        logical_len <= SIZE_MAX / sizeof(CettaIndex) &&
        space_match_backend_materialize_native_storage(s, NULL) &&
        s->native.len == logical_len &&
        (logical_len == 0u ||
         (s->native.atom_ids &&
          space_atom_id_width_bytes_bits(s->native.atom_id_width_bits) != 0u))) {
        if (space_is_queue(s))
            space_linearize(s);

        CettaIndex *source_to_target = cetta_malloc(
            sizeof(*source_to_target) * (size_t)logical_len);
        CettaIndex target = 0u;
        for (CettaIndex source_index = 0u;
             source_index < logical_len; source_index++) {
            if (remove_mask[source_index] != 0u) {
                source_to_target[source_index] =
                    SPACE_OCCURRENCE_COORDINATE_REMOVED;
            } else {
                source_to_target[source_index] = target++;
            }
        }
        SpaceStableOccurrenceTransport transport = {
            .source_to_target = source_to_target,
            .source_len = logical_len,
            .target_len = target,
        };
        SpaceBackendBatchResult transport_result =
            space_match_backend_transport_stable_occurrence_coordinates(
                s, &transport);
        if (transport_result == SPACE_BACKEND_BATCH_ERROR) {
            free(source_to_target);
            return false;
        }
        if (transport_result == SPACE_BACKEND_BATCH_APPLIED) {
            SpaceDispatchRetention retained = space_transport_dispatch_coordinates(
                s, equation_projection, &transport, SPACE_OCCURRENCE_COORDINATE_REMOVED);
            /* A current exact index holds the live rows: marked rows left it
             * when marked, and the rows this mask removes leave it now, while
             * their ids are still stored. */
            bool exact_current = !s->native.exact_idx_dirty;
            for (CettaIndex source_index = 0u;
                 exact_current && !settling && source_index < logical_len;
                 source_index++) {
                if (remove_mask[source_index] != 0u &&
                    !exact_index_forget_row(
                        s, space_atom_id_storage_load_at(
                               s->native.atom_ids,
                               s->native.atom_id_width_bits, source_index),
                        source_index))
                    exact_current = false;
            }
            for (CettaIndex source_index = 0u;
                 !settling && source_index < logical_len; source_index++) {
                if (remove_mask[source_index] != 0u)
                    term_universe_release(
                        s->native.universe,
                        space_atom_id_storage_load_at(
                            s->native.atom_ids, s->native.atom_id_width_bits,
                            source_index));
            }
            CettaCount moved = 0u;
            for (CettaIndex source_index = 0u;
                 source_index < logical_len; source_index++) {
                CettaIndex target_index = source_to_target[source_index];
                if (target_index == SPACE_OCCURRENCE_COORDINATE_REMOVED ||
                    target_index == source_index) {
                    continue;
                }
                AtomId atom_id = space_atom_id_storage_load_at(
                    s->native.atom_ids, s->native.atom_id_width_bits,
                    source_index);
                if (!space_atom_id_storage_store_at(
                        s->native.atom_ids, s->native.atom_id_width_bits,
                        target_index, atom_id)) {
                    fputs("CeTTa: admitted stable occurrence transport lost "
                          "its native coordinate store\n", stderr);
                    abort();
                }
                moved++;
            }
            s->native.len = target;
            s->native.start = 0u;
            if (!settling)
                space_note_released(s, removed);
            space_mark_derived_state_dirty(s);
            s->native.eq_idx_dirty = !retained.equations;
            s->native.ty_idx_dirty = !retained.declarations;
            s->native.exact_idx_dirty =
                !exact_current ||
                !exact_index_transport(&s->native.exact_idx,
                                       source_to_target, logical_len);
            if (!settling)
                space_publish_mutation(
                    s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_SPACE_STABLE_COORDINATE_TRANSPORT_RETAINED_MOVE,
                moved);
            free(source_to_target);
            if (out_removed)
                *out_removed = removed;
            return true;
        }
        free(source_to_target);
    }
    if (settling)
        return false;

    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_SPACE_STABLE_MASK_RETAINED_ROW_COPY,
        logical_len - removed);

    SpaceReadToken source = space_read_token(s);
    Space *replacement = cetta_malloc(sizeof(*replacement));
    space_init_with_universe(replacement, s->native.universe);
    replacement->kind = s->kind;
    space_set_strict(replacement, s->strict);
    bool ok = space_match_backend_try_set(
                  replacement, s->match_backend.kind) &&
              space_match_backend_require_logical_order(
                  replacement, NULL);

    for (CettaIndex index = 0u;
         ok && index < logical_len; index++) {
        if (remove_mask[index] != 0u)
            continue;
        CettaCount before = space_length64(replacement);
        AtomId atom_id = space_get_atom_id_at64(s, index);
        if (atom_id != CETTA_ATOM_ID_NONE) {
            space_add_atom_id(replacement, atom_id);
        } else {
            Atom *atom = space_get_at64(s, index);
            ok = atom && space_admit_atom(replacement, NULL, atom);
        }
        ok = ok && space_length64(replacement) == before + 1u;
    }

    CettaIndex retained_index = 0u;
    for (CettaIndex source_index = 0u;
         ok && source_index < logical_len; source_index++) {
        if (remove_mask[source_index] != 0u)
            continue;
        AtomId source_id = space_get_atom_id_at64(s, source_index);
        AtomId retained_id = space_get_atom_id_at64(
            replacement, retained_index);
        if (source_id != CETTA_ATOM_ID_NONE &&
            retained_id != CETTA_ATOM_ID_NONE) {
            ok = source_id == retained_id;
        } else {
            Atom *source_atom = space_get_at64(s, source_index);
            Atom *retained_atom = space_get_at64(
                replacement, retained_index);
            ok = source_atom && retained_atom &&
                 atom_eq(source_atom, retained_atom);
        }
        retained_index++;
    }
    ok = ok &&
         retained_index == logical_len - removed &&
         space_length64(replacement) == retained_index &&
         space_read_token_matches_live_space(source, s);

    if (ok) {
        replacement->payload_owner_epoch = s->payload_owner_epoch;
        replacement->payload_export_owner_epoch =
            s->payload_export_owner_epoch;
        space_replace_contents_classified(
            s, replacement, equation_projection);
    }
    space_free(replacement);
    free(replacement);

    if (ok && out_removed)
        *out_removed = removed;
    return ok;
}

AtomId space_get_atom_id_at(const Space *s, uint32_t idx) {
    return space_get_atom_id_at64(s, idx);
}

bool space_remove_occurrence_mask_stable(
        Space *s, const uint8_t *remove_mask, CettaCount mask_len,
        CettaCount *out_removed) {
    return space_contract_occurrence_mask(
        s, remove_mask, mask_len, out_removed, false);
}

CettaCount space_length64(const Space *s) {
    if (!s)
        return 0;
    if (!space_has_overlay_base(s))
        return (CettaCount)space_match_backend_logical_len64(s);
    return space_overlay_visible_base_count(s) + space_local_length64(s);
}

AtomId space_get_atom_id_at64(const Space *s, CettaIndex idx) {
    if (!s)
        return CETTA_ATOM_ID_NONE;
    if (!space_has_overlay_base(s))
        return space_match_backend_get_atom_id_at64(s, idx);
    CettaCount base_visible = space_overlay_visible_base_count(s);
    if (idx < base_visible) {
        CettaIndex raw = 0;
        if (!space_overlay_visible_base_raw_index(s, idx, &raw))
            return CETTA_ATOM_ID_NONE;
        return space_get_atom_id_at64(s->overlay_base, raw);
    }
    return space_local_get_atom_id_at64(s, idx - base_visible);
}

Atom *space_get_at64(const Space *s, CettaIndex idx) {
    if (!s)
        return NULL;
    if (!space_has_overlay_base(s))
        return space_match_backend_get_at64(s, idx);
    CettaCount base_visible = space_overlay_visible_base_count(s);
    if (idx < base_visible) {
        CettaIndex raw = 0;
        if (!space_overlay_visible_base_raw_index(s, idx, &raw))
            return NULL;
        return space_get_at64(s->overlay_base, raw);
    }
    return space_local_get_at64(s, idx - base_visible);
}

Atom *space_get_at(const Space *s, uint32_t idx) {
    return space_get_at64(s, idx);
}

Atom *space_view_at64(const Space *s, CettaIndex idx, Arena *dst) {
    if (s && dst && s->native.universe && !space_has_overlay_base(s) &&
        space_dense_facts(s) == 0u &&
        (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
         s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)) {
        CETTA_SCOPED_SHARED_TRANSITION(read);
        AtomId id = space_get_atom_id_at64(s, idx);
        if (id != CETTA_ATOM_ID_NONE)
            return term_universe_view_atom(s->native.universe, dst, id);
    }
    return space_get_at64(s, idx);
}

Atom *space_peek(const Space *s) {
    CettaCount logical_len = space_length64(s);
    if (!s || logical_len == 0)
        return NULL;
    return space_get_at64(s, space_is_queue(s) ? 0 : (logical_len - 1));
}

bool space_pop(Space *s, Atom **out) {
    space_dense_settle(s, SYMBOL_ID_NONE);
    space_prepare_prefix_rewrite(s);
    CettaCount logical_len;
    Atom *top;

    if (!s)
        return false;
    if (space_has_overlay_base(s)) {
        if (s->module_own_view)
            return space_length64(s) != 0u &&
                   space_pop(space_mutation_target(s), out);
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaCount local_len = space_local_length64(s);
        logical_len = base_visible + local_len;
        if (logical_len == 0)
            return false;
        top = space_peek(s);
        if (!top)
            return false;
        if (out)
            *out = top;
        if (local_len > 0)
            return space_overlay_remove_local_raw_index(s, local_len - 1u);
        if (base_visible > 0) {
            CettaIndex raw = 0;
            if (!space_overlay_visible_base_raw_index(s, base_visible - 1u, &raw) ||
                !space_overlay_mark_base_removed(s, raw)) {
                return false;
            }
            space_publish_mutation(
                s, space_atom_equation_projection(
                       s->overlay_base,
                       space_get_atom_id_at64(s->overlay_base, raw), top),
                SPACE_MUTATION_PREFIX_REWRITTEN);
            return true;
        }
        return false;
    }
    logical_len = space_length64(s);
    if (logical_len == 0)
        return false;
    top = space_peek(s);
    if (!top)
        return false;
    if (out)
        *out = top;
    SpaceMutationEquationProjection equation_projection =
        space_atom_equation_projection(
            s, space_get_atom_id_at64(
                   s, space_is_queue(s) ? 0u : logical_len - 1u), top);
    if (space_truncate_via_backend_primary64(s, logical_len - 1))
        return true;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s)) {
        space_release_rows(s, 0u, 1u);
        s->native.start++;
        s->native.len--;
        if (s->native.len == 0)
            s->native.start = 0;
    } else {
        space_release_rows(s, s->native.len - 1u, s->native.len);
        s->native.len--;
    }
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_publish_mutation(
        s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

bool space_truncate(Space *s, uint32_t new_len) {
    space_prepare_prefix_rewrite(s);
    uint32_t logical_len;

    if (!s || new_len < space_imported_length(s))
        return false;
    if (space_has_overlay_base(s))
        return space_truncate64(s, new_len);
    if (!space_length_u32_checked(s, &logical_len))
        return false;
    if (new_len > logical_len)
        return false;
    if (space_truncate_via_backend_primary(s, new_len))
        return true;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (new_len == s->native.len)
        return true;
    SpaceMutationEquationProjection equation_projection =
        space_logical_removed_range_equation_projection(
            s, new_len, logical_len);
    space_release_rows(s, new_len, s->native.len);
    s->native.len = new_len;
    if (s->native.len == 0)
        s->native.start = 0;
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_publish_mutation(
        s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

bool space_truncate64(Space *s, CettaCount new_len) {
    space_dense_settle(s, SYMBOL_ID_NONE);
    space_prepare_prefix_rewrite(s);
    CettaCount logical_len;

    if (!s || new_len < space_imported_length(s))
        return false;
    if (space_has_overlay_base(s)) {
        if (s->module_own_view)
            return new_len <= space_length64(s) &&
                   space_truncate64(space_mutation_target(s), new_len +
                       space_imported_length(s->overlay_base));
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaCount local_len = space_local_length64(s);
        logical_len = base_visible + local_len;
        if (new_len > logical_len)
            return false;
        if (new_len >= base_visible) {
            CettaCount want_local = new_len - base_visible;
            if (want_local == local_len)
                return true;
            if (!space_match_backend_materialize_native_storage(s, NULL))
                return false;
            SpaceMutationEquationProjection equation_projection =
                space_logical_removed_range_equation_projection(
                    s, new_len, logical_len);
            space_release_rows(s, want_local, s->native.len);
            s->native.len = want_local;
            if (s->native.len == 0)
                s->native.start = 0;
            space_mark_indexes_dirty(s);
            space_match_backend_note_remove(s);
            space_publish_mutation(
                s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
            return true;
        }
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return false;
        SpaceMutationEquationProjection equation_projection =
            space_logical_removed_range_equation_projection(
                s, new_len, logical_len);
        space_release_rows(s, 0u, s->native.len);
        s->native.len = 0;
        s->native.start = 0;
        if (new_len == 0) {
            s->overlay_base_visible_len = 0;
            s->overlay_removed_base_len = 0;
        } else {
            CettaIndex raw = 0;
            if (!space_overlay_visible_base_raw_index(s, new_len - 1u, &raw))
                return false;
            s->overlay_base_visible_len = raw + 1u;
            space_overlay_clear_removed_at_or_after(s, s->overlay_base_visible_len);
        }
        space_mark_indexes_dirty(s);
        space_match_backend_note_remove(s);
        space_publish_mutation(
            s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
        return true;
    }
    if (space_truncate_via_backend_primary64(s, new_len))
        return true;
    logical_len = space_length64(s);
    if (new_len > logical_len)
        return false;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (new_len == s->native.len)
        return true;
    SpaceMutationEquationProjection equation_projection =
        space_logical_removed_range_equation_projection(
            s, new_len, logical_len);
    space_release_rows(s, new_len, s->native.len);
    s->native.len = new_len;
    if (s->native.len == 0)
        s->native.start = 0;
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_publish_mutation(
        s, equation_projection, SPACE_MUTATION_PREFIX_REWRITTEN);
    return true;
}

bool space_length_u32_checked(const Space *s, uint32_t *out_len) {
    return space_match_backend_u32_bound_checked(
        space_length64(s), SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE,
        out_len);
}

CettaIndex space_exact_match_indices64(Space *s, Atom *atom, CettaIndex **out) {
    space_dense_settle_for_pattern(s, atom);
    /* Settling marks and building the index are physical transitions. */
    CETTA_SCOPED_SHARED_TRANSITION(exact_match_indices);
    space_settle_marks(s);
    if (out)
        *out = NULL;
    /* Exact index is maintained for ALL space kinds - enable for native too */
    if (!s || !atom || atom_has_variables(atom) || !atom_is_exact_indexable(atom))
        return 0;
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        CettaIndex *matches = cetta_malloc(sizeof(CettaIndex) * (size_t)logical_len);
        CettaIndex n = 0;
        for (CettaIndex i = 0; i < logical_len; i++) {
            Atom *candidate = space_get_at64(s, i);
            if (candidate && atom_eq(candidate, atom))
                matches[n++] = i;
        }
        if (n == 0) {
            free(matches);
            return 0;
        }
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT, n);
        if (out)
            *out = matches;
        else
            free(matches);
        return n;
    }
    if (!space_sync_exact_membership_from_backend(s))
        return 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP);
    if (!s->native.universe)
        return 0;
    AtomId query_id = term_universe_lookup_atom_id(s->native.universe, atom);
    if (query_id == CETTA_ATOM_ID_NONE)
        return 0;
    ensure_exact_index(s);
    CettaIndex *matches = NULL;
    CettaIndex n = exact_index_rows(&s->native.exact_idx, query_id, &matches);
    if (n == 0) {
        free(matches);
        return 0;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT, n);
    if (out)
        *out = matches;
    else
        free(matches);
    return n;
}

uint32_t space_exact_match_indices(Space *s, Atom *atom, uint32_t **out) {
    space_dense_settle_for_pattern(s, atom);
    CettaIndex *wide = NULL;
    CettaIndex n64 = space_exact_match_indices64(s, atom, &wide);
    uint32_t n32 = 0;

    if (out)
        *out = NULL;
    if (!space_match_backend_u32_bound_checked(
            n64, SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE, &n32)) {
        free(wide);
        return 0;
    }
    if (n32 == 0) {
        free(wide);
        return 0;
    }
    uint32_t *narrow = cetta_malloc(sizeof(uint32_t) * n32);
    for (uint32_t i = 0; i < n32; i++) {
        if (!space_match_backend_u32_bound_checked(
                wide[i], SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE,
                &narrow[i])) {
            free(narrow);
            free(wide);
            return 0;
        }
    }
    free(wide);
    if (out)
        *out = narrow;
    return n32;
}

bool space_contains_exact(Space *s, Atom *atom) {
    if (space_dense_holds_atom(s, atom))
        return true;
    bool native_membership_authoritative =
        s && (!space_engine_uses_pathmap(s->match_backend.kind) ||
              (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
               s->match_backend.pathmap.bridge.preserve_logical_order));
    /* Doctrine-1 fast path: when native storage is authoritative, an exact-indexable
       atom's membership is an interned-id predicate over the dense presence
       bitset -- O(1), unclusterable -- so add-atom-nodup dedup no longer walks a
       fixed-bucket structural index for the FC-hot ground-theorem case.  Native
       overlays and order-pinned PathMap spaces use the same logical-view
       projection.  Backend-primary spaces and
       var/non-indexable atoms fall through to
       the exact-match path (id_present mirrors exact_idx, so this is equivalent
       to n>0, only cheaper). */
    if (native_membership_authoritative && atom &&
        atom_is_exact_indexable(atom)) {
        /* A removal leaves the presence bitset to a full resync; the exact
           index, which marked and swapped removals keep current, answers in
           its place. */
        CETTA_SCOPED_SHARED_TRANSITION(exact_membership);
        if (s->native.id_present_dirty && !s->native.exact_idx_dirty &&
            !space_has_overlay_base(s) &&
            (s->match_backend.kind == SPACE_ENGINE_NATIVE ||
             s->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) &&
            s->native.universe) {
            AtomId qid = term_universe_lookup_atom_id(s->native.universe, atom);
            return qid != CETTA_ATOM_ID_NONE &&
                   exact_index_holds_id(&s->native.exact_idx, qid);
        }
        /* Membership only needs the presence bitset, not the exact-index buckets,
           so bring id_present up to date lazily (append-only suffix sync; full
           resync only after a removal) -- never a full bucket rebuild inside a
           deferral window. */
        if (id_present_sync(s)) {
            AtomId qid = s->native.universe
                ? term_universe_lookup_atom_id(s->native.universe, atom)
                : CETTA_ATOM_ID_NONE;
            return qid != CETTA_ATOM_ID_NONE &&
                   id_present_contains(&s->native, qid);
        }
    }
    CettaIndex *matches = NULL;
    CettaIndex n = space_exact_match_indices64(s, atom, &matches);
    free(matches);
    return n > 0;
}

bool space_contains_exact_symbol_application(
        Space *s, SymbolId head, Atom *const *arguments,
        CettaExprLen argument_count, bool *out_applicable) {
    if (out_applicable)
        *out_applicable = false;
    bool native_membership_authoritative =
        s && !space_has_dependencies(s) &&
        (!space_engine_uses_pathmap(s->match_backend.kind) ||
         (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
          s->match_backend.pathmap.bridge.preserve_logical_order));
    if (!native_membership_authoritative || !s->native.universe ||
        head == SYMBOL_ID_NONE ||
        (argument_count > 0u && !arguments)) {
        return false;
    }
    for (CettaExprIndex index = 0u;
         index < argument_count; index++) {
        if (!atom_is_exact_indexable(arguments[index]))
            return false;
    }
    if (!id_present_sync(s))
        return false;
    SpaceDenseHead *dense_head = NULL;
    AtomId columns[CETTA_TUPLE_MAX_ARITY];
    if (space_dense_parts(s, head, arguments, argument_count, &dense_head,
                          columns) &&
        space_dense_kept_contains(dense_head, columns)) {
        if (out_applicable)
            *out_applicable = true;
        return true;
    }
    AtomId query_id = term_universe_lookup_symbol_application(
        s->native.universe, head, arguments, argument_count);
    if (out_applicable)
        *out_applicable = true;
    return query_id != CETTA_ATOM_ID_NONE &&
        id_present_contains(&s->native, query_id);
}

bool space_match_exists_ground_exact(Space *s, Atom *pattern,
                                     bool *out_applicable) {
    bool found = false;

    if (out_applicable)
        *out_applicable = false;
    if (!s || !pattern || atom_has_vars(pattern) ||
        !atom_is_exact_indexable(pattern) || space_has_dependencies(s)) {
        return false;
    }
    /* Membership answers only where a present pattern matches by identity. */
    if (!space_match_ground_pattern_matches_by_identity(pattern))
        return false;

    if (space_engine_uses_pathmap(s->match_backend.kind)) {
        /* Backend-primary PathMap maintains this one-bit row-class summary as
           exact AtomIds are added.  A dirty summary would require projecting
           the backend to prove the premise, so decline and leave the ordinary
           matcher in charge. */
        if (s->native.has_non_exact_atoms_dirty ||
            s->native.has_non_exact_atoms ||
            !space_match_backend_contains_atom_structural_direct(
                s, pattern, &found)) {
            return false;
        }
    } else if (space_contains_exact(s, pattern)) {
        /* A present pattern matches its own occurrence, whatever rows with
         * variables the space also holds; only a miss needs the frontier's
         * proof that none of them could match. */
        found = true;
    } else if (!space_contains_only_exact_atoms(s) &&
               !space_match_backend_ground_exact_exists_frontier(
                   s, pattern, &found)) {
        return false;
    }

    if (out_applicable)
        *out_applicable = true;
    return found;
}

bool space_match_exists_ground_exact_expression_coordinates(
        Space *s, Atom *const *coordinates,
        CettaExprLen coordinate_count, bool *out_applicable) {
    if (out_applicable)
        *out_applicable = false;
    if (!s || coordinate_count == 0u || !coordinates ||
        space_has_overlay_base(s) || space_has_dependencies(s) ||
        space_engine_uses_pathmap(s->match_backend.kind) ||
        !s->native.universe || !space_contains_only_exact_atoms(s)) {
        return false;
    }
    for (CettaExprIndex index = 0u;
         index < coordinate_count; index++) {
        if (!atom_is_exact_indexable(coordinates[index]))
            return false;
    }
    if (!id_present_sync(s))
        return false;
    AtomId query_id = term_universe_lookup_expression_coordinates(
        s->native.universe, coordinates, coordinate_count);
    if (out_applicable)
        *out_applicable = true;
    return query_id != CETTA_ATOM_ID_NONE &&
        id_present_contains(&s->native, query_id);
}

bool space_contains_canonical(Space *s, Atom *atom, bool *out_applicable) {
    bool native_membership_authoritative =
        s && (!space_engine_uses_pathmap(s->match_backend.kind) ||
              (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
               s->match_backend.pathmap.bridge.preserve_logical_order));
    /* Alpha-aware membership when native storage is authoritative -- O(1) amortized via the
       canonical presence bitset (keyed on first-occurrence-ordinal canonical
       ids). Native overlays and order-pinned PathMap spaces project their
       logical view through the same index.
       add-atom-nodup uses this so non-ground theorem dedup no longer scans
       every atom. *out_applicable is false for backend-primary spaces, where
       the caller keeps the exact + alpha-scan path. */
    if (out_applicable)
        *out_applicable = false;
    if (!native_membership_authoritative || !atom ||
        !term_universe_atom_is_stable(atom))
        return false;
    /* Alpha-aware membership needs only the presence bitset.  Sync it lazily here
       (append-only suffix; full resync only after a removal) so the forward-
       chaining deferral window stays O(1) per contains instead of rebuilding the
       exact-index buckets each time (the O(N^2) source). */
    if (!id_present_sync(s))
        return false;
    if (out_applicable)
        *out_applicable = true;
    if (space_dense_holds_atom(s, atom))
        return true;
    AtomId cid = space_canonical_id_for_query(s, atom);
    return cid != CETTA_ATOM_ID_NONE && id_present_contains(&s->native, cid);
}

bool space_contains_for_add_nodup(Space *s, Atom *atom) {
    if (!s || !atom)
        return false;
    bool found = false;
    bool backend_checked =
        space_match_backend_contains_atom_structural_direct(s, atom, &found);
    if (!backend_checked)
        found = space_contains_exact(s, atom);
    if (found || backend_checked || space_atom_is_exact_indexable(atom))
        return found;

    bool canonical_applicable = false;
    found = space_contains_canonical(s, atom, &canonical_applicable);
    if (canonical_applicable)
        return found;

    bool alpha_fallback = atom_has_vars(atom);
    /* The facts kept in relations are ground and were looked for above. */
    CettaCount logical_len = space_length64(s) - space_dense_facts(s);
    for (CettaIndex index = 0u; index < logical_len; index++) {
        Atom *candidate = space_get_at64(s, index);
        if (!candidate)
            continue;
        if (alpha_fallback ? atom_alpha_eq(candidate, atom)
                           : atom_eq(candidate, atom)) {
            return true;
        }
    }
    return false;
}

bool space_contains_only_exact_atoms(Space *s) {
    if (!s)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = 0; i < logical_len; i++) {
            Atom *atom = space_get_at64(s, i);
            if (!atom || !atom_is_exact_indexable(atom))
                return false;
        }
        return true;
    }
    if (!ensure_has_non_exact_atoms(s))
        return false;
    return !s->native.has_non_exact_atoms;
}

bool space_atom_is_exact_indexable(Atom *atom) {
    return atom_is_exact_indexable(atom);
}

CettaIndex space_exact_index_rows64(Space *s, Atom *pattern,
                                    CettaIndex **out) {
    space_dense_settle_for_pattern(s, pattern);
    *out = NULL;
    CETTA_SCOPED_SHARED_TRANSITION(exact_rows);
    if (!s || !pattern || !s->native.universe || s->native.exact_idx_dirty)
        return 0u;
    AtomId id = term_universe_lookup_atom_id(s->native.universe, pattern);
    return id == CETTA_ATOM_ID_NONE
        ? 0u : exact_index_rows(&s->native.exact_idx, id, out);
}

CettaIndex space_exact_index_rows_ids64(Space *s, SymbolId head,
                                        const AtomId *ids, CettaExprLen len,
                                        CettaIndex **out) {
    space_dense_settle_for_head(s, head, len);
    *out = NULL;
    CETTA_SCOPED_SHARED_TRANSITION(exact_rows);
    if (!s || !ids || !s->native.universe || s->native.exact_idx_dirty)
        return 0u;
    AtomId id = tu_expr_lookup_ids(s->native.universe, ids, len);
    return id == CETTA_ATOM_ID_NONE
        ? 0u : exact_index_rows(&s->native.exact_idx, id, out);
}

/* Whether the exact index, read or rebuilt here, holds no row with a
 * variable under HEAD and none without a symbol head: then every row an
 * atom headed by HEAD meets is an exact row.  False when it cannot answer.
 * Inside a deferral window rows arrive without index upkeep, so a rebuild
 * per query would cost the whole space each time; a rebuild would also
 * settle marks a query leaves to their own schedule.  A strict space builds
 * none.  The caller holds the space's transition. */
static bool exact_index_head_exact(Space *s, SymbolId head) {
    if (s->native.exact_idx_dirty) {
        if (!space_keeps_exact_index(s) ||
            s->native.secondary_index_deferral_depth > 0u ||
            s->native.marked != 0u)
            return false;
        exact_index_rebuild(s);
    }
    ExactAtomIndex *idx = &s->native.exact_idx;
    if (idx->inexact_unheaded != 0u)
        return false;
    ExactIndexInexactHead *entry = exact_index_inexact_head(idx, head, false);
    return !entry || entry->rows == 0u;
}

bool space_exact_frontier_certified(Space *s, Atom *pattern) {
    if (pattern && pattern->kind == ATOM_EXPR && pattern->expr.len != 0u &&
        pattern->expr.elems[0]->kind == ATOM_SYMBOL)
        return space_exact_frontier_certified_head(
            s, pattern->expr.elems[0]->sym_id, pattern->expr.len);
    space_dense_settle_for_pattern(s, pattern);
    return false;
}

bool space_exact_frontier_certified_head(Space *s, SymbolId head,
                                         CettaExprLen len) {
    space_dense_settle_for_head(s, head, len);
    if (!s || len == 0u || space_has_overlay_base(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    /* The index is read, and rebuilt by its first reader, as one physical
     * transition of the space. */
    CETTA_SCOPED_SHARED_TRANSITION(exact_frontier);
    if (!exact_index_head_exact(s, head))
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_EXACT_FRONTIER_CERTIFIED);
    return true;
}

bool space_exact_head_certified(Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE || !s->native.universe ||
        space_has_overlay_base(s) || space_has_dependencies(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    CETTA_SCOPED_SHARED_TRANSITION(exact_head);
    return exact_index_head_exact(s, head);
}

bool space_remove_exact_ids(Space *s, SymbolId head, const AtomId *ids,
                            size_t count) {
    if (!s || head == SYMBOL_ID_NONE || (count && !ids) ||
        !s->native.universe || space_has_overlay_base(s) ||
        space_has_dependencies(s) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    CETTA_SCOPED_SHARED_TRANSITION(exact_removal);
    /* A row holding a variable could unify with an instance without
     * equalling it; the index certifies there is none under this head. */
    if (!exact_index_head_exact(s, head))
        return false;
    ExactAtomIndex *idx = &s->native.exact_idx;
    /* Only an exact row is filed under its id. */
    for (size_t i = 0u; i < count; i++) {
        if (!atom_id_is_exact_indexable(s, ids[i]))
            return false;
    }
    /* Each id's fact goes from its relation; its row copies, below. */
    for (size_t i = 0u; space_dense_facts(s) != 0u && i < count; i++)
        (void)space_dense_route_remove(s, ids[i]);
    CettaIndex *positions = NULL;
    CettaIndex position_len = 0u, position_cap = 0u;
    for (size_t i = 0u; i < count; i++) {
        bool repeated = false;
        for (size_t j = 0u; j < i && !repeated && i - j <= 8u; j++)
            repeated = ids[j] == ids[i];
        if (repeated)
            continue;
        CettaIndex *rows = NULL;
        CettaIndex row_len = exact_index_rows(idx, ids[i], &rows);
        for (CettaIndex r = 0u; r < row_len; r++) {
            if (position_len == position_cap) {
                position_cap = position_cap ? position_cap * 2u : 16u;
                positions = cetta_realloc(positions,
                                          sizeof(*positions) * position_cap);
            }
            positions[position_len++] = rows[r];
        }
        free(rows);
    }
    if (position_len == 0u) {
        free(positions);
        return true;
    }
    /* Index rows are physical; the removal reads ascending logical
     * positions, each once. */
    position_len = space_marks_physical_to_logical(s, positions, position_len);
    qsort(positions, (size_t)position_len, sizeof(*positions),
          exact_index_row_compare);
    CettaIndex unique = 0u;
    for (CettaIndex i = 0u; i < position_len; i++) {
        if (unique == 0u || positions[unique - 1u] != positions[i])
            positions[unique++] = positions[i];
    }
    bool removed = unique == 0u ||
        space_remove_logical_positions(s, positions, unique);
    free(positions);
    return removed;
}

/* ── Type Expression Normalization ───────────────────────────────────────── */

/* Evaluate grounded arithmetic in type expressions using an explicit
   post-order worklist.  Structural depth is an input property, not an
   implicit semantic budget.
   E.g., (VecN String (+ (+ 0 1) 1)) → (VecN String 2) */
typedef struct {
    Atom *input;
    Atom **children;
    CettaExprIndex next_child;
    bool changed;
} TypeNormalizeFrame;

Atom *normalize_type_expr_head(Arena *a, Atom *norm) {
    if (!norm || norm->kind != ATOM_EXPR || norm->expr.len < 2) return norm;
    /* Dispatch only the type-pure capability: type conversion must never run
       an effectful or state-reading grounded op.  Anything outside the
       capability stays un-dispatched (an inert expression in the type). */
    SymbolId op_id = SYMBOL_ID_NONE;
    if (norm->expr.elems[0]->kind == ATOM_SYMBOL)
        op_id = norm->expr.elems[0]->sym_id;
    if (norm->expr.len >= 3 && op_id != SYMBOL_ID_NONE &&
        grounded_op_is_type_pure(op_id)) {
        Atom *result = grounded_dispatch(a, norm->expr.elems[0],
            norm->expr.elems + 1, norm->expr.len - 1);
        if (result) return result;
    }
    return norm;
}

Atom *normalize_type_expr(Arena *a, Atom *ty) {
    if (!ty || ty->kind != ATOM_EXPR || ty->expr.len < 2) return ty;

    size_t cap = 16;
    size_t len = 1;
    TypeNormalizeFrame *stack = cetta_malloc(sizeof(*stack) * cap);
    stack[0] = (TypeNormalizeFrame){.input = ty};

    for (;;) {
        TypeNormalizeFrame *frame = &stack[len - 1u];
        Atom *input = frame->input;
        if (!frame->children) {
            frame->children = arena_alloc(
                a, sizeof(Atom *) * (size_t)input->expr.len);
        }

        if (frame->next_child < input->expr.len) {
            Atom *child = input->expr.elems[frame->next_child];
            if (!child || child->kind != ATOM_EXPR || child->expr.len < 2) {
                frame->children[frame->next_child++] = child;
                continue;
            }
            if (len == cap) {
                if (cap > SIZE_MAX / 2u ||
                    cap * 2u > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    fputs("fatal: type-normalization worklist exceeds "
                          "addressable storage\n", stderr);
                    abort();
                }
                cap *= 2u;
                stack = cetta_realloc(stack, sizeof(*stack) * cap);
            }
            stack[len++] = (TypeNormalizeFrame){.input = child};
            continue;
        }

        Atom *norm = frame->changed
            ? atom_expr(a, frame->children, input->expr.len)
            : input;
        Atom *completed = normalize_type_expr_head(a, norm);
        len--;
        if (len == 0) {
            free(stack);
            return completed;
        }
        frame = &stack[len - 1u];
        CettaExprIndex child_index = frame->next_child++;
        frame->children[child_index] = completed;
        if (completed != frame->input->expr.elems[child_index])
            frame->changed = true;
    }
}

/* ── Type Lookup ─────────────────────────────────────────────────────────── */

static const char *native_handle_kind_name(Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3) return NULL;
    if (!atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.native_handle)) return NULL;
    Atom *kind = atom->expr.elems[1];
    if (kind->kind == ATOM_GROUNDED && kind->ground.gkind == GV_STRING) {
        return kind->ground.sval;
    }
    if (kind->kind == ATOM_SYMBOL) {
        return atom_name_cstr(kind);
    }
    return NULL;
}

static Atom *get_native_handle_type(Arena *a, Atom *atom) {
    const char *kind = native_handle_kind_name(atom);
    if (!kind) return atom_undefined_type(a);
    if (strcmp(kind, "mork-space") == 0) {
        return atom_symbol(a, "MorkSpace");
    }
    return atom_undefined_type(a);
}

Atom *get_grounded_type(Arena *a, Atom *atom) {
    if (atom->kind != ATOM_GROUNDED) return atom_undefined_type(a);
    switch (atom->ground.gkind) {
    case GV_INT:    return atom_symbol_literal(a, "Number");
    case GV_BIGINT: return atom_symbol_literal(a, "Number");
    case GV_RATIONAL: return atom_symbol_literal(a, "Number");
    case GV_FLOAT:  return atom_symbol_literal(a, "Number");
    case GV_BOOL:   return atom_symbol_literal(a, "Bool");
    case GV_STRING: return atom_symbol_literal(a, "String");
    case GV_SPACE: {
        const Space *space = (const Space *)atom->ground.ptr;
        const char *space_type = "atom";
        if (space) {
            if (space_is_stack(space)) {
                space_type = "stack";
            } else if (space_is_queue(space)) {
                space_type = "queue";
            } else if (space_is_hash(space)) {
                space_type = "hash";
            } else if (space->match_backend.kind == SPACE_ENGINE_PATHMAP) {
                space_type = "pathmap";
            } else if (space->match_backend.kind == SPACE_ENGINE_MORK) {
                space_type = "pathmap";
            }
        }
        return atom_expr2(a, atom_symbol_literal(a, "Space"),
                          atom_symbol_literal(a, space_type));
    }
    case GV_BINDINGS:
        return atom_symbol_literal(a, "Bindings");
    case GV_FOREIGN:
        return atom_symbol_literal(a, "Foreign");
    case GV_STRUCTURAL: {
        const char *name = atom_structural_type_name(atom);
        return name ? atom_symbol(a, name) : atom_undefined_type(a);
    }
    case GV_SEAL: {
        /* Sealed, SealFacet or Key: the kind of authority, never its brand. */
        const char *name = atom_seal_type_name(atom);
        return name ? atom_symbol(a, name) : atom_undefined_type(a);
    }
    case GV_TERM_GRAPH:
        /* A rational term is a list or a compound term: an expression. */
        return atom_symbol_literal(a, "Expression");
    case GV_CAPTURE:
        return atom_expr3(a, atom_symbol_literal(a, "->"),
                          atom_atom_type(a), atom_atom_type(a));
    case GV_STATE: {
        StateCell *cell = (StateCell *)atom->ground.ptr;
        if (cell->content_type &&
            !atom_is_symbol_id(cell->content_type, g_builtin_syms.undefined_type))
            return atom_expr2(a, atom_symbol_literal(a, "StateMonad"), cell->content_type);
        return atom_symbol_literal(a, "State");
    }
    case GV_PRIME_NEED_CAPABILITY:
        return atom_symbol_literal(a, "PrimeNeedCapability");
    case GV_PRIME_CONTEXT:
        return atom_symbol_literal(a, "context");
    case GV_INTERNAL_TAG:
        return atom_undefined_type(a);
    }
    return atom_undefined_type(a);
}

static bool type_inference_step(CettaTypeInferenceBudget *budget,
                                uint64_t amount) {
    if (!budget) return true;
    if (!budget->complete) return false;
    if (!budget->steps_limited) return true;
    if (budget->work_steps_observed > UINT64_MAX - amount)
        budget->work_steps_observed = UINT64_MAX;
    else
        budget->work_steps_observed += amount;
    return true;
}

static bool type_inference_can_add(CettaTypeInferenceBudget *budget,
                                   uint32_t count) {
    if (!budget || budget->type_capacity == 0 ||
        count < budget->type_capacity) return true;
    budget->complete = false;
    budget->type_capacity_exhausted = true;
    return false;
}

/* Exclusion uses the same incrementally maintained declaration index as
 * exact type lookup. Bucket zero retains variable/structured heads. A hash
 * collision admits extra candidates; it cannot exclude a real match.
 * Open outer rows and unsupported stores retain the ordinary relational path. */
bool space_type_annotation_may_match_subject(Space *s, const Atom *subject) {
    CETTA_SCOPED_SHARED_TRANSITION(observation);
    if (!s || !subject || space_has_overlay_base(s) ||
        space_has_dependencies(s) ||
        s->match_backend.kind != SPACE_ENGINE_NATIVE)
        return true;
    if (subject->kind != ATOM_SYMBOL &&
        (subject->kind != ATOM_EXPR ||
         atom_petta_value_representation(subject) != PETTA_VALUE_ORDINARY ||
         atom_structural_has_rational(subject)))
        return true;
    SymbolId head = subject->kind == ATOM_SYMBOL ? subject->sym_id :
        atom_head_symbol_id(subject);
    if (head == SYMBOL_ID_NONE)
        return true;
    ensure_ty_ann_index(s);
    if (s->native.ty_idx_dirty || s->native.ty_idx.has_open_rows)
        return true;
    const SpaceHeadChain *chain =
        space_head_chains_find(&s->native.ty_idx.rows, head);
    return (chain && chain->count != 0u) ||
           s->native.ty_idx.rows.headless.count != 0u;
}

/* Symbol-subject declarations cannot refine variables in a compound subject.
 * The caller separately excludes authored classifiers before using an
 * intrinsic answer producer. Unsupported views receive no certificate. */
bool space_type_annotations_have_only_symbol_subjects(Space *s) {
    CETTA_SCOPED_SHARED_TRANSITION(observation);
    if (!s || space_has_overlay_base(s) || space_has_dependencies(s) ||
        s->match_backend.kind != SPACE_ENGINE_NATIVE)
        return false;
    ensure_ty_ann_index(s);
    return !s->native.ty_idx_dirty && !s->native.ty_idx.has_open_rows &&
           !s->native.ty_idx.has_non_symbol_subjects;
}

/* With symbol subjects only, each subject's rows form one chain, and a chain
 * longer than one row is a second declaration (a duplicated row included). */
bool space_type_annotations_functional(Space *s) {
    CETTA_SCOPED_SHARED_TRANSITION(observation);
    if (!space_type_annotations_have_only_symbol_subjects(s))
        return false;
    const SpaceHeadChains *rows = &s->native.ty_idx.rows;
    if (rows->headless.count != 0u)
        return false;
    for (uint32_t slot = 0u; slot < rows->chain_cap; slot++) {
        if (rows->chains[slot].head != SYMBOL_ID_NONE &&
            rows->chains[slot].count > 1u)
            return false;
    }
    return !space_equations_may_match_known_head(s, g_builtin_syms.get_type);
}

bool space_type_arrow_declarations_visit(
    Space *s,
    bool (*visit)(void *context, SymbolId subject, uint32_t declarations,
                  Atom *arrow),
    void *context) {
    CETTA_SCOPED_SHARED_TRANSITION(observation);
    if (!s || !visit || space_has_overlay_base(s) ||
        space_has_dependencies(s) ||
        s->match_backend.kind != SPACE_ENGINE_NATIVE)
        return false;
    ensure_ty_ann_index(s);
    if (s->native.ty_idx_dirty || s->native.ty_idx.has_open_rows)
        return false;
    const SpaceHeadChains *rows = &s->native.ty_idx.rows;
    for (uint32_t slot = 0u; slot < rows->chain_cap; slot++) {
        const SpaceHeadChain *chain = &rows->chains[slot];
        if (chain->head == SYMBOL_ID_NONE || chain->count == 0u)
            continue;
        for (uint32_t i = chain->first; i != SPACE_HEAD_CHAIN_END;
             i = rows->next[i]) {
            Atom *annotation = space_indexed_occurrence_atom(
                s, rows->atom_indices, rows->atom_ids, i);
            if (!annotation || annotation->kind != ATOM_EXPR ||
                annotation->expr.len != 3u ||
                !atom_is_symbol_id(annotation->expr.elems[0],
                                   g_builtin_syms.colon) ||
                !atom_is_symbol_id(annotation->expr.elems[1], chain->head))
                continue;
            Atom *type = annotation->expr.elems[2];
            if (type->kind == ATOM_EXPR && type->expr.len > 0u &&
                atom_is_symbol_id(type->expr.elems[0],
                                  g_builtin_syms.arrow) &&
                !visit(context, chain->head, chain->count, type))
                return true;
        }
    }
    return true;
}

/* Resolve (: atom type) annotations through the native index. Overlay spaces
 * retain their logical-view fallback and optional lookup cost record. */
/* Declared types gathered across the members of a composed lookup. */
typedef struct {
    Atom **types;
    uint32_t count;
    uint32_t cap;
    bool stopped;
} SpaceAnnotatedTypes;

static void space_annotated_types_append(SpaceAnnotatedTypes *list,
                                         Atom *type) {
    if (list->count >= list->cap) {
        list->cap = list->cap ? list->cap * 2u : 4u;
        list->types = cetta_realloc(list->types,
                                    sizeof(Atom *) * list->cap);
    }
    list->types[list->count++] = type;
}

/* One member of a type lookup: the space's own declarations, then, unless
 * `own_only`, the ones it holds as imported atoms.  False when allocation
 * fails. */
static inline __attribute__((always_inline)) bool
space_member_annotated_types(
        Space *s, Arena *a, Atom *atom, SpaceAnnotatedTypes *list,
        CettaTypeInferenceBudget *budget,
        SpaceDeclaredTypeLookupCost *cost, bool own_only) {
    if (space_has_overlay_base(s) || !declared_type_index_enabled()) {
        CettaCount logical_len = space_length64(s);
        CettaIndex imported = space_imported_length(s);
        if (imported > logical_len)
            imported = 0u;
        int passes = own_only || imported == 0u ? 1 : 2;
        for (int pass = 0; pass < passes && !list->stopped; pass++)
        for (CettaIndex i = pass == 0 ? imported : 0u;
             i < (pass == 0 ? logical_len : imported); i++) {
            if (cost)
                cost->full_space_rows_examined++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_DECLARED_TYPE_FULL_SCAN_ROW);
            if (!type_inference_step(budget, 1)) {
                list->stopped = true;
                break;
            }
            Atom *annotation = space_get_at64(s, i);
            if (!annotation || annotation->kind != ATOM_EXPR ||
                annotation->expr.len != 3)
                continue;
            if (!atom_is_symbol_id(annotation->expr.elems[0], g_builtin_syms.colon))
                continue;
            if (!atom_eq(annotation->expr.elems[1], atom))
                continue;
            if (!type_inference_can_add(budget, list->count)) {
                list->stopped = true;
                break;
            }
            Atom *type = annotation->expr.elems[2];
            Atom *copy = atom_has_vars(type)
                ? cetta_instantiate_frame_syntax(a, type)
                : atom_deep_copy_shared(a, type);
            if (!copy)
                return false;
            space_annotated_types_append(list, copy);
        }
        return true;
    }
    /* Use type annotation index for O(bucket_size) instead of O(N) */
    ensure_ty_ann_index(s);
    if (cost)
        cost->indexed_lookups++;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_DECLARED_TYPE_INDEXED_LOOKUP);
    const SpaceHeadChains *index = &s->native.ty_idx.rows;
    const SpaceHeadChain *chain =
        space_head_chains_find(index, ty_ann_subject_head(atom));
    if (!chain || chain->count == 0u)
        return true;
    CettaIndex imported = s->imported_len;
    int passes = own_only || imported == 0u ? 1 : 2;
    /* The space's own declarations precede imported ones. */
    for (int pass = 0; pass < passes && !list->stopped; pass++)
    for (uint32_t i = chain->first; i != SPACE_HEAD_CHAIN_END; i = index->next[i]) {
        if (imported != 0u &&
            (index->atom_indices[i] < imported) != (pass == 1))
            continue;
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        if (cost)
            cost->indexed_rows_examined++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_DECLARED_TYPE_INDEXED_ROW);
        if (!type_inference_step(budget, 1)) {
            list->stopped = true;
            break;
        }
        AtomId annotation_id = space_indexed_occurrence_atom_id(
            s, index->atom_indices, index->atom_ids, i);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, annotation_id,
                                                  &subject_id, &type_id)) {
            if (term_universe_atom_id_eq(s->native.universe, subject_id, atom)) {
                CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
                CettaFrameIdentity identity = CETTA_FRAME_IDENTITY_NONE;
                if (tu_has_vars(s->native.universe, type_id) &&
                    !cetta_frame_identity_scope_try(&frame_identity_scope, &identity))
                    return false;
                Atom *type_copy = !cetta_frame_identity_is_none(identity)
                    ? term_universe_copy_atom_epoch(s->native.universe, a, type_id, identity)
                    : term_universe_copy_atom(s->native.universe, a, type_id);
                if (!type_copy)
                    return false;
                if (!type_inference_can_add(budget, list->count)) {
                    list->stopped = true;
                    break;
                }
                space_annotated_types_append(list, type_copy);
                continue;
            }
        }
        Atom *annotation = space_indexed_occurrence_atom(
            s, index->atom_indices, index->atom_ids, i);
        if (!annotation || annotation->kind != ATOM_EXPR || annotation->expr.len != 3)
            continue;
        if (!atom_is_symbol_id(annotation->expr.elems[0], g_builtin_syms.colon))
            continue;
        if (!atom_eq(annotation->expr.elems[1], atom))
            continue;
        if (!type_inference_can_add(budget, list->count)) {
            list->stopped = true;
            break;
        }
        Atom *type = annotation->expr.elems[2];
        Atom *copy = atom_has_vars(type)
            ? cetta_instantiate_frame_syntax(a, type)
            : atom_deep_copy_shared(a, type);
        if (!copy)
            return false;
        space_annotated_types_append(list, copy);
    }
    return true;
}

/* The imported modules' share of a type lookup, off the common path. */
static __attribute__((noinline)) bool space_dependencies_annotated_types(
        Space *s, Arena *a, Atom *atom, SpaceAnnotatedTypes *list,
        CettaTypeInferenceBudget *budget,
        SpaceDeclaredTypeLookupCost *cost) {
    bool ok = true;
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; ok && !list->stopped && d < dependencies; d++)
        ok = space_member_annotated_types(
            space_dependency_at(s, d), a, atom, list, budget, cost, true);
    return ok;
}

static uint32_t get_annotated_types(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types,
                                    CettaTypeInferenceBudget *budget,
                                    SpaceDeclaredTypeLookupCost *cost) {
    Atom *const *grounded = NULL;
    uint32_t grounded_count = atom && atom->kind == ATOM_SYMBOL
        ? space_grounded_types(atom->sym_id, &grounded) : 0u;
    if (grounded_count != 0u) {
        /* A grounded operation's type is its own. */
        Atom **types = cetta_malloc(sizeof(*types) * grounded_count);
        uint32_t count = 0u;
        for (uint32_t i = 0u; i < grounded_count; i++) {
            if (!type_inference_step(budget, 1) ||
                !type_inference_can_add(budget, count))
                break;
            Atom *copy = atom_has_vars(grounded[i])
                ? cetta_instantiate_frame_syntax(a, grounded[i])
                : atom_deep_copy_shared(a, grounded[i]);
            if (!copy) {
                free(types);
                goto allocation_failed;
            }
            types[count++] = copy;
        }
        if (count == 0u) {
            free(types);
            types = NULL;
        }
        *out_types = types;
        return count;
    }
    /* A module space answers its own declarations, then each imported
       module's own, in import order (HE's ModuleSpace query). */
    SpaceAnnotatedTypes list = {0};
    bool ok = space_member_annotated_types(
        s, a, atom, &list, budget, cost, false);
    if (ok && !list.stopped && space_has_dependencies(s))
        ok = space_dependencies_annotated_types(
            s, a, atom, &list, budget, cost);
    if (!ok) {
        free(list.types);
        goto allocation_failed;
    }
    *out_types = list.types;
    return list.count;
allocation_failed:
    if (budget) {
        budget->complete = false;
        budget->evaluator_capacity_exhausted = true;
    }
    *out_types = NULL;
    return 0u;
}

uint32_t space_get_declared_types(
    Space *s, Arena *a, Atom *subject, Atom ***out_types) {
    return space_get_declared_types_costed(
        s, a, subject, out_types, NULL);
}

uint32_t space_get_declared_types_costed(
    Space *s, Arena *a, Atom *subject, Atom ***out_types,
    SpaceDeclaredTypeLookupCost *cost) {
    if (!out_types)
        return 0u;
    *out_types = NULL;
    if (!s || !a || !subject)
        return 0u;
    return get_annotated_types(s, a, subject, out_types, NULL, cost);
}

static bool tuple_type_part_keep(Atom *type, bool is_head) {
    if (!type || atom_is_symbol_id(type, g_builtin_syms.undefined_type))
        return false;
    if (is_head && type->kind == ATOM_EXPR && type->expr.len >= 2 &&
        atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow)) {
        return false;
    }
    return true;
}

/* What one inference learned of the ground expressions it met.  A value
 * shares subterms, and inference reads each argument's types, so a shared
 * argument is inferred once per inference rather than once per path to it.
 * Only ground answers are kept: a polymorphic type is instantiated afresh
 * where it is read.  Nothing inference reads changes while it runs, and its
 * answers live in the arena it was given until it returns.  A budgeted
 * inference counts its steps, so it is never answered from here. */
typedef struct HeTypingsMemoValue HeTypingsMemoValue;

/* What a memo entry answers: the types of `get_atom_types`, with or without
 * the subject's own declarations, or HE's typing derivations. */
typedef enum {
    TYPE_INFERENCE_MEMO_TYPES_INFERRED = 0,
    TYPE_INFERENCE_MEMO_TYPES_DECLARED = 1,
    TYPE_INFERENCE_MEMO_HE_TYPINGS = 2,
} TypeInferenceMemoQuestion;

typedef struct {
    const Atom *atom;
    TypeInferenceMemoQuestion question;
    uint32_t count;
    Atom **types;
    HeTypingsMemoValue *typings;
} TypeInferenceMemoEntry;

typedef struct {
    TypeInferenceMemoEntry *slots;
    size_t cap;
    size_t len;
    uint32_t depth;
    Space *space;
    Arena *arena;
} TypeInferenceMemo;

static __thread TypeInferenceMemo g_type_inference_memo;

static size_t type_inference_memo_slot(const Atom *atom,
                                       TypeInferenceMemoQuestion question,
                                       size_t cap) {
    uintptr_t key = (uintptr_t)atom ^ (uintptr_t)question;
    key ^= key >> 33u;
    key *= (uintptr_t)UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 29u;
    return (size_t)key & (cap - 1u);
}

static TypeInferenceMemoEntry *type_inference_memo_find(
        const Atom *atom, TypeInferenceMemoQuestion question) {
    TypeInferenceMemo *memo = &g_type_inference_memo;
    if (!memo->cap)
        return NULL;
    size_t slot = type_inference_memo_slot(atom, question, memo->cap);
    while (memo->slots[slot].atom) {
        TypeInferenceMemoEntry *entry = &memo->slots[slot];
        if (entry->atom == atom && entry->question == question)
            return entry;
        slot = (slot + 1u) & (memo->cap - 1u);
    }
    return NULL;
}

/* A slot for `atom`, or NULL where the memo cannot grow (it is only a
 * shortcut, so the caller then keeps nothing). */
static TypeInferenceMemoEntry *type_inference_memo_claim(
        const Atom *atom, TypeInferenceMemoQuestion question) {
    TypeInferenceMemo *memo = &g_type_inference_memo;
    if ((memo->len + 1u) * 2u > memo->cap) {
        size_t cap = memo->cap ? memo->cap * 2u : 64u;
        TypeInferenceMemoEntry *slots = calloc(cap, sizeof(*slots));
        if (!slots)
            return NULL;
        for (size_t i = 0u; i < memo->cap; i++) {
            if (!memo->slots[i].atom)
                continue;
            size_t slot = type_inference_memo_slot(
                memo->slots[i].atom, memo->slots[i].question, cap);
            while (slots[slot].atom)
                slot = (slot + 1u) & (cap - 1u);
            slots[slot] = memo->slots[i];
        }
        free(memo->slots);
        memo->slots = slots;
        memo->cap = cap;
    }
    size_t slot = type_inference_memo_slot(atom, question, memo->cap);
    while (memo->slots[slot].atom)
        slot = (slot + 1u) & (memo->cap - 1u);
    memo->len++;
    memo->slots[slot].atom = atom;
    memo->slots[slot].question = question;
    return &memo->slots[slot];
}

static void he_typings_memo_value_free(HeTypingsMemoValue *value);

static void type_inference_memo_clear(void) {
    TypeInferenceMemo *memo = &g_type_inference_memo;
    for (size_t i = 0u; i < memo->cap; i++) {
        free(memo->slots[i].types);
        he_typings_memo_value_free(memo->slots[i].typings);
    }
    free(memo->slots);
    *memo = (TypeInferenceMemo){0};
}

/* Whether an inference of `atom` in `s` and `a` may read and fill the memo:
 * it runs inside another one over the same program and arena, unbudgeted,
 * and asks about a ground expression. */
static bool type_inference_memo_applies(Space *s, Arena *a, const Atom *atom,
                                        CettaTypeInferenceBudget *budget) {
    TypeInferenceMemo *memo = &g_type_inference_memo;
    return !budget && memo->depth > 0u && memo->space == s &&
        memo->arena == a && atom && atom->kind == ATOM_EXPR &&
        !atom_has_vars(atom);
}

/* Open or join the inference that owns the memo; false when this call runs
 * over another program or arena than the one open, and so stays outside. */
static bool type_inference_memo_enter(Space *s, Arena *a,
                                      CettaTypeInferenceBudget *budget) {
    TypeInferenceMemo *memo = &g_type_inference_memo;
    if (budget)
        return false;
    if (memo->depth == 0u) {
        memo->space = s;
        memo->arena = a;
    } else if (memo->space != s || memo->arena != a) {
        return false;
    }
    memo->depth++;
    return true;
}

static void type_inference_memo_leave(void) {
    if (--g_type_inference_memo.depth == 0u)
        type_inference_memo_clear();
}

static bool type_inference_types_ground(Atom *const *types, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++)
        if (!types[i] || atom_has_vars(types[i]))
            return false;
    return true;
}

static Atom **type_inference_types_copy(Atom *const *types, uint32_t count) {
    if (!count)
        return NULL;
    Atom **copy = malloc(sizeof(*copy) * count);
    if (copy)
        memcpy(copy, types, sizeof(*copy) * count);
    return copy;
}

static uint32_t get_tuple_value_part_types(Space *s, Arena *a, Atom *atom,
                                           bool is_head, Atom ***out_types,
                                           CettaTypeInferenceBudget *budget);
static uint32_t get_atom_types_mode(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types,
                                    bool include_direct_annotations,
                                    CettaTypeInferenceBudget *budget);

static uint32_t get_tuple_value_part_types(Space *s, Arena *a, Atom *atom,
                                           bool is_head, Atom ***out_types,
                                           CettaTypeInferenceBudget *budget) {
    Atom **raw = NULL;
    uint32_t raw_count = 0;

    if (!type_inference_step(budget, 1)) {
        *out_types = NULL;
        return 0;
    }

    switch (atom->kind) {
    case ATOM_VAR:
        *out_types = NULL;
        return 0;
    case ATOM_GROUNDED: {
        Atom *ty = get_grounded_type(a, atom);
        if (tuple_type_part_keep(ty, is_head)) {
            raw = cetta_malloc(sizeof(Atom *));
            raw[0] = ty;
            *out_types = raw;
            return 1;
        }
        *out_types = NULL;
        return 0;
    }
    case ATOM_SYMBOL:
        raw_count = get_annotated_types(s, a, atom, &raw, budget, NULL);
        break;
    case ATOM_EXPR:
        raw_count = get_atom_types_mode(s, a, atom, &raw, true, budget);
        break;
    }

    Atom **types = NULL;
    uint32_t count = 0;
    for (uint32_t i = 0; i < raw_count; i++) {
        if (!type_inference_step(budget, 1)) break;
        if (!tuple_type_part_keep(raw[i], is_head))
            continue;
        if (!type_inference_can_add(budget, count)) break;
        types = cetta_realloc(types, sizeof(Atom *) * (count + 1));
        types[count++] = raw[i];
    }
    free(raw);
    *out_types = types;
    return count;
}

typedef struct {
    Atom **items;
    uint32_t len;
} TupleTypeChoices;

static void tuple_type_choices_free(TupleTypeChoices *choices, CettaExprLen len) {
    if (!choices)
        return;
    for (CettaExprIndex i = 0; i < len; i++)
        free(choices[i].items);
    free(choices);
}

static uint32_t infer_tuple_value_types(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types,
                                        CettaTypeInferenceBudget *budget) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        *out_types = NULL;
        return 0;
    }

    CettaExprLen len = atom->expr.len;
    TupleTypeChoices *choices = cetta_malloc(sizeof(TupleTypeChoices) * len);
    memset(choices, 0, sizeof(TupleTypeChoices) * len);
    uint64_t total = 1;

    for (CettaExprIndex i = 0; i < len; i++) {
        choices[i].len = get_tuple_value_part_types(s, a, atom->expr.elems[i],
                                                    i == 0, &choices[i].items,
                                                    budget);
        if (choices[i].len == 0 ||
            total > UINT32_MAX / choices[i].len ||
            total > SIZE_MAX / sizeof(Atom *) / choices[i].len) {
            if (budget && choices[i].len != 0) {
                budget->complete = false;
                budget->type_capacity_exhausted = true;
            }
            tuple_type_choices_free(choices, len);
            *out_types = NULL;
            return 0;
        }
        total *= choices[i].len;
    }

    uint32_t allocation_count = (uint32_t)total;
    if (budget && budget->type_capacity != 0 &&
        allocation_count > budget->type_capacity)
        allocation_count = budget->type_capacity;
    Atom **types = cetta_malloc(sizeof(Atom *) * allocation_count);
    uint32_t count = 0;
    for (uint64_t n = 0; n < total; n++) {
        if (!type_inference_step(budget, 1) ||
            !type_inference_can_add(budget, count)) {
            break;
        }
        uint64_t rem = n;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
        for (CettaExprIndex pos = len; pos > 0; pos--) {
            uint32_t idx = (uint32_t)(rem % choices[pos - 1].len);
            rem /= choices[pos - 1].len;
            elems[pos - 1] = choices[pos - 1].items[idx];
        }
        types[count++] = atom_expr(a, elems, len);
    }

    tuple_type_choices_free(choices, len);
    *out_types = types;
    return count;
}

static uint32_t he_close_type_array(Space *s, Arena *a, Atom ***types_io,
                                    uint32_t count, uint32_t from,
                                    CettaTypeInferenceBudget *budget);

static bool he_program_declares_subtypes(const Space *s);

/* HE closes types under the program's subtypes; most programs declare
 * none, so the space's own flag is read before anything else. */
static bool he_types_close_under_subtypes(const Space *s) {
    return he_program_declares_subtypes(s) && eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_HE;
}

static uint32_t get_atom_types_mode_inferred(Space *s, Arena *a, Atom *atom,
                                             Atom ***out_types,
                                             bool include_direct_annotations,
                                             CettaTypeInferenceBudget *budget);

static uint32_t get_atom_types_mode(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types,
                                    bool include_direct_annotations,
                                    CettaTypeInferenceBudget *budget) {
    TypeInferenceMemoQuestion question = include_direct_annotations
        ? TYPE_INFERENCE_MEMO_TYPES_DECLARED
        : TYPE_INFERENCE_MEMO_TYPES_INFERRED;
    bool keyed = type_inference_memo_applies(s, a, atom, budget);
    if (keyed) {
        TypeInferenceMemoEntry *known = type_inference_memo_find(atom, question);
        if (known) {
            Atom **types = type_inference_types_copy(known->types, known->count);
            if (types || !known->count) {
                *out_types = types;
                return known->count;
            }
        }
    }
    bool entered = type_inference_memo_enter(s, a, budget);
    uint32_t count = get_atom_types_mode_inferred(
        s, a, atom, out_types, include_direct_annotations, budget);
    if (keyed && type_inference_types_ground(*out_types, count) &&
        !type_inference_memo_find(atom, question)) {
        Atom **kept = type_inference_types_copy(*out_types, count);
        TypeInferenceMemoEntry *entry = (kept || !count)
            ? type_inference_memo_claim(atom, question) : NULL;
        if (entry) {
            entry->count = count;
            entry->types = kept;
        } else {
            free(kept);
        }
    }
    if (entered)
        type_inference_memo_leave();
    return count;
}

static uint32_t get_atom_types_mode_inferred(Space *s, Arena *a, Atom *atom,
                                             Atom ***out_types,
                                             bool include_direct_annotations,
                                             CettaTypeInferenceBudget *budget) {
    uint32_t count = 0;
    Atom **types = NULL;
    if (!type_inference_step(budget, 1)) {
        *out_types = NULL;
        return 0;
    }
    Atom *native_handle_type = get_native_handle_type(a, atom);

    if (!atom_is_symbol_id(native_handle_type, g_builtin_syms.undefined_type)) {
        types = cetta_malloc(sizeof(Atom *));
        types[0] = native_handle_type;
        *out_types = types;
        return 1;
    }

    switch (atom->kind) {
    case ATOM_VAR:
        /* Variables have no type → %Undefined% */
        break;
    case ATOM_GROUNDED: {
        Atom *ty = get_grounded_type(a, atom);
        if (!atom_is_symbol_id(ty, g_builtin_syms.undefined_type)) {
            types = cetta_malloc(sizeof(Atom *));
            types[0] = ty;
            count = 1;
        }
        break;
    }
    case ATOM_SYMBOL:
        count = include_direct_annotations
                    ? get_annotated_types(s, a, atom, &types, budget, NULL)
                    : 0;
        if (count && he_types_close_under_subtypes(s))
            count = he_close_type_array(s, a, &types, count, 0u, budget);
        break;
    case ATOM_EXPR:
        if (atom_is_list_form(atom)) {
            types = cetta_malloc(sizeof(Atom *));
            types[0] = atom_list_type(a);
            count = 1;
            break;
        }
        count = include_direct_annotations
                    ? get_annotated_types(s, a, atom, &types, budget, NULL)
                    : 0;
        if (count && he_types_close_under_subtypes(s))
            count = he_close_type_array(s, a, &types, count, 0u, budget);
        /* Also try to infer type from operator's function type */
        bool tried_func_type = false;
        if (count == 0 && atom->expr.len >= 1) {
            Atom *op = atom->expr.elems[0];
            Atom **op_types = NULL;
            uint32_t nop = get_annotated_types(
                s, a, op, &op_types, budget, NULL);
            Arena scratch;
            bool scratch_ready = false;
            /* Also try recursively inferred types for the operator */
            if (nop == 0 && op->kind == ATOM_EXPR) {
                Atom **recur_types = NULL;
                nop = get_atom_types_mode(s, a, op, &recur_types, true,
                                          budget);
                /* Filter: only keep function types */
                op_types = NULL;
                uint32_t nfunc = 0;
                for (uint32_t ri = 0; ri < nop; ri++) {
                    if (!type_inference_step(budget, 1)) break;
                    if (recur_types[ri]->kind == ATOM_EXPR && recur_types[ri]->expr.len >= 2 &&
                        atom_is_symbol_id(recur_types[ri]->expr.elems[0], g_builtin_syms.arrow)) {
                        if (!type_inference_can_add(budget, nfunc)) break;
                        op_types = cetta_realloc(op_types, sizeof(Atom *) * (nfunc + 1));
                        op_types[nfunc++] = recur_types[ri];
                    }
                }
                free(recur_types);
                nop = nfunc;
            }
            for (uint32_t oi = 0; oi < nop; oi++) {
                if (!type_inference_step(budget, 1)) break;
                Atom *ft = op_types[oi];
                /* Check if it's a function type (-> ...) */
                if (ft->kind == ATOM_EXPR && ft->expr.len >= 2 &&
                    atom_is_symbol_id(ft->expr.elems[0], g_builtin_syms.arrow)) {
                    tried_func_type = true;
                    /* Check arity match */
                    if (ft->expr.len - 2 != atom->expr.len - 1) continue;
                    /* A non-function tuple needs no signature-instantiation
                       storage, including the newly admitted nullary case. */
                    if (!scratch_ready) {
                        arena_init(&scratch);
                        arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
                        arena_set_hashcons(&scratch, NULL);
                        scratch_ready = true;
                    }
                    /* Freshen type vars and try to unify args to get concrete ret type */
                    ArenaMark scratch_mark = arena_mark(&scratch);
                    Atom *fresh_ft = cetta_instantiate_frame_syntax(&scratch, ft);
                    Atom *fresh_ret = fresh_ft->expr.elems[fresh_ft->expr.len - 1];
                    Bindings tb;
                    bindings_init(&tb);
                    bool all_ok = true;
                    for (CettaExprIndex ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
                        if (!type_inference_step(budget, 1)) {
                            all_ok = false;
                            break;
                        }
                        /* Apply accumulated bindings to resolve type vars from earlier args */
                        Atom *arg_type_decl =
                            bindings_apply_if_vars(&tb, &scratch, fresh_ft->expr.elems[ai + 1]);
                        if (atom_is_meta_type(arg_type_decl)) {
                            all_ok = atom_meta_type_accepts(a, arg_type_decl,
                                                            atom->expr.elems[ai + 1]);
                            continue;
                        }
                        Atom **atypes = NULL;
                        uint32_t nat = get_atom_types_mode(
                            s, a, atom->expr.elems[ai + 1], &atypes, true,
                            budget);
                        bool found = false;
                        SearchContext trial_context;
                        if (!search_context_init(&trial_context, &tb, &scratch)) {
                            free(atypes);
                            bindings_free(&tb);
                            free(types);
                            free(op_types);
                            arena_free(&scratch);
                            *out_types = NULL;
                            return 0;
                        }
                        for (uint32_t ti = 0; ti < nat; ti++) {
                            if (!type_inference_step(budget, 1)) break;
                            ChoicePoint point = search_context_save(&trial_context);
                            if (match_types_builder(atypes[ti], arg_type_decl,
                                                    search_context_builder(&trial_context),
                                                    a)) {
                                Bindings next_tb;
                                bindings_init(&next_tb);
                                search_context_take(&trial_context, &next_tb);
                                bindings_replace(&tb, &next_tb);
                                found = true;
                                break;
                            }
                            search_context_rollback(&trial_context, point);
                        }
                        search_context_free(&trial_context);
                        free(atypes);
                        if (!found) all_ok = false;
                    }
                    if (all_ok) {
                        /* Apply accumulated type bindings to return type,
                           then normalize arithmetic in type expressions */
                        Atom *concrete_ret = normalize_type_expr(
                            &scratch, bindings_apply_if_vars(&tb, &scratch, fresh_ret));
                        if (!type_inference_can_add(budget, count)) {
                            bindings_free(&tb);
                            arena_reset(&scratch, scratch_mark);
                            break;
                        }
                        if (count >= 1) {
                            types = cetta_realloc(types, sizeof(Atom *) * (count + 1));
                        } else {
                            types = cetta_malloc(sizeof(Atom *));
                        }
                        types[count++] = atom_deep_copy(a, concrete_ret);
                    }
                    bindings_free(&tb);
                    arena_reset(&scratch, scratch_mark);
                }
            }
            if (scratch_ready)
                arena_free(&scratch);
            free(op_types);
            /* If we tried function types but none matched → type error (empty) */
            if (tried_func_type && count == 0 &&
                (!budget || budget->complete)) {
                *out_types = NULL;
                return 0;  /* empty = ill-typed */
            }
        }
        if (count == 0 && atom->expr.len > 0 && !tried_func_type &&
            (!budget || budget->complete)) {
            count = infer_tuple_value_types(s, a, atom, &types, budget);
            if (count && he_types_close_under_subtypes(s))
                count = he_close_type_array(s, a, &types, count, 0u, budget);
        }
        break;
    }

    /* If no types found, return [%Undefined%] */
    if (count == 0 && (!budget || budget->complete)) {
        if (!type_inference_can_add(budget, count)) {
            *out_types = NULL;
            return 0;
        }
        types = cetta_malloc(sizeof(Atom *));
        types[0] = atom_undefined_type(a);
        count = 1;
    }
    *out_types = types;
    return count;
}

/* ── HE typing derivations ─────────────────────────────────────────────── */

/* One typing of an atom as HE's type checker derives it (types.rs). An
 * application error records the failing function's type. */
typedef struct {
    Atom *type;
    bool error;
} HeTyping;

typedef struct {
    HeTyping *items;
    uint32_t *parents; /* supertype closure: the typing each was derived from */
    uint32_t len, cap;
} HeTypings;

#define HE_TYPING_NO_PARENT UINT32_MAX

static bool he_typings_push(HeTypings *list, Atom *type, bool error,
                            uint32_t parent, CettaTypeInferenceBudget *budget) {
    if (!type || !type_inference_can_add(budget, list->len))
        return false;
    if (list->len == list->cap) {
        list->cap = list->cap ? list->cap * 2u : 4u;
        list->items = cetta_realloc(list->items, list->cap * sizeof(*list->items));
        list->parents = cetta_realloc(list->parents,
                                      list->cap * sizeof(*list->parents));
    }
    list->items[list->len] = (HeTyping){type, error};
    list->parents[list->len++] = parent;
    return true;
}

static void he_typings_free(HeTypings *list) {
    free(list->items);
    free(list->parents);
    *list = (HeTypings){0};
}

/* HE's typings of a ground expression, as one inference keeps them. */
struct HeTypingsMemoValue {
    HeTypings typings;
};

static void he_typings_memo_value_free(HeTypingsMemoValue *value) {
    if (!value)
        return;
    he_typings_free(&value->typings);
    free(value);
}

/* `dst` (empty) becomes a copy of `src`; false, and `dst` still empty,
 * where it cannot be allocated. */
static bool he_typings_copy(HeTypings *dst, const HeTypings *src) {
    if (!src->len)
        return true;
    HeTyping *items = malloc(sizeof(*items) * src->len);
    uint32_t *parents = malloc(sizeof(*parents) * src->len);
    if (!items || !parents) {
        free(items);
        free(parents);
        return false;
    }
    memcpy(items, src->items, sizeof(*items) * src->len);
    memcpy(parents, src->parents, sizeof(*parents) * src->len);
    *dst = (HeTypings){items, parents, src->len, src->len};
    return true;
}

static bool he_typings_ground(const HeTypings *list) {
    for (uint32_t i = 0u; i < list->len; i++)
        if (!list->items[i].type || atom_has_vars(list->items[i].type))
            return false;
    return true;
}

static bool he_type_is_function(const Atom *type) {
    return type && type->kind == ATOM_EXPR && type->expr.len > 1u &&
        atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow);
}

/* The types T of the rows (:< type T). */
static uint32_t he_direct_supertypes(Space *s, Arena *a, Atom *type,
                                     Atom ***out) {
    *out = NULL;
    Atom *super = atom_var(a, "%he-supertype");
    Atom *pattern = atom_expr3(a, atom_symbol_id(a, g_builtin_syms.subtype),
                               type, super);
    BindingSet found;
    binding_set_init(&found);
    space_query_conjunction(s, a, &pattern, 1u, NULL, &found);
    uint32_t count = 0u;
    if (found.len) {
        *out = cetta_malloc(sizeof(Atom *) * found.len);
        for (CettaCount i = 0u; i < found.len; i++) {
            Atom *value = bindings_apply_if_vars(&found.items[i], a, super);
            if (value && !(value->kind == ATOM_VAR &&
                           var_id_eq(value->var_id, super->var_id)))
                (*out)[count++] = value;
        }
    }
    binding_set_free(&found);
    return count;
}

/* Whether the program `s` presents declares a subtype: its own rows, the
 * spaces its overlays read through, and its imports. */
static bool he_program_declares_subtypes(const Space *s) {
    if (!s)
        return false;
    if (s->declares_subtypes)
        return true;
    if (!s->overlay_base && s->dep_count == 0u)
        return false;
    for (const Space *at = s->overlay_base; at; at = at->overlay_base)
        if (at->declares_subtypes)
            return true;
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies; d++) {
        const Space *dependency = space_dependency_at(s, d);
        if (dependency && dependency->declares_subtypes)
            return true;
    }
    return false;
}

static bool he_repeats_ancestor(const HeTypings *list, uint32_t index,
                                Atom *type) {
    for (uint32_t at = index; at != HE_TYPING_NO_PARENT; at = list->parents[at])
        if (atom_alpha_eq(list->items[at].type, type))
            return true;
    return false;
}

/* HE's supertype closure from `from` on, in rounds: a round adds each
 * supertype of the previous round's types unless it was known before the
 * round, or repeats one of its own ancestors up to renaming. */
static void he_close_supertypes(Space *s, Arena *a, HeTypings *list,
                                uint32_t from, CettaTypeInferenceBudget *budget) {
    if (list->len == from || !he_program_declares_subtypes(s))
        return;
    uint32_t round_start = from;
    for (;;) {
        Atom **found = NULL;
        uint32_t *found_parent = NULL;
        uint32_t found_len = 0u, found_cap = 0u;
        uint32_t known = list->len;
        for (uint32_t i = round_start; i < known; i++) {
            if (list->items[i].error || !type_inference_step(budget, 1))
                continue;
            Atom **supers = NULL;
            uint32_t n = he_direct_supertypes(s, a, list->items[i].type, &supers);
            for (uint32_t j = 0u; j < n; j++) {
                bool seen = false;
                for (uint32_t k = 0u; k < known && !seen; k++)
                    seen = atom_eq(list->items[k].type, supers[j]);
                if (seen || he_repeats_ancestor(list, i, supers[j]))
                    continue;
                if (found_len == found_cap) {
                    found_cap = found_cap ? found_cap * 2u : 4u;
                    found = cetta_realloc(found, found_cap * sizeof(*found));
                    found_parent = cetta_realloc(found_parent,
                                                 found_cap * sizeof(*found_parent));
                }
                found[found_len] = supers[j];
                found_parent[found_len++] = i;
            }
            free(supers);
        }
        round_start = list->len;
        for (uint32_t j = 0u; j < found_len; j++)
            he_typings_push(list, found[j], false, found_parent[j], budget);
        free(found);
        free(found_parent);
        if (!found_len || list->len == round_start)
            return;
    }
}

/* The declared types of `atom` other than %Undefined%, then their
 * supertypes. */
static void he_declared_typings(Space *s, Arena *a, Atom *atom, HeTypings *out,
                                CettaTypeInferenceBudget *budget) {
    Atom **declared = NULL;
    uint32_t n = get_annotated_types(s, a, atom, &declared, budget, NULL);
    for (uint32_t i = 0u; i < n; i++)
        if (!atom_is_symbol_id(declared[i], g_builtin_syms.undefined_type))
            he_typings_push(out, declared[i], false, HE_TYPING_NO_PARENT, budget);
    free(declared);
}

static void he_atom_typings(Space *s, Arena *a, Atom *atom, HeTypings *out,
                            CettaTypeInferenceBudget *budget);

typedef struct {
    Space *space;
    Arena *arena;
    Atom *call;
    Atom *function;          /* the function type as declared */
    Atom **expected;         /* its parameters, instantiated */
    Atom *result;
    HeTypings *arguments;
    CettaExprLen count;
    HeTypings *out;
    CettaTypeInferenceBudget *budget;
} HeApplication;

/* One application typing per combination of argument typings that meets
 * the parameters; errors where an argument meets none. */
static void he_check_arguments(HeApplication *app, CettaExprLen index,
                               const Bindings *bindings) {
    if (!type_inference_step(app->budget, 1))
        return;
    if (index == app->count) {
        Atom *result = normalize_type_expr(app->arena,
            bindings_apply_if_vars(bindings, app->arena, app->result));
        he_typings_push(app->out, result, false, HE_TYPING_NO_PARENT, app->budget);
        return;
    }
    HeTypings *actual = &app->arguments[index];
    Atom *expected = app->expected[index];
    Atom *argument = app->call->expr.elems[index + 1u];
    bool unchecked = actual->len == 0u ||
        atom_is_symbol_id(expected, g_builtin_syms.undefined_type) ||
        atom_meta_type_accepts(app->arena, expected, argument);
    if (unchecked) {
        he_check_arguments(app, index + 1u, bindings);
        return;
    }
    bool valid = false;
    for (uint32_t i = 0u; i < actual->len && !valid; i++)
        valid = !actual->items[i].error;
    if (!valid) {
        /* Every typing of the argument is an error: those errors. */
        for (uint32_t i = 0u; i < actual->len; i++)
            he_typings_push(app->out, actual->items[i].type, true,
                            HE_TYPING_NO_PARENT, app->budget);
        return;
    }
    Atom *wanted = bindings_apply_if_vars(bindings, app->arena, expected);
    bool matched = false;
    for (uint32_t i = 0u; i < actual->len; i++) {
        if (actual->items[i].error)
            continue;
        Bindings next;
        bindings_init(&next);
        if (bindings_copy(&next, bindings) &&
            match_types(actual->items[i].type, wanted, &next, app->arena)) {
            matched = true;
            he_check_arguments(app, index + 1u, &next);
        }
        bindings_free(&next);
    }
    if (!matched)
        for (uint32_t i = 0u; i < actual->len; i++)
            if (!actual->items[i].error)
                he_typings_push(app->out, app->function, true,
                                HE_TYPING_NO_PARENT, app->budget);
}

static void he_application_typings(Space *s, Arena *a, Atom *call,
                                   Atom *function, HeTypings *arguments,
                                   HeTypings *out,
                                   CettaTypeInferenceBudget *budget) {
    CettaExprLen count = call->expr.len - 1u;
    if (function->expr.len - 2u != count) {
        he_typings_push(out, function, true, HE_TYPING_NO_PARENT, budget);
        return;
    }
    Atom *fresh = atom_has_vars(function)
        ? cetta_instantiate_frame_syntax(a, function) : function;
    HeApplication app = {
        .space = s, .arena = a, .call = call, .function = function,
        .expected = fresh->expr.elems + 1u,
        .result = fresh->expr.elems[fresh->expr.len - 1u],
        .arguments = arguments, .count = count, .out = out, .budget = budget,
    };
    Bindings empty;
    bindings_init(&empty);
    he_check_arguments(&app, 0u, &empty);
    bindings_free(&empty);
}

static void he_atom_typings_derived(Space *s, Arena *a, Atom *atom,
                                    HeTypings *out,
                                    CettaTypeInferenceBudget *budget);

static void he_atom_typings(Space *s, Arena *a, Atom *atom, HeTypings *out,
                            CettaTypeInferenceBudget *budget) {
    bool keyed = out->len == 0u &&
        type_inference_memo_applies(s, a, atom, budget);
    if (keyed) {
        TypeInferenceMemoEntry *known =
            type_inference_memo_find(atom, TYPE_INFERENCE_MEMO_HE_TYPINGS);
        if (known && known->typings &&
            he_typings_copy(out, &known->typings->typings))
            return;
    }
    bool entered = type_inference_memo_enter(s, a, budget);
    he_atom_typings_derived(s, a, atom, out, budget);
    if (keyed && he_typings_ground(out) &&
        !type_inference_memo_find(atom, TYPE_INFERENCE_MEMO_HE_TYPINGS)) {
        HeTypingsMemoValue *value = calloc(1u, sizeof(*value));
        TypeInferenceMemoEntry *entry =
            value && he_typings_copy(&value->typings, out)
            ? type_inference_memo_claim(atom, TYPE_INFERENCE_MEMO_HE_TYPINGS)
            : NULL;
        if (entry)
            entry->typings = value;
        else
            he_typings_memo_value_free(value);
    }
    if (entered)
        type_inference_memo_leave();
}

static void he_atom_typings_derived(Space *s, Arena *a, Atom *atom,
                                    HeTypings *out,
                                    CettaTypeInferenceBudget *budget) {
    if (!atom || !type_inference_step(budget, 1))
        return;
    Atom *native = get_native_handle_type(a, atom);
    if (!atom_is_symbol_id(native, g_builtin_syms.undefined_type)) {
        he_typings_push(out, native, false, HE_TYPING_NO_PARENT, budget);
        return;
    }
    switch (atom->kind) {
    case ATOM_VAR:
        return;
    case ATOM_GROUNDED: {
        Atom *type = get_grounded_type(a, atom);
        if (!atom_is_symbol_id(type, g_builtin_syms.undefined_type))
            he_typings_push(out, atom_has_vars(type)
                ? cetta_instantiate_frame_syntax(a, type) : type,
                false, HE_TYPING_NO_PARENT, budget);
        return;
    }
    case ATOM_SYMBOL: {
        uint32_t start = out->len;
        he_declared_typings(s, a, atom, out, budget);
        he_close_supertypes(s, a, out, start, budget);
        return;
    }
    case ATOM_EXPR:
        break;
    default:
        return;
    }
    if (atom->expr.len == 0u)
        return;
    if (atom_is_list_form(atom)) {
        he_typings_push(out, atom_list_type(a), false, HE_TYPING_NO_PARENT, budget);
        return;
    }
    CettaExprLen len = atom->expr.len;
    HeTypings *parts = cetta_malloc(sizeof(HeTypings) * len);
    memset(parts, 0, sizeof(HeTypings) * len);
    for (CettaExprIndex i = 0u; i < len; i++)
        he_atom_typings(s, a, atom->expr.elems[i], &parts[i], budget);
    /* Tuples: the operator's value types by every argument's types. */
    uint32_t start = out->len;
    uint32_t values = 0u;
    for (uint32_t i = 0u; i < parts[0].len; i++)
        values += !he_type_is_function(parts[0].items[i].type);
    bool complete = values != 0u;
    for (CettaExprIndex i = 1u; i < len && complete; i++)
        complete = parts[i].len != 0u;
    if (complete) {
        uint32_t *index = cetta_malloc(sizeof(uint32_t) * len);
        memset(index, 0, sizeof(uint32_t) * len);
        for (;;) {
            /* The operator's coordinate runs over its value types only. */
            while (index[0] < parts[0].len &&
                   he_type_is_function(parts[0].items[index[0]].type))
                index[0]++;
            if (index[0] >= parts[0].len)
                break;
            Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
            for (CettaExprIndex i = 0u; i < len; i++)
                elems[i] = parts[i].items[index[i]].type;
            if (!he_typings_push(out, atom_expr(a, elems, len), false,
                                 HE_TYPING_NO_PARENT, budget))
                break;
            CettaExprIndex at = len - 1u;
            for (;;) {
                if (++index[at] < parts[at].len || at == 0u)
                    break;
                index[at] = 0u;
                at--;
            }
            if (at == 0u && index[0] >= parts[0].len)
                break;
        }
        free(index);
    }
    he_declared_typings(s, a, atom, out, budget);
    he_close_supertypes(s, a, out, start, budget);
    /* Applications: one per function type of the operator. */
    for (uint32_t i = 0u; i < parts[0].len; i++)
        if (he_type_is_function(parts[0].items[i].type))
            he_application_typings(s, a, atom, parts[0].items[i].type,
                                   parts + 1u, out, budget);
    for (CettaExprIndex i = 0u; i < len; i++)
        he_typings_free(&parts[i]);
    free(parts);
}

/* HE closes the types of a symbol, and an expression's tuple and declared
 * types, under the program's subtypes; `types[from..]` are such types. */
static uint32_t he_close_type_array(Space *s, Arena *a, Atom ***types_io,
                                    uint32_t count, uint32_t from,
                                    CettaTypeInferenceBudget *budget) {
    if (count == from || !he_program_declares_subtypes(s))
        return count;
    HeTypings list = {0};
    for (uint32_t i = 0u; i < count; i++)
        he_typings_push(&list, (*types_io)[i], false, HE_TYPING_NO_PARENT, NULL);
    he_close_supertypes(s, a, &list, from, budget);
    if (list.len != count) {
        *types_io = cetta_realloc(*types_io, sizeof(Atom *) * list.len);
        for (uint32_t i = count; i < list.len; i++)
            (*types_io)[i] = list.items[i].type;
        count = list.len;
    }
    he_typings_free(&list);
    return count;
}

uint32_t space_he_type_answers(Space *s, Arena *a, Atom *atom,
                               Atom ***out_types) {
    *out_types = NULL;
    if (!s || !a || !atom)
        return 0u;
    HeTypings typings = {0};
    he_atom_typings(s, a, atom, &typings, NULL);
    uint32_t count = 0u;
    if (typings.len == 0u) {
        *out_types = cetta_malloc(sizeof(Atom *));
        (*out_types)[count++] = atom_undefined_type(a);
    } else {
        for (uint32_t i = 0u; i < typings.len; i++) {
            if (typings.items[i].error)
                continue;
            if (!*out_types)
                *out_types = cetta_malloc(sizeof(Atom *) * typings.len);
            (*out_types)[count++] = typings.items[i].type;
        }
    }
    he_typings_free(&typings);
    return count;
}

uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types) {
    return get_atom_types_mode(s, a, atom, out_types, true, NULL);
}

uint32_t get_atom_types_budgeted(Space *s, Arena *a, Atom *atom,
                                 Atom ***out_types,
                                 CettaTypeInferenceBudget *budget) {
    return get_atom_types_mode(s, a, atom, out_types, true, budget);
}

uint32_t get_atom_types_structural(Space *s, Arena *a, Atom *atom,
                                   Atom ***out_types) {
    /* Structural checking ignores only an expression subject's own top-level
       annotation. Head and argument inference still uses the shared engine. */
    return get_atom_types_mode(s, a, atom, out_types,
                               !(atom && atom->kind == ATOM_EXPR), NULL);
}

uint32_t get_atom_types_structural_budgeted(
    Space *s, Arena *a, Atom *atom, Atom ***out_types,
    CettaTypeInferenceBudget *budget) {
    return get_atom_types_mode(s, a, atom, out_types,
                               !(atom && atom->kind == ATOM_EXPR), budget);
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

typedef struct QueryResultSink {
    QueryResults *results;
    QueryResultVisitor visitor;
    void *visitor_ctx;
    QueryEquationCandidatePlan candidate_plan;
    void *candidate_plan_ctx;
    CettaCount emitted;
    bool stop;
} QueryResultSink;

static bool query_equation_emit_stored(Space *s, AtomId lhs_id, AtomId rhs_id,
                                       Atom *query,
                                       const QueryVisibleVarSet *visible,
                                       Arena *a, CettaFrameIdentity epoch,
                                       BindingsFrameSchema *frame_schema,
                                       const Bindings *seed,
                                       QueryResultSink *sink);
static bool query_equation_emit_decoded_epoch(Atom *lhs, Atom *rhs,
                                              Atom *query,
                                              const QueryVisibleVarSet *visible,
                                              Arena *a, CettaFrameIdentity epoch,
                                              const Bindings *seed,
                                              QueryResultSink *sink);
static bool query_equation_emit_decoded_plan_epoch(
    Atom *lhs, Atom *rhs,
    BindingsFrameSchema *frame_schema, const CettaOpenPatternPlan *match_plan,
    Atom *query, const QueryVisibleVarSet *visible,
    Arena *a, CettaFrameIdentity epoch, const Bindings *seed, QueryResultSink *sink);
static bool query_equation_emit_candidate_plan_epoch(
    Atom *equation, const QueryEquationPlan *plan,
    Atom *query, const QueryVisibleVarSet *visible,
    Arena *a, CettaFrameIdentity epoch, const Bindings *seed,
    QueryResultSink *sink);

static void query_result_sink_init_collect(QueryResultSink *sink,
                                           QueryResults *results) {
    sink->results = results;
    sink->visitor = NULL;
    sink->visitor_ctx = NULL;
    sink->candidate_plan = NULL;
    sink->candidate_plan_ctx = NULL;
    sink->emitted = 0;
    sink->stop = false;
}

static void query_result_sink_init_visit(QueryResultSink *sink,
                                         QueryResultVisitor visitor,
                                         void *ctx) {
    sink->results = NULL;
    sink->visitor = visitor;
    sink->visitor_ctx = ctx;
    sink->candidate_plan = NULL;
    sink->candidate_plan_ctx = NULL;
    sink->emitted = 0;
    sink->stop = false;
}

static bool query_result_sink_candidate_plan(
        const QueryResultSink *sink, Atom *equation, QueryEquationPlan *plan) {
    *plan = (QueryEquationPlan){0};
    if (!sink || !sink->candidate_plan || !equation)
        return true;
    if (!sink->candidate_plan(equation, sink->candidate_plan_ctx, plan))
        return false;
    /* A partial syntax override retains only its independent frame schema. */
    if (!plan->match_pattern || !plan->result_template) {
        BindingsFrameSchema *schema = plan->frame_schema;
        *plan = (QueryEquationPlan){.frame_schema = schema};
    }
    return true;
}

static bool query_result_sink_emit(QueryResultSink *sink, Atom *result,
                                   Bindings *bindings) {
    if (!sink || sink->stop)
        return false;
    if (sink->results) {
        if (!query_results_push_move(sink->results, result, bindings))
            return false;
        sink->emitted++;
        return true;
    }
    if (!sink->visitor)
        return false;
    sink->emitted++;
    if (!sink->visitor(result, bindings, sink->visitor_ctx)) {
        sink->stop = true;
        return true;
    }
    return true;
}

static Atom *query_bucket_equation_at(
    Space *space, const EqBucket *bucket, CettaIndex position) {
    if (!space || !bucket || position >= bucket->len)
        return NULL;
    AtomId equation_id = space_indexed_occurrence_atom_id(
        space, bucket->atom_indices, bucket->atom_ids, position);
    if (equation_id != CETTA_ATOM_ID_NONE &&
        space->native.universe) {
        return term_universe_get_atom(
            space->native.universe, equation_id);
    }
    /* Indexes hold rows; while rows are marked, a row is not a position. */
    if (space_has_marks(space))
        return NULL;
    CettaIndex logical_index = bucket->atom_indices[position];
    return logical_index < space_length64(space)
        ? space_get_at64(space, logical_index) : NULL;
}

/* A module space answers its own equations, across the exact-head and
 * wildcard buckets together, before those of the modules it imports.  The
 * equation query therefore visits both buckets once per pass: pass 0 the
 * space's own entries, pass 1 the imported ones; pass -1 visits every entry
 * of a space without imported atoms.  An entry without a logical index is
 * the space's own. */
static inline bool eq_bucket_entry_in_pass(const Space *s,
                                           const EqBucket *bucket,
                                           CettaIndex position, int pass) {
    if (pass < 0)
        return true;
    if (!bucket->atom_indices)
        return pass == 0;
    return (bucket->atom_indices[position] < s->imported_len) == (pass == 1);
}

static void query_bucket_legacy(Space *s, EqBucket *bucket, Atom *query,
                                const QueryVisibleVarSet *visible, Arena *a,
                                QueryResultSink *sink, int pass) {
    SymbolId query_head = eq_head_symbol(query);
    if (!sink || sink->stop)
        return;
    if (bucket->trie && bucket->len > 4) {
        CettaIndex *candidates = NULL;
        CettaIndex ncand = 0, ccand = 0;
        CettaIndex considered = 0;
        (void)considered;
        disc_lookup(bucket->trie, query, &candidates, &ncand, &ccand);
        for (CettaIndex ci = 0; ci < ncand; ci++) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            CettaIndex i = candidates[ci];
            if (i >= bucket->len) continue;
            if (!eq_bucket_entry_in_pass(s, bucket, i, pass)) continue;
            QueryEquationPlan planned = {0};
            Atom *equation = NULL;
            if (sink->candidate_plan) {
                equation = query_bucket_equation_at(s, bucket, i);
                if (!query_result_sink_candidate_plan(
                        sink, equation, &planned)) {
                    continue;
                }
            }
            AtomId equation_id = space_indexed_occurrence_atom_id(
                s, bucket->atom_indices, bucket->atom_ids, i);
            if (planned.match_pattern && planned.result_template) {
                CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
                SymbolId lhs_head = eq_head_symbol(planned.match_pattern);
                if (query_head != SYMBOL_ID_NONE &&
                    lhs_head != SYMBOL_ID_NONE &&
                    lhs_head != query_head) {
                    continue;
                }
                considered++;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
                (void)query_equation_emit_candidate_plan_epoch(
                    equation, &planned,
                    query, visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
                if (sink->stop)
                    break;
                continue;
            }
            AtomId lhs_id = CETTA_ATOM_ID_NONE;
            AtomId rhs_id = CETTA_ATOM_ID_NONE;
            if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
                CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
                SymbolId lhs_head = eq_head_symbol_id(s, lhs_id);
                if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                    lhs_head != query_head) {
                    continue;
                }
                considered++;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
                (void)query_equation_emit_stored(
                    s, lhs_id, rhs_id, query, visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope),
                    planned.frame_schema, NULL, sink);
                if (sink->stop)
                    break;
                continue;
            }
            Atom *lhs = NULL, *rhs = NULL;
            if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
                continue;
            SymbolId lhs_head = eq_head_symbol(lhs);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            (void)query_equation_emit_decoded_plan_epoch(
        lhs, rhs,
        planned.frame_schema, NULL,
        query, visible,
        a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
            if (sink->stop)
                break;
        }
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                                considered);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
        free(candidates);
        return;
    }

    uint32_t considered = 0;
    (void)considered;
    for (CettaIndex i = 0; i < bucket->len; i++) {
        if (!eq_bucket_entry_in_pass(s, bucket, i, pass)) continue;
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        QueryEquationPlan planned = {0};
        Atom *equation = NULL;
        if (sink->candidate_plan) {
            equation = query_bucket_equation_at(s, bucket, i);
            if (!query_result_sink_candidate_plan(
                    sink, equation, &planned)) {
                continue;
            }
        }
        AtomId equation_id = space_indexed_occurrence_atom_id(
            s, bucket->atom_indices, bucket->atom_ids, i);
        if (planned.match_pattern && planned.result_template) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            SymbolId lhs_head = eq_head_symbol(planned.match_pattern);
            if (query_head != SYMBOL_ID_NONE &&
                lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            (void)query_equation_emit_candidate_plan_epoch(
                equation, &planned,
                query, visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
            if (sink->stop)
                break;
            continue;
        }
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            SymbolId lhs_head = eq_head_symbol_id(s, lhs_id);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            (void)query_equation_emit_stored(
                s, lhs_id, rhs_id, query, visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope),
                planned.frame_schema, NULL, sink);
            if (sink->stop)
                break;
            continue;
        }
        Atom *lhs = NULL, *rhs = NULL;
        if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
            continue;
        SymbolId lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
        (void)query_equation_emit_decoded_plan_epoch(
        lhs, rhs,
        planned.frame_schema, NULL,
        query, visible,
        a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
        if (sink->stop)
            break;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
}

static bool query_equation_emit_stored(Space *s, AtomId lhs_id, AtomId rhs_id,
                                       Atom *query,
                                       const QueryVisibleVarSet *visible,
                                       Arena *a, CettaFrameIdentity epoch,
                                       BindingsFrameSchema *frame_schema,
                                       const Bindings *seed,
                                       QueryResultSink *sink) {
    if (!s || !s->native.universe || lhs_id == CETTA_ATOM_ID_NONE ||
        rhs_id == CETTA_ATOM_ID_NONE || !tu_hdr(s->native.universe, lhs_id) ||
        !tu_hdr(s->native.universe, rhs_id) || !sink) {
        return false;
    }
    Bindings merged;
    bindings_init(&merged);
    if (seed && !bindings_try_merge_live(&merged, seed)) {
        bindings_free(&merged);
        return false;
    }
    if (frame_schema &&
        !bindings_register_complete_frame_schema(
            &merged, frame_schema, epoch)) {
        bindings_free(&merged);
        return false;
    }
    bool emitted = false;
    if (match_atoms_atom_id_epoch(query, s->native.universe, lhs_id,
                                  &merged, a, epoch)) {
        CettaSurvivorAllocationScope allocation_scope =
            cetta_survivor_allocation_scope_enter(
                CETTA_SURVIVOR_ALLOC_ROLE_EQUATION_RESULT_INSTANTIATION);
        Atom *rhs_copy = tu_has_vars(s->native.universe, rhs_id)
            ? term_universe_copy_atom_epoch(s->native.universe, a, rhs_id, epoch)
            : term_universe_copy_atom(s->native.universe, a, rhs_id);
        Atom *result =
            rhs_copy ? bindings_apply_if_vars(&merged, a, rhs_copy) : NULL;
        if (result)
            result = rewrite_query_visible_aliases(a, result, visible, &merged);
        cetta_survivor_allocation_scope_leave(allocation_scope);
        if (result) {
            Bindings projected;
            if (project_query_visible_bindings(
                    a, visible, &merged, &projected)) {
                emitted = query_result_sink_emit(sink, result, &projected);
                bindings_free(&projected);
            }
        }
    }
    bindings_free(&merged);
    return emitted;
}

static bool query_equation_match_decoded_epoch(
    Atom *lhs, Atom *query, const CettaOpenPatternPlan *plan,
    Arena *a, CettaFrameIdentity epoch, Bindings *merged) {
    if (!lhs || !query || !a || !merged)
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_EQ_DECODED);
    /* Leaf-patch view (OFF by default): for a flat linear pattern with
     * non-variable query args, the positional bind reproduces the matcher's
     * bindings without the worklist; it pre-checks and refuses anything else,
     * falling back to the general matcher on clean state.  OFF-vs-ON is proven
     * byte-identical before any default flip. */
    bool leaf_matched = match_leaf_patch_view_enabled() &&
                        match_atoms_epoch_positional_linear(query, lhs, merged,
                                                            a, epoch);
    if (leaf_matched)
        return true;
    BindingsBuilder match_builder;
    bindings_builder_init_owned(&match_builder, merged);
    bool matched = match_atoms_epoch_builder_planned(
        query, lhs, plan, &match_builder, a, epoch);
    bindings_builder_take(&match_builder, merged);
    return matched;
}

static bool query_equation_emit_decoded_plan_epoch(
    Atom *lhs, Atom *rhs,
    BindingsFrameSchema *frame_schema, const CettaOpenPatternPlan *match_plan,
    Atom *query, const QueryVisibleVarSet *visible,
    Arena *a, CettaFrameIdentity epoch, const Bindings *seed, QueryResultSink *sink) {
    if (!lhs || !rhs || !query || !a || !sink)
        return false;
    Bindings merged;
    bindings_init(&merged);
    if (seed && !bindings_try_merge_live(&merged, seed)) {
        bindings_free(&merged);
        return false;
    }
    if (frame_schema &&
        !bindings_register_complete_frame_schema(
            &merged, frame_schema, epoch)) {
        bindings_free(&merged);
        return false;
    }
    bool emitted = false;
    bool matched = query_equation_match_decoded_epoch(
        lhs, query, match_plan, a, epoch, &merged);
    if (matched) {
        CettaSurvivorAllocationScope allocation_scope =
            cetta_survivor_allocation_scope_enter(
                CETTA_SURVIVOR_ALLOC_ROLE_EQUATION_RESULT_INSTANTIATION);
        Atom *result = bindings_apply_epoch(&merged, a, rhs, epoch);
        result = rewrite_query_visible_aliases(a, result, visible, &merged);
        cetta_survivor_allocation_scope_leave(allocation_scope);
        Bindings projected;
        if (project_query_visible_bindings(
                a, visible, &merged, &projected)) {
            emitted = query_result_sink_emit(sink, result, &projected);
            bindings_free(&projected);
        }
    }
    bindings_free(&merged);
    return emitted;
}

static bool query_equation_emit_decoded_epoch(Atom *lhs, Atom *rhs,
                                              Atom *query,
                                              const QueryVisibleVarSet *visible,
                                              Arena *a, CettaFrameIdentity epoch,
                                              const Bindings *seed,
                                              QueryResultSink *sink) {
    return query_equation_emit_decoded_plan_epoch(
        lhs, rhs,
        NULL, NULL,
        query, visible,
        a, epoch, seed, sink);
}

static bool query_equation_emit_candidate_plan_epoch(
        Atom *equation, const QueryEquationPlan *plan,
        Atom *query, const QueryVisibleVarSet *visible,
        Arena *a, CettaFrameIdentity epoch, const Bindings *seed,
        QueryResultSink *sink) {
    if (!equation || !plan || !plan->match_pattern || !plan->result_template)
        return false;
    return query_equation_emit_decoded_plan_epoch(
        plan->match_pattern, plan->result_template,
        plan->frame_schema, plan->match_plan,
        query, visible,
        a, epoch, seed, sink);
}

/* Try matching equations from a bucket against a query.
   Large buckets use the substitution tree only to select candidates.  A leaf's
   epoch belongs to the persistent index build; it must never enter a later
   query environment.  Each candidate is therefore standardized apart with a
   fresh query-time epoch and rechecked by the authoritative matcher. */
static void query_bucket(Space *s, EqBucket *bucket, Atom *query,
                         const QueryVisibleVarSet *visible, Arena *a,
                         QueryResultSink *sink, int pass) {
    SymbolId query_head = eq_head_symbol(query);
    CettaCount emitted_before;
    bool head_bucket_mismatch;
    if (!bucket || bucket->len == 0 || !sink || sink->stop)
        return;
    emitted_before = sink->emitted;
    head_bucket_mismatch =
        query_head != SYMBOL_ID_NONE &&
        (bucket->mixed_heads || bucket->head != query_head);
    if (head_bucket_mismatch || bucket->subst.count <= 4 || !bucket->subst.root ||
        !bucket->subst_safe || !atom_is_eq_subst_safe(query)) {
        query_bucket_legacy(s, bucket, query, visible, a, sink, pass);
        return;
    }
    SubstMatchSet matches;
    uint32_t considered = 0;
    (void)considered;
    smset_init(&matches);
    stree_query_bucket(&bucket->subst, a, query, NULL, &matches);
    for (CettaIndex mi = 0; mi < matches.len; mi++) {
        CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
        const SubstMatch *sm = &matches.items[mi];
        if (sm->atom_idx >= bucket->len)
            continue;
        if (!eq_bucket_entry_in_pass(s, bucket, sm->atom_idx, pass))
            continue;
        QueryEquationPlan planned = {0};
        Atom *equation = NULL;
        if (sink->candidate_plan) {
            equation = query_bucket_equation_at(s, bucket, sm->atom_idx);
            if (!query_result_sink_candidate_plan(
                    sink, equation, &planned)) {
                continue;
            }
        }
        AtomId equation_id = space_indexed_occurrence_atom_id(
            s, bucket->atom_indices, bucket->atom_ids, sm->atom_idx);
        if (planned.match_pattern && planned.result_template) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            SymbolId lhs_head = eq_head_symbol(planned.match_pattern);
            if (query_head != SYMBOL_ID_NONE &&
                lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            if (query_equation_emit_candidate_plan_epoch(
                    equation, &planned,
                    query, visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_EMITTED);
            }
            if (sink->stop)
                break;
            continue;
        }
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            SymbolId lhs_head = eq_head_symbol_id(s, lhs_id);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            if (query_equation_emit_stored(s, lhs_id, rhs_id, query, visible, a,
                                           cetta_frame_identity_scope_fresh(&frame_identity_scope),
                                           planned.frame_schema, NULL, sink)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_EMITTED);
            }
            if (sink->stop)
                break;
            continue;
        }
        Atom *lhs = NULL, *rhs = NULL;
        if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
            continue;
        SymbolId lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
        bool emitted = query_equation_emit_decoded_plan_epoch(
        lhs, rhs,
        planned.frame_schema, NULL,
        query, visible,
        a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
        if (emitted) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_EMITTED);
        }
        if (!emitted) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_CANDIDATE_FALLBACK);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
            (void)query_equation_emit_decoded_plan_epoch(
        lhs, rhs,
        planned.frame_schema, NULL,
        query, visible,
        a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
        }
        if (sink->stop)
            break;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_CANDIDATES, considered);
    smset_free(&matches);
    if (!sink->stop && sink->emitted == emitted_before) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_BUCKET_FALLBACK);
        query_bucket_legacy(s, bucket, query, visible, a, sink, pass);
    }
}

static CettaCount query_equations_core_overlay(Space *s, Atom *query, Arena *a,
                                               QueryResultSink *sink,
                                               bool own_only) {
    QueryVisibleVarSet visible;
    SymbolId query_head = eq_head_symbol(query);
    CettaCount logical_len;
    CettaCount considered = 0;
    (void)considered;

    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return 0;
    }

    logical_len = space_length64(s);
    /* Own equations first, then imported ones (see eq_bucket_entry_in_pass). */
    CettaIndex imported = space_imported_length(s);
    if (imported > logical_len)
        imported = 0u;
    int passes = own_only || imported == 0u ? 1 : 2;
    for (int pass = 0; pass < passes && !sink->stop; pass++)
    for (CettaIndex i = pass == 0 ? imported : 0u;
         i < (pass == 0 ? logical_len : imported) && !sink->stop;
         i++) {
        Atom *equation = space_get_at64(s, i);
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        SymbolId lhs_head;
        if (!equation || !is_equation_atom(equation, &lhs, &rhs))
            continue;
        QueryEquationPlan planned = {0};
        if (!query_result_sink_candidate_plan(
                sink, equation, &planned)) {
            continue;
        }
        if (planned.match_pattern && planned.result_template) {
            lhs = planned.match_pattern;
            rhs = planned.result_template;
        }
        lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_DECISION_EXACT_ATTEMPT);
        if (planned.match_pattern && planned.result_template) {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            (void)query_equation_emit_candidate_plan_epoch(
                equation, &planned,
                query, &visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
        } else {
            CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
            (void)query_equation_emit_decoded_plan_epoch(
        lhs, rhs,
        planned.frame_schema, NULL,
        query, &visible,
        a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, sink);
        }
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
    query_visible_var_set_free(&visible);
    return sink->emitted;
}

/* One member of an equation query: the space's own equations, then, unless
 * `own_only`, its imported ones. */
static inline __attribute__((always_inline)) void
query_equations_member(Space *s, Atom *query, Arena *a,
                       QueryResultSink *sink, bool own_only) {
    if (space_has_overlay_base(s)) {
        (void)query_equations_core_overlay(s, query, a, sink, own_only);
        return;
    }
    ensure_eq_index(s);
    QueryVisibleVarSet visible;
    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return;
    }
    /* Use head-symbol index for O(1) lookup instead of O(N) scan.
       This is the key optimization from Vampire's LiteralIndex. */
    SymbolId head = eq_head_symbol(query);
    bool imported = s->imported_len != 0u;
    int first_pass = imported ? 0 : -1;
    int end_pass = !imported ? 0 : own_only ? 1 : 2;
    for (int pass = first_pass; pass < end_pass && !sink->stop; pass++) {
        if (head != SYMBOL_ID_NONE) {
            /* Query has a known head symbol — look up matching bucket */
            query_bucket(s, &s->native.eq_idx.buckets[symbol_hash(head)],
                         query, &visible, a, sink, pass);
        }
        /* Non-symbol-headed queries may still match wildcard equations whose
           LHS head is itself a variable or complex term, but they must not
           unlock every named equation bucket by unifying the head variable
           with an unrelated function symbol. HE treats ($f x) as data unless
           a wildcard equation explicitly matches it. */
        if (!sink->stop)
            query_bucket(s, &s->native.eq_idx.wildcard, query, &visible, a,
                         sink, pass);
    }
    query_visible_var_set_free(&visible);
}

/* A module space answers its own equations, then each imported module's
 * own, in import order (HE's ModuleSpace query). */
static __attribute__((noinline)) void query_equations_dependencies(
        Space *s, Atom *query, Arena *a, QueryResultSink *sink) {
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies && !sink->stop; d++)
        query_equations_member(space_dependency_at(s, d), query, a, sink,
                               true);
}

static CettaCount query_equations_core(Space *s, Atom *query, Arena *a,
                                       QueryResultSink *sink) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS);
    query_equations_member(s, query, a, sink, false);
    if (!sink->stop && space_has_dependencies(s))
        query_equations_dependencies(s, query, a, sink);
    return sink->emitted;
}

CettaCount query_equations_visit(Space *s, Atom *query, Arena *a,
                                 QueryResultVisitor visitor, void *ctx) {
    QueryResultSink sink;
    query_result_sink_init_visit(&sink, visitor, ctx);
    return query_equations_core(s, query, a, &sink);
}

CettaCount query_equations_visit_planned(
    Space *s, Atom *query, Arena *a,
    QueryEquationCandidatePlan plan, void *plan_ctx,
    QueryResultVisitor visitor, void *visitor_ctx) {
    QueryResultSink sink;
    query_result_sink_init_visit(&sink, visitor, visitor_ctx);
    sink.candidate_plan = plan;
    sink.candidate_plan_ctx = plan_ctx;
    return query_equations_core(s, query, a, &sink);
}

/* MAM loop-body view entry guard -- the necessary (not yet sufficient)
 * condition for the deterministic-tail loop lane: the head resolves to EXACTLY
 * ONE equation in a clean single-head bucket, and the space has no overlay base
 * to complicate revision/visibility.  Cheap: one hashed bucket read, no match.
 * Conservative by construction -- any ambiguity (mixed bucket, overlay, zero or
 * many equations) returns false and the general evaluator handles the call.
 * Body-determinism (single-branch, no superpose tail) is verified later when
 * the view is compiled; this predicate only gates measurement + view lookup. */
/* Every ATOM_VAR in the pattern occurs at most once.  A repeated variable is
 * an equality constraint (both occurrences must bind equal terms) that a naive
 * positional leaf-patch cannot honour -- it reads the two leaves independently
 * and would accept terms the real matcher rejects.  Patterns are tiny, so the
 * O(vars^2) dedup is negligible; if a pattern somehow exceeds the small cap we
 * conservatively report non-linear (refuse), never a false linear. */
static bool pattern_vars_unique_rec(const Atom *a, VarId *seen, uint32_t *n,
                                    uint32_t cap) {
    if (!a)
        return true;
    if (a->kind == ATOM_VAR) {
        for (uint32_t i = 0; i < *n; i++)
            if (var_id_eq(seen[i], a->var_id))
                return false; /* repeated variable -> non-linear */
        if (*n >= cap)
            return false; /* too many vars to verify -> conservative refuse */
        seen[(*n)++] = a->var_id;
        return true;
    }
    if (a->kind == ATOM_EXPR) {
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            if (!pattern_vars_unique_rec(a->expr.elems[i], seen, n, cap))
                return false;
    }
    return true;
}

/* Resolve the single, linear equation a head is eligible for (or NULL).  Shared
 * by the eligibility predicate and the revision-keyed view cache.  Cheap: one
 * hashed bucket read + a tiny linearity walk; the cache calls it once per
 * (head, revision), not per reduction. */
static inline __attribute__((always_inline))
bool space_dependencies_may_match_known_head(const Space *s,
                                                    SymbolId head);

static Atom *space_single_linear_equation_at(Space *s, SymbolId head,
                                             CettaIndex *logical_index) {
    if (logical_index)
        *logical_index = 0u;
    if (!s || head == SYMBOL_ID_NONE || space_has_overlay_base(s))
        return NULL;
    /* An imported module's equation for the head adds a branch. */
    if (space_dependencies_may_match_known_head(s, head))
        return NULL;
    ensure_eq_index(s);
    if (s->native.eq_idx_dirty)
        return NULL;
    /* A genuinely variable-headed equation can add a branch to this named
     * call.  Structured callable heads are disjoint and do not invalidate the
     * singleton view. */
    for (CettaIndex position = 0u;
         position < s->native.eq_idx.wildcard.len; position++) {
        Atom *wildcard_equation = space_indexed_occurrence_atom(
            s, s->native.eq_idx.wildcard.atom_indices,
            s->native.eq_idx.wildcard.atom_ids, position);
        Atom *wildcard_lhs = NULL;
        Atom *wildcard_rhs = NULL;
        if (wildcard_equation &&
            is_equation_atom(
                wildcard_equation, &wildcard_lhs, &wildcard_rhs)) {
            if (eq_lhs_may_match_known_head(wildcard_lhs, head)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_SINGLETON_OPEN_BLOCKED);
                return NULL;
            }
#if CETTA_BUILD_WITH_RUNTIME_STATS || defined(CETTA_RUNTIME_STATS_IMPL)
            if (eq_lhs_has_structured_head(wildcard_lhs)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_SINGLETON_STRUCTURED_DISJOINT);
            }
#endif
        }
    }
    /* The head's own rows, whichever heads share its hash bucket. */
    const SpaceHeadChains *rows = &s->native.eq_idx.head_rows;
    const SpaceHeadChain *chain = space_head_chains_find(rows, head);
    if (!chain || chain->count != 1u)
        return NULL;
    CettaIndex idx = rows->atom_indices[chain->first];
    Atom *equation = space_indexed_occurrence_atom(
        s, rows->atom_indices, rows->atom_ids, chain->first);
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs))
        return NULL;
    /* Linearity is required: a repeated LHS variable is an
     * equality constraint the positional leaf-patch cannot honour. */
    VarId seen[64];
    uint32_t nseen = 0;
    if (!pattern_vars_unique_rec(lhs, seen, &nseen, 64u))
        return NULL;
    if (logical_index)
        *logical_index = idx;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_SPACE_KNOWN_HEAD_SINGLETON_ADMITTED);
    return equation;
}

Atom *space_single_linear_equation(Space *s, SymbolId head) {
    return space_single_linear_equation_at(s, head, NULL);
}

bool space_head_has_single_equation(Space *s, SymbolId head) {
    return space_single_linear_equation(s, head) != NULL;
}

static bool prepared_rhs_is_range_restricted(
    const Atom *atom, const SpacePreparedEquation *plan) {
    if (!atom || !plan)
        return false;
    if (!atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR) {
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            if (var_id_eq(plan->registers[i], atom->var_id))
                return true;
        return false;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
        if (!prepared_rhs_is_range_restricted(atom->expr.elems[i], plan))
            return false;
    return true;
}

static bool prepared_register_head_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out,
    CettaGsltRegisterInstruction *instruction_out) {
    /* A register instruction realizes an operation the current dialect
     * defines; one it leaves undefined is data. */
    CettaGsltRegisterResultKind kind;
    CettaGsltRegisterInstruction instruction;
    if (!cetta_gslt_register_head_program(
            head, arity, &kind, &instruction, &g_builtin_syms) ||
        (grounded_op_is_cetta_only(head) && !is_grounded_op(head)))
        return false;
    if (kind_out)
        *kind_out = kind;
    if (instruction_out)
        *instruction_out = instruction;
    return true;
}

static bool prepared_register_head_kind(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out) {
    return prepared_register_head_program(
        head, arity, kind_out, NULL);
}

static bool prepared_register_is_register(
    const SpacePreparedEquation *plan, const Atom *atom) {
    if (!plan || !atom || atom->kind != ATOM_VAR)
        return false;
    for (CettaExprIndex i = 0u; i < plan->arity; i++)
        if (var_id_eq(plan->registers[i], atom->var_id))
            return true;
    return false;
}

static bool prepared_register_template_kind(
    const SpacePreparedEquation *plan, const Atom *atom, uint32_t depth,
    CettaGsltRegisterResultKind *kind_out) {
    if (!plan || !atom || !kind_out || depth > 64u)
        return false;
    if (prepared_register_is_register(plan, atom) ||
        (atom->kind == ATOM_GROUNDED &&
         (atom->ground.gkind == GV_INT ||
          atom->ground.gkind == GV_BIGINT))) {
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return false;
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_register_head_kind(
            head->sym_id, atom->expr.len - 1u, kind_out)) {
        return false;
    }
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        CettaGsltRegisterResultKind child_kind;
        if (!prepared_register_template_kind(
                plan, atom->expr.elems[i], depth + 1u, &child_kind) ||
            child_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
    }
    return true;
}

static bool prepared_register_exact_integer(const Atom *atom);

/* The recursive register fragment is an exact, pure expression grammar:
 * input registers and exact integer literals; generated register heads;
 * lazy `if`; and calls back to the same revision-pinned singleton equation.
 * The native evaluator below consumes precisely this proof bit and no wider
 * syntax. */
static bool prepared_register_recursive_template_kind(
    const SpacePreparedEquation *plan, const Atom *atom, uint32_t depth,
    CettaGsltRegisterResultKind *kind_out, bool *saw_self_call) {
    if (!plan || !atom || !kind_out || !saw_self_call || depth > 64u)
        return false;
    if (prepared_register_is_register(plan, atom) ||
        prepared_register_exact_integer(atom)) {
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
        !atom->expr.elems[0] ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    SymbolId head = atom->expr.elems[0]->sym_id;
    CettaExprLen nargs = atom->expr.len - 1u;
    if (head == g_builtin_syms.if_text) {
        if (nargs != 3u)
            return false;
        CettaGsltRegisterResultKind condition_kind;
        CettaGsltRegisterResultKind true_kind;
        CettaGsltRegisterResultKind false_kind;
        if (!prepared_register_recursive_template_kind(
                plan, atom->expr.elems[1], depth + 1u,
                &condition_kind, saw_self_call) ||
            condition_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
            !prepared_register_recursive_template_kind(
                plan, atom->expr.elems[2], depth + 1u,
                &true_kind, saw_self_call) ||
            !prepared_register_recursive_template_kind(
                plan, atom->expr.elems[3], depth + 1u,
                &false_kind, saw_self_call) ||
            true_kind != false_kind ||
            true_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
        *kind_out = true_kind;
        return true;
    }
    if (head == plan->head) {
        if (nargs != plan->arity)
            return false;
        for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
            CettaGsltRegisterResultKind argument_kind;
            if (!prepared_register_recursive_template_kind(
                    plan, atom->expr.elems[i], depth + 1u,
                    &argument_kind, saw_self_call) ||
                argument_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
        }
        *saw_self_call = true;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    }
    CettaGsltRegisterResultKind result_kind;
    if (!prepared_register_head_kind(head, nargs, &result_kind))
        return false;
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        CettaGsltRegisterResultKind argument_kind;
        if (!prepared_register_recursive_template_kind(
                plan, atom->expr.elems[i], depth + 1u,
                &argument_kind, saw_self_call) ||
            argument_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
    }
    *kind_out = result_kind;
    return true;
}

static bool prepared_register_tail_shape(
    const SpacePreparedEquation *plan, Atom *atom) {
    if (!plan || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(atom->expr.elems[0], plan->head)) {
        return false;
    }
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        CettaGsltRegisterResultKind kind;
        if (!prepared_register_template_kind(
                plan, atom->expr.elems[i], 0u, &kind) ||
            kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
    }
    return true;
}

static bool prepared_register_compile_guarded_step(
    SpacePreparedEquation *plan) {
    if (!plan || !plan->rhs || plan->rhs->kind != ATOM_EXPR ||
        plan->rhs->expr.len != 4u ||
        !atom_is_symbol_id(plan->rhs->expr.elems[0],
                           g_builtin_syms.if_text)) {
        return false;
    }
    Atom *guard = plan->rhs->expr.elems[1];
    Atom *when_true = plan->rhs->expr.elems[2];
    Atom *when_false = plan->rhs->expr.elems[3];
    CettaGsltRegisterResultKind guard_kind;
    if (!prepared_register_template_kind(
            plan, guard, 0u, &guard_kind) ||
        guard_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        return false;
    }
    bool true_is_tail = prepared_register_tail_shape(plan, when_true);
    bool false_is_tail = prepared_register_tail_shape(plan, when_false);
    if (true_is_tail == false_is_tail)
        return false;
    Atom *base = true_is_tail ? when_false : when_true;
    CettaGsltRegisterResultKind base_kind;
    if (!prepared_register_template_kind(
            plan, base, 0u, &base_kind) ||
        base_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        return false;
    }
    plan->register_guard = guard;
    plan->register_base = base;
    plan->register_tail = true_is_tail ? when_true : when_false;
    plan->register_base_when_true = !true_is_tail;
    plan->evidence |= CETTA_GSLT_EVIDENCE_REGISTER_GUARD |
                      CETTA_GSLT_EVIDENCE_REGISTER_BASE |
                      CETTA_GSLT_EVIDENCE_REGISTER_TAIL;
    return true;
}

/* Whether one member of a composed read declares `head` with an arrow type
 * of the call arity: the space's declarations, or with `own_only` those that
 * are not imported atoms. */
static bool space_member_has_arrow_signature(Space *s, SymbolId head,
                                             CettaExprLen arity,
                                             bool own_only) {
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = own_only ? space_imported_length(s) : 0u;
             i < logical_len; i++) {
            Atom *annotation = space_get_at64(s, i);
            if (!annotation || annotation->kind != ATOM_EXPR ||
                annotation->expr.len != 3u ||
                !atom_is_symbol_id(annotation->expr.elems[0],
                                   g_builtin_syms.colon) ||
                !atom_is_symbol_id(annotation->expr.elems[1], head)) {
                continue;
            }
            Atom *type = annotation->expr.elems[2];
            if (type && type->kind == ATOM_EXPR &&
                type->expr.len == (CettaExprLen)(arity + 2u) &&
                atom_is_symbol_id(type->expr.elems[0],
                                  g_builtin_syms.arrow)) {
                return true;
            }
        }
    } else {
        ensure_ty_ann_index(s);
        const SpaceHeadChains *index = &s->native.ty_idx.rows;
        const SpaceHeadChain *chain = space_head_chains_find(index, head);
        for (uint32_t i = chain ? chain->first : SPACE_HEAD_CHAIN_END;
             i != SPACE_HEAD_CHAIN_END; i = index->next[i]) {
            if (own_only && index->atom_indices[i] < s->imported_len)
                continue;
            AtomId annotation_id = space_indexed_occurrence_atom_id(
                s, index->atom_indices, index->atom_ids, i);
            AtomId subject_id = CETTA_ATOM_ID_NONE;
            AtomId type_id = CETTA_ATOM_ID_NONE;
            if (space_type_annotation_child_ids_at_id(
                    s, annotation_id, &subject_id, &type_id) &&
                tu_hdr(s->native.universe, subject_id) &&
                tu_kind(s->native.universe, subject_id) == ATOM_SYMBOL &&
                tu_sym(s->native.universe, subject_id) == head &&
                tu_hdr(s->native.universe, type_id) &&
                tu_kind(s->native.universe, type_id) == ATOM_EXPR &&
                tu_arity(s->native.universe, type_id) ==
                    (CettaExprLen)(arity + 2u) &&
                tu_head_sym(s->native.universe, type_id) ==
                    g_builtin_syms.arrow) {
                return true;
            }

            Atom *annotation = space_indexed_occurrence_atom(
                s, index->atom_indices, index->atom_ids, i);
            if (!annotation || annotation->kind != ATOM_EXPR ||
                annotation->expr.len != 3u ||
                !atom_is_symbol_id(annotation->expr.elems[0],
                                   g_builtin_syms.colon) ||
                !atom_is_symbol_id(annotation->expr.elems[1], head)) {
                continue;
            }
            Atom *type = annotation->expr.elems[2];
            if (type && type->kind == ATOM_EXPR &&
                type->expr.len == (CettaExprLen)(arity + 2u) &&
                atom_is_symbol_id(type->expr.elems[0],
                                  g_builtin_syms.arrow)) {
                return true;
            }
        }
    }
    return false;
}

/* A declared arrow signature at the call arity hands the head to the
 * typed-demand discipline; the prepared ground-call lane may not evaluate
 * arguments whose declared parameter types keep them syntax. */
bool space_head_has_arrow_signature(Space *s, SymbolId head,
                                    CettaExprLen arity) {
    if (!s || head == SYMBOL_ID_NONE)
        return false;
    Atom *const *grounded = NULL;
    uint32_t grounded_count = space_grounded_types(head, &grounded);
    if (grounded_count != 0u) {
        for (uint32_t i = 0u; i < grounded_count; i++) {
            Atom *type = grounded[i];
            if (type->kind == ATOM_EXPR &&
                type->expr.len == (CettaExprLen)(arity + 2u) &&
                atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
                return true;
        }
        return false;
    }
    /* The consult runs on hot accelerator admissions (eager closed-call
     * compiles ask once per call), so the indexed annotation consult is memoized
     * per (space instance, revision, head, arity).  Any space mutation
     * advances the revision and naturally invalidates the entry. */
    typedef struct {
        const Space *space;
        uint64_t instance_id;
        uint64_t revision;
        SymbolId head;
        CettaExprLen arity;
        bool shadowed;
        bool valid;
    } ArrowSignatureMemo;
    static _Thread_local ArrowSignatureMemo memo[64];
    SpaceReadToken read = space_read_token(s);
    size_t slot = ((size_t)head * 31u + (size_t)arity +
                   (size_t)read.revision) & 63u;
    ArrowSignatureMemo *entry = &memo[slot];
    if (entry->valid && entry->space == (const Space *)s &&
        entry->instance_id == read.instance_id &&
        entry->revision == read.revision &&
        entry->head == head && entry->arity == arity) {
        return entry->shadowed;
    }
    bool shadowed = space_member_has_arrow_signature(s, head, arity, false);
    for (uint32_t d = 0u; !shadowed && d < space_dependency_count(s); d++)
        shadowed = space_member_has_arrow_signature(
            space_dependency_at(s, d), head, arity, true);
    *entry = (ArrowSignatureMemo){
        .space = (const Space *)s,
        .instance_id = read.instance_id,
        .revision = read.revision,
        .head = head,
        .arity = arity,
        .shadowed = shadowed,
        .valid = true,
    };
    return shadowed;
}

/* Whether one member of a composed read declares a type for `head`. */
static bool space_member_declares_type(Space *s, SymbolId head,
                                       bool own_only) {
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = own_only ? space_imported_length(s) : 0u;
             i < logical_len; i++) {
            Atom *annotation = space_get_at64(s, i);
            if (annotation && annotation->kind == ATOM_EXPR &&
                annotation->expr.len == 3u &&
                atom_is_symbol_id(annotation->expr.elems[0],
                                  g_builtin_syms.colon) &&
                atom_is_symbol_id(annotation->expr.elems[1], head))
                return true;
        }
        return false;
    }
    ensure_ty_ann_index(s);
    const SpaceHeadChains *index = &s->native.ty_idx.rows;
    const SpaceHeadChain *chain = space_head_chains_find(index, head);
    for (uint32_t i = chain ? chain->first : SPACE_HEAD_CHAIN_END;
         i != SPACE_HEAD_CHAIN_END; i = index->next[i]) {
        if (own_only && index->atom_indices[i] < s->imported_len)
            continue;
        AtomId annotation_id = space_indexed_occurrence_atom_id(
            s, index->atom_indices, index->atom_ids, i);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(
                s, annotation_id, &subject_id, &type_id) &&
            tu_hdr(s->native.universe, subject_id) &&
            tu_kind(s->native.universe, subject_id) == ATOM_SYMBOL &&
            tu_sym(s->native.universe, subject_id) == head)
            return true;
        Atom *annotation = space_indexed_occurrence_atom(
            s, index->atom_indices, index->atom_ids, i);
        if (annotation && annotation->kind == ATOM_EXPR &&
            annotation->expr.len == 3u &&
            atom_is_symbol_id(annotation->expr.elems[0],
                              g_builtin_syms.colon) &&
            atom_is_symbol_id(annotation->expr.elems[1], head))
            return true;
    }
    return false;
}

bool space_head_declares_type(Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE)
        return false;
    if (space_grounded_types(head, NULL) != 0u)
        return true;
    if (space_member_declares_type(s, head, false))
        return true;
    for (uint32_t d = 0u; d < space_dependency_count(s); d++) {
        if (space_member_declares_type(space_dependency_at(s, d), head, true))
            return true;
    }
    return false;
}

bool space_head_call_untyped(Space *s, SymbolId head, CettaExprLen arity) {
    if (eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_HE)
        return !space_head_declares_type(s, head);
    return !space_head_has_arrow_signature(s, head, arity);
}

static bool space_prepare_single_equation_uncached(
        Space *s, SymbolId head, SpacePreparedEquation *out) {
    SpacePreparedEquation plan;
    CettaIndex logical_index = 0u;
    Atom *equation;
    Atom *lhs = NULL;
    Atom *rhs = NULL;

    if (out)
        memset(out, 0, sizeof(*out));
    if (!s || !out || head == SYMBOL_ID_NONE)
        return false;
    equation = space_single_linear_equation_at(s, head, &logical_index);
    if (!equation || !is_equation_atom(equation, &lhs, &rhs) ||
        !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head)) {
        return false;
    }
    CettaExprLen arity = lhs->expr.len - 1u;
    if (arity > SPACE_PREPARED_EQUATION_MAX_REGISTERS)
        return false;

    memset(&plan, 0, sizeof(plan));
    plan.occurrence.read = space_read_token(s);
    plan.occurrence.logical_index = logical_index;
    plan.equation = equation;
    plan.lhs = lhs;
    plan.rhs = rhs;
    plan.head = head;
    plan.arity = arity;
    plan.evidence = CETTA_GSLT_EVIDENCE_SINGLETON_HEAD;
    for (CettaExprIndex i = 0u; i < arity; i++) {
        Atom *argument = lhs->expr.elems[i + 1u];
        if (!argument || argument->kind != ATOM_VAR)
            return false;
        for (CettaExprIndex j = 0u; j < i; j++)
            if (var_id_eq(plan.registers[j], argument->var_id))
                return false;
        plan.registers[i] = argument->var_id;
    }
    plan.evidence |= CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS;
    if (!prepared_rhs_is_range_restricted(rhs, &plan))
        return false;
    plan.evidence |= CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS;
    if (space_read_token_is_current(plan.occurrence.read))
        plan.evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    if (CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(s, head, arity))
        plan.evidence |= CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED;
    if (!cetta_gslt_prepared_equation_plan_admitted(plan.evidence))
        return false;
    (void)prepared_register_compile_guarded_step(&plan);
    CettaGsltRegisterResultKind recursive_kind;
    bool saw_self_call = false;
    if (prepared_register_recursive_template_kind(
            &plan, plan.rhs, 0u, &recursive_kind, &saw_self_call) &&
        saw_self_call &&
        recursive_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        plan.evidence |= CETTA_GSLT_EVIDENCE_REGISTER_RECURSIVE;
    }
    *out = plan;
    return true;
}

bool space_prepare_single_equation(Space *s, SymbolId head,
                                   SpacePreparedEquation *out) {
    enum { PREPARED_EQUATION_CACHE_SLOTS = 64 };
    typedef struct {
        SpaceReadToken read;
        SymbolId head;
        SpacePreparedEquation plan;
        bool applicable;
        bool valid;
    } PreparedEquationCacheEntry;
    static _Thread_local PreparedEquationCacheEntry
        cache[PREPARED_EQUATION_CACHE_SLOTS];

    if (out)
        memset(out, 0, sizeof(*out));
    if (!s || !out || head == SYMBOL_ID_NONE)
        return false;

    SpaceReadToken read = space_read_token(s);
    size_t slot = ((size_t)head * 31u +
                   (size_t)read.revision) &
                  (PREPARED_EQUATION_CACHE_SLOTS - 1u);
    PreparedEquationCacheEntry *entry = &cache[slot];
    if (entry->valid && entry->read.space == read.space &&
        entry->read.instance_id == read.instance_id &&
        entry->read.revision == read.revision &&
        entry->head == head) {
        if (entry->applicable)
            *out = entry->plan;
        return entry->applicable;
    }

    SpacePreparedEquation plan;
    bool applicable = space_prepare_single_equation_uncached(
        s, head, &plan);
    *entry = (PreparedEquationCacheEntry){
        .read = read,
        .head = head,
        .plan = plan,
        .applicable = applicable,
        .valid = true,
    };
    if (applicable)
        *out = plan;
    return applicable;
}

static Atom *prepared_equation_instantiate_rec(
    const SpacePreparedEquation *plan, Atom *source,
    Atom *const *register_values, Arena *arena) {
    if (!source || !plan || !arena)
        return NULL;
    if (!atom_has_vars(source))
        return source;
    if (source->kind == ATOM_VAR) {
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            if (var_id_eq(plan->registers[i], source->var_id))
                return register_values[i];
        return NULL;
    }
    if (source->kind != ATOM_EXPR)
        return source;

    Atom **children = arena_alloc(
        arena, sizeof(*children) * (size_t)source->expr.len);
    if (!children)
        return NULL;
    for (CettaExprIndex i = 0u; i < source->expr.len; i++) {
        children[i] = prepared_equation_instantiate_rec(
            plan, source->expr.elems[i], register_values, arena);
        if (!children[i])
            return NULL;
    }
    return atom_expr(arena, children, source->expr.len);
}

Atom *space_prepared_equation_instantiate_ground(
    const SpacePreparedEquation *plan, Atom *call, Arena *arena) {
    if (!plan || !call || !arena ||
        call->kind != ATOM_EXPR || call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return NULL;
    }
    uint32_t evidence =
        plan->evidence & ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    Atom *register_values[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    bool ground_call = true;
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        Atom *value = call->expr.elems[i + 1u];
        if (!value || atom_has_vars(value))
            ground_call = false;
        register_values[i] = value;
    }
    if (ground_call)
        evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_equation_call_admitted(evidence))
        return NULL;
    return prepared_equation_instantiate_rec(
        plan, plan->rhs, register_values, arena);
}

static bool prepared_register_exact_integer(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           (atom->ground.gkind == GV_INT ||
            atom->ground.gkind == GV_BIGINT);
}

static Atom *prepared_register_execute_expression(
    const SpacePreparedEquation *plan, Atom *source,
    Atom *const *register_values, Arena *arena, uint32_t depth,
    CettaGsltRegisterResultKind *kind_out) {
    if (!plan || !source || !register_values || !arena || !kind_out ||
        depth > 64u) {
        return NULL;
    }
    if (source->kind == ATOM_VAR) {
        for (CettaExprIndex i = 0u; i < plan->arity; i++) {
            if (var_id_eq(plan->registers[i], source->var_id)) {
                Atom *value = register_values[i];
                if (!prepared_register_exact_integer(value))
                    return NULL;
                *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
                return value;
            }
        }
        return NULL;
    }
    if (prepared_register_exact_integer(source)) {
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return source;
    }
    if (source->kind != ATOM_EXPR || source->expr.len == 0u)
        return NULL;
    Atom *head = source->expr.elems[0];
    CettaExprLen nargs = source->expr.len - 1u;
    CettaGsltRegisterResultKind result_kind;
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_register_head_kind(head->sym_id, nargs, &result_kind) ||
        nargs > CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS) {
        return NULL;
    }
    Atom *args[CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS];
    for (CettaExprIndex i = 0u; i < nargs; i++) {
        CettaGsltRegisterResultKind child_kind;
        args[i] = prepared_register_execute_expression(
            plan, source->expr.elems[i + 1u], register_values,
            arena, depth + 1u, &child_kind);
        if (!args[i] ||
            child_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return NULL;
        }
    }
    Atom *result = grounded_dispatch(arena, head, args, (uint32_t)nargs);
    if (!result)
        return NULL;
    if ((result_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER &&
         !prepared_register_exact_integer(result)) ||
        (result_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN &&
         !(result->kind == ATOM_GROUNDED &&
           result->ground.gkind == GV_BOOL))) {
        return NULL;
    }
    *kind_out = result_kind;
    return result;
}

SpacePreparedRegisterStep space_prepared_equation_execute_register_step(
    const SpacePreparedEquation *plan, Atom *call, Arena *arena,
    Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!plan || !call || !arena || !result_out ||
        !plan->register_guard || !plan->register_base ||
        !plan->register_tail || call->kind != ATOM_EXPR ||
        call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    uint32_t evidence =
        plan->evidence & ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    Atom *register_values[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        Atom *value = call->expr.elems[i + 1u];
        if (!prepared_register_exact_integer(value))
            return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
        register_values[i] = value;
    }
    evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_register_step_admitted(evidence))
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

    CettaGsltRegisterResultKind guard_kind;
    Atom *guard = prepared_register_execute_expression(
        plan, plan->register_guard, register_values,
        arena, 0u, &guard_kind);
    if (!guard || guard_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    bool take_base = guard->ground.bval == plan->register_base_when_true;
    if (take_base) {
        CettaGsltRegisterResultKind base_kind;
        Atom *base = prepared_register_execute_expression(
            plan, plan->register_base, register_values,
            arena, 0u, &base_kind);
        if (!base || base_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
        *result_out = base;
        return SPACE_PREPARED_REGISTER_VALUE;
    }

    Atom **tail_elems = arena_alloc(
        arena, sizeof(*tail_elems) * (size_t)(plan->arity + 1u));
    tail_elems[0] = plan->register_tail->expr.elems[0];
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        CettaGsltRegisterResultKind argument_kind;
        tail_elems[i + 1u] = prepared_register_execute_expression(
            plan, plan->register_tail->expr.elems[i + 1u],
            register_values, arena, 0u, &argument_kind);
        if (!tail_elems[i + 1u] ||
            argument_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
        }
    }
    *result_out = atom_expr(arena, tail_elems, plan->arity + 1u);
    return *result_out ? SPACE_PREPARED_REGISTER_TAIL_CALL
                       : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
}

#if CETTA_BUILD_WITH_GMP
#define PREPARED_MPZ_SCRATCH_CAP 192u

typedef struct {
    mpz_t values[PREPARED_MPZ_SCRATCH_CAP];
    size_t initialized;
    size_t used;
} PreparedMpzScratch;

typedef struct {
    mpz_srcptr source;
    mpz_ptr temporary;
} PreparedMpzValue;

static mpz_ptr prepared_mpz_scratch_take(PreparedMpzScratch *scratch) {
    if (!scratch || scratch->used >= PREPARED_MPZ_SCRATCH_CAP)
        return NULL;
    if (scratch->used == scratch->initialized) {
        mpz_init(scratch->values[scratch->initialized]);
        scratch->initialized++;
    }
    return scratch->values[scratch->used++];
}

static void prepared_mpz_scratch_reset(PreparedMpzScratch *scratch) {
    if (scratch)
        scratch->used = 0u;
}

static void prepared_mpz_scratch_clear(PreparedMpzScratch *scratch) {
    if (!scratch)
        return;
    for (size_t i = 0u; i < scratch->initialized; i++)
        mpz_clear(scratch->values[i]);
    scratch->initialized = 0u;
    scratch->used = 0u;
}

static void prepared_mpz_set_i64(mpz_ptr out, int64_t value) {
    uint64_t magnitude = value < 0
        ? (uint64_t)(-(value + 1)) + 1u
        : (uint64_t)value;
    mpz_import(out, 1, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (value < 0)
        mpz_neg(out, out);
}

static bool prepared_mpz_set_atom(mpz_ptr out, const Atom *atom) {
    if (!out || !prepared_register_exact_integer(atom))
        return false;
    if (atom->ground.gkind == GV_INT) {
        prepared_mpz_set_i64(out, atom->ground.ival);
        return true;
    }
    mpz_srcptr source = atom_bigint_mpz_view(atom);
    if (!source)
        return false;
    mpz_set(out, source);
    return true;
}

static bool prepared_mpz_register_index(
    const SpacePreparedEquation *plan, const Atom *atom,
    CettaExprIndex *index_out) {
    if (!plan || !atom || atom->kind != ATOM_VAR)
        return false;
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        if (var_id_eq(plan->registers[i], atom->var_id)) {
            if (index_out)
                *index_out = i;
            return true;
        }
    }
    return false;
}

static bool prepared_mpz_eval_integer(
    const SpacePreparedEquation *plan, const Atom *source,
    mpz_t *registers, PreparedMpzScratch *scratch,
    uint32_t depth, PreparedMpzValue *value_out) {
    if (!plan || !source || !registers || !scratch || !value_out ||
        depth > 64u) {
        return false;
    }
    CettaExprIndex register_index = 0u;
    if (prepared_mpz_register_index(plan, source, &register_index)) {
        value_out->source = registers[register_index];
        value_out->temporary = NULL;
        return true;
    }
    if (prepared_register_exact_integer(source)) {
        if (source->ground.gkind == GV_BIGINT) {
            value_out->source = atom_bigint_mpz_view(source);
            value_out->temporary = NULL;
            return value_out->source != NULL;
        }
        mpz_ptr literal = prepared_mpz_scratch_take(scratch);
        if (!literal)
            return false;
        prepared_mpz_set_i64(literal, source->ground.ival);
        value_out->source = literal;
        value_out->temporary = literal;
        return true;
    }
    if (source->kind != ATOM_EXPR || source->expr.len != 3u ||
        !source->expr.elems[0] ||
        source->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    CettaGsltRegisterResultKind result_kind;
    SymbolId head = source->expr.elems[0]->sym_id;
    if (!prepared_register_head_kind(head, 2u, &result_kind) ||
        result_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        return false;
    }
    PreparedMpzValue left = {0};
    PreparedMpzValue right = {0};
    if (!prepared_mpz_eval_integer(
            plan, source->expr.elems[1], registers, scratch,
            depth + 1u, &left) ||
        !prepared_mpz_eval_integer(
            plan, source->expr.elems[2], registers, scratch,
            depth + 1u, &right)) {
        return false;
    }
    mpz_ptr result = prepared_mpz_scratch_take(scratch);
    if (!result)
        return false;
    CettaExactIntegerV1Operation operation;
    if (head == g_builtin_syms.op_plus)
        operation = CETTA_EXACT_INTEGER_V1_ADD;
    else if (head == g_builtin_syms.op_minus)
        operation = CETTA_EXACT_INTEGER_V1_SUB;
    else if (head == g_builtin_syms.op_mul)
        operation = CETTA_EXACT_INTEGER_V1_MUL;
    else
        return false;
    if (!cetta_exact_integer_big(operation, left.source, right.source, result))
        return false;
    value_out->source = result;
    value_out->temporary = result;
    return true;
}

static bool prepared_mpz_eval_guard(
    const SpacePreparedEquation *plan, mpz_t *registers,
    PreparedMpzScratch *scratch, bool *value_out) {
    Atom *guard = plan ? plan->register_guard : NULL;
    if (!guard || guard->kind != ATOM_EXPR || guard->expr.len != 3u ||
        !guard->expr.elems[0] || guard->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    CettaGsltRegisterResultKind result_kind;
    SymbolId head = guard->expr.elems[0]->sym_id;
    if (!prepared_register_head_kind(head, 2u, &result_kind) ||
        result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
        head != g_builtin_syms.op_eq) {
        return false;
    }
    prepared_mpz_scratch_reset(scratch);
    PreparedMpzValue left = {0};
    PreparedMpzValue right = {0};
    if (!prepared_mpz_eval_integer(
            plan, guard->expr.elems[1], registers, scratch, 0u, &left) ||
        !prepared_mpz_eval_integer(
            plan, guard->expr.elems[2], registers, scratch, 0u, &right)) {
        return false;
    }
    *value_out = mpz_cmp(left.source, right.source) == 0;
    return true;
}

static Atom *prepared_mpz_materialize_call(
    const SpacePreparedEquation *plan, mpz_t *registers, Arena *arena) {
    if (!plan || !registers || !arena)
        return NULL;
    Atom **elems = arena_alloc(
        arena, sizeof(*elems) * (size_t)(plan->arity + 1u));
    elems[0] = atom_symbol_id(arena, plan->head);
    for (CettaExprIndex i = 0u; i < plan->arity; i++)
        elems[i + 1u] = atom_bigint_from_mpz(arena, registers[i]);
    return atom_expr(arena, elems, plan->arity + 1u);
}

static SpacePreparedRegisterStep prepared_mpz_run_register_loop(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_steps, Atom **result_out) {
    if (!plan || !call || !result_arena || !result_out || max_steps == 0u ||
        !plan->register_guard || !plan->register_base ||
        !plan->register_tail || call->kind != ATOM_EXPR ||
        call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    uint32_t evidence = plan->evidence;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    else
        evidence &= ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_register_step_admitted(evidence))
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

    mpz_t banks[2][SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    for (unsigned bank = 0u; bank < 2u; bank++)
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            mpz_init(banks[bank][i]);
    bool initialized = true;
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        if (!prepared_mpz_set_atom(banks[0][i], call->expr.elems[i + 1u])) {
            initialized = false;
            break;
        }
    }
    if (!initialized) {
        for (unsigned bank = 0u; bank < 2u; bank++)
            for (CettaExprIndex i = 0u; i < plan->arity; i++)
                mpz_clear(banks[bank][i]);
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }

    PreparedMpzScratch scratch = {0};
    unsigned active = 0u;
    size_t completed = 0u;
    SpacePreparedRegisterStep outcome = SPACE_PREPARED_REGISTER_TAIL_CALL;
    while (completed < max_steps) {
        if (!space_read_token_is_current(plan->occurrence.read))
            break;
        bool guard = false;
        if (!prepared_mpz_eval_guard(
                plan, banks[active], &scratch, &guard)) {
            outcome = completed == 0u
                ? SPACE_PREPARED_REGISTER_NOT_APPLICABLE
                : SPACE_PREPARED_REGISTER_TAIL_CALL;
            break;
        }
        completed++;
        bool take_base = guard == plan->register_base_when_true;
        if (take_base) {
            prepared_mpz_scratch_reset(&scratch);
            PreparedMpzValue base = {0};
            if (!prepared_mpz_eval_integer(
                    plan, plan->register_base, banks[active],
                    &scratch, 0u, &base)) {
                outcome = SPACE_PREPARED_REGISTER_TAIL_CALL;
                break;
            }
            *result_out = atom_bigint_from_mpz(result_arena, base.source);
            outcome = *result_out
                ? SPACE_PREPARED_REGISTER_VALUE
                : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
            goto prepared_mpz_done;
        }

        unsigned next = active ^ 1u;
        for (CettaExprIndex i = 0u; i < plan->arity; i++) {
            prepared_mpz_scratch_reset(&scratch);
            PreparedMpzValue argument = {0};
            if (!prepared_mpz_eval_integer(
                    plan, plan->register_tail->expr.elems[i + 1u],
                    banks[active], &scratch, 0u, &argument)) {
                outcome = SPACE_PREPARED_REGISTER_TAIL_CALL;
                goto prepared_mpz_materialize;
            }
            if (argument.temporary)
                mpz_swap(banks[next][i], argument.temporary);
            else
                mpz_set(banks[next][i], argument.source);
        }
        active = next;
    }

prepared_mpz_materialize:
    if (outcome != SPACE_PREPARED_REGISTER_NOT_APPLICABLE)
        *result_out = prepared_mpz_materialize_call(
            plan, banks[active], result_arena);
prepared_mpz_done:
    prepared_mpz_scratch_clear(&scratch);
    for (unsigned bank = 0u; bank < 2u; bank++)
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            mpz_clear(banks[bank][i]);
    return *result_out ? outcome
                       : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
}

typedef struct {
    mpz_t registers[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
} PreparedRecursiveEnvironment;

typedef struct {
    mpz_t integer;
    CettaGsltRegisterResultKind kind;
    bool boolean;
} PreparedRecursiveValue;

typedef enum {
    PREPARED_RECURSIVE_ENTER = 0,
    PREPARED_RECURSIVE_ARGUMENTS,
    PREPARED_RECURSIVE_IF_CONDITION,
    PREPARED_RECURSIVE_IF_BRANCH,
    PREPARED_RECURSIVE_SELF_RESULT,
} PreparedRecursiveFrameState;

typedef struct {
    const Atom *source;
    size_t environment;
    size_t value_base;
    CettaExprIndex next_argument;
    CettaExprLen argument_count;
    SymbolId head;
    CettaGsltRegisterResultKind result_kind;
    CettaGsltRegisterInstruction instruction;
    PreparedRecursiveFrameState state;
} PreparedRecursiveFrame;

typedef struct {
    const SpacePreparedEquation *plan;
    PreparedRecursiveEnvironment **environments;
    size_t environment_len;
    size_t environment_pool_len;
    size_t environment_cap;
    PreparedRecursiveValue **values;
    size_t value_len;
    size_t value_pool_len;
    size_t value_cap;
    PreparedRecursiveFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    size_t calls;
    size_t max_calls;
} PreparedRecursiveMachine;

static bool prepared_recursive_grow_pointer_array(
    void ***items, size_t *capacity, size_t needed) {
    if (!items || !capacity)
        return false;
    if (needed <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 32u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(**items))
        return false;
    *items = cetta_realloc(*items, sizeof(**items) * next);
    *capacity = next;
    return true;
}

static PreparedRecursiveEnvironment *
prepared_recursive_push_environment(PreparedRecursiveMachine *machine) {
    if (!machine)
        return NULL;
    if (machine->environment_len == machine->environment_pool_len) {
        if (!prepared_recursive_grow_pointer_array(
                (void ***)&machine->environments,
                &machine->environment_cap,
                machine->environment_pool_len + 1u)) {
            return NULL;
        }
        PreparedRecursiveEnvironment *environment =
            cetta_malloc(sizeof(*environment));
        for (CettaExprIndex i = 0u; i < machine->plan->arity; i++)
            mpz_init(environment->registers[i]);
        machine->environments[machine->environment_pool_len++] =
            environment;
    }
    return machine->environments[machine->environment_len++];
}

static PreparedRecursiveValue *
prepared_recursive_push_value(PreparedRecursiveMachine *machine) {
    if (!machine)
        return NULL;
    if (machine->value_len == machine->value_pool_len) {
        if (!prepared_recursive_grow_pointer_array(
                (void ***)&machine->values, &machine->value_cap,
                machine->value_pool_len + 1u)) {
            return NULL;
        }
        PreparedRecursiveValue *value = cetta_malloc(sizeof(*value));
        mpz_init(value->integer);
        value->kind = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        value->boolean = false;
        machine->values[machine->value_pool_len++] = value;
    }
    return machine->values[machine->value_len++];
}

static bool prepared_recursive_push_integer(
    PreparedRecursiveMachine *machine, mpz_srcptr source) {
    if (!source)
        return false;
    PreparedRecursiveValue *value =
        prepared_recursive_push_value(machine);
    if (!value)
        return false;
    mpz_set(value->integer, source);
    value->kind = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
    value->boolean = false;
    return true;
}

static bool prepared_recursive_push_atom_integer(
    PreparedRecursiveMachine *machine, const Atom *source) {
    PreparedRecursiveValue *value =
        prepared_recursive_push_value(machine);
    if (!value)
        return false;
    if (!prepared_mpz_set_atom(value->integer, source)) {
        machine->value_len--;
        return false;
    }
    value->kind = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
    value->boolean = false;
    return true;
}

static bool prepared_recursive_push_frame(
    PreparedRecursiveMachine *machine, const Atom *source,
    size_t environment) {
    if (!machine || !source || environment >= machine->environment_len)
        return false;
    if (machine->frame_len == machine->frame_cap) {
        size_t next = machine->frame_cap ? machine->frame_cap * 2u : 64u;
        if (next < machine->frame_cap ||
            next > SIZE_MAX / sizeof(*machine->frames)) {
            return false;
        }
        machine->frames = cetta_realloc(
            machine->frames, sizeof(*machine->frames) * next);
        machine->frame_cap = next;
    }
    PreparedRecursiveFrame *frame =
        &machine->frames[machine->frame_len++];
    memset(frame, 0, sizeof(*frame));
    frame->source = source;
    frame->environment = environment;
    frame->state = PREPARED_RECURSIVE_ENTER;
    return true;
}

static void prepared_recursive_machine_free(
    PreparedRecursiveMachine *machine) {
    if (!machine)
        return;
    for (size_t i = 0u; i < machine->environment_pool_len; i++) {
        PreparedRecursiveEnvironment *environment =
            machine->environments[i];
        for (CettaExprIndex j = 0u; j < machine->plan->arity; j++)
            mpz_clear(environment->registers[j]);
        free(environment);
    }
    for (size_t i = 0u; i < machine->value_pool_len; i++) {
        mpz_clear(machine->values[i]->integer);
        free(machine->values[i]);
    }
    free(machine->environments);
    free(machine->values);
    free(machine->frames);
    memset(machine, 0, sizeof(*machine));
}

static bool prepared_recursive_apply_instruction(
    PreparedRecursiveMachine *machine,
    const PreparedRecursiveFrame *frame) {
    if (!machine || !frame || frame->argument_count != 2u ||
        machine->value_len != frame->value_base + 2u) {
        return false;
    }
    PreparedRecursiveValue *left =
        machine->values[frame->value_base];
    PreparedRecursiveValue *right =
        machine->values[frame->value_base + 1u];
    if (left->kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER ||
        right->kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        return false;
    }
    bool boolean = false;
    CettaGsltRegisterResultKind result_kind;
    if (!cetta_gslt_register_execute_binary(
            frame->instruction, left->integer, &boolean,
            left->integer, right->integer, &result_kind) ||
        result_kind != frame->result_kind) {
        return false;
    }
    left->kind = result_kind;
    left->boolean = boolean;
    machine->value_len = frame->value_base + 1u;
    return true;
}

static bool prepared_recursive_start_self_call(
    PreparedRecursiveMachine *machine,
    PreparedRecursiveFrame *frame) {
    if (!machine || !frame ||
        frame->argument_count != machine->plan->arity ||
        machine->value_len !=
            frame->value_base + (size_t)frame->argument_count ||
        machine->calls >= machine->max_calls ||
        !space_read_token_is_current(machine->plan->occurrence.read)) {
        return false;
    }
    for (CettaExprIndex i = 0u; i < frame->argument_count; i++)
        if (machine->values[frame->value_base + (size_t)i]->kind !=
            CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            return false;
    PreparedRecursiveEnvironment *environment =
        prepared_recursive_push_environment(machine);
    if (!environment)
        return false;
    for (CettaExprIndex i = 0u; i < frame->argument_count; i++) {
        mpz_set(
            environment->registers[i],
            machine->values[frame->value_base + (size_t)i]->integer);
    }
    machine->value_len = frame->value_base;
    machine->calls++;
    frame->state = PREPARED_RECURSIVE_SELF_RESULT;
    return prepared_recursive_push_frame(
        machine, machine->plan->rhs, machine->environment_len - 1u);
}

static bool prepared_recursive_run(
    PreparedRecursiveMachine *machine) {
    while (machine && machine->frame_len > 0u) {
        PreparedRecursiveFrame *frame =
            &machine->frames[machine->frame_len - 1u];
        if (frame->state == PREPARED_RECURSIVE_ENTER) {
            const Atom *source = frame->source;
            CettaExprIndex register_index = 0u;
            if (prepared_mpz_register_index(
                    machine->plan, source, &register_index)) {
                if (!prepared_recursive_push_integer(
                        machine,
                        machine->environments[frame->environment]
                            ->registers[register_index])) {
                    return false;
                }
                machine->frame_len--;
                continue;
            }
            if (prepared_register_exact_integer(source)) {
                if (!prepared_recursive_push_atom_integer(machine, source))
                    return false;
                machine->frame_len--;
                continue;
            }
            if (source->kind != ATOM_EXPR || source->expr.len == 0u ||
                !source->expr.elems[0] ||
                source->expr.elems[0]->kind != ATOM_SYMBOL) {
                return false;
            }
            frame->head = source->expr.elems[0]->sym_id;
            frame->argument_count = source->expr.len - 1u;
            frame->value_base = machine->value_len;
            if (frame->head == g_builtin_syms.if_text) {
                if (frame->argument_count != 3u)
                    return false;
                frame->state = PREPARED_RECURSIVE_IF_CONDITION;
                if (!prepared_recursive_push_frame(
                        machine, source->expr.elems[1],
                        frame->environment)) {
                    return false;
                }
                continue;
            }
            if (frame->head == machine->plan->head) {
                if (frame->argument_count != machine->plan->arity)
                    return false;
                frame->result_kind =
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
            } else if (!prepared_register_head_program(
                           frame->head, frame->argument_count,
                           &frame->result_kind, &frame->instruction)) {
                return false;
            }
            frame->next_argument = 0u;
            frame->state = PREPARED_RECURSIVE_ARGUMENTS;
            if (frame->argument_count == 0u) {
                if (frame->head != machine->plan->head ||
                    !prepared_recursive_start_self_call(machine, frame)) {
                    return false;
                }
                continue;
            }
            if (!prepared_recursive_push_frame(
                    machine, source->expr.elems[1],
                    frame->environment)) {
                return false;
            }
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_ARGUMENTS) {
            if (machine->value_len !=
                    frame->value_base +
                    (size_t)frame->next_argument + 1u ||
                machine->values[machine->value_len - 1u]->kind !=
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
            frame->next_argument++;
            if (frame->next_argument < frame->argument_count) {
                if (!prepared_recursive_push_frame(
                        machine,
                        frame->source->expr.elems[
                            frame->next_argument + 1u],
                        frame->environment)) {
                    return false;
                }
                continue;
            }
            if (frame->head == machine->plan->head) {
                if (!prepared_recursive_start_self_call(machine, frame))
                    return false;
                continue;
            }
            if (!prepared_recursive_apply_instruction(machine, frame))
                return false;
            machine->frame_len--;
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_IF_CONDITION) {
            if (machine->value_len != frame->value_base + 1u)
                return false;
            PreparedRecursiveValue *condition =
                machine->values[frame->value_base];
            if (condition->kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
                return false;
            const Atom *branch = frame->source->expr.elems[
                condition->boolean ? 2u : 3u];
            machine->value_len = frame->value_base;
            frame->state = PREPARED_RECURSIVE_IF_BRANCH;
            if (!prepared_recursive_push_frame(
                    machine, branch, frame->environment)) {
                return false;
            }
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_IF_BRANCH) {
            if (machine->value_len != frame->value_base + 1u ||
                machine->values[frame->value_base]->kind !=
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
            machine->frame_len--;
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_SELF_RESULT) {
            if (machine->environment_len != frame->environment + 2u ||
                machine->value_len != frame->value_base + 1u ||
                machine->values[frame->value_base]->kind !=
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
            machine->environment_len--;
            machine->frame_len--;
            continue;
        }
        return false;
    }
    return machine && machine->environment_len == 1u &&
           machine->value_len == 1u &&
           machine->values[0]->kind ==
               CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
}

static SpacePreparedRegisterStep prepared_mpz_run_register_recursion(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_calls, Atom **result_out) {
    if (!plan || !call || !result_arena || !result_out ||
        max_calls == 0u || call->kind != ATOM_EXPR ||
        call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    uint32_t evidence = plan->evidence;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    else
        evidence &= ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_register_recursion_admitted(evidence))
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

    PreparedRecursiveMachine machine = {
        .plan = plan,
        .max_calls = max_calls,
        .calls = 1u,
    };
    PreparedRecursiveEnvironment *initial =
        prepared_recursive_push_environment(&machine);
    bool initialized = initial != NULL;
    for (CettaExprIndex i = 0u; initialized && i < plan->arity; i++) {
        initialized = prepared_mpz_set_atom(
            initial->registers[i], call->expr.elems[i + 1u]);
    }
    bool completed = initialized &&
        prepared_recursive_push_frame(&machine, plan->rhs, 0u) &&
        prepared_recursive_run(&machine);
    if (completed)
        *result_out = atom_bigint_from_mpz(
            result_arena, machine.values[0]->integer);
    prepared_recursive_machine_free(&machine);
    return *result_out ? SPACE_PREPARED_REGISTER_VALUE
                       : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
}
#endif

/* Execute a bounded run of the same proved register instruction in a private
 * nursery.  GMP builds keep exact numerics in reusable machine registers;
 * the arena semispace below is the representation-independent fallback. */
SpacePreparedRegisterStep space_prepared_equation_run_register_loop(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_steps, Atom **result_out) {
    enum { REGISTER_LOOP_NURSERY_BYTES = 8u * 1024u * 1024u };
    if (result_out)
        *result_out = NULL;
    if (!plan || !call || !result_arena || !result_out || max_steps == 0u)
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

#if CETTA_BUILD_WITH_GMP
    SpacePreparedRegisterStep mpz_outcome =
        prepared_mpz_run_register_loop(
            plan, call, result_arena, max_steps, result_out);
    if (mpz_outcome != SPACE_PREPARED_REGISTER_NOT_APPLICABLE)
        return mpz_outcome;
#endif

    Arena semispaces[2];
    arena_init(&semispaces[0]);
    arena_init(&semispaces[1]);
    arena_set_hashcons(&semispaces[0], NULL);
    arena_set_hashcons(&semispaces[1], NULL);
    arena_set_runtime_kind(
        &semispaces[0], CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_runtime_kind(
        &semispaces[1], CETTA_ARENA_RUNTIME_KIND_SCRATCH);

    unsigned active = 0u;
    Atom *current = call;
    SpacePreparedRegisterStep outcome =
        SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    size_t completed = 0u;
    while (completed < max_steps) {
        Atom *next = NULL;
        SpacePreparedRegisterStep step =
            space_prepared_equation_execute_register_step(
                plan, current, &semispaces[active], &next);
        if (step == SPACE_PREPARED_REGISTER_NOT_APPLICABLE) {
            /* A revision change or a value outside the proved fragment falls
             * back at the exact current call, never at the stale entry call. */
            outcome = completed == 0u
                ? SPACE_PREPARED_REGISTER_NOT_APPLICABLE
                : SPACE_PREPARED_REGISTER_TAIL_CALL;
            break;
        }
        completed++;
        current = next;
        outcome = step;
        if (step == SPACE_PREPARED_REGISTER_VALUE)
            break;

        if (arena_accounted_live_bytes(&semispaces[active]) >=
            REGISTER_LOOP_NURSERY_BYTES) {
            unsigned destination = active ^ 1u;
            arena_free(&semispaces[destination]);
            arena_init(&semispaces[destination]);
            arena_set_hashcons(&semispaces[destination], NULL);
            arena_set_runtime_kind(
                &semispaces[destination],
                CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            Atom *evacuated = atom_deep_copy(
                &semispaces[destination], current);
            if (!evacuated) {
                fputs("fatal: prepared register-loop evacuation failed\n",
                      stderr);
                abort();
            }
            arena_free(&semispaces[active]);
            arena_init(&semispaces[active]);
            arena_set_hashcons(&semispaces[active], NULL);
            arena_set_runtime_kind(
                &semispaces[active], CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            active = destination;
            current = evacuated;
        }
    }

    if (outcome != SPACE_PREPARED_REGISTER_NOT_APPLICABLE) {
        *result_out = atom_deep_copy(result_arena, current);
        if (!*result_out)
            outcome = SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    arena_free(&semispaces[0]);
    arena_free(&semispaces[1]);
    return outcome;
}

/* Execute the generated pure recursive-register program on explicit
 * environments, work frames, and value slots.  The machine owns no arena
 * pointers other than the revision-pinned source plan; exact numerics remain
 * in reusable GMP cells and only the final value is materialized. */
SpacePreparedRegisterStep space_prepared_equation_run_register_recursion(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_calls, Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!plan || !call || !result_arena || !result_out || max_calls == 0u)
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
#if CETTA_BUILD_WITH_GMP
    return prepared_mpz_run_register_recursion(
        plan, call, result_arena, max_calls, result_out);
#else
    return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
#endif
}

CettaTermMatchStatus query_rule_visit(const CettaRuleDescriptor *rule,
    Atom *query, Arena *a, QueryResultVisitor visitor, void *ctx,
    CettaCount *emitted) {
    if (emitted) *emitted = 0u;
    if (!rule || !query || !a || !visitor || !emitted)
        return CETTA_TERM_MATCH_INVALID;
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    CettaFrameIdentity epoch;
    if (!cetta_frame_identity_scope_try(&frame_identity_scope, &epoch))
        return CETTA_TERM_MATCH_NO_MEMORY;
    Bindings environment;
    bindings_init(&environment);
    Atom *body = NULL;
    CettaTermMatchStatus status = cetta_rule_prepare(rule, query, epoch, a,
                                                     &environment, atom_eq, &body);
    if (status == CETTA_TERM_MATCH_OK && rule->binding == CETTA_RULE_BIND_HEAD) {
        /* A directional head writes only its fresh rule frame. The prepared
         * body already contains the caller's unchanged identities; exporting
         * private rule bindings or rewriting caller aliases would be wrong. */
        Bindings empty;
        bindings_init(&empty);
        QueryResultSink sink;
        query_result_sink_init_visit(&sink, visitor, ctx);
        (void)query_result_sink_emit(&sink, body, &empty);
        *emitted = sink.emitted;
        bindings_free(&empty);
    } else if (status == CETTA_TERM_MATCH_OK) {
        QueryVisibleVarSet visible;
        query_visible_var_set_init(&visible);
        Bindings projected;
        bindings_init(&projected);
        if (!collect_query_visible_vars_rec(query, &visible) ||
            !project_query_visible_bindings(a, &visible, &environment, &projected))
            status = CETTA_TERM_MATCH_NO_MEMORY;
        else {
            body = rewrite_query_visible_aliases(a, body, &visible, &environment);
            if (!body)
                status = CETTA_TERM_MATCH_NO_MEMORY;
            else {
                QueryResultSink sink;
                query_result_sink_init_visit(&sink, visitor, ctx);
                (void)query_result_sink_emit(&sink, body, &projected);
                *emitted = sink.emitted;
            }
        }
        bindings_free(&projected);
        query_visible_var_set_free(&visible);
    }
    bindings_free(&environment);
    return status;
}

CettaCount query_equation_visit(Atom *equation, Atom *query, Arena *a,
                                QueryResultVisitor visitor, void *ctx) {
                                    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    QueryResultSink sink;
    QueryVisibleVarSet visible;
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !query || !a || !visitor ||
        !is_equation_atom(equation, &lhs, &rhs)) {
        return 0;
    }
    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return 0;
    }
    query_result_sink_init_visit(&sink, visitor, ctx);
    (void)query_equation_emit_decoded_epoch(
        lhs, rhs, query, &visible, a, cetta_frame_identity_scope_fresh(&frame_identity_scope), NULL, &sink);
    query_visible_var_set_free(&visible);
    return sink.emitted;
}

CettaCount query_equations_visit_singleton(
    Atom *equation, Atom *query, Arena *a,
    QueryResultVisitor visitor, void *ctx) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS);
    if (!equation || !query || !a || !visitor)
        return 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES);
    return query_equation_visit(equation, query, a, visitor, ctx);
}

void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out) {
    QueryResultSink sink;
    query_result_sink_init_collect(&sink, out);
    (void)query_equations_core(s, query, a, &sink);
}

static bool space_member_equations_may_match_known_head(Space *s,
                                                        SymbolId head);

static bool space_member_directional_rules_may_match(Space *s, SymbolId head) {
    if (!s) return false;
    if (space_has_overlay_base(s)) {
        CettaCount begin = space_overlay_visible_base_count(s);
        CettaCount end = space_length64(s);
        for (CettaIndex i = begin; i < end; ++i) {
            CettaRuleDescriptor rule;
            if (cetta_rule_view(space_get_at64(s, i), CETTA_RULE_SYNTAX_DIRECTIONAL, &rule) &&
                (head == SYMBOL_ID_NONE || eq_lhs_may_match_known_head(rule.head, head))) return true;
        }
        return space_member_directional_rules_may_match((Space *)s->overlay_base, head);
    }
    ensure_eq_index(s);
    if (head == SYMBOL_ID_NONE) return s->native.eq_idx.directional_present;
    return s->native.eq_idx.directional_wildcard ||
        eq_head_set_contains(&s->native.eq_idx.directional_heads, head);
}

bool space_directional_rules_may_match_general(Space *s, SymbolId head) {
    if (space_member_directional_rules_may_match(s, head)) return true;
    for (uint32_t i = 0u; i < space_dependency_count(s); ++i)
        if (space_member_directional_rules_may_match(space_dependency_at(s, i), head))
            return true;
    return false;
}

/* Whether a module that `s` imports may define `head`.  A dependency's
 * imported atoms belong to the modules `s` reads anyway, so its whole space
 * is consulted. */
static inline __attribute__((always_inline))
bool space_dependencies_may_match_known_head(const Space *s,
                                                    SymbolId head) {
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies; d++) {
        if (space_member_equations_may_match_known_head(
                space_dependency_at(s, d), head))
            return true;
    }
    return false;
}

/* Whether an equation whose left side has no head symbol (a variable, an
 * expression or a grounded value in head position) may be visible. */
bool space_equations_may_match_unnamed_head(Space *s) {
    if (!s || space_has_overlay_base(s))
        return true;
    ensure_eq_index(s);
    if (s->native.eq_idx.wildcard.len || s->native.eq_idx.directional_wildcard)
        return true;
    for (uint32_t d = 0u; d < space_dependency_count(s); d++)
        if (space_equations_may_match_unnamed_head(space_dependency_at(s, d)))
            return true;
    return false;
}

bool space_equations_may_match_known_head(Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE)
        return true;
    return space_member_equations_may_match_known_head(s, head) ||
           space_dependencies_may_match_known_head(s, head);
}

static bool space_member_equations_may_match_known_head(Space *s,
                                                        SymbolId head) {
    if (space_has_overlay_base(s)) {
        /* Scan only the local scratch tail and delegate the base-visible
         * prefix to the base's own (indexed) check.  Base equations removed
         * through the overlay can yield a conservative true, which is
         * harmless for a may-match predicate. */
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = base_visible; i < logical_len; i++) {
            Atom *equation = space_get_at64(s, i);
            Atom *lhs = NULL;
            Atom *rhs = NULL;
            if (!equation || !is_equation_atom(equation, &lhs, &rhs))
                continue;
            if (eq_lhs_may_match_known_head(lhs, head))
                return true;
        }
        return space_member_equations_may_match_known_head(
            (Space *)s->overlay_base, head);
    }
    ensure_eq_index(s);
    if (space_head_chains_find(&s->native.eq_idx.head_rows, head))
        return true;
    for (CettaIndex position = 0u;
         position < s->native.eq_idx.wildcard.len; position++) {
        Atom *equation = space_indexed_occurrence_atom(
            s, s->native.eq_idx.wildcard.atom_indices,
            s->native.eq_idx.wildcard.atom_ids, position);
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        if (equation && is_equation_atom(equation, &lhs, &rhs) &&
            eq_lhs_may_match_known_head(lhs, head)) {
            return true;
        }
    }
    return false;
}

static void space_equation_note_head_arity(
    Atom *equation, SymbolId head, CettaExprLen query_arity,
    bool *found, CettaExprLen *minimum, CettaExprLen *maximum,
    bool *has_exact) {
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs) ||
        !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
        eq_head_symbol(lhs) != head)
        return;
    CettaExprLen arity = lhs->expr.len - 1u;
    if (!*found || arity < *minimum)
        *minimum = arity;
    if (!*found || arity > *maximum)
        *maximum = arity;
    *found = true;
    *has_exact = *has_exact || arity == query_arity;
}

static void space_member_equation_head_arity(
    Space *s, SymbolId head, CettaExprLen query_arity, bool *found,
    CettaExprLen *minimum, CettaExprLen *maximum, bool *has_exact) {
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        for (CettaIndex index = 0u; index < logical_len; index++) {
            space_equation_note_head_arity(
                space_get_at64(s, index), head, query_arity,
                found, minimum, maximum, has_exact);
        }
        return;
    }
    ensure_eq_index(s);
    const SpaceHeadChains *rows = &s->native.eq_idx.head_rows;
    const SpaceHeadChain *chain = space_head_chains_find(rows, head);
    for (uint32_t row = chain ? chain->first : SPACE_HEAD_CHAIN_END;
         row != SPACE_HEAD_CHAIN_END; row = rows->next[row]) {
        space_equation_note_head_arity(
            space_indexed_occurrence_atom(
                s, rows->atom_indices, rows->atom_ids, row),
            head, query_arity,
            found, minimum, maximum, has_exact);
    }
}

bool space_equation_head_arity_bounds(
    Space *s, SymbolId head, CettaExprLen *minimum,
    CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity) {
    if (minimum)
        *minimum = 0u;
    if (maximum)
        *maximum = 0u;
    if (has_exact)
        *has_exact = false;
    if (!s || head == SYMBOL_ID_NONE || !minimum || !maximum ||
        !has_exact)
        return false;

    bool found = false;
    space_member_equation_head_arity(s, head, query_arity, &found, minimum,
                                     maximum, has_exact);
    uint32_t dependencies = space_dependency_count(s);
    for (uint32_t d = 0u; d < dependencies; d++)
        space_member_equation_head_arity(
            space_dependency_at(s, d), head, query_arity, &found, minimum,
            maximum, has_exact);
    return found;
}

bool space_extended_rule_head_arity_bounds(Space *s, SymbolId head,
    CettaExprLen *minimum, CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity, unsigned admitted_forms) {
    if ((admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) &&
        (!(admitted_forms & CETTA_RULE_SYNTAX_DIRECTIONAL) ||
         !space_directional_rules_may_match_known_head(s, head)))
        return space_equation_head_arity_bounds(s, head, minimum, maximum,
                                                has_exact, query_arity);
    if (minimum) *minimum = 0u;
    if (maximum) *maximum = 0u;
    if (has_exact) *has_exact = false;
    if (!s || head == SYMBOL_ID_NONE || !minimum || !maximum || !has_exact)
        return false;
    bool found = (admitted_forms & CETTA_RULE_SYNTAX_ORDINARY) &&
        space_equation_head_arity_bounds(s, head, minimum, maximum,
                                         has_exact, query_arity);
    if (!(admitted_forms & CETTA_RULE_SYNTAX_DIRECTIONAL) || !minimum ||
        !maximum || !has_exact ||
        !space_directional_rules_may_match_known_head(s, head)) return found;
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init(s, head, CETTA_RULE_SYNTAX_DIRECTIONAL, &cursor))
        return found;
    SpaceEquationOccurrenceId id;
    while (space_equation_cursor_next(&cursor, &id) == SPACE_EQUATION_CURSOR_ITEM) {
        SpaceEquationOccurrence occurrence;
        CettaRuleDescriptor rule;
        if (!space_rule_occurrence_resolve(id, CETTA_RULE_SYNTAX_DIRECTIONAL, &occurrence) ||
            !cetta_rule_view(occurrence.equation, CETTA_RULE_SYNTAX_DIRECTIONAL, &rule) ||
            rule.head->kind != ATOM_EXPR || !rule.head->expr.len ||
            eq_head_symbol(rule.head) != head) continue;
        CettaExprLen arity = rule.head->expr.len - 1u;
        if (!found || arity < *minimum) *minimum = arity;
        if (!found || arity > *maximum) *maximum = arity;
        *has_exact = *has_exact || arity == query_arity;
        found = true;
    }
    return found;
}
