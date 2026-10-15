/* hol_replay.h - checked replay of HOLR v1 streams, stream writing and
 * certificate export.  Format: docs/hol_light_stream.txt.
 *
 * A stream is a linear, chronological list of kernel instructions.  Replay
 * executes each one with the same kernel operations as the C API; no record
 * turns a stated sequent into a theorem.  EXPECT records only compare.
 *
 * Laws (tested in tests/hol_light/test_stream.c):
 *  - Atomic records: a record either commits all its outputs or none.
 *  - Resource failures (NOMEM, LIMIT, INTERRUPTED) leave the current record
 *    uncommitted and retryable; resuming retries that record only.
 *  - Logical refusal and malformed input are absorbing in strict mode.  In
 *    probe mode a refused record commits a FAILED placeholder for each of its
 *    outputs and replay continues; records using a FAILED id are DEPFAIL.
 *  - Budgets count records.  Any split of budgets, and any split of the
 *    input bytes across replay_feed calls, reaches the same committed
 *    history as one uninterrupted run; no committed record runs twice.
 *  - EMIT records are delivery occurrences: equal theorems emitted twice are
 *    delivered twice, in order, through a cursor.
 *  - A checkpoint is an in-memory clone of the replay (input, tables,
 *    cursor, environment).  Continuing two clones gives two branches; the
 *    kernel refuses to mix their objects. */
#ifndef HOL_REPLAY_H
#define HOL_REPLAY_H

#include "hol_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOLR_MAGIC "HOLRPLY1"
#define HOLR_VERSION 1u

/* Opcodes of HOLR v1. */
enum {
    HOLR_NAME = 0x01, HOLR_TYVAR = 0x02, HOLR_TYAPP = 0x03, HOLR_VAR = 0x04,
    HOLR_CONST = 0x05, HOLR_COMB = 0x06, HOLR_ABS = 0x07,
    HOLR_REFL = 0x10, HOLR_TRANS = 0x11, HOLR_MK_COMB = 0x12, HOLR_ABS_RULE = 0x13,
    HOLR_BETA = 0x14, HOLR_ASSUME = 0x15, HOLR_EQ_MP = 0x16, HOLR_DEDUCT = 0x17,
    HOLR_INST_TYPE = 0x18, HOLR_INST = 0x19,
    HOLR_NEW_TYPE = 0x20, HOLR_NEW_CONSTANT = 0x21, HOLR_NEW_AXIOM = 0x22,
    HOLR_NEW_DEFINITION = 0x23, HOLR_NEW_TYPEDEF = 0x24, HOLR_POLICY = 0x25,
    HOLR_EXPECT = 0x30, HOLR_LABEL = 0x31, HOLR_EMIT = 0x32, HOLR_EXPECT_REFUSED = 0x33,
    HOLR_EXPECT_AXIOMS = 0x34, HOLR_EXPECT_ALPHA = 0x35,
    HOLR_DROP_TYPE = 0x40, HOLR_DROP_TERM = 0x41, HOLR_DROP_THM = 0x42,
    HOLR_END = 0x7f
};

/* Header flags. */
#define HOLR_FLAG_PROBE 0x1u /* continue after logical refusals */

typedef enum {
    HOL_REPLAY_RUNNING = 0, /* budget used up; more records may follow */
    HOL_REPLAY_NEED_INPUT,  /* an incomplete record awaits more bytes */
    HOL_REPLAY_ACCEPTED,    /* END checked: every record committed */
    HOL_REPLAY_REFUSED,     /* strict mode: a record was logically refused */
    HOL_REPLAY_MALFORMED,   /* encoding, reference, checksum or limit error */
    HOL_REPLAY_RESOURCE     /* NOMEM/LIMIT/INTERRUPTED: retry the same record */
} HolReplayState;

typedef struct {
    uint64_t max_buffered_bytes; /* unconsumed input held at once; 0: 1 GiB */
    uint64_t max_records;     /* 0: 2^40 */
    uint64_t max_ids;         /* per space; 0: 2^32 */
    uint32_t max_name_bytes;  /* 0: 1 MiB */
    uint32_t max_operands;    /* arguments or pairs per record; 0: 2^20 */
} HolReplayLimits;

typedef enum { HOLR_OUT_OK = 0, HOLR_OUT_REFUSED, HOLR_OUT_DEPFAIL, HOLR_OUT_CHECK } HolReplayOutcome;

/* One committed record, reported through the observer callback. */
typedef struct {
    uint64_t index;        /* record number, 0-based */
    uint8_t opcode;
    HolReplayOutcome outcome;
    HolStatus status;      /* kernel status of a refusal */
    HolReason reason;
    char space;            /* 'N','Y','T','H' or 0 when nothing is defined */
    uint64_t first_id;     /* first id defined by the record */
    uint32_t count;        /* ids defined (2 for NEW_TYPEDEF) */
    uint64_t digest[2];    /* FNV-1a 64 of canonical text of each output */
    uint64_t alpha[2];     /* FNV-1a 64 of alpha-normal text (terms, theorems) */
} HolReplayRecord;

typedef void (*HolReplayObserver)(void *ctx, const HolReplayRecord *rec);

typedef struct HolReplay HolReplay;

/* Starts a replay rooted at the bare bootstrap, under at least the given
 * axiom policy (a stream may tighten it, never loosen it).  The replay
 * registers its tables as GC roots; free it before the kernel. */
HolReplay *hol_replay_new(HolKernel *k, const HolReplayLimits *limits,
                          HolAxiomPolicy host_policy);
void hol_replay_free(HolReplay *r);
/* Appends input bytes (copied).  final = no more bytes will follow. */
HolReplayState hol_replay_feed(HolReplay *r, const void *bytes, size_t n, bool final);
/* Processes up to budget complete records (0 = no limit). */
HolReplayState hol_replay_resume(HolReplay *r, uint64_t budget);
HolReplayState hol_replay_state(const HolReplay *r);
/* Mnemonic of a HOLR opcode ("?" if unassigned). */
const char *hol_replay_opname(uint8_t opcode);
HolStatus hol_replay_status(const HolReplay *r);
HolReason hol_replay_reason(const HolReplay *r);
/* Calls fn after each committed record with its outcome and the FNV-1a
 * digests of its outputs' canonical text.  A diagnostic: the digests cost
 * the size of the text, which can be exponential in a shared DAG, and are
 * not bounded by fuel; observe only trusted streams. */
void hol_replay_set_observer(HolReplay *r, HolReplayObserver fn, void *ctx);
/* Observe only records whose index is a multiple of period (refused and
 * failed records are always observed); digests are computed only for
 * observed records.  0 or 1: every record. */
void hol_replay_set_sampling(HolReplay *r, uint64_t period);
/* Committed records, stream offset of the next record, ids per space,
 * emissions, deliveries, failed records, checks. */
typedef struct {
    uint64_t records, offset, names, types, terms, thms, emitted, delivered, failed;
    uint64_t expectations_checked;
} HolReplayInfo;
void hol_replay_info(const HolReplay *r, HolReplayInfo *out);
/* In-memory clone at the current record boundary (shares nothing mutable). */
HolReplay *hol_replay_clone(const HolReplay *r);
/* A fresh reference to theorem id h (0-based), owned by the current owner. */
HolStatus hol_replay_thm(HolReplay *r, uint64_t h, HolRef *out);
/* The environment reached so far. */
HolStatus hol_replay_env(HolReplay *r, HolRef *out);
/* Next delivery occurrence: a fresh reference and its ordinal. */
HolStatus hol_replay_next(HolReplay *r, HolRef *out, uint64_t *ordinal);
/* Labels (LABEL records) in order: theorem id and name bytes. */
size_t hol_replay_nlabels(const HolReplay *r);
bool hol_replay_label(const HolReplay *r, size_t i, uint64_t *thm, const char **name,
                      size_t *len);

/* ---- Structural scan ------------------------------------------------------ */

/* One decoded record, without execution.  layout names the operand kinds
 * in encoding order (docs/hol_light_stream.txt); fixed holds the non-list operands
 * in that order and list the counted list (pairs flattened). */
typedef struct {
    uint64_t index;
    uint8_t opcode;
    const char *layout;
    char out;        /* id space the record defines ids in, or 0 */
    unsigned nout;   /* ids it defines */
    const uint64_t *fixed;
    unsigned nfixed;
    const uint64_t *list;
    uint64_t nlist;
    const uint8_t *bytes; /* NAME */
    uint64_t nbytes;
    const uint8_t *raw;   /* the record's bytes */
    size_t raw_len;
} HolScanRecord;

typedef struct {
    uint32_t flags;
    HolAxiomPolicy policy;
    size_t header_len;
} HolScanHeader;

/* Return false to stop the scan (reported as HOL_REPLAY_RUNNING). */
typedef bool (*HolScanFn)(void *ctx, const HolScanRecord *rec);

/* Decodes a complete stream: header, every record up to END (which is
 * checked for count and checksum, not reported), no trailing bytes.  No
 * kernel is involved and references are not resolved; replay is what
 * checks a stream.  HOL_REPLAY_ACCEPTED means well-formed; MALFORMED sets
 * *why. */
HolReplayState hol_stream_scan(const uint8_t *bytes, size_t len, HolScanHeader *hdr, HolScanFn fn,
                               void *ctx, HolReason *why);

/* ---- Writing streams --------------------------------------------------- */

typedef struct HolWriter HolWriter;
HolWriter *hol_writer_new(uint32_t flags, HolAxiomPolicy policy);
void hol_writer_free(HolWriter *w);
/* Low-level record emission; ids are the writer's responsibility. */
bool hol_writer_op(HolWriter *w, uint8_t opcode);
bool hol_writer_uv(HolWriter *w, uint64_t v);
bool hol_writer_bytes(HolWriter *w, const void *p, size_t n);
/* Appends one complete encoded record (as reported by hol_stream_scan). */
bool hol_writer_record(HolWriter *w, const void *p, size_t n);
/* Counted records so far; finish appends END and returns the bytes
 * (owned by the writer until it is freed). */
uint64_t hol_writer_records(const HolWriter *w);
const uint8_t *hol_writer_finish(HolWriter *w, size_t *len);
/* Raw access to the bytes written so far (no END). */
const uint8_t *hol_writer_data(const HolWriter *w, size_t *len);

/* ---- Certificate export -------------------------------------------------- */

/* Enables proof journaling for theorems produced through the public API
 * from now on (theory events are always recorded). It is off by default;
 * ordinary checking does not need a second copy of its input derivation.
 * Switching it on cannot reconstruct earlier, unrecorded proofs. */
void hol_journal_enable(HolKernel *k, bool on);
/* Writes a HOLR stream containing the theory history needed by the roots
 * (from the bare bootstrap), every journaled proof event they depend on,
 * and one EMIT per root (repeats preserved), followed by EXPECT records
 * stating each root's sequent.  Refuses roots from incompatible branches
 * (HOL_FOREIGN) and theorems without a journaled derivation (HOL_REFUSED).
 * *out is malloc'd; the caller frees it. */
HolStatus hol_export(HolKernel *k, const HolRef *roots, size_t n, uint8_t **out,
                     size_t *len);

#ifdef __cplusplus
}
#endif
#endif /* HOL_REPLAY_H */
