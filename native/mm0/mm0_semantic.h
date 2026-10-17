/* mm0_semantic.h - MM0 declarations and proofs given as data.
 *
 * A builder (Mm0Decl) collects one declaration: its binders, its statement
 * and, for a theorem, the rows of a proof DAG.  Every value is checked when
 * it is added, and nothing reaches the theory until a finish call admits the
 * whole declaration, so a failed declaration leaves the theory unchanged.
 *
 * The rules are mm0.md "Verification" and "Definition checking", in the
 * form of the semantic kernel of the accompanying Lean development
 * (MM0/Kernel/BorrowedProofCursor.lean: hypothesis, theorem application and
 * conversion rows; Conversion.lean: refl, symm, trans, cong and unfold
 * witnesses; DeclarationAdmission.lean).  Expressions of a builder are
 * hash-consed for its lifetime, so equality is a pointer comparison and
 * substitution is linear in the distinct nodes of a statement; the table is
 * released with the builder, and the admitted statement is copied into the
 * theory.  Unlike MMB, this route has no limit on bound variables.
 *
 * Ownership.  An expression or conversion belongs to the builder that made
 * it; every call refuses one from another builder (MM0_FOREIGN).  An
 * expression stays valid until its builder is freed.
 *
 * Steps.  Every expression node a call creates or looks up, every
 * conversion witness, every proof row, and the traversal work of admission
 * is one step of the builder's budget (Mm0Options); the poll function is
 * consulted every 4096 steps.  A builder that ran out of steps or was
 * interrupted keeps everything it had checked: mm0_decl_resume gives it a
 * new budget, and the call that stopped may be made again. */
#ifndef CETTA_MM0_SEMANTIC_H
#define CETTA_MM0_SEMANTIC_H

#include "mm0.h"

typedef struct Mm0Decl Mm0Decl;
typedef struct Mm0Node Mm0Node;      /* an expression of a builder */

/* A checked conversion left = right: the identifier its builder issued
 * when a constructor below checked it.  Copying the value is harmless; a
 * builder accepts only identifiers it issued itself, so a value written by
 * hand or taken from another builder is refused (MM0_FOREIGN), and the
 * fields give no other authority.  Read the sides with mm0_conv_left and
 * mm0_conv_right. */
typedef struct {
    uint64_t issuer;                 /* the issuing builder's serial; 0: none */
    uint32_t index;                  /* its position in that builder's table */
} Mm0Conv;

Mm0Decl *mm0_decl_new(Mm0Theory *theory, const Mm0Options *options);
void mm0_decl_free(Mm0Decl *decl);
/* The first failure of a builder; later calls fail without effect. */
const Mm0Error *mm0_decl_error(const Mm0Decl *decl);
/* After MM0_LIMIT or MM0_INTERRUPTED (and only then): continue with a new
 * budget and poll function, keeping the binders, statement and rows already
 * checked.  The call that stopped had no effect besides cached expressions
 * and may be made again.  Any other failure stays final (MM0_MALFORMED). */
Mm0Status mm0_decl_resume(Mm0Decl *decl, const Mm0Options *options);

/* Binders in order.  `deps` lists positions of earlier bound binders.  A
 * dummy (of a definition, or of a theorem's proof) follows every ordinary
 * binder, is bound, and may not have a strict or free sort. */
Mm0Status mm0_decl_binder(Mm0Decl *decl, const char *name, uint32_t sort, bool bound,
                          const uint32_t *deps, uint32_t ndeps);
Mm0Status mm0_decl_dummy(Mm0Decl *decl, const char *name, uint32_t sort);
uint32_t mm0_decl_nbinders(const Mm0Decl *decl);

/* Expressions; NULL on failure (see mm0_decl_error).  An application must
 * be well typed: each argument has its binder's sort, and a bound binder
 * receives a bound variable. */
Mm0Node *mm0_decl_var(Mm0Decl *decl, uint32_t index);
Mm0Node *mm0_decl_app(Mm0Decl *decl, uint32_t term, Mm0Node *const *args, uint32_t nargs);
uint32_t mm0_node_sort(const Mm0Node *node);

/* The statement of an axiom or theorem: hypotheses in order, then the
 * conclusion; each must have a provable sort. */
Mm0Status mm0_decl_hyp(Mm0Decl *decl, Mm0Node *hyp);
Mm0Status mm0_decl_concl(Mm0Decl *decl, Mm0Node *concl);

/* Conversion witnesses, checked as they are built; each issues a fresh
 * identifier in *out.  unfold: `term` is a definition, `args` its
 * arguments, `images` the context positions of fresh bound variables for
 * its dummies. */
bool mm0_conv_refl(Mm0Decl *decl, Mm0Node *e, Mm0Conv *out);
bool mm0_conv_symm(Mm0Decl *decl, const Mm0Conv *c, Mm0Conv *out);
bool mm0_conv_trans(Mm0Decl *decl, const Mm0Conv *first, const Mm0Conv *second, Mm0Conv *out);
bool mm0_conv_cong(Mm0Decl *decl, uint32_t term, const Mm0Conv *children, uint32_t n, Mm0Conv *out);
bool mm0_conv_unfold(Mm0Decl *decl, uint32_t term, Mm0Node *const *args, uint32_t nargs,
                     const uint32_t *images, uint32_t nimages, Mm0Conv *out);
/* The sides of a conversion this builder issued; NULL for any other value. */
Mm0Node *mm0_conv_left(const Mm0Decl *decl, Mm0Conv conv);
Mm0Node *mm0_conv_right(const Mm0Decl *decl, Mm0Conv conv);

/* Proof rows, checked as they are added.  Parents are earlier row numbers,
 * in the order of the cited theorem's hypotheses; a row may be cited any
 * number of times.  A claim, when given, must equal the row's conclusion. */
Mm0Status mm0_row_hyp(Mm0Decl *decl, uint32_t hyp, Mm0Node *claim, uint32_t *row);
Mm0Status mm0_row_thm(Mm0Decl *decl, uint32_t thm, Mm0Node *const *args, uint32_t nargs,
                      const uint32_t *parents, uint32_t nparents, Mm0Node *claim, uint32_t *row);
Mm0Status mm0_row_conv(Mm0Decl *decl, const Mm0Conv *conv, uint32_t parent, Mm0Node *claim,
                       uint32_t *row);
Mm0Node *mm0_row_concl(const Mm0Decl *decl, uint32_t row);

/* Admission.  `name` NULL admits an unnamed (local) declaration. */
Mm0Status mm0_add_sort(Mm0Theory *theory, const char *name, uint8_t mods, Mm0Error *error);
Mm0Status mm0_decl_finish_term(Mm0Decl *decl, const char *name, uint32_t ret_sort,
                               const uint32_t *ret_deps, uint32_t nret_deps, uint32_t *index);
Mm0Status mm0_decl_finish_def(Mm0Decl *decl, const char *name, uint32_t ret_sort,
                              const uint32_t *ret_deps, uint32_t nret_deps, Mm0Node *body,
                              uint32_t *index);
Mm0Status mm0_decl_finish_axiom(Mm0Decl *decl, const char *name, uint32_t *index);
/* The proof's `root` row must conclude the statement's conclusion. */
Mm0Status mm0_decl_finish_theorem(Mm0Decl *decl, const char *name, uint32_t root, uint32_t *index);

#endif
