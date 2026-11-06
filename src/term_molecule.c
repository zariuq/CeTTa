#include "term_molecule.h"

#include <stdlib.h>
#include <string.h>

/* The deepest open term a key is built for: as deep as alpha keys nest. */
#define MOL_KEY_DEPTH 512u

void cetta_mol_store_init(CettaMolStore *store, TermUniverse *universe) {
    memset(store, 0, sizeof(*store));
    store->universe = universe;
}

void cetta_mol_store_free(CettaMolStore *store) {
    free(store->cells);
    free(store->cell_frames);
    free(store->frame_bound);
    free(store->vars);
    free(store->var_log);
    free(store->trail);
    free(store->stack);
    free(store->exact);
    memset(store, 0, sizeof(*store));
}

static bool mol_reserve(void **items, uint32_t *cap, uint32_t need,
                        size_t size) {
    if (need <= *cap)
        return true;
    uint32_t grown = *cap ? *cap : 16u;
    while (grown < need) {
        if (grown > UINT32_MAX / 2u)
            return false;
        grown *= 2u;
    }
    void *moved = realloc(*items, (size_t)grown * size);
    if (!moved)
        return false;
    *items = moved;
    *cap = grown;
    return true;
}

uint32_t cetta_mol_frame(CettaMolStore *store) {
    /* Without room the frame counts as bound throughout: its cells are
     * then always checked. */
    uint32_t old_cap = store->frame_cap;
    if (store->frames < UINT32_MAX - 1u &&
        mol_reserve((void **)&store->frame_bound, &store->frame_cap,
                    store->frames + 1u, sizeof(*store->frame_bound))) {
        if (store->frame_cap > old_cap) {
            memset(store->frame_bound + old_cap, 0,
                   (size_t)(store->frame_cap - old_cap) * sizeof(*store->frame_bound));
            /* A refused earlier growth may have issued frames whose
             * bindings could not be counted. Admit them with their actual
             * counts, before allowing the free-frame occurs shortcut. */
            for (uint32_t cell = 0u; cell < store->cell_len; cell++) {
                uint32_t frame = store->cell_frames[cell];
                if (frame >= old_cap && frame < store->frame_cap &&
                    (store->cells[cell].id != CETTA_ATOM_ID_NONE ||
                     store->cells[cell].at != cell))
                    store->frame_bound[frame]++;
            }
        }
        store->frame_bound[store->frames] = 0u;
    }
    return store->frames++;
}

/* Whether no cell of FRAME is bound. */
static inline bool mol_frame_free(const CettaMolStore *store, uint32_t frame) {
    return frame < store->frames && frame < store->frame_cap &&
           store->frame_bound[frame] == 0u;
}

/* The cells and the frames that read them share one capacity, published
 * once both hold it: a store refused a growth stays one a later branch may
 * use. */
static CettaMolResult mol_cell_of(CettaMolStore *store, uint32_t frame,
                                  CettaMol *cell_out) {
    uint32_t cells_cap = store->cell_cap;
    uint32_t frames_cap = store->cell_cap;
    if (store->cell_len == UINT32_MAX - 1u ||
        !mol_reserve((void **)&store->cells, &cells_cap,
                     store->cell_len + 1u, sizeof(*store->cells)) ||
        !mol_reserve((void **)&store->cell_frames, &frames_cap,
                     store->cell_len + 1u, sizeof(*store->cell_frames)))
        return CETTA_MOL_DECLINE;
    store->cell_cap = cells_cap;
    uint32_t cell = store->cell_len++;
    store->cells[cell] = (CettaMol){CETTA_ATOM_ID_NONE, cell};
    store->cell_frames[cell] = frame;
    *cell_out = store->cells[cell];
    return CETTA_MOL_OK;
}

CettaMolResult cetta_mol_cell(CettaMolStore *store, CettaMol *cell_out) {
    return mol_cell_of(store, CETTA_MOL_GROUND, cell_out);
}

/* ── The cells of frames' variables ── */

static inline uint32_t mol_var_slot(uint32_t frame, AtomId var, uint32_t cap) {
    uint64_t mixed = ((uint64_t)frame << 32 | var) *
                     UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(mixed >> 32) & (cap - 1u);
}

static bool mol_vars_grow(CettaMolStore *store) {
    uint32_t cap = store->var_cap ? store->var_cap * 2u : 64u;
    CettaMolVar *grown = malloc((size_t)cap * sizeof(*grown));
    if (!grown)
        return false;
    for (uint32_t slot = 0u; slot < cap; slot++)
        grown[slot].var = CETTA_ATOM_ID_NONE;
    /* Entries are put back in the order they were made, so that taking
     * them out last first leaves each probe sequence whole. */
    for (uint32_t i = 0u; i < store->var_len; i++) {
        CettaMolVar entry = store->vars[store->var_log[i]];
        uint32_t slot = mol_var_slot(entry.frame, entry.var, cap);
        while (grown[slot].var != CETTA_ATOM_ID_NONE)
            slot = (slot + 1u) & (cap - 1u);
        grown[slot] = entry;
        store->var_log[i] = slot;
    }
    free(store->vars);
    store->vars = grown;
    store->var_cap = cap;
    return true;
}

/* The cell variable VAR stands for in FRAME, made when first met. */
static bool mol_var_cell(CettaMolStore *store, uint32_t frame, AtomId var,
                         uint32_t *cell_out) {
    if (store->var_cap) {
        for (uint32_t slot = mol_var_slot(frame, var, store->var_cap);;
             slot = (slot + 1u) & (store->var_cap - 1u)) {
            const CettaMolVar *entry = &store->vars[slot];
            if (entry->var == CETTA_ATOM_ID_NONE)
                break;
            if (entry->var == var && entry->frame == frame) {
                *cell_out = entry->cell;
                return true;
            }
        }
    }
    if ((store->var_len + 1u > store->var_cap / 2u && !mol_vars_grow(store)) ||
        !mol_reserve((void **)&store->var_log, &store->var_log_cap,
                     store->var_len + 1u, sizeof(*store->var_log)))
        return false;
    CettaMol cell;
    if (mol_cell_of(store, frame, &cell) != CETTA_MOL_OK)
        return false;
    uint32_t slot = mol_var_slot(frame, var, store->var_cap);
    while (store->vars[slot].var != CETTA_ATOM_ID_NONE)
        slot = (slot + 1u) & (store->var_cap - 1u);
    store->vars[slot] = (CettaMolVar){frame, cell.at, var};
    store->var_log[store->var_len++] = slot;
    *cell_out = cell.at;
    return true;
}

void cetta_mol_undo(CettaMolStore *store, CettaMolMark mark) {
    while (store->trail_len > mark.trail) {
        uint32_t cell = store->trail[--store->trail_len];
        store->cells[cell] = (CettaMol){CETTA_ATOM_ID_NONE, cell};
        uint32_t frame = store->cell_frames[cell];
        if (frame < store->frame_cap)
            store->frame_bound[frame]--;
    }
    while (store->var_len > mark.vars)
        store->vars[store->var_log[--store->var_len]].var = CETTA_ATOM_ID_NONE;
    store->cell_len = mark.cells;
    store->frames = mark.frames;
}

/* ── Reading ── */

/* X with its bound cells followed: an unbound bare cell, or a term that
 * is no variable.  False without room or for a variable read under no
 * frame. */
static inline bool mol_deref_header(CettaMolStore *store, CettaMol *x,
                                     const CettaTermHdr **header) {
    for (;;) {
        if (x->id == CETTA_ATOM_ID_NONE) {
            CettaMol value = store->cells[x->at];
            if (value.id == CETTA_ATOM_ID_NONE && value.at == x->at) {
                *header = NULL;
                return true;
            }
            *x = value;
            continue;
        }
        const CettaTermHdr *hdr = tu_hdr(store->universe, x->id);
        if (!hdr)
            return false;
        if (hdr->tag != ATOM_VAR) {
            *header = hdr;
            return true;
        }
        uint32_t cell = 0u;
        if (x->at == CETTA_MOL_GROUND ||
            !mol_var_cell(store, x->at, x->id, &cell))
            return false;
        *x = (CettaMol){CETTA_ATOM_ID_NONE, cell};
    }
}

static bool mol_deref(CettaMolStore *store, CettaMol *x) {
    const CettaTermHdr *header;
    return mol_deref_header(store, x, &header);
}

static inline uint32_t mol_exact_slot(AtomId id, uint32_t cap) {
    return (uint32_t)(((uint64_t)id * UINT64_C(0x9e3779b97f4a7c15)) >> 32) &
           (cap - 1u);
}

static bool mol_exact_known(const CettaMolStore *store, AtomId id) {
    if (!store->exact_cap)
        return false;
    for (uint32_t slot = mol_exact_slot(id, store->exact_cap);;
         slot = (slot + 1u) & (store->exact_cap - 1u)) {
        if (store->exact[slot] == id)
            return true;
        if (store->exact[slot] == CETTA_ATOM_ID_NONE)
            return false;
    }
}

/* Remember a ground expression known exact; without room nothing is kept,
 * which costs only a walk again. */
static void mol_exact_note(CettaMolStore *store, AtomId id) {
    if (store->exact_len + 1u > store->exact_cap / 2u) {
        uint32_t cap = store->exact_cap ? store->exact_cap * 2u : 64u;
        AtomId *grown = malloc((size_t)cap * sizeof(*grown));
        if (!grown)
            return;
        for (uint32_t slot = 0u; slot < cap; slot++)
            grown[slot] = CETTA_ATOM_ID_NONE;
        for (uint32_t old = 0u; old < store->exact_cap; old++) {
            AtomId kept = store->exact[old];
            if (kept == CETTA_ATOM_ID_NONE)
                continue;
            uint32_t slot = mol_exact_slot(kept, cap);
            while (grown[slot] != CETTA_ATOM_ID_NONE)
                slot = (slot + 1u) & (cap - 1u);
            grown[slot] = kept;
        }
        free(store->exact);
        store->exact = grown;
        store->exact_cap = cap;
    }
    uint32_t slot = mol_exact_slot(id, store->exact_cap);
    while (store->exact[slot] != CETTA_ATOM_ID_NONE) {
        if (store->exact[slot] == id)
            return;
        slot = (slot + 1u) & (store->exact_cap - 1u);
    }
    store->exact[slot] = id;
    store->exact_len++;
}

/* Whether the ground term ID equals only itself: a symbol, an integer, a
 * truth value, a string, or a plain expression of them. */
static bool mol_exact(CettaMolStore *store, AtomId id, uint32_t depth) {
    const CettaTermHdr *hdr = tu_hdr(store->universe, id);
    if (!hdr || depth > MOL_KEY_DEPTH)
        return false;
    if (hdr->tag == ATOM_SYMBOL)
        return true;
    if (hdr->tag == ATOM_GROUNDED)
        return hdr->subtag == GV_INT || hdr->subtag == GV_BOOL ||
               hdr->subtag == GV_STRING;
    if (hdr->tag != ATOM_EXPR || !tu_plain_expr(store->universe, id, hdr))
        return false;
    if (mol_exact_known(store, id))
        return true;
    uint32_t len = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
    for (uint32_t c = 0u; c < len; c++) {
        if (!mol_exact(store, tu_child(store->universe, id, c), depth + 1u))
            return false;
    }
    mol_exact_note(store, id);
    return true;
}

static bool mol_push(CettaMolStore *store, uint32_t *len, CettaMol x) {
    if (!mol_reserve((void **)&store->stack, &store->stack_cap, *len + 1u,
                     sizeof(*store->stack)))
        return false;
    store->stack[(*len)++] = x;
    return true;
}

static bool mol_bind(CettaMolStore *store, uint32_t cell, CettaMol value) {
    if (!mol_reserve((void **)&store->trail, &store->trail_cap,
                     store->trail_len + 1u, sizeof(*store->trail)))
        return false;
    store->trail[store->trail_len++] = cell;
    store->cells[cell] = value;
    uint32_t frame = store->cell_frames[cell];
    if (frame < store->frame_cap)
        store->frame_bound[frame]++;
    return true;
}

/* Whether CELL occurs in X; DECLINE without room.  The walk uses the stack
 * above BASE. */
static CettaMolResult mol_occurs(CettaMolStore *store, uint32_t cell,
                                 CettaMol x, uint32_t base) {
    uint32_t len = base;
    if (!mol_push(store, &len, x))
        return CETTA_MOL_DECLINE;
    while (len > base) {
        CettaMol y = store->stack[--len];
        const CettaTermHdr *hdr;
        if (!mol_deref_header(store, &y, &hdr))
            return CETTA_MOL_DECLINE;
        if (y.id == CETTA_ATOM_ID_NONE) {
            if (y.at == cell)
                return CETTA_MOL_OK;
            continue;
        }
        if ((hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) == 0u)
            continue;
        if (hdr->tag != ATOM_EXPR || !tu_plain_expr(store->universe, y.id, hdr))
            return CETTA_MOL_DECLINE;
        uint32_t arity = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
        for (uint32_t c = 0u; c < arity; c++) {
            if (!mol_push(store, &len,
                          (CettaMol){tu_child_from_header(store->universe, hdr,
                                                         c), y.at}))
                return CETTA_MOL_DECLINE;
        }
    }
    return CETTA_MOL_FAIL;
}

CettaMolResult cetta_mol_touch(CettaMolStore *store, CettaMol x) {
    uint32_t len = 0u;
    if (!mol_push(store, &len, x))
        return CETTA_MOL_DECLINE;
    while (len > 0u) {
        CettaMol y = store->stack[--len];
        const CettaTermHdr *hdr;
        if (!mol_deref_header(store, &y, &hdr))
            return CETTA_MOL_DECLINE;
        if (y.id == CETTA_ATOM_ID_NONE)
            continue;
        if ((hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) == 0u)
            continue;
        if (hdr->tag != ATOM_EXPR || !tu_plain_expr(store->universe, y.id, hdr))
            return CETTA_MOL_DECLINE;
        uint32_t arity = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
        for (uint32_t c = 0u; c < arity; c++) {
            if (!mol_push(store, &len,
                          (CettaMol){tu_child_from_header(store->universe, hdr,
                                                         c), y.at}))
                return CETTA_MOL_DECLINE;
        }
    }
    return CETTA_MOL_OK;
}

/* Bind the unbound CELL to X, which is no bare cell, unless CELL occurs in
 * X. */
static CettaMolResult mol_bind_checked(CettaMolStore *store, uint32_t cell,
                                       CettaMol x, uint32_t base) {
    /* A term read under a frame none of whose cells is bound reaches only
     * that frame's cells, all unbound: a cell of another frame is not
     * among them. */
    if (x.at != CETTA_MOL_GROUND && store->cell_frames[cell] != x.at &&
        mol_frame_free(store, x.at))
        return mol_bind(store, cell, x) ? CETTA_MOL_OK : CETTA_MOL_DECLINE;
    CettaMolResult occurs = mol_occurs(store, cell, x, base);
    if (occurs != CETTA_MOL_FAIL)
        return occurs == CETTA_MOL_OK ? CETTA_MOL_FAIL : CETTA_MOL_DECLINE;
    return mol_bind(store, cell, x) ? CETTA_MOL_OK : CETTA_MOL_DECLINE;
}

/* The first pair and queued pairs use the same transition. Dereferencing
 * supplies the headers consumed here; no universe mutation occurs while a
 * pair's children are read. Bind/occurs/rollback retain their existing law. */
static inline CettaMolResult mol_unify_pair(CettaMolStore *store,
        CettaMol x, CettaMol y, uint32_t *len) {
    const CettaTermHdr *hx, *hy;
    if (!mol_deref_header(store, &x, &hx) || !mol_deref_header(store, &y, &hy))
        return CETTA_MOL_DECLINE;
    if (x.id == CETTA_ATOM_ID_NONE && y.id == CETTA_ATOM_ID_NONE) {
        if (x.at != y.at && !(x.at > y.at ? mol_bind(store, x.at, y)
                                         : mol_bind(store, y.at, x)))
            return CETTA_MOL_DECLINE;
        return CETTA_MOL_OK;
    }
    if (x.id == CETTA_ATOM_ID_NONE || y.id == CETTA_ATOM_ID_NONE)
        return x.id == CETTA_ATOM_ID_NONE
            ? mol_bind_checked(store, x.at, y, *len)
            : mol_bind_checked(store, y.at, x, *len);
    bool open_x = (hx->aux32 & CETTA_TERM_HDR_HAS_VARS) != 0u;
    bool open_y = (hy->aux32 & CETTA_TERM_HDR_HAS_VARS) != 0u;
    if (!open_x && !open_y) {
        if (!mol_exact(store, x.id, 0u) || !mol_exact(store, y.id, 0u))
            return CETTA_MOL_DECLINE;
        return x.id == y.id ? CETTA_MOL_OK : CETTA_MOL_FAIL;
    }
    if (x.id == y.id && x.at == y.at)
        return CETTA_MOL_OK;
    if (hx->tag != ATOM_EXPR || hy->tag != ATOM_EXPR) {
        AtomId leaf = hx->tag != ATOM_EXPR ? x.id : y.id;
        return mol_exact(store, leaf, 0u) ? CETTA_MOL_FAIL : CETTA_MOL_DECLINE;
    }
    if (!tu_plain_expr(store->universe, x.id, hx) ||
        !tu_plain_expr(store->universe, y.id, hy))
        return CETTA_MOL_DECLINE;
    uint32_t arity = hx->aux32 & CETTA_TERM_HDR_DATA_MASK;
    if (arity != (hy->aux32 & CETTA_TERM_HDR_DATA_MASK))
        return CETTA_MOL_FAIL;
    for (uint32_t c = arity; c-- > 0u;) {
        CettaMol left = {tu_child_from_header(store->universe, hx, c), x.at};
        CettaMol right = {tu_child_from_header(store->universe, hy, c), y.at};
        if (!mol_push(store, len, left) || !mol_push(store, len, right))
            return CETTA_MOL_DECLINE;
    }
    return CETTA_MOL_OK;
}

/* Unify the pairs on the stack above BASE. */
static CettaMolResult mol_unify_pending(CettaMolStore *store, uint32_t base,
                                        uint32_t len) {
    while (len > base) {
        CettaMol y = store->stack[--len];
        CettaMol x = store->stack[--len];
        CettaMolResult result = mol_unify_pair(store, x, y, &len);
        if (result != CETTA_MOL_OK)
            return result;
    }
    return CETTA_MOL_OK;
}

CettaMolResult cetta_mol_unify(CettaMolStore *store, CettaMol a, CettaMol b) {
    uint32_t len = 0u;
    CettaMolResult result = mol_unify_pair(store, a, b, &len);
    return result == CETTA_MOL_OK ? mol_unify_pending(store, 0u, len) : result;
}

CettaMolResult cetta_mol_unify_kept(CettaMolStore *store, CettaMol pattern,
                                    const CettaSkeleton *skeleton,
                                    const AtomId *holes, uint32_t frame) {
    /* The pattern's parts at the nodes still to read, last first. */
    CettaMol parts[CETTA_SKELETON_NODES];
    uint32_t pending = 0u;
    parts[pending++] = pattern;
    uint32_t i = 0u, h = 0u;
    while (i < skeleton->count) {
        if (pending == 0u)
            return CETTA_MOL_DECLINE;
        CettaMol part = parts[--pending];
        const CettaSkeletonNode *node = &skeleton->nodes[i];
        if (node->len == 0u) {
            i++;
            CettaMolResult unified = cetta_mol_unify(
                store, part, (CettaMol){holes[h++], frame});
            if (unified != CETTA_MOL_OK)
                return unified;
            continue;
        }
        const CettaTermHdr *hdr;
        if (!mol_deref_header(store, &part, &hdr))
            return CETTA_MOL_DECLINE;
        if (part.id == CETTA_ATOM_ID_NONE) {
            /* A variable meets the spine: it takes the subtree's record. */
            AtomId whole = cetta_skeleton_build_at(store->universe, skeleton,
                                                   &i, holes, &h);
            if (whole == CETTA_ATOM_ID_NONE)
                return CETTA_MOL_DECLINE;
            CettaMolResult unified =
                cetta_mol_unify(store, part, (CettaMol){whole, frame});
            if (unified != CETTA_MOL_OK)
                return unified;
            continue;
        }
        if (hdr->tag != ATOM_EXPR) {
            return mol_exact(store, part.id, 0u) ? CETTA_MOL_FAIL
                                                  : CETTA_MOL_DECLINE;
        }
        if (!tu_plain_expr(store->universe, part.id, hdr))
            return CETTA_MOL_DECLINE;
        if ((hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) != node->len)
            return CETTA_MOL_FAIL;
        i++;
        AtomId head_id = tu_child_from_header(store->universe, hdr, 0u);
        if (head_id != node->head_id) {
            CettaMolResult head = cetta_mol_unify(
                store, (CettaMol){head_id, part.at},
                (CettaMol){node->head_id, CETTA_MOL_GROUND});
            if (head != CETTA_MOL_OK)
                return head;
        }
        if (pending + node->len - 1u > CETTA_SKELETON_NODES)
            return CETTA_MOL_DECLINE;
        for (uint32_t c = node->len; c-- > 1u;)
            parts[pending++] =
                (CettaMol){tu_child_from_header(store->universe, hdr, c),
                           part.at};
    }
    return pending == 0u && h == skeleton->holes ? CETTA_MOL_OK
                                                 : CETTA_MOL_DECLINE;
}

CettaMolResult cetta_mol_align(CettaMolStore *store, CettaMol pattern,
                               const CettaSkeleton *skeleton,
                               CettaMolAligned *aligned) {
    CettaMol parts[CETTA_SKELETON_NODES];
    uint32_t pending = 0u, h = 0u;
    parts[pending++] = pattern;
    for (uint32_t i = 0u; i < skeleton->count; i++) {
        if (pending == 0u)
            return CETTA_MOL_DECLINE;
        CettaMol part = parts[--pending];
        const CettaSkeletonNode *node = &skeleton->nodes[i];
        if (node->len == 0u) {
            aligned->parts[h++] = part;
            continue;
        }
        const CettaTermHdr *hdr;
        if (!mol_deref_header(store, &part, &hdr) ||
            part.id == CETTA_ATOM_ID_NONE)
            return CETTA_MOL_DECLINE;
        if (hdr->tag != ATOM_EXPR)
            return mol_exact(store, part.id, 0u) ? CETTA_MOL_FAIL
                                                  : CETTA_MOL_DECLINE;
        if (!tu_plain_expr(store->universe, part.id, hdr))
            return CETTA_MOL_DECLINE;
        if ((hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) != node->len)
            return CETTA_MOL_FAIL;
        CettaMol head = {tu_child_from_header(store->universe, hdr, 0u),
                         part.at};
        if (!mol_deref(store, &head) || head.id == CETTA_ATOM_ID_NONE)
            return CETTA_MOL_DECLINE;
        if (head.id != node->head_id)
            return mol_exact(store, head.id, 0u) ? CETTA_MOL_FAIL
                                                  : CETTA_MOL_DECLINE;
        if (pending + node->len - 1u > CETTA_SKELETON_NODES)
            return CETTA_MOL_DECLINE;
        for (uint32_t c = node->len; c-- > 1u;)
            parts[pending++] =
                (CettaMol){tu_child_from_header(store->universe, hdr, c),
                           part.at};
    }
    if (pending != 0u || h != skeleton->holes)
        return CETTA_MOL_DECLINE;
    aligned->holes = h;
    return CETTA_MOL_OK;
}

CettaMolResult cetta_mol_aligned_stable_cells(CettaMolStore *store,
                                             CettaMolAligned *aligned,
                                             CettaMolMark lifetime) {
    if (lifetime.cells > store->cell_len || store->trail_len != lifetime.trail)
        return CETTA_MOL_DECLINE;
    for (uint32_t h = 0u; h < aligned->holes; h++) {
        CettaMol part = aligned->parts[h];
        if (!mol_deref(store, &part))
            return CETTA_MOL_DECLINE;
        if (part.id == CETTA_ATOM_ID_NONE && part.at < lifetime.cells)
            aligned->parts[h] = part;
    }
    return CETTA_MOL_OK;
}

CettaMolResult cetta_mol_unify_aligned(CettaMolStore *store,
                                       const CettaMolAligned *aligned,
                                       const AtomId *holes, uint32_t frame) {
    for (uint32_t h = 0u; h < aligned->holes; h++) {
        CettaMolResult unified = cetta_mol_unify(
            store, aligned->parts[h], (CettaMol){holes[h], frame});
        if (unified != CETTA_MOL_OK)
            return unified;
    }
    return CETTA_MOL_OK;
}

/* ── Instances ── */

/* Whether the stored term X, read under a frame none of whose cells is
 * bound, is its own key under NUMBERING: its variables, met in preorder,
 * are the alpha key variables numbered on from NUMBERING, each new one next
 * in order of first occurrence and each other one the cell NUMBERING gives
 * that number.  When it is, the new variables' cells join NUMBERING;
 * otherwise NUMBERING is as it was.  DECLINE without room. */
static CettaMolResult mol_own_key(CettaMolStore *store, CettaMol x,
                                  CettaMolNumbering *numbering, bool *own) {
    *own = false;
    uint32_t start = numbering->len;
    uint32_t len = 0u;
    bool fits = true;
    if (!mol_push(store, &len, x))
        return CETTA_MOL_DECLINE;
    while (fits && len > 0u) {
        CettaMol y = store->stack[--len];
        const CettaTermHdr *hdr = tu_hdr(store->universe, y.id);
        if (!hdr)
            return CETTA_MOL_DECLINE;
        if ((hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) == 0u)
            continue;
        if (hdr->tag == ATOM_VAR) {
            /* An alpha key's variable: unspelled, numbered, of no epoch. */
            VarId var = tu_var_id(store->universe, y.id);
            uint32_t cell = 0u;
            if (hdr->sym_or_head != SYMBOL_ID_NONE || hdr->subtag != 0u ||
                var.epoch.word != 0u || var.base == 0u ||
                var.base > (uint64_t)numbering->len + 1u) {
                fits = false;
            } else if (!mol_var_cell(store, y.at, y.id, &cell)) {
                return CETTA_MOL_DECLINE;
            } else {
                /* A variable numbered already keeps its number; a new one
                 * takes the next. */
                uint32_t at = 0u;
                while (at < numbering->len && numbering->cells[at] != cell)
                    at++;
                if (at < numbering->len)
                    fits = var.base == (uint64_t)at + 1u;
                else if (var.base != (uint64_t)numbering->len + 1u ||
                         numbering->len == TERM_UNIVERSE_ALPHA_KEY_VARS)
                    fits = false;
                else
                    numbering->cells[numbering->len++] = cell;
            }
            continue;
        }
        if (hdr->tag != ATOM_EXPR ||
            !tu_plain_expr(store->universe, y.id, hdr)) {
            fits = false;
            continue;
        }
        uint32_t arity = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
        for (uint32_t c = arity; c-- > 0u;) {
            if (!mol_push(store, &len,
                          (CettaMol){tu_child_from_header(store->universe, hdr,
                                                          c),
                                     y.at}))
                return CETTA_MOL_DECLINE;
        }
    }
    if (!fits)
        numbering->len = start;
    *own = fits;
    return CETTA_MOL_OK;
}

static CettaMolResult mol_key_at(CettaMolStore *store, CettaMol x,
                                 CettaMolNumbering *numbering, uint32_t depth,
                                 AtomId *key_out) {
    const CettaTermHdr *hdr;
    if (depth > MOL_KEY_DEPTH || !mol_deref_header(store, &x, &hdr))
        return CETTA_MOL_DECLINE;
    if (x.id == CETTA_ATOM_ID_NONE) {
        uint32_t ordinal = 0u;
        for (uint32_t i = 0u; !ordinal && i < numbering->len; i++) {
            if (numbering->cells[i] == x.at)
                ordinal = i + 1u;
        }
        if (!ordinal) {
            if (numbering->len == TERM_UNIVERSE_ALPHA_KEY_VARS)
                return CETTA_MOL_DECLINE;
            numbering->cells[numbering->len++] = x.at;
            ordinal = numbering->len;
        }
        *key_out = tu_intern_var(store->universe, SYMBOL_ID_NONE,
                                 var_id_from_base(ordinal));
        return *key_out != CETTA_ATOM_ID_NONE ? CETTA_MOL_OK
                                              : CETTA_MOL_DECLINE;
    }
    if ((hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) == 0u) {
        *key_out = x.id;
        return CETTA_MOL_OK;
    }
    if (hdr->tag != ATOM_EXPR || !tu_plain_expr(store->universe, x.id, hdr))
        return CETTA_MOL_DECLINE;
    /* A stored term read under a frame none of whose cells is bound is the
     * term itself, its own key when its variables already have the
     * numbers the key would give them. */
    if (x.at != CETTA_MOL_GROUND && mol_frame_free(store, x.at)) {
        bool own = false;
        if (mol_own_key(store, x, numbering, &own) != CETTA_MOL_OK)
            return CETTA_MOL_DECLINE;
        if (own) {
            *key_out = x.id;
            return CETTA_MOL_OK;
        }
        hdr = tu_hdr(store->universe, x.id);
    }
    /* The children are read before any part is stored: storing may move
     * the records. */
    uint32_t arity = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
    AtomId inline_children[16];
    AtomId *children = arity <= 16u
        ? inline_children : malloc(sizeof(*children) * (size_t)arity);
    if (!children)
        return CETTA_MOL_DECLINE;
    for (uint32_t c = 0u; c < arity; c++)
        children[c] = tu_child_from_header(store->universe, hdr, c);
    CettaMolResult made = CETTA_MOL_OK;
    for (uint32_t c = 0u; made == CETTA_MOL_OK && c < arity; c++)
        made = mol_key_at(store, (CettaMol){children[c], x.at}, numbering,
                          depth + 1u, &children[c]);
    if (made == CETTA_MOL_OK) {
        *key_out = tu_expr_from_ids(store->universe, children, arity);
        if (*key_out == CETTA_ATOM_ID_NONE)
            made = CETTA_MOL_DECLINE;
    }
    if (children != inline_children)
        free(children);
    return made;
}

CettaMolResult cetta_mol_key(CettaMolStore *store, CettaMol x,
                             CettaMolNumbering *numbering, AtomId *key_out) {
    return mol_key_at(store, x, numbering, 0u, key_out);
}

CettaMolResult cetta_mol_key_holes(CettaMolStore *store, CettaMol x,
                                   const CettaSkeleton *skeleton,
                                   AtomId *holes) {
    CettaMolNumbering numbering = {.len = 0u};
    CettaMol parts[CETTA_SKELETON_NODES];
    uint32_t pending = 0u;
    parts[pending++] = x;
    uint32_t h = 0u;
    for (uint32_t i = 0u; i < skeleton->count; i++) {
        if (pending == 0u)
            return CETTA_MOL_DECLINE;
        CettaMol part = parts[--pending];
        const CettaSkeletonNode *node = &skeleton->nodes[i];
        if (node->len == 0u) {
            CettaMolResult made =
                cetta_mol_key(store, part, &numbering, &holes[h++]);
            if (made != CETTA_MOL_OK)
                return made;
            continue;
        }
        const CettaTermHdr *hdr;
        if (!mol_deref_header(store, &part, &hdr))
            return CETTA_MOL_DECLINE;
        if (part.id == CETTA_ATOM_ID_NONE)
            return CETTA_MOL_FAIL;
        if (hdr->tag != ATOM_EXPR ||
            (hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) != node->len)
            return CETTA_MOL_FAIL;
        if (!tu_plain_expr(store->universe, part.id, hdr))
            return CETTA_MOL_DECLINE;
        CettaMol head = {tu_child_from_header(store->universe, hdr, 0u),
                         part.at};
        if (!mol_deref(store, &head))
            return CETTA_MOL_DECLINE;
        if (head.id != node->head_id)
            return CETTA_MOL_FAIL;
        if (pending + node->len - 1u > CETTA_SKELETON_NODES)
            return CETTA_MOL_DECLINE;
        for (uint32_t c = node->len; c-- > 1u;)
            parts[pending++] =
                (CettaMol){tu_child_from_header(store->universe, hdr, c),
                           part.at};
    }
    return pending == 0u && h == skeleton->holes ? CETTA_MOL_OK
                                                 : CETTA_MOL_DECLINE;
}
