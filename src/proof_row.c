#include "proof_row.h"
#include "shared_transition.h"
#include <limits.h>

enum { ROW_LABEL_LIMIT = CETTA_PROOF_ROW_LABEL_LIMIT,
       ROW_CHILD_LIMIT = CETTA_PROOF_ROW_CHILD_LIMIT };

static bool exact_label(const TermUniverse *u, AtomId id) {
    const CettaTermHdr *h = tu_hdr(u, id);
    return h && (h->tag == ATOM_SYMBOL ||
        (h->tag == ATOM_GROUNDED && tu_ground_kind(u, id) == GV_INT));
}

bool cetta_proof_row_layout_capture(const CettaProofProducerPlan *plan,
                                    CettaProofRowLayout *out) {
    CettaProofProducerDescription d;
    if (!out || !cetta_proof_producer_describe(plan, &d) ||
        d.layout.source_leg >= d.source_count || !d.layout.proof.length ||
        d.layout.proof.length > CETTA_PROOF_PATH_MAX) return false;
    CettaProofRowLayout copy = {.proof = d.layout.proof,
        .universe_instance = d.sources[d.layout.source_leg].universe_instance,
        .storage_epoch = d.sources[d.layout.source_leg].universe_epoch};
    const Atom *at = cetta_proof_producer_pattern(plan, d.layout.source_leg);
    for (uint32_t i = 0u; i < copy.proof.length; i++) {
        if (!at || at->kind != ATOM_EXPR || at->expr.len != d.proof_ancestor_arity[i] ||
            !at->expr.len || at->expr.elems[0]->kind != ATOM_SYMBOL ||
            !copy.proof.child[i] || copy.proof.child[i] >= at->expr.len) return false;
        copy.arity[i] = at->expr.len; copy.head[i] = atom_symbol_current_id(at->expr.elems[0]);
        at = at->expr.elems[copy.proof.child[i]];
    }
    *out = copy; return true;
}

CettaProofRowStatus cetta_proof_row_materialize(
    TermUniverse *u, const CettaProofRowLayout *layout,
    const CettaProofWord *word, CettaProofRowRetain retain, void *context,
    AtomId *out, CettaProofRowWork *work) {
    CETTA_SCOPED_SHARED_TRANSITION(guard);
    if (work) *work = (CettaProofRowWork){0};
    if (!u || !layout || !word || !out || !work || !retain ||
        u->instance_id != layout->universe_instance ||
        u->storage_epoch != layout->storage_epoch ||
        word->label_count > ROW_LABEL_LIMIT ||
        (word->label_count && !word->outer_labels) || !exact_label(u, word->seed))
        return CETTA_PROOF_ROW_DECLINED;
    const CettaProofFieldPath *path = &layout->proof;
    if (!path->length || path->length > CETTA_PROOF_PATH_MAX)
        return CETTA_PROOF_ROW_DECLINED;
    AtomId ancestors[CETTA_PROOF_PATH_MAX];
    AtomId at = word->state_key;
    uint32_t children = 2u * word->label_count;
    for (uint32_t i = 0u; i < word->label_count; i++)
        if (!exact_label(u, word->outer_labels[i])) return CETTA_PROOF_ROW_DECLINED;
    for (uint32_t i = 0u; i < path->length; i++) {
        CettaExprLen full = layout->arity[i];
        bool last = i + 1u == path->length;
        CettaExprIndex chosen = path->child[i];
        if (full < 2u || full > ROW_CHILD_LIMIT || !chosen || chosen >= full ||
            tu_kind(u, at) != ATOM_EXPR || tu_arity(u, at) != full - (last ? 1u : 0u))
            return CETTA_PROOF_ROW_DECLINED;
        AtomId head = tu_child(u, at, 0u);
        if (tu_kind(u, head) != ATOM_SYMBOL ||
            tu_sym(u, head) != layout->head[i] ||
            full > ROW_CHILD_LIMIT - children)
            return CETTA_PROOF_ROW_DECLINED;
        children += (uint32_t)full;
        ancestors[i] = at;
        if (!last) {
            at = tu_child(u, at, chosen);
        }
    }
    /* No borrowed header/child pointer survives insertion. The bounded
     * construction section roots its result before allowing collection. */
    if (u->collection_inhibit == UINT32_MAX) return CETTA_PROOF_ROW_CAPACITY;
    term_universe_collection_inhibit_enter(u);
    AtomId made = word->seed;
    for (uint32_t i = word->label_count; i; i--) {
        AtomId pair[2] = {word->outer_labels[i - 1u], made};
        work->labels++; work->constructors++; work->child_slots += 2u;
        made = tu_expr_from_ids(u, pair, 2u);
        if (made == CETTA_ATOM_ID_NONE) {
            term_universe_collection_inhibit_leave(u);
            return CETTA_PROOF_ROW_CAPACITY;
        }
    }
    AtomId kids[ROW_CHILD_LIMIT];
    for (uint32_t i = path->length; i; i--) {
        uint32_t depth = i - 1u;
        CettaExprLen full = layout->arity[depth];
        CettaExprIndex chosen = path->child[depth];
        bool insert = i == path->length;
        for (CettaExprIndex c = 0u; c < full; c++) {
            if (c == chosen) kids[c] = made;
            else kids[c] = tu_child(u, ancestors[depth], c - (insert && c > chosen ? 1u : 0u));
        }
        work->constructors++; work->child_slots += (uint32_t)full;
        made = tu_expr_from_ids(u, kids, full);
        if (made == CETTA_ATOM_ID_NONE) {
            term_universe_collection_inhibit_leave(u);
            return CETTA_PROOF_ROW_CAPACITY;
        }
    }
    if (!retain(context, u, made)) {
        term_universe_collection_inhibit_leave(u);
        return CETTA_PROOF_ROW_CAPACITY;
    }
    term_universe_collection_inhibit_leave(u);
    *out = made;
    return CETTA_PROOF_ROW_READY;
}
