#ifndef CETTA_MEGALODON_SESSION_H
#define CETTA_MEGALODON_SESSION_H

/*
 * Retained checking sessions over borrowed proof rows, and checked
 * theorem records (the native payload of theorem capsules).
 *
 * NativeSession.lean / ObservedCursor.lean / SourceCursor.lean:
 *   begin   -> an empty checkpoint against an immutable snapshot
 *   resume  -> walk the submitted schedule under a row bound and a work
 *              budget; every stop keeps the checked prefix (checkpoint)
 *              and the exact unfinished suffix (pending), including the
 *              row that stopped it
 *   finish  -> publishable only when the whole batch finished, the root is
 *              closed, and the claim is formed and normalizes to the root
 * A row stating its conclusion, witness, parents and origin is decoded by
 * the caller's row source; decoding never grants authority, and the
 * SourceCursor guard (JudgmentSafe, WitnessSafe) runs on every row.
 */

#include "mg_core.h"

typedef enum {
    MG_SESSION_FINISHED = 0,
    MG_SESSION_PAUSED,
    MG_SESSION_REFUSED,
    MG_SESSION_MALFORMED,
    MG_SESSION_EXHAUSTED,
    MG_SESSION_INTERRUPTED,
    MG_SESSION_NOMEM,
    MG_SESSION_CAPACITY,
    MG_SESSION_CANCELLED,
} MgSessionStatus;

const char *mg_session_status_name(MgSessionStatus status);

/* A decoded row (NativeCursorWire.decodeNode).  Parents may repeat. */
typedef struct {
    MgJudgment conclusion;
    MgWitness witness;
    uint64_t *parents;
    uint32_t parent_count;
    uint64_t origin;
} MgRow;

/* A borrowed row store (ObservedCursor.Store).  revision() is read live:
 * a session started at one revision refuses to read rows at another
 * (malformed) until mg_session_rebase confirms every checked row is
 * unchanged.  read() decodes one row into the theory's store (caller holds
 * the theory lock); MG_MALFORMED when the address has no well-formed row,
 * MG_NOMEM on allocation failure.  The parents array must stay valid until
 * the next read. */
typedef struct {
    uint64_t (*revision)(void *context);
    MgStatus (*read)(void *context, MgTheory *theory, uint64_t address,
                     MgRow *out, const char **reason);
    void (*release)(void *context);
    void *context;
} MgRowSource;

typedef struct MgSession MgSession;

typedef struct {
    MgSessionStatus status;
    const char *reason;
    uint64_t failed_address;      /* the row that stopped the walk */
    uint64_t checked_rows;        /* rows in the checkpoint */
    uint64_t attempted_rows;      /* rows attempted by this resume */
    uint64_t work_used;           /* work units of this resume */
    const uint64_t *pending;      /* borrowed until the next resume */
    uint64_t pending_len;
} MgSessionReport;

/* Begin against the theory's live snapshot.  Takes ownership of `source`
 * (its release runs exactly once, at the session's end).  Rows are checked
 * exactly (ObservedCursor): each states the reference rule's judgment. */
MgSession *mg_session_begin(MgTheory *theory, MgRowSource source,
                            MgStatus *status);
/* The same; with `lazy`, a row states a formed judgment (mg_ctx_formed,
 * mg_prop_formed) whose context and proposition are convertible to its
 * rule's conclusion from the cited parents (mg_step_lazy): formed rows are
 * accepted exactly when their normal forms are rows an exact session
 * accepts.  Evidence marked lazy (MgEvidence.lazy) is rechecked so. */
MgSession *mg_session_begin_mode(MgTheory *theory, MgRowSource source,
                                 bool lazy, MgStatus *status);
void mg_session_retain(MgSession *s);
void mg_session_release(MgSession *s);
/* Walk `schedule`; row_bound 0 = unlimited.  Caller holds the theory lock. */
MgStatus mg_session_resume(MgSession *s, const uint64_t *schedule,
                           uint64_t schedule_len, uint64_t row_bound,
                           MgBudget *budget, MgSessionReport *report);
/* Read-only observation of the retained checkpoint. */
void mg_session_inspect(const MgSession *s, MgSessionReport *report);
bool mg_session_judgment(const MgSession *s, uint64_t address, MgJudgment *out);
MgEnv mg_session_env(const MgSession *s);
const MgFootprint *mg_session_footprint(const MgSession *s);
/* Move the checkpoint to the theory's live snapshot when the footprint of
 * every checked row agrees (CursorEnvironment.walk_frame); otherwise the
 * session stays on its old snapshot and REFUSED is returned. */
MgStatus mg_session_revalidate(MgSession *s);
/* Adopt the row store's current revision when every checked address still
 * reads the same row (conclusion, witness and parents); otherwise the
 * session keeps its revision and REFUSED is returned.  This is explicit
 * revalidation of the borrowed store; nothing is rechecked. */
MgStatus mg_session_rebase(MgSession *s);
/* Release the borrowed source and the checkpoint now; idempotent. */
void mg_session_cancel(MgSession *s);

/* ── theorem records ──────────────────────────────────────────────────── */

typedef enum {
    MG_THM_STEP = 1,      /* one LCF rule over retained premises */
    MG_THM_TREE,          /* a checked proof tree */
    MG_THM_SESSION,       /* the closed root of a finished session */
    MG_THM_DECLARATION,   /* a published known (axiom or theorem) */
} MgThmOrigin;

typedef struct MgThm MgThm;
struct MgThm {
    atomic_size_t holders;
    MgTheory *theory;          /* retained */
    uint64_t rev;              /* snapshot the judgment was checked in */
    MgJudgment judgment;
    MgFootprint footprint;     /* all direct names its derivation consulted */
    MgThmOrigin origin;
    MgWitness witness;         /* STEP */
    MgThm *premises[2];        /* STEP, retained */
    uint32_t premise_count;
    MgPf *proof;               /* TREE */
    MgEvidence *evidence;      /* SESSION: the root's derivation cone, owned
                                * (independent of the session's lifetime) */
    MgTm *claim;               /* the formed source claim, if closed */
    const MgDecl *decl;        /* DECLARATION, or once published */
    /* The judgment is in source form (lazy conversion): convertible to the
     * reference judgment, which is never built.  Every rule taking it as a
     * premise decides up to conversion; its evidence is marked lazy. */
    bool lazy;
};

void mg_thm_retain(MgThm *t);
void mg_thm_release(MgThm *t);

/* Whether a theorem checked at its snapshot remains valid at `env`
 * (same theory; footprint agreement from its snapshot to env). */
bool mg_thm_valid_in(const MgThm *thm, MgEnv env);

/* LCF: one checked rule step.  `ctx` is required for hyp and known and
 * ignored otherwise.  Premises must be valid in the theory's live snapshot.
 * Caller holds the theory lock. */
MgStatus mg_thm_step(MgTheory *theory, const MgWitness *w, MgCtx *ctx,
                     MgThm *const *premises, uint32_t count,
                     MgBudget *budget, const char **reason, MgThm **out);
/* A checked proof tree in an explicitly supplied context. */
MgStatus mg_thm_check_tree(MgTheory *theory, MgCtx *ctx, MgPf *proof,
                           MgBudget *budget, const char **reason,
                           MgThm **out);
/* The checked judgment at `root`, in whatever context it states, as a
 * theorem; the whole submitted batch must have finished (UNFINISHED
 * otherwise).  Its evidence is the root's derivation cone. */
MgStatus mg_session_theorem(MgSession *s, uint64_t root, const char **reason,
                            MgThm **out);
/* NativeSession.finish: whole batch finished, closed root, formed claim. */
MgStatus mg_session_finish(MgSession *s, uint64_t root, MgTm *claim,
                           MgBudget *budget, const char **reason,
                           MgThm **out);
/* A published known as a theorem value (axioms stay assumptions: the
 * declaration kind is recorded). */
MgStatus mg_thm_from_known(MgTheory *theory, uint32_t name,
                           const char **reason, MgThm **out);
/* NativeTheorem.publish + NativePublication.commit: a closed theorem whose
 * claim is formed and normalizes to its proposition becomes the known
 * `name`, atomically, at the live revision.  `claim` NULL uses the
 * theorem's own proposition. */
MgStatus mg_thm_publish(MgTheory *theory, MgThm *thm, uint32_t name,
                        MgTm *claim, MgBudget *budget, const char **reason,
                        const MgDecl **out);

#endif /* CETTA_MEGALODON_SESSION_H */
