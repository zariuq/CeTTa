#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Atom *node;
    CettaExprIndex next, index;
    bool entered;
} AtomWalkFrame;
struct AtomWalk {
    AtomWalkFrame inline_frames[32], *frames;
    size_t len, cap;
    AtomVisitSet active, completed;
    bool unique, can_prune;
};

static bool atom_walk_seen(const AtomVisitSet *set, const Atom *node) {
    if (!set->cap) return false;
    size_t slot = atom_visit_slot(node, set->cap);
    while (set->slots[slot]) {
        if (set->slots[slot] == node) return true;
        slot = (slot + 1u) & (set->cap - 1u);
    }
    return false;
}
/* Removing a gray path entry allocates nothing. Reinsert its following
 * cluster so every remaining lookup still reaches the same member. */
static void atom_walk_erase(AtomVisitSet *set, const Atom *node) {
    size_t slot = atom_visit_slot(node, set->cap);
    while (set->slots[slot] != node)
        slot = (slot + 1u) & (set->cap - 1u);
    set->slots[slot] = NULL;
    --set->len;
    for (slot = (slot + 1u) & (set->cap - 1u); set->slots[slot];
         slot = (slot + 1u) & (set->cap - 1u)) {
        const Atom *moved = set->slots[slot];
        set->slots[slot] = NULL;
        size_t target = atom_visit_slot(moved, set->cap);
        while (set->slots[target]) target = (target + 1u) & (set->cap - 1u);
        set->slots[target] = moved;
    }
}
static bool atom_walk_reserve(AtomWalk *walk) {
    if (walk->len < walk->cap) return true;
    if (walk->cap > SIZE_MAX / 2u || walk->cap * 2u > SIZE_MAX / sizeof(*walk->frames)) return false;
    size_t capacity = walk->cap * 2u;
    AtomWalkFrame *frames = walk->frames == walk->inline_frames
        ? malloc(capacity * sizeof(*frames))
        : realloc(walk->frames, capacity * sizeof(*frames));
    if (!frames) return false;
    if (walk->frames == walk->inline_frames)
        memcpy(frames, walk->inline_frames, walk->len * sizeof(*frames));
    walk->frames = frames;
    walk->cap = capacity;
    return true;
}
static void atom_walk_event(AtomWalk *walk, AtomWalkEvent *out) {
    AtomWalkFrame *frame = &walk->frames[walk->len - 1u];
    *out = (AtomWalkEvent){frame->node,
        walk->len > 1u ? walk->frames[walk->len - 2u].node : NULL,
        frame->index, walk->len - 1u};
}
AtomWalk *atom_walk_new(const Atom *root, bool unique) {
    AtomWalk *walk = calloc(1u, sizeof(*walk));
    if (!walk) return NULL;
    walk->frames = walk->inline_frames;
    walk->cap = sizeof(walk->inline_frames) / sizeof(walk->inline_frames[0]);
    walk->frames[walk->len++] = (AtomWalkFrame){.node = root};
    walk->unique = unique;
    return walk;
}
AtomWalkStep atom_walk_next(AtomWalk *walk, AtomWalkEvent *out) {
    if (out) *out = (AtomWalkEvent){0};
    if (!walk || !out) return ATOM_WALK_INVALID;
    walk->can_prune = false;
    for (;;) {
        if (!walk->len) return ATOM_WALK_DONE;
        AtomWalkFrame *frame = &walk->frames[walk->len - 1u];
        const Atom *node = frame->node;
        if (!node || (node->kind != ATOM_SYMBOL && node->kind != ATOM_VAR &&
            node->kind != ATOM_GROUNDED && node->kind != ATOM_EXPR) ||
            (node->kind == ATOM_EXPR && node->expr.len && !node->expr.elems))
            return ATOM_WALK_INVALID;
        if (!frame->entered) {
            if (atom_walk_seen(&walk->active, node)) return ATOM_WALK_CYCLE;
            if (walk->unique && atom_walk_seen(&walk->completed, node)) {
                atom_walk_event(walk, out); --walk->len;
                return ATOM_WALK_SHARED;
            }
            bool fault = false;
            (void)atom_visit_set_add(&walk->active, node, &fault);
            if (fault) return ATOM_WALK_RESOURCE;
            frame->entered = true;
            walk->can_prune = true;
            atom_walk_event(walk, out);
            return ATOM_WALK_ENTER;
        }
        if (node->kind == ATOM_EXPR && frame->next < node->expr.len) {
            CettaExprIndex child = frame->next;
            if (!atom_walk_reserve(walk)) return ATOM_WALK_RESOURCE;
            frame = &walk->frames[walk->len - 1u];
            ++frame->next;
            walk->frames[walk->len++] = (AtomWalkFrame){
                .node = node->expr.elems[child], .index = child};
            continue;
        }
        if (walk->unique && !atom_walk_seen(&walk->completed, node)) {
            bool fault = false;
            (void)atom_visit_set_add(&walk->completed, node, &fault);
            if (fault) return ATOM_WALK_RESOURCE;
        }
        atom_walk_erase(&walk->active, node);
        atom_walk_event(walk, out); --walk->len;
        return ATOM_WALK_LEAVE;
    }
}
bool atom_walk_prune(AtomWalk *walk) {
    if (!walk || !walk->can_prune || !walk->len) return false;
    AtomWalkFrame *frame = &walk->frames[walk->len - 1u];
    if (frame->node->kind == ATOM_EXPR) frame->next = frame->node->expr.len;
    walk->can_prune = false;
    return true;
}
void atom_walk_free(AtomWalk *walk) {
    if (!walk) return;
    if (walk->frames != walk->inline_frames) free(walk->frames);
    atom_visit_set_free(&walk->active);
    atom_visit_set_free(&walk->completed);
    free(walk);
}
