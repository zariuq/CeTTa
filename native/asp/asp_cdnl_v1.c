/* A ground answer-set solver refining Mettapedia's verified reference driver
 * (Logic/ASP/Driver.lean, Program.solve) step for step.  Lean lists that are
 * kept newest first are arrays here with the newest element last, except the
 * watch lists, which keep Lean's front-first order.  Each function names the
 * Lean definition it refines. */
#include "asp_cdnl_v1.h"
#include "../smt/euf_cc_v1.h"
#include "../smt/lra_simplex_v1.h"
#include "asp_hall_v1.h"
#include "../support/support_v1.h"

#include <stdlib.h>
#include <string.h>

/* ---- small vectors ------------------------------------------------------ */

typedef struct {
    int32_t *v;
    uint32_t len;
    uint32_t cap;
} IVec;

static bool iv_reserve(IVec *a, uint32_t need) {
    if (need <= a->cap)
        return true;
    uint32_t cap = a->cap ? a->cap : 8u;
    while (cap < need)
        cap *= 2u;
    int32_t *v = realloc(a->v, sizeof(*v) * cap);
    if (!v)
        return false;
    a->v = v;
    a->cap = cap;
    return true;
}

static bool iv_push(IVec *a, int32_t x) {
    if (!iv_reserve(a, a->len + 1u))
        return false;
    a->v[a->len++] = x;
    return true;
}

static void iv_free(IVec *a) {
    free(a->v);
    a->v = NULL;
    a->len = a->cap = 0u;
}

static bool iv_contains(const IVec *a, int32_t x) {
    for (uint32_t i = 0u; i < a->len; i++)
        if (a->v[i] == x)
            return true;
    return false;
}

/* List.dedup: each element once, keeping its last occurrence, in order.
 * A long list is read from its end against a set of the values met, so each
 * element is kept exactly when no later one equals it. */
static bool iv_dedup(IVec *out, const int32_t *xs, uint32_t n) {
    out->len = 0u;
    if (n <= 16u) {
        for (uint32_t i = 0u; i < n; i++) {
            bool later = false;
            for (uint32_t j = i + 1u; j < n && !later; j++)
                later = xs[j] == xs[i];
            if (!later && !iv_push(out, xs[i]))
                return false;
        }
        return true;
    }
    uint32_t cap = 32u;
    while (cap < 2u * n)
        cap <<= 1;
    int32_t *keys = malloc(sizeof(*keys) * cap);
    uint8_t *used = calloc(cap, 1u);
    uint8_t *keep = malloc(n);
    bool ok = keys && used && keep && iv_reserve(out, n);
    for (uint32_t i = n; ok && i-- > 0u;) {
        uint32_t at = ((uint32_t)xs[i] * 2654435761u) & (cap - 1u);
        while (used[at] && keys[at] != xs[i])
            at = (at + 1u) & (cap - 1u);
        keep[i] = !used[at];
        used[at] = 1u;
        keys[at] = xs[i];
    }
    for (uint32_t i = 0u; ok && i < n; i++)
        if (keep[i])
            out->v[out->len++] = xs[i];
    free(keys);
    free(used);
    free(keep);
    return ok;
}

/* ---- literals: variable * 2 + sign (1 = true) ---------------------------- */

#define LIT(var, sign) ((int32_t)((uint32_t)(var) * 2u + ((sign) ? 1u : 0u)))
#define LIT_VAR(l) ((uint32_t)(l) >> 1)
#define LIT_SIGN(l) ((uint32_t)(l) & 1u)
#define LIT_COMPL(l) ((l) ^ 1)
#define ASP_NO_BODY UINT32_MAX

static _Thread_local AspHallStats g_hall_stats;

/* ---- solver state --------------------------------------------------------- */

typedef struct {
    IVec lits;
    /* The fast search names a hint by its nogood's own step: id, 0 before
     * the step. */
    uint32_t id;
} Nogood;

typedef struct {
    int32_t lit;
    uint32_t level;
    int32_t reason; /* nogood index, or -1 for a decision */
} Entry;

typedef struct {
    int32_t nogood;
    int32_t w1;
    int32_t w2;
} Watched;

typedef struct {
    int32_t *lits;
    uint32_t len;
    uint32_t id;
    uint32_t next; /* the next older name in its hash chain, plus one */
} Name;

/* The fast search's watches, MiniSat's layout.  A clause is the propagation
 * copy of a watched nogood: its literals, the watched pair first, without
 * literals true at level 0; the nogood stays the reason.  A watch entry has
 * a blocker literal, and ref, the clause's index, or ~nogood for a clause
 * of two literals, whose entries the propagation settles alone. */
typedef struct {
    int32_t nogood;
    uint32_t len;
    int32_t *lits;
    uint32_t lbd;      /* a learned nogood's levels when learned, else 0 */
} FClause;

typedef struct {
    int32_t blocker;
    int32_t ref;
} FWatch;

typedef struct {
    FWatch *v;
    uint32_t len;
    uint32_t cap;
} FWList;

typedef struct {
    const AspRule *rules;
    uint32_t nrules;
    /* the SAT personality's clauses, which are then the inputs (else NULL) */
    const AspClause *clauses;
    uint32_t nclauses;
    /* atoms: input id <-> variable number (allAtoms order) */
    int32_t *atom_ids;
    uint32_t natoms;
    int32_t *atom_key;  /* open addressing: input id */
    int32_t *atom_var;  /* variable number */
    uint32_t atom_cap;
    uint32_t nvars;
    uint32_t *rule_body; /* variable of each rule's body, ASP_NO_BODY for a
                          * rule whose head is a falsum atom or whose body is
                          * a unit */
    int32_t *rule_unit;  /* a unit body's literal (one positive or negative
                          * atom, which stands for the body), else -1 */
    uint32_t *rule_head; /* variable of each rule's head */
    /* per atom: a falsum atom, one that heads rules and that every rule of
     * it negates in its body.  No stable model holds it, and no stable model
     * satisfies a body of its rules without that negated atom; so each of
     * its rules is a single nogood over its other literals, with no body
     * variable, and its support nogood is the unit T a. */
    uint8_t *falsum;
    uint32_t *occ_start; /* per atom: start of its positive occurrences */
    uint32_t *occ_rules; /* the rules where it occurs positively, once per occurrence */
    /* The fast search's source pointers over a program that is not tight
     * (native/support/support_v1.h): the rules as justifications, a rule
     * usable while its body is not false, false atoms excluded; the rules by
     * body variable; the trail position up to which false bodies have been
     * read. */
    SupportGraph *support;
    uint32_t *body_start;
    uint32_t *body_rules;
    uint32_t *unit_start;   /* per atom literal: the unit-body rules it falsifies */
    uint32_t *unit_rules;
    uint32_t sp_read;
    uint32_t *head_start; /* per atom: start of its rules, on first use */
    uint32_t *head_rules; /* the rules by head, each head's in program order */
    uint32_t *need;      /* scratch: unfounded positive occurrences per rule */
    uint32_t *queue;     /* scratch */
    /* nogoods: the completion first, in order, then made ones */
    Nogood *pool;
    uint32_t npool;
    uint32_t cappool;
    uint32_t ncompletion;
    /* watched database and watch lists, per literal */
    Watched *db;
    uint32_t ndb;
    uint32_t capdb;
    IVec *watches;       /* per literal, reversed: the list's front is last */
    /* trail, oldest first */
    Entry *trail;
    uint32_t ntrail;
    uint32_t level;
    int8_t *val;          /* per variable: -1, 0, 1 */
    uint32_t *trail_pos;  /* per assigned variable */
    IVec pending;         /* front last */
    IVec shorts;          /* nogood indices, in order */
    /* the certificate book */
    AspStep *steps;
    uint32_t nsteps;
    uint32_t capsteps;
    Name *names;          /* emitted names, oldest first */
    uint32_t nnames;
    uint32_t capnames;
    uint32_t next_id;
    /* names by the hash of their literal lists: emitted names newest first;
     * completion nogoods lowest identifier first (idOf's order) */
    uint32_t *name_bucket;
    uint32_t *comp_bucket;
    uint32_t *comp_next;
    uint32_t bucket_mask;
    bool oom;
    const AspTraceSink *trace;
    void *trace_context;
    /* No atom depends positively on itself through the rules: then every
     * atom an unfounded check finds is already false at a propagation
     * fixpoint, and the check is skipped. */
    bool tight;
    /* ASP_SEARCH_FAST (see asp_cdnl_v1.h) */
    bool fast;
    /* where the completion's atoms' support nogoods are, from support_first on */
    uint32_t support_first;
    /* The fast search's decisions: each variable's activity and saved phase,
     * a max-heap of the variables by activity (ties to the lower variable),
     * and the restart schedule. */
    double *activity;
    double bump;
    uint8_t *phase;
    uint32_t *heap;
    int32_t *heap_pos;     /* a variable's index in the heap, or -1 */
    uint32_t heap_len;
    uint64_t conflicts;
    uint64_t restart_at;
    uint32_t restarts;
    /* per variable true at level 0, the pool's unit nogood that holds its
     * value, once emitted (else -1): learned nogoods leave such literals out */
    int32_t *unit0;
    uint8_t *seen;         /* per variable, during one conflict's analysis */
    FClause *fc;
    uint32_t nfc;
    uint32_t capfc;
    FWList *fw;            /* per literal */
    uint32_t fc_level0;    /* level-0 trail length at the last simplify */
    uint32_t *level_stamp; /* per level, for counting a nogood's levels */
    uint32_t stamp;
    uint64_t reduce_at;    /* the conflict count of the next reduction */
    uint32_t reductions;
    /* ASP_SEARCH_FAST_HALL: Hall's condition over the at-least-one
     * constraints (groups, by their rules) and cliques of pairwise
     * exclusions (their atoms, CSR) */
    AspHall *hall;
    uint32_t *hall_group_rule;
    uint32_t *hall_clique_start;
    uint32_t *hall_clique_atoms;
    uint64_t hall_conflicts;
    uint64_t decisions;
    /* Weight definitions (asp_cdnl_v1.h).  Their literals, each true when it
     * contributes, by definition (CSR); per definition its atom's variable,
     * its total weight, and the weights of its true and its false literals
     * under the assignment; per variable the definition literals over it
     * (CSR) and the definition whose atom it is, else -1; the definitions
     * to look at again. */
    const AspWeightDef *defs;
    uint32_t ndefs;
    uint32_t *def_var;
    int32_t *var_def;
    uint32_t *dl_start;
    int32_t *dl_lit;
    uint64_t *dl_w;
    uint32_t *dl_def;
    uint64_t *d_total;
    uint64_t *d_true;
    uint64_t *d_false;
    uint32_t *vo_start;
    uint32_t *vo_lit;
    IVec wdirty;
    uint8_t *in_wdirty;
    /* The SMT personality's equality atoms (asp_cdnl_v1.h): the closure,
     * the input terms' closure terms, per variable its equality or -1;
     * how much of the trail the theory has read; for undoing, each read
     * atom's trail position and the closure's height before it, and the
     * atoms assumed false with their trail positions. */
    const AspSmtTerm *smt_terms;
    uint32_t smt_nterms;
    const AspSmtEquality *smt_eqs;
    uint32_t smt_neqs;
    EufCC *cc;
    uint32_t *cc_term;
    int32_t *var_eq;
    uint32_t euf_read;
    IVec euf_marks;
    IVec euf_false;
    /* The LRA theory, like EUF's: its bound atoms, each variable's atom (or
     * -1) and each atom's column; how much of the trail it has read; per
     * read atom its trail position and the bounds' height before it. */
    const AspSmtTheories *theories;
    LraSolver *lra;
    int32_t *var_lra;
    uint32_t *lra_col;
    int32_t lra_objective;   /* the objective's column, or -1 */
    /* per literal: the stamp of the last attach that met it */
    uint32_t *lit_stamp;
    uint32_t attach_stamp;
    /* constraints added after the program (multi-shot), as rule bodies */
    const AspRule *extra;
    uint32_t nextra;
    /* the next decision a caller chose (asp_session_decide): literal + 1 */
    int32_t forced_plus1;
    uint32_t lra_read;
    IVec lra_marks;
} Solver;

static void *grow(void *p, uint32_t *cap, uint32_t need, size_t size, bool *oom) {
    if (need <= *cap)
        return p;
    uint32_t c = *cap ? *cap : 8u;
    while (c < need)
        c *= 2u;
    void *q = realloc(p, size * c);
    if (!q) {
        *oom = true;
        return p;
    }
    *cap = c;
    return q;
}

/* ---- atoms ------------------------------------------------------------- */

static uint32_t hash32(int32_t x) {
    uint32_t h = (uint32_t)x * 2654435761u;
    return h ^ (h >> 16);
}

static int32_t atom_lookup(const Solver *s, int32_t id) {
    uint32_t mask = s->atom_cap - 1u;
    for (uint32_t i = hash32(id) & mask;; i = (i + 1u) & mask) {
        if (s->atom_var[i] < 0)
            return -1;
        if (s->atom_key[i] == id)
            return s->atom_var[i];
    }
}

static void atom_insert(Solver *s, int32_t id, int32_t var) {
    uint32_t mask = s->atom_cap - 1u;
    for (uint32_t i = hash32(id) & mask;; i = (i + 1u) & mask) {
        if (s->atom_var[i] < 0 || s->atom_key[i] == id) {
            s->atom_key[i] = id;
            s->atom_var[i] = var;
            return;
        }
    }
}

static uint32_t lits_hash(const int32_t *lits, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0u; i < n; i++)
        h = (h ^ (uint32_t)lits[i]) * 16777619u;
    return (h ^ n) * 2654435761u;
}

static bool same_list(const int32_t *a, uint32_t na, const int32_t *b, uint32_t nb) {
    return na == nb && (na == 0u || memcmp(a, b, sizeof(*a) * na) == 0);
}

static bool same_body(const AspRule *r, const AspRule *q) {
    return same_list(r->pos, r->pos_len, q->pos, q->pos_len) &&
           same_list(r->neg, r->neg_len, q->neg, q->neg_len);
}

/* DefProgram.atomList and DefProgram.varList (WeightCertificate.lean):
 * atoms in the order of their last occurrence in head :: pos ++ neg over the
 * rules, then over the definitions; then the distinct bodies of the rules
 * whose head is not a falsum atom and whose body is not a unit (one literal,
 * which stands for it), in the order of their last occurrence. */
static bool number_variables(Solver *s) {
    IVec seq = {0};
    for (uint32_t r = 0u; r < s->nrules; r++) {
        const AspRule *rule = &s->rules[r];
        bool ok = iv_push(&seq, rule->head);
        for (uint32_t i = 0u; ok && i < rule->pos_len; i++)
            ok = iv_push(&seq, rule->pos[i]);
        for (uint32_t i = 0u; ok && i < rule->neg_len; i++)
            ok = iv_push(&seq, rule->neg[i]);
        if (!ok) {
            iv_free(&seq);
            return false;
        }
    }
    for (uint32_t d = 0u; d < s->ndefs; d++) {
        const AspWeightDef *def = &s->defs[d];
        bool ok = iv_push(&seq, def->atom);
        for (uint32_t i = 0u; ok && i < def->pos_len; i++)
            ok = iv_push(&seq, def->pos[i]);
        for (uint32_t i = 0u; ok && i < def->neg_len; i++)
            ok = iv_push(&seq, def->neg[i]);
        if (!ok) {
            iv_free(&seq);
            return false;
        }
    }
    IVec atoms = {0};
    if (!iv_dedup(&atoms, seq.v, seq.len)) {
        iv_free(&seq);
        return false;
    }
    iv_free(&seq);
    s->atom_ids = atoms.v;
    s->natoms = atoms.len;
    uint32_t cap = 16u;
    while (cap < 2u * (s->natoms + 1u))
        cap *= 2u;
    s->atom_cap = cap;
    s->atom_key = malloc(sizeof(*s->atom_key) * cap);
    s->atom_var = malloc(sizeof(*s->atom_var) * cap);
    s->rule_body = malloc(sizeof(*s->rule_body) * (s->nrules ? s->nrules : 1u));
    if (!s->atom_key || !s->atom_var || !s->rule_body)
        return false;
    for (uint32_t i = 0u; i < cap; i++)
        s->atom_var[i] = -1;
    for (uint32_t i = 0u; i < s->natoms; i++)
        atom_insert(s, s->atom_ids[i], (int32_t)i);
    /* the falsum atoms: those heading rules, every rule of which negates its
     * head (DefProgram.falsum) */
    s->falsum = calloc(s->natoms ? s->natoms : 1u, 1u);
    uint8_t *negated = malloc(s->natoms ? s->natoms : 1u);
    if (!s->falsum || !negated) {
        free(negated);
        return false;
    }
    memset(negated, 1, s->natoms ? s->natoms : 1u);
    for (uint32_t r = 0u; r < s->nrules; r++) {
        const AspRule *rule = &s->rules[r];
        uint32_t h = (uint32_t)atom_lookup(s, rule->head);
        bool self = false;
        for (uint32_t k = 0u; k < rule->neg_len && !self; k++)
            self = rule->neg[k] == rule->head;
        s->falsum[h] = 1u;
        if (!self)
            negated[h] = 0u;
    }
    for (uint32_t a = 0u; a < s->natoms; a++)
        s->falsum[a] = s->falsum[a] && negated[a];
    free(negated);
    /* unit bodies: the literal of the one atom, a falsum atom's rules aside */
    s->rule_unit = malloc(sizeof(*s->rule_unit) * (s->nrules ? s->nrules : 1u));
    if (!s->rule_unit)
        return false;
    for (uint32_t r = 0u; r < s->nrules; r++) {
        const AspRule *rule = &s->rules[r];
        s->rule_unit[r] = -1;
        if (s->falsum[atom_lookup(s, rule->head)] || rule->pos_len + rule->neg_len != 1u)
            continue;
        s->rule_unit[r] = rule->pos_len
            ? LIT(atom_lookup(s, rule->pos[0]), 1) : LIT(atom_lookup(s, rule->neg[0]), 0);
    }
    /* bodies: a rule's body is numbered where its last equal body is.
     * Scanning from the end, the first rule met with a body is its last
     * occurrence, which represents it. */
    uint32_t size = 64u;
    while (size < 2u * s->nrules)
        size *= 2u;
    uint32_t *bucket = calloc(size, sizeof(*bucket));
    uint32_t *chain = malloc(sizeof(*chain) * (s->nrules ? s->nrules : 1u));
    uint32_t *rep = malloc(sizeof(*rep) * (s->nrules ? s->nrules : 1u));
    if (!bucket || !chain || !rep) {
        free(bucket);
        free(chain);
        free(rep);
        return false;
    }
    for (uint32_t r = s->nrules; r-- > 0u;) {
        const AspRule *rule = &s->rules[r];
        rep[r] = r;
        if (s->falsum[atom_lookup(s, rule->head)] || s->rule_unit[r] >= 0)
            continue;
        uint32_t h = (lits_hash(rule->pos, rule->pos_len) * 31u) ^
                     lits_hash(rule->neg, rule->neg_len);
        uint32_t b = h & (size - 1u);
        for (uint32_t q = bucket[b]; q; q = chain[q - 1u])
            if (same_body(rule, &s->rules[q - 1u])) {
                rep[r] = q - 1u;
                break;
            }
        if (rep[r] == r) {
            chain[r] = bucket[b];
            bucket[b] = r + 1u;
        }
    }
    uint32_t next = s->natoms;
    for (uint32_t r = 0u; r < s->nrules; r++)
        if (rep[r] == r && !s->falsum[atom_lookup(s, s->rules[r].head)] &&
            s->rule_unit[r] < 0)
            s->rule_body[r] = next++;
    for (uint32_t r = 0u; r < s->nrules; r++)
        s->rule_body[r] = s->falsum[atom_lookup(s, s->rules[r].head)] || s->rule_unit[r] >= 0
            ? ASP_NO_BODY : s->rule_body[rep[r]];
    free(bucket);
    free(chain);
    free(rep);
    s->nvars = next;
    s->rule_head = malloc(sizeof(*s->rule_head) * (s->nrules ? s->nrules : 1u));
    s->occ_start = calloc(s->natoms + 1u, sizeof(*s->occ_start));
    s->need = malloc(sizeof(*s->need) * (s->nrules ? s->nrules : 1u));
    s->queue = malloc(sizeof(*s->queue) * (s->nrules ? s->nrules : 1u));
    if (!s->rule_head || !s->occ_start || !s->need || !s->queue)
        return false;
    uint32_t occurrences = 0u;
    for (uint32_t r = 0u; r < s->nrules; r++) {
        s->rule_head[r] = (uint32_t)atom_lookup(s, s->rules[r].head);
        for (uint32_t i = 0u; i < s->rules[r].pos_len; i++)
            s->occ_start[atom_lookup(s, s->rules[r].pos[i]) + 1]++;
        occurrences += s->rules[r].pos_len;
    }
    for (uint32_t a = 0u; a < s->natoms; a++)
        s->occ_start[a + 1u] += s->occ_start[a];
    s->occ_rules = malloc(sizeof(*s->occ_rules) * (occurrences ? occurrences : 1u));
    uint32_t *fill = malloc(sizeof(*fill) * (s->natoms ? s->natoms : 1u));
    if (!s->occ_rules || !fill) {
        free(fill);
        return false;
    }
    memcpy(fill, s->occ_start, sizeof(*fill) * s->natoms);
    for (uint32_t r = 0u; r < s->nrules; r++)
        for (uint32_t i = 0u; i < s->rules[r].pos_len; i++)
            s->occ_rules[fill[atom_lookup(s, s->rules[r].pos[i])]++] = r;
    free(fill);
    return true;
}

static int32_t t_atom_var(const Solver *s, int32_t id, int sign) {
    return LIT(atom_lookup(s, id), sign);
}

/* The definitions over the numbering: their literals by definition and by
 * variable, their total weights; every definition to look at.  Refused when
 * a definition's atom heads a rule, is another's, or lies on a cycle of
 * positive dependencies through its own body. */
static bool weights_acyclic(const Solver *s);

static bool weights_setup(Solver *s) {
    s->var_def = malloc(sizeof(*s->var_def) * (s->nvars ? s->nvars : 1u));
    s->def_var = malloc(sizeof(*s->def_var) * (s->ndefs ? s->ndefs : 1u));
    s->dl_start = calloc((size_t)s->ndefs + 1u, sizeof(*s->dl_start));
    s->d_total = calloc(s->ndefs ? s->ndefs : 1u, sizeof(*s->d_total));
    s->d_true = calloc(s->ndefs ? s->ndefs : 1u, sizeof(*s->d_true));
    s->d_false = calloc(s->ndefs ? s->ndefs : 1u, sizeof(*s->d_false));
    s->in_wdirty = calloc(s->ndefs ? s->ndefs : 1u, 1u);
    s->vo_start = calloc((size_t)s->nvars + 1u, sizeof(*s->vo_start));
    if (!s->var_def || !s->def_var || !s->dl_start || !s->d_total ||
        !s->d_true || !s->d_false || !s->in_wdirty || !s->vo_start)
        return false;
    for (uint32_t v = 0u; v < s->nvars; v++)
        s->var_def[v] = -1;
    uint32_t n = 0u;
    for (uint32_t d = 0u; d < s->ndefs; d++) {
        const AspWeightDef *def = &s->defs[d];
        uint32_t v = (uint32_t)atom_lookup(s, def->atom);
        if (s->var_def[v] >= 0)
            return false;
        s->var_def[v] = (int32_t)d;
        s->def_var[d] = v;
        n += def->pos_len + def->neg_len;
        s->dl_start[d + 1u] = n;
    }
    for (uint32_t r = 0u; r < s->nrules; r++)
        if (s->var_def[s->rule_head[r]] >= 0)
            return false;
    s->dl_lit = malloc(sizeof(*s->dl_lit) * (n ? n : 1u));
    s->dl_w = malloc(sizeof(*s->dl_w) * (n ? n : 1u));
    s->dl_def = malloc(sizeof(*s->dl_def) * (n ? n : 1u));
    s->vo_lit = malloc(sizeof(*s->vo_lit) * (n ? n : 1u));
    if (!s->dl_lit || !s->dl_w || !s->dl_def || !s->vo_lit)
        return false;
    for (uint32_t d = 0u; d < s->ndefs; d++) {
        const AspWeightDef *def = &s->defs[d];
        uint32_t k = s->dl_start[d];
        for (uint32_t i = 0u; i < def->pos_len; i++, k++) {
            s->dl_lit[k] = t_atom_var(s, def->pos[i], 1);
            s->dl_w[k] = def->pos_w[i];
            s->dl_def[k] = d;
        }
        for (uint32_t i = 0u; i < def->neg_len; i++, k++) {
            s->dl_lit[k] = t_atom_var(s, def->neg[i], 0);
            s->dl_w[k] = def->neg_w[i];
            s->dl_def[k] = d;
        }
        for (uint32_t j = s->dl_start[d]; j < k; j++) {
            if (s->dl_w[j] == 0u || s->d_total[d] > UINT64_MAX - s->dl_w[j])
                return false;
            s->d_total[d] += s->dl_w[j];
            s->vo_start[LIT_VAR(s->dl_lit[j]) + 1u]++;
        }
    }
    for (uint32_t v = 0u; v < s->nvars; v++)
        s->vo_start[v + 1u] += s->vo_start[v];
    uint32_t *fill = malloc(sizeof(*fill) * ((size_t)s->nvars + 1u));
    if (!fill)
        return false;
    memcpy(fill, s->vo_start, sizeof(*fill) * ((size_t)s->nvars + 1u));
    for (uint32_t k = 0u; k < n; k++)
        s->vo_lit[fill[LIT_VAR(s->dl_lit[k])]++] = k;
    free(fill);
    if (!weights_acyclic(s))
        return false;
    for (uint32_t d = 0u; d < s->ndefs; d++) {
        s->in_wdirty[d] = 1u;
        if (!iv_push(&s->wdirty, (int32_t)d))
            return false;
    }
    return true;
}

/* Whether no definition's atom lies on a cycle of positive dependencies,
 * a rule's head depending on its positive body atoms and a definition's
 * atom on its positive literals: the components of that graph (Tarjan's
 * algorithm, iterative) put each definition's atom alone, without an edge to
 * itself. */
static bool weights_acyclic(const Solver *s) {
    uint32_t n = s->natoms;
    uint32_t *start = calloc((size_t)n + 1u, sizeof(*start));
    size_t edges = 0u;
    for (uint32_t r = 0u; start && r < s->nrules; r++) {
        start[s->rule_head[r] + 1u] += s->rules[r].pos_len;
        edges += s->rules[r].pos_len;
    }
    for (uint32_t d = 0u; start && d < s->ndefs; d++)
        for (uint32_t k = s->dl_start[d]; k < s->dl_start[d + 1u]; k++)
            if (LIT_SIGN(s->dl_lit[k])) {
                start[s->def_var[d] + 1u]++;
                edges++;
            }
    uint32_t *adj = malloc(sizeof(*adj) * (edges ? edges : 1u));
    uint32_t *fill = malloc(sizeof(*fill) * ((size_t)n + 1u));
    uint32_t *index = malloc(sizeof(*index) * ((size_t)n + 1u));
    uint32_t *low = malloc(sizeof(*low) * ((size_t)n + 1u));
    uint32_t *comp = malloc(sizeof(*comp) * ((size_t)n + 1u));
    uint32_t *size = calloc((size_t)n + 1u, sizeof(*size));
    uint32_t *stack = malloc(sizeof(*stack) * ((size_t)n + 1u));
    uint32_t *calls = malloc(sizeof(*calls) * ((size_t)n + 1u));
    uint32_t *next = malloc(sizeof(*next) * ((size_t)n + 1u));
    uint8_t *on = calloc((size_t)n + 1u, 1u);
    bool ok = start && adj && fill && index && low && comp && size && stack &&
              calls && next && on;
    if (ok) {
        for (uint32_t a = 0u; a < n; a++)
            start[a + 1u] += start[a];
        memcpy(fill, start, sizeof(*fill) * n);
        for (uint32_t r = 0u; r < s->nrules; r++)
            for (uint32_t k = 0u; k < s->rules[r].pos_len; k++)
                adj[fill[s->rule_head[r]]++] =
                    (uint32_t)atom_lookup(s, s->rules[r].pos[k]);
        for (uint32_t d = 0u; d < s->ndefs; d++)
            for (uint32_t k = s->dl_start[d]; k < s->dl_start[d + 1u]; k++)
                if (LIT_SIGN(s->dl_lit[k]))
                    adj[fill[s->def_var[d]]++] = LIT_VAR(s->dl_lit[k]);
        uint32_t counter = 0u, top = 0u, ncomp = 0u;
        for (uint32_t a = 0u; a < n; a++)
            index[a] = UINT32_MAX;
        for (uint32_t root = 0u; root < n; root++) {
            if (index[root] != UINT32_MAX)
                continue;
            uint32_t depth = 0u;
            calls[depth++] = root;
            index[root] = low[root] = counter++;
            next[root] = start[root];
            stack[top++] = root;
            on[root] = 1u;
            while (depth) {
                uint32_t v = calls[depth - 1u];
                if (next[v] < start[v + 1u]) {
                    uint32_t w = adj[next[v]++];
                    if (index[w] == UINT32_MAX) {
                        index[w] = low[w] = counter++;
                        next[w] = start[w];
                        stack[top++] = w;
                        on[w] = 1u;
                        calls[depth++] = w;
                    } else if (on[w] && index[w] < low[v]) {
                        low[v] = index[w];
                    }
                    continue;
                }
                depth--;
                if (depth && low[v] < low[calls[depth - 1u]])
                    low[calls[depth - 1u]] = low[v];
                if (low[v] == index[v]) {
                    uint32_t w;
                    do {
                        w = stack[--top];
                        on[w] = 0u;
                        comp[w] = ncomp;
                        size[ncomp]++;
                    } while (w != v);
                    ncomp++;
                }
            }
        }
        for (uint32_t d = 0u; ok && d < s->ndefs; d++) {
            uint32_t w = s->def_var[d];
            ok = size[comp[w]] == 1u;
            for (uint32_t k = start[w]; ok && k < start[w + 1u]; k++)
                ok = adj[k] != w;
        }
    }
    free(start);
    free(adj);
    free(fill);
    free(index);
    free(low);
    free(comp);
    free(size);
    free(stack);
    free(calls);
    free(next);
    free(on);
    return ok;
}

static int32_t t_atom(const Solver *s, int32_t id) { return LIT(atom_lookup(s, id), 1); }
static int32_t f_atom(const Solver *s, int32_t id) { return LIT(atom_lookup(s, id), 0); }
static bool falsum_rule(const Solver *s, uint32_t r) { return s->falsum[s->rule_head[r]] != 0u; }
/* T body r and F body r: for a unit body, its literal and that literal's
 * complement */
static int32_t t_body(const Solver *s, uint32_t r) {
    return s->rule_unit[r] >= 0 ? s->rule_unit[r] : LIT(s->rule_body[r], 1);
}
static int32_t f_body(const Solver *s, uint32_t r) {
    return s->rule_unit[r] >= 0 ? LIT_COMPL(s->rule_unit[r]) : LIT(s->rule_body[r], 0);
}

static int32_t new_nogood(Solver *s, const int32_t *lits, uint32_t n) {
    s->pool = grow(s->pool, &s->cappool, s->npool + 1u, sizeof(*s->pool), &s->oom);
    if (s->oom)
        return -1;
    Nogood *g = &s->pool[s->npool];
    memset(g, 0, sizeof(*g));
    for (uint32_t i = 0u; i < n; i++)
        if (!iv_push(&g->lits, lits[i])) {
            s->oom = true;
            return -1;
        }
    return (int32_t)s->npool++;
}

/* Whether no atom depends positively on itself: a depth-first search over
 * the positive dependency graph (head -> each positive body atom) meets no
 * atom still on its stack. */
static bool program_tight(Solver *s) {
    uint32_t n = s->natoms;
    uint8_t *state = calloc(n ? n : 1u, 1u);       /* 0 new, 1 on stack, 2 done */
    uint32_t *stack = malloc(sizeof(*stack) * (n ? n : 1u));
    uint32_t *edge = malloc(sizeof(*edge) * (n ? n : 1u));
    uint32_t *head_start = calloc(n + 1u, sizeof(*head_start));
    uint32_t *head_rules = malloc(sizeof(*head_rules) * (s->nrules ? s->nrules : 1u));
    bool tight = state && stack && edge && head_start && head_rules;
    if (tight) {
        /* the rules of each head, in order */
        for (uint32_t r = 0u; r < s->nrules; r++)
            head_start[s->rule_head[r] + 1u]++;
        for (uint32_t a = 0u; a < n; a++)
            head_start[a + 1u] += head_start[a];
        uint32_t *fill = calloc(n ? n : 1u, sizeof(*fill));
        if (!fill) {
            tight = false;
        } else {
            for (uint32_t r = 0u; r < s->nrules; r++) {
                uint32_t h = s->rule_head[r];
                head_rules[head_start[h] + fill[h]++] = r;
            }
            free(fill);
        }
    }
    /* edge[a] walks the positive body atoms of a's rules, in order: rule
     * head_rules[...] and position, packed as rule index then position */
    uint32_t *pos_of = tight ? calloc(n ? n : 1u, sizeof(*pos_of)) : NULL;
    if (tight && !pos_of)
        tight = false;
    for (uint32_t root = 0u; tight && root < n; root++) {
        if (state[root])
            continue;
        uint32_t depth = 0u;
        stack[depth++] = root;
        state[root] = 1u;
        edge[root] = head_start[root];
        pos_of[root] = 0u;
        while (depth && tight) {
            uint32_t a = stack[depth - 1u];
            if (edge[a] == head_start[a + 1u]) {
                state[a] = 2u;
                depth--;
                continue;
            }
            const AspRule *rule = &s->rules[head_rules[edge[a]]];
            if (pos_of[a] >= rule->pos_len) {
                edge[a]++;
                pos_of[a] = 0u;
                continue;
            }
            uint32_t b = (uint32_t)atom_lookup(s, rule->pos[pos_of[a]++]);
            if (state[b] == 1u) {
                tight = false;
            } else if (state[b] == 0u) {
                state[b] = 1u;
                edge[b] = head_start[b];
                pos_of[b] = 0u;
                stack[depth++] = b;
            }
        }
    }
    free(state);
    free(stack);
    free(edge);
    free(head_start);
    free(head_rules);
    free(pos_of);
    return tight;
}

/* DefProgram.completionNogoods (WeightCertificate.lean): per rule, its head
 * nogood and its body's nogoods (a unit body, whose literal stands for it,
 * has none), or for a rule of a falsum atom the one nogood of its body
 * without the negated head; then each atom's support nogood, a falsum
 * atom's the unit T a. */
static bool completion(Solver *s) {
    IVec l = {0};
    for (uint32_t r = 0u; r < s->nrules && !s->oom; r++) {
        const AspRule *rule = &s->rules[r];
        if (falsum_rule(s, r)) {
            /* T pos, F neg but the head: the body without its negated head */
            l.len = 0u;
            for (uint32_t i = 0u; i < rule->pos_len; i++)
                iv_push(&l, t_atom(s, rule->pos[i]));
            for (uint32_t i = 0u; i < rule->neg_len; i++)
                if (rule->neg[i] != rule->head)
                    iv_push(&l, f_atom(s, rule->neg[i]));
            new_nogood(s, l.v, l.len);
            continue;
        }
        int32_t head[2] = {t_body(s, r), f_atom(s, rule->head)};
        new_nogood(s, head, 2u);
        if (s->rule_unit[r] >= 0)   /* the literal is the body: no nogoods of its own */
            continue;
        l.len = 0u;
        iv_push(&l, f_body(s, r));
        for (uint32_t i = 0u; i < rule->pos_len; i++)
            iv_push(&l, t_atom(s, rule->pos[i]));
        for (uint32_t i = 0u; i < rule->neg_len; i++)
            iv_push(&l, f_atom(s, rule->neg[i]));
        new_nogood(s, l.v, l.len);
        for (uint32_t i = 0u; i < rule->pos_len; i++) {
            int32_t g[2] = {t_body(s, r), f_atom(s, rule->pos[i])};
            new_nogood(s, g, 2u);
        }
        for (uint32_t i = 0u; i < rule->neg_len; i++) {
            int32_t g[2] = {t_body(s, r), t_atom(s, rule->neg[i])};
            new_nogood(s, g, 2u);
        }
    }
    /* Each atom's rules, in program order: the heads' counts, then a
     * stable bucket fill. */
    uint32_t *start = calloc((size_t)s->natoms + 1u, sizeof(*start));
    uint32_t *order = malloc(sizeof(*order) * (s->nrules ? s->nrules : 1u));
    if (!start || !order)
        s->oom = true;
    for (uint32_t r = 0u; !s->oom && r < s->nrules; r++)
        start[s->rule_head[r] + 1u]++;
    for (uint32_t a = 0u; !s->oom && a < s->natoms; a++)
        start[a + 1u] += start[a];
    for (uint32_t r = 0u; !s->oom && r < s->nrules; r++)
        order[start[s->rule_head[r]]++] = r;
    for (uint32_t a = s->natoms; !s->oom && a > 0u; a--)
        start[a] = start[a - 1u];
    if (!s->oom)
        start[0] = 0u;
    s->support_first = s->npool;
    for (uint32_t a = 0u; a < s->natoms && !s->oom; a++) {
        l.len = 0u;
        iv_push(&l, LIT(a, 1));
        if (s->falsum[a]) {          /* no stable model holds it */
            new_nogood(s, l.v, l.len);
            continue;
        }
        /* a definition's atom is supported by its definition, which the
         * solver propagates itself: its place holds a nogood no assignment
         * makes true */
        if (s->var_def && s->var_def[a] >= 0)
            iv_push(&l, LIT(a, 0));
        for (uint32_t k = start[a]; k < start[a + 1u]; k++)
            iv_push(&l, f_body(s, order[k]));
        new_nogood(s, l.v, l.len);
    }
    free(start);
    free(order);
    iv_free(&l);
    s->ncompletion = s->npool;
    for (uint32_t g = 0u; g < s->ncompletion; g++)
        s->pool[g].id = g + 1u;
    return !s->oom;
}

/* ---- trail -------------------------------------------------------------- */

static bool on_trail(const Solver *s, int32_t lit) {
    return s->val[LIT_VAR(lit)] == (int8_t)LIT_SIGN(lit);
}

static int32_t native_lit(int32_t lit);

/* The definitions over variable v as it is assigned (or unassigned): their
 * true and false weights, and each to be looked at again. */
static void weights_assign(Solver *s, uint32_t v, bool assigned) {
    for (uint32_t k = s->vo_start[v]; k < s->vo_start[v + 1u]; k++) {
        uint32_t j = s->vo_lit[k];
        uint32_t d = s->dl_def[j];
        bool contributes = on_trail(s, s->dl_lit[j]);
        uint64_t *sum = contributes ? &s->d_true[d] : &s->d_false[d];
        *sum = assigned ? *sum + s->dl_w[j] : *sum - s->dl_w[j];
        if (assigned && !s->in_wdirty[d]) {
            s->in_wdirty[d] = 1u;
            if (!iv_push(&s->wdirty, (int32_t)d))
                s->oom = true;
        }
    }
    if (assigned && v < s->natoms && s->var_def[v] >= 0) {
        uint32_t d = (uint32_t)s->var_def[v];
        if (!s->in_wdirty[d]) {
            s->in_wdirty[d] = 1u;
            if (!iv_push(&s->wdirty, (int32_t)d))
                s->oom = true;
        }
    }
}

static void push_entry(Solver *s, int32_t lit, uint32_t level, int32_t reason) {
    if (s->trace)
        s->trace->event(s->trace_context,
                        reason < 0 ? ASP_TRACE_DECIDE : ASP_TRACE_IMPLY,
                        native_lit(lit), level, reason, NULL, 0u);
    s->trail[s->ntrail] = (Entry){lit, level, reason};
    s->val[LIT_VAR(lit)] = (int8_t)LIT_SIGN(lit);
    if (s->ndefs)
        weights_assign(s, LIT_VAR(lit), true);
    if (s->hall && !LIT_SIGN(lit) && LIT_VAR(lit) < s->natoms)
        asp_hall_exclude(s->hall, LIT_VAR(lit));
    s->trail_pos[LIT_VAR(lit)] = s->ntrail;
    s->ntrail++;
    if (reason < 0)
        s->level = level;
}

/* WState.pushLit */
static void push_lit(Solver *s, int32_t lit, int32_t reason) {
    push_entry(s, lit, s->level, reason);
    if (!iv_push(&s->pending, lit))
        s->oom = true;
}

/* ---- the fast search's decisions --------------------------------------- */

static bool heap_before(const Solver *s, uint32_t a, uint32_t b) {
    return s->activity[a] > s->activity[b] ||
        (s->activity[a] == s->activity[b] && a < b);
}

static void heap_place(Solver *s, uint32_t i, uint32_t v) {
    s->heap[i] = v;
    s->heap_pos[v] = (int32_t)i;
}

static void heap_up(Solver *s, uint32_t i) {
    uint32_t v = s->heap[i];
    while (i > 0u) {
        uint32_t parent = (i - 1u) / 2u;
        if (!heap_before(s, v, s->heap[parent]))
            break;
        heap_place(s, i, s->heap[parent]);
        i = parent;
    }
    heap_place(s, i, v);
}

static void heap_down(Solver *s, uint32_t i) {
    uint32_t v = s->heap[i];
    for (;;) {
        uint32_t child = 2u * i + 1u;
        if (child >= s->heap_len)
            break;
        if (child + 1u < s->heap_len && heap_before(s, s->heap[child + 1u], s->heap[child]))
            child++;
        if (!heap_before(s, s->heap[child], v))
            break;
        heap_place(s, i, s->heap[child]);
        i = child;
    }
    heap_place(s, i, v);
}

static void heap_insert(Solver *s, uint32_t v) {
    if (s->heap_pos[v] >= 0)
        return;
    heap_place(s, s->heap_len++, v);
    heap_up(s, s->heap_len - 1u);
}

static uint32_t heap_pop(Solver *s) {
    uint32_t v = s->heap[0];
    s->heap_pos[v] = -1;
    if (--s->heap_len > 0u) {
        heap_place(s, 0u, s->heap[s->heap_len]);
        heap_down(s, 0u);
    }
    return v;
}

/* VSIDS: a bumped variable gains the current increment, which grows after
 * each conflict so that older bumps weigh less; all activities shrink
 * together before they overflow. */
static void bump_variable(Solver *s, uint32_t v) {
    s->activity[v] += s->bump;
    if (s->activity[v] > 1e100) {
        for (uint32_t k = 0u; k < s->nvars; k++)
            s->activity[k] *= 1e-100;
        s->bump *= 1e-100;
    }
    if (s->heap_pos[v] >= 0)
        heap_up(s, (uint32_t)s->heap_pos[v]);
}

/* Conflicts per unit of the Luby sequence (MiniSat's 100).  Learned nogoods
 * are not deleted yet, so no unit wins everywhere: 4096 was faster on
 * n-queens 20 and 24 and on the pigeonhole problems, and far slower on
 * n-queens 40 (104 s against 18 s). */
#define FAST_RESTART_UNIT 100u

/* the Luby sequence: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8 ... */
static uint64_t luby(uint32_t i) {
    uint64_t size = 1u;
    uint32_t seq = 0u;
    while (size < (uint64_t)i + 1u) {
        seq++;
        size = 2u * size + 1u;
    }
    uint64_t x = i;
    while (size - 1u != x) {
        size = (size - 1u) / 2u;
        seq--;
        x %= size;
    }
    return (uint64_t)1u << seq;
}

static void truncate_trail(Solver *s, uint32_t n) {
    if (s->sp_read > n)      /* the false bodies read are those still there */
        s->sp_read = n;
    if (s->cc && s->euf_read > n) {
        /* the closure back to before the first atom read at or after n */
        uint32_t height = UINT32_MAX;
        while (s->euf_marks.len &&
               (uint32_t)s->euf_marks.v[s->euf_marks.len - 2u] >= n) {
            height = (uint32_t)s->euf_marks.v[s->euf_marks.len - 1u];
            s->euf_marks.len -= 2u;
        }
        if (height != UINT32_MAX)
            euf_undo(s->cc, height);
        while (s->euf_false.len &&
               (uint32_t)s->euf_false.v[s->euf_false.len - 1u] >= n)
            s->euf_false.len -= 2u;
        s->euf_read = n;
    }
    if (s->lra && s->lra_read > n) {
        uint32_t height = UINT32_MAX;
        while (s->lra_marks.len &&
               (uint32_t)s->lra_marks.v[s->lra_marks.len - 2u] >= n) {
            height = (uint32_t)s->lra_marks.v[s->lra_marks.len - 1u];
            s->lra_marks.len -= 2u;
        }
        if (height != UINT32_MAX)
            lra_undo(s->lra, height);
        s->lra_read = n;
    }
    while (s->ntrail > n) {
        s->ntrail--;
        int32_t lit = s->trail[s->ntrail].lit;
        if (s->ndefs)
            weights_assign(s, LIT_VAR(lit), false);
        s->val[LIT_VAR(lit)] = -1;
        if (s->hall && !LIT_SIGN(lit) && LIT_VAR(lit) < s->natoms)
            asp_hall_include(s->hall, LIT_VAR(lit));
        if (s->fast && (uint32_t)LIT_VAR(lit) < s->natoms) {
            s->phase[LIT_VAR(lit)] = (uint8_t)LIT_SIGN(lit);
            heap_insert(s, (uint32_t)LIT_VAR(lit));
            if (s->support && !support_reopen(s->support, (uint32_t)LIT_VAR(lit)))
                s->oom = true;
        }
    }
    /* levels never fall along the trail, each entry's being its newest
     * decision's, so the newest entry's level is the newest decision's */
    s->level = s->ntrail ? s->trail[s->ntrail - 1u].level : 0u;
}

/* the index of the newest decision, or ntrail if none */
static uint32_t newest_decision(const Solver *s) {
    for (uint32_t i = s->ntrail; i-- > 0u;)
        if (s->trail[i].reason < 0)
            return i;
    return s->ntrail;
}

/* ---- the certificate book (Emission.lean) ------------------------------ */

static int32_t native_lit(int32_t lit) {
    int32_t n = (int32_t)LIT_VAR(lit) + 1;
    return LIT_SIGN(lit) ? n : -n;
}

/* Program.lratClause: the dedup'd literals' complements, numbered */
static bool lrat_clause(const int32_t *lits, uint32_t n, int32_t **out, uint32_t *out_len) {
    IVec d = {0};
    if (!iv_dedup(&d, lits, n))
        return false;
    for (uint32_t i = 0u; i < d.len; i++)
        d.v[i] = native_lit(LIT_COMPL(d.v[i]));
    *out = d.v;
    *out_len = d.len;
    return true;
}


/* the name tables: completion nogoods chained lowest identifier first */
static bool name_tables(Solver *s) {
    uint32_t size = 1024u;
    while (size < 2u * s->ncompletion)
        size *= 2u;
    s->bucket_mask = size - 1u;
    s->name_bucket = calloc(size, sizeof(*s->name_bucket));
    s->comp_bucket = calloc(size, sizeof(*s->comp_bucket));
    s->comp_next = malloc(sizeof(*s->comp_next) * (s->ncompletion ? s->ncompletion : 1u));
    if (!s->name_bucket || !s->comp_bucket || !s->comp_next)
        return false;
    for (uint32_t k = s->ncompletion; k-- > 0u;) {
        uint32_t b = lits_hash(s->pool[k].lits.v, s->pool[k].lits.len) & s->bucket_mask;
        s->comp_next[k] = s->comp_bucket[b];
        s->comp_bucket[b] = k + 1u;
    }
    return true;
}

/* idOf: the newest emitted name with these literals, else the first
 * completion nogood with them (nameFrom 1). */
static uint32_t id_of(const Solver *s, const int32_t *lits, uint32_t n) {
    uint32_t b = lits_hash(lits, n) & s->bucket_mask;
    for (uint32_t i = s->name_bucket[b]; i; i = s->names[i - 1u].next)
        if (same_list(s->names[i - 1u].lits, s->names[i - 1u].len, lits, n))
            return s->names[i - 1u].id;
    for (uint32_t k = s->comp_bucket[b]; k; k = s->comp_next[k - 1u])
        if (same_list(s->pool[k - 1u].lits.v, s->pool[k - 1u].lits.len, lits, n))
            return k;
    return 0u;
}

static AspStep *new_step(Solver *s) {
    s->steps = grow(s->steps, &s->capsteps, s->nsteps + 1u, sizeof(*s->steps), &s->oom);
    if (s->oom)
        return NULL;
    AspStep *st = &s->steps[s->nsteps++];
    memset(st, 0, sizeof(*st));
    return st;
}

static void add_name(Solver *s, const int32_t *lits, uint32_t n, uint32_t id) {
    s->names = grow(s->names, &s->capnames, s->nnames + 1u, sizeof(*s->names), &s->oom);
    if (s->oom)
        return;
    Name *nm = &s->names[s->nnames];
    nm->lits = malloc(sizeof(*lits) * (n ? n : 1u));
    if (!nm->lits) {
        s->oom = true;
        return;
    }
    if (n)
        memcpy(nm->lits, lits, sizeof(*lits) * n);
    nm->len = n;
    nm->id = id;
    uint32_t b = lits_hash(lits, n) & s->bucket_mask;
    nm->next = s->name_bucket[b];
    s->name_bucket[b] = s->nnames + 1u;
    s->nnames++;
}

/* The newest step's name: by its literals for the reference search's idOf,
 * on its nogood (named, when there is one) for the fast search. */
static void name_step(Solver *s, const int32_t *lits, uint32_t n, int32_t named) {
    if (!s->fast)
        add_name(s, lits, n, s->next_id);
    else if (named >= 0)
        s->pool[named].id = s->next_id;
    s->next_id++;
}

static void emit_rup(Solver *s, const int32_t *lits, uint32_t n,
                     const IVec *hints, int32_t named);

/* A hint's name: idOf for the reference search; for the fast search the
 * nogood's step. */
static uint32_t hint_name(Solver *s, int32_t g) {
    if (!s->fast)
        return id_of(s, s->pool[g].lits.v, s->pool[g].lits.len);
    return s->pool[g].id;
}

/* whether nogood g has a step of its own (not only an input's name) */
static bool derived_step(Solver *s, int32_t g) {
    uint32_t id = s->fast ? s->pool[g].id
                          : id_of(s, s->pool[g].lits.v, s->pool[g].lits.len);
    return id > s->ncompletion;
}

/* Program.emitRup; hints are nogood indices; named is the nogood the step
 * derives, or -1 */
static void emit_rup(Solver *s, const int32_t *lits, uint32_t n,
                     const IVec *hints, int32_t named) {
    uint32_t *ids = malloc(sizeof(*ids) * (hints->len ? hints->len : 1u));
    if (!ids) {
        s->oom = true;
        return;
    }
    for (uint32_t i = 0u; i < hints->len; i++)
        ids[i] = hint_name(s, hints->v[i]);
    AspStep *st = new_step(s);
    if (!st) {
        free(ids);
        return;
    }
    st->kind = ASP_STEP_RUP;
    st->id = s->next_id;
    st->hints = ids;
    st->hints_len = hints->len;
    if (!lrat_clause(lits, n, &st->clause, &st->clause_len)) {
        s->oom = true;
        return;
    }
    name_step(s, lits, n, named);
}

/* Program.emitLoop */
static void emit_loop(Solver *s, const IVec *marked, int32_t nogood, uint32_t a) {
    AspStep *st = new_step(s);
    if (!st)
        return;
    const Nogood *g = &s->pool[nogood];
    st->kind = ASP_STEP_LOOP;
    st->id = s->next_id;
    if (!lrat_clause(g->lits.v, g->lits.len, &st->clause, &st->clause_len)) {
        s->oom = true;
        return;
    }
    st->atoms = malloc(sizeof(*st->atoms) * (marked->len ? marked->len : 1u));
    if (!st->atoms) {
        s->oom = true;
        return;
    }
    for (uint32_t i = 0u; i < marked->len; i++)
        st->atoms[i] = (uint32_t)marked->v[i];
    st->atoms_len = marked->len;
    st->atom = a;
    name_step(s, g->lits.v, g->lits.len, nogood);
}

/* ---- watches (Watches.lean) --------------------------------------------- */

/* twoWatches: the first literal and the first later one differing from it */
static bool two_watches(const IVec *lits, int32_t *w1, int32_t *w2) {
    if (lits->len == 0u)
        return false;
    for (uint32_t i = 1u; i < lits->len; i++)
        if (lits->v[i] != lits->v[0]) {
            *w1 = lits->v[0];
            *w2 = lits->v[i];
            return true;
        }
    return false;
}

static bool on_trail(const Solver *s, int32_t lit);
static uint32_t lit_level(const Solver *s, int32_t m);

static bool fw_push(Solver *s, int32_t lit, FWatch w) {
    FWList *l = &s->fw[lit];
    if (l->len == l->cap) {
        uint32_t cap = l->cap ? 2u * l->cap : 4u;
        FWatch *v = realloc(l->v, sizeof(*v) * cap);
        if (!v) {
            s->oom = true;
            return false;
        }
        l->v = v;
        l->cap = cap;
    }
    l->v[l->len++] = w;
    return true;
}

static bool true_at_level0(const Solver *s, int32_t m) {
    return on_trail(s, m) && lit_level(s, m) == 0u;
}

/* the clause's two entries: a binary one's blocker is the other literal */
static void fast_watch(Solver *s, uint32_t i) {
    const FClause *c = &s->fc[i];
    int32_t ref = c->len == 2u ? ~c->nogood : (int32_t)i;
    fw_push(s, c->lits[0], (FWatch){c->lits[1], ref});
    fw_push(s, c->lits[1], (FWatch){c->lits[0], ref});
}

/* The nogood's clause, watched on w1 and w2 */
static void fast_attach(Solver *s, int32_t nogood, int32_t w1, int32_t w2) {
    const IVec *g = &s->pool[nogood].lits;
    s->fc = grow(s->fc, &s->capfc, s->nfc + 1u, sizeof(*s->fc), &s->oom);
    int32_t *lits = s->oom ? NULL : malloc(sizeof(*lits) * (g->len + 2u));
    if (!s->lit_stamp && !s->oom)
        s->lit_stamp = calloc(2u * (size_t)s->nvars + 2u, sizeof(*s->lit_stamp));
    if (!lits || !s->lit_stamp) {
        free(lits);
        s->oom = true;
        return;
    }
    /* each literal once, by a stamp per literal (a long learned nogood made
     * this quadratic) */
    if (++s->attach_stamp == 0u) {
        memset(s->lit_stamp, 0, sizeof(*s->lit_stamp) * (2u * (size_t)s->nvars + 2u));
        s->attach_stamp = 1u;
    }
    uint32_t n = 0u;
    lits[n++] = w1;
    lits[n++] = w2;
    s->lit_stamp[w1] = s->lit_stamp[w2] = s->attach_stamp;
    for (uint32_t k = 0u; k < g->len; k++) {
        int32_t m = g->v[k];
        if (s->lit_stamp[m] == s->attach_stamp)
            continue;
        s->lit_stamp[m] = s->attach_stamp;
        if (!true_at_level0(s, m))
            lits[n++] = m;
    }
    s->fc[s->nfc] = (FClause){nogood, n, lits, 0u};
    fast_watch(s, s->nfc++);
}

/* At level 0, after new facts there: clauses with a literal false at level
 * 0 are satisfied for good and dropped, literals true there leave the rest
 * (at a fixpoint no watched literal is true), and the watches are rebuilt. */
static void fast_simplify(Solver *s) {
    if (s->ntrail == s->fc_level0)
        return;
    s->fc_level0 = s->ntrail;
    for (uint32_t l = 0u; l < 2u * s->nvars; l++)
        s->fw[l].len = 0u;
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < s->nfc; i++) {
        FClause c = s->fc[i];
        bool satisfied = false;
        uint32_t n = 2u;
        for (uint32_t k = 0u; k < c.len && !satisfied; k++) {
            int32_t m = c.lits[k];
            if (on_trail(s, LIT_COMPL(m)))
                satisfied = true;
            else if (k >= 2u && !on_trail(s, m))
                c.lits[n++] = m;
        }
        if (satisfied) {
            free(c.lits);
            continue;
        }
        c.len = n;
        s->fc[kept] = c;
        fast_watch(s, kept++);
    }
    s->nfc = kept;
}

/* Learned nogoods kept (Glucose's schedule): a reduction after 2000
 * conflicts, then each 300 more than the last apart; it drops the worse half
 * of the learned clauses of more than two levels, worse meaning more levels,
 * then older, keeping every clause that is the reason of a literal on the
 * trail.  The nogoods themselves stay, for the certificate. */
#define FAST_REDUCE_FIRST 2000u
#define FAST_REDUCE_STEP 300u

/* the number of levels among the nogood's literals, all on the trail */
static uint32_t nogood_levels(Solver *s, const int32_t *lits, uint32_t n) {
    uint32_t count = 0u;
    if (++s->stamp == 0u) {
        memset(s->level_stamp, 0, sizeof(*s->level_stamp) * (s->nvars + 1u));
        s->stamp = 1u;
    }
    for (uint32_t k = 0u; k < n; k++) {
        uint32_t l = lit_level(s, lits[k]);
        if (s->level_stamp[l] != s->stamp) {
            s->level_stamp[l] = s->stamp;
            count++;
        }
    }
    return count;
}

/* whether the clause is the reason of its first literal's complement */
static bool fast_locked(const Solver *s, const FClause *c) {
    int32_t m = c->lits[0];
    return on_trail(s, LIT_COMPL(m)) &&
        s->trail[s->trail_pos[LIT_VAR(m)]].reason == c->nogood;
}

typedef struct {
    uint32_t lbd;
    uint32_t index;
} ReduceKey;

/* worst first: most levels, then oldest */
static int cmp_reduce(const void *x, const void *y) {
    const ReduceKey *a = x, *b = y;
    if (a->lbd != b->lbd)
        return a->lbd > b->lbd ? -1 : 1;
    return (a->index > b->index) - (a->index < b->index);
}

static void fast_reduce(Solver *s) {
    ReduceKey *cand = malloc(sizeof(*cand) * (s->nfc ? s->nfc : 1u));
    uint8_t *drop = calloc(s->nfc ? s->nfc : 1u, 1u);
    if (!cand || !drop) {
        free(cand);
        free(drop);
        s->oom = true;
        return;
    }
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < s->nfc; i++)
        if (s->fc[i].lbd > 2u && s->fc[i].len > 2u && !fast_locked(s, &s->fc[i]))
            cand[n++] = (ReduceKey){s->fc[i].lbd, i};
    if (n > 1u)
        qsort(cand, n, sizeof(*cand), cmp_reduce);
    for (uint32_t k = 0u; k < n / 2u; k++)
        drop[cand[k].index] = 1u;
    for (uint32_t l = 0u; l < 2u * s->nvars; l++)
        s->fw[l].len = 0u;
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < s->nfc; i++) {
        if (drop[i]) {
            free(s->fc[i].lits);
            continue;
        }
        s->fc[kept] = s->fc[i];
        fast_watch(s, kept++);
    }
    s->nfc = kept;
    free(cand);
    free(drop);
}

/* WState.addWatched */
static void add_watched(Solver *s, int32_t nogood, int32_t w1, int32_t w2) {
    if (s->fast) {
        fast_attach(s, nogood, w1, w2);
        return;
    }
    s->db = grow(s->db, &s->capdb, s->ndb + 1u, sizeof(*s->db), &s->oom);
    if (s->oom)
        return;
    uint32_t i = s->ndb++;
    s->db[i] = (Watched){nogood, w1, w2};
    if (!iv_push(&s->watches[w1], (int32_t)i) ||
        !iv_push(&s->watches[w2], (int32_t)i))
        s->oom = true;
}

typedef enum { PASS_OK, PASS_CONFLICT, PASS_FUEL } PassResult;

/* The fast search's visit of the nogoods watching x, in place: a nogood
 * whose watch moves leaves the list, the others stay in order, and at a
 * conflict the unvisited ones stay too. */
/* x became true: each clause watched on x keeps its other watched literal
 * w, moves to a literal not true, or makes compl w true, or conflicts. */
static PassResult process_list_fast(Solver *s, int32_t x, int32_t *conflict) {
    FWList *list = &s->fw[x];
    uint32_t n = list->len, i = 0u, kept = 0u;
    PassResult result = PASS_OK;
    for (; i < n; i++) {
        FWatch w = list->v[i];
        if (on_trail(s, LIT_COMPL(w.blocker))) {
            list->v[kept++] = w;
            continue;
        }
        if (w.ref < 0) {
            list->v[kept++] = w;
            if (on_trail(s, w.blocker)) {
                *conflict = ~w.ref;
                result = PASS_CONFLICT;
                i++;
                break;
            }
            push_lit(s, LIT_COMPL(w.blocker), ~w.ref);
            continue;
        }
        FClause *c = &s->fc[w.ref];
        if (c->lits[0] == x) {
            c->lits[0] = c->lits[1];
            c->lits[1] = x;
        }
        int32_t first = c->lits[0];
        FWatch keep = {first, w.ref};
        if (first != w.blocker && on_trail(s, LIT_COMPL(first))) {
            list->v[kept++] = keep;
            continue;
        }
        bool moved = false;
        for (uint32_t k = 2u; k < c->len; k++) {
            int32_t m = c->lits[k];
            if (!on_trail(s, m)) {
                c->lits[1] = m;
                c->lits[k] = x;
                moved = fw_push(s, m, keep);
                break;
            }
        }
        if (moved)
            continue;
        list->v[kept++] = keep;
        if (on_trail(s, first)) {
            *conflict = c->nogood;
            result = PASS_CONFLICT;
            i++;
            break;
        }
        push_lit(s, LIT_COMPL(first), c->nogood);
    }
    while (i < n)
        list->v[kept++] = list->v[i++];
    list->len = kept;
    return result;
}

static PassResult process_list(Solver *s, int32_t x, int32_t *conflict) {
    if (s->fast)
        return process_list_fast(s, x, conflict);
    IVec todo = s->watches[x];
    s->watches[x] = (IVec){0};
    IVec kept = {0}; /* visit order */
    PassResult result = PASS_OK;
    uint32_t t = todo.len;
    while (t-- > 0u) {
        int32_t i = todo.v[t];
        Watched *w = &s->db[i];
        if (w->w1 != x && w->w2 != x)
            continue;
        int32_t other = w->w1 == x ? w->w2 : w->w1;
        if (on_trail(s, LIT_COMPL(other))) {
            iv_push(&kept, i);
            continue;
        }
        const Nogood *g = &s->pool[w->nogood];
        int32_t r = -1;
        for (uint32_t k = 0u; k < g->lits.len && r < 0; k++) {
            int32_t m = g->lits.v[k];
            if (m != x && m != other && !on_trail(s, m))
                r = m;
        }
        if (r >= 0) {
            *w = (Watched){w->nogood, other, r};
            if (!iv_push(&s->watches[r], i))
                s->oom = true;
            continue;
        }
        if (on_trail(s, other)) {
            *conflict = w->nogood;
            result = PASS_CONFLICT;
            break;
        }
        push_lit(s, LIT_COMPL(other), w->nogood);
        iv_push(&kept, i);
    }
    IVec list = kept;
    if (result == PASS_CONFLICT) {
        for (uint32_t k = 0u; k <= t; k++)   /* the unvisited, then the current */
            iv_push(&list, todo.v[k]);
    }
    s->watches[x] = list;
    iv_free(&todo);
    return result;
}

/* propagateWatched */
static PassResult propagate_watched(Solver *s, uint32_t fuel, int32_t *conflict) {
    for (uint32_t f = 0u; f < fuel; f++) {
        if (s->pending.len == 0u)
            return PASS_OK;
        int32_t x = s->pending.v[--s->pending.len];
        PassResult r = process_list(s, x, conflict);
        if (r != PASS_OK)
            return r;
    }
    return PASS_FUEL;
}

static uint32_t lit_level(const Solver *s, int32_t m);

/* scanShort.  A unit nogood whose literal is false at level 0 is satisfied
 * for good, since level 0 is never undone; it is dropped from the list, the
 * others keeping their order, so the scan answers as before. */
static bool scan_short(Solver *s, int32_t *conflict) {
    uint32_t kept = 0u;
    bool ok = true;
    uint32_t k = 0u;
    for (; k < s->shorts.len; k++) {
        int32_t g = s->shorts.v[k];
        const IVec *lits = &s->pool[g].lits;
        if (lits->len == 0u || on_trail(s, lits->v[0])) {
            *conflict = g;
            ok = false;
            break;
        }
        if (on_trail(s, LIT_COMPL(lits->v[0]))) {
            if (lit_level(s, LIT_COMPL(lits->v[0])) > 0u)
                s->shorts.v[kept++] = g;
            continue;
        }
        push_lit(s, LIT_COMPL(lits->v[0]), g);
        s->shorts.v[kept++] = g;
    }
    while (k < s->shorts.len)
        s->shorts.v[kept++] = s->shorts.v[k++];
    s->shorts.len = kept;
    return ok;
}

/* ---- source pointers (SourcePointers.lean) ------------------------------- */

/* a falsum atom's rule never supports it: its body is never usable */
static bool body_false(const Solver *s, uint32_t r) {
    return falsum_rule(s, r) || on_trail(s, f_body(s, r));
}

/* The atoms the source-pointer check sources.  SourcePointers.lean proves
 * they are exactly the founded atoms (mem_sources_iff_founded), whatever the
 * sources kept from earlier checks: the least set holding the head of every
 * rule whose body is not false and whose positive atoms it holds.  Only that
 * set is observable (the marked atoms are the program atoms outside it), so
 * it is computed directly, by counting each rule's unfounded positive
 * occurrences. */
static void founded(Solver *s, uint8_t *sourced) {
    memset(sourced, 0, s->natoms ? s->natoms : 1u);
    uint32_t head = 0u, tail = 0u;
    for (uint32_t r = 0u; r < s->nrules; r++) {
        s->need[r] = s->rules[r].pos_len;
        if (s->need[r] == 0u && !body_false(s, r))
            s->queue[tail++] = r;
    }
    while (head < tail) {
        uint32_t r = s->queue[head++];
        uint32_t h = s->rule_head[r];
        if (sourced[h])
            continue;
        sourced[h] = 1u;
        for (uint32_t k = s->occ_start[h]; k < s->occ_start[h + 1u]; k++) {
            uint32_t q = s->occ_rules[k];
            if (--s->need[q] == 0u && !body_false(s, q))
                s->queue[tail++] = q;
        }
    }
}

static int cmp_i32(const void *a, const void *b);
static bool head_index(Solver *s);

/* Source pointers (the fast search, a program that is not tight), by the
 * shared core (native/support/support_v1.h).  An atom's source is a rule of
 * it whose body is not false and whose positive atoms have sources, the
 * sources well founded; at a fixpoint the atoms with sources are exactly the
 * founded atoms (mem_sources_iff_founded).  A source is lost when its body
 * becomes false, and so are the sources resting on an atom that lost its
 * own; the atoms without one that are not false are then given sources
 * again, from the atoms that have them, as founded() does from nothing.  The
 * atoms left without a source and not false are the unfounded set the fast
 * search uses, the same as founded() leaves. */
static bool asp_rule_usable(void *context, uint32_t r) {
    const Solver *s = context;
    /* justifications past the rules are the definitions' atoms', always
     * usable: no atom of a definition depends positively on its atom */
    return r >= s->nrules || !body_false(s, r);
}

static bool asp_atom_false(void *context, uint32_t a) {
    return on_trail((const Solver *)context, LIT(a, 0));
}

static bool sources_setup(Solver *s) {
    uint32_t nb = s->nvars - s->natoms;
    s->support = support_new(s->natoms);
    s->body_start = calloc(nb + 1u, sizeof(*s->body_start));
    s->body_rules = malloc(sizeof(*s->body_rules) * (s->nrules ? s->nrules : 1u));
    uint32_t *fill = malloc(sizeof(*fill) * (nb + 1u));
    uint32_t *body = NULL;
    uint32_t capbody = 0u;
    bool ok = s->support && s->body_start && s->body_rules && fill;
    /* rule r is justification r */
    for (uint32_t r = 0u; ok && r < s->nrules; r++) {
        const AspRule *rule = &s->rules[r];
        if (rule->pos_len > capbody) {
            uint32_t *grown = realloc(body, sizeof(*body) * rule->pos_len);
            ok = grown != NULL;
            if (grown) {
                body = grown;
                capbody = rule->pos_len;
            }
        }
        for (uint32_t k = 0u; ok && k < rule->pos_len; k++)
            body[k] = (uint32_t)atom_lookup(s, rule->pos[k]);
        ok = ok && support_add(s->support, s->rule_head[r], body, rule->pos_len) == r;
    }
    for (uint32_t d = 0u; ok && d < s->ndefs; d++)
        ok = support_add(s->support, s->def_var[d], NULL, 0u) == s->nrules + d;
    free(body);
    if (ok) {
        for (uint32_t r = 0u; r < s->nrules; r++)
            if (!falsum_rule(s, r) && s->rule_unit[r] < 0)
                s->body_start[s->rule_body[r] - s->natoms + 1u]++;
        for (uint32_t b = 0u; b < nb; b++)
            s->body_start[b + 1u] += s->body_start[b];
        memcpy(fill, s->body_start, sizeof(*fill) * (nb + 1u));
        for (uint32_t r = 0u; r < s->nrules; r++)
            if (!falsum_rule(s, r) && s->rule_unit[r] < 0)
                s->body_rules[fill[s->rule_body[r] - s->natoms]++] = r;
        /* the unit-body rules by the atom literal that makes them false */
        uint32_t nl = 2u * s->natoms;
        s->unit_start = calloc(nl + 1u, sizeof(*s->unit_start));
        s->unit_rules = malloc(sizeof(*s->unit_rules) * (s->nrules ? s->nrules : 1u));
        uint32_t *ufill = malloc(sizeof(*ufill) * (nl + 1u));
        ok = s->unit_start && s->unit_rules && ufill;
        for (uint32_t r = 0u; ok && r < s->nrules; r++)
            if (s->rule_unit[r] >= 0)
                s->unit_start[LIT_COMPL(s->rule_unit[r]) + 1u]++;
        for (uint32_t x = 0u; ok && x < nl; x++)
            s->unit_start[x + 1u] += s->unit_start[x];
        if (ok)
            memcpy(ufill, s->unit_start, sizeof(*ufill) * (nl + 1u));
        for (uint32_t r = 0u; ok && r < s->nrules; r++)
            if (s->rule_unit[r] >= 0)
                s->unit_rules[ufill[LIT_COMPL(s->rule_unit[r])]++] = r;
        free(ufill);
    }
    free(fill);
    s->sp_read = 0u;
    return ok;
}

/* The unfounded atoms that are not false, in atom order, into marked/in_m. */
static bool sources_check(Solver *s, IVec *marked, uint8_t *in_m) {
    bool ok = true;
    /* bodies false since the last check lose the sources they are */
    for (uint32_t i = s->sp_read; ok && i < s->ntrail; i++) {
        int32_t lit = s->trail[i].lit;
        uint32_t v = (uint32_t)LIT_VAR(lit);
        if (v < s->natoms) {
            for (uint32_t k = s->unit_start[lit]; ok && k < s->unit_start[lit + 1u]; k++)
                ok = support_lose(s->support, s->unit_rules[k]);
            continue;
        }
        if (LIT_SIGN(lit))
            continue;
        uint32_t b = v - s->natoms;
        for (uint32_t k = s->body_start[b]; ok && k < s->body_start[b + 1u]; k++)
            ok = support_lose(s->support, s->body_rules[k]);
    }
    s->sp_read = s->ntrail;
    uint32_t *unsupported = NULL;
    uint32_t n = 0u;
    ok = ok && support_recheck(s->support, asp_rule_usable, asp_atom_false, s,
                               &unsupported, &n);
    marked->len = 0u;
    for (uint32_t i = 0u; ok && i < n; i++)
        ok = iv_push(marked, (int32_t)unsupported[i]);
    free(unsupported);
    for (uint32_t i = 0u; ok && i < marked->len; i++)
        in_m[marked->v[i]] = 1u;
    return ok;
}

static int cmp_i32(const void *a, const void *b);

/* the rules by head, built once */
static bool head_index(Solver *s) {
    if (s->head_start)
        return true;
    s->head_start = calloc(s->natoms + 1u, sizeof(*s->head_start));
    s->head_rules = malloc(sizeof(*s->head_rules) * (s->nrules ? s->nrules : 1u));
    uint32_t *fill = malloc(sizeof(*fill) * (s->natoms + 1u));
    if (!s->head_start || !s->head_rules || !fill) {
        free(fill);
        free(s->head_start);
        s->head_start = NULL;
        return false;
    }
    for (uint32_t r = 0u; r < s->nrules; r++)
        s->head_start[s->rule_head[r] + 1u]++;
    for (uint32_t a = 0u; a < s->natoms; a++)
        s->head_start[a + 1u] += s->head_start[a];
    memcpy(fill, s->head_start, sizeof(*fill) * (s->natoms + 1u));
    for (uint32_t r = 0u; r < s->nrules; r++)
        s->head_rules[fill[s->rule_head[r]]++] = r;
    free(fill);
    return true;
}

/* The external bodies of the marked set U, in program order: the rules whose
 * head is in U and none of whose positive atoms is (Program.externalBodies).
 * U's atoms are listed in `atoms` (marked in in_u); only their rules are
 * visited, then put back in program order. */
static bool external_rules(Solver *s, const uint8_t *in_u, const int32_t *atoms,
                           uint32_t n, IVec *out) {
    out->len = 0u;
    if (!head_index(s))
        return false;
    for (uint32_t i = 0u; i < n; i++) {
        uint32_t a = (uint32_t)atoms[i];
        for (uint32_t k = s->head_start[a]; k < s->head_start[a + 1u]; k++) {
            uint32_t r = s->head_rules[k];
            bool external = true;
            const AspRule *rule = &s->rules[r];
            for (uint32_t j = 0u; j < rule->pos_len && external; j++)
                external = !in_u[atom_lookup(s, rule->pos[j])];
            if (external && !iv_push(out, (int32_t)r))
                return false;
        }
    }
    if (out->len > 1u)
        qsort(out->v, out->len, sizeof(*out->v), cmp_i32);
    uint32_t kept = 0u;   /* an atom listed twice gives its rules twice */
    for (uint32_t k = 0u; k < out->len; k++)
        if (kept == 0u || out->v[kept - 1u] != out->v[k])
            out->v[kept++] = out->v[k];
    out->len = kept;
    return true;
}

/* Program.loopNogood: T a, then F body for each external rule of U */
static bool loop_lits(const Solver *s, const IVec *external, uint32_t a, IVec *l) {
    bool ok = iv_push(l, LIT(a, 1));
    for (uint32_t k = 0u; ok && k < external->len; k++)
        ok = iv_push(l, f_body(s, (uint32_t)external->v[k]));
    return ok;
}

static int32_t loop_nogood_of(Solver *s, const IVec *external, uint32_t a) {
    IVec l = {0};
    int32_t g = loop_lits(s, external, a, &l) ? new_nogood(s, l.v, l.len) : -1;
    iv_free(&l);
    return g;
}

/* ---- combined propagation (Propagation.lean, propagateAll) --------------- */

typedef enum { PROP_CLOSED, PROP_CONFLICT, PROP_FUEL } PropResult;

/* ---- weight definitions ------------------------------------------------ */

/* A theory's consequence as a nogood over lits, with its step (a weight
 * step over definition d, or an EUF step): the clause of the nogood is one
 * the theory implies.  It is watched on
 * its unassigned literal, if any (the one it implies), and its newest other
 * literal, as hall_nogood watches. */
static int32_t theory_nogood(Solver *s, AspStepKind kind, uint32_t d,
                             const IVec *lits) {
    int32_t g = new_nogood(s, lits->v, lits->len);
    if (g < 0)
        return -1;
    AspStep *st = new_step(s);
    if (!st)
        return -1;
    st->kind = kind;
    st->id = s->next_id;
    st->def = d;
    if (!lrat_clause(lits->v, lits->len, &st->clause, &st->clause_len)) {
        s->oom = true;
        return -1;
    }
    name_step(s, lits->v, lits->len, g);
    const IVec *l = &s->pool[g].lits;
    if (l->len >= 2u) {
        /* rank: unassigned first, then by trail position, newest first */
        uint32_t i1 = UINT32_MAX, i2 = UINT32_MAX;
        for (uint32_t k = 0u; k < l->len; k++) {
            uint32_t v = LIT_VAR(l->v[k]);
            uint64_t rank = s->val[v] < 0 ? UINT64_MAX : s->trail_pos[v];
            uint64_t r1 = i1 == UINT32_MAX ? 0u
                : s->val[LIT_VAR(l->v[i1])] < 0 ? UINT64_MAX
                : s->trail_pos[LIT_VAR(l->v[i1])];
            uint64_t r2 = i2 == UINT32_MAX ? 0u
                : s->val[LIT_VAR(l->v[i2])] < 0 ? UINT64_MAX
                : s->trail_pos[LIT_VAR(l->v[i2])];
            if (i1 == UINT32_MAX || rank > r1) {
                i2 = i1;
                i1 = k;
            } else if (i2 == UINT32_MAX || rank > r2) {
                i2 = k;
            }
        }
        add_watched(s, g, l->v[i1], l->v[i2]);
    } else if (!iv_push(&s->shorts, g)) {
        s->oom = true;
    }
    return s->oom ? -1 : g;
}

/* The literals of definition d with the given truth (true: contributing),
 * in trail order, pushed onto `out` as nogood literals (each as it is on
 * the trail), until their weight passes `enough`; their weight. */
static uint64_t weight_reasons(Solver *s, uint32_t d, bool truth,
                               uint64_t enough, IVec *out, int32_t skip) {
    /* the definition's literals with that truth, oldest first */
    uint64_t sum = 0u;
    uint32_t from = s->dl_start[d], to = s->dl_start[d + 1u];
    uint32_t count = 0u;
    for (uint32_t k = from; k < to; k++)
        if ((int32_t)k != skip &&
            s->val[LIT_VAR(s->dl_lit[k])] >= 0 &&
            on_trail(s, s->dl_lit[k]) == truth)
            count++;
    uint32_t *order = malloc(sizeof(*order) * (count ? count : 1u));
    if (!order) {
        s->oom = true;
        return 0u;
    }
    uint32_t m = 0u;
    for (uint32_t k = from; k < to; k++)
        if ((int32_t)k != skip &&
            s->val[LIT_VAR(s->dl_lit[k])] >= 0 &&
            on_trail(s, s->dl_lit[k]) == truth)
            order[m++] = k;
    /* insertion by trail position: definitions are short next to the trail */
    for (uint32_t i = 1u; i < m; i++) {
        uint32_t x = order[i], j = i;
        uint32_t px = s->trail_pos[LIT_VAR(s->dl_lit[x])];
        while (j > 0u && s->trail_pos[LIT_VAR(s->dl_lit[order[j - 1u]])] > px) {
            order[j] = order[j - 1u];
            j--;
        }
        order[j] = x;
    }
    for (uint32_t i = 0u; i < m && sum <= enough; i++) {
        int32_t lit = s->dl_lit[order[i]];
        if (!iv_push(out, truth ? lit : LIT_COMPL(lit))) {
            s->oom = true;
            break;
        }
        sum += s->dl_w[order[i]];
    }
    free(order);
    return sum;
}

/* Every definition looked at again: its atom W holds exactly when its true
 * literals weigh at least L.  Weight t true, f false, of total T:
 *   t >= L: W; T - f < L: not W;
 *   W and T - f - w(l) < L: l; not W and t + w(l) >= L: not l,
 * each consequence a nogood with its weight step.  -1 on a conflict (its
 * nogood in *conflict), else how many literals it implied. */
static int32_t weight_propagate(Solver *s, int32_t *conflict) {
    int32_t implied = 0;
    IVec lits = {0};
    while (s->wdirty.len && !s->oom) {
        uint32_t d = (uint32_t)s->wdirty.v[--s->wdirty.len];
        s->in_wdirty[d] = 0u;
        uint64_t bound = s->defs[d].bound;
        uint64_t total = s->d_total[d], t = s->d_true[d], f = s->d_false[d];
        uint64_t most = total - f;
        int32_t W = LIT(s->def_var[d], 1);
        int8_t wv = s->val[s->def_var[d]];
        if (t >= bound || most < bound) {
            /* W's value is forced: W true when t >= L, false when most < L */
            bool value = t >= bound;
            if (wv >= 0 && (bool)wv == value)
                continue;
            lits.len = 0u;
            /* bound 0: W holds with no reason; bound past the total: W fails
             * with none */
            if (value && bound > 0u)
                weight_reasons(s, d, true, bound - 1u, &lits, -1);
            else if (!value && bound <= total)
                weight_reasons(s, d, false, total - bound, &lits, -1);
            if (s->oom)
                break;
            if (!iv_push(&lits, value ? LIT_COMPL(W) : W)) {
                s->oom = true;
                break;
            }
            int32_t g = theory_nogood(s, ASP_STEP_WEIGHT, d, &lits);
            if (g < 0)
                break;
            if (wv >= 0) {
                *conflict = g;
                iv_free(&lits);
                return -1;
            }
            push_lit(s, value ? W : LIT_COMPL(W), g);
            implied++;
            continue;
        }
        if (wv < 0)
            continue;
        /* W decided: each open literal whose value would decide it otherwise */
        for (uint32_t k = s->dl_start[d]; k < s->dl_start[d + 1u] && !s->oom; k++) {
            int32_t l = s->dl_lit[k];
            if (s->val[LIT_VAR(l)] >= 0)
                continue;
            uint64_t w = s->dl_w[k];
            bool force;
            if (wv == 1)
                force = most - w < bound;        /* l false would leave too little */
            else
                force = t + w >= bound;          /* l true would reach the bound */
            if (!force)
                continue;
            lits.len = 0u;
            if (wv == 1) {
                /* false literals F with total - w(F) - w < L (none needed
                 * when total - w < L already) */
                if (total - w >= bound)
                    weight_reasons(s, d, false, total - w - bound, &lits, (int32_t)k);
                iv_push(&lits, W);
                iv_push(&lits, LIT_COMPL(l));
            } else {
                /* true literals S with w(S) + w >= L */
                if (bound > w)
                    weight_reasons(s, d, true, bound - w - 1u, &lits, (int32_t)k);
                iv_push(&lits, LIT_COMPL(W));
                iv_push(&lits, l);
            }
            if (s->oom)
                break;
            int32_t g = theory_nogood(s, ASP_STEP_WEIGHT, d, &lits);
            if (g < 0)
                break;
            push_lit(s, wv == 1 ? l : LIT_COMPL(l), g);
            implied++;
            /* the definition's sums moved: it is looked at again */
        }
    }
    iv_free(&lits);
    return s->oom ? -1 : implied;
}

/* ---- the EUF theory (the SMT personality) ------------------------------ */

/* The nogood of atom e's consequence: the atoms explaining lhs = rhs true,
 * and `last`. */
static int32_t euf_consequence(Solver *s, uint32_t e, int32_t last) {
    const AspSmtEquality *eq = &s->smt_eqs[e];
    uint32_t *reasons = NULL, n = 0u;
    if (!euf_explain(s->cc, s->cc_term[eq->lhs], s->cc_term[eq->rhs],
                     &reasons, &n)) {
        s->oom = true;
        return -1;
    }
    IVec lits = {0};
    bool ok = true;
    for (uint32_t k = 0u; ok && k < n; k++)
        ok = iv_push(&lits, LIT(s->smt_eqs[reasons[k]].var - 1u, 1));
    ok = ok && iv_push(&lits, last);
    free(reasons);
    int32_t g = ok ? theory_nogood(s, ASP_STEP_EUF, 0u, &lits) : -1;
    if (!ok)
        s->oom = true;
    iv_free(&lits);
    return g;
}

/* The equality atoms on the trail since the last read, into the closure;
 * then a false atom whose terms became equal is a conflict, and an open
 * atom whose terms are equal is implied.  -1 on a conflict (its nogood in
 * *conflict), else how many atoms it implied. */
static int32_t euf_propagate(Solver *s, int32_t *conflict) {
    for (uint32_t i = s->euf_read; i < s->ntrail && !s->oom; i++) {
        int32_t lit = s->trail[i].lit;
        int32_t e = s->var_eq[LIT_VAR(lit)];
        if (e < 0)
            continue;
        if (!iv_push(&s->euf_marks, (int32_t)i) ||
            !iv_push(&s->euf_marks, (int32_t)euf_height(s->cc))) {
            s->oom = true;
            break;
        }
        const AspSmtEquality *eq = &s->smt_eqs[e];
        if (LIT_SIGN(lit)) {
            if (!euf_merge(s->cc, s->cc_term[eq->lhs], s->cc_term[eq->rhs],
                           (uint32_t)e))
                s->oom = true;
        } else if (!iv_push(&s->euf_false, e) ||
                   !iv_push(&s->euf_false, (int32_t)i)) {
            s->oom = true;
        }
    }
    s->euf_read = s->ntrail;
    if (s->oom)
        return -1;
    for (uint32_t k = 0u; k < s->euf_false.len; k += 2u) {
        uint32_t e = (uint32_t)s->euf_false.v[k];
        const AspSmtEquality *eq = &s->smt_eqs[e];
        if (!euf_equal(s->cc, s->cc_term[eq->lhs], s->cc_term[eq->rhs]))
            continue;
        int32_t g = euf_consequence(s, e, LIT(eq->var - 1u, 0));
        if (g < 0)
            return -1;
        *conflict = g;
        return -1;
    }
    int32_t implied = 0;
    for (uint32_t e = 0u; e < s->smt_neqs && !s->oom; e++) {
        const AspSmtEquality *eq = &s->smt_eqs[e];
        uint32_t v = eq->var - 1u;
        if (s->val[v] >= 0 ||
            !euf_equal(s->cc, s->cc_term[eq->lhs], s->cc_term[eq->rhs]))
            continue;
        int32_t g = euf_consequence(s, e, LIT(v, 0));
        if (g < 0)
            return -1;
        push_lit(s, LIT(v, 1), g);
        implied++;
    }
    return s->oom ? -1 : implied;
}

static bool lra_setup(Solver *s, const AspSmtTheories *th);

/* The closure over the input terms; each equality atom's variable; the
 * LRA atoms' columns. */
static bool smt_setup(Solver *s, const AspSmtTheories *th) {
    const AspSmtTerm *terms = th->terms;
    uint32_t nterms = th->nterms;
    const AspSmtEquality *eqs = th->eqs;
    uint32_t neqs = th->neqs;
    s->theories = th;
    s->smt_terms = terms;
    s->smt_nterms = nterms;
    s->smt_eqs = eqs;
    s->smt_neqs = neqs;
    s->cc = euf_new();
    s->cc_term = malloc(sizeof(*s->cc_term) * (nterms ? nterms : 1u));
    s->var_eq = malloc(sizeof(*s->var_eq) * (s->nvars ? s->nvars : 1u));
    uint32_t *args = NULL;
    uint32_t cap = 0u;
    bool ok = s->cc && s->cc_term && s->var_eq;
    for (uint32_t v = 0u; ok && v < s->nvars; v++)
        s->var_eq[v] = -1;
    for (uint32_t t = 0u; ok && t < nterms; t++) {
        if (terms[t].nargs > cap) {
            cap = terms[t].nargs;
            uint32_t *grown = realloc(args, sizeof(*args) * cap);
            ok = grown != NULL;
            if (grown)
                args = grown;
        }
        for (uint32_t i = 0u; ok && i < terms[t].nargs; i++) {
            ok = terms[t].args[i] < t;
            if (ok)
                args[i] = s->cc_term[terms[t].args[i]];
        }
        if (ok) {
            s->cc_term[t] = euf_term(s->cc, terms[t].symbol, args, terms[t].nargs);
            ok = s->cc_term[t] != UINT32_MAX;
        }
    }
    free(args);
    for (uint32_t e = 0u; ok && e < neqs; e++) {
        ok = eqs[e].var >= 1u && eqs[e].var <= s->nvars &&
             eqs[e].lhs < nterms && eqs[e].rhs < nterms &&
             s->var_eq[eqs[e].var - 1u] < 0;
        if (ok)
            s->var_eq[eqs[e].var - 1u] = (int32_t)e;
    }
    return ok && lra_setup(s, th);
}

/* ---- the LRA theory (the SMT personality) ------------------------------ */

static bool lra_setup(Solver *s, const AspSmtTheories *th) {
    s->lra_objective = -1;
    if (!th->nlra && !th->objective_n)
        return true;
    s->lra = lra_new(th->nreals);
    s->var_lra = malloc(sizeof(*s->var_lra) * (s->nvars ? s->nvars : 1u));
    s->lra_col = malloc(sizeof(*s->lra_col) * (th->nlra ? th->nlra : 1u));
    bool ok = s->lra && s->var_lra && s->lra_col;
    for (uint32_t v = 0u; ok && v < s->nvars; v++)
        s->var_lra[v] = -1;
    for (uint32_t e = 0u; ok && e < th->nlra; e++) {
        const AspLraAtom *at = &th->lra[e];
        ok = at->var >= 1u && at->var <= s->nvars && s->var_lra[at->var - 1u] < 0 &&
             s->var_eq[at->var - 1u] < 0 && at->rel <= ASP_LRA_GT && at->bound;
        for (uint32_t i = 0u; ok && i < at->n; i++)
            ok = at->reals[i] < th->nreals && at->coefs[i];
        int32_t col = ok ? lra_form(s->lra, at->reals, at->coefs, at->n) : -1;
        ok = col >= 0;
        if (ok) {
            s->lra_col[e] = (uint32_t)col;
            s->var_lra[at->var - 1u] = (int32_t)e;
        }
    }
    for (uint32_t i = 0u; ok && i < th->objective_n; i++)
        ok = th->objective_reals[i] < th->nreals && th->objective_coefs[i];
    if (ok && th->objective_n) {
        s->lra_objective = lra_form(s->lra, th->objective_reals, th->objective_coefs,
                                    th->objective_n);
        ok = s->lra_objective >= 0;
    }
    return ok;
}

/* The bound atom e asserts when its literal holds (truth) or fails. */
static void lra_side(const AspLraAtom *at, bool truth, bool *upper, bool *strict) {
    bool le = at->rel == ASP_LRA_LE || at->rel == ASP_LRA_LT;
    bool st = at->rel == ASP_LRA_LT || at->rel == ASP_LRA_GT;
    *upper = truth ? le : !le;     /* not (t <= b) is t > b */
    *strict = truth ? st : !st;
}

/* The explanation of the last conflict as a nogood (its reasons are the
 * trail's literals) with an LRA step, weights per clause literal. */
static int32_t lra_conflict(Solver *s) {
    const int32_t *reasons = NULL;
    const char *const *coefs = NULL;
    uint32_t n = lra_explanation(s->lra, &reasons, &coefs);
    IVec lits = {0};
    bool ok = n > 0u;
    for (uint32_t k = 0u; ok && k < n; k++)
        for (uint32_t j = 0u; ok && j < k; j++)
            ok = reasons[j] != reasons[k];   /* each bound its own atom */
    for (uint32_t k = 0u; ok && k < n; k++)
        ok = iv_push(&lits, reasons[k]);
    int32_t g = ok ? theory_nogood(s, ASP_STEP_LRA, 0u, &lits) : -1;
    if (g >= 0) {
        AspStep *st = &s->steps[s->nsteps - 1u];
        st->weights = malloc(sizeof(*st->weights) * n);
        ok = st->weights != NULL && st->clause_len == n;
        for (uint32_t k = 0u; ok && k < n; k++) {
            size_t len = strlen(coefs[k]) + 1u;
            st->weights[k] = malloc(len);
            if (st->weights[k])
                memcpy(st->weights[k], coefs[k], len);
            ok = st->weights[k] != NULL;
            st->weights_len = k + 1u;
        }
        if (!ok)
            g = -1;
    }
    if (g < 0)
        s->oom = true;
    iv_free(&lits);
    return g;
}

/* The bound atoms on the trail since the last read, asserted; then the
 * simplex's check.  -1 on a conflict (its nogood in *conflict) or a fault,
 * else 0. */
static int32_t lra_propagate(Solver *s, int32_t *conflict) {
    const AspSmtTheories *th = s->theories;
    for (uint32_t i = s->lra_read; i < s->ntrail && !s->oom; i++) {
        int32_t lit = s->trail[i].lit;
        int32_t e = s->var_lra[LIT_VAR(lit)];
        if (e < 0)
            continue;
        if (!iv_push(&s->lra_marks, (int32_t)i) ||
            !iv_push(&s->lra_marks, (int32_t)lra_height(s->lra))) {
            s->oom = true;
            break;
        }
        bool upper, strict;
        lra_side(&th->lra[e], LIT_SIGN(lit) != 0, &upper, &strict);
        LraResult r = lra_assert(s->lra, s->lra_col[e], upper, th->lra[e].bound,
                                 strict, lit);
        if (r != LRA_OK) {
            s->lra_read = i + 1u;
            if (r == LRA_ERROR) {
                s->oom = true;
                return -1;
            }
            *conflict = lra_conflict(s);
            return -1;
        }
    }
    s->lra_read = s->ntrail;
    if (s->oom)
        return -1;
    LraResult r = lra_check(s->lra);
    if (r == LRA_OK)
        return 0;
    if (r == LRA_ERROR)
        s->oom = true;
    else
        *conflict = lra_conflict(s);
    return -1;
}

/* An input step is accepted exactly for an added constraint: its clause is
 * the constraint's (the complements of the nogood T pos, F neg), as sets. */
static bool constraint_lits(Solver *s, const AspRule *c, IVec *lits) {
    lits->len = 0u;
    for (uint32_t i = 0u; i < c->pos_len; i++) {
        int32_t v = atom_lookup(s, c->pos[i]);
        if (v < 0 || !iv_push(lits, native_lit(LIT(v, 0))))
            return false;
    }
    for (uint32_t i = 0u; i < c->neg_len; i++) {
        int32_t v = atom_lookup(s, c->neg[i]);
        if (v < 0 || !iv_push(lits, native_lit(LIT(v, 1))))
            return false;
    }
    return true;
}

/* The added constraints' clauses as sets (sorted, without repeats), in a
 * hash table: an input step is one lookup.  A constraint over an atom the
 * program does not have matches no step. */
typedef struct {
    int32_t **sets;
    uint32_t *lens;
    uint32_t n;
    uint32_t *table;    /* index + 1, 0 empty */
    uint32_t mask;
    bool built;
} InputIndex;

static uint32_t set_canonical(int32_t *v, uint32_t n) {
    qsort(v, n, sizeof(*v), cmp_i32);
    uint32_t k = 0u;
    for (uint32_t i = 0u; i < n; i++)
        if (k == 0u || v[k - 1u] != v[i])
            v[k++] = v[i];
    return k;
}

static uint32_t set_hash(const int32_t *v, uint32_t n) {
    uint32_t h = 2166136261u ^ n;
    for (uint32_t i = 0u; i < n; i++)
        h = (h ^ (uint32_t)v[i]) * 16777619u;
    return h;
}

static void input_index_free(InputIndex *ix) {
    for (uint32_t i = 0u; i < ix->n; i++)
        free(ix->sets[i]);
    free(ix->sets);
    free(ix->lens);
    free(ix->table);
    *ix = (InputIndex){0};
}

static bool input_index_build(Solver *s, InputIndex *ix) {
    ix->built = true;
    uint32_t cap = 16u;
    while (cap < 2u * s->nextra + 1u)
        cap *= 2u;
    ix->sets = malloc(sizeof(*ix->sets) * (s->nextra ? s->nextra : 1u));
    ix->lens = malloc(sizeof(*ix->lens) * (s->nextra ? s->nextra : 1u));
    ix->table = calloc(cap, sizeof(*ix->table));
    ix->mask = cap - 1u;
    if (!ix->sets || !ix->lens || !ix->table)
        return false;
    IVec lits = {0};
    bool ok = true;
    for (uint32_t c = 0u; ok && c < s->nextra; c++) {
        if (!constraint_lits(s, &s->extra[c], &lits))
            continue;
        int32_t *set = malloc(sizeof(*set) * (lits.len ? lits.len : 1u));
        if (!set) {
            ok = false;
            break;
        }
        if (lits.len)
            memcpy(set, lits.v, sizeof(*set) * lits.len);
        uint32_t len = set_canonical(set, lits.len);
        uint32_t slot = set_hash(set, len) & ix->mask;
        while (ix->table[slot])
            slot = (slot + 1u) & ix->mask;
        ix->sets[ix->n] = set;
        ix->lens[ix->n] = len;
        ix->table[slot] = ++ix->n;
    }
    iv_free(&lits);
    return ok;
}

static bool input_step_ok(Solver *s, InputIndex *ix, const AspStep *st) {
    if (!ix->built && !input_index_build(s, ix))
        return false;
    if (!ix->table)
        return false;
    int32_t *set = malloc(sizeof(*set) * (st->clause_len ? st->clause_len : 1u));
    if (!set)
        return false;
    if (st->clause_len)
        memcpy(set, st->clause, sizeof(*set) * st->clause_len);
    uint32_t len = set_canonical(set, st->clause_len);
    bool ok = false;
    for (uint32_t slot = set_hash(set, len) & ix->mask; !ok && ix->table[slot];
         slot = (slot + 1u) & ix->mask) {
        uint32_t i = ix->table[slot] - 1u;
        ok = ix->lens[i] == len &&
             (len == 0u || memcmp(ix->sets[i], set, sizeof(*set) * len) == 0);
    }
    free(set);
    return ok;
}

/* An LRA step is accepted when its clause holds a variable both ways, or
 * the bounds its falsification asserts, weighed by its coefficients, pass
 * the Farkas check: no reals meet them. */
static bool lra_step_ok(Solver *s, const AspStep *st) {
    const AspSmtTheories *th = s->theories;
    if (!s->var_lra || !th || st->weights_len != st->clause_len || st->clause_len == 0u)
        return false;
    for (uint32_t i = 0u; i < st->clause_len; i++) {
        int32_t x = st->clause[i];
        uint32_t v = (uint32_t)(x < 0 ? -x : x) - 1u;
        if (x == 0 || v >= s->nvars)
            return false;
        for (uint32_t j = 0u; j < i; j++)
            if (st->clause[j] == -x)
                return true;
    }
    LraFarkasBound *bounds = malloc(sizeof(*bounds) * st->clause_len);
    bool ok = bounds != NULL;
    for (uint32_t i = 0u; ok && i < st->clause_len; i++) {
        int32_t x = st->clause[i];
        int32_t e = s->var_lra[(uint32_t)(x < 0 ? -x : x) - 1u];
        ok = e >= 0;
        if (!ok)
            break;
        const AspLraAtom *at = &th->lra[e];
        bool upper, strict;
        lra_side(at, x < 0, &upper, &strict);   /* falsified: -a holds a */
        bounds[i] = (LraFarkasBound){at->reals, at->coefs, at->n, upper, strict,
                                     at->bound, st->weights[i]};
    }
    ok = ok && lra_farkas_check(bounds, st->clause_len);
    free(bounds);
    return ok;
}

/* An EUF step is accepted when its clause holds a variable both ways, or
 * the equalities its falsification makes true entail, by congruence, one it
 * makes false. */
static bool euf_step_ok(Solver *s, const AspStep *st) {
    if (!s->cc)
        return false;
    for (uint32_t i = 0u; i < st->clause_len; i++) {
        int32_t x = st->clause[i];
        uint32_t v = (uint32_t)(x < 0 ? -x : x) - 1u;
        if (x == 0 || v >= s->nvars)
            return false;
        for (uint32_t j = 0u; j < i; j++)
            if (st->clause[j] == -x)
                return true;
    }
    uint32_t height = euf_height(s->cc);
    bool ok = true;
    for (uint32_t i = 0u; ok && i < st->clause_len; i++) {
        int32_t x = st->clause[i];
        int32_t e = s->var_eq[(uint32_t)(x < 0 ? -x : x) - 1u];
        if (x < 0 && e >= 0)   /* falsified, the atom holds */
            ok = euf_merge(s->cc, s->cc_term[s->smt_eqs[e].lhs],
                           s->cc_term[s->smt_eqs[e].rhs], (uint32_t)e);
    }
    bool entailed = false;
    for (uint32_t i = 0u; ok && !entailed && i < st->clause_len; i++) {
        int32_t x = st->clause[i];
        int32_t e = s->var_eq[(uint32_t)(x < 0 ? -x : x) - 1u];
        if (x > 0 && e >= 0)   /* falsified, the atom fails */
            entailed = euf_equal(s->cc, s->cc_term[s->smt_eqs[e].lhs],
                                 s->cc_term[s->smt_eqs[e].rhs]);
    }
    euf_undo(s->cc, height);
    return ok && entailed;
}

static PropResult propagate_all(Solver *s, int32_t *conflict) {
    uint8_t *sourced = NULL;   /* for the unfounded-set check, on first use */
    uint8_t *in_m = NULL;
    IVec marked = {0};
    IVec fresh = {0};
    IVec external = {0};
    PropResult result = PROP_FUEL;
    for (uint32_t round = 0u; round < s->nvars + 1u && !s->oom; round++) {
        PassResult w = propagate_watched(s, s->nvars + 1u, conflict);
        if (w == PASS_FUEL)
            goto done;
        if (w == PASS_CONFLICT) {
            result = PROP_CONFLICT;
            goto done;
        }
        if (!scan_short(s, conflict)) {
            result = PROP_CONFLICT;
            goto done;
        }
        if (s->pending.len)
            continue;
        if (s->wdirty.len) {
            int32_t implied = weight_propagate(s, conflict);
            if (implied < 0) {
                result = s->oom ? PROP_FUEL : PROP_CONFLICT;
                goto done;
            }
            if (s->pending.len)
                continue;
        }
        if (s->cc) {
            int32_t implied = euf_propagate(s, conflict);
            if (implied < 0) {
                result = s->oom ? PROP_FUEL : PROP_CONFLICT;
                goto done;
            }
            if (s->pending.len)
                continue;
        }
        if (s->lra && lra_propagate(s, conflict) < 0) {
            result = s->oom ? PROP_FUEL : PROP_CONFLICT;
            goto done;
        }
        /* At a fixpoint of a tight program an atom whose every body is false
         * is false, so by induction along the positive dependencies every
         * atom the founded set leaves out is false: the check finds no
         * fresh atom and no true one. */
        if (s->tight) {
            result = PROP_CLOSED;
            goto done;
        }
        if (!sourced) {
            sourced = malloc(s->natoms ? s->natoms : 1u);
            in_m = malloc(s->natoms ? s->natoms : 1u);
            if (!sourced || !in_m) {
                s->oom = true;
                goto done;
            }
        }
        marked.len = 0u;
        memset(in_m, 0, s->natoms ? s->natoms : 1u);
        /* The fast search leaves the false atoms out of the unfounded set:
         * at this fixpoint every body with a false positive atom is false,
         * so the rest is unfounded as well, and its external bodies, the
         * loop nogoods' literals, are only those of its open and true
         * atoms.  It finds them by its source pointers. */
        if (s->fast) {
            if (!s->support && !sources_setup(s)) {
                s->oom = true;
                goto done;
            }
            if (!sources_check(s, &marked, in_m)) {
                s->oom = true;
                goto done;
            }
        } else {
            /* the atoms not founded, but the falsum atoms: those are false
             * from their units on, so every rule resting on one has a false
             * body and the rest is unfounded as well; and their own rules
             * have no body variables for a loop nogood */
            founded(s, sourced);
            for (uint32_t a = 0u; a < s->natoms; a++)
                if (!sourced[a] && !s->falsum[a]) {
                    iv_push(&marked, (int32_t)a);
                    in_m[a] = 1u;
                }
        }
        int32_t true_marked = -1;
        for (uint32_t k = 0u; k < marked.len && true_marked < 0; k++)
            if (on_trail(s, LIT(marked.v[k], 1)))
                true_marked = marked.v[k];
        /* The external bodies make loop nogoods, made only for a true or a
         * fresh marked atom. */
        bool loops_needed = true_marked >= 0;
        for (uint32_t k = 0u; k < marked.len && !loops_needed; k++)
            loops_needed = !on_trail(s, LIT(marked.v[k], 0));
        if (loops_needed) {
            if (!external_rules(s, in_m, marked.v, marked.len, &external)) {
                s->oom = true;
                goto done;
            }
        }
        if (s->trace && marked.len) {
            for (uint32_t k = 0u; k < marked.len; k++)
                marked.v[k] = native_lit(LIT(marked.v[k], 1));
            s->trace->event(s->trace_context, ASP_TRACE_UNFOUNDED, 0, s->level,
                            -1, marked.v, marked.len);
            for (uint32_t k = 0u; k < marked.len; k++)
                marked.v[k] = marked.v[k] - 1;
        }
        if (true_marked >= 0) {
            int32_t g = loop_nogood_of(s, &external, (uint32_t)true_marked);
            emit_loop(s, &marked, g, (uint32_t)true_marked);
            *conflict = g;
            result = PROP_CONFLICT;
            goto done;
        }
        fresh.len = 0u;
        for (uint32_t k = 0u; k < marked.len; k++)
            if (!on_trail(s, LIT(marked.v[k], 0)))
                iv_push(&fresh, marked.v[k]);
        if (fresh.len == 0u) {
            result = PROP_CLOSED;
            goto done;
        }
        for (uint32_t k = 0u; k < fresh.len; k++) {   /* pushUnfounded */
            uint32_t a = (uint32_t)fresh.v[k];
            int32_t g = loop_nogood_of(s, &external, a);
            push_lit(s, LIT(a, 0), g);
            emit_loop(s, &marked, g, a);
        }
    }
done:
    free(sourced);
    free(in_m);
    iv_free(&marked);
    iv_free(&fresh);
    iv_free(&external);
    return result;
}

/* ---- conflict analysis (FirstUIP.lean, RUP.lean) -------------------------- */

/* resolveOn c rho m = dedup (c.erase m ++ (dedup rho).erase (compl m)) */
static bool resolve_on(IVec *c, const IVec *rho, int32_t m) {
    IVec cat = {0};
    IVec rd = {0};
    bool erased = false;
    bool ok = true;
    for (uint32_t i = 0u; ok && i < c->len; i++) {
        if (!erased && c->v[i] == m) {
            erased = true;
            continue;
        }
        ok = iv_push(&cat, c->v[i]);
    }
    ok = ok && iv_dedup(&rd, rho->v, rho->len);
    erased = false;
    for (uint32_t i = 0u; ok && i < rd.len; i++) {
        if (!erased && rd.v[i] == LIT_COMPL(m)) {
            erased = true;
            continue;
        }
        ok = iv_push(&cat, rd.v[i]);
    }
    ok = ok && iv_dedup(c, cat.v, cat.len);
    iv_free(&cat);
    iv_free(&rd);
    return ok;
}

/* whether m is on the trail at an index in [lo, hi) */
static bool on_trail_between(const Solver *s, int32_t m, uint32_t lo, uint32_t hi) {
    if (!on_trail(s, m))
        return false;
    uint32_t p = s->trail_pos[LIT_VAR(m)];
    return p >= lo && p < hi;
}

/* uipWalk and uipReasons over the current level, newest entry first */
static bool uip_walk(Solver *s, IVec *c, IVec *reasons) {
    uint32_t lo = newest_decision(s);
    for (uint32_t i = s->ntrail; i-- > lo;) {
        const Entry *e = &s->trail[i];
        if (!iv_contains(c, e->lit))
            continue;
        bool another = false;
        for (uint32_t k = 0u; k < c->len && !another; k++)
            another = c->v[k] != e->lit && on_trail_between(s, c->v[k], lo, i);
        if (!another || e->reason < 0)
            return true;
        if (!iv_push(reasons, e->reason) ||
            !resolve_on(c, &s->pool[e->reason].lits, e->lit))
            return false;
    }
    return true;
}

static uint32_t lit_level(const Solver *s, int32_t m) {
    return on_trail(s, m) ? s->trail[s->trail_pos[LIT_VAR(m)]].level : 0u;
}

/* the trail's reasons, oldest first, then the conflict (refutationHints) */
static void refutation_hints(const Solver *s, int32_t conflict, IVec *hints) {
    hints->len = 0u;
    for (uint32_t i = 0u; i < s->ntrail; i++)
        if (s->trail[i].reason >= 0)
            iv_push(hints, s->trail[i].reason);
    iv_push(hints, conflict);
}

/* The fast search's watches of a learned nogood: its asserting literal u,
 * false once the backjump asserts its complement, and the other literal of
 * the highest level, so that a nogood watched true is one watched false. */
static bool asserting_watches(const Solver *s, const IVec *lits, int32_t u,
                              int32_t *w1, int32_t *w2) {
    int32_t best = -1;
    uint32_t level = 0u;
    for (uint32_t k = 0u; k < lits->len; k++) {
        int32_t m = lits->v[k];
        if (m == u)
            continue;
        uint32_t l = lit_level(s, m);
        if (best < 0 || l > level) {
            best = m;
            level = l;
        }
    }
    if (best < 0)
        return false;
    *w1 = u;
    *w2 = best;
    return true;
}

/* After a backjump the fast search propagates only the asserted literal:
 * every level below was at a fixpoint before its next decision. */
static void requeue_asserted(Solver *s, int32_t lit) {
    s->pending.len = 0u;
    if (!iv_push(&s->pending, lit))
        s->oom = true;
}

/* DState.requeue: everything on the new trail becomes pending, newest first */
static void requeue(Solver *s) {
    s->pending.len = 0u;
    for (uint32_t i = 0u; i < s->ntrail; i++)
        if (!iv_push(&s->pending, s->trail[i].lit))
            s->oom = true;
}

typedef enum { ITER_NEXT, ITER_SAT, ITER_UNSAT, ITER_STUCK } IterResult;

/* resolveConflict */
/* A learned nogood's literals, numbered, for the observer. */
static void trace_learned(Solver *s, int32_t nogood, uint32_t level) {
    const IVec *lits = &s->pool[nogood].lits;
    int32_t inline_lits[32] = {0};
    int32_t *numbered = lits->len <= 32u ? inline_lits
        : malloc(sizeof(*numbered) * lits->len);
    if (!numbered) {
        s->oom = true;
        return;
    }
    for (uint32_t k = 0u; k < lits->len; k++)
        numbered[k] = native_lit(lits->v[k]);
    s->trace->event(s->trace_context, ASP_TRACE_LEARN, 0, level, nogood,
                    numbered, lits->len);
    if (numbered != inline_lits)
        free(numbered);
}

/* A literal x true at level 0 holds for good: the unit nogood {compl x},
 * RUP by the units of its reason's other literals (all true at level 0)
 * and then the reason.  Each is emitted once, on first use, the units it
 * rests on first; a reason that is itself that unit is used as it is.
 * Answers the unit's pool index, or -1. */
static int32_t level0_unit(Solver *s, int32_t x) {
    if (s->unit0[LIT_VAR(x)] >= 0)
        return s->unit0[LIT_VAR(x)];
    IVec stack = {0};
    IVec hints = {0};
    bool ok = iv_push(&stack, x);
    while (ok && stack.len > 0u) {
        int32_t y = stack.v[stack.len - 1u];
        uint32_t w = (uint32_t)LIT_VAR(y);
        if (s->unit0[w] >= 0) {
            stack.len--;
            continue;
        }
        int32_t r = s->trail[s->trail_pos[w]].reason;
        if (r < 0) {
            ok = false;
            break;
        }
        const IVec *rl = &s->pool[r].lits;
        bool ready = true;
        bool other = false;
        for (uint32_t k = 0u; ok && k < rl->len; k++) {
            int32_t z = rl->v[k];
            if (z == LIT_COMPL(y))
                continue;
            other = true;
            if (s->unit0[LIT_VAR(z)] < 0) {
                ready = false;
                ok = iv_push(&stack, z);
            }
        }
        if (!ok || !ready)
            continue;
        if (!other) {
            s->unit0[w] = r;
            stack.len--;
            continue;
        }
        hints.len = 0u;
        for (uint32_t k = 0u; ok && k < rl->len; k++)
            if (rl->v[k] != LIT_COMPL(y))
                ok = iv_push(&hints, s->unit0[LIT_VAR(rl->v[k])]);
        ok = ok && iv_push(&hints, r);
        int32_t lit = LIT_COMPL(y);
        int32_t g = ok ? new_nogood(s, &lit, 1u) : -1;
        if (g < 0) {
            ok = false;
            break;
        }
        emit_rup(s, &lit, 1u, &hints, g);
        s->unit0[w] = g;
        stack.len--;
    }
    iv_free(&stack);
    iv_free(&hints);
    if (!ok) {
        s->oom = true;
        return -1;
    }
    return s->unit0[LIT_VAR(x)];
}

/* The fast search's conflict analysis (MiniSat's analyze and litRedundant).
 * One walk down the trail over seen marks finds the first UIP u, resolving
 * on the same literals as uip_walk, and bumps every variable it meets above
 * level 0.  The learned nogood is u and the lower-level literals met,
 * without
 *   - literals true at level 0, through their units (level0_unit), and
 *   - literals that the rest of the nogood implies through their reasons
 *     (recursive minimization), every literal of such a derivation having
 *     a reason and a level below the current one.
 * Its hints: the units, then the derived literals' reasons in trail order,
 * then the resolved reasons oldest first, then the conflict.  Under the
 * nogood's literals each hint is then unit or conflicting in turn. */
typedef struct {
    IVec units;     /* unit nogoods of level-0 literals */
    IVec derived;   /* trail positions of literals the minimization derives */
    IVec toclear;   /* variables marked seen */
} Analysis;

static void analysis_free(Analysis *a) {
    iv_free(&a->units);
    iv_free(&a->derived);
    iv_free(&a->toclear);
}

static bool mark_seen(Solver *s, Analysis *a, uint32_t v) {
    s->seen[v] = 1u;
    return iv_push(&a->toclear, (int32_t)v);
}

static uint32_t level_bit(uint32_t level) {
    return 1u << (level & 31u);
}

/* Trail positions (nonnegative) in increasing order: insertion for a few,
 * else four byte-wise counting passes, linear in their number. */
static bool sort_positions(int32_t *v, uint32_t n) {
    if (n < 64u) {
        for (uint32_t i = 1u; i < n; i++) {
            int32_t x = v[i];
            uint32_t j = i;
            while (j > 0u && v[j - 1u] > x) {
                v[j] = v[j - 1u];
                j--;
            }
            v[j] = x;
        }
        return true;
    }
    int32_t *tmp = malloc(sizeof(*tmp) * n);
    if (!tmp)
        return false;
    int32_t *from = v, *to = tmp;
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        uint32_t count[257] = {0};
        for (uint32_t i = 0u; i < n; i++)
            count[(((uint32_t)from[i] >> shift) & 255u) + 1u]++;
        for (uint32_t b = 0u; b < 256u; b++)
            count[b + 1u] += count[b];
        for (uint32_t i = 0u; i < n; i++)
            to[count[((uint32_t)from[i] >> shift) & 255u]++] = from[i];
        int32_t *t = from;
        from = to;
        to = t;
    }
    /* four passes: the result is back in v */
    free(tmp);
    return true;
}

/* whether literal x of the nogood follows from the others, its reason's
 * literals being in the nogood, true at level 0, or so derived in turn.  A
 * variable an exploration failed through is poisoned (seen 2) for the rest
 * of the analysis, so no implication is explored twice; poison only keeps a
 * literal in the nogood. */
static bool lit_redundant(Solver *s, Analysis *a, int32_t x, uint32_t lo,
                          uint32_t levels, IVec *stack, bool *ok) {
    uint32_t clear_from = a->toclear.len;
    uint32_t units_from = a->units.len;
    uint32_t derived_from = a->derived.len;
    stack->len = 0u;
    *ok = iv_push(stack, x);
    while (*ok && stack->len > 0u) {
        int32_t y = stack->v[--stack->len];
        int32_t r = s->trail[s->trail_pos[LIT_VAR(y)]].reason;
        for (uint32_t k = 0u; *ok && k < s->pool[r].lits.len; k++) {
            int32_t z = s->pool[r].lits.v[k];
            uint32_t v = (uint32_t)LIT_VAR(z);
            if (z == LIT_COMPL(y) || s->seen[v] == 1u)
                continue;
            const Entry *e = &s->trail[s->trail_pos[v]];
            bool poisoned = s->seen[v] == 2u;
            if (e->level == 0u) {
                int32_t g = level0_unit(s, z);
                *ok = g >= 0 && iv_push(&a->units, g) && mark_seen(s, a, v);
                continue;
            }
            if (poisoned || e->reason < 0 || s->trail_pos[v] >= lo ||
                !(level_bit(e->level) & levels)) {
                /* what this exploration marked is poisoned, but the level-0
                 * units, which hold whatever comes; the failing variable too */
                for (uint32_t i = clear_from; i < a->toclear.len; i++) {
                    uint32_t u = (uint32_t)a->toclear.v[i];
                    s->seen[u] = s->trail[s->trail_pos[u]].level ? 2u : 0u;
                }
                if (!poisoned && (*ok = mark_seen(s, a, v)))
                    s->seen[v] = 2u;
                a->units.len = units_from;
                a->derived.len = derived_from;
                return false;
            }
            *ok = mark_seen(s, a, v) &&
                iv_push(&a->derived, (int32_t)s->trail_pos[v]) &&
                iv_push(stack, z);
        }
    }
    return *ok;
}

static int cmp_i32(const void *a, const void *b);

static bool fast_analyze(Solver *s, int32_t conflict, uint32_t lo, IVec *c,
                         IVec *hints, int32_t *uip) {
    Analysis a = {0};
    IVec reasons = {0};
    IVec stack = {0};
    bool ok = iv_push(c, -1);              /* u's place */
    uint32_t path = 0u;
    uint32_t index = s->ntrail;
    int32_t pivot = -1;
    int32_t g = conflict;
    bool structural = false;
    while (ok) {
        /* level0_unit may grow the pool: its nogoods are read by index */
        for (uint32_t k = 0u; ok && k < s->pool[g].lits.len; k++) {
            int32_t z = s->pool[g].lits.v[k];
            uint32_t v = (uint32_t)LIT_VAR(z);
            if ((pivot >= 0 && z == LIT_COMPL(pivot)) || s->seen[v])
                continue;
            ok = mark_seen(s, &a, v);
            uint32_t pos = s->trail_pos[v];
            if (s->trail[pos].level == 0u) {
                int32_t unit = level0_unit(s, z);
                ok = ok && unit >= 0 && iv_push(&a.units, unit);
                continue;
            }
            bump_variable(s, v);
            if (pos >= lo)
                path++;
            else
                ok = ok && iv_push(c, z);
        }
        if (!ok || path == 0u)
            break;
        do
            index--;
        while (!s->seen[LIT_VAR(s->trail[index].lit)]);
        pivot = s->trail[index].lit;
        if (--path == 0u)
            break;
        g = s->trail[index].reason;
        if (g < 0) {
            structural = true;
            ok = false;
        } else {
            ok = iv_push(&reasons, g);
        }
    }
    if (ok && path == 0u && pivot >= 0) {
        c->v[0] = pivot;
        uint32_t levels = 0u;
        for (uint32_t k = 1u; k < c->len; k++)
            levels |= level_bit(s->trail[s->trail_pos[LIT_VAR(c->v[k])]].level);
        uint32_t kept = 1u;
        for (uint32_t k = 1u; ok && k < c->len; k++) {
            int32_t x = c->v[k];
            uint32_t pos = s->trail_pos[LIT_VAR(x)];
            if (s->trail[pos].reason >= 0 &&
                lit_redundant(s, &a, x, lo, levels, &stack, &ok))
                ok = iv_push(&a.derived, (int32_t)pos);
            else
                c->v[kept++] = x;
        }
        c->len = kept;
        if (a.derived.len > 1u && !sort_positions(a.derived.v, a.derived.len))
            ok = false;
        for (uint32_t k = 0u; ok && k < a.units.len; k++)
            ok = iv_push(hints, a.units.v[k]);
        for (uint32_t k = 0u; ok && k < a.derived.len; k++)
            ok = iv_push(hints, s->trail[a.derived.v[k]].reason);
        for (uint32_t k = reasons.len; ok && k-- > 0u;)
            ok = iv_push(hints, reasons.v[k]);
        ok = ok && iv_push(hints, conflict);
        *uip = pivot;
    } else if (ok) {
        structural = true;
        ok = false;
    }
    if (!ok && !structural)
        s->oom = true;
    for (uint32_t k = 0u; k < a.toclear.len; k++)
        s->seen[a.toclear.v[k]] = 0u;
    analysis_free(&a);
    iv_free(&reasons);
    iv_free(&stack);
    return ok;
}

/* firstUIP over the reference search: the conflict's literals resolved by
 * uip_walk; u its literal of the current level; the hints the resolved
 * reasons, oldest first, then the conflict (firstUIPHints). */
static bool reference_analyze(Solver *s, const IVec *d, uint32_t lo,
                              int32_t conflict, IVec *c, IVec *hints,
                              int32_t *uip) {
    IVec reasons = {0};
    bool ok = iv_dedup(c, d->v, d->len) && uip_walk(s, c, &reasons);
    if (!ok)
        s->oom = true;
    int32_t u = -1;
    for (uint32_t k = 0u; ok && k < c->len && u < 0; k++)
        if (on_trail_between(s, c->v[k], lo, s->ntrail))
            u = c->v[k];
    ok = ok && u >= 0;
    for (uint32_t k = reasons.len; ok && k-- > 0u;)
        ok = iv_push(hints, reasons.v[k]);
    ok = ok && iv_push(hints, conflict);
    iv_free(&reasons);
    *uip = u;
    return ok;
}

static IterResult resolve_conflict(Solver *s, int32_t conflict) {
    IVec hints = {0};
    IterResult result = ITER_NEXT;
    if (s->trace)
        s->trace->event(s->trace_context, ASP_TRACE_CONFLICT, 0, s->level,
                        conflict, NULL, 0u);
    if (s->level == 0u) {
        /* a derived empty nogood (a Hall step with no atom left open) is
         * the refutation already */
        if (s->pool[conflict].lits.len > 0u || !derived_step(s, conflict)) {
            refutation_hints(s, conflict, &hints);
            emit_rup(s, NULL, 0u, &hints, -1);
        }
        iv_free(&hints);
        return ITER_UNSAT;
    }
    uint32_t lo = newest_decision(s);
    const IVec *d = &s->pool[conflict].lits;
    bool current = false;
    for (uint32_t k = 0u; k < d->len && !current; k++)
        current = on_trail_between(s, d->v[k], lo, s->ntrail);
    if (current) {
        IVec c = {0};
        int32_t u = -1;
        if (!(s->fast ? fast_analyze(s, conflict, lo, &c, &hints, &u)
                      : reference_analyze(s, d, lo, conflict, &c, &hints, &u))) {
            result = ITER_STUCK;
            goto learned_done;
        }
        uint32_t j = 0u;
        for (uint32_t k = 0u; k < c.len; k++)
            if (c.v[k] != u && lit_level(s, c.v[k]) > j)
                j = lit_level(s, c.v[k]);
        if (s->fast) {
            s->bump /= 0.95;
            s->conflicts++;
        }
        int32_t learned = new_nogood(s, c.v, c.len);
        if (learned < 0) {
            result = ITER_STUCK;
            goto learned_done;
        }
        /* DState.learn */
        int32_t w1, w2;
        if (s->fast ? asserting_watches(s, &s->pool[learned].lits, u, &w1, &w2)
                    : two_watches(&s->pool[learned].lits, &w1, &w2)) {
            add_watched(s, learned, w1, w2);
            if (s->fast && !s->oom)
                s->fc[s->nfc - 1u].lbd = nogood_levels(s, c.v, c.len);
        } else if (!iv_push(&s->shorts, learned)) {
            s->oom = true;
        }
        /* cutTo j, then the asserted literal at level j */
        uint32_t keep = s->ntrail;
        while (keep > 0u && s->trail[keep - 1u].level > j)
            keep--;
        emit_rup(s, c.v, c.len, &hints, learned);
        if (s->trace)
            trace_learned(s, learned, j);
        truncate_trail(s, keep);
        push_entry(s, LIT_COMPL(u), j, learned);
        if (s->fast)
            requeue_asserted(s, LIT_COMPL(u));
        else
            requeue(s);
learned_done:
        iv_free(&c);
        iv_free(&hints);
        return result;
    }
    /* a stale conflict: the decisions nogood */
    IVec dl = {0};
    for (uint32_t i = s->ntrail; i-- > 0u;)
        if (s->trail[i].reason < 0)
            iv_push(&dl, s->trail[i].lit);
    refutation_hints(s, conflict, &hints);
    int32_t reason = new_nogood(s, dl.v, dl.len);
    emit_rup(s, dl.v, dl.len, &hints, reason);
    int32_t decision = s->trail[lo].lit;
    truncate_trail(s, lo);
    if (s->trace && reason >= 0)
        trace_learned(s, reason, s->level);
    push_entry(s, LIT_COMPL(decision), s->level, reason);
    if (s->fast)
        requeue_asserted(s, LIT_COMPL(decision));
    else
        requeue(s);
    iv_free(&dl);
    iv_free(&hints);
    return reason < 0 ? ITER_STUCK : ITER_NEXT;
}

/* ---- Hall's condition over the program's constraints --------------------
 *
 * A falsum atom heads rules, each of which has its negation in the body:
 * no stable model holds it.  So a rule  h :- not a1, ..., not ak, not h  with
 * h such an atom says that some ai holds (an at-least-one group), and a rule
 * h :- a, b, not h  that a and b do not both hold (an exclusion).  Cliques of
 * exclusions are at-most-one sets.  When a set S of groups has its open atoms
 * inside fewer than |S| cliques, no stable model exists under the false atoms
 * that keep them there: a Hall step, whose clause is those atoms. */

/* The exclusion of a rule: a, b with  h :- a, b, not h,  h a falsum atom. */
static bool exclusion_of(const Solver *s, uint32_t r, uint32_t *a, uint32_t *b) {
    const AspRule *rule = &s->rules[r];
    if (!falsum_rule(s, r) || rule->pos_len != 2u || rule->neg_len != 1u)
        return false;
    *a = (uint32_t)atom_lookup(s, rule->pos[0]);
    *b = (uint32_t)atom_lookup(s, rule->pos[1]);
    return *a != *b;
}

/* A set of unordered atom pairs, by open addressing. */
typedef struct {
    uint64_t *keys;   /* 0 empty, else ((min+1) << 32) | (max+1) */
    uint32_t mask;
} PairSet;

static uint64_t pair_key(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
    return ((uint64_t)(lo + 1u) << 32) | (uint64_t)(hi + 1u);
}

static uint32_t pair_slot(const PairSet *p, uint64_t k) {
    uint64_t x = k * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(x >> 32) & p->mask;
}

static bool pairs_init(PairSet *p, uint32_t n) {
    uint32_t size = 64u;
    while (size < 2u * n + 2u)
        size *= 2u;
    p->mask = size - 1u;
    p->keys = calloc(size, sizeof(*p->keys));
    return p->keys != NULL;
}

static void pairs_add(PairSet *p, uint32_t a, uint32_t b) {
    uint64_t k = pair_key(a, b);
    uint32_t i = pair_slot(p, k);
    while (p->keys[i] && p->keys[i] != k)
        i = (i + 1u) & p->mask;
    p->keys[i] = k;
}

static bool pairs_has(const PairSet *p, uint32_t a, uint32_t b) {
    uint64_t k = pair_key(a, b);
    uint32_t i = pair_slot(p, k);
    while (p->keys[i]) {
        if (p->keys[i] == k)
            return true;
        i = (i + 1u) & p->mask;
    }
    return false;
}

/* Every exclusion of the program. */
static bool program_exclusions(const Solver *s, PairSet *p) {
    uint32_t n = 0u, a, b;
    for (uint32_t r = 0u; r < s->nrules; r++)
        n += exclusion_of(s, r, &a, &b);
    if (!pairs_init(p, n))
        return false;
    for (uint32_t r = 0u; r < s->nrules; r++)
        if (exclusion_of(s, r, &a, &b))
            pairs_add(p, a, b);
    return true;
}

/* The group atoms of an at-least-one rule: its negative atoms but its head,
 * once each; false when the rule is not one. */
static bool group_of_rule(const Solver *s, uint32_t r, IVec *atoms) {
    const AspRule *rule = &s->rules[r];
    uint32_t h = s->rule_head[r];
    atoms->len = 0u;
    if (!s->falsum[h] || rule->pos_len != 0u)
        return false;
    for (uint32_t k = 0u; k < rule->neg_len; k++) {
        int32_t a = atom_lookup(s, rule->neg[k]);
        if ((uint32_t)a != h && !iv_contains(atoms, a) && !iv_push(atoms, a))
            return false;
    }
    return atoms->len > 0u;
}

static const uint32_t *g_degree_of;
/* more exclusions first; ties by atom number */
static int by_degree(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    uint32_t da = g_degree_of[a + 1u] - g_degree_of[a];
    uint32_t db = g_degree_of[b + 1u] - g_degree_of[b];
    if (da != db)
        return da > db ? -1 : 1;
    return (a > b) - (a < b);
}

static bool hall_setup(Solver *s) {
    uint32_t *group = malloc(sizeof(*group) * (s->natoms ? s->natoms : 1u));
    uint32_t *clique = malloc(sizeof(*clique) * (s->natoms ? s->natoms : 1u));
    s->hall_group_rule = malloc(sizeof(uint32_t) * (s->nrules ? s->nrules : 1u));
    PairSet pairs = {0};
    IVec atoms = {0}, cand = {0}, members = {0}, next = {0}, used = {0};
    uint32_t *adj_start = NULL, *adj = NULL, *fill = NULL;
    bool ok = group && clique && s->hall_group_rule &&
              program_exclusions(s, &pairs);
    uint32_t ngroups = 0u, ncliques = 0u;
    for (uint32_t a = 0u; ok && a < s->natoms; a++)
        group[a] = clique[a] = ASP_HALL_NONE;
    /* groups: at-least-one rules whose atoms no earlier group has */
    for (uint32_t r = 0u; ok && r < s->nrules; r++) {
        if (!group_of_rule(s, r, &atoms))
            continue;
        bool fresh = true;
        for (uint32_t k = 0u; k < atoms.len && fresh; k++)
            fresh = group[atoms.v[k]] == ASP_HALL_NONE;
        if (!fresh)
            continue;
        for (uint32_t k = 0u; k < atoms.len; k++)
            group[atoms.v[k]] = ngroups;
        s->hall_group_rule[ngroups++] = r;
    }
    /* exclusions between atoms of different groups, as adjacency lists */
    adj_start = calloc((size_t)s->natoms + 1u, sizeof(*adj_start));
    ok = ok && adj_start;
    uint32_t a0, b0, nadj = 0u;
    for (uint32_t r = 0u; ok && ngroups && r < s->nrules; r++)
        if (exclusion_of(s, r, &a0, &b0) && group[a0] != ASP_HALL_NONE &&
            group[b0] != ASP_HALL_NONE && group[a0] != group[b0]) {
            adj_start[a0 + 1u]++;
            adj_start[b0 + 1u]++;
            nadj += 2u;
        }
    for (uint32_t a = 0u; ok && a < s->natoms; a++)
        adj_start[a + 1u] += adj_start[a];
    adj = malloc(sizeof(*adj) * (nadj ? nadj : 1u));
    fill = malloc(sizeof(*fill) * (s->natoms ? s->natoms : 1u));
    ok = ok && adj && fill;
    if (ok)
        memcpy(fill, adj_start, sizeof(*fill) * s->natoms);
    for (uint32_t r = 0u; ok && ngroups && r < s->nrules; r++)
        if (exclusion_of(s, r, &a0, &b0) && group[a0] != ASP_HALL_NONE &&
            group[b0] != ASP_HALL_NONE && group[a0] != group[b0]) {
            adj[fill[a0]++] = b0;
            adj[fill[b0]++] = a0;
        }
    /* cliques, greedily: from each atom no clique has yet, add the
     * candidate with most neighbours among the candidates, the candidates
     * being uncovered neighbours of every member, from groups not yet in */
    uint32_t nclique_atoms = 0u;
    s->hall_clique_start = malloc(sizeof(uint32_t) * ((size_t)s->natoms + 1u));
    s->hall_clique_atoms = malloc(sizeof(uint32_t) * (s->natoms ? s->natoms : 1u));
    ok = ok && s->hall_clique_start && s->hall_clique_atoms;
    /* start cliques from the atoms with most exclusions first, so that a
     * large clique is not split by a small one grown before it */
    uint32_t *order = malloc(sizeof(*order) * (s->natoms ? s->natoms : 1u));
    ok = ok && order;
    for (uint32_t a = 0u; ok && a < s->natoms; a++)
        order[a] = a;
    if (ok) {
        g_degree_of = adj_start;
        qsort(order, s->natoms, sizeof(*order), by_degree);
    }
    for (uint32_t oi = 0u; ok && oi < s->natoms; oi++) {
        uint32_t a = order[oi];
        if (group[a] == ASP_HALL_NONE || clique[a] != ASP_HALL_NONE)
            continue;
        cand.len = members.len = used.len = 0u;
        iv_push(&members, (int32_t)a);
        iv_push(&used, (int32_t)group[a]);
        for (uint32_t k = adj_start[a]; k < adj_start[a + 1u]; k++)
            if (clique[adj[k]] == ASP_HALL_NONE && !iv_contains(&cand, (int32_t)adj[k]))
                iv_push(&cand, (int32_t)adj[k]);
        while (cand.len) {
            uint32_t best = 0u, best_deg = 0u;
            if (cand.len <= 256u) {
                for (uint32_t i = 0u; i < cand.len; i++) {
                    uint32_t deg = 0u;
                    for (uint32_t j = 0u; j < cand.len; j++)
                        deg += i != j && pairs_has(&pairs, (uint32_t)cand.v[i],
                                                   (uint32_t)cand.v[j]);
                    if (i == 0u || deg > best_deg) {
                        best = i;
                        best_deg = deg;
                    }
                }
            }
            uint32_t b = (uint32_t)cand.v[best];
            iv_push(&members, (int32_t)b);
            iv_push(&used, (int32_t)group[b]);
            next.len = 0u;
            for (uint32_t i = 0u; i < cand.len; i++) {
                uint32_t x = (uint32_t)cand.v[i];
                if (x != b && pairs_has(&pairs, x, b) &&
                    !iv_contains(&used, (int32_t)group[x]))
                    iv_push(&next, (int32_t)x);
            }
            IVec t = cand;
            cand = next;
            next = t;
        }
        if (members.len < 2u)
            continue;
        s->hall_clique_start[ncliques] = nclique_atoms;
        for (uint32_t k = 0u; k < members.len; k++) {
            clique[members.v[k]] = ncliques;
            s->hall_clique_atoms[nclique_atoms++] = (uint32_t)members.v[k];
        }
        ncliques++;
    }
    if (ok)
        s->hall_clique_start[ncliques] = nclique_atoms;
    if (ok && ngroups && ncliques) {
        AspHallShape shape = {s->natoms, group, ngroups, clique, ncliques};
        s->hall = asp_hall_new(&shape);
        ok = s->hall != NULL;
    }
    uint32_t group_atoms = 0u;
    for (uint32_t a = 0u; ok && a < s->natoms; a++)
        group_atoms += group[a] != ASP_HALL_NONE;
    s->hall_conflicts = 0u;
    if (ok) {
        g_hall_stats = (AspHallStats){0};
        g_hall_stats.groups = s->hall ? ngroups : 0u;
        g_hall_stats.cliques = s->hall ? ncliques : 0u;
        g_hall_stats.clique_atoms = s->hall ? nclique_atoms : 0u;
        g_hall_stats.group_atoms = s->hall ? group_atoms : 0u;
    }
    free(order);
    free(group);
    free(clique);
    free(pairs.keys);
    free(adj_start);
    free(adj);
    free(fill);
    iv_free(&atoms);
    iv_free(&cand);
    iv_free(&members);
    iv_free(&next);
    iv_free(&used);
    if (!ok)
        s->oom = true;
    return ok;
}

/* The Hall step for a violator: its clause, its groups' rules, its cliques. */
static void emit_hall(Solver *s, int32_t nogood, const AspHallWhy *why) {
    AspStep *st = new_step(s);
    if (!st)
        return;
    const Nogood *g = &s->pool[nogood];
    st->kind = ASP_STEP_HALL;
    st->id = s->next_id;
    if (!lrat_clause(g->lits.v, g->lits.len, &st->clause, &st->clause_len)) {
        s->oom = true;
        return;
    }
    uint32_t n = 0u;
    for (uint32_t k = 0u; k < why->ncliques; k++) {
        uint32_t c = why->cliques[k];
        n += s->hall_clique_start[c + 1u] - s->hall_clique_start[c] + 1u;
    }
    st->groups = malloc(sizeof(*st->groups) * (why->ngroups ? why->ngroups : 1u));
    st->cliques = malloc(sizeof(*st->cliques) * (n ? n : 1u));
    if (!st->groups || !st->cliques) {
        s->oom = true;
        return;
    }
    for (uint32_t k = 0u; k < why->ngroups; k++)
        st->groups[k] = s->hall_group_rule[why->groups[k]];
    st->groups_len = why->ngroups;
    for (uint32_t k = 0u; k < why->ncliques; k++) {
        uint32_t c = why->cliques[k];
        for (uint32_t j = s->hall_clique_start[c]; j < s->hall_clique_start[c + 1u]; j++)
            st->cliques[st->cliques_len++] = s->hall_clique_atoms[j];
        st->cliques[st->cliques_len++] = UINT32_MAX;
    }
    name_step(s, g->lits.v, g->lits.len, nogood);
}

/* The violator's nogood: its excluded atoms false.  Every literal is on the
 * trail, so it conflicts; it is watched on its two newest literals. */
static int32_t hall_nogood(Solver *s, const AspHallWhy *why) {
    IVec lits = {0};
    for (uint32_t k = 0u; k < why->nitems; k++)
        if (!iv_push(&lits, LIT(why->items[k], 0))) {
            s->oom = true;
            iv_free(&lits);
            return -1;
        }
    int32_t g = new_nogood(s, lits.v, lits.len);
    iv_free(&lits);
    if (g < 0)
        return -1;
    emit_hall(s, g, why);
    const IVec *l = &s->pool[g].lits;
    if (l->len >= 2u) {
        uint32_t i1 = 0u, i2 = 1u;
        if (s->trail_pos[LIT_VAR(l->v[i2])] > s->trail_pos[LIT_VAR(l->v[i1])]) {
            i1 = 1u;
            i2 = 0u;
        }
        for (uint32_t k = 2u; k < l->len; k++) {
            uint32_t p = s->trail_pos[LIT_VAR(l->v[k])];
            if (p > s->trail_pos[LIT_VAR(l->v[i1])]) {
                i2 = i1;
                i1 = k;
            } else if (p > s->trail_pos[LIT_VAR(l->v[i2])]) {
                i2 = k;
            }
        }
        add_watched(s, g, l->v[i1], l->v[i2]);
    } else if (!iv_push(&s->shorts, g)) {
        s->oom = true;
    }
    return s->oom ? -1 : g;
}

static void hall_record_stats(const Solver *s) {
    if (s->hall) {
        AspHallMatchStats h = asp_hall_stats(s->hall);
        g_hall_stats.hall_checks = h.checks;
        g_hall_stats.hall_searches = h.searches;
    }
    g_hall_stats.hall_conflicts = s->hall_conflicts;
    g_hall_stats.conflicts = s->conflicts;
    g_hall_stats.decisions = s->decisions;
}

AspHallStats asp_hall_last_stats(void) {
    return g_hall_stats;
}

/* A Hall step is accepted when its groups are at-least-one rules with
 * pairwise disjoint atoms, its cliques pairwise disjoint sets of atoms any
 * two of which an exclusion rule forbids together, fewer cliques than
 * groups, and every atom of its groups is in its clause or in one of its
 * cliques.  Then in a stable model with the clause false each group has a
 * true atom in some clique, no two in the same one: impossible. */
static bool hall_step_ok(Solver *s, const AspStep *st) {
    uint8_t *mark = calloc(s->natoms ? s->natoms : 1u, 1u);  /* 1 clause, 2 clique, 4 group */
    PairSet pairs = {0};
    IVec atoms = {0};
    bool ok = mark && program_exclusions(s, &pairs);
    for (uint32_t k = 0u; ok && k < st->clause_len; k++) {
        int32_t l = st->clause[k];
        ok = l > 0 && (uint32_t)l <= s->natoms;
        if (ok)
            mark[l - 1] |= 1u;
    }
    uint32_t ncliques = 0u, start = 0u;
    for (uint32_t k = 0u; ok && k < st->cliques_len; k++) {
        uint32_t a = st->cliques[k];
        if (a == UINT32_MAX) {
            ok = k > start;
            ncliques++;
            start = k + 1u;
            continue;
        }
        ok = a < s->natoms && !(mark[a] & 2u);
        for (uint32_t j = start; ok && j < k; j++)
            ok = pairs_has(&pairs, st->cliques[j], a);
        if (ok)
            mark[a] |= 2u;
    }
    ok = ok && start == st->cliques_len && ncliques < st->groups_len;
    for (uint32_t k = 0u; ok && k < st->groups_len; k++) {
        ok = st->groups[k] < s->nrules && group_of_rule(s, st->groups[k], &atoms);
        for (uint32_t j = 0u; ok && j < atoms.len; j++) {
            uint32_t a = (uint32_t)atoms.v[j];
            ok = !(mark[a] & 4u) && (mark[a] & 3u);
            mark[a] |= 4u;
        }
    }
    free(mark);
    free(pairs.keys);
    iv_free(&atoms);
    return ok;
}

/* iterate */
static IterResult iterate(Solver *s) {
    int32_t conflict = -1;
    PropResult p = propagate_all(s, &conflict);
    if (s->oom || p == PROP_FUEL)
        return ITER_STUCK;
    if (p == PROP_CONFLICT)
        return resolve_conflict(s, conflict);
    if (s->hall) {
        AspHallWhy why;
        if (!asp_hall_feasible(s->hall, &why)) {
            int32_t g = hall_nogood(s, &why);
            if (g < 0)
                return ITER_STUCK;
            s->hall_conflicts++;
            return resolve_conflict(s, g);
        }
    }
    if (s->forced_plus1) {
        int32_t x = s->forced_plus1 - 1;
        s->forced_plus1 = 0;
        if (s->val[LIT_VAR(x)] < 0) {
            s->decisions++;
            push_entry(s, x, s->level + 1u, -1);
            s->pending.len = 0u;
            if (!iv_push(&s->pending, x))
                s->oom = true;
            return ITER_NEXT;
        }
    }
    if (s->fast) {
        if (s->level == 0u)
            fast_simplify(s);
        /* The fast search decides atoms only: once every atom has a value,
         * the completion's nogoods give every body its value. */
        while (s->heap_len > 0u) {
            uint32_t v = heap_pop(s);
            if (s->val[v] >= 0)
                continue;
            int32_t x = LIT(v, s->phase[v]);
            s->decisions++;
            push_entry(s, x, s->level + 1u, -1);
            s->pending.len = 0u;
            if (!iv_push(&s->pending, x))
                s->oom = true;
            return ITER_NEXT;
        }
    }
    for (uint32_t v = 0u; v < s->nvars; v++) {
        if (s->val[v] < 0) {
            int32_t x = LIT(v, 0);
            push_entry(s, x, s->level + 1u, -1);
            s->pending.len = 0u;
            if (!iv_push(&s->pending, x))
                s->oom = true;
            return ITER_NEXT;
        }
    }
    return ITER_SAT;
}

/* ---- the driver (Driver.lean, Program.solve) -------------------------------- */

static void solver_free(Solver *s) {
    euf_free(s->cc);
    free(s->cc_term);
    free(s->var_eq);
    iv_free(&s->euf_marks);
    iv_free(&s->euf_false);
    free(s->lit_stamp);
    lra_free(s->lra);
    free(s->var_lra);
    free(s->lra_col);
    iv_free(&s->lra_marks);
    free(s->def_var);
    free(s->var_def);
    free(s->dl_start);
    free(s->dl_lit);
    free(s->dl_w);
    free(s->dl_def);
    free(s->d_total);
    free(s->d_true);
    free(s->d_false);
    free(s->vo_start);
    free(s->vo_lit);
    iv_free(&s->wdirty);
    free(s->in_wdirty);
    asp_hall_free(s->hall);
    free(s->hall_group_rule);
    free(s->hall_clique_start);
    free(s->hall_clique_atoms);
    free(s->activity);
    free(s->phase);
    free(s->heap);
    free(s->heap_pos);
    free(s->unit0);
    free(s->seen);
    for (uint32_t i = 0u; i < s->nfc; i++)
        free(s->fc[i].lits);
    free(s->fc);
    free(s->level_stamp);
    if (s->fw)
        for (uint32_t i = 0u; i < 2u * s->nvars; i++)
            free(s->fw[i].v);
    free(s->fw);
    free(s->atom_ids);
    free(s->atom_key);
    free(s->atom_var);
    free(s->rule_body);
    free(s->rule_unit);
    free(s->unit_start);
    free(s->unit_rules);
    free(s->falsum);
    for (uint32_t i = 0u; i < s->npool; i++)
        iv_free(&s->pool[i].lits);
    free(s->pool);
    free(s->db);
    if (s->watches)
        for (uint32_t i = 0u; i < 2u * s->nvars; i++)
            iv_free(&s->watches[i]);
    free(s->watches);
    free(s->trail);
    free(s->val);
    free(s->trail_pos);
    iv_free(&s->pending);
    iv_free(&s->shorts);
    free(s->rule_head);
    free(s->occ_start);
    free(s->head_start);
    free(s->head_rules);
    support_free(s->support);
    free(s->body_start);
    free(s->body_rules);
    free(s->occ_rules);
    free(s->need);
    free(s->queue);
    for (uint32_t i = 0u; i < s->nnames; i++)
        free(s->names[i].lits);
    free(s->names);
    free(s->name_bucket);
    free(s->comp_bucket);
    free(s->comp_next);
}

AspAnswer asp_cdnl_solve(const AspRule *rules, uint32_t rule_count) {
    return asp_cdnl_solve_search(rules, rule_count, ASP_SEARCH_FAST, NULL, NULL);
}

AspAnswer asp_cdnl_solve_traced(const AspRule *rules, uint32_t rule_count,
                                const AspTraceSink *trace, void *context) {
    return asp_cdnl_solve_search(rules, rule_count, ASP_SEARCH_FAST, trace, context);
}

/* ---- a solve as a session of steps ---------------------------------------
 * The solve is set up once, then advanced one iteration at a time (a step
 * propagates to a fixpoint, then analyses a conflict and backjumps, or
 * decides), then finished: asp_cdnl_solve_search is exactly these three. */

/* initState and the search's own setup; false when an allocation fails. */
static bool kernel_setup(Solver *s, AspSearch search, const AspTraceSink *trace,
                         void *context);

static bool solver_setup(Solver *s, AspSearch search, const AspTraceSink *trace,
                         void *context) {
    if (!number_variables(s) || (s->ndefs && !weights_setup(s)) ||
        !completion(s))
        return false;
    s->tight = program_tight(s);
    return kernel_setup(s, search, trace, context);
}

/* The kernel over the inputs in the pool: watches, the trail, the fast
 * search's state, the inputs watched or scanned and named 1, 2, ... */
static bool kernel_setup(Solver *s, AspSearch search, const AspTraceSink *trace,
                         void *context) {
    for (uint32_t a = 0u; trace && a < s->natoms; a++)
        trace->event(context, ASP_TRACE_ATOM, (int32_t)a + 1, (uint32_t)s->atom_ids[a],
                     -1, NULL, 0u);
    uint32_t nlits = 2u * s->nvars;
    s->watches = calloc(nlits ? nlits : 1u, sizeof(*s->watches));
    s->trail = malloc(sizeof(*s->trail) * (s->nvars ? s->nvars : 1u));
    s->val = malloc(s->nvars ? s->nvars : 1u);
    s->trail_pos = calloc(s->nvars ? s->nvars : 1u, sizeof(*s->trail_pos));
    if (!s->watches || !s->trail || !s->val || !s->trail_pos)
        return false;
    memset(s->val, -1, s->nvars ? s->nvars : 1u);
    if (s->fast) {
        s->activity = calloc(s->nvars ? s->nvars : 1u, sizeof(*s->activity));
        s->phase = calloc(s->nvars ? s->nvars : 1u, 1u);
        s->heap = malloc(sizeof(*s->heap) * (s->nvars ? s->nvars : 1u));
        s->heap_pos = malloc(sizeof(*s->heap_pos) * (s->nvars ? s->nvars : 1u));
        s->unit0 = malloc(sizeof(*s->unit0) * (s->nvars ? s->nvars : 1u));
        s->seen = calloc(s->nvars ? s->nvars : 1u, 1u);
        s->fw = calloc(nlits ? nlits : 1u, sizeof(*s->fw));
        s->level_stamp = calloc(s->nvars + 1u, sizeof(*s->level_stamp));
        if (!s->activity || !s->phase || !s->heap || !s->heap_pos || !s->unit0 ||
            !s->seen || !s->fw || !s->level_stamp)
            return false;
        s->reduce_at = FAST_REDUCE_FIRST;
        for (uint32_t v = 0u; v < s->nvars; v++)
            s->unit0[v] = -1;
        s->bump = 1.0;
        s->restart_at = FAST_RESTART_UNIT * luby(0u);
        for (uint32_t v = 0u; v < s->nvars; v++)
            s->heap_pos[v] = -1;
        for (uint32_t v = 0u; v < s->natoms; v++) {
            s->heap[v] = v;
            s->heap_pos[v] = (int32_t)v;
        }
        s->heap_len = s->natoms;
    }
    /* initState: the completion watched or scanned, named 1, 2, ... */
    for (uint32_t g = 0u; g < s->ncompletion && !s->oom; g++) {
        int32_t w1, w2;
        if (two_watches(&s->pool[g].lits, &w1, &w2) && s->fast) {
            add_watched(s, (int32_t)g, w1, w2);
        } else if (two_watches(&s->pool[g].lits, &w1, &w2)) {
            s->db = grow(s->db, &s->capdb, s->ndb + 1u, sizeof(*s->db), &s->oom);
            if (s->oom)
                break;
            uint32_t i = s->ndb++;
            s->db[i] = (Watched){(int32_t)g, w1, w2};
            if (!iv_push(&s->watches[w1], (int32_t)i) || !iv_push(&s->watches[w2], (int32_t)i))
                s->oom = true;
        } else if (!iv_push(&s->shorts, (int32_t)g)) {
            s->oom = true;
        }
    }
    for (uint32_t l = 0u; l < nlits; l++) {   /* stored reversed */
        IVec *w = &s->watches[l];
        for (uint32_t a = 0u, b = w->len; b > a + 1u; a++, b--) {
            int32_t tmp = w->v[a];
            w->v[a] = w->v[b - 1u];
            w->v[b - 1u] = tmp;
        }
    }
    s->next_id = s->ncompletion + 1u;
    if (!s->fast && !name_tables(s))
        return false;
    if (search == ASP_SEARCH_FAST_HALL && !hall_setup(s))
        return false;
    return true;
}

/* One iteration, then a restart where the fast search's schedule says. */
static IterResult solver_step(Solver *s) {
    if (s->oom)
        return ITER_STUCK;
    IterResult r = iterate(s);
    if (r == ITER_NEXT && s->fast && s->conflicts >= s->reduce_at) {
        fast_reduce(s);
        s->reduce_at = s->conflicts + FAST_REDUCE_FIRST +
            FAST_REDUCE_STEP * ++s->reductions;
    }
    /* a restart: back to level 0, which stays at its fixpoint */
    if (r == ITER_NEXT && s->fast && s->conflicts >= s->restart_at) {
        uint32_t keep = 0u;
        while (keep < s->ntrail && s->trail[keep].level == 0u)
            keep++;
        truncate_trail(s, keep);
        /* a literal the conflict asserted at level 0 is still to propagate */
        uint32_t still = 0u;
        for (uint32_t k = 0u; k < s->pending.len; k++)
            if (on_trail(s, s->pending.v[k]))
                s->pending.v[still++] = s->pending.v[k];
        s->pending.len = still;
        s->restart_at = s->conflicts + FAST_RESTART_UNIT * luby(++s->restarts);
    }
    return r;
}

static void step_weights_free(AspStep *st) {
    for (uint32_t k = 0u; k < st->weights_len; k++)
        free(st->weights[k]);
    free(st->weights);
}

static void steps_free(AspStep *steps, uint32_t n) {
    for (uint32_t i = 0u; i < n; i++) {
        free(steps[i].clause);
        free(steps[i].hints);
        free(steps[i].atoms);
        free(steps[i].groups);
        free(steps[i].cliques);
        step_weights_free(&steps[i]);
    }
    free(steps);
}

/* The answer of a finished search; the solver is freed.  The fast search's
 * own answers are checked: a model by the stable-model check, a refutation by
 * the certificate checker. */
static AspAnswer solver_finish(Solver *s, AspSearch search, IterResult r) {
    AspAnswer answer = {.kind = ASP_ANSWER_ERROR};
    if (r == ITER_SAT && !s->oom) {
        answer.model = malloc(sizeof(*answer.model) * (s->natoms ? s->natoms : 1u));
        if (!answer.model) {
            steps_free(s->steps, s->nsteps);
            s->steps = NULL;
            solver_free(s);
            return (AspAnswer){.kind = ASP_ANSWER_ERROR};
        }
        answer.kind = ASP_ANSWER_SAT;
        for (uint32_t a = 0u; a < s->natoms; a++)
            if (s->val[a] == 1)
                answer.model[answer.model_len++] = s->atom_ids[a];
        /* the reals' values under the whole assignment's bounds, where the
         * objective is least when there is one */
        if (s->lra && s->theories->nreals) {
            uint32_t nr = s->theories->nreals;
            answer.reals = calloc(nr, sizeof(*answer.reals));
            bool ok = answer.reals != NULL && lra_check(s->lra) == LRA_OK;
            if (ok && s->lra_objective >= 0) {
                bool unbounded = false;
                char *c = NULL, *k = NULL;
                ok = lra_minimize(s->lra, (uint32_t)s->lra_objective, &unbounded, &c, &k) == LRA_OK;
                answer.objective_unbounded = unbounded;
                if (ok && !unbounded) {
                    answer.objective = c;
                    answer.objective_attained = strcmp(k, "0") == 0;
                    c = NULL;
                }
                free(c);
                free(k);
            }
            for (uint32_t v = 0u; ok && v < nr; v++) {
                answer.reals[v] = lra_value(s->lra, v);
                ok = answer.reals[v] != NULL;
                answer.reals_len = v + 1u;
            }
            if (!ok)
                answer.kind = ASP_ANSWER_ERROR;
        }
    } else if (r == ITER_UNSAT && !s->oom) {
        answer.kind = ASP_ANSWER_UNSAT;
    } else {
        answer.kind = s->oom ? ASP_ANSWER_ERROR : ASP_ANSWER_STUCK;
    }
    answer.steps = s->steps;
    answer.steps_len = s->nsteps;
    s->steps = NULL;
    hall_record_stats(s);
    const AspRule *rules = s->rules;
    uint32_t rule_count = s->nrules;
    const AspWeightDef *defs = s->defs;
    uint32_t def_count = s->ndefs;
    const AspClause *clauses = s->clauses;
    uint32_t nclauses = s->nclauses;
    const AspSmtTheories *theories = s->theories;
    const AspRule *extra = s->extra;
    uint32_t nextra = s->nextra;
    bool smt = s->cc != NULL;
    uint32_t nvars = s->nvars;
    bool fast = s->fast;
    solver_free(s);
    (void)search;
    bool refused = false;
    if (fast && smt)
        refused = (answer.kind == ASP_ANSWER_SAT &&
                   !asp_smt_model_check(nvars, clauses, nclauses, theories,
                                        answer.model, answer.model_len,
                                        (const char *const *)answer.reals)) ||
            (answer.kind == ASP_ANSWER_UNSAT &&
             !asp_smt_certificate_check(nvars, clauses, nclauses, theories,
                                        answer.steps, answer.steps_len));
    else if (fast && clauses)
        refused = (answer.kind == ASP_ANSWER_SAT &&
                   !asp_sat_model_check(nvars, clauses, nclauses, answer.model, answer.model_len)) ||
            (answer.kind == ASP_ANSWER_UNSAT &&
             !asp_sat_certificate_check(nvars, clauses, nclauses, answer.steps, answer.steps_len));
    else if (fast)
        refused = (answer.kind == ASP_ANSWER_SAT &&
                   !asp_model_check_constrained(rules, rule_count, defs, def_count,
                                                extra, nextra,
                                                answer.model, answer.model_len)) ||
            (answer.kind == ASP_ANSWER_UNSAT &&
             !asp_certificate_check_constrained(rules, rule_count, defs, def_count,
                                                extra, nextra,
                                                answer.steps, answer.steps_len));
    if (refused)
        answer.kind = ASP_ANSWER_REFUSED;
    return answer;
}

AspAnswer asp_cdnl_solve_search(const AspRule *rules, uint32_t rule_count,
                                AspSearch search, const AspTraceSink *trace,
                                void *context) {
    return asp_cdnl_solve_weighted(rules, rule_count, NULL, 0u, search, trace,
                                   context);
}

AspAnswer asp_cdnl_solve_weighted(const AspRule *rules, uint32_t rule_count,
                                  const AspWeightDef *defs, uint32_t def_count,
                                  AspSearch search, const AspTraceSink *trace,
                                  void *context) {
    if (def_count && search == ASP_SEARCH_REFERENCE)
        return (AspAnswer){.kind = ASP_ANSWER_ERROR};
    Solver s = {.rules = rules, .nrules = rule_count,
                .defs = defs, .ndefs = def_count,
                .trace = trace, .trace_context = context,
                .fast = search == ASP_SEARCH_FAST ||
                         search == ASP_SEARCH_FAST_HALL};
    if (!solver_setup(&s, search, trace, context)) {
        steps_free(s.steps, s.nsteps);
        s.steps = NULL;
        solver_free(&s);
        return (AspAnswer){.kind = ASP_ANSWER_ERROR};
    }
    IterResult r;
    do
        r = solver_step(&s);
    while (r == ITER_NEXT);
    return solver_finish(&s, search, r);
}

struct AspSession {
    Solver s;
    AspSearch search;
    IterResult last;    /* ITER_NEXT while the search runs */
    bool finished;      /* the answer has been taken, the solver freed */
    AspRule *extra;     /* the added constraints, owned */
    uint32_t nextra, capextra;
};

AspSession *asp_session_new(const AspRule *rules, uint32_t rule_count,
                            AspSearch search, const AspTraceSink *trace,
                            void *context) {
    return asp_session_new_weighted(rules, rule_count, NULL, 0u, search, trace,
                                    context);
}

AspSession *asp_session_new_weighted(const AspRule *rules, uint32_t rule_count,
                                     const AspWeightDef *defs,
                                     uint32_t def_count, AspSearch search,
                                     const AspTraceSink *trace, void *context) {
    if (def_count && search == ASP_SEARCH_REFERENCE)
        return NULL;
    AspSession *session = calloc(1u, sizeof(*session));
    if (!session)
        return NULL;
    session->s = (Solver){.rules = rules, .nrules = rule_count,
                          .defs = defs, .ndefs = def_count,
                          .trace = trace, .trace_context = context,
                          .fast = search == ASP_SEARCH_FAST ||
                                   search == ASP_SEARCH_FAST_HALL};
    session->search = search;
    session->last = ITER_NEXT;
    if (!solver_setup(&session->s, search, trace, context)) {
        steps_free(session->s.steps, session->s.nsteps);
        session->s.steps = NULL;
        solver_free(&session->s);
        free(session);
        return NULL;
    }
    return session;
}

AspSessionStatus asp_session_step(AspSession *session) {
    if (!session || session->finished)
        return ASP_SESSION_DONE;
    if (session->last == ITER_NEXT)
        session->last = solver_step(&session->s);
    return session->last == ITER_NEXT ? ASP_SESSION_RUNNING : ASP_SESSION_DONE;
}

uint32_t asp_session_trail(const AspSession *session, AspTrailEntry *out,
                           uint32_t capacity) {
    if (!session || session->finished)
        return 0u;
    const Solver *s = &session->s;
    for (uint32_t i = 0u; out && i < s->ntrail && i < capacity; i++)
        out[i] = (AspTrailEntry){native_lit(s->trail[i].lit), s->trail[i].level,
                                 s->trail[i].reason};
    return s->ntrail;
}

AspSessionCounts asp_session_counts(const AspSession *session) {
    if (!session || session->finished)
        return (AspSessionCounts){0};
    const Solver *s = &session->s;
    return (AspSessionCounts){s->natoms, s->nvars, s->level, s->ntrail,
                              s->nsteps, s->conflicts};
}

int32_t asp_session_atom(const AspSession *session, uint32_t variable) {
    if (!session || session->finished || variable >= session->s.natoms)
        return -1;
    return session->s.atom_ids[variable];
}

AspAnswer asp_session_answer(AspSession *session) {
    if (!session || session->finished || session->last == ITER_NEXT)
        return (AspAnswer){.kind = ASP_ANSWER_ERROR};
    session->finished = true;
    return solver_finish(&session->s, session->search, session->last);
}

int asp_session_decide(AspSession *session, int32_t atom, bool value) {
    if (!session || session->finished || session->last != ITER_NEXT)
        return -1;
    Solver *s = &session->s;
    int32_t v = atom_lookup(s, atom);
    if (v < 0)
        return -1;
    if (s->val[v] >= 0)
        return s->val[v] ? 2 : 0;
    s->forced_plus1 = LIT(v, value ? 1 : 0) + 1;
    return 1;
}

bool asp_session_nogoods(const AspSession *session, int32_t **lits,
                         uint32_t **starts, uint32_t *count) {
    *lits = NULL;
    *starts = NULL;
    *count = 0u;
    if (!session || session->finished)
        return true;
    const Solver *s = &session->s;
    uint32_t total = 0u, n = 0u;
    for (uint32_t g = s->ncompletion; g < s->npool; g++)
        if (s->pool[g].lits.len) {
            total += s->pool[g].lits.len;
            n++;
        }
    int32_t *out = malloc(sizeof(*out) * (total ? total : 1u));
    uint32_t *at = malloc(sizeof(*at) * (n + 1u));
    if (!out || !at) {
        free(out);
        free(at);
        return false;
    }
    uint32_t k = 0u, pos = 0u;
    for (uint32_t g = s->ncompletion; g < s->npool; g++) {
        const IVec *ls = &s->pool[g].lits;
        if (!ls->len)
            continue;
        at[k++] = pos;
        for (uint32_t i = 0u; i < ls->len; i++)
            out[pos++] = native_lit(ls->v[i]);
    }
    at[k] = pos;
    *lits = out;
    *starts = at;
    *count = n;
    return true;
}

bool asp_session_model(AspSession *session, int32_t **atoms, uint32_t *len) {
    *atoms = NULL;
    *len = 0u;
    if (!session || session->finished || session->last != ITER_SAT)
        return false;
    Solver *s = &session->s;
    int32_t *model = malloc(sizeof(*model) * (s->natoms ? s->natoms : 1u));
    if (!model)
        return false;
    uint32_t n = 0u;
    for (uint32_t a = 0u; a < s->natoms; a++)
        if (s->val[a] == 1)
            model[n++] = s->atom_ids[a];
    if (!asp_model_check_constrained(s->rules, s->nrules, s->defs, s->ndefs,
                                     session->extra, session->nextra, model, n)) {
        free(model);
        return false;
    }
    *atoms = model;
    *len = n;
    return true;
}

bool asp_session_constrain(AspSession *session, const int32_t *pos, uint32_t npos,
                           const int32_t *neg, uint32_t nneg) {
    if (!session || session->finished ||
        (session->last != ITER_SAT && session->last != ITER_NEXT))
        return false;
    Solver *s = &session->s;
    /* the constraint over the program's atoms */
    int32_t *store = malloc(sizeof(*store) * (npos + nneg + 1u));
    if (!store)
        return false;
    uint32_t kp = 0u, kn = 0u;
    for (uint32_t i = 0u; i < npos; i++) {
        if (atom_lookup(s, pos[i]) < 0) {   /* never true: never met */
            free(store);
            return true;
        }
        store[kp++] = pos[i];
    }
    for (uint32_t i = 0u; i < nneg; i++)
        if (atom_lookup(s, neg[i]) >= 0)
            store[kp + kn++] = neg[i];
    if (session->nextra == session->capextra) {
        uint32_t cap = session->capextra ? 2u * session->capextra : 16u;
        AspRule *grown = realloc(session->extra, sizeof(*grown) * cap);
        if (!grown) {
            free(store);
            return false;
        }
        /* the solver's view moves with the store */
        session->extra = grown;
        session->capextra = cap;
    }
    session->extra[session->nextra++] =
        (AspRule){.head = -1, .pos = store, .pos_len = kp, .neg = store + kp, .neg_len = kn};
    s->extra = session->extra;
    s->nextra = session->nextra;
    /* back to level 0, the new input watched, everything propagated again */
    uint32_t keep = 0u;
    while (keep < s->ntrail && s->trail[keep].level == 0u)
        keep++;
    truncate_trail(s, keep);
    s->level = 0u;
    IVec lits = {0};
    for (uint32_t i = 0u; i < kp; i++)
        if (!iv_push(&lits, LIT(atom_lookup(s, store[i]), 1)))
            s->oom = true;
    for (uint32_t i = 0u; i < kn; i++)
        if (!iv_push(&lits, LIT(atom_lookup(s, store[kp + i]), 0)))
            s->oom = true;
    int32_t g = s->oom ? -1 : new_nogood(s, lits.v, lits.len);
    AspStep *st = g >= 0 ? new_step(s) : NULL;
    if (st) {
        st->kind = ASP_STEP_INPUT;
        st->id = s->next_id;
        if (!lrat_clause(lits.v, lits.len, &st->clause, &st->clause_len))
            s->oom = true;
        name_step(s, lits.v, lits.len, g);
        int32_t w1, w2;
        if (two_watches(&s->pool[g].lits, &w1, &w2))
            add_watched(s, g, w1, w2);
        else if (!iv_push(&s->shorts, g))
            s->oom = true;
    }
    iv_free(&lits);
    requeue(s);
    session->last = ITER_NEXT;
    return !s->oom;
}

void asp_session_free(AspSession *session) {
    if (!session)
        return;
    for (uint32_t i = 0u; i < session->nextra; i++)
        free((int32_t *)session->extra[i].pos);
    free(session->extra);
    if (!session->finished) {
        steps_free(session->s.steps, session->s.nsteps);
        session->s.steps = NULL;
        solver_free(&session->s);
    }
    free(session);
}

void asp_answer_free(AspAnswer *answer) {
    if (!answer)
        return;
    free(answer->model);
    for (uint32_t i = 0u; i < answer->steps_len; i++) {
        free(answer->steps[i].clause);
        free(answer->steps[i].hints);
        free(answer->steps[i].atoms);
        free(answer->steps[i].groups);
        free(answer->steps[i].cliques);
        step_weights_free(&answer->steps[i]);
    }
    free(answer->steps);
    for (uint32_t i = 0u; i < answer->reals_len; i++)
        free(answer->reals[i]);
    free(answer->reals);
    free(answer->objective);
    memset(answer, 0, sizeof(*answer));
}

void asp_answer_print(FILE *out, const AspAnswer *answer) {
    switch (answer->kind) {
    case ASP_ANSWER_SAT:
        fputs("sat", out);
        for (uint32_t i = 0u; i < answer->model_len; i++)
            fprintf(out, " %d", answer->model[i]);
        fputc('\n', out);
        return;
    case ASP_ANSWER_UNSAT:
        fputs("unsat\n", out);
        for (uint32_t i = 0u; i < answer->steps_len; i++) {
            const AspStep *st = &answer->steps[i];
            fprintf(out, "%s %u :", st->kind == ASP_STEP_RUP ? "rup"
                         : st->kind == ASP_STEP_LOOP ? "loop"
                         : st->kind == ASP_STEP_WEIGHT ? "weight"
                         : st->kind == ASP_STEP_EUF ? "euf"
                         : st->kind == ASP_STEP_LRA ? "lra"
                         : st->kind == ASP_STEP_INPUT ? "input" : "hall", st->id);
            for (uint32_t k = 0u; k < st->clause_len; k++)
                fprintf(out, " %d", st->clause[k]);
            fputs(" :", out);
            if (st->kind == ASP_STEP_HALL) {
                for (uint32_t k = 0u; k < st->groups_len; k++)
                    fprintf(out, " %u", st->groups[k]);
                fputs(" :", out);
                for (uint32_t k = 0u; k < st->cliques_len; k++)
                    if (st->cliques[k] == UINT32_MAX)
                        fputs(" |", out);
                    else
                        fprintf(out, " %u", st->cliques[k]);
            } else if (st->kind == ASP_STEP_RUP) {
                for (uint32_t k = 0u; k < st->hints_len; k++)
                    fprintf(out, " %u", st->hints[k]);
            } else if (st->kind == ASP_STEP_WEIGHT) {
                fprintf(out, " %u", st->def);
            } else {
                for (uint32_t k = 0u; k < st->atoms_len; k++)
                    fprintf(out, " %u", st->atoms[k]);
                fprintf(out, " : %u", st->atom);
            }
            fputc('\n', out);
        }
        return;
    case ASP_ANSWER_STUCK:
        fputs("stuck\n", out);
        return;
    case ASP_ANSWER_ERROR:
        fputs("error\n", out);
        return;
    case ASP_ANSWER_REFUSED:
        fputs("refused\n", out);
        return;
    }
}

/* ---- the stable-model check --------------------------------------------- */

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

/* the index of x among the sorted distinct ids, or -1 */
static int32_t id_index(const int32_t *ids, uint32_t n, int32_t x) {
    uint32_t lo = 0u, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (ids[mid] < x)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo < n && ids[lo] == x ? (int32_t)lo : -1;
}

/* The distinct ids of the model and the rules, numbered 0 .. n-1 in
 * increasing order: by a table over their range when it is no wider than
 * twice their occurrences, else by sorting. */
typedef struct {
    int32_t *ids;       /* sorted distinct ids (the sorted way), else NULL */
    uint32_t *rank;     /* per id - lo: its number, or UINT32_MAX */
    int32_t lo;
    uint32_t n;
} IdNumbering;

static bool id_numbering(IdNumbering *num, const AspRule *rules, uint32_t rule_count,
                         const int32_t *atoms, uint32_t len) {
    memset(num, 0, sizeof(*num));
    size_t m = len;
    int64_t lo = INT32_MAX, hi = INT32_MIN;
#define ID_SEE(x) do { if ((x) < lo) lo = (x); if ((x) > hi) hi = (x); } while (0)
    for (uint32_t i = 0u; i < len; i++)
        ID_SEE(atoms[i]);
    for (uint32_t r = 0u; r < rule_count; r++) {
        m += 1u + rules[r].pos_len + rules[r].neg_len;
        ID_SEE(rules[r].head);
        for (uint32_t i = 0u; i < rules[r].pos_len; i++)
            ID_SEE(rules[r].pos[i]);
        for (uint32_t i = 0u; i < rules[r].neg_len; i++)
            ID_SEE(rules[r].neg[i]);
    }
#undef ID_SEE
    if (m == 0u)
        return true;
    uint64_t range = (uint64_t)(hi - lo) + 1u;
    if (range <= 2u * (uint64_t)m + 64u) {
        num->lo = (int32_t)lo;
        num->rank = malloc(sizeof(*num->rank) * range);
        if (!num->rank)
            return false;
        for (uint64_t i = 0u; i < range; i++)
            num->rank[i] = UINT32_MAX;
        for (uint32_t i = 0u; i < len; i++)
            num->rank[atoms[i] - lo] = 0u;
        for (uint32_t r = 0u; r < rule_count; r++) {
            num->rank[rules[r].head - lo] = 0u;
            for (uint32_t i = 0u; i < rules[r].pos_len; i++)
                num->rank[rules[r].pos[i] - lo] = 0u;
            for (uint32_t i = 0u; i < rules[r].neg_len; i++)
                num->rank[rules[r].neg[i] - lo] = 0u;
        }
        for (uint64_t i = 0u; i < range; i++)
            if (num->rank[i] == 0u)
                num->rank[i] = num->n++;
        return true;
    }
    num->ids = malloc(sizeof(*num->ids) * m);
    if (!num->ids)
        return false;
    size_t k = 0u;
    for (uint32_t i = 0u; i < len; i++)
        num->ids[k++] = atoms[i];
    for (uint32_t r = 0u; r < rule_count; r++) {
        num->ids[k++] = rules[r].head;
        for (uint32_t i = 0u; i < rules[r].pos_len; i++)
            num->ids[k++] = rules[r].pos[i];
        for (uint32_t i = 0u; i < rules[r].neg_len; i++)
            num->ids[k++] = rules[r].neg[i];
    }
    qsort(num->ids, m, sizeof(*num->ids), cmp_i32);
    for (size_t i = 0u; i < m; i++)
        if (num->n == 0u || num->ids[num->n - 1u] != num->ids[i])
            num->ids[num->n++] = num->ids[i];
    return true;
}

/* an id's number; every id of the model and the rules has one */
static uint32_t id_number(const IdNumbering *num, int32_t x) {
    if (num->rank)
        return num->rank[x - num->lo];
    return (uint32_t)id_index(num->ids, num->n, x);
}

bool asp_model_check(const AspRule *rules, uint32_t rule_count,
                     const int32_t *atoms, uint32_t len) {
    IdNumbering num;
    if (!id_numbering(&num, rules, rule_count, atoms, len)) {
        free(num.ids);
        free(num.rank);
        return false;
    }
    uint32_t n = num.n;
    uint8_t *in_model = calloc(n ? n : 1u, 1u);
    uint8_t *derived = calloc(n ? n : 1u, 1u);
    uint32_t *need = malloc(sizeof(*need) * (rule_count ? rule_count : 1u));
    uint32_t *occ_start = calloc((size_t)n + 1u, sizeof(*occ_start));
    size_t occ_total = 0u;
    for (uint32_t r = 0u; r < rule_count; r++)
        occ_total += rules[r].pos_len;
    uint32_t *occ = malloc(sizeof(*occ) * (occ_total ? occ_total : 1u));
    uint32_t *queue = malloc(sizeof(*queue) * (n ? n : 1u));
    bool ok = in_model && derived && need && occ_start && occ && queue;
    for (uint32_t i = 0u; ok && i < len; i++)
        in_model[id_number(&num, atoms[i])] = 1u;
    /* the reduct: a rule stays when the model leaves its negative atoms false */
    uint32_t head = 0u, tail = 0u;
    if (ok) {
        for (uint32_t r = 0u; r < rule_count; r++) {
            bool kept = true;
            for (uint32_t i = 0u; i < rules[r].neg_len && kept; i++)
                kept = !in_model[id_number(&num, rules[r].neg[i])];
            need[r] = kept ? rules[r].pos_len : UINT32_MAX;
            if (kept)
                for (uint32_t i = 0u; i < rules[r].pos_len; i++)
                    occ_start[id_number(&num, rules[r].pos[i]) + 1u]++;
        }
        for (uint32_t a = 0u; a < n; a++)
            occ_start[a + 1u] += occ_start[a];
        uint32_t *fill = calloc(n ? n : 1u, sizeof(*fill));
        ok = fill != NULL;
        for (uint32_t r = 0u; ok && r < rule_count; r++)
            if (need[r] != UINT32_MAX)
                for (uint32_t i = 0u; i < rules[r].pos_len; i++) {
                    uint32_t a = id_number(&num, rules[r].pos[i]);
                    occ[occ_start[a] + fill[a]++] = r;
                }
        free(fill);
        /* its least model, by counting each rule's underived positive atoms */
        for (uint32_t r = 0u; ok && r < rule_count; r++)
            if (need[r] == 0u) {
                uint32_t h = id_number(&num, rules[r].head);
                if (!derived[h]) {
                    derived[h] = 1u;
                    queue[tail++] = h;
                }
            }
        while (ok && head < tail) {
            uint32_t a = queue[head++];
            for (uint32_t k = occ_start[a]; k < occ_start[a + 1u]; k++) {
                uint32_t r = occ[k];
                if (--need[r] == 0u) {
                    uint32_t h = id_number(&num, rules[r].head);
                    if (!derived[h]) {
                        derived[h] = 1u;
                        queue[tail++] = h;
                    }
                }
            }
        }
        for (uint32_t a = 0u; ok && a < n; a++)
            ok = derived[a] == in_model[a];
    }
    free(num.ids);
    free(num.rank);
    free(in_model);
    free(derived);
    free(need);
    free(occ_start);
    free(occ);
    free(queue);
    return ok;
}

bool asp_model_check_weighted(const AspRule *rules, uint32_t rule_count,
                              const AspWeightDef *defs, uint32_t def_count,
                              const int32_t *atoms, uint32_t len) {
    if (!def_count)
        return asp_model_check(rules, rule_count, atoms, len);
    int32_t *sorted = malloc(sizeof(*sorted) * (len ? len : 1u));
    if (!sorted)
        return false;
    if (len)
        memcpy(sorted, atoms, sizeof(*sorted) * len);
    qsort(sorted, len, sizeof(*sorted), cmp_i32);
    bool ok = true;
    uint32_t given = 0u;
    for (uint32_t d = 0u; ok && d < def_count; d++) {
        const AspWeightDef *def = &defs[d];
        uint64_t sum = 0u;
        for (uint32_t i = 0u; i < def->pos_len; i++)
            if (id_index(sorted, len, def->pos[i]) >= 0)
                sum += def->pos_w[i];
        for (uint32_t i = 0u; i < def->neg_len; i++)
            if (id_index(sorted, len, def->neg[i]) < 0)
                sum += def->neg_w[i];
        bool holds = id_index(sorted, len, def->atom) >= 0;
        ok = holds == (sum >= def->bound);
        given += holds ? 1u : 0u;
    }
    /* the definitions' atoms that hold, given as facts */
    AspRule *all = ok ? malloc(sizeof(*all) * ((size_t)rule_count + given + 1u)) : NULL;
    if (ok && !all)
        ok = false;
    if (ok) {
        if (rule_count)
            memcpy(all, rules, sizeof(*rules) * rule_count);
        uint32_t n = rule_count;
        for (uint32_t d = 0u; d < def_count; d++)
            if (id_index(sorted, len, defs[d].atom) >= 0)
                all[n++] = (AspRule){.head = defs[d].atom};
        ok = asp_model_check(all, n, atoms, len);
    }
    free(all);
    free(sorted);
    return ok;
}

/* ---- the certificate checker (Certificate.lean, Program.checkCert) -------- */

typedef struct {
    uint32_t id;
    int32_t *lits;
    uint32_t len;
} LiveClause;

/* Live clauses by identifier.  A step whose identifier is live is refused,
 * so identifiers are unique among live clauses and the one an identifier
 * finds is the newest with it, as Lean's lookup over the live list. */
typedef struct {
    uint64_t *keys;   /* identifier + 1; 0 for an empty slot */
    uint32_t *at;     /* its index in the live array */
    uint32_t mask;
} LiveIndex;

static bool live_index_init(LiveIndex *x, uint32_t n) {
    uint32_t size = 64u;
    while (size < 2u * n + 2u)
        size *= 2u;
    x->mask = size - 1u;
    x->keys = calloc(size, sizeof(*x->keys));
    x->at = malloc(sizeof(*x->at) * size);
    return x->keys && x->at;
}

static void live_index_free(LiveIndex *x) {
    free(x->keys);
    free(x->at);
}

static uint32_t live_slot(const LiveIndex *x, uint32_t id) {
    return (uint32_t)(((uint64_t)id * 0x9E3779B97F4A7C15ull) >> 32) & x->mask;
}

static void live_index_put(LiveIndex *x, uint32_t id, uint32_t at) {
    uint32_t i = live_slot(x, id);
    while (x->keys[i] && x->keys[i] != (uint64_t)id + 1u)
        i = (i + 1u) & x->mask;
    x->keys[i] = (uint64_t)id + 1u;
    x->at[i] = at;
}

static const LiveClause *lookup_clause(const LiveClause *live,
                                       const LiveIndex *index, uint32_t id) {
    for (uint32_t i = live_slot(index, id); index->keys[i];
         i = (i + 1u) & index->mask)
        if (index->keys[i] == (uint64_t)id + 1u)
            return &live[index->at[i]];
    return NULL;
}

/* The literals a RUP check has assumed: a set by open addressing, emptied
 * through the slots it filled.  Membership is the list membership of Lean's
 * rupCheck. */
typedef struct {
    int32_t *keys;
    uint8_t *used;
    uint32_t *filled;
    uint32_t nfilled, mask;
} LitSet;

/* Room for `n` literals at half load; an empty set keeps a larger table. */
static bool litset_reserve(LitSet *x, uint32_t n) {
    uint32_t size = 64u;
    while (size < 2u * n + 2u)
        size *= 2u;
    if (x->keys && x->mask + 1u >= size)
        return true;
    free(x->keys);
    free(x->used);
    free(x->filled);
    x->keys = malloc(sizeof(*x->keys) * size);
    x->used = calloc(size, 1u);
    x->filled = malloc(sizeof(*x->filled) * size);
    x->mask = size - 1u;
    x->nfilled = 0u;
    return x->keys && x->used && x->filled;
}

static void litset_free(LitSet *x) {
    free(x->keys);
    free(x->used);
    free(x->filled);
}

static uint32_t litset_slot(const LitSet *x, int32_t k) {
    return (uint32_t)(((uint64_t)(uint32_t)k * 0x9E3779B97F4A7C15ull) >> 32) & x->mask;
}

static bool litset_has(const LitSet *x, int32_t k) {
    for (uint32_t i = litset_slot(x, k); x->used[i]; i = (i + 1u) & x->mask)
        if (x->keys[i] == k)
            return true;
    return false;
}

static void litset_add(LitSet *x, int32_t k) {
    uint32_t i = litset_slot(x, k);
    for (; x->used[i]; i = (i + 1u) & x->mask)
        if (x->keys[i] == k)
            return;
    x->used[i] = 1u;
    x->keys[i] = k;
    x->filled[x->nfilled++] = i;
}

static void litset_clear(LitSet *x) {
    for (uint32_t i = 0u; i < x->nfilled; i++)
        x->used[x->filled[i]] = 0u;
    x->nfilled = 0u;
}

static bool lits_contains(const int32_t *xs, uint32_t n, int32_t x) {
    for (uint32_t i = 0u; i < n; i++)
        if (xs[i] == x)
            return true;
    return false;
}

/* PropositionalLRATActions.rupCheck: assume the clause's complements, then each
 * hint in order must be unit (its open literal is assumed) or conflicting. */
static bool rup_check(const LiveClause *live, const LiveIndex *index,
                      LitSet *assigned, const AspStep *st) {
    /* every hint must name a live clause (Lean's hints.mapM lookupClause) */
    for (uint32_t h = 0u; h < st->hints_len; h++)
        if (!lookup_clause(live, index, st->hints[h]))
            return false;
    /* at most every clause literal and one unit per hint are assumed */
    if (!litset_reserve(assigned, st->clause_len + st->hints_len + 1u))
        return false;
    bool ok = false;
    for (uint32_t i = 0u; i < st->clause_len; i++)
        litset_add(assigned, -st->clause[i]);
    for (uint32_t h = 0u; h < st->hints_len; h++) {
        const LiveClause *c = lookup_clause(live, index, st->hints[h]);
        if (!c)
            break;
        uint32_t open = 0u;
        int32_t unit = 0;
        for (uint32_t k = 0u; k < c->len; k++)
            if (!litset_has(assigned, -c->lits[k])) {
                open++;
                unit = c->lits[k];
            }
        if (open == 0u) {
            ok = true;
            break;
        }
        if (open > 1u)
            break;
        litset_add(assigned, unit);
    }
    litset_clear(assigned);
    return ok;
}

/* DefProgram.loopStepOk: the clause has exactly the literals of the loop
 * clause recomputed from the program for the stated atoms and atom, each of
 * the two lists' literals among the other's (by marks, emptied after).  A set
 * holding a definition's atom or a falsum atom is refused: the one is
 * supported by its definition, not its rules, and the rules of the other
 * have no body variables. */
static bool loop_step_ok(Solver *s, const AspStep *st, LitSet *set) {
    if (!lits_contains((const int32_t *)st->atoms, st->atoms_len, (int32_t)st->atom))
        return false;
    /* a weight definition's atom is supported by its definition, which the
     * loop clause over the rules does not see: no loop step holds one */
    for (uint32_t i = 0u; i < st->atoms_len; i++)
        if (st->atoms[i] >= s->natoms || s->falsum[st->atoms[i]] ||
            (s->var_def && s->var_def[st->atoms[i]] >= 0))
            return false;
    uint8_t *in_u = calloc(s->natoms ? s->natoms : 1u, 1u);
    if (!in_u)
        return false;
    for (uint32_t i = 0u; i < st->atoms_len; i++)
        in_u[st->atoms[i]] = 1u;
    IVec external = {0};
    IVec l = {0};
    bool built = external_rules(s, in_u, (const int32_t *)st->atoms, st->atoms_len, &external) &&
        loop_lits(s, &external, st->atom, &l);
    iv_free(&external);
    free(in_u);
    int32_t *clause = NULL;
    uint32_t len = 0u;
    if (!built || !lrat_clause(l.v, l.len, &clause, &len)) {
        iv_free(&l);
        return false;
    }
    iv_free(&l);
    bool same = litset_reserve(set, len > st->clause_len ? len : st->clause_len);
    for (uint32_t i = 0u; same && i < len; i++)
        litset_add(set, clause[i]);
    for (uint32_t i = 0u; same && i < st->clause_len; i++)
        same = litset_has(set, st->clause[i]);
    litset_clear(set);
    for (uint32_t i = 0u; same && i < st->clause_len; i++)
        litset_add(set, st->clause[i]);
    for (uint32_t i = 0u; same && i < len; i++)
        same = litset_has(set, clause[i]);
    litset_clear(set);
    free(clause);
    return same;
}

/* Program.checkCert over the inputs in s's pool, named 1, 2, ...: every step
 * accepted and the empty clause derived.  Loop and Hall steps need the
 * program's rules. */
/* A weight step is accepted when every assignment falsifying its clause
 * violates its definition: with W in the clause (falsified, W false) the
 * literals it forces to contribute weigh at least the bound; with not W in
 * it (W true) the total less the literals it forces not to contribute
 * weighs less than the bound.  A clause holding a variable both ways holds
 * always. */
static bool weight_step_ok(const Solver *s, const AspStep *st) {
    if (!s->ndefs || st->def >= s->ndefs)
        return false;
    uint32_t d = st->def;
    uint32_t wvar = s->def_var[d];
    int w_in = 0;   /* +1: W in the clause, -1: not W in it */
    for (uint32_t i = 0u; i < st->clause_len; i++) {
        int32_t x = st->clause[i];
        uint32_t v = (uint32_t)(x < 0 ? -x : x) - 1u;
        if (v >= s->nvars)
            return false;
        for (uint32_t j = 0u; j < i; j++)
            if (st->clause[j] == -x)
                return true;
        if (v == wvar)
            w_in = x > 0 ? 1 : -1;
    }
    if (w_in == 0)
        return false;
    uint64_t forced_in = 0u, forced_out = 0u;
    for (uint32_t k = s->dl_start[d]; k < s->dl_start[d + 1u]; k++) {
        uint32_t v = LIT_VAR(s->dl_lit[k]);
        for (uint32_t i = 0u; i < st->clause_len; i++) {
            int32_t x = st->clause[i];
            if ((uint32_t)(x < 0 ? -x : x) - 1u != v)
                continue;
            /* falsified, x's variable takes the value x denies */
            bool value = x < 0;
            if (value == (bool)LIT_SIGN(s->dl_lit[k]))
                forced_in += s->dl_w[k];
            else
                forced_out += s->dl_w[k];
            break;
        }
    }
    uint64_t bound = s->defs[d].bound;
    return w_in > 0 ? forced_in >= bound
                    : s->d_total[d] - forced_out < bound;
}

static bool check_steps(Solver *s, const AspStep *steps, uint32_t steps_len) {
    LiveClause *live = NULL;
    uint32_t nlive = 0u;
    LiveIndex index = {0};
    LitSet assigned = {0};
    InputIndex inputs = {0};
    bool accepted = false;
    live = malloc(sizeof(*live) * (s->ncompletion + steps_len + 1u));
    if (!live || !live_index_init(&index, s->ncompletion + steps_len + 1u))
        goto done;
    for (uint32_t g = 0u; g < s->ncompletion; g++) {     /* Program.inputs */
        LiveClause *c = &live[nlive];
        c->id = g + 1u;
        c->lits = NULL;
        if (!lrat_clause(s->pool[g].lits.v, s->pool[g].lits.len, &c->lits, &c->len))
            goto done;
        live_index_put(&index, c->id, nlive++);
    }
    for (uint32_t i = 0u; i < steps_len; i++) {          /* Program.certStep */
        const AspStep *st = &steps[i];
        if (lookup_clause(live, &index, st->id))
            goto done;
        bool ok = st->kind == ASP_STEP_RUP ? rup_check(live, &index, &assigned, st)
            : st->kind == ASP_STEP_EUF ? euf_step_ok(s, st)
            : st->kind == ASP_STEP_LRA ? lra_step_ok(s, st)
            : st->kind == ASP_STEP_INPUT ? input_step_ok(s, &inputs, st)
            : s->clauses ? false
            : st->kind == ASP_STEP_LOOP ? loop_step_ok(s, st, &assigned)
            : st->kind == ASP_STEP_WEIGHT ? weight_step_ok(s, st)
            : hall_step_ok(s, st);
        if (!ok)
            goto done;
        live_index_put(&index, st->id, nlive);
        LiveClause *c = &live[nlive++];
        c->id = st->id;
        c->len = st->clause_len;
        c->lits = malloc(sizeof(*c->lits) * (st->clause_len ? st->clause_len : 1u));
        if (!c->lits)
            goto done;
        if (st->clause_len)
            memcpy(c->lits, st->clause, sizeof(*c->lits) * st->clause_len);
    }
    for (uint32_t i = 0u; i < nlive && !accepted; i++)  /* the empty clause is live */
        accepted = live[i].len == 0u;
done:
    for (uint32_t i = 0u; i < nlive; i++)
        free(live[i].lits);
    free(live);
    live_index_free(&index);
    litset_free(&assigned);
    input_index_free(&inputs);
    return accepted;
}

bool asp_certificate_check(const AspRule *rules, uint32_t rule_count,
                           const AspStep *steps, uint32_t steps_len) {
    return asp_certificate_check_weighted(rules, rule_count, NULL, 0u, steps,
                                          steps_len);
}

bool asp_certificate_check_weighted(const AspRule *rules, uint32_t rule_count,
                                    const AspWeightDef *defs,
                                    uint32_t def_count, const AspStep *steps,
                                    uint32_t steps_len) {
    Solver s = {.rules = rules, .nrules = rule_count,
                .defs = defs, .ndefs = def_count};
    bool accepted = number_variables(&s) &&
        (!def_count || weights_setup(&s)) && completion(&s) &&
        check_steps(&s, steps, steps_len);
    solver_free(&s);
    return accepted;
}

bool asp_certificate_check_constrained(const AspRule *rules, uint32_t rule_count,
                                       const AspWeightDef *defs, uint32_t def_count,
                                       const AspRule *extra, uint32_t nextra,
                                       const AspStep *steps, uint32_t steps_len) {
    Solver s = {.rules = rules, .nrules = rule_count,
                .defs = defs, .ndefs = def_count, .extra = extra, .nextra = nextra};
    bool accepted = number_variables(&s) &&
        (!def_count || weights_setup(&s)) && completion(&s) &&
        check_steps(&s, steps, steps_len);
    solver_free(&s);
    return accepted;
}

/* A model of the program meeting every added constraint: none has all its
 * positive atoms in the model and all its negative ones out. */
bool asp_model_check_constrained(const AspRule *rules, uint32_t rule_count,
                                 const AspWeightDef *defs, uint32_t def_count,
                                 const AspRule *extra, uint32_t nextra,
                                 const int32_t *atoms, uint32_t len) {
    if (!asp_model_check_weighted(rules, rule_count, defs, def_count, atoms, len))
        return false;
    if (nextra == 0u)
        return true;
    /* the model's atoms marked by id (ids are nonnegative) */
    int32_t top = -1;
    for (uint32_t k = 0u; k < len; k++)
        if (atoms[k] > top)
            top = atoms[k];
    for (uint32_t c = 0u; c < nextra; c++) {
        for (uint32_t i = 0u; i < extra[c].pos_len; i++)
            if (extra[c].pos[i] > top)
                top = extra[c].pos[i];
        for (uint32_t i = 0u; i < extra[c].neg_len; i++)
            if (extra[c].neg[i] > top)
                top = extra[c].neg[i];
    }
    uint8_t *in = calloc((size_t)top + 2u, 1u);
    if (!in)
        return false;
    for (uint32_t k = 0u; k < len; k++)
        if (atoms[k] >= 0)
            in[atoms[k]] = 1u;
    bool ok = true;
    for (uint32_t c = 0u; ok && c < nextra; c++) {
        bool violated = true;
        for (uint32_t i = 0u; violated && i < extra[c].pos_len; i++)
            violated = extra[c].pos[i] >= 0 && in[extra[c].pos[i]];
        for (uint32_t i = 0u; violated && i < extra[c].neg_len; i++)
            violated = extra[c].neg[i] < 0 || !in[extra[c].neg[i]];
        ok = !violated;
    }
    free(in);
    return ok;
}

/* ---- the SAT personality ------------------------------------------------ */

/* The CNF as the kernel's inputs: variable v (1 <= v <= nvars) is the
 * kernel's variable v - 1, decided like an atom; clause i is the nogood of
 * its literals' complements, with the name i + 1.  No rules, so no
 * unfounded sets. */
static bool sat_inputs(Solver *s, uint32_t nvars, const AspClause *clauses,
                       uint32_t nclauses) {
    s->clauses = clauses;
    s->nclauses = nclauses;
    s->natoms = nvars;
    s->nvars = nvars;
    s->tight = true;
    s->atom_ids = malloc(sizeof(*s->atom_ids) * (nvars ? nvars : 1u));
    if (!s->atom_ids)
        return false;
    for (uint32_t v = 0u; v < nvars; v++)
        s->atom_ids[v] = (int32_t)v + 1;
    IVec g = {0};
    bool ok = true;
    for (uint32_t i = 0u; ok && i < nclauses; i++) {
        g.len = 0u;
        for (uint32_t k = 0u; ok && k < clauses[i].len; k++) {
            int32_t d = clauses[i].lits[k];
            uint32_t v = d < 0 ? (uint32_t)(-(int64_t)d) : (uint32_t)d;
            ok = d != 0 && v <= nvars && iv_push(&g, LIT(v - 1u, d < 0));
        }
        ok = ok && new_nogood(s, g.v, g.len) >= 0;
    }
    iv_free(&g);
    s->ncompletion = s->npool;
    for (uint32_t k = 0u; k < s->ncompletion; k++)
        s->pool[k].id = k + 1u;
    s->support_first = s->npool;
    return ok;
}

AspAnswer asp_sat_solve(uint32_t nvars, const AspClause *clauses, uint32_t nclauses) {
    Solver s = {.fast = true};
    if (!sat_inputs(&s, nvars, clauses, nclauses) ||
        !kernel_setup(&s, ASP_SEARCH_FAST, NULL, NULL)) {
        steps_free(s.steps, s.nsteps);
        s.steps = NULL;
        solver_free(&s);
        return (AspAnswer){.kind = ASP_ANSWER_ERROR};
    }
    IterResult r;
    do
        r = solver_step(&s);
    while (r == ITER_NEXT);
    return solver_finish(&s, ASP_SEARCH_FAST, r);
}

AspAnswer asp_smt_solve(uint32_t nvars, const AspClause *clauses,
                        uint32_t nclauses, const AspSmtTheories *theories) {
    Solver s = {.fast = true};
    if (!sat_inputs(&s, nvars, clauses, nclauses) ||
        !smt_setup(&s, theories) ||
        !kernel_setup(&s, ASP_SEARCH_FAST, NULL, NULL)) {
        steps_free(s.steps, s.nsteps);
        s.steps = NULL;
        solver_free(&s);
        return (AspAnswer){.kind = ASP_ANSWER_ERROR};
    }
    IterResult r;
    do
        r = solver_step(&s);
    while (r == ITER_NEXT);
    return solver_finish(&s, ASP_SEARCH_FAST, r);
}

bool asp_smt_model_check(uint32_t nvars, const AspClause *clauses,
                         uint32_t nclauses, const AspSmtTheories *theories,
                         const int32_t *vars, uint32_t len,
                         const char *const *reals) {
    if (!asp_sat_model_check(nvars, clauses, nclauses, vars, len))
        return false;
    const AspSmtEquality *eqs = theories->eqs;
    uint32_t neqs = theories->neqs;
    Solver s = {0};
    s.nvars = nvars;
    uint8_t *val = calloc(nvars + 1u, 1u);
    bool ok = val && smt_setup(&s, theories);
    for (uint32_t k = 0u; ok && k < len; k++)
        val[vars[k]] = 1u;
    for (uint32_t e = 0u; ok && e < neqs; e++)
        if (val[eqs[e].var])
            ok = euf_merge(s.cc, s.cc_term[eqs[e].lhs], s.cc_term[eqs[e].rhs], e);
    for (uint32_t e = 0u; ok && e < neqs; e++)
        if (!val[eqs[e].var])
            ok = !euf_equal(s.cc, s.cc_term[eqs[e].lhs], s.cc_term[eqs[e].rhs]);
    /* every bound atom holds exactly when it is true, at the reals' values */
    ok = ok && (theories->nlra == 0u || (reals != NULL));
    for (uint32_t e = 0u; ok && e < theories->nlra; e++) {
        const AspLraAtom *at = &theories->lra[e];
        int sign = 0;
        ok = lra_compare(at->reals, at->coefs, at->n, reals, theories->nreals,
                         at->bound, &sign);
        bool holds = at->rel == ASP_LRA_LE ? sign <= 0
                   : at->rel == ASP_LRA_LT ? sign < 0
                   : at->rel == ASP_LRA_GE ? sign >= 0 : sign > 0;
        ok = ok && holds == (val[at->var] != 0u);
    }
    free(val);
    solver_free(&s);
    return ok;
}

bool asp_smt_certificate_check(uint32_t nvars, const AspClause *clauses,
                               uint32_t nclauses, const AspSmtTheories *theories,
                               const AspStep *steps, uint32_t steps_len) {
    Solver s = {0};
    bool accepted = sat_inputs(&s, nvars, clauses, nclauses) &&
        smt_setup(&s, theories) &&
        check_steps(&s, steps, steps_len);
    solver_free(&s);
    return accepted;
}

bool asp_sat_model_check(uint32_t nvars, const AspClause *clauses, uint32_t nclauses,
                         const int32_t *vars, uint32_t len) {
    uint8_t *val = calloc(nvars + 1u, 1u);
    if (!val)
        return false;
    bool ok = true;
    for (uint32_t k = 0u; ok && k < len; k++) {
        ok = vars[k] >= 1 && (uint32_t)vars[k] <= nvars;
        if (ok)
            val[vars[k]] = 1u;
    }
    for (uint32_t i = 0u; ok && i < nclauses; i++) {
        bool sat = false;
        for (uint32_t k = 0u; !sat && k < clauses[i].len; k++) {
            int32_t d = clauses[i].lits[k];
            uint32_t v = d < 0 ? (uint32_t)(-(int64_t)d) : (uint32_t)d;
            sat = d != 0 && v <= nvars && val[v] == (d > 0);
        }
        ok = sat;
    }
    free(val);
    return ok;
}

bool asp_sat_certificate_check(uint32_t nvars, const AspClause *clauses,
                               uint32_t nclauses, const AspStep *steps,
                               uint32_t steps_len) {
    Solver s = {0};
    bool accepted = sat_inputs(&s, nvars, clauses, nclauses) &&
        check_steps(&s, steps, steps_len);
    solver_free(&s);
    return accepted;
}

void asp_lrat_print(FILE *out, const AspStep *steps, uint32_t steps_len) {
    for (uint32_t i = 0u; i < steps_len; i++) {
        const AspStep *st = &steps[i];
        if (st->kind != ASP_STEP_RUP)
            continue;
        fprintf(out, "%u", st->id);
        for (uint32_t k = 0u; k < st->clause_len; k++)
            fprintf(out, " %d", st->clause[k]);
        fputs(" 0", out);
        for (uint32_t k = 0u; k < st->hints_len; k++)
            fprintf(out, " %u", st->hints[k]);
        fputs(" 0\n", out);
    }
}

/* ---- enumeration (Enumeration.lean, Program.enumerate) --------------------- */

void asp_enumeration_free(AspEnumeration *e) {
    if (!e)
        return;
    for (uint32_t i = 0u; i < e->models_len; i++)
        free(e->models[i].atoms);
    free(e->models);
    asp_answer_free(&e->final);
    memset(e, 0, sizeof(*e));
}

/* Each found model M is blocked by the rule
 *   none :- M, not none, not (atomList \ M)
 * over the program's atoms lifted to options (some a is a, none a fresh
 * label), and the blocked program is solved again; the last refutation
 * certifies that no further model exists. */
AspEnumeration asp_cdnl_enumerate(const AspRule *rules, uint32_t rule_count) {
    return asp_cdnl_enumerate_search(rules, rule_count, ASP_SEARCH_FAST);
}

AspEnumeration asp_cdnl_enumerate_search(const AspRule *rules, uint32_t rule_count,
                                         AspSearch search) {
    return asp_cdnl_enumerate_weighted(rules, rule_count, NULL, 0u, search);
}

AspEnumeration asp_cdnl_enumerate_weighted(const AspRule *rules,
                                           uint32_t rule_count,
                                           const AspWeightDef *defs,
                                           uint32_t def_count,
                                           AspSearch search) {
    AspEnumeration e = {.kind = ASP_ANSWER_ERROR};
    int32_t none = 0;
    bool any = false;
    for (uint32_t r = 0u; r < rule_count; r++) {
        const AspRule *rule = &rules[r];
        int32_t m = rule->head;
        for (uint32_t i = 0u; i < rule->pos_len; i++)
            m = rule->pos[i] > m ? rule->pos[i] : m;
        for (uint32_t i = 0u; i < rule->neg_len; i++)
            m = rule->neg[i] > m ? rule->neg[i] : m;
        if (!any || m >= none) {
            none = m + 1;
            any = true;
        }
    }
    for (uint32_t d = 0u; d < def_count; d++) {
        const AspWeightDef *def = &defs[d];
        int32_t m = def->atom;
        for (uint32_t i = 0u; i < def->pos_len; i++)
            m = def->pos[i] > m ? def->pos[i] : m;
        for (uint32_t i = 0u; i < def->neg_len; i++)
            m = def->neg[i] > m ? def->neg[i] : m;
        if (!any || m >= none) {
            none = m + 1;
            any = true;
        }
    }
    /* atomList: the heads, each once, keeping the last occurrence */
    IVec heads = {0};
    IVec seq = {0};
    for (uint32_t r = 0u; r < rule_count; r++)
        iv_push(&seq, rules[r].head);
    bool ok = iv_dedup(&heads, seq.v, seq.len);
    iv_free(&seq);
    AspRule *blocked = NULL;
    int32_t *store = NULL;
    uint32_t cap_rules = 0u;
    size_t cap_store = 0u;
    for (uint32_t round = 0u; ok; round++) {
        uint32_t nb = rule_count + e.models_len;
        if (nb > cap_rules) {
            cap_rules = nb * 2u + 4u;
            AspRule *nr = realloc(blocked, sizeof(*nr) * cap_rules);
            if (!nr) { ok = false; break; }
            blocked = nr;
        }
        /* each blocking rule: the model's atoms (a definition's atom among
         * them heads no rule), the label, the heads outside the model */
        size_t need = 1u;
        for (uint32_t k = 0u; k < e.models_len; k++)
            need += (size_t)e.models[k].len + heads.len + 1u;
        if (need > cap_store) {
            cap_store = need * 2u;
            int32_t *ns = realloc(store, sizeof(*ns) * cap_store);
            if (!ns) { ok = false; break; }
            store = ns;
        }
        memcpy(blocked, rules, sizeof(*rules) * rule_count);
        size_t at = 0u;
        for (uint32_t k = 0u; k < e.models_len; k++) {
            const AspModel *m = &e.models[k];
            AspRule *b = &blocked[rule_count + k];
            b->head = none;
            b->pos = store + at;
            b->pos_len = m->len;
            for (uint32_t i = 0u; i < m->len; i++)
                store[at++] = m->atoms[i];
            b->neg = store + at;
            store[at++] = none;
            for (uint32_t i = 0u; i < heads.len; i++) {
                bool in_m = false;
                for (uint32_t j = 0u; j < m->len && !in_m; j++)
                    in_m = m->atoms[j] == heads.v[i];
                if (!in_m)
                    store[at++] = heads.v[i];
            }
            b->neg_len = (uint32_t)(store + at - b->neg);
        }
        AspAnswer a = asp_cdnl_solve_weighted(blocked, nb, defs, def_count,
                                              search, NULL, NULL);
        if (a.kind == ASP_ANSWER_SAT) {
            AspModel *ms = realloc(e.models, sizeof(*ms) * (e.models_len + 1u));
            if (!ms) { asp_answer_free(&a); ok = false; break; }
            e.models = ms;
            AspModel *m = &e.models[e.models_len++];
            m->atoms = malloc(sizeof(*m->atoms) * (a.model_len ? a.model_len : 1u));
            m->len = 0u;
            if (!m->atoms) { asp_answer_free(&a); ok = false; break; }
            for (uint32_t i = 0u; i < a.model_len; i++)
                if (a.model[i] != none)
                    m->atoms[m->len++] = a.model[i];
            asp_answer_free(&a);
            continue;
        }
        e.kind = a.kind;
        e.final = a;
        break;
    }
    if (!ok)
        e.kind = ASP_ANSWER_ERROR;
    else if (e.kind == ASP_ANSWER_UNSAT)
        e.none_label = none;
    free(blocked);
    free(store);
    iv_free(&heads);
    return e;
}
