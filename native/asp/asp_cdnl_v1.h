/* A ground answer-set solver: conflict-driven nogood learning with
 * source-pointer unfounded sets, emitting a checkable certificate when a
 * program has no stable model.
 *
 * It refines the verified reference driver of Mettapedia's
 * Logic/ASP/Driver.lean (Program.solve) step for step: the same variable
 * numbering, nogood order, watch-list order, decision order and conflict
 * analysis, so on any program it returns the same model or the same
 * certificate.  The certificate's steps are those of Certificate.lean
 * (CertStep): RUP additions with hints, and loop-nogood additions. */
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

typedef enum {
    ASP_STEP_RUP = 0,
    ASP_STEP_LOOP = 1,
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
} AspAnswer;

/* Whether the atoms are a stable model of the program: exactly the least
 * model of its reduct by them (the rules whose negative atoms they leave
 * false, without those atoms). */
bool asp_model_check(const AspRule *rules, uint32_t rule_count,
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
} AspSearch;

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
void asp_answer_free(AspAnswer *answer);

/* The certificate checker (Certificate.lean, Program.checkCert): every step is
 * accepted, RUP steps by unit propagation over their hints and loop steps by
 * recomputing the loop clause, and the empty clause is derived.  An accepted
 * certificate means the program has no stable model. */
bool asp_certificate_check(const AspRule *rules, uint32_t rule_count,
                           const AspStep *steps, uint32_t steps_len);

typedef struct {
    int32_t *atoms;
    uint32_t len;
} AspModel;

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
void asp_enumeration_free(AspEnumeration *enumeration);

/* Print an answer in the reference's text form. */
void asp_answer_print(FILE *out, const AspAnswer *answer);

#endif
