/* A ground answer-set solver: conflict-driven nogood learning with
 * source-pointer unfounded sets, emitting a checkable certificate when a
 * program has no stable model.
 *
 * It refines the verified reference driver of Mettapedia's
 * Logic/ASP/Driver.lean (Program.solve) step for step: the same variable
 * numbering, nogood order, watch-list order, decision order and conflict
 * analysis, so on any program it returns the same model or the same
 * certificate.  The certificate's steps are those of Certificate.lean
 * (CertStep): RUP additions with hints, loop-nogood additions, and Hall
 * steps (the search with Hall's condition; HallStep.lean). */
#ifndef CETTA_ASP_CDNL_V1_H
#define CETTA_ASP_CDNL_V1_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int32_t head;
    const int32_t *pos;
    uint32_t pos_len;
    const int32_t *neg;
    uint32_t neg_len;
} AspRule;

/* A weight definition: the atom holds exactly when the true literals of its
 * body, the positive atoms pos and the atoms neg taken false, weigh at least
 * bound.  The atom is the head of no rule and no atom of the body depends
 * positively on it: the solver propagates the definition itself, and
 * explains each consequence by a clause the definition implies, a weight
 * step of the certificate.  Weights are positive. */
typedef struct {
    int32_t atom;
    uint64_t bound;
    const int32_t *pos;
    const uint64_t *pos_w;
    uint32_t pos_len;
    const int32_t *neg;
    const uint64_t *neg_w;
    uint32_t neg_len;
} AspWeightDef;

typedef enum {
    ASP_STEP_RUP = 0,
    ASP_STEP_LOOP = 1,
    ASP_STEP_HALL = 2,
    ASP_STEP_WEIGHT = 3,
    ASP_STEP_EUF = 4,
    ASP_STEP_LRA = 5,
    ASP_STEP_INPUT = 6,
} AspStepKind;

/* A certificate step.  Literals are native: +(n+1) or -(n+1) for variable
 * n of the program's numbering (atoms first, then distinct bodies). */
typedef struct {
    AspStepKind kind;
    uint32_t id;
    int32_t *clause;
    uint32_t clause_len;
    uint32_t *hints;      /* RUP: hint identifiers */
    uint32_t hints_len;
    uint32_t *atoms;      /* loop: the set's atom numbers */
    uint32_t atoms_len;
    uint32_t atom;        /* loop: the atom's number */
    uint32_t *groups;     /* hall: the at-least-one constraints, as rule indices */
    uint32_t groups_len;
    uint32_t *cliques;    /* hall: the at-most-one cliques' atom numbers, each
                           * clique ended by UINT32_MAX */
    uint32_t cliques_len;
    uint32_t def;         /* weight: the definition's index; the clause is one
                           * every assignment satisfying it satisfies */
    char **weights;       /* lra: per clause literal, its Farkas coefficient,
                           * exact text */
    uint32_t weights_len;
} AspStep;

typedef enum {
    ASP_ANSWER_SAT = 0,
    ASP_ANSWER_UNSAT = 1,
    ASP_ANSWER_STUCK = 2,   /* never expected: the reference proves totality */
    ASP_ANSWER_ERROR = 3,   /* allocation failure */
    ASP_ANSWER_REFUSED = 4, /* a fast answer its own check refused: a solver fault */
} AspAnswerKind;

typedef struct {
    AspAnswerKind kind;
    int32_t *model;       /* true atoms, in the program's atom order */
    uint32_t model_len;
    AspStep *steps;       /* the certificate, oldest step first */
    uint32_t steps_len;
    char **reals;         /* the SMT personality's model: each real's value,
                           * exact text */
    uint32_t reals_len;
    /* with an objective: its least value under the model's bounds, the
     * infimum c (exact text), whether it is attained, or unbounded */
    char *objective;
    bool objective_attained;
    bool objective_unbounded;
} AspAnswer;

/* Whether the atoms are a stable model of the program: exactly the least
 * model of its reduct by them (the rules whose negative atoms they leave
 * false, without those atoms). */
bool asp_model_check(const AspRule *rules, uint32_t rule_count,
                     const int32_t *atoms, uint32_t len);
/* With weight definitions: each definition's atom is in the atoms exactly
 * when its body weighs at least its bound in them, and the other atoms are
 * the least model of the reduct with the definitions' atoms given. */
bool asp_model_check_weighted(const AspRule *rules, uint32_t rule_count,
                              const AspWeightDef *defs, uint32_t def_count,
                              const int32_t *atoms, uint32_t len);

/* How the solver searches.  ASP_SEARCH_REFERENCE takes the steps of the
 * verified driver (Driver.lean) one for one, so its answers and certificates
 * are the reference's byte for byte.  ASP_SEARCH_FAST re-queues only the
 * literal a backjump asserts, watches a learned nogood on its asserting
 * literal and its highest-level other literal, and keeps watch lists in
 * place.  It checks its own answers: a model by asp_model_check, a refutation
 * by asp_certificate_check, and answers ASP_ANSWER_REFUSED where either
 * refuses. */
typedef enum {
    ASP_SEARCH_FAST,
    ASP_SEARCH_REFERENCE,
    ASP_SEARCH_FAST_HALL,   /* the fast search with Hall's condition checked
                             * over the program's at-least-one constraints
                             * and its cliques of pairwise exclusions; its
                             * refutations may hold Hall steps */
} AspSearch;

/* Counters of the last solve in this thread, for measurement. */
typedef struct {
    uint32_t groups, cliques, clique_atoms, group_atoms;
    uint64_t hall_checks, hall_searches, hall_conflicts, conflicts, decisions;
} AspHallStats;
AspHallStats asp_hall_last_stats(void);

/* Solve the ground program, by the fast search.  The answer owns its
 * arrays. */
AspAnswer asp_cdnl_solve(const AspRule *rules, uint32_t rule_count);

/* An observer of a solve's steps, over the certificate's numbering
 * (variable v as v + 1, its atoms first): the atoms the variables are, then
 * each assignment (a decision, or implied by a nogood), each conflicting
 * nogood, each nogood learned with the level the solver returns to, and each
 * unfounded set.  Nogoods are numbered from 0 in the order the solver holds
 * them, the completion's first.  Observing changes nothing the solver does. */
typedef enum {
    ASP_TRACE_ATOM,      /* lit: the variable; level: the program's atom id */
    ASP_TRACE_DECIDE,    /* lit, level */
    ASP_TRACE_IMPLY,     /* lit, level, nogood */
    ASP_TRACE_CONFLICT,  /* nogood */
    ASP_TRACE_LEARN,     /* nogood, lits, level */
    ASP_TRACE_UNFOUNDED, /* lits: the atoms' variables */
} AspTraceKind;

typedef struct {
    void (*event)(void *context, AspTraceKind kind, int32_t lit,
                  uint32_t level, int32_t nogood, const int32_t *lits,
                  uint32_t count);
} AspTraceSink;

AspAnswer asp_cdnl_solve_traced(const AspRule *rules, uint32_t rule_count,
                                const AspTraceSink *trace, void *context);
/* Either search, observed or not (trace may be NULL). */
AspAnswer asp_cdnl_solve_search(const AspRule *rules, uint32_t rule_count,
                                AspSearch search, const AspTraceSink *trace,
                                void *context);
/* The same over rules and weight definitions; the reference search takes
 * no definitions (ASP_ANSWER_ERROR). */
AspAnswer asp_cdnl_solve_weighted(const AspRule *rules, uint32_t rule_count,
                                  const AspWeightDef *defs, uint32_t def_count,
                                  AspSearch search, const AspTraceSink *trace,
                                  void *context);
void asp_answer_free(AspAnswer *answer);

/* A solve as a session of steps: the same search, one iteration at a time.  A
 * step propagates to a fixpoint and then analyses a conflict and backjumps, or
 * decides (the fast search also restarts on its schedule).  The trail is
 * observable between steps, over the certificate's numbering; once the search
 * is done, asp_session_answer gives exactly the answer asp_cdnl_solve_search
 * gives, checked the same way. */
typedef struct AspSession AspSession;

typedef enum {
    ASP_SESSION_RUNNING = 0,
    ASP_SESSION_DONE = 1,
} AspSessionStatus;

typedef struct {
    int32_t lit;        /* +(v+1) or -(v+1) */
    uint32_t level;
    int32_t reason;     /* the implying nogood, or -1 for a decision */
} AspTrailEntry;

typedef struct {
    uint32_t atoms, variables, level, trail, steps;
    uint64_t conflicts;
} AspSessionCounts;

AspSession *asp_session_new(const AspRule *rules, uint32_t rule_count,
                            AspSearch search, const AspTraceSink *trace,
                            void *context);
AspSession *asp_session_new_weighted(const AspRule *rules, uint32_t rule_count,
                                     const AspWeightDef *defs,
                                     uint32_t def_count, AspSearch search,
                                     const AspTraceSink *trace, void *context);
AspSessionStatus asp_session_step(AspSession *session);
/* The trail's length; its first `capacity` entries are copied to `out`. */
uint32_t asp_session_trail(const AspSession *session, AspTrailEntry *out,
                           uint32_t capacity);
AspSessionCounts asp_session_counts(const AspSession *session);
/* The program's atom that variable `variable` (an atom variable) stands for, or -1. */
int32_t asp_session_atom(const AspSession *session, uint32_t variable);
AspAnswer asp_session_answer(AspSession *session);
/* Multi-shot.  After a step that found a stable model (ASP_SESSION_DONE with
 * asp_session_model true), its true atoms (input ids, the caller frees), the
 * model checked against the program and every added constraint.  Then
 * constraints may be added and the search goes on, its learned nogoods kept:
 * each constraint (no stable model has every POS atom true and every NEG atom
 * false, over the program's atoms) is a new input, an input step of the
 * certificate (ASP_STEP_INPUT, its clause the constraint's), which the
 * checker accepts exactly for an added constraint.  A constraint holding an
 * atom the program does not mention is skipped (no stable model meets it),
 * one negated is dropped from it (always false); the checker is given the
 * constraints so stored.  False on an allocation
 * failure or when the session has finished. */
bool asp_session_model(AspSession *session, int32_t **atoms, uint32_t *len);
bool asp_session_constrain(AspSession *session, const int32_t *pos, uint32_t npos,
                           const int32_t *neg, uint32_t nneg);
/* Heuristics as data.  The next decision, made by the next step after it
 * propagates, if the atom (an input id) is still unassigned then: 1 when
 * set, 0 when the atom is false now and 2 when it is true now, -1 for an
 * atom the program does not have or a finished session.  A decision bears no proof obligation: the
 * search stays sound and complete whatever it decides. */
int asp_session_decide(AspSession *session, int32_t atom, bool value);
/* The nogoods made so far (learned, loop, weight, Hall and input ones; the
 * completion's are the program's), each as literals over the certificate's
 * numbering, +(v+1) for variable v true and -(v+1) false: *lits holds them
 * one after another, the k-th from (*starts)[k] to (*starts)[k + 1], *count
 * of them (the caller frees both).  False on an allocation failure. */
bool asp_session_nogoods(const AspSession *session, int32_t **lits,
                         uint32_t **starts, uint32_t *count);
void asp_session_free(AspSession *session);

/* The certificate checker (Certificate.lean, Program.checkCert): every step is
 * accepted, RUP steps by unit propagation over their hints and loop steps by
 * recomputing the loop clause, and the empty clause is derived.  An accepted
 * certificate means the program has no stable model. */
bool asp_certificate_check(const AspRule *rules, uint32_t rule_count,
                           const AspStep *steps, uint32_t steps_len);
/* With weight definitions, whose weight steps are checked by arithmetic: a
 * clause the falsifying assignments of which all violate the definition. */
/* The checks over a program and constraints added to it (multi-shot): the
 * constraints as AspRule bodies (pos, neg; head unused). */
bool asp_model_check_constrained(const AspRule *rules, uint32_t rule_count,
                                 const AspWeightDef *defs, uint32_t def_count,
                                 const AspRule *extra, uint32_t nextra,
                                 const int32_t *atoms, uint32_t len);
bool asp_certificate_check_constrained(const AspRule *rules, uint32_t rule_count,
                                       const AspWeightDef *defs, uint32_t def_count,
                                       const AspRule *extra, uint32_t nextra,
                                       const AspStep *steps, uint32_t steps_len);
bool asp_certificate_check_weighted(const AspRule *rules, uint32_t rule_count,
                                    const AspWeightDef *defs,
                                    uint32_t def_count, const AspStep *steps,
                                    uint32_t steps_len);

typedef struct {
    int32_t *atoms;
    uint32_t len;
} AspModel;

/* The SAT personality: the same kernel and fast search over a CNF.  Its
 * variables are 1..nvars, decided as atoms are; clause i is an input named
 * i + 1, as in LRAT.  A model lists the true variables; a refutation is a
 * certificate of RUP steps over the clauses, over the same numbering, so
 * it is an LRAT proof.  Each answer is checked as the ASP answers are. */
typedef struct {
    const int32_t *lits;   /* DIMACS literals: v or -v, 1 <= v <= nvars */
    uint32_t len;
} AspClause;

AspAnswer asp_sat_solve(uint32_t nvars, const AspClause *clauses, uint32_t nclauses);
bool asp_sat_model_check(uint32_t nvars, const AspClause *clauses, uint32_t nclauses,
                         const int32_t *vars, uint32_t len);
bool asp_sat_certificate_check(uint32_t nvars, const AspClause *clauses,
                               uint32_t nclauses, const AspStep *steps,
                               uint32_t steps_len);
/* The SMT personality: the SAT personality's clauses, some of whose
 * variables are equality atoms over ground terms of uninterpreted symbols.
 * Term i is symbol(args...), its arguments earlier terms; equality atom
 * `var` (1 <= var <= nvars) holds exactly when terms lhs and rhs are equal.
 * The theory of equality with uninterpreted functions propagates by
 * congruence closure (native/smt/euf_cc_v1.h): an atom that holds merges its
 * terms, one that fails keeps them apart, and a conflict or an implied atom
 * is a nogood with an EUF step, a clause whose falsifying assignments make
 * true equalities that entail, by congruence, one they make false; the
 * checker recomputes that closure.  Models and refutations are checked as
 * the SAT personality's are, the models also for the theory. */
typedef struct {
    uint32_t symbol;
    const uint32_t *args;
    uint32_t nargs;
} AspSmtTerm;

typedef struct {
    uint32_t var;
    uint32_t lhs, rhs;
} AspSmtEquality;

/* Linear real arithmetic (native/smt/lra_simplex_v1.h): bound atom `var`
 * holds exactly when sum coefs[i] * x[reals[i]] REL bound, coefficients and
 * bound exact text ("-3", "7/2").  The theory checks the bounds the trail
 * asserts by the simplex, and a conflict is a nogood with an LRA step, a
 * clause with a Farkas coefficient per literal: the bounds its falsification
 * asserts, so weighed, sum to a contradiction, which the checker verifies
 * exactly.  A model gives each real an exact value. */
typedef enum {
    ASP_LRA_LE = 0,
    ASP_LRA_LT = 1,
    ASP_LRA_GE = 2,
    ASP_LRA_GT = 3,
} AspLraRelation;

typedef struct {
    uint32_t var;
    const uint32_t *reals;
    const char *const *coefs;
    uint32_t n;
    AspLraRelation rel;
    const char *bound;
} AspLraAtom;

/* The theories of an SMT problem: EUF terms and equality atoms, and LRA
 * reals and bound atoms.  No real is an EUF term: the theories share no
 * symbol. */
typedef struct {
    const AspSmtTerm *terms;
    uint32_t nterms;
    const AspSmtEquality *eqs;
    uint32_t neqs;
    uint32_t nreals;
    const AspLraAtom *lra;
    uint32_t nlra;
    /* optional: a linear form of the reals to minimize (n = 0: none); a model
     * then puts the reals where it is least under the model's bounds */
    const uint32_t *objective_reals;
    const char *const *objective_coefs;
    uint32_t objective_n;
} AspSmtTheories;

AspAnswer asp_smt_solve(uint32_t nvars, const AspClause *clauses,
                        uint32_t nclauses, const AspSmtTheories *theories);
/* `reals`: each real's value, exact text (NULL when there are none). */
bool asp_smt_model_check(uint32_t nvars, const AspClause *clauses,
                         uint32_t nclauses, const AspSmtTheories *theories,
                         const int32_t *vars, uint32_t len,
                         const char *const *reals);
bool asp_smt_certificate_check(uint32_t nvars, const AspClause *clauses,
                               uint32_t nclauses, const AspSmtTheories *theories,
                               const AspStep *steps, uint32_t steps_len);

/* The certificate's RUP steps as LRAT lines: id literals 0 hints 0 */
void asp_lrat_print(FILE *out, const AspStep *steps, uint32_t steps_len);

/* All stable models, each once, in the order found, and the refutation of
 * the program with every one of them blocked (kind ASP_ANSWER_UNSAT).  The
 * blocking constraints use the fresh atom none_label. */
typedef struct {
    AspAnswerKind kind;
    AspModel *models;
    uint32_t models_len;
    AspAnswer final;
    int32_t none_label;
} AspEnumeration;

AspEnumeration asp_cdnl_enumerate(const AspRule *rules, uint32_t rule_count);
AspEnumeration asp_cdnl_enumerate_search(const AspRule *rules, uint32_t rule_count,
                                         AspSearch search);
AspEnumeration asp_cdnl_enumerate_weighted(const AspRule *rules,
                                           uint32_t rule_count,
                                           const AspWeightDef *defs,
                                           uint32_t def_count,
                                           AspSearch search);
void asp_enumeration_free(AspEnumeration *enumeration);

/* Print an answer in the reference's text form. */
void asp_answer_print(FILE *out, const AspAnswer *answer);

#endif
