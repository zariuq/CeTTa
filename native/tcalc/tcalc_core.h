/* tcalc_core.h - the transform calculus as a library: terms, rules, programs,
 * and the outcomes of a program under a stated interpretation.
 *
 * This header is the whole interface of a tcalc core.  The CLI, the MeTTa
 * module and any other implementation of the calculus (a verified machine-code
 * core, for one) meet here, so that their outcomes can be compared program by
 * program.  The calculus itself is described at the top of tcalc_core.c. */
#ifndef CETTA_TCALC_CORE_H
#define CETTA_TCALC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- Terms ---------------------------------------------------------------
 * Terms are hash-consed: two equal terms have one id.  Id 0 is no term. */

typedef uint32_t TcTerm;

typedef enum {
    TC_SYMBOL,     /* a name, alone or at the head of an expression          */
    TC_INTEGER,    /* a 64-bit integer                                        */
    TC_STRING,     /* a string of bytes                                       */
    TC_LOCAL,      /* in a rule: one of the rule's own unknowns              */
    TC_UNKNOWN,    /* in a state: an unknown that nothing has determined yet */
    TC_EXPRESSION, /* an ordered tuple (t1 ... tn), n >= 0                    */
} TcKind;

typedef struct TcTerms TcTerms;

TcTerms *tc_terms_new(void);
void tc_terms_free(TcTerms *terms);

TcTerm tc_symbol(TcTerms *terms, const char *bytes, size_t length);
TcTerm tc_integer(TcTerms *terms, int64_t value);
TcTerm tc_string(TcTerms *terms, const char *bytes, size_t length);
TcTerm tc_local(TcTerms *terms, uint32_t index);
TcTerm tc_unknown(TcTerms *terms, uint32_t index);
TcTerm tc_expression(TcTerms *terms, const TcTerm *items, uint32_t count);

TcKind tc_kind(const TcTerms *terms, TcTerm term);
uint32_t tc_length(const TcTerms *terms, TcTerm term);     /* expression  */
TcTerm tc_item(const TcTerms *terms, TcTerm term, uint32_t i);
const char *tc_bytes(const TcTerms *terms, TcTerm term, size_t *length); /* symbol, string */
int64_t tc_value(const TcTerms *terms, TcTerm term);        /* integer     */
uint32_t tc_index(const TcTerms *terms, TcTerm term);       /* local, unknown */

/* Writes a term in MeTTa's notation: a local as $i, an unknown as ?i. */
void tc_print(const TcTerms *terms, TcTerm term, FILE *out);

/* ---- Rules and programs (RuleSyntax/Core.lean: Rule, Program) ------------
 * A rule's unknowns are its locals 0 .. locals-1. */

typedef enum {
    TC_CHECK_EQUAL,  /* left = right at every solution                     */
    TC_CHECK_ABSENT, /* no present row matches left at any solution        */
    TC_CHECK_COUNT,  /* exactly count rows match left, at every solution   */
    TC_CHECK_CENSUS, /* the rows that match left are the same rows at every
                        solution (Core.lean: a census pattern and the rows
                        it claims to see)                                  */
} TcCheckKind;

typedef struct {
    TcCheckKind kind;
    TcTerm left, right;
    uint64_t count;
} TcCheck;

typedef struct {
    const char *name;
    const TcTerm *take;   uint32_t takes;   /* rows consumed                    */
    const TcTerm *read;   uint32_t reads;   /* rows read and kept              */
    const TcTerm *tell;   uint32_t tells;   /* equations: tell[2i] = tell[2i+1] */
    const TcCheck *check; uint32_t checks;
    const TcTerm *make;   uint32_t makes;   /* rows made                        */
    uint32_t locals;
} TcRule;

typedef struct TcProgram TcProgram;

TcProgram *tc_program_new(TcTerms *terms);
void tc_program_free(TcProgram *program);
/* Copies the rule.  A rule is refused (false) when a term mentions a local
 * outside 0 .. locals-1 or an unknown.  A program is a set of rules up to the
 * names of their locals: a rule equal to an earlier one up to renaming is that
 * rule again, and adds no matches of its own. */
bool tc_program_add_rule(TcProgram *program, const TcRule *rule);
/* A row of the initial state.  It may mention unknowns, never locals. */
bool tc_program_add_row(TcProgram *program, TcTerm row);
uint32_t tc_program_rules(const TcProgram *program);
/* The program's rows: its initial state, as added. */
const TcTerm *tc_program_rows(const TcProgram *program, size_t *n);
const char *tc_program_rule_name(const TcProgram *program, uint32_t rule);
TcTerms *tc_program_terms(TcProgram *program);

/* ---- The interpretation of a run ------------------------------------------
 * tcalc is a moving target: a family of transform steps that differ in a few
 * choices (a bag or a set store; how many matches one step commits; checks
 * asked before or after the firing's own tells; rules beside the store or held
 * in it; one store or several worlds), of which each implementation is one
 * point.  An interpretation names the point a run is computed at.  This core
 * reaches the points where rules sit beside one store: what one step commits is
 * a discipline over bags of matches (the members of RuleSyntax/JointFirings);
 * whether the checks see the firing's own tells; whether the store is a
 * multiset or a set; and what counts as one match.  The axes and their
 * meanings are declared once, in tcalc_axes.data:
 *   TcStep:    ONE ROUND SYNCHRONOUS BAG EVERY MAXIMAL UNIQUE
 *   TcAsk:     AFTER_TELLS (Core.lean) BEFORE_TELLS
 *   TcStore:   MULTISET (Core.lean) SET
 *   TcMatches: SYMMETRIC KEYS (JointFirings)
 * Every axis is chosen explicitly; nothing has a hidden default.  The count of
 * matches decides every and unique only: two matches that differ by which of
 * two identical patterns took which row are one match up to symmetry, two keys. */
#include "tcalc_axes_enums.generated.h"
#include "../../src/generated/runner_axes.generated.h"

/* ---- Another decider of firings (backend x86-flat) -------------------------
 * A flat firing is packed as flat_fire takes it: the selected rows, four words
 * each (term ids, unused words 0), as premise offsets into the state's packed
 * rows; the rule's constants, which follow the premises as one more row;
 * guards, each two (row, column) selections packed into bytes as
 * row | column << 8 | row' << 16 | column' << 24, which must be equal words;
 * and make entries, each a selection row | column << 8 whose word is copied
 * out.  A decider returns 1 with nmakes words in out, 0 when a guard fails, or
 * 2 when it does not take the descriptor. */
typedef uint64_t (*TcFlatDecider)(const uint32_t *source, size_t source_words,
                                  const uint32_t *premises, size_t npremises,
                                  const uint32_t *constants, size_t nconstants,
                                  const uint32_t *guards, size_t nguards,
                                  const uint32_t *makes, size_t nmakes, uint32_t *out);

/* Whether a flat decider can decide every firing of the program under step
 * one: its rows are ground expressions of one to four items; each rule's
 * patterns and made rows are such expressions whose items are ground terms or
 * the rule's locals, every local of a made row occurring in a pattern; no rule
 * tells or checks; at most 24 patterns, 32 constants, 32 guards and 32 made
 * words a rule.  Otherwise the first reason it cannot is written to `why`. */
bool tc_program_flat(const TcProgram *program, char *why, size_t why_size);

/* Not part of the interpretation: the output is the same either way.  The
 * exploration's shortcuts are a successor merged rather than sorted at a state
 * of fixed terms, and the second order of an independent pair of firings
 * looked up rather than fired (tcalc_core.c, section 5).  Computing every
 * successor in full is the reference; computing both checks the shortcuts
 * against it (tc_outcomes_shortcuts_disagreed). */
typedef enum {
    TC_SHORTCUTS_TAKEN,
    TC_SHORTCUTS_NONE,
    TC_SHORTCUTS_CHECKED,
} TcShortcuts;

typedef struct {
    TcStep step;
    TcAsk ask;
    TcStore store;
    TcMatches matches;
    uint64_t budget;        /* units of work: states expanded, bags examined,
                               orders tried; never exceeded                   */
    const uint32_t *region; /* every: the rules it ranges over                 */
    uint32_t regions;       /* 0: every rule of the program                    */
    TcBackend backend;      /* x86-flat: `flat` decides each firing, under step
                               one, of a program tc_program_flat accepts      */
    TcFlatDecider flat;
    TcObserve observe;      /* run: the outcomes are instead the states of one
                               run, in order, its steps chosen by the
                               schedule; the run ends with no step, at a state
                               it has been in (cyclic), or when the budget is
                               spent                                          */
    RunSchedule schedule;   /* the runner's, shared with rho (lib/runner_axes
                               .data).  Each step of a run takes, of its
                               successors in a structural order of states (rows
                               compared as terms, never by identity): canonical,
                               the first; rotating, the n-th at the n-th step,
                               counted round                                  */
    TcShortcuts shortcuts;  /* not an axis: how successors are found          */
} TcInterpretation;

/* ---- Outcomes ------------------------------------------------------------
 * An outcome is a state in which no step is admitted.  States are taken up to
 * the identities of their rows and the names of their unknowns, so two
 * outcomes are different states.  Rows are listed in a canonical order, which
 * is an order of printing and nothing else: an outcome is a bag. */

typedef struct TcOutcomes TcOutcomes;

TcOutcomes *tc_explore(TcProgram *program, const TcInterpretation *how);
/* One step instead of an exploration: the states that one step of the
 * interpretation's discipline reaches from the state `rows` (rows of the
 * program's terms, with unknowns of that state), each once, read with the
 * accessors below in place of outcomes.  None: the state is quiescent; under
 * unique an ambiguous state is listed by tc_ambiguous_rows instead.  A set
 * store's firing that changes nothing is no step.  `observe` is not read.
 * NULL when a row mentions a local or memory ran out. */
TcOutcomes *tc_successors(TcProgram *program, const TcInterpretation *how,
                          const TcTerm *rows, size_t n);
/* Whether the state `rows` is quiescent under the interpretation: no step of
 * its discipline is admitted there (under unique, an ambiguous state has no
 * step either).  1 or 0; -1 when a row mentions a local or memory ran out. */
int tc_quiescent(TcProgram *program, const TcInterpretation *how,
                 const TcTerm *rows, size_t n);
/* The canonical key of a state: its rows (terms of `terms`, with unknowns of
 * the state) in canonical order, unknowns renamed in order of first appearance,
 * and under a set store each row once.  Two states with one key are one state,
 * and so are their rows' meanings; the exploration compares states by it.
 * A new array of *count rows, to be freed by the caller; NULL when memory ran
 * out (or a row mentions a local). */
TcTerm *tc_canonical_key(TcTerms *terms, const TcTerm *rows, size_t n, TcStore store,
                         size_t *count);
void tc_outcomes_free(TcOutcomes *outcomes);

size_t tc_outcomes_count(const TcOutcomes *outcomes);
const TcTerm *tc_outcome_rows(const TcOutcomes *outcomes, size_t i, size_t *rows);
/* unique: states at which more than one match was admitted; each is listed
 * with the number of matches, and its successors were not explored. */
size_t tc_outcomes_ambiguous(const TcOutcomes *outcomes);
const TcTerm *tc_ambiguous_rows(const TcOutcomes *outcomes, size_t i, size_t *rows,
                                size_t *matches);
/* False when the budget ran out: the outcomes listed are then a part. */
bool tc_outcomes_finished(const TcOutcomes *outcomes);
uint64_t tc_outcomes_states(const TcOutcomes *outcomes);
uint64_t tc_outcomes_work(const TcOutcomes *outcomes);
/* False when memory ran out; the outcomes are then incomplete as well. */
bool tc_outcomes_complete_memory(const TcOutcomes *outcomes);
/* True when the steps found among the states reached contain a cycle: some run
 * never ends.  (In a set store a firing that changes nothing is no step.) */
bool tc_outcomes_cyclic(const TcOutcomes *outcomes);
/* backend x86-flat: true when the decider gave no verdict on a firing (or the
 * interpretation was not one it decides); the outcomes are then incomplete. */
bool tc_outcomes_decider_failed(const TcOutcomes *outcomes);
/* Shortcuts checked: true when a shortcut found a successor other than the
 * full computation's (the outcomes are the full computation's). */
bool tc_outcomes_shortcuts_disagreed(const TcOutcomes *outcomes);

/* ---- A firing decided over a host's store ----------------------------------
 * The seam agreed with transform's owner (header v2, sha256 92d8e0b0...):
 *   - the host (transform) proposes a selection with its own cursors and query
 *     engine, lowers its surface to a tcalc rule (differences named, never
 *     conformed), and commits through its own hooks under its dialect's
 *     multiplicity policy;
 *   - tcalc decides the selection by the protocol tc_fire (Fire.module: mark
 *     the trail; tells and checks in the ask axis's order; admit, or discard),
 *     reading the store only through TcStoreOps.
 * Nothing copies a store: a row is imported into tcalc's terms only when it is
 * selected or when a check (absent, count, census) meets it.
 *
 * The decision is per firing: no knowledge persists from one tc_decide to the
 * next.  Bindings the host already holds are in the lowered terms of the rule,
 * never discarded.  The host names this in its interpretation metadata;
 * TcInterpretation has no field for it. */

/* One row occurrence, the host adapter's own handle: it names a row of the
 * store and the pinned view it was read in, never a bare index whose meaning
 * depends on a later view.  Distinct occurrences are distinct rows, whatever
 * their terms. */
typedef uint64_t TcOccurrence;

/* The decision in progress, tcalc's. */
typedef struct TcDecideScope TcDecideScope;

/* A fresh unknown of the decision in progress.  The adapter makes every
 * imported row's variables with it: a held schema's anew at each import, a
 * variable shared between rows once per decision (through the adapter's own
 * map), so imported unknowns never collide with the rule's own variables.
 * 0 when memory runs out. */
TcTerm tc_decide_unknown(TcDecideScope *scope);

typedef enum {
    TC_STORE_ROW = 1,          /* next: an occurrence; open, import: done */
    TC_STORE_END = 0,          /* next: no more occurrences */
    TC_STORE_INVALIDATED = -1, /* the pinned view is no longer valid; never read as END */
    TC_STORE_INTERRUPTED = -2, /* cancelled by the host (fuel, interruption) */
    TC_STORE_NO_MEMORY = -3
} TcStoreStatus;

typedef enum {
    TC_COMMIT_DONE = 1,
    TC_COMMIT_REFUSED = 0,      /* the host's policy refused it; the store is unchanged */
    TC_COMMIT_HOST_ERROR = -1,  /* failed after host effects may have happened: no retry,
                                   no fallback; the host keeps its own error payload */
    TC_COMMIT_INTERRUPTED = -2, /* cancelled; same rule as HOST_ERROR once effects began */
    TC_COMMIT_NO_MEMORY = -3    /* before any effect: the store is unchanged */
} TcCommitStatus;

/* During a decision T's table is lent to it: the TcTerms each callback below
 * receives is the decision's active table, not the T the caller passed.  A
 * callback reads and writes terms through the table it is given; T holds the
 * table again, with every term made meanwhile, when tc_decide returns. */
typedef struct TcStoreOps {
    void *store;
    /* The occurrences that may meet PATTERN (a term of T, unknowns allowed); a
     * superset is fine, tcalc unifies.  The host's read view is pinned from
     * open to close.  TC_STORE_ROW and *CURSOR on success. */
    TcStoreStatus (*open)(void *store, const TcTerms *t, TcTerm pattern, void **cursor);
    /* TC_STORE_ROW and the next occurrence, TC_STORE_END, or a failure, which
     * ends the decision without effects (exhaustion never hides it). */
    TcStoreStatus (*next)(void *cursor, TcOccurrence *occurrence);
    void (*close)(void *cursor);
    /* The occurrence's row term, imported into T with unknowns from SCOPE. */
    TcStoreStatus (*import)(void *store, TcDecideScope *scope, TcTerms *t,
                            TcOccurrence occurrence, TcTerm *row);
    /* Commit one admitted firing: TAKEN (distinct) removed, MADE (terms of T)
     * added, in the host's order (publications before removals) and under the
     * host's multiplicity policy.  Host mutation is sequential: there is no
     * rollback after an effect, and tcalc never retries. */
    TcCommitStatus (*commit)(void *store, const TcTerms *t, const TcOccurrence *taken, size_t ntaken,
                             const TcTerm *made, size_t nmade);
    /* Optional (NULL for none): asked before each unit of work (a row a check
     * examines, a selected row unified); 0 cancels, and the decision ends
     * INTERRUPTED, before any effect. */
    int (*spend)(void *store, uint64_t units);
} TcStoreOps;

typedef enum {
    TC_DECIDED_ADMITTED,      /* committed: TC_COMMIT_DONE */
    TC_DECIDED_DECLINED,      /* the protocol declined: not enabled, unsolvable, or a check failed */
    TC_DECIDED_STORE_REFUSED, /* admitted by tcalc, refused by the host's policy; no effect */
    TC_DECIDED_INVALIDATED,   /* a cursor's view was invalidated before any effect */
    TC_DECIDED_INTERRUPTED,   /* cancelled: before any effect, or reported by commit */
    TC_DECIDED_HOST_ERROR,    /* the commit failed after effects may have happened: no
                                 retry, no fallback, the host's payload stands */
    TC_DECIDED_NO_MEMORY      /* before any effect */
} TcDecision;

/* Decide RULE of P at SELECTED (one occurrence per take and read pattern, in
 * the rule's order) under HOW, by tc_fire as one match (the step is one):
 * open the rule apart, import the selected rows, unify, tell, check at every
 * solution (absence, counts and census read the store through OPS), then
 * commit through OPS or discard.  Takes must be distinct occurrences and a
 * read is never a taken one, or the selection is not enabled (DECLINED).
 *
 * P's terms are T: P was built in T, and T also receives the imported rows
 * and the rows made.  On TC_DECIDED_ADMITTED, *MADE and *NMADE give the rows
 * made, terms of T in a buffer T owns, valid until the next tc_decide with T
 * or until T is released.  ADMITTED commits; HOST_ERROR, and INTERRUPTED
 * reported by the commit, may have changed the store; every other outcome
 * leaves it unchanged. */
TcDecision tc_decide(TcProgram *p, uint32_t rule, const TcInterpretation *how,
                     const TcStoreOps *ops, const TcOccurrence *selected, size_t nselected,
                     TcTerms *t, const TcTerm **made, size_t *nmade);

#endif
