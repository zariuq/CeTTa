#include "gslt_u32_index_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { INITIAL_CAPACITY = 16u, PROBE_LIMIT = 32u };

#ifdef CETTA_GSLT_INDEX_TEST_HOOKS
static uint64_t test_front_probes, test_tree_probes;
#define FRONT_PROBE() (test_front_probes++)
#define TREE_PROBE() (test_tree_probes++)
#else
#define FRONT_PROBE() ((void)0)
#define TREE_PROBE() ((void)0)
#endif

/* The complete runtime supplies its process-key source. The standalone
 * component remains exact and bounded with deterministic placement. */
extern uint64_t cetta_placement_key_next(void) __attribute__((weak));

typedef struct CettaGsltU32OverflowV1 Node;
struct CettaGsltU32OverflowV1 {
    Node *left, *right;
    uint32_t key, value, height;
};

size_t cetta_gslt_u32_index_bytes_v1(const CettaGsltU32IndexV1 *index) {
    if (!index)
        return 0u;
    const size_t slot_bytes = sizeof(*index->keys) + sizeof(*index->values) +
                              sizeof(*index->occupied);
    if ((size_t)index->cap > SIZE_MAX / slot_bytes)
        return SIZE_MAX;
    size_t bytes = (size_t)index->cap * slot_bytes;
    if ((size_t)index->overflow_len > (SIZE_MAX - bytes) / sizeof(Node))
        return SIZE_MAX;
    return bytes + (size_t)index->overflow_len * sizeof(Node);
}

static uint32_t u32_index_key_hash(uint32_t value) {
    value ^= value >> 16u;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15u;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16u);
}

static uint32_t u32_index_height(const Node *node) { return node ? node->height : 0u; }

static void u32_index_update(Node *node) {
    uint32_t left = u32_index_height(node->left), right = u32_index_height(node->right);
    node->height = 1u + (left > right ? left : right);
}

static Node *u32_index_rotate_right(Node *node) {
    Node *root = node->left;
    node->left = root->right;
    root->right = node;
    u32_index_update(node);
    u32_index_update(root);
    return root;
}

static Node *u32_index_rotate_left(Node *node) {
    Node *root = node->right;
    node->right = root->left;
    root->left = node;
    u32_index_update(node);
    u32_index_update(root);
    return root;
}

/* The caller has already established absence and owns the fresh node.
 * Neither insertion nor a rotation allocates after this boundary. */
static Node *u32_index_tree_insert(Node *root, Node *fresh) {
    if (!root)
        return fresh;
    if (fresh->key < root->key)
        root->left = u32_index_tree_insert(root->left, fresh);
    else
        root->right = u32_index_tree_insert(root->right, fresh);
    u32_index_update(root);
    uint32_t left = u32_index_height(root->left), right = u32_index_height(root->right);
    if (left > right + 1u) {
        if (u32_index_height(root->left->right) > u32_index_height(root->left->left))
            root->left = u32_index_rotate_left(root->left);
        return u32_index_rotate_right(root);
    }
    if (right > left + 1u) {
        if (u32_index_height(root->right->left) > u32_index_height(root->right->right))
            root->right = u32_index_rotate_right(root->right);
        return u32_index_rotate_left(root);
    }
    return root;
}

static void u32_index_tree_free(Node *node) {
    if (!node)
        return;
    u32_index_tree_free(node->left);
    u32_index_tree_free(node->right);
    free(node);
}

static const Node *u32_index_tree_find(const Node *node, uint32_t key) {
    while (node) {
        TREE_PROBE();
        if (node->key == key)
            return node;
        node = key < node->key ? node->left : node->right;
    }
    return NULL;
}

void cetta_gslt_u32_index_init_v1(CettaGsltU32IndexV1 *index) {
    if (index)
        memset(index, 0, sizeof(*index));
}

void cetta_gslt_u32_index_free_v1(CettaGsltU32IndexV1 *index) {
    if (!index)
        return;
    free(index->keys);
    free(index->values);
    free(index->occupied);
    u32_index_tree_free(index->overflow);
    cetta_gslt_u32_index_init_v1(index);
}

void cetta_gslt_u32_index_reset_v1(CettaGsltU32IndexV1 *index) {
    if (!index)
        return;
    if (index->occupied)
        memset(index->occupied, 0, index->cap);
    u32_index_tree_free(index->overflow);
    index->overflow = NULL;
    index->overflow_len = 0u;
    index->len = 0u;
}

bool cetta_gslt_u32_index_shape_valid_v1(const CettaGsltU32IndexV1 *index) {
    if (!index)
        return false;
    if (!index->cap)
        return !index->len && !index->keys && !index->values &&
            !index->occupied && !index->overflow && !index->overflow_len;
    return index->cap >= INITIAL_CAPACITY &&
        (index->cap & (index->cap - 1u)) == 0u &&
        index->len <= index->cap && index->keys && index->values &&
        index->occupied && index->overflow_len <= index->len &&
        ((index->overflow == NULL) == (index->overflow_len == 0u));
}

/* False means that all permitted slots were occupied. Such a miss still
 * consults the exact overflow; it never extends the linear probe. */
static bool u32_index_locate(const CettaGsltU32IndexV1 *index, uint32_t key,
                   uint32_t *slot, bool *found) {
    uint32_t mask = index->cap - 1u;
    uint32_t start = u32_index_key_hash(key ^ index->placement_salt) & mask;
    uint32_t limit = index->cap < PROBE_LIMIT ? index->cap : PROBE_LIMIT;
    for (uint32_t probe = 0u; probe < limit; probe++) {
        FRONT_PROBE();
        uint32_t position = (start + probe) & mask;
        if (!index->occupied[position]) {
            *slot = position;
            *found = false;
            return true;
        }
        if (index->keys[position] == key) {
            *slot = position;
            *found = true;
            return true;
        }
    }
    return false;
}

bool cetta_gslt_u32_index_find_v1(const CettaGsltU32IndexV1 *index,
                                 uint32_t key, uint32_t *value) {
    if (!value || !cetta_gslt_u32_index_shape_valid_v1(index) || !index->cap)
        return false;
    uint32_t slot;
    bool found;
    if (u32_index_locate(index, key, &slot, &found) && found) {
        *value = index->values[slot];
        return true;
    }
    const Node *node = u32_index_tree_find(index->overflow, key);
    if (!node)
        return false;
    *value = node->value;
    return true;
}

static bool u32_index_put_absent(CettaGsltU32IndexV1 *index, uint32_t key, uint32_t value) {
    uint32_t slot;
    bool found;
    if (u32_index_locate(index, key, &slot, &found)) {
        if (found)
            return false;
        index->keys[slot] = key;
        index->values[slot] = value;
        index->occupied[slot] = 1u;
    } else {
        Node *node = malloc(sizeof(*node));
        if (!node)
            return false;
        *node = (Node){.key = key, .value = value, .height = 1u};
        index->overflow = u32_index_tree_insert(index->overflow, node);
        index->overflow_len++;
    }
    index->len++;
    return true;
}

static bool u32_index_tree_copy(const Node *node, CettaGsltU32IndexV1 *destination) {
    return !node || (u32_index_tree_copy(node->left, destination) &&
        u32_index_put_absent(destination, node->key, node->value) &&
        u32_index_tree_copy(node->right, destination));
}

/* Prepare the entire resized map and the requested binding privately. A
 * refused allocation preserves the old bindings and storage descriptor. */
static bool u32_index_rebuild_insert(CettaGsltU32IndexV1 *index, uint32_t capacity,
                            uint32_t key, uint32_t value) {
    CettaGsltU32IndexV1 fresh;
    cetta_gslt_u32_index_init_v1(&fresh);
    fresh.cap = capacity;
    fresh.placement_salt = index->placement_salt;
    if (!index->cap && cetta_placement_key_next) {
        uint64_t random = cetta_placement_key_next();
        fresh.placement_salt = (uint32_t)(random ^ (random >> 32u));
    }
    if ((uint64_t)capacity * sizeof(*fresh.keys) > SIZE_MAX)
        return false;
    fresh.keys = malloc((size_t)capacity * sizeof(*fresh.keys));
    fresh.values = malloc((size_t)capacity * sizeof(*fresh.values));
    fresh.occupied = calloc(capacity, sizeof(*fresh.occupied));
    bool ready = fresh.keys && fresh.values && fresh.occupied;
    for (uint32_t slot = 0u; ready && slot < index->cap; slot++)
        if (index->occupied[slot])
            ready = u32_index_put_absent(&fresh, index->keys[slot], index->values[slot]);
    ready = ready && u32_index_tree_copy(index->overflow, &fresh) &&
        u32_index_put_absent(&fresh, key, value) && fresh.len == index->len + 1u;
    if (!ready) {
        cetta_gslt_u32_index_free_v1(&fresh);
        return false;
    }
    cetta_gslt_u32_index_free_v1(index);
    *index = fresh;
    return true;
}

CettaGsltU32IndexInsertResultV1 cetta_gslt_u32_index_insert_unique_v1(
        CettaGsltU32IndexV1 *index, uint32_t key, uint32_t value) {
    if (!cetta_gslt_u32_index_shape_valid_v1(index))
        return CETTA_GSLT_U32_INDEX_INVALID_V1;
    uint32_t previous;
    if (cetta_gslt_u32_index_find_v1(index, key, &previous))
        return CETTA_GSLT_U32_INDEX_DUPLICATE_V1;
    if (index->len == UINT32_MAX)
        return CETTA_GSLT_U32_INDEX_RESOURCE_V1;
    if (!index->cap || (uint64_t)(index->len + 1u) * 10u > (uint64_t)index->cap * 7u) {
        if (index->cap > UINT32_MAX / 2u)
            return CETTA_GSLT_U32_INDEX_RESOURCE_V1;
        uint32_t cap = index->cap ? index->cap * 2u : INITIAL_CAPACITY;
        return u32_index_rebuild_insert(index, cap, key, value)
            ? CETTA_GSLT_U32_INDEX_INSERTED_V1 : CETTA_GSLT_U32_INDEX_RESOURCE_V1;
    }
    return u32_index_put_absent(index, key, value)
        ? CETTA_GSLT_U32_INDEX_INSERTED_V1 : CETTA_GSLT_U32_INDEX_RESOURCE_V1;
}

static bool u32_index_validate_tree(const CettaGsltU32IndexV1 *index, const Node *node,
        bool has_low, uint32_t low, bool has_high, uint32_t high,
        uint32_t depth, uint64_t *count) {
    if (!node)
        return true;
    if (depth > 65u || (has_low && node->key <= low) ||
        (has_high && node->key >= high))
        return false;
    uint32_t left = u32_index_height(node->left), right = u32_index_height(node->right);
    if (left > right + 1u || right > left + 1u ||
        node->height != 1u + (left > right ? left : right))
        return false;
    uint32_t slot;
    bool found;
    if (u32_index_locate(index, node->key, &slot, &found) && found)
        return false;
    (*count)++;
    return u32_index_validate_tree(index, node->left, has_low, low, true, node->key,
               depth + 1u, count) &&
        u32_index_validate_tree(index, node->right, true, node->key, has_high, high,
               depth + 1u, count);
}

bool cetta_gslt_u32_index_validate_v1(const CettaGsltU32IndexV1 *index) {
    if (!cetta_gslt_u32_index_shape_valid_v1(index))
        return false;
    uint64_t fast_count = 0u, overflow_count = 0u;
    for (uint32_t slot = 0u; slot < index->cap; slot++) {
        if (!index->occupied[slot])
            continue;
        uint32_t position;
        bool found;
        if (!u32_index_locate(index, index->keys[slot], &position, &found) ||
            !found || position != slot)
            return false;
        fast_count++;
    }
    return u32_index_validate_tree(index, index->overflow, false, 0u, false, 0u, 1u,
                         &overflow_count) && overflow_count == index->overflow_len &&
        fast_count + overflow_count == index->len;
}
