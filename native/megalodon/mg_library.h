#ifndef CETTA_MEGALODON_LIBRARY_H
#define CETTA_MEGALODON_LIBRARY_H

/*
 * Source libraries and portable evidence.
 *
 *   - Native import of upstream `megalodon -sexprinfo` exports.  OCaml is
 *     the development oracle that produces an export; checking it here runs
 *     no OCaml.  Every definition is type-checked and normal-checked, every
 *     theorem proof is checked by the native kernel against its stated
 *     proposition; primitives must agree with the selected profile;
 *     parameters and axioms are admitted only under the importing
 *     authority's explicit policy.  Admitted-but-unproved theorems
 *     (ADMITTED, QEDWITHADMITS) grant nothing and are counted.  Unknown
 *     export forms are reported, never skipped silently.
 *   - CeTTa-native libraries: a theory's live declarations, in admission
 *     order, with theorem evidence, as ordinary wire-syntax data.  Loading
 *     one rechecks everything; the file is untrusted data, not a cache.
 *   - Portable evidence: a theorem's derivation as rows (NativeCursorWire),
 *     premises before conclusions, recheckable by a session.
 *
 * Callers hold the theory lock; deep work belongs on a big-stack worker.
 */

#include "mg_core.h"
#include "mg_session.h"

#include <stdio.h>

/* ── portable evidence ────────────────────────────────────────────────── */

/* Rows at addresses 0..row_count-1, every parent before its child; the
 * root is the last row.  Lazy evidence states judgments in source form
 * (derived under lazy conversion): it is rechecked by a lazy session, which
 * accepts it exactly when the rows' normal forms are exact evidence. */
struct MgEvidence {
    MgRow *rows;
    uint64_t *addresses;
    uint64_t *parent_store;
    uint32_t row_count;
    uint64_t root;
    bool lazy;
};

/* The derivation of a theorem (caller holds its theory's lock). */
MgStatus mg_evidence_of(const MgThm *thm, MgEvidence *out);
void mg_evidence_free(MgEvidence *ev);
/* Owned copy (NULL on allocation failure). */
MgEvidence *mg_evidence_clone(const MgEvidence *ev);
void mg_evidence_destroy(MgEvidence *ev);
/* Check evidence rows in a fresh session (lazy for lazy evidence) against
 * the theory's live snapshot; the root judgment becomes a theorem. */
MgStatus mg_evidence_check(MgTheory *theory, const MgEvidence *ev,
                           MgBudget *budget, const char **reason,
                           MgThm **out);

/* ── imports and libraries ────────────────────────────────────────────── */

typedef struct {
    bool all_axioms;           /* the authority accepts every axiom */
    const char **axioms;       /* else: accepted axiom source names or hashes */
    uint32_t axiom_count;
} MgImportPolicy;

typedef struct {
    uint64_t primitives, parameters, definitions, axioms, theorems;
    uint64_t repeated;         /* repeated definitions, axioms, parameters */
    uint64_t reproved;         /* proofs, checked, of propositions already known */
    uint64_t normalized;       /* source definitions admitted in beta-eta
                                * normal form (NativeAdmission requires it;
                                * conversion is unchanged) */
    uint64_t identities;       /* content identities recomputed and matched */
    uint64_t realized;         /* content-addressed parameters realized by
                                * the definition their name identifies */
    uint64_t unproved;         /* ADMITTED / QEDWITHADMITS: no authority */
    uint64_t dependent;        /* proofs citing an unproved theorem: not
                                * checkable, skipped, and unproved in turn */
    uint64_t unsupported;      /* forms this importer does not know */
    uint64_t revision;
    bool stopped;              /* stopped at an item; nothing after it */
    uint64_t line;             /* export line of the stopping item */
    MgStatus status;
    char message[512];
} MgImportReport;

/* Import an upstream export.  Stops at the first item that does not check
 * (report->stopped, line, message); items before it stay admitted, each
 * having been admitted atomically on its own.  A theorem without a proof
 * (ADMITTED, QEDWITHADMITS) is never admitted; a later proof that cites one
 * is not checkable and is skipped the same way (report->dependent), since
 * the front end lets such proofs continue.  No skipped theorem becomes a
 * known. */
MgStatus mg_import_sexprinfo(MgTheory *theory, const char *text, size_t len,
                             const char *origin, const MgImportPolicy *policy,
                             MgBudget *budget, MgImportReport *report);
MgStatus mg_import_sexprinfo_file(MgTheory *theory, const char *path,
                                  const MgImportPolicy *policy,
                                  MgBudget *budget, MgImportReport *report);

/* The same admission, item by item, for a front end that states
 * declarations itself (mg_elab.c): each item as the export would state it,
 * with its source name, its identity (64 hexadecimal digits), its type
 * binders, and its type, body, proposition or proof in the kernel's
 * conventions, type binders included.  Every identity is recomputed and
 * every item checked; a false return stops the admission (report->stopped,
 * line, message).  A theorem waits for mg_admit_proof or
 * mg_admit_unproved; mg_admission_finish ends the session (a theorem still
 * waiting has no authority) and frees it. */
typedef struct MgAdmission MgAdmission;
MgAdmission *mg_admission_new(MgTheory *theory, const char *origin,
                              const MgImportPolicy *policy, MgBudget *budget,
                              MgImportReport *report);
bool mg_admit_prim(MgAdmission *a, const char *name, const char *hash, uint32_t prim,
                   MgTp *type, uint64_t line);
bool mg_admit_param(MgAdmission *a, const char *name, const char *hash, uint32_t levels,
                    MgTp *type, uint64_t line);
bool mg_admit_def(MgAdmission *a, const char *name, const char *hash, uint32_t levels,
                  MgTp *type, MgTm *body, uint64_t line);
bool mg_admit_axiom(MgAdmission *a, const char *name, const char *hash, uint32_t levels,
                    MgTm *prop, uint64_t line);
bool mg_admit_theorem(MgAdmission *a, const char *name, const char *hash, uint32_t levels,
                      MgTm *prop, uint64_t line);
bool mg_admit_proof(MgAdmission *a, MgPf *proof);
bool mg_admit_unproved(MgAdmission *a);
MgStatus mg_admission_finish(MgAdmission *a);

/* ── verdicts: what each theorem rests on ─────────────────────────────── */

/* Where an admitted axiom or theorem was stated. */
typedef enum {
    MG_ORIGIN_DOCUMENT = 0,   /* by the document being checked */
    MG_ORIGIN_SIGNATURE,      /* by a signature it is checked against (megalodon -I) */
} MgOrigin;

/* What became of a theorem. */
typedef enum {
    MG_VERDICT_CHECKED = 1,   /* the kernel checked its proof */
    MG_VERDICT_REPROVED,      /* checked; its proposition was already known */
    MG_VERDICT_DEPENDENT,     /* its proof cites a theorem without authority */
    MG_VERDICT_ADMITTED,      /* the document admits it (Admitted, admit) */
    MG_VERDICT_ABY,           /* the document leaves steps to automation (aby) */
    MG_VERDICT_PENDING,       /* the document ended inside its proof */
    MG_VERDICT_EXHAUSTED,     /* checking it used up its work budget: undecided */
} MgVerdict;

/* The ledger of an admission: every axiom and theorem in the order
 * admitted, each theorem with its verdict and what its proof rests on.
 * The kernel decides authority; the ledger records it. */
typedef struct MgLedger MgLedger;

typedef struct {
    const char *name;
    uint32_t id;               /* identity: the name id of its 64-digit hash */
    uint64_t line;
    bool theorem;              /* else an axiom */
    MgOrigin origin;
    MgVerdict verdict;         /* a theorem's */
    /* A checked theorem's document axioms: those its proof cites, directly
     * or through the checked theorems it cites (ledger positions,
     * ascending).  Signature axioms are the theory and are not listed. */
    const uint32_t *axioms;
    uint32_t axiom_count;
    /* A dependent theorem's cited theorems without authority. */
    const uint32_t *unproved;
    uint32_t unproved_count;
    /* Cited knowns admitted before this ledger began (their verdicts are
     * recorded elsewhere). */
    uint32_t external_count;
    /* The work units checking the theorem used, when the admission budgets
     * each theorem (mg_admission_budget_theorems); else 0. */
    uint64_t work;
} MgLedgerEntry;

MgLedger *mg_ledger_new(void);
void mg_ledger_free(MgLedger *l);
/* False once the ledger failed to record an item (out of memory, or its
 * view of a citation disagreed with the kernel's): its entries are then
 * not a verdict. */
bool mg_ledger_complete(const MgLedger *l);
uint32_t mg_ledger_len(const MgLedger *l);
/* Entry i; its arrays stay valid until the ledger next records. */
void mg_ledger_get(const MgLedger *l, uint32_t i, MgLedgerEntry *out);

/* Record this admission's axioms and theorems in `ledger`, which the
 * caller owns and which outlives the admission. */
void mg_admission_record(MgAdmission *a, MgLedger *ledger);
/* The origin of the axioms and theorems admitted from now on. */
void mg_admission_set_origin(MgAdmission *a, MgOrigin origin);
/* The pending theorem has no authority, for `why` (ADMITTED, ABY,
 * PENDING or EXHAUSTED); mg_admit_unproved is ADMITTED. */
bool mg_admit_unproved_as(MgAdmission *a, MgVerdict why);
/* Each theorem from now on is checked within `work` units (zero:
 * unlimited), counted from its statement through its proof's kernel check,
 * the elaborator's work included when it shares the admission's budget.  A
 * theorem whose budget runs out gets the verdict EXHAUSTED and the
 * admission goes on; work outside theorems is not limited. */
void mg_admission_budget_theorems(MgAdmission *a, uint64_t work);

/* CeTTa-native libraries. */
MgStatus mg_library_save(MgTheory *theory, FILE *out, const char **reason);
MgStatus mg_library_save_file(MgTheory *theory, const char *path,
                              const char **reason);
MgStatus mg_library_load(MgTheory *theory, const char *text, size_t len,
                         const char *origin, const MgImportPolicy *policy,
                         MgBudget *budget, MgImportReport *report);
MgStatus mg_library_load_file(MgTheory *theory, const char *path,
                              const MgImportPolicy *policy, MgBudget *budget,
                              MgImportReport *report);

/* Wire-syntax printers (iterative; shared nodes print as trees). */
bool mg_print_tp(FILE *out, const MgTp *t);
bool mg_print_tm(FILE *out, const MgStore *s, const MgTm *m);
bool mg_print_pf(FILE *out, const MgStore *s, const MgPf *p);
bool mg_print_row(FILE *out, const MgStore *s, const MgRow *row);

#endif /* CETTA_MEGALODON_LIBRARY_H */
