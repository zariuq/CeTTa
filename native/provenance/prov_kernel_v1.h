#ifndef CETTA_PROV_KERNEL_V1_H
#define CETTA_PROV_KERNEL_V1_H

#include <stdbool.h>
#include <stdint.h>

/* Why-provenance of derivations in a rule program.
 *
 * A program has base facts (p a1 ... an) and rules: a rule has a label, a head
 * literal and body literals, each (p v1 ... vn) over the rule's variables.  A
 * predicate that heads a rule is derived and has no facts; every other
 * predicate is base.  Each predicate has one arity, at least 1.
 *
 * With steps: (p a...) holds at grade g >= 1 when some rule with head p
 * derives it from body literals whose derived ones hold at grades summing to
 * g - 1 (base literals are facts, grade 0).  A derivation's support is the set
 * of the labels of the rules it uses.  The answer is the set of supports of
 * the goals' derivations at grades 1..K.  Grades strictly decrease from an item
 * to its children, so no fixpoint is needed.
 *
 * With labels: (p a...) is derivable without grades, and a support is a set
 * of at most K labels such that the goal has a derivation using exactly those
 * labels (a label may be used many times).  The families are the least
 * fixpoint of the same recurrence, computed below the goals only.
 *
 * A rule's label is either fixed or taken from the fact one of its base
 * literals matches: such a fact carries a label, so one rule can stand for
 * many labelled instances (a metarule and its metasubstitutions, say).
 *
 * Nodes, predicates and labels are dense ids chosen by the caller.  Supports
 * are reported as label ids in increasing order. */

#define PROV_ANY UINT32_MAX
#define PROV_NO_LABEL UINT32_MAX
#define PROV_MAX_ARITY 8u

/* a literal: a predicate and, per argument, a variable index (in a rule) or a
 * node id or PROV_ANY (in a goal) */
typedef struct {
    uint32_t pred, arity;
    uint32_t args[PROV_MAX_ARITY];
} ProvLit;

typedef struct ProvProgram ProvProgram;

typedef enum {
    PROV_OK = 0,
    PROV_OOM,
    PROV_UNSAFE_RULE,   /* a head variable is not bound by the body */
    PROV_MIXED_PRED,    /* a predicate both heads a rule and has facts */
    PROV_ARITY,         /* a predicate is used with two arities, or none */
    PROV_LIMIT,         /* an id, arity or grade beyond the kernel's limits */
    PROV_BAD_LABEL,     /* a label literal that is derived, or meets an unlabelled
                           fact, or a fact labelled twice differently */
    PROV_STOPPED,       /* the emitter asked to stop */
} ProvStatus;

ProvProgram *prov_new(void);
void prov_free(ProvProgram *p);

bool prov_add_fact(ProvProgram *p, uint32_t pred, uint32_t arity, const uint32_t *args);
/* a fact that labels the rule instances matching it through a label literal */
bool prov_add_labelled_fact(ProvProgram *p, uint32_t label, uint32_t pred,
                            uint32_t arity, const uint32_t *args);
bool prov_add_rule(ProvProgram *p, uint32_t label, const ProvLit *head,
                   uint32_t nvars, uint32_t nlits, const ProvLit *lits);
/* a rule whose instances take the label of the fact body literal label_lit
 * matches; that literal must be base, and its facts labelled */
bool prov_add_rule_labelled_by(ProvProgram *p, uint32_t label_lit, const ProvLit *head,
                               uint32_t nvars, uint32_t nlits, const ProvLit *lits);

/* emit returns false to stop */
typedef bool (*ProvEmit)(void *ctx, const uint32_t *labels, uint32_t n);

/* The supports of the goals' derivations of at most K rule instances; each
 * distinct support is emitted once. */
ProvStatus prov_supports_graded(ProvProgram *p, const ProvLit *goals,
                                uint32_t ngoals, uint32_t K,
                                ProvEmit emit, void *ctx);

/* The supports of at most K distinct labels of the goals' derivations. */
ProvStatus prov_supports_reuse(ProvProgram *p, const ProvLit *goals,
                               uint32_t ngoals, uint32_t K,
                               ProvEmit emit, void *ctx);

/* Goals in groups: one computation, the supports collected per group (the
 * examples of one problem, say); each support is emitted with its group, and
 * a support found in two groups is emitted for each. */
typedef bool (*ProvGroupEmit)(void *ctx, uint32_t group, const uint32_t *labels, uint32_t n);
ProvStatus prov_supports_graded_groups(ProvProgram *p, const ProvLit *goals, const uint32_t *group,
                                       uint32_t ngoals, uint32_t ngroups, uint32_t K,
                                       ProvGroupEmit emit, void *ctx);
ProvStatus prov_supports_reuse_groups(ProvProgram *p, const ProvLit *goals, const uint32_t *group,
                                      uint32_t ngoals, uint32_t ngroups, uint32_t K,
                                      ProvGroupEmit emit, void *ctx);

typedef struct {
    uint64_t items;          /* derived items */
    uint64_t family_items;   /* items whose families were computed */
    uint64_t supports;       /* supports stored over those families */
} ProvStats;
void prov_stats(const ProvProgram *p, ProvStats *out);

#endif
