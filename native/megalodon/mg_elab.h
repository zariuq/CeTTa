#ifndef CETTA_MEGALODON_ELAB_H
#define CETTA_MEGALODON_ELAB_H

/*
 * Megalodon document evaluation (megalodon.ml evaluate_docitem over
 * interpret.ml's elaboration): each item read by mg_source becomes what
 * Megalodon makes of it.  Types and terms are elaborated against the
 * signature and the section context, through the notation the document
 * declared (infix, prefix and postfix operators, user binders, set
 * formers, numerals, strings), and every declaration receives Megalodon's
 * identities: the content hash of its normal form, and for monomorphic
 * propositions the Proofgold identity in the selected theory.  With a rows
 * stream, the evaluation prints Megalodon's -sexprinfo rows.
 *
 * A failure stops the document with Megalodon's message, at the position
 * Megalodon reports.
 */

#include <stdbool.h>
#include <stdio.h>

#include "mg_core.h"
#include "mg_source.h"

typedef struct MgElab MgElab;
typedef struct MgLedger MgLedger;

typedef struct {
    MgProfileTag theory;   /* Egal unless -hf, -mizar, -hoas or -setmm */
    FILE *rows;            /* -sexprinfo rows, or NULL */
    /* Admit every declaration into the kernel theory as it is evaluated
     * (mg_library.h MgAdmission): identities recomputed, items checked,
     * the document's axioms accepted as its authority, theorems without a
     * checked proof left without authority. */
    bool kernel;
    /* With kernel: record each axiom and theorem, with its origin
     * (signature or document) and each theorem's verdict and what its
     * proof rests on (mg_library.h MgLedger, owned by the caller). */
    MgLedger *ledger;
    /* With kernel: each theorem is checked within this many work units
     * (mg_admission_budget_theorems; zero: unlimited), its proof script's
     * evaluation included.  A theorem that runs out has the verdict
     * EXHAUSTED and the document goes on. */
    uint64_t theorem_work;
    /* Native aby (mg_aby.c): an aby step is first proved by search, from
     * its goal's context and the facts it names, within aby_max_steps
     * search steps (0: 200,000) and aby_max_depth levels (0: 8); what the
     * search finds is the step's proof, checked like any other.  Off,
     * aby admits its goal, as Megalodon does. */
    bool prove_aby;
    uint64_t aby_max_steps;
    uint32_t aby_max_depth;
    /* The tactic engine's conversion as Megalodon's syntax.ml runs it,
     * remembering nothing (mg_proof.c conv2).  The answers are the same;
     * the count of conversions is then what Megalodon itself pays, proof
     * by proof. */
    bool reference_conversion;
} MgElabOptions;

MgElab *mg_elab_new(const MgElabOptions *options);
/* The aby steps native aby tried and proved so far. */
void mg_elab_aby_counts(const MgElab *e, uint64_t *tried, uint64_t *proved);
/* The conversions the tactic engine was asked for so far (rewrite, apply,
 * exact and the others compare terms up to beta, eta and definitions): an
 * exact count of its work, as the kernel's work units are of the kernel's. */
uint64_t mg_elab_conversion_steps(const MgElab *e);
void mg_elab_free(MgElab *e);

/* Megalodon's mgcheck of the next file: sections and the local context
 * start empty, everything declared stays.  A signature (-I) may not state
 * theorems. */
void mg_elab_begin_file(MgElab *e, bool signature);

/* Evaluate one item that mg_source_next returned, at the position
 * Megalodon evaluates it (item->end_line, item->end_char).  0, or -1 with
 * mg_elab_error; -2 when Megalodon itself would stop with an uncaught
 * exception (it exits 2 then); -3 when the kernel refuses an item that
 * Megalodon accepts (a disagreement, never Megalodon's own outcome). */
int mg_elab_item(MgElab *e, const MgSourceItem *item);

/* "Failure at line L char C: MESSAGE", as Megalodon prints it, or the
 * uncaught exception ("Fatal error: exception ..."). */
const char *mg_elab_error(const MgElab *e);

/* Proofs not yet checked: tactics other than proof structure, admit and
 * the closing commands are counted, not evaluated. */
uint64_t mg_elab_unchecked_tactics(const MgElab *e);

MgTheory *mg_elab_theory(MgElab *e);

/* End the kernel admission (with options.kernel): the report of what the
 * kernel admitted, and MG_OK or why it stopped. */
MgStatus mg_elab_finish(MgElab *e, char *summary, size_t cap);

#endif
