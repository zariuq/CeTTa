#include "select/code_tree.h"

#include <stdlib.h>
#include <string.h>

/*
 * Nodes live in one growable array and refer to each other by index, so the
 * tree can grow on insert without invalidating references.  A node at depth d
 * branches on coordinate d; depth == coordinate_count is a leaf.
 *
 * Branches are kept sorted by tag for a binary search on known tags.  The
 * absence of a branch is the empty subtree: the invariant the laws need is
 * exactly that a tag with no branch has nothing beneath it.
 */

#define NODE_NONE UINT32_MAX

typedef struct {
    uint32_t tag;
    uint32_t child;
} Branch;

typedef struct {
    uint32_t depth;
    /* interior */
    Branch *branches;
    uint32_t branch_len;
    uint32_t branch_cap;
    uint32_t free_child;       /* NODE_NONE until an unconstrained pattern arrives */
    /* leaf */
    uint32_t *ids;
    uint32_t id_len;
    uint32_t id_cap;
} Node;

struct CettaCodeTree {
    uint32_t coordinate_count;
    uint32_t occurrence_capacity;  /* one more than the largest index seen */
    Node *nodes;
    uint32_t node_len;
    uint32_t node_cap;
    uint32_t root;
};

static bool grow(void **items, uint32_t *cap, uint32_t needed, size_t size) {
    if (needed <= *cap)
        return true;
    uint32_t next = *cap ? *cap : 4u;
    while (next < needed) {
        if (next > UINT32_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (size == 0u || next > SIZE_MAX / size)
        return false;
    void *fresh = realloc(*items, (size_t)next * size);
    if (!fresh)
        return false;
    *items = fresh;
    *cap = next;
    return true;
}

static uint32_t node_new(CettaCodeTree *tree, uint32_t depth) {
    if (tree->node_len == UINT32_MAX)
        return NODE_NONE;
    if (!grow((void **)&tree->nodes, &tree->node_cap, tree->node_len + 1u, sizeof(Node)))
        return NODE_NONE;
    Node *node = &tree->nodes[tree->node_len];
    memset(node, 0, sizeof(*node));
    node->depth = depth;
    node->free_child = NODE_NONE;
    return tree->node_len++;
}

static bool leaf_add(CettaCodeTree *tree, uint32_t leaf, uint32_t index) {
    Node *node = &tree->nodes[leaf];
    if (index == UINT32_MAX || node->id_len == UINT32_MAX)
        return false;
    if (!grow((void **)&node->ids, &node->id_cap, node->id_len + 1u, sizeof(uint32_t)))
        return false;
    node->ids[node->id_len++] = index;
    if (index >= tree->occurrence_capacity)
        tree->occurrence_capacity = index + 1u;
    return true;
}

/* Sorted branch lookup; returns the position, and whether it holds `tag`. */
static uint32_t branch_position(const Node *node, uint32_t tag, bool *found) {
    uint32_t low = 0u;
    uint32_t high = node->branch_len;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if (node->branches[mid].tag < tag)
            low = mid + 1u;
        else
            high = mid;
    }
    *found = low < node->branch_len && node->branches[low].tag == tag;
    return low;
}

/* Child for `tag`, creating the branch (and its node) when `create`. */
static uint32_t branch_child(CettaCodeTree *tree, uint32_t node_index, uint32_t tag,
                             bool create) {
    Node *node = &tree->nodes[node_index];
    bool found;
    uint32_t position = branch_position(node, tag, &found);
    if (found)
        return node->branches[position].child;
    if (!create)
        return NODE_NONE;
    uint32_t child = node_new(tree, node->depth + 1u);
    if (child == NODE_NONE)
        return NODE_NONE;
    node = &tree->nodes[node_index];  /* node array may have moved */
    if (!grow((void **)&node->branches, &node->branch_cap, node->branch_len + 1u, sizeof(Branch)))
        return NODE_NONE;
    memmove(&node->branches[position + 1u], &node->branches[position],
            (size_t)(node->branch_len - position) * sizeof(Branch));
    node->branches[position].tag = tag;
    node->branches[position].child = child;
    node->branch_len++;
    return child;
}

static uint32_t free_child(CettaCodeTree *tree, uint32_t node_index, bool create) {
    Node *node = &tree->nodes[node_index];
    if (node->free_child != NODE_NONE || !create)
        return node->free_child;
    uint32_t child = node_new(tree, node->depth + 1u);
    if (child == NODE_NONE)
        return NODE_NONE;
    tree->nodes[node_index].free_child = child;
    return child;
}

/* Descend along a pattern, creating the path, and add the index at the leaf. */
static bool insert_path(CettaCodeTree *tree, uint32_t index, const uint32_t *pattern) {
    uint32_t node = tree->root;
    for (uint32_t d = 0u; d < tree->coordinate_count; d++) {
        uint32_t tag = pattern[d];
        node = tag == CETTA_CODE_TREE_UNKNOWN
            ? free_child(tree, node, true)
            : branch_child(tree, node, tag, true);
        if (node == NODE_NONE)
            return false;
    }
    return leaf_add(tree, node, index);
}

CettaCodeTree *cetta_code_tree_build(uint32_t coordinate_count,
                                     const uint32_t *const *patterns,
                                     uint32_t occurrence_count) {
    if (coordinate_count == UINT32_MAX ||
        (coordinate_count && occurrence_count && !patterns))
        return NULL;
    CettaCodeTree *tree = calloc(1u, sizeof(*tree));
    if (!tree)
        return NULL;
    tree->coordinate_count = coordinate_count;
    tree->root = node_new(tree, 0u);
    if (tree->root == NODE_NONE) {
        cetta_code_tree_free(tree);
        return NULL;
    }
    for (uint32_t i = 0u; i < occurrence_count; i++) {
        if ((coordinate_count && !patterns[i]) ||
            !insert_path(tree, i, coordinate_count ? patterns[i] : NULL)) {
            cetta_code_tree_free(tree);
            return NULL;
        }
    }
    if (tree->occurrence_capacity < occurrence_count)
        tree->occurrence_capacity = occurrence_count;
    return tree;
}

void cetta_code_tree_free(CettaCodeTree *tree) {
    if (!tree)
        return;
    for (uint32_t i = 0u; i < tree->node_len; i++) {
        free(tree->nodes[i].branches);
        free(tree->nodes[i].ids);
    }
    free(tree->nodes);
    free(tree);
}

uint32_t cetta_code_tree_coordinate_count(const CettaCodeTree *tree) {
    return tree ? tree->coordinate_count : 0u;
}

uint32_t cetta_code_tree_occurrence_capacity(const CettaCodeTree *tree) {
    return tree ? tree->occurrence_capacity : 0u;
}

typedef struct {
    uint32_t node;
    uint32_t next;
    bool entered;
    bool free_done;
    CettaCodeTreeObservation observation;
} TraversalFrame;

struct CettaCodeTreeCursor {
    uint8_t *marks;
    uint32_t mark_capacity;
    uint32_t *output;
    uint32_t output_capacity;
    TraversalFrame *stack;
    uint32_t stack_capacity;
};

static bool cursor_prepare(CettaCodeTreeCursor *cursor, const CettaCodeTree *tree) {
    return grow((void **)&cursor->marks, &cursor->mark_capacity,
                tree->occurrence_capacity, sizeof(*cursor->marks)) &&
           grow((void **)&cursor->output, &cursor->output_capacity,
                tree->occurrence_capacity, sizeof(*cursor->output)) &&
           grow((void **)&cursor->stack, &cursor->stack_capacity,
                tree->coordinate_count + 1u, sizeof(*cursor->stack));
}

CettaCodeTreeCursor *cetta_code_tree_cursor_new(const CettaCodeTree *tree) {
    if (!tree)
        return NULL;
    CettaCodeTreeCursor *cursor = calloc(1u, sizeof(*cursor));
    if (cursor && !cursor_prepare(cursor, tree)) {
        cetta_code_tree_cursor_free(cursor);
        cursor = NULL;
    }
    return cursor;
}

void cetta_code_tree_cursor_free(CettaCodeTreeCursor *cursor) {
    if (!cursor)
        return;
    free(cursor->marks);
    free(cursor->output);
    free(cursor->stack);
    free(cursor);
}

bool cetta_code_tree_select_observed(const CettaCodeTree *tree,
                                    CettaCodeTreeCursor *cursor,
                                    CettaCodeTreeObserveFn observe, void *context,
                                    const uint32_t **out, uint32_t *count,
                                    CettaCodeTreeStats *stats) {
    if (out) *out = NULL;
    if (count) *count = 0u;
    if (!tree || !cursor || !out || !count ||
        (tree->coordinate_count && !observe) || !cursor_prepare(cursor, tree))
        return false;
    if (tree->occurrence_capacity)
        memset(cursor->marks, 0, tree->occurrence_capacity);
    uint32_t depth = 1u;
    cursor->stack[0] = (TraversalFrame){.node = tree->root};
    while (depth) {
        TraversalFrame *frame = &cursor->stack[depth - 1u];
        const Node *node = &tree->nodes[frame->node];
        if (!frame->entered) {
            frame->entered = true;
            if (stats) stats->node_visits++;
            if (node->depth == tree->coordinate_count) {
                if (stats) stats->leaf_visits++;
                for (uint32_t i = 0u; i < node->id_len; i++)
                    cursor->marks[node->ids[i]] = 1u;
                depth--;
                continue;
            }
            if (!observe(context, node->depth, &frame->observation) ||
                (!frame->observation.all_tags && frame->observation.count &&
                 !frame->observation.tags))
                return false;
        }
        uint32_t child = NODE_NONE;
        if (frame->observation.all_tags) {
            if (frame->next < node->branch_len)
                child = node->branches[frame->next++].child;
        } else {
            while (frame->next < frame->observation.count && child == NODE_NONE) {
                bool found;
                uint32_t pos = branch_position(node,
                    frame->observation.tags[frame->next++], &found);
                if (found) child = node->branches[pos].child;
                else if (stats) stats->branch_misses++;
            }
        }
        if (child == NODE_NONE && !frame->free_done) {
            frame->free_done = true;
            bool include_free = frame->observation.include_free;
#ifdef CETTA_CODE_TREE_MUTATION_SKIP_FREE
            include_free = include_free && frame->observation.all_tags;
#endif
            if (include_free)
                child = node->free_child;
        }
        if (child == NODE_NONE) {
            depth--;
        } else {
            cursor->stack[depth++] = (TraversalFrame){.node = child};
        }
    }
    uint32_t written = 0u;
    for (uint32_t i = 0u; i < tree->occurrence_capacity; i++)
        if (cursor->marks[i]) cursor->output[written++] = i;
    *out = cursor->output;
    *count = written;
    return true;
}

static bool observe_tag(void *context, uint32_t coordinate,
                        CettaCodeTreeObservation *observation) {
    const uint32_t *tags = context;
    if (!tags) return false;
    *observation = (CettaCodeTreeObservation){
        .tags = &tags[coordinate], .count = 1u,
        .all_tags = tags[coordinate] == CETTA_CODE_TREE_UNKNOWN,
        .include_free = true,
    };
    return true;
}

bool cetta_code_tree_traverse(const CettaCodeTree *tree,
                              const uint32_t *observation,
                              uint8_t *marks,
                              CettaCodeTreeStats *stats) {
    if (!tree || !marks) return false;
    CettaCodeTreeCursor *cursor = cetta_code_tree_cursor_new(tree);
    const uint32_t *selected = NULL;
    uint32_t count = 0u;
    bool ok = cetta_code_tree_select_observed(tree, cursor, observe_tag,
        (void *)observation, &selected, &count, stats);
    if (ok)
        for (uint32_t i = 0u; i < count; i++) marks[selected[i]] = 1u;
    cetta_code_tree_cursor_free(cursor);
    return ok;
}

bool cetta_code_tree_select(const CettaCodeTree *tree,
                           const uint32_t *observation, uint32_t *out,
                           uint32_t *count, CettaCodeTreeStats *stats) {
    if (count) *count = 0u;
    if (!tree || !out || !count) return false;
    CettaCodeTreeCursor *cursor = cetta_code_tree_cursor_new(tree);
    const uint32_t *selected = NULL;
    bool ok = cetta_code_tree_select_observed(tree, cursor, observe_tag,
        (void *)observation, &selected, count, stats);
    if (ok && *count) memcpy(out, selected, *count * sizeof(*out));
    cetta_code_tree_cursor_free(cursor);
    return ok;
}

bool cetta_code_tree_insert(CettaCodeTree *tree, uint32_t index,
                            const uint32_t *pattern) {
    if (!tree || index == UINT32_MAX || (tree->coordinate_count && !pattern))
        return false;
    /*
     * Allocation failure part-way would leave created but empty interior
     * nodes on the path; that preserves every law (an empty branch is the
     * empty subtree), so no rollback is needed.
     */
    return insert_path(tree, index, pattern);
}

uint32_t cetta_code_tree_remove(CettaCodeTree *tree, uint32_t index) {
    if (!tree)
        return 0u;
    uint32_t removed = 0u;
    for (uint32_t n = 0u; n < tree->node_len; n++) {
        Node *node = &tree->nodes[n];
        if (node->depth != tree->coordinate_count)
            continue;
        uint32_t kept = 0u;
        for (uint32_t i = 0u; i < node->id_len; i++) {
            if (node->ids[i] == index)
                removed++;
            else
                node->ids[kept++] = node->ids[i];
        }
        node->id_len = kept;
    }
    return removed;
}

void cetta_code_tree_reference_marks(uint32_t coordinate_count,
                                     const uint32_t *const *patterns,
                                     uint32_t occurrence_count,
                                     const uint32_t *observation,
                                     uint8_t *marks) {
    for (uint32_t i = 0u; i < occurrence_count; i++) {
        bool refuted = false;
        for (uint32_t d = 0u; d < coordinate_count && !refuted; d++) {
            uint32_t o = observation[d];
            uint32_t p = patterns[i][d];
            refuted = o != CETTA_CODE_TREE_UNKNOWN && p != CETTA_CODE_TREE_UNKNOWN && o != p;
        }
        marks[i] = refuted ? 0u : 1u;
    }
}

uint32_t cetta_code_tree_residual_coordinates(uint32_t coordinate_count,
                                              const uint32_t *observation,
                                              uint32_t *out) {
    uint32_t count = 0u;
    for (uint32_t d = 0u; d < coordinate_count; d++)
        if (observation[d] == CETTA_CODE_TREE_UNKNOWN)
            out[count++] = d;
    return count;
}
