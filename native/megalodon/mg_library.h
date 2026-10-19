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
