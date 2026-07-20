#include "prime_need.h"

#include <stdatomic.h>
#include <stdlib.h>

struct PrimeNeedFrame {
    const PrimeNeedFrame *parent;
    uint64_t session_id;
    uint64_t serial;
    uint64_t thunk_id;
    uint64_t depth;
    PrimeNeedCellState state;
    Atom *payload;
};

static _Atomic uint64_t g_prime_need_next_session = 1u;
static _Atomic uint64_t g_prime_need_next_serial = 1u;
static _Atomic uint64_t g_prime_need_next_thunk = 1u;

static uint64_t prime_need_fresh_nonzero(_Atomic uint64_t *counter) {
    uint64_t value = atomic_fetch_add_explicit(
        counter, 1u, memory_order_relaxed);
    if (value != 0u)
        return value;
    return atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}

void prime_need_snapshot_init(PrimeNeedSnapshot *snapshot) {
    if (!snapshot)
        return;
    snapshot->top = NULL;
    snapshot->session_id = 0u;
}

bool prime_need_snapshot_present(const PrimeNeedSnapshot *snapshot) {
    return snapshot && snapshot->session_id != 0u;
}

bool prime_need_snapshot_begin(PrimeNeedSnapshot *snapshot) {
    if (!snapshot)
        return false;
    if (prime_need_snapshot_present(snapshot))
        return true;
    snapshot->top = NULL;
    snapshot->session_id = prime_need_fresh_nonzero(
        &g_prime_need_next_session);
    return snapshot->session_id != 0u;
}

static const PrimeNeedFrame *prime_need_frame_at_depth(
    const PrimeNeedFrame *frame, uint64_t depth) {
    while (frame && frame->depth > depth)
        frame = frame->parent;
    return frame && frame->depth == depth ? frame : NULL;
}

bool prime_need_snapshot_is_ancestor(const PrimeNeedSnapshot *ancestor,
                                     const PrimeNeedSnapshot *descendant) {
    if (!ancestor || !descendant)
        return false;
    if (!prime_need_snapshot_present(ancestor))
        return true;
    if (!prime_need_snapshot_present(descendant) ||
        ancestor->session_id != descendant->session_id)
        return false;
    if (!ancestor->top)
        return true;
    if (!descendant->top ||
        ancestor->top->depth > descendant->top->depth)
        return false;
    const PrimeNeedFrame *at_depth = prime_need_frame_at_depth(
        descendant->top, ancestor->top->depth);
    return at_depth && at_depth->serial == ancestor->top->serial;
}

bool prime_need_snapshot_merge(PrimeNeedSnapshot *dst,
                               const PrimeNeedSnapshot *src) {
    if (!dst || !src || !prime_need_snapshot_present(src))
        return dst != NULL;
    if (!prime_need_snapshot_present(dst) ||
        prime_need_snapshot_is_ancestor(dst, src)) {
        *dst = *src;
        return true;
    }
#ifdef CETTA_PRIME_NEED_MUTATION_SIBLING_MERGE
    /* Deliberately invalid mutation: sibling refinements are not ordered. */
    *dst = *src;
    return true;
#endif
    return prime_need_snapshot_is_ancestor(src, dst);
}

static bool prime_need_snapshot_push(Arena *owner,
                                     const PrimeNeedSnapshot *base,
                                     uint64_t serial,
                                     uint64_t thunk_id,
                                     PrimeNeedCellState state,
                                     Atom *payload,
                                     PrimeNeedSnapshot *out) {
    if (!owner || !base || !out || !prime_need_snapshot_present(base) ||
        thunk_id == 0u)
        return false;
    PrimeNeedFrame *frame = arena_alloc(owner, sizeof(*frame));
    if (!frame)
        return false;
    frame->parent = base->top;
    frame->session_id = base->session_id;
    frame->serial = serial ? serial : prime_need_fresh_nonzero(
        &g_prime_need_next_serial);
    frame->thunk_id = thunk_id;
    frame->depth = base->top ? base->top->depth + 1u : 1u;
    frame->state = state;
    frame->payload = payload;
    out->top = frame;
    out->session_id = base->session_id;
    return frame->serial != 0u;
}

bool prime_need_snapshot_lookup(const PrimeNeedSnapshot *snapshot,
                                uint64_t thunk_id,
                                PrimeNeedCellView *out) {
    if (!snapshot || !out || !prime_need_snapshot_present(snapshot) ||
        thunk_id == 0u)
        return false;
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        if (frame->session_id == snapshot->session_id &&
            frame->thunk_id == thunk_id) {
            out->state = frame->state;
            out->payload = frame->payload;
            return true;
        }
    }
    return false;
}

bool prime_need_snapshot_allocate(Arena *owner,
                                  const PrimeNeedSnapshot *base,
                                  Atom *term,
                                  PrimeNeedSnapshot *out,
                                  uint64_t *out_thunk_id) {
    if (!owner || !base || !out || !out_thunk_id || !term ||
        !prime_need_snapshot_present(base))
        return false;
    uint64_t thunk_id = prime_need_fresh_nonzero(&g_prime_need_next_thunk);
    if (!prime_need_snapshot_push(owner, base, 0u, thunk_id,
                                  PRIME_NEED_UNEVALUATED, term, out))
        return false;
    *out_thunk_id = thunk_id;
    return true;
}

bool prime_need_snapshot_update(Arena *owner,
                                const PrimeNeedSnapshot *base,
                                uint64_t thunk_id,
                                PrimeNeedCellState state,
                                Atom *payload,
                                PrimeNeedSnapshot *out) {
    PrimeNeedCellView previous;
    if (!owner || !base || !out ||
        !prime_need_snapshot_lookup(base, thunk_id, &previous))
        return false;
    if (state == PRIME_NEED_UNEVALUATED && !payload)
        payload = previous.payload;
    if ((state == PRIME_NEED_UNEVALUATED || state == PRIME_NEED_VALUE ||
         state == PRIME_NEED_FAULT) && !payload)
        return false;
#ifdef CETTA_PRIME_NEED_MUTATION_FAULT_NOT_CACHED
    if (state == PRIME_NEED_FAULT)
        state = PRIME_NEED_UNEVALUATED;
#endif
    return prime_need_snapshot_push(owner, base, 0u, thunk_id, state,
                                    payload, out);
}

bool prime_need_snapshot_promote(Arena *dst,
                                 PrimeNeedSnapshot *snapshot) {
    if (!dst || !snapshot || !snapshot->top)
        return dst && snapshot;
    uint64_t depth = snapshot->top->depth;
    if (depth > SIZE_MAX / sizeof(const PrimeNeedFrame *))
        return false;
    const PrimeNeedFrame **path = malloc(sizeof(*path) * (size_t)depth);
    if (!path)
        return false;
    const PrimeNeedFrame *cursor = snapshot->top;
    for (uint64_t i = depth; i > 0u; i--) {
        if (!cursor) {
            free(path);
            return false;
        }
        path[i - 1u] = cursor;
        cursor = cursor->parent;
    }
    PrimeNeedSnapshot promoted = {.top = NULL,
                                  .session_id = snapshot->session_id};
    for (uint64_t i = 0u; i < depth; i++) {
        Atom *payload = path[i]->payload
            ? atom_deep_copy(dst, path[i]->payload) : NULL;
        if (path[i]->payload && !payload) {
            free(path);
            return false;
        }
        PrimeNeedSnapshot next;
        if (!prime_need_snapshot_push(dst, &promoted, path[i]->serial,
                                      path[i]->thunk_id, path[i]->state,
                                      payload, &next)) {
            free(path);
            return false;
        }
        promoted = next;
    }
    free(path);
    *snapshot = promoted;
    return true;
}

Atom *prime_need_ref(Arena *arena, const PrimeNeedSnapshot *snapshot,
                     uint64_t thunk_id) {
    if (!arena || !snapshot || !prime_need_snapshot_present(snapshot) ||
        snapshot->session_id > INT64_MAX || thunk_id == 0u ||
        thunk_id > INT64_MAX)
        return NULL;
    return atom_expr3(
        arena, atom_symbol(arena, "__prime_need_ref_v1"),
        atom_int(arena, (int64_t)snapshot->session_id),
        atom_int(arena, (int64_t)thunk_id));
}

bool prime_need_ref_parse(const Atom *atom, uint64_t *session_id,
                          uint64_t *thunk_id) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "__prime_need_ref_v1") ||
        atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT ||
        atom->expr.elems[2]->kind != ATOM_GROUNDED ||
        atom->expr.elems[2]->ground.gkind != GV_INT ||
        atom->expr.elems[1]->ground.ival <= 0 ||
        atom->expr.elems[2]->ground.ival <= 0)
        return false;
    if (session_id)
        *session_id = (uint64_t)atom->expr.elems[1]->ground.ival;
    if (thunk_id)
        *thunk_id = (uint64_t)atom->expr.elems[2]->ground.ival;
    return true;
}

bool prime_need_ref_belongs_to(const Atom *atom,
                               const PrimeNeedSnapshot *snapshot,
                               uint64_t *thunk_id) {
    uint64_t session = 0u;
    uint64_t id = 0u;
    if (!snapshot || !prime_need_ref_parse(atom, &session, &id) ||
        session != snapshot->session_id)
        return false;
    if (thunk_id)
        *thunk_id = id;
    return true;
}
