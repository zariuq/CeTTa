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


void cetta_code_tree_storage_census(const CettaCodeTree *tree, CettaStorageCensus *census) {
    if (!tree || !cetta_storage_object(census, tree, sizeof(*tree), CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census, tree->nodes, tree->node_cap, sizeof(*tree->nodes), 0u);
    for (uint32_t i = 0; i < tree->node_len; i++) {
        const Node *node = &tree->nodes[i];
        cetta_storage_array(census, node->branches, node->branch_cap, sizeof(*node->branches), 0u);
        cetta_storage_array(census, node->ids, node->id_cap, sizeof(*node->ids), 0u);
    }
}

void cetta_code_tree_cursor_storage_census(const CettaCodeTreeCursor *cursor, CettaStorageCensus *census) {
    if (!cursor || !cetta_storage_object(census, cursor, sizeof(*cursor), CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census, cursor->marks, cursor->mark_capacity, sizeof(*cursor->marks), 0u);
    cetta_storage_array(census, cursor->output, cursor->output_capacity, sizeof(*cursor->output), 0u);
    cetta_storage_array(census, cursor->stack, cursor->stack_capacity, sizeof(*cursor->stack), 0u);
}

/* A completed selection exposes no persistent traversal stack. Copy the
 * immutable routing tree and allocate private scratch at the retained sizes. */
static bool code_tree_copy_array(void **out, const void *source,
    size_t length, size_t capacity, size_t width) {
    *out = NULL;
    if (length > capacity || (capacity && !source) ||
        !width || capacity > SIZE_MAX / width)
        return false;
    if (!capacity) return source == NULL;
    void *copy = calloc(capacity, width);
    if (!copy) return false;
    if (length) memcpy(copy, source, length * width);
    *out = copy;
    return true;
}

CettaCodeTree *cetta_code_tree_copy(const CettaCodeTree *source) {
    if (!source || source->node_len > source->node_cap ||
        source->root >= source->node_len)
        return NULL;
    CettaCodeTree *image = calloc(1u, sizeof(*image));
    if (!image) return NULL;
    image->coordinate_count = source->coordinate_count;
    image->occurrence_capacity = source->occurrence_capacity;
    image->root = source->root;
    image->node_cap = source->node_cap;
    if (!code_tree_copy_array((void **)&image->nodes, source->nodes,
            0u, source->node_cap, sizeof(*source->nodes)))
        goto fail;
    image->node_len = source->node_len;
    for (uint32_t i = 0u; i < source->node_len; i++) {
        const Node *s = &source->nodes[i];
        Node *d = &image->nodes[i];
        d->depth = s->depth; d->free_child = s->free_child;
        d->branch_len = s->branch_len; d->branch_cap = s->branch_cap;
        d->id_len = s->id_len; d->id_cap = s->id_cap;
        if (!code_tree_copy_array((void **)&d->branches, s->branches,
                s->branch_len, s->branch_cap, sizeof(*s->branches)) ||
            !code_tree_copy_array((void **)&d->ids, s->ids,
                s->id_len, s->id_cap, sizeof(*s->ids)))
            goto fail;
    }
    return image;
fail:
    cetta_code_tree_free(image);
    return NULL;
}

CettaCodeTreeCursor *cetta_code_tree_cursor_copy_quiescent(
    const CettaCodeTreeCursor *source) {
    if (!source) return NULL;
    CettaCodeTreeCursor *image = calloc(1u, sizeof(*image));
    if (!image) return NULL;
    image->mark_capacity = source->mark_capacity;
    image->output_capacity = source->output_capacity;
    image->stack_capacity = source->stack_capacity;
    if (!code_tree_copy_array((void **)&image->marks, source->marks,
            0u, source->mark_capacity, sizeof(*source->marks)) ||
        !code_tree_copy_array((void **)&image->output, source->output,
            0u, source->output_capacity, sizeof(*source->output)) ||
        !code_tree_copy_array((void **)&image->stack, source->stack,
            0u, source->stack_capacity, sizeof(*source->stack))) {
        cetta_code_tree_cursor_free(image);
        return NULL;
    }
    return image;
}

/* Selection has returned and no caller retains its borrowed result array.
 * The next selection initializes marks and stack entries before reading them. */
bool cetta_code_tree_checkpoint_compare(const CettaCodeTree *a,
    const CettaCodeTreeCursor *ac,const CettaCodeTree *b,
    const CettaCodeTreeCursor *bc,const CettaCheckpointCompare *read) {
    if(!cetta_checkpoint_compare_valid(read))return false;
#define CT_S(n,x,y) do { if(!read->scalar(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define CT_F(n,x,y) do { if(!read->physical(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define CT_P(n,x,y) do { if(!read->address(read->context,n,x,y))return false; } while(0)
    CT_P("code_tree",a,b);CT_P("code_tree.cursor",ac,bc);
    if(!a||!b)return !a&&!b&&!ac&&!bc;
    if(!cetta_checkpoint_extent(a->nodes,a->node_len,a->node_cap,b->nodes,b->node_len,b->node_cap))return false;
    CT_S("code_tree.coordinates",a->coordinate_count,b->coordinate_count);
    CT_S("code_tree.occurrences",a->occurrence_capacity,b->occurrence_capacity);
    CT_S("code_tree.root",a->root,b->root);CT_S("code_tree.nodes",a->node_len,b->node_len);
    CT_F("code_tree.capacity",a->node_cap,b->node_cap);
    for(uint32_t i=0u;i<a->node_len;i++) {
        const Node *x=&a->nodes[i],*y=&b->nodes[i];
        if(!cetta_checkpoint_extent(x->branches,x->branch_len,x->branch_cap,
            y->branches,y->branch_len,y->branch_cap)||
            !cetta_checkpoint_extent(x->ids,x->id_len,x->id_cap,y->ids,y->id_len,y->id_cap))return false;
        CT_S("code_tree.node_index",i,i);CT_S("code_tree.depth",x->depth,y->depth);
        CT_S("code_tree.free",x->free_child,y->free_child);
        CT_S("code_tree.branches",x->branch_len,y->branch_len);CT_F("code_tree.branch_capacity",x->branch_cap,y->branch_cap);
        CT_P("code_tree.branch_storage",x->branches,y->branches);
        for(uint32_t j=0u;j<x->branch_len;j++) {
            CT_S("code_tree.branch_index",j,j);CT_S("code_tree.tag",x->branches[j].tag,y->branches[j].tag);
            CT_S("code_tree.child",x->branches[j].child,y->branches[j].child);
        }
        CT_S("code_tree.ids",x->id_len,y->id_len);CT_F("code_tree.id_capacity",x->id_cap,y->id_cap);
        CT_P("code_tree.id_storage",x->ids,y->ids);
        for(uint32_t j=0u;j<x->id_len;j++)CT_S("code_tree.id",x->ids[j],y->ids[j]);
    }
    if(!ac||!bc)return !ac&&!bc;
    CT_F("code_tree.mark_capacity",ac->mark_capacity,bc->mark_capacity);
    CT_F("code_tree.output_capacity",ac->output_capacity,bc->output_capacity);
    CT_F("code_tree.stack_capacity",ac->stack_capacity,bc->stack_capacity);
    CT_F("code_tree.marks_storage",(uintptr_t)ac->marks,(uintptr_t)bc->marks);
    CT_F("code_tree.output_storage",(uintptr_t)ac->output,(uintptr_t)bc->output);
    CT_F("code_tree.stack_storage",(uintptr_t)ac->stack,(uintptr_t)bc->stack);
    return (!ac->mark_capacity||ac->marks)&&(!bc->mark_capacity||bc->marks)&&
        (!ac->output_capacity||ac->output)&&(!bc->output_capacity||bc->output)&&
        (!ac->stack_capacity||ac->stack)&&(!bc->stack_capacity||bc->stack);
#undef CT_S
#undef CT_F
#undef CT_P
}
