/* A ground answer-set solver refining Mettapedia's verified reference driver
 * (Logic/ASP/Driver.lean, Program.solve) step for step.  Lean lists that are
 * kept newest first are arrays here with the newest element last, except the
 * watch lists, which keep Lean's front-first order.  Each function names the
 * Lean definition it refines. */
#include "asp_cdnl_v1.h"

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

/* ---- solver state --------------------------------------------------------- */

typedef struct {
    IVec lits;
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

typedef struct {
    const AspRule *rules;
    uint32_t nrules;
    /* atoms: input id <-> variable number (allAtoms order) */
    int32_t *atom_ids;
    uint32_t natoms;
    int32_t *atom_key;  /* open addressing: input id */
    int32_t *atom_var;  /* variable number */
    uint32_t atom_cap;
    uint32_t nvars;
    uint32_t *rule_body; /* variable of each rule's body */
    uint32_t *rule_head; /* variable of each rule's head */
    uint32_t *occ_start; /* per atom: start of its positive occurrences */
    uint32_t *occ_rules; /* the rules where it occurs positively, once per occurrence */
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
    /* where the completion's nogoods are: each rule's first, then the
     * atoms' support nogoods from support_first on */
    uint32_t *rule_first;
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

/* Program.allAtoms and Program.varList (ProgramCNF.lean): atoms in the
 * order of their last occurrence in head :: pos ++ neg over the rules,
 * then the distinct bodies in the order of their last occurrence. */
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
        uint32_t h = (lits_hash(rule->pos, rule->pos_len) * 31u) ^
                     lits_hash(rule->neg, rule->neg_len);
        uint32_t b = h & (size - 1u);
        rep[r] = r;
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
        if (rep[r] == r)
            s->rule_body[r] = next++;
    for (uint32_t r = 0u; r < s->nrules; r++)
        s->rule_body[r] = s->rule_body[rep[r]];
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

static int32_t t_atom(const Solver *s, int32_t id) { return LIT(atom_lookup(s, id), 1); }
static int32_t f_atom(const Solver *s, int32_t id) { return LIT(atom_lookup(s, id), 0); }
static int32_t t_body(const Solver *s, uint32_t r) { return LIT(s->rule_body[r], 1); }
static int32_t f_body(const Solver *s, uint32_t r) { return LIT(s->rule_body[r], 0); }

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

/* Program.completionNogoods (Nogoods.lean): per rule, its head nogood and
 * its body's nogoods; then each atom's support nogood. */
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

static bool completion(Solver *s) {
    IVec l = {0};
    for (uint32_t r = 0u; r < s->nrules && !s->oom; r++) {
        const AspRule *rule = &s->rules[r];
        if (s->rule_first)
            s->rule_first[r] = s->npool;
        int32_t head[2] = {t_body(s, r), f_atom(s, rule->head)};
        new_nogood(s, head, 2u);
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
        for (uint32_t k = start[a]; k < start[a + 1u]; k++)
            iv_push(&l, f_body(s, order[k]));
        new_nogood(s, l.v, l.len);
    }
    free(start);
    free(order);
    iv_free(&l);
    s->ncompletion = s->npool;
    return !s->oom;
}

/* ---- trail -------------------------------------------------------------- */

static bool on_trail(const Solver *s, int32_t lit) {
    return s->val[LIT_VAR(lit)] == (int8_t)LIT_SIGN(lit);
}

static int32_t native_lit(int32_t lit);

static void push_entry(Solver *s, int32_t lit, uint32_t level, int32_t reason) {
    if (s->trace)
        s->trace->event(s->trace_context,
                        reason < 0 ? ASP_TRACE_DECIDE : ASP_TRACE_IMPLY,
                        native_lit(lit), level, reason, NULL, 0u);
    s->trail[s->ntrail] = (Entry){lit, level, reason};
    s->val[LIT_VAR(lit)] = (int8_t)LIT_SIGN(lit);
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
    while (s->ntrail > n) {
        s->ntrail--;
        int32_t lit = s->trail[s->ntrail].lit;
        s->val[LIT_VAR(lit)] = -1;
        if (s->fast && (uint32_t)LIT_VAR(lit) < s->natoms) {
            s->phase[LIT_VAR(lit)] = (uint8_t)LIT_SIGN(lit);
            heap_insert(s, (uint32_t)LIT_VAR(lit));
        }
    }
    s->level = 0u;
    for (uint32_t i = 0u; i < s->ntrail; i++)
        if (s->trail[i].reason < 0)
            s->level = s->trail[i].level;
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

/* Program.emitRup; hints are nogood indices */
static void emit_rup(Solver *s, const int32_t *lits, uint32_t n, const IVec *hints) {
    AspStep *st = new_step(s);
    if (!st)
        return;
    st->kind = ASP_STEP_RUP;
    st->id = s->next_id;
    if (!lrat_clause(lits, n, &st->clause, &st->clause_len)) {
        s->oom = true;
        return;
    }
    st->hints = malloc(sizeof(*st->hints) * (hints->len ? hints->len : 1u));
    if (!st->hints) {
        s->oom = true;
        return;
    }
    for (uint32_t i = 0u; i < hints->len; i++) {
        const Nogood *g = &s->pool[hints->v[i]];
        st->hints[i] = id_of(s, g->lits.v, g->lits.len);
    }
    st->hints_len = hints->len;
    add_name(s, lits, n, s->next_id);
    s->next_id++;
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
    add_name(s, g->lits.v, g->lits.len, s->next_id);
    s->next_id++;
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

/* WState.addWatched */
static void add_watched(Solver *s, int32_t nogood, int32_t w1, int32_t w2) {
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
static PassResult process_list_fast(Solver *s, int32_t x, int32_t *conflict) {
    IVec *list = &s->watches[x];
    uint32_t n = list->len, i = 0u, kept = 0u;
    PassResult result = PASS_OK;
    for (; i < n; i++) {
        int32_t wi = list->v[i];
        Watched *w = &s->db[wi];
        if (w->w1 != x && w->w2 != x)
            continue;
        int32_t other = w->w1 == x ? w->w2 : w->w1;
        if (on_trail(s, LIT_COMPL(other))) {
            list->v[kept++] = wi;
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
            if (!iv_push(&s->watches[r], wi))
                s->oom = true;
            continue;
        }
        list->v[kept++] = wi;
        if (on_trail(s, other)) {
            *conflict = w->nogood;
            result = PASS_CONFLICT;
            i++;
            break;
        }
        push_lit(s, LIT_COMPL(other), w->nogood);
    }
    while (i < n)
        list->v[kept++] = list->v[i++];
    list->len = kept;
    return result;
}

/* processList: visit the nogoods watching x, the list's front first.  The
 * stored list is reversed, so the visit runs from its end; Lean's resulting
 * list (kept positions, newest kept first) is stored as the kept positions in
 * visit order, and at a conflict Lean's (current :: unvisited ++ kept) is
 * stored as kept, then the unvisited in stored order, then the current. */
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

static bool body_false(const Solver *s, uint32_t r) {
    return on_trail(s, f_body(s, r));
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

/* The external bodies of the marked set U, in program order: the rules whose
 * head is in U and none of whose positive atoms is (Program.externalBodies). */
static void external_rules(const Solver *s, const uint8_t *in_u, IVec *out) {
    out->len = 0u;
    for (uint32_t r = 0u; r < s->nrules; r++) {
        if (!in_u[s->rule_head[r]])
            continue;
        bool external = true;
        const AspRule *rule = &s->rules[r];
        for (uint32_t i = 0u; i < rule->pos_len && external; i++)
            external = !in_u[atom_lookup(s, rule->pos[i])];
        if (external)
            iv_push(out, (int32_t)r);
    }
}

/* Program.loopNogood: T a, then F body for each external rule of U */
static int32_t loop_nogood_of(Solver *s, const IVec *external, uint32_t a) {
    IVec l = {0};
    iv_push(&l, LIT(a, 1));
    for (uint32_t k = 0u; k < external->len; k++)
        iv_push(&l, f_body(s, (uint32_t)external->v[k]));
    int32_t g = new_nogood(s, l.v, l.len);
    iv_free(&l);
    return g;
}

/* ---- combined propagation (Propagation.lean, propagateAll) --------------- */

typedef enum { PROP_CLOSED, PROP_CONFLICT, PROP_FUEL } PropResult;

static PropResult propagate_all(Solver *s, int32_t *conflict) {
    uint8_t *sourced = malloc(s->natoms ? s->natoms : 1u);
    uint8_t *in_m = malloc(s->natoms ? s->natoms : 1u);
    IVec marked = {0};
    IVec fresh = {0};
    IVec external = {0};
    PropResult result = PROP_FUEL;
    if (!sourced || !in_m) {
        s->oom = true;
        goto done;
    }
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
        /* At a fixpoint of a tight program an atom whose every body is false
         * is false, so by induction along the positive dependencies every
         * atom the founded set leaves out is false: the check finds no
         * fresh atom and no true one. */
        if (s->tight) {
            result = PROP_CLOSED;
            goto done;
        }
        founded(s, sourced);
        marked.len = 0u;
        memset(in_m, 0, s->natoms ? s->natoms : 1u);
        for (uint32_t a = 0u; a < s->natoms; a++)
            if (!sourced[a]) {
                iv_push(&marked, (int32_t)a);
                in_m[a] = 1u;
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
            external_rules(s, in_m, &external);
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

/* The fast search's lemmas before it searches: an atom a whose only rule r
 * has not a in its body is false.  One RUP step: a's support nogood
 * {T a, F body r} makes the body true, and r's nogood {T body r, T a}
 * conflicts. */
static void self_defeating_lemmas(Solver *s) {
    uint32_t *rules_of = calloc(s->natoms ? s->natoms : 1u, sizeof(*rules_of));
    uint32_t *only = malloc(sizeof(*only) * (s->natoms ? s->natoms : 1u));
    if (!rules_of || !only) {
        free(rules_of);
        free(only);
        return;
    }
    for (uint32_t r = 0u; r < s->nrules; r++) {
        uint32_t h = s->rule_head[r];
        rules_of[h]++;
        only[h] = r;
    }
    IVec hints = {0};
    for (uint32_t a = 0u; a < s->natoms && !s->oom; a++) {
        if (rules_of[a] != 1u)
            continue;
        uint32_t r = only[a];
        const AspRule *rule = &s->rules[r];
        for (uint32_t k = 0u; k < rule->neg_len; k++) {
            if ((uint32_t)atom_lookup(s, rule->neg[k]) != a)
                continue;
            int32_t unit = LIT(a, 1);
            hints.len = 0u;
            iv_push(&hints, (int32_t)(s->support_first + a));
            iv_push(&hints, (int32_t)(s->rule_first[r] + 2u + rule->pos_len + k));
            emit_rup(s, &unit, 1u, &hints);
            int32_t g = new_nogood(s, &unit, 1u);
            if (g < 0 || !iv_push(&s->shorts, g))
                s->oom = true;
            break;
        }
    }
    iv_free(&hints);
    free(rules_of);
    free(only);
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

static IterResult resolve_conflict(Solver *s, int32_t conflict) {
    IVec hints = {0};
    IterResult result = ITER_NEXT;
    if (s->trace)
        s->trace->event(s->trace_context, ASP_TRACE_CONFLICT, 0, s->level,
                        conflict, NULL, 0u);
    if (s->level == 0u) {
        refutation_hints(s, conflict, &hints);
        emit_rup(s, NULL, 0u, &hints);
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
        IVec reasons = {0};
        if (!iv_dedup(&c, d->v, d->len) || !uip_walk(s, &c, &reasons)) {
            s->oom = true;
            result = ITER_STUCK;
            goto learned_done;
        }
        int32_t u = -1;
        for (uint32_t k = 0u; k < c.len && u < 0; k++)
            if (on_trail_between(s, c.v[k], lo, s->ntrail))
                u = c.v[k];
        if (u < 0) {
            result = ITER_STUCK;
            goto learned_done;
        }
        uint32_t j = 0u;
        for (uint32_t k = 0u; k < c.len; k++)
            if (c.v[k] != u && lit_level(s, c.v[k]) > j)
                j = lit_level(s, c.v[k]);
        /* firstUIPHints: the resolved reasons, oldest first, then the conflict */
        for (uint32_t k = reasons.len; k-- > 0u;)
            iv_push(&hints, reasons.v[k]);
        iv_push(&hints, conflict);
        if (s->fast) {
            for (uint32_t k = 0u; k < c.len; k++)
                bump_variable(s, (uint32_t)LIT_VAR(c.v[k]));
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
                    : two_watches(&s->pool[learned].lits, &w1, &w2))
            add_watched(s, learned, w1, w2);
        else if (!iv_push(&s->shorts, learned))
            s->oom = true;
        /* cutTo j, then the asserted literal at level j */
        uint32_t keep = s->ntrail;
        while (keep > 0u && s->trail[keep - 1u].level > j)
            keep--;
        emit_rup(s, c.v, c.len, &hints);
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
        iv_free(&reasons);
        iv_free(&hints);
        return result;
    }
    /* a stale conflict: the decisions nogood */
    IVec dl = {0};
    for (uint32_t i = s->ntrail; i-- > 0u;)
        if (s->trail[i].reason < 0)
            iv_push(&dl, s->trail[i].lit);
    refutation_hints(s, conflict, &hints);
    emit_rup(s, dl.v, dl.len, &hints);
    int32_t reason = new_nogood(s, dl.v, dl.len);
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

/* iterate */
static IterResult iterate(Solver *s) {
    int32_t conflict = -1;
    PropResult p = propagate_all(s, &conflict);
    if (s->oom || p == PROP_FUEL)
        return ITER_STUCK;
    if (p == PROP_CONFLICT)
        return resolve_conflict(s, conflict);
    if (s->fast) {
        /* The fast search decides atoms only: once every atom has a value,
         * the completion's nogoods give every body its value. */
        while (s->heap_len > 0u) {
            uint32_t v = heap_pop(s);
            if (s->val[v] >= 0)
                continue;
            int32_t x = LIT(v, s->phase[v]);
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
    free(s->rule_first);
    free(s->activity);
    free(s->phase);
    free(s->heap);
    free(s->heap_pos);
    free(s->atom_ids);
    free(s->atom_key);
    free(s->atom_var);
    free(s->rule_body);
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

AspAnswer asp_cdnl_solve_search(const AspRule *rules, uint32_t rule_count,
                                AspSearch search, const AspTraceSink *trace,
                                void *context) {
    AspAnswer answer = {.kind = ASP_ANSWER_ERROR};
    Solver s = {.rules = rules, .nrules = rule_count,
                .trace = trace, .trace_context = context,
                .fast = search == ASP_SEARCH_FAST};
    if (s.fast) {
        s.rule_first = malloc(sizeof(*s.rule_first) * (rule_count ? rule_count : 1u));
        if (!s.rule_first)
            goto fail;
    }
    if (!number_variables(&s) || !completion(&s))
        goto fail;
    s.tight = program_tight(&s);
    for (uint32_t a = 0u; trace && a < s.natoms; a++)
        trace->event(context, ASP_TRACE_ATOM, (int32_t)a + 1, (uint32_t)s.atom_ids[a],
                     -1, NULL, 0u);
    uint32_t nlits = 2u * s.nvars;
    s.watches = calloc(nlits ? nlits : 1u, sizeof(*s.watches));
    s.trail = malloc(sizeof(*s.trail) * (s.nvars ? s.nvars : 1u));
    s.val = malloc(s.nvars ? s.nvars : 1u);
    s.trail_pos = calloc(s.nvars ? s.nvars : 1u, sizeof(*s.trail_pos));
    if (!s.watches || !s.trail || !s.val || !s.trail_pos)
        goto fail;
    memset(s.val, -1, s.nvars ? s.nvars : 1u);
    if (s.fast) {
        s.activity = calloc(s.nvars ? s.nvars : 1u, sizeof(*s.activity));
        s.phase = calloc(s.nvars ? s.nvars : 1u, 1u);
        s.heap = malloc(sizeof(*s.heap) * (s.nvars ? s.nvars : 1u));
        s.heap_pos = malloc(sizeof(*s.heap_pos) * (s.nvars ? s.nvars : 1u));
        if (!s.activity || !s.phase || !s.heap || !s.heap_pos)
            goto fail;
        s.bump = 1.0;
        s.restart_at = FAST_RESTART_UNIT * luby(0u);
        for (uint32_t v = 0u; v < s.nvars; v++)
            s.heap_pos[v] = -1;
        for (uint32_t v = 0u; v < s.natoms; v++) {
            s.heap[v] = v;
            s.heap_pos[v] = (int32_t)v;
        }
        s.heap_len = s.natoms;
    }
    /* initState: the completion watched or scanned, named 1, 2, ... */
    for (uint32_t g = 0u; g < s.ncompletion && !s.oom; g++) {
        int32_t w1, w2;
        if (two_watches(&s.pool[g].lits, &w1, &w2)) {
            s.db = grow(s.db, &s.capdb, s.ndb + 1u, sizeof(*s.db), &s.oom);
            if (s.oom)
                break;
            uint32_t i = s.ndb++;
            s.db[i] = (Watched){(int32_t)g, w1, w2};
            if (!iv_push(&s.watches[w1], (int32_t)i) || !iv_push(&s.watches[w2], (int32_t)i))
                s.oom = true;
        } else if (!iv_push(&s.shorts, (int32_t)g)) {
            s.oom = true;
        }
    }
    for (uint32_t l = 0u; l < nlits; l++) {   /* stored reversed */
        IVec *w = &s.watches[l];
        for (uint32_t a = 0u, b = w->len; b > a + 1u; a++, b--) {
            int32_t tmp = w->v[a];
            w->v[a] = w->v[b - 1u];
            w->v[b - 1u] = tmp;
        }
    }
    s.next_id = s.ncompletion + 1u;
    if (!name_tables(&s))
        goto fail;
    if (s.fast)
        self_defeating_lemmas(&s);
    for (;;) {
        if (s.oom)
            goto fail;
        IterResult r = iterate(&s);
        if (r == ITER_NEXT) {
            /* a restart: back to level 0, which stays at its fixpoint */
            if (s.fast && s.conflicts >= s.restart_at) {
                uint32_t keep = 0u;
                while (keep < s.ntrail && s.trail[keep].level == 0u)
                    keep++;
                truncate_trail(&s, keep);
                s.pending.len = 0u;
                s.restart_at = s.conflicts + FAST_RESTART_UNIT * luby(++s.restarts);
            }
            continue;
        }
        if (r == ITER_SAT) {
            answer.kind = ASP_ANSWER_SAT;
            answer.model = malloc(sizeof(*answer.model) * (s.natoms ? s.natoms : 1u));
            if (!answer.model)
                goto fail;
            for (uint32_t a = 0u; a < s.natoms; a++)
                if (s.val[a] == 1)
                    answer.model[answer.model_len++] = s.atom_ids[a];
        } else if (r == ITER_UNSAT) {
            answer.kind = ASP_ANSWER_UNSAT;
        } else {
            answer.kind = s.oom ? ASP_ANSWER_ERROR : ASP_ANSWER_STUCK;
        }
        break;
    }
    answer.steps = s.steps;
    answer.steps_len = s.nsteps;
    s.steps = NULL;
    solver_free(&s);
    if (search == ASP_SEARCH_FAST &&
        ((answer.kind == ASP_ANSWER_SAT &&
          !asp_model_check(rules, rule_count, answer.model, answer.model_len)) ||
         (answer.kind == ASP_ANSWER_UNSAT &&
          !asp_certificate_check(rules, rule_count, answer.steps, answer.steps_len))))
        answer.kind = ASP_ANSWER_REFUSED;
    return answer;
fail:
    for (uint32_t i = 0u; i < s.nsteps; i++) {
        free(s.steps[i].clause);
        free(s.steps[i].hints);
        free(s.steps[i].atoms);
    }
    free(s.steps);
    solver_free(&s);
    free(answer.model);
    return (AspAnswer){.kind = ASP_ANSWER_ERROR};
}

void asp_answer_free(AspAnswer *answer) {
    if (!answer)
        return;
    free(answer->model);
    for (uint32_t i = 0u; i < answer->steps_len; i++) {
        free(answer->steps[i].clause);
        free(answer->steps[i].hints);
        free(answer->steps[i].atoms);
    }
    free(answer->steps);
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
            fprintf(out, "%s %u :", st->kind == ASP_STEP_RUP ? "rup" : "loop", st->id);
            for (uint32_t k = 0u; k < st->clause_len; k++)
                fprintf(out, " %d", st->clause[k]);
            fputs(" :", out);
            if (st->kind == ASP_STEP_RUP) {
                for (uint32_t k = 0u; k < st->hints_len; k++)
                    fprintf(out, " %u", st->hints[k]);
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

bool asp_model_check(const AspRule *rules, uint32_t rule_count,
                     const int32_t *atoms, uint32_t len) {
    size_t total = len;
    for (uint32_t r = 0u; r < rule_count; r++)
        total += 1u + rules[r].pos_len + rules[r].neg_len;
    int32_t *ids = malloc(sizeof(*ids) * (total ? total : 1u));
    if (!ids)
        return false;
    size_t m = 0u;
    for (uint32_t i = 0u; i < len; i++)
        ids[m++] = atoms[i];
    for (uint32_t r = 0u; r < rule_count; r++) {
        ids[m++] = rules[r].head;
        for (uint32_t i = 0u; i < rules[r].pos_len; i++)
            ids[m++] = rules[r].pos[i];
        for (uint32_t i = 0u; i < rules[r].neg_len; i++)
            ids[m++] = rules[r].neg[i];
    }
    qsort(ids, m, sizeof(*ids), cmp_i32);
    uint32_t n = 0u;
    for (size_t i = 0u; i < m; i++)
        if (n == 0u || ids[n - 1u] != ids[i])
            ids[n++] = ids[i];
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
        in_model[id_index(ids, n, atoms[i])] = 1u;
    /* the reduct: a rule stays when the model leaves its negative atoms false */
    uint32_t head = 0u, tail = 0u;
    if (ok) {
        for (uint32_t r = 0u; r < rule_count; r++) {
            bool kept = true;
            for (uint32_t i = 0u; i < rules[r].neg_len && kept; i++)
                kept = !in_model[id_index(ids, n, rules[r].neg[i])];
            need[r] = kept ? rules[r].pos_len : UINT32_MAX;
            if (kept)
                for (uint32_t i = 0u; i < rules[r].pos_len; i++)
                    occ_start[id_index(ids, n, rules[r].pos[i]) + 1u]++;
        }
        for (uint32_t a = 0u; a < n; a++)
            occ_start[a + 1u] += occ_start[a];
        uint32_t *fill = calloc(n ? n : 1u, sizeof(*fill));
        ok = fill != NULL;
        for (uint32_t r = 0u; ok && r < rule_count; r++)
            if (need[r] != UINT32_MAX)
                for (uint32_t i = 0u; i < rules[r].pos_len; i++) {
                    uint32_t a = (uint32_t)id_index(ids, n, rules[r].pos[i]);
                    occ[occ_start[a] + fill[a]++] = r;
                }
        free(fill);
        /* its least model, by counting each rule's underived positive atoms */
        for (uint32_t r = 0u; ok && r < rule_count; r++)
            if (need[r] == 0u) {
                uint32_t h = (uint32_t)id_index(ids, n, rules[r].head);
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
                    uint32_t h = (uint32_t)id_index(ids, n, rules[r].head);
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
    free(ids);
    free(in_model);
    free(derived);
    free(need);
    free(occ_start);
    free(occ);
    free(queue);
    return ok;
}

/* ---- the certificate checker (Certificate.lean, Program.checkCert) -------- */

typedef struct {
    uint32_t id;
    int32_t *lits;
    uint32_t len;
} LiveClause;

static const LiveClause *lookup_clause(const LiveClause *live, uint32_t n, uint32_t id) {
    for (uint32_t i = n; i-- > 0u;)   /* the newest first, as Lean's live list */
        if (live[i].id == id)
            return &live[i];
    return NULL;
}

static bool lits_contains(const int32_t *xs, uint32_t n, int32_t x) {
    for (uint32_t i = 0u; i < n; i++)
        if (xs[i] == x)
            return true;
    return false;
}

/* PropositionalLRATActions.rupCheck: assume the clause's complements, then each
 * hint in order must be unit (its open literal is assumed) or conflicting. */
static bool rup_check(const LiveClause *live, uint32_t nlive, const AspStep *st) {
    /* every hint must name a live clause (Lean's hints.mapM lookupClause) */
    for (uint32_t h = 0u; h < st->hints_len; h++)
        if (!lookup_clause(live, nlive, st->hints[h]))
            return false;
    IVec assigned = {0};
    bool ok = false;
    for (uint32_t i = 0u; i < st->clause_len; i++)
        iv_push(&assigned, -st->clause[i]);
    for (uint32_t h = 0u; h < st->hints_len; h++) {
        const LiveClause *c = lookup_clause(live, nlive, st->hints[h]);
        if (!c)
            break;
        uint32_t open = 0u;
        int32_t unit = 0;
        for (uint32_t k = 0u; k < c->len; k++)
            if (!iv_contains(&assigned, -c->lits[k])) {
                open++;
                unit = c->lits[k];
            }
        if (open == 0u) {
            ok = true;
            break;
        }
        if (open > 1u || !iv_push(&assigned, unit))
            break;
    }
    iv_free(&assigned);
    return ok;
}

/* Program.loopStepOk: the clause has exactly the literals of the loop
 * clause recomputed from the program for the stated atoms and atom. */
static bool loop_step_ok(Solver *s, const AspStep *st) {
    if (!lits_contains((const int32_t *)st->atoms, st->atoms_len, (int32_t)st->atom))
        return false;
    for (uint32_t i = 0u; i < st->atoms_len; i++)
        if (st->atoms[i] >= s->natoms)
            return false;
    uint8_t *in_u = calloc(s->natoms ? s->natoms : 1u, 1u);
    if (!in_u)
        return false;
    for (uint32_t i = 0u; i < st->atoms_len; i++)
        in_u[st->atoms[i]] = 1u;
    IVec external = {0};
    external_rules(s, in_u, &external);
    int32_t g = loop_nogood_of(s, &external, st->atom);
    iv_free(&external);
    free(in_u);
    if (g < 0)
        return false;
    int32_t *clause = NULL;
    uint32_t len = 0u;
    if (!lrat_clause(s->pool[g].lits.v, s->pool[g].lits.len, &clause, &len))
        return false;
    bool same = true;
    for (uint32_t i = 0u; i < len && same; i++)
        same = lits_contains(st->clause, st->clause_len, clause[i]);
    for (uint32_t i = 0u; i < st->clause_len && same; i++)
        same = lits_contains(clause, len, st->clause[i]);
    free(clause);
    return same;
}

bool asp_certificate_check(const AspRule *rules, uint32_t rule_count,
                           const AspStep *steps, uint32_t steps_len) {
    Solver s = {.rules = rules, .nrules = rule_count};
    LiveClause *live = NULL;
    uint32_t nlive = 0u;
    bool accepted = false;
    if (!number_variables(&s) || !completion(&s))
        goto done;
    live = malloc(sizeof(*live) * (s.ncompletion + steps_len + 1u));
    if (!live)
        goto done;
    for (uint32_t g = 0u; g < s.ncompletion; g++) {     /* Program.inputs */
        LiveClause *c = &live[nlive++];
        c->id = g + 1u;
        if (!lrat_clause(s.pool[g].lits.v, s.pool[g].lits.len, &c->lits, &c->len))
            goto done;
    }
    for (uint32_t i = 0u; i < steps_len; i++) {          /* Program.certStep */
        const AspStep *st = &steps[i];
        if (lookup_clause(live, nlive, st->id))
            goto done;
        bool ok = st->kind == ASP_STEP_RUP ? rup_check(live, nlive, st)
                                           : loop_step_ok(&s, st);
        if (!ok)
            goto done;
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
    solver_free(&s);
    return accepted;
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
        size_t need = (size_t)e.models_len * (heads.len + 1u) + 1u;
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
        AspAnswer a = asp_cdnl_solve_search(blocked, nb, search, NULL, NULL);
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
