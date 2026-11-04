#ifndef CETTA_TERM_MOLECULE_H
#define CETTA_TERM_MOLECULE_H

#include "space_skeleton.h"
#include "term_universe.h"

/*
 * Terms of a term universe read under frames, unified and instantiated by
 * their ids, with nothing decoded.  A stored term read under a frame is a
 * molecule: each of its variables stands for a cell of the frame, made when
 * first met, so a fact read under a fresh frame is renamed apart from every
 * other read of it.  A cell is unbound or bound to a molecule.  Unification
 * binds cells, with the occurs check; a mark undoes every binding, frame and
 * cell made since it was taken.  A molecule is instantiated as the alpha key
 * it stands for: its ground parts kept by id, its open ones built again with
 * their variables numbered by first occurrence.
 */

typedef struct {
    /* The term, or CETTA_ATOM_ID_NONE for the bare cell AT. */
    AtomId id;
    /* The frame the term's variables are read under, or the cell. */
    uint32_t at;
} CettaMol;

/* The frame of a term that holds no variable. */
#define CETTA_MOL_GROUND UINT32_MAX

typedef enum {
    CETTA_MOL_FAIL = 0,
    CETTA_MOL_OK,
    /* An atom whose equality is not its identity, a term the walks do not
     * take apart, or no room: the caller answers some other way. */
    CETTA_MOL_DECLINE,
} CettaMolResult;

typedef struct {
    uint32_t frame;
    uint32_t cell;
    AtomId var;
} CettaMolVar;

typedef struct {
    TermUniverse *universe;
    /* Per cell, its value; an unbound cell is the bare cell itself.  And
     * per cell, the frame whose variable it stands for (CETTA_MOL_GROUND
     * for a bare cell). */
    CettaMol *cells;
    uint32_t *cell_frames;
    uint32_t cell_len, cell_cap;
    /* The cells of frames' variables, open-addressed by (frame, variable),
     * and the slots taken, in order: a mark gives them back last first. */
    CettaMolVar *vars;
    uint32_t var_cap;
    uint32_t *var_log;
    uint32_t var_len, var_log_cap;
    /* The cells bound, in order. */
    uint32_t *trail;
    uint32_t trail_len, trail_cap;
    uint32_t frames;
    /* Per frame, how many of its cells are bound. */
    uint32_t *frame_bound;
    uint32_t frame_cap;
    /* The walks' pending molecules. */
    CettaMol *stack;
    uint32_t stack_cap;
    /* Ground expressions known to equal only themselves. */
    AtomId *exact;
    uint32_t exact_cap, exact_len;
} CettaMolStore;

typedef struct {
    uint32_t cells, vars, trail, frames;
} CettaMolMark;

/* The variables of an instance, numbered by first occurrence: the cells
 * met unbound, in that order. */
typedef struct {
    uint32_t cells[TERM_UNIVERSE_ALPHA_KEY_VARS];
    uint32_t len;
} CettaMolNumbering;

void cetta_mol_store_init(CettaMolStore *store, TermUniverse *universe);
void cetta_mol_store_free(CettaMolStore *store);

/* A frame no term has been read under. */
uint32_t cetta_mol_frame(CettaMolStore *store);
/* A bare cell, unbound; CETTA_MOL_DECLINE without room. */
CettaMolResult cetta_mol_cell(CettaMolStore *store, CettaMol *cell_out);

static inline CettaMolMark cetta_mol_mark(const CettaMolStore *store) {
    return (CettaMolMark){store->cell_len, store->var_len, store->trail_len,
                          store->frames};
}
/* Undo every binding, frame and cell made since MARK. */
void cetta_mol_undo(CettaMolStore *store, CettaMolMark mark);
/* Make now the cells of X's variables, which a mark taken after stays
 * clear of. */
CettaMolResult cetta_mol_touch(CettaMolStore *store, CettaMol x);

/* Unify A and B, binding cells; on FAIL or DECLINE the bindings made are
 * left for the caller's mark to undo. */
CettaMolResult cetta_mol_unify(CettaMolStore *store, CettaMol a, CettaMol b);
/* Unify PATTERN with the fact whose spine is SKELETON and whose holes are
 * HOLES, read under FRAME: each hole with the part of the pattern at it,
 * the spine node by node.  A variable of the pattern meeting the spine
 * takes the record of the subtree there, which is then made. */
CettaMolResult cetta_mol_unify_kept(CettaMolStore *store, CettaMol pattern,
                                    const CettaSkeleton *skeleton,
                                    const AtomId *holes, uint32_t frame);

/* A pattern aligned with a skeleton: the part of the pattern at each hole,
 * in preorder. */
typedef struct {
    CettaMol parts[CETTA_SKELETON_NODES];
    uint32_t holes;
} CettaMolAligned;

/* Align PATTERN with SKELETON under the bindings made so far: OK with the
 * part at each hole; FAIL when no fact of the skeleton can meet the
 * pattern; DECLINE when a variable of the pattern meets the spine, whose
 * facts cetta_mol_unify_kept then unifies one by one. */
CettaMolResult cetta_mol_align(CettaMolStore *store, CettaMol pattern,
                               const CettaSkeleton *skeleton,
                               CettaMolAligned *aligned);
/* Cache an aligned hole's dereferenced bare cell only when it predates
 * LIFETIME and no binding has been added since that mark. The caller keeps
 * the prefix, its earlier bindings and the original parts' frames until
 * the plan is discarded; each row may undo to LIFETIME. Other holes retain
 * their original framed molecule. DECLINE leaves allocations for the
 * caller's mark to undo, as unification does. */
CettaMolResult cetta_mol_aligned_stable_cells(CettaMolStore *store,
                                             CettaMolAligned *aligned,
                                             CettaMolMark lifetime);
/* Unify an aligned pattern with the kept fact whose holes are HOLES, read
 * under FRAME, as cetta_mol_unify_kept would. */
CettaMolResult cetta_mol_unify_aligned(CettaMolStore *store,
                                       const CettaMolAligned *aligned,
                                       const AtomId *holes, uint32_t frame);

/* The alpha key part X stands for, stored, its variables numbered under
 * NUMBERING (one numbering across a whole instance gives its key). */
CettaMolResult cetta_mol_key(CettaMolStore *store, CettaMol x,
                             CettaMolNumbering *numbering, AtomId *key_out);
/* The holes, in preorder, of the instance X stands for, cut by SKELETON
 * under one numbering, as cetta_skeleton_cut_atom cuts the instance's
 * atom; FAIL when the instance does not fit the skeleton. */
CettaMolResult cetta_mol_key_holes(CettaMolStore *store, CettaMol x,
                                   const CettaSkeleton *skeleton,
                                   AtomId *holes);

#endif
