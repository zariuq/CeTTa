#include "space_skeleton.h"

#include <string.h>

/* The number of nodes in the subtree whose root is node I. */
static uint32_t skeleton_subtree(const CettaSkeleton *skeleton, uint32_t i) {
    uint32_t end = i + 1u;
    uint32_t open = skeleton->nodes[i].len ? skeleton->nodes[i].len - 1u : 0u;
    while (open != 0u) {
        open--;
        if (skeleton->nodes[end].len)
            open += skeleton->nodes[end].len - 1u;
        end++;
    }
    return end - i;
}

static void skeleton_count_holes(CettaSkeleton *skeleton) {
    skeleton->holes = 0u;
    for (uint32_t i = 0u; i < skeleton->count; i++)
        skeleton->holes += skeleton->nodes[i].len == 0u;
}

/* An expression of the universe whose first element is a symbol: its
 * length, head symbol and head id; false for any other term. */
static bool skeleton_expr(const TermUniverse *universe, AtomId id,
                          uint32_t *len, SymbolId *head, AtomId *head_id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr || hdr->tag != ATOM_EXPR || hdr->sym_or_head == SYMBOL_ID_NONE)
        return false;
    uint32_t n = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
    if (n == 0u || n > UINT8_MAX)
        return false;
    *len = n;
    *head = hdr->sym_or_head;
    *head_id = tu_child_from_header(universe, hdr, 0u);
    return true;
}

static bool skeleton_push(CettaSkeleton *skeleton, uint32_t len,
                          SymbolId head, AtomId head_id) {
    if (skeleton->count == CETTA_SKELETON_NODES)
        return false;
    skeleton->nodes[skeleton->count++] =
        (CettaSkeletonNode){.len = len, .head = head, .head_id = head_id};
    return true;
}

/* The shape of ID, an element of an expression above, as many nodes as
 * room allows.  *PENDING counts the element slots promised to expressions
 * already shaped, this one's among them; each takes a node at least, so an
 * expression is shaped only while the nodes made and promised fit, and is
 * a hole otherwise. */
static void skeleton_shape(const TermUniverse *universe, AtomId id,
                           CettaSkeleton *skeleton, uint32_t *pending) {
    uint32_t len = 0u;
    SymbolId head = SYMBOL_ID_NONE;
    AtomId head_id = CETTA_ATOM_ID_NONE;
    (*pending)--;
    if (!skeleton_expr(universe, id, &len, &head, &head_id) ||
        skeleton->count + 1u + *pending + (len - 1u) > CETTA_SKELETON_NODES) {
        (void)skeleton_push(skeleton, 0u, SYMBOL_ID_NONE, CETTA_ATOM_ID_NONE);
        return;
    }
    (void)skeleton_push(skeleton, len, head, head_id);
    *pending += len - 1u;
    for (uint32_t c = 1u; c < len; c++)
        skeleton_shape(universe, tu_child(universe, id, c), skeleton, pending);
}

bool cetta_skeleton_of(const TermUniverse *universe, AtomId id,
                       CettaSkeleton *skeleton) {
    uint32_t len = 0u;
    SymbolId head = SYMBOL_ID_NONE;
    AtomId head_id = CETTA_ATOM_ID_NONE;
    skeleton->count = 0u;
    if (!skeleton_expr(universe, id, &len, &head, &head_id) ||
        len > CETTA_SKELETON_NODES)
        return false;
    (void)skeleton_push(skeleton, len, head, head_id);
    uint32_t pending = len - 1u;
    for (uint32_t c = 1u; c < len; c++)
        skeleton_shape(universe, tu_child(universe, id, c), skeleton, &pending);
    skeleton_count_holes(skeleton);
    return true;
}

/* Copy into OUT the generalization of the subtree at node *I of FROM with
 * the term ID, advancing *I past the subtree. */
static void skeleton_meet(const CettaSkeleton *from, uint32_t *i,
                          const TermUniverse *universe, AtomId id,
                          CettaSkeleton *out) {
    const CettaSkeletonNode *node = &from->nodes[*i];
    uint32_t len = 0u;
    SymbolId head = SYMBOL_ID_NONE;
    AtomId head_id = CETTA_ATOM_ID_NONE;
    if (node->len == 0u || !skeleton_expr(universe, id, &len, &head, &head_id) ||
        len != node->len || head != node->head) {
        (void)skeleton_push(out, 0u, SYMBOL_ID_NONE, CETTA_ATOM_ID_NONE);
        *i += skeleton_subtree(from, *i);
        return;
    }
    (void)skeleton_push(out, node->len, node->head, node->head_id);
    (*i)++;
    for (uint32_t c = 1u; c < len; c++)
        skeleton_meet(from, i, universe, tu_child(universe, id, c), out);
}

/* The header of ID when it is an expression of NODE's length and head,
 * NULL otherwise. */
static inline const CettaTermHdr *
skeleton_node_hdr(const TermUniverse *universe, const CettaSkeletonNode *node,
                  AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    return hdr && hdr->tag == ATOM_EXPR && hdr->sym_or_head == node->head &&
                   (hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) == node->len
        ? hdr : NULL;
}

/* Whether the expression of header HDR fits the subtree whose root, node
 * *I - 1 of SKELETON, it matched, advancing *I as far as it compared.  A
 * hole fits any element, which is not read. */
static bool skeleton_fits_at(const TermUniverse *universe,
                             const CettaSkeleton *skeleton, uint32_t *i,
                             const CettaTermHdr *hdr, uint32_t len) {
    for (uint32_t c = 1u; c < len; c++) {
        const CettaSkeletonNode *node = &skeleton->nodes[(*i)++];
        if (node->len == 0u)
            continue;
        const CettaTermHdr *child = skeleton_node_hdr(
            universe, node, tu_child_from_header(universe, hdr, c));
        if (!child || !skeleton_fits_at(universe, skeleton, i, child, node->len))
            return false;
    }
    return true;
}

bool cetta_skeleton_generalize(CettaSkeleton *skeleton,
                               const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = skeleton->count != 0u
        ? skeleton_node_hdr(universe, &skeleton->nodes[0], id) : NULL;
    if (!hdr)
        return false;
    /* A fact the spine fits leaves it as it is: their common shape is the
     * spine. */
    uint32_t i = 1u;
    if (skeleton_fits_at(universe, skeleton, &i, hdr, skeleton->nodes[0].len))
        return true;
    CettaSkeleton out = {.count = 0u};
    i = 0u;
    skeleton_meet(skeleton, &i, universe, id, &out);
    skeleton_count_holes(&out);
    *skeleton = out;
    return true;
}

bool cetta_skeleton_fit_holes(CettaSkeleton *skeleton, uint32_t max) {
    while (skeleton->holes > max) {
        /* The last expression below the root whose elements are all holes
         * becomes one hole. */
        uint32_t pick = 0u;
        for (uint32_t i = 1u; i < skeleton->count; i++) {
            const CettaSkeletonNode *node = &skeleton->nodes[i];
            if (node->len == 0u)
                continue;
            bool leaves = true;
            for (uint32_t c = 1u; leaves && c < node->len; c++)
                leaves = skeleton->nodes[i + c].len == 0u;
            if (leaves)
                pick = i;
        }
        if (pick == 0u)
            return false;
        uint32_t span = skeleton_subtree(skeleton, pick);
        skeleton->nodes[pick] = (CettaSkeletonNode){.len = 0u};
        memmove(&skeleton->nodes[pick + 1u], &skeleton->nodes[pick + span],
                (skeleton->count - pick - span) * sizeof(skeleton->nodes[0]));
        skeleton->count -= span - 1u;
        skeleton_count_holes(skeleton);
    }
    return true;
}

static bool skeleton_cut_at(const TermUniverse *universe,
                            const CettaSkeleton *skeleton, uint32_t *i,
                            AtomId id, AtomId *holes, uint32_t *h) {
    const CettaSkeletonNode *node = &skeleton->nodes[(*i)++];
    if (node->len == 0u) {
        holes[(*h)++] = id;
        return true;
    }
    const CettaTermHdr *hdr = skeleton_node_hdr(universe, node, id);
    if (!hdr)
        return false;
    for (uint32_t c = 1u; c < node->len; c++) {
        if (!skeleton_cut_at(universe, skeleton, i,
                             tu_child_from_header(universe, hdr, c), holes, h))
            return false;
    }
    return true;
}

bool cetta_skeleton_cut(const TermUniverse *universe,
                        const CettaSkeleton *skeleton, AtomId id,
                        AtomId *holes) {
    uint32_t i = 0u, h = 0u;
    return skeleton_cut_at(universe, skeleton, &i, id, holes, &h) &&
           h == skeleton->holes;
}

static bool skeleton_cut_atom_at(TermUniverse *universe,
                                 const CettaSkeleton *skeleton, uint32_t *i,
                                 Atom *atom, bool store,
                                 TermUniverseAlphaParts *parts,
                                 AtomId *holes, uint32_t *h, bool *missing) {
    const CettaSkeletonNode *node = &skeleton->nodes[(*i)++];
    if (node->len == 0u) {
        bool declined = false;
        AtomId part = store
            ? term_universe_store_alpha_key_part(universe, atom, parts)
            : term_universe_alpha_key_lookup_part(universe, atom, parts,
                                                  &declined);
        if (part == CETTA_ATOM_ID_NONE) {
            if (store || declined)
                return false;
            *missing = true;
        }
        holes[(*h)++] = part;
        return true;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len != node->len ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom->expr.elems[0]->sym_id != node->head)
        return false;
    for (uint32_t c = 1u; c < node->len; c++) {
        if (!skeleton_cut_atom_at(universe, skeleton, i, atom->expr.elems[c],
                                  store, parts, holes, h, missing))
            return false;
    }
    return true;
}

bool cetta_skeleton_cut_atom(TermUniverse *universe,
                             const CettaSkeleton *skeleton, Atom *atom,
                             bool store, AtomId *holes, bool *missing) {
    TermUniverseAlphaParts parts;
    parts.len = 0u;
    uint32_t i = 0u, h = 0u;
    *missing = false;
    return atom &&
           skeleton_cut_atom_at(universe, skeleton, &i, atom, store, &parts,
                                holes, &h, missing) &&
           h == skeleton->holes;
}

AtomId cetta_skeleton_build_at(TermUniverse *universe,
                               const CettaSkeleton *skeleton, uint32_t *i,
                               const AtomId *holes, uint32_t *h) {
    const CettaSkeletonNode *node = &skeleton->nodes[(*i)++];
    if (node->len == 0u)
        return holes[(*h)++];
    AtomId kids[UINT8_MAX];
    kids[0] = node->head_id;
    for (uint32_t c = 1u; c < node->len; c++) {
        kids[c] = cetta_skeleton_build_at(universe, skeleton, i, holes, h);
        if (kids[c] == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
    }
    return tu_expr_from_ids(universe, kids, node->len);
}

static AtomId skeleton_lookup_at(const TermUniverse *universe,
                                 const CettaSkeleton *skeleton, uint32_t *i,
                                 const AtomId *holes, uint32_t *h) {
    const CettaSkeletonNode *node = &skeleton->nodes[(*i)++];
    if (node->len == 0u)
        return holes[(*h)++];
    AtomId kids[UINT8_MAX];
    kids[0] = node->head_id;
    for (uint32_t c = 1u; c < node->len; c++) {
        kids[c] = skeleton_lookup_at(universe, skeleton, i, holes, h);
        if (kids[c] == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
    }
    return tu_expr_lookup_ids(universe, kids, node->len);
}

AtomId cetta_skeleton_lookup(const TermUniverse *universe,
                             const CettaSkeleton *skeleton,
                             const AtomId *holes) {
    uint32_t i = 0u, h = 0u;
    return skeleton_lookup_at(universe, skeleton, &i, holes, &h);
}

AtomId cetta_skeleton_build(TermUniverse *universe,
                            const CettaSkeleton *skeleton,
                            const AtomId *holes) {
    uint32_t i = 0u, h = 0u;
    return cetta_skeleton_build_at(universe, skeleton, &i, holes, &h);
}

/* The variables of a pattern, each with its number of occurrences. */
typedef struct {
    VarId vars[64];
    uint32_t counts[64];
    uint32_t len;
    bool full;
} SkeletonPatternVars;

static void skeleton_note_vars(const Atom *atom, SkeletonPatternVars *seen) {
    if (atom->kind == ATOM_VAR) {
        for (uint32_t v = 0u; v < seen->len; v++) {
            if (var_id_eq(seen->vars[v], atom->var_id)) {
                seen->counts[v]++;
                return;
            }
        }
        if (seen->len == 64u) {
            seen->full = true;
            return;
        }
        seen->vars[seen->len] = atom->var_id;
        seen->counts[seen->len++] = 1u;
        return;
    }
    if (atom->kind == ATOM_EXPR) {
        for (CettaExprIndex c = 0u; c < atom->expr.len; c++)
            skeleton_note_vars(atom->expr.elems[c], seen);
    }
}

static bool skeleton_var_once(const SkeletonPatternVars *seen, VarId var) {
    for (uint32_t v = 0u; v < seen->len; v++) {
        if (var_id_eq(seen->vars[v], var))
            return seen->counts[v] == 1u && !seen->full;
    }
    return false;
}

/* Whether ATOM is equal only to itself, as an id names it: symbols,
 * integers, truth values, strings and expressions of them. */
static bool skeleton_exact_atom(const Atom *atom) {
    if (atom->kind == ATOM_SYMBOL)
        return true;
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_INT || atom->ground.gkind == GV_BOOL ||
               atom->ground.gkind == GV_STRING;
    if (atom->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex c = 0u; c < atom->expr.len; c++) {
        if (!skeleton_exact_atom(atom->expr.elems[c]))
            return false;
    }
    return true;
}

static bool skeleton_align_at(const TermUniverse *universe,
                              const CettaSkeleton *skeleton, uint32_t *i,
                              const Atom *pattern,
                              const SkeletonPatternVars *seen,
                              uint32_t open_holes, CettaDenseId *fixed,
                              uint32_t *h, bool *exact) {
    const CettaSkeletonNode *node = &skeleton->nodes[*i];
    if (node->len != 0u) {
        if (pattern->kind == ATOM_VAR) {
            /* A variable over a spine's subtree meets every fact there. */
            uint32_t span = skeleton_subtree(skeleton, *i);
            for (uint32_t k = *i; k < *i + span; k++) {
                if (skeleton->nodes[k].len == 0u)
                    fixed[(*h)++] = CETTA_DENSE_ANY;
            }
            *i += span;
            *exact = *exact && skeleton_var_once(seen, pattern->var_id);
            return true;
        }
        if (pattern->kind != ATOM_EXPR || pattern->expr.len != node->len)
            return false;
        /* A variable heading the pattern's expression meets the symbol
         * heading the spine's. */
        const Atom *first = pattern->expr.elems[0];
        if (first->kind == ATOM_VAR)
            *exact = *exact && skeleton_var_once(seen, first->var_id);
        else if (first->kind != ATOM_SYMBOL || first->sym_id != node->head)
            return false;
        (*i)++;
        for (uint32_t c = 1u; c < node->len; c++) {
            if (!skeleton_align_at(universe, skeleton, i,
                                   pattern->expr.elems[c], seen, open_holes,
                                   fixed, h, exact))
                return false;
        }
        return true;
    }
    (*i)++;
    uint32_t hole = (*h)++;
    fixed[hole] = CETTA_DENSE_ANY;
    if (pattern->kind == ATOM_VAR) {
        *exact = *exact && skeleton_var_once(seen, pattern->var_id);
        return true;
    }
    /* A ground part equal only to itself fixes the hole by its id, unless
     * some fact holds a variable there, which may unify with it. */
    if (!atom_has_vars(pattern) && skeleton_exact_atom(pattern) &&
        (open_holes & (UINT32_C(1) << hole)) == 0u) {
        AtomId id = term_universe_lookup_atom_id(universe, (Atom *)pattern);
        if (id == CETTA_ATOM_ID_NONE)
            return false;
        fixed[hole] = id;
        return true;
    }
    *exact = false;
    return true;
}

bool cetta_skeleton_align(const TermUniverse *universe,
                          const CettaSkeleton *skeleton, const Atom *pattern,
                          uint32_t open_holes, CettaDenseId *fixed,
                          bool *exact) {
    if (!pattern)
        return false;
    SkeletonPatternVars seen = {.len = 0u};
    skeleton_note_vars(pattern, &seen);
    uint32_t i = 0u, h = 0u;
    *exact = true;
    return skeleton_align_at(universe, skeleton, &i, pattern, &seen,
                             open_holes, fixed, &h, exact);
}

bool cetta_skeleton_align_elements(const TermUniverse *universe,
                                   const CettaSkeleton *skeleton,
                                   Atom *const *elements, uint32_t len,
                                   uint32_t open_holes, CettaDenseId *fixed,
                                   bool *exact) {
    const CettaSkeletonNode *root = &skeleton->nodes[0];
    if (!elements || len != root->len || root->len == 0u)
        return false;
    SkeletonPatternVars seen = {.len = 0u};
    for (uint32_t c = 0u; c < len; c++)
        skeleton_note_vars(elements[c], &seen);
    *exact = true;
    if (elements[0]->kind == ATOM_VAR)
        *exact = skeleton_var_once(&seen, elements[0]->var_id);
    else if (elements[0]->kind != ATOM_SYMBOL ||
             elements[0]->sym_id != root->head)
        return false;
    uint32_t i = 1u, h = 0u;
    for (uint32_t c = 1u; c < len; c++) {
        if (!skeleton_align_at(universe, skeleton, &i, elements[c], &seen,
                               open_holes, fixed, &h, exact))
            return false;
    }
    return true;
}
