/* mm0.h - native Metamath Zero verifier: public interface.
 *
 * A theory (Mm0Theory) is an append-only list of admitted MM0 declarations:
 * sorts, terms and definitions, axioms and theorems.  Declarations reach a
 * theory only through checked routes:
 *
 *   - mm0_load_mmb: an MMB proof file checked against its .mm0 specification
 *     (the allocation-sensitive command machine of mm0-c/mmb.md);
 *   - mm0_add_* and mm0_prove (mm0_semantic.h): declarations and proofs given
 *     as data, checked by the semantic rules of mm0.md (hypothesis, theorem
 *     application, conversion).
 *
 * A theory owns no global state, so separate theories may be used from
 * separate threads.  One theory must not be used by two threads at once.
 *
 * Every failure is reported as a status with a static reason.  A failed
 * operation leaves the theory as it was.
 */
#ifndef CETTA_MM0_H
#define CETTA_MM0_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MM0_OK = 0,
    MM0_MALFORMED,   /* the bytes, text or data do not have the required shape */
    MM0_REFUSED,     /* well-formed, but a logical check failed */
    MM0_UNSUPPORTED, /* MM0 this verifier does not check (input/output, Sorry) */
    MM0_LIMIT,       /* the caller's step budget ran out */
    MM0_NOMEM,       /* an allocation failed */
    MM0_INTERRUPTED, /* the caller's poll function asked to stop */
    MM0_FOREIGN,     /* a value that belongs to another theory or builder */
    MM0_CAPACITY,    /* a fixed table or identifier space of the host is full */
    MM0_CLOSED       /* the host's registry no longer accepts objects */
} Mm0Status;

const char *mm0_status_name(Mm0Status status);

/* Where a failure happened.  `reason` is a static string. */
typedef struct {
    Mm0Status status;
    const char *reason;
    int64_t offset;     /* byte offset in the MMB file of the failing command, or -1 */
    uint32_t line;      /* 1-based .mm0 position of the failure, or 0 */
    uint32_t column;
    char decl[96];      /* the declaration being checked, when known */
} Mm0Error;

typedef bool (*Mm0PollFn)(void *context);

typedef struct {
    /* Steps allowed before MM0_LIMIT; UINT64_MAX (the default) means no
     * budget.  A step is one proof, unify or specification command. */
    uint64_t budget;
    /* Polled every 4096 steps; returning true stops with MM0_INTERRUPTED. */
    Mm0PollFn poll;
    void *poll_context;
    /* Check an MMB file without a specification.  Its statements then come
     * from the file alone and carry no names: what is proved is only that
     * the file's theorems follow from the file's own axioms. */
    bool without_spec;
    /* Check only, retaining no declarations, as mm0-c does.  Used to compare
     * the two verifiers on the same work. */
    bool check_only;
} Mm0Options;

void mm0_options_init(Mm0Options *options);

typedef struct Mm0Theory Mm0Theory;

Mm0Theory *mm0_theory_new(void);
void mm0_theory_free(Mm0Theory *theory);

/* Check an MMB proof file against a .mm0 specification and append its
 * declarations to `theory`, which must be empty.  The specification is read
 * in step with the file, as mm0-c does: every sort, term, axiom, public
 * definition and public theorem must match the next statement of the
 * specification, and neither may continue past the other.  The load is all
 * or nothing: on failure the theory is left empty. */
Mm0Status mm0_load_mmb(Mm0Theory *theory, const uint8_t *mmb, size_t mmb_len,
                       const char *spec, size_t spec_len,
                       const Mm0Options *options, Mm0Error *error);

typedef struct {
    size_t sorts, terms, theorems;
    size_t bytes;        /* bytes held by the theory's tables and statements */
} Mm0TheoryStats;

Mm0TheoryStats mm0_theory_stats(const Mm0Theory *theory);

#endif
