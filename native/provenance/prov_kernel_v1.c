#include "prov_kernel_v1.h"

#include <stdlib.h>
#include <string.h>

#define UNBOUND UINT32_MAX
#define MAX_VARS 32u
#define MAX_LITS 32u
#define STRIDE (1u + PROV_MAX_ARITY)

/* ---------- hashing ---------- */

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static uint64_t hash_words(const uint32_t *w, uint32_t n) {
    uint64_t h = 0x84222325cbf29ce4ULL ^ n;
    for (uint32_t i = 0; i < n; i++)
        h = mix64(h + w[i] + 0x9e3779b97f4a7c15ULL);
    return h;
}

/* ---------- a map from two words to a word ---------- */

typedef struct { uint32_t k0, k1, val, used; } MapSlot;
typedef struct { MapSlot *slots; uint32_t cap, len; } Map;

static uint32_t map_hash(uint32_t a, uint32_t b) {
    return (uint32_t)mix64(((uint64_t)a << 32) | b);
}

static bool map_grow(Map *m) {
    uint32_t cap = m->cap ? m->cap * 2u : 64u;
    MapSlot *slots = calloc(cap, sizeof(*slots));
    if (!slots)
        return false;
    for (uint32_t i = 0; i < m->cap; i++) {
        if (!m->slots[i].used)
            continue;
        uint32_t j = map_hash(m->slots[i].k0, m->slots[i].k1) & (cap - 1u);
        while (slots[j].used)
            j = (j + 1u) & (cap - 1u);
        slots[j] = m->slots[i];
    }
    free(m->slots);
    m->slots = slots;
    m->cap = cap;
    return true;
}

static bool map_get(const Map *m, uint32_t a, uint32_t b, uint32_t *val) {
    if (!m->cap)
        return false;
    uint32_t j = map_hash(a, b) & (m->cap - 1u);
    while (m->slots[j].used) {
        if (m->slots[j].k0 == a && m->slots[j].k1 == b) {
            *val = m->slots[j].val;
            return true;
        }
        j = (j + 1u) & (m->cap - 1u);
    }
    return false;
}

static bool map_put(Map *m, uint32_t a, uint32_t b, uint32_t val) {
    if ((m->len + 1u) * 2u > m->cap && !map_grow(m))
        return false;
    uint32_t j = map_hash(a, b) & (m->cap - 1u);
    while (m->slots[j].used)
        j = (j + 1u) & (m->cap - 1u);
    m->slots[j] = (MapSlot){a, b, val, 1u};
    m->len++;
    return true;
}

static void map_free(Map *m) {
    free(m->slots);
    memset(m, 0, sizeof(*m));
}

/* ---------- relations: tuples (pred, args...) with sorted views ---------- */

typedef struct {
    uint32_t *t;                       /* n tuples of STRIDE words */
    uint32_t n, cap;
    uint32_t *slots;                   /* dedup: id + 1, 0 empty */
    uint32_t nslots;
    uint32_t *view[PROV_MAX_ARITY];    /* ids sorted by (pred, arg i) */
    uint32_t nviews, viewed;           /* views cover ids < viewed */
} Rel;

static inline const uint32_t *rel_tuple(const Rel *r, uint32_t id) {
    return r->t + (size_t)id * STRIDE;
}

static void rel_free(Rel *r) {
    free(r->t);
    free(r->slots);
    for (uint32_t i = 0; i < PROV_MAX_ARITY; i++)
        free(r->view[i]);
    memset(r, 0, sizeof(*r));
}

static bool rel_rehash(Rel *r) {
    uint32_t nslots = r->nslots ? r->nslots * 2u : 64u;
    uint32_t *slots = calloc(nslots, sizeof(*slots));
    if (!slots)
        return false;
    for (uint32_t id = 0; id < r->n; id++) {
        uint32_t j = (uint32_t)hash_words(rel_tuple(r, id), STRIDE) & (nslots - 1u);
        while (slots[j])
            j = (j + 1u) & (nslots - 1u);
        slots[j] = id + 1u;
    }
    free(r->slots);
    r->slots = slots;
    r->nslots = nslots;
    return true;
}

/* the id of the tuple (STRIDE words, unused arguments 0), adding it if absent */
static bool rel_add(Rel *r, const uint32_t *tuple, uint32_t *id, bool *fresh) {
    if ((r->n + 1u) * 2u > r->nslots && !rel_rehash(r))
        return false;
    uint32_t j = (uint32_t)hash_words(tuple, STRIDE) & (r->nslots - 1u);
    while (r->slots[j]) {
        uint32_t o = r->slots[j] - 1u;
        if (memcmp(rel_tuple(r, o), tuple, sizeof(uint32_t) * STRIDE) == 0) {
            *id = o;
            *fresh = false;
            return true;
        }
        j = (j + 1u) & (r->nslots - 1u);
    }
    if (r->n == r->cap) {
        uint32_t cap = r->cap ? r->cap * 2u : 64u;
        uint32_t *t = realloc(r->t, (size_t)cap * STRIDE * sizeof(*t));
        if (!t)
            return false;
        r->t = t;
        r->cap = cap;
    }
    memcpy(r->t + (size_t)r->n * STRIDE, tuple, sizeof(uint32_t) * STRIDE);
    *id = r->n;
    r->slots[j] = ++r->n;
    *fresh = true;
    return true;
}

static const Rel *sort_rel;
static uint32_t sort_pos;

static int cmp_view(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    const uint32_t *ta = rel_tuple(sort_rel, a), *tb = rel_tuple(sort_rel, b);
    if (ta[0] != tb[0]) return ta[0] < tb[0] ? -1 : 1;
    if (ta[1 + sort_pos] != tb[1 + sort_pos]) return ta[1 + sort_pos] < tb[1 + sort_pos] ? -1 : 1;
    return a < b ? -1 : a > b;
}

/* views over the tuples present now, for argument positions below npos */
static bool rel_index(Rel *r, uint32_t npos) {
    for (uint32_t i = 0; i < npos; i++) {
        uint32_t *v = realloc(r->view[i], sizeof(uint32_t) * (r->n ? r->n : 1u));
        if (!v)
            return false;
        r->view[i] = v;
        for (uint32_t id = 0; id < r->n; id++)
            v[id] = id;
        sort_rel = r;
        sort_pos = i;
        qsort(v, r->n, sizeof(uint32_t), cmp_view);
    }
    r->nviews = npos;
    r->viewed = r->n;
    return true;
}

/* view positions [lo, hi) whose tuples have pred p and, when pos < arity,
 * argument pos equal to val (pos = UNBOUND: pred only, through view 0) */
static void rel_range(const Rel *r, uint32_t p, uint32_t pos, uint32_t val,
                      uint32_t *lo, uint32_t *hi) {
    uint32_t vp = pos == UNBOUND ? 0u : pos;
    const uint32_t *v = r->view[vp];
    uint32_t n = r->viewed;
    if (!v || !n) {
        *lo = *hi = 0;
        return;
    }
    uint32_t a = 0, b = n;
    while (a < b) {
        uint32_t m = a + (b - a) / 2u;
        const uint32_t *t = rel_tuple(r, v[m]);
        bool less = t[0] < p || (pos != UNBOUND && t[0] == p && t[1 + vp] < val);
        if (less) a = m + 1u; else b = m;
    }
    *lo = a;
    b = a;
    while (b < n) {
        const uint32_t *t = rel_tuple(r, v[b]);
        if (t[0] != p || (pos != UNBOUND && t[1 + vp] != val))
            break;
        b++;
    }
    *hi = b;
}

/* ---------- support sets ---------- */

typedef struct {
    uint32_t *data;        /* per support: n, then n labels */
    size_t len, cap;
    uint32_t *start;
    uint32_t count, start_cap;
    uint32_t *slots;
    uint32_t nslots;
} SupportSet;

static const uint32_t *ss_get(const SupportSet *s, uint32_t i, uint32_t *n) {
    const uint32_t *p = s->data + s->start[i];
    *n = p[0];
    return p + 1;
}

static bool ss_rehash(SupportSet *s) {
    uint32_t nslots = s->nslots ? s->nslots * 2u : 16u;
    uint32_t *slots = calloc(nslots, sizeof(*slots));
    if (!slots)
        return false;
    for (uint32_t i = 0; i < s->count; i++) {
        uint32_t n;
        const uint32_t *l = ss_get(s, i, &n);
        uint32_t j = (uint32_t)hash_words(l, n) & (nslots - 1u);
        while (slots[j])
            j = (j + 1u) & (nslots - 1u);
        slots[j] = i + 1u;
    }
    free(s->slots);
    s->slots = slots;
    s->nslots = nslots;
    return true;
}

static bool ss_insert(SupportSet *s, const uint32_t *l, uint32_t n) {
    if ((s->count + 1u) * 2u > s->nslots && !ss_rehash(s))
        return false;
    uint32_t j = (uint32_t)hash_words(l, n) & (s->nslots - 1u);
    while (s->slots[j]) {
        uint32_t m;
        const uint32_t *o = ss_get(s, s->slots[j] - 1u, &m);
        if (m == n && memcmp(o, l, sizeof(uint32_t) * n) == 0)
            return true;
        j = (j + 1u) & (s->nslots - 1u);
    }
    if (s->len + n + 1u > s->cap) {
        size_t cap = s->cap ? s->cap * 2u : 64u;
        while (cap < s->len + n + 1u)
            cap *= 2u;
        uint32_t *data = realloc(s->data, cap * sizeof(*data));
        if (!data)
            return false;
        s->data = data;
        s->cap = cap;
    }
    if (s->count == s->start_cap) {
        uint32_t cap = s->start_cap ? s->start_cap * 2u : 16u;
        uint32_t *start = realloc(s->start, cap * sizeof(*start));
        if (!start)
            return false;
        s->start = start;
        s->start_cap = cap;
    }
    s->start[s->count] = (uint32_t)s->len;
    s->data[s->len++] = n;
    memcpy(s->data + s->len, l, sizeof(uint32_t) * n);
    s->len += n;
    s->slots[j] = ++s->count;
    return true;
}

static void ss_free(SupportSet *s) {
    free(s->data);
    free(s->start);
    free(s->slots);
    memset(s, 0, sizeof(*s));
}

/* out = a ∪ b, both strictly increasing */
static uint32_t merge(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb,
                      uint32_t *out) {
    uint32_t i = 0, k = 0, n = 0;
    while (i < na && k < nb) {
        if (a[i] < b[k]) out[n++] = a[i++];
        else if (b[k] < a[i]) out[n++] = b[k++];
        else { out[n++] = a[i++]; k++; }
    }
    while (i < na) out[n++] = a[i++];
    while (k < nb) out[n++] = b[k++];
    return n;
}

/* ---------- the program ---------- */

typedef struct {
    uint32_t label;             /* or PROV_NO_LABEL: from the label literal */
    uint32_t label_lit;         /* or PROV_NO_LABEL: a fixed label */
    ProvLit head;
    uint32_t nvars, nlits, nderived;
    ProvLit *lits;
} Rule;

struct ProvProgram {
    Rel base;
    uint32_t *base_label;       /* per base tuple id, PROV_NO_LABEL if none */
    uint32_t base_label_cap;
    Rule *rules;
    uint32_t nrules, rules_cap;
    uint32_t npreds, preds_cap;
    uint32_t *arity;            /* per predicate, 0 unknown */
    uint8_t *derived;
    uint32_t *rule_start, *rule_index;
    uint32_t maxarity;
    Rel *grade;                 /* grade[1..ngrades] */
    uint32_t ngrades;
    bool reuse;
    Rel cur;                    /* reuse: the derived items */
    Map fam_of;                 /* (g, item) -> family */
    SupportSet *fams;
    uint32_t nfams, fams_cap;
    ProvStats stats;
    ProvStatus bad;             /* a malformed addition, reported at the query */
    bool oom;
};

ProvProgram *prov_new(void) {
    return calloc(1, sizeof(ProvProgram));
}

static void reset_derived_state(ProvProgram *p) {
    for (uint32_t g = 0; p->grade && g <= p->ngrades; g++)
        rel_free(&p->grade[g]);
    free(p->grade);
    p->grade = NULL;
    p->ngrades = 0;
    rel_free(&p->cur);
    p->reuse = false;
    map_free(&p->fam_of);
    for (uint32_t i = 0; i < p->nfams; i++)
        ss_free(&p->fams[i]);
    free(p->fams);
    p->fams = NULL;
    p->nfams = p->fams_cap = 0;
    free(p->derived);
    free(p->rule_start);
    free(p->rule_index);
    p->derived = NULL;
    p->rule_start = p->rule_index = NULL;
    memset(&p->stats, 0, sizeof(p->stats));
    p->oom = false;
}

void prov_free(ProvProgram *p) {
    if (!p)
        return;
    reset_derived_state(p);
    rel_free(&p->base);
    free(p->base_label);
    for (uint32_t i = 0; i < p->nrules; i++)
        free(p->rules[i].lits);
    free(p->rules);
    free(p->arity);
    free(p);
}

static bool note_pred(ProvProgram *p, uint32_t pred, uint32_t arity) {
    if (pred == UNBOUND || arity == 0u || arity > PROV_MAX_ARITY) {
        p->bad = PROV_LIMIT;
        return false;
    }
    if (pred >= p->preds_cap) {
        uint32_t cap = p->preds_cap ? p->preds_cap : 16u;
        while (cap <= pred)
            cap *= 2u;
        uint32_t *a = realloc(p->arity, sizeof(*a) * cap);
        if (!a)
            return false;
        memset(a + p->preds_cap, 0, sizeof(*a) * (cap - p->preds_cap));
        p->arity = a;
        p->preds_cap = cap;
    }
    if (pred + 1u > p->npreds)
        p->npreds = pred + 1u;
    if (p->arity[pred] == 0u)
        p->arity[pred] = arity;
    else if (p->arity[pred] != arity)
        p->bad = PROV_ARITY;
    if (arity > p->maxarity)
        p->maxarity = arity;
    return true;
}

static bool add_fact(ProvProgram *p, uint32_t label, uint32_t pred, uint32_t arity,
                     const uint32_t *args) {
    if (!note_pred(p, pred, arity))
        return false;
    uint32_t t[STRIDE] = {pred};
    for (uint32_t i = 0; i < arity; i++) {
        if (args[i] == UNBOUND)
            return false;
        t[1 + i] = args[i];
    }
    uint32_t id;
    bool fresh;
    if (!rel_add(&p->base, t, &id, &fresh))
        return false;
    if (id >= p->base_label_cap) {
        uint32_t cap = p->base_label_cap ? p->base_label_cap * 2u : 64u;
        while (cap <= id)
            cap *= 2u;
        uint32_t *l = realloc(p->base_label, sizeof(*l) * cap);
        if (!l)
            return false;
        for (uint32_t i = p->base_label_cap; i < cap; i++)
            l[i] = PROV_NO_LABEL;
        p->base_label = l;
        p->base_label_cap = cap;
    }
    if (fresh)
        p->base_label[id] = label;
    else if (p->base_label[id] != label)
        p->bad = PROV_BAD_LABEL;
    return true;
}

bool prov_add_fact(ProvProgram *p, uint32_t pred, uint32_t arity, const uint32_t *args) {
    return add_fact(p, PROV_NO_LABEL, pred, arity, args);
}

bool prov_add_labelled_fact(ProvProgram *p, uint32_t label, uint32_t pred,
                            uint32_t arity, const uint32_t *args) {
    if (label == PROV_NO_LABEL)
        return false;
    return add_fact(p, label, pred, arity, args);
}

static bool add_rule(ProvProgram *p, uint32_t label, uint32_t label_lit, const ProvLit *head,
                     uint32_t nvars, uint32_t nlits, const ProvLit *lits) {
    if (nvars > MAX_VARS || nlits > MAX_LITS || !note_pred(p, head->pred, head->arity))
        return false;
    if (label_lit != PROV_NO_LABEL && label_lit >= nlits)
        return false;
    for (uint32_t k = 0; k < head->arity; k++)
        if (head->args[k] >= nvars)
            return false;
    for (uint32_t i = 0; i < nlits; i++) {
        if (!note_pred(p, lits[i].pred, lits[i].arity))
            return false;
        for (uint32_t k = 0; k < lits[i].arity; k++)
            if (lits[i].args[k] >= nvars)
                return false;
    }
    if (p->nrules == p->rules_cap) {
        uint32_t cap = p->rules_cap ? p->rules_cap * 2u : 32u;
        Rule *r = realloc(p->rules, cap * sizeof(*r));
        if (!r)
            return false;
        p->rules = r;
        p->rules_cap = cap;
    }
    ProvLit *copy = malloc(sizeof(*copy) * (nlits ? nlits : 1u));
    if (!copy)
        return false;
    memcpy(copy, lits, sizeof(*copy) * nlits);
    p->rules[p->nrules++] = (Rule){label, label_lit, *head, nvars, nlits, 0u, copy};
    return true;
}

bool prov_add_rule(ProvProgram *p, uint32_t label, const ProvLit *head,
                   uint32_t nvars, uint32_t nlits, const ProvLit *lits) {
    if (label == PROV_NO_LABEL)
        return false;
    return add_rule(p, label, PROV_NO_LABEL, head, nvars, nlits, lits);
}

bool prov_add_rule_labelled_by(ProvProgram *p, uint32_t label_lit, const ProvLit *head,
                               uint32_t nvars, uint32_t nlits, const ProvLit *lits) {
    if (label_lit == PROV_NO_LABEL)
        return false;
    return add_rule(p, PROV_NO_LABEL, label_lit, head, nvars, nlits, lits);
}

/* the label of a completed join's rule instance */
static uint32_t instance_label(const ProvProgram *P, const Rule *r, const uint32_t *ids) {
    if (r->label_lit == PROV_NO_LABEL)
        return r->label;
    uint32_t id = ids[r->label_lit];
    return id < P->base_label_cap ? P->base_label[id] : PROV_NO_LABEL;
}

/* ---------- joins over a rule body ---------- */

typedef struct Join Join;
struct Join {
    ProvProgram *P;
    const Rule *r;
    uint32_t vars[MAX_VARS];
    uint32_t grades[MAX_LITS];
    uint32_t ids[MAX_LITS];      /* the tuple each literal matched */
    void (*done)(Join *j);
    void *ctx;
};

static void join_lits(Join *j, uint32_t i, uint32_t remaining, uint32_t derived_left);

static void join_rel(Join *j, uint32_t i, const Rel *rel, uint32_t remaining,
                     uint32_t derived_left) {
    const ProvLit *l = &j->r->lits[i];
    uint32_t first = UNBOUND;
    for (uint32_t k = 0; k < l->arity; k++)
        if (j->vars[l->args[k]] != UNBOUND) {
            first = k;
            break;
        }
    uint32_t lo, hi;
    rel_range(rel, l->pred, first, first == UNBOUND ? 0u : j->vars[l->args[first]], &lo, &hi);
    const uint32_t *view = rel->view[first == UNBOUND ? 0u : first];
    for (uint32_t x = lo; x < hi && !j->P->oom; x++) {
        uint32_t id = view[x];
        const uint32_t *t = rel_tuple(rel, id);
        uint32_t bound[PROV_MAX_ARITY], nb = 0;
        bool ok = true;
        for (uint32_t k = 0; k < l->arity; k++) {
            uint32_t v = l->args[k];
            if (j->vars[v] == UNBOUND) {
                j->vars[v] = t[1 + k];
                bound[nb++] = v;
            } else if (j->vars[v] != t[1 + k]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            j->ids[i] = id;
            join_lits(j, i + 1u, remaining, derived_left);
        }
        for (uint32_t b = 0; b < nb; b++)
            j->vars[bound[b]] = UNBOUND;
    }
}

static void join_lits(Join *j, uint32_t i, uint32_t remaining, uint32_t derived_left) {
    ProvProgram *P = j->P;
    if (P->oom)
        return;
    if (i == j->r->nlits) {
        if (remaining == 0u)
            j->done(j);
        return;
    }
    const ProvLit *l = &j->r->lits[i];
    if (!P->derived[l->pred]) {
        j->grades[i] = 0u;
        join_rel(j, i, &P->base, remaining, derived_left);
        return;
    }
    if (P->reuse) {
        j->grades[i] = 0u;
        join_rel(j, i, &P->cur, remaining, derived_left);
        return;
    }
    /* a derived literal takes a grade >= 1, leaving >= 1 for each later one */
    if (remaining < derived_left)
        return;
    uint32_t lo = derived_left == 1u ? remaining : 1u;
    uint32_t hi = remaining - (derived_left - 1u);
    for (uint32_t g = lo; g <= hi && g <= P->ngrades; g++) {
        if (!P->grade[g].n)
            continue;
        j->grades[i] = g;
        join_rel(j, i, &P->grade[g], remaining - g, derived_left - 1u);
    }
}

static void reset_vars(Join *j) {
    for (uint32_t v = 0; v < MAX_VARS; v++)
        j->vars[v] = UNBOUND;
}

/* binds the head's variables to the tuple's arguments; false when a repeated
 * head variable meets two different arguments */
static bool bind_head(Join *j, const Rule *r, const uint32_t *t) {
    for (uint32_t k = 0; k < r->head.arity; k++) {
        uint32_t v = r->head.args[k];
        if (j->vars[v] == UNBOUND)
            j->vars[v] = t[1 + k];
        else if (j->vars[v] != t[1 + k])
            return false;
    }
    return true;
}

static void join_rule(Join *j, const Rule *r, uint32_t total) {
    j->r = r;
    if (j->P->reuse) {
        join_lits(j, 0u, 0u, 0u);
        return;
    }
    if (r->nderived > total || (r->nderived == 0u && total != 0u))
        return;
    join_lits(j, 0u, total, r->nderived);
}

/* the item a completed join derives: the head over the bound variables */
static void head_tuple(const Join *j, uint32_t *t) {
    memset(t, 0, sizeof(uint32_t) * STRIDE);
    t[0] = j->r->head.pred;
    for (uint32_t k = 0; k < j->r->head.arity; k++)
        t[1 + k] = j->vars[j->r->head.args[k]];
}

/* ---------- preparing a query ---------- */

static ProvStatus prepare(ProvProgram *P, uint32_t K) {
    reset_derived_state(P);
    if (P->bad != PROV_OK)
        return P->bad;
    if (K >= UNBOUND - 1u)
        return PROV_LIMIT;
    uint32_t np = P->npreds ? P->npreds : 1u;
    P->derived = calloc(np, 1u);
    P->rule_start = calloc((size_t)np + 1u, sizeof(uint32_t));
    P->rule_index = malloc(sizeof(uint32_t) * (P->nrules ? P->nrules : 1u));
    P->grade = calloc((size_t)K + 1u, sizeof(Rel));
    if (!P->derived || !P->rule_start || !P->rule_index || !P->grade)
        return PROV_OOM;
    P->ngrades = K;
    for (uint32_t r = 0; r < P->nrules; r++) {
        P->derived[P->rules[r].head.pred] = 1u;
        P->rule_start[P->rules[r].head.pred + 1u]++;
    }
    for (uint32_t i = 0; i < P->npreds; i++)
        P->rule_start[i + 1u] += P->rule_start[i];
    uint32_t *fill = calloc(np, sizeof(uint32_t));
    if (!fill)
        return PROV_OOM;
    for (uint32_t r = 0; r < P->nrules; r++) {
        uint32_t h = P->rules[r].head.pred;
        P->rule_index[P->rule_start[h] + fill[h]++] = r;
    }
    free(fill);
    for (uint32_t r = 0; r < P->nrules; r++) {
        Rule *rule = &P->rules[r];
        rule->nderived = 0u;
        for (uint32_t i = 0; i < rule->nlits; i++)
            rule->nderived += P->derived[rule->lits[i].pred];
        if (rule->label_lit != PROV_NO_LABEL && P->derived[rule->lits[rule->label_lit].pred])
            return PROV_BAD_LABEL;
        for (uint32_t k = 0; k < rule->head.arity; k++) {
            bool seen = false;
            for (uint32_t i = 0; i < rule->nlits && !seen; i++)
                for (uint32_t m = 0; m < rule->lits[i].arity; m++)
                    seen |= rule->lits[i].args[m] == rule->head.args[k];
            if (!seen)
                return PROV_UNSAFE_RULE;
        }
    }
    for (uint32_t id = 0; id < P->base.n; id++)
        if (P->derived[rel_tuple(&P->base, id)[0]])
            return PROV_MIXED_PRED;
    if (!rel_index(&P->base, P->maxarity))
        return PROV_OOM;
    return PROV_OK;
}

/* the items of rel matching the goal (PROV_ANY arguments open) */
typedef bool (*ItemVisit)(void *ctx, uint32_t id);

static bool goal_items(const Rel *rel, const ProvLit *goal, ItemVisit visit, void *ctx) {
    uint32_t first = UNBOUND;
    for (uint32_t k = 0; k < goal->arity; k++)
        if (goal->args[k] != PROV_ANY) {
            first = k;
            break;
        }
    uint32_t lo, hi;
    rel_range(rel, goal->pred, first, first == UNBOUND ? 0u : goal->args[first], &lo, &hi);
    const uint32_t *view = rel->view[first == UNBOUND ? 0u : first];
    for (uint32_t x = lo; x < hi; x++) {
        const uint32_t *t = rel_tuple(rel, view[x]);
        bool ok = true;
        for (uint32_t k = 0; k < goal->arity && ok; k++)
            ok = goal->args[k] == PROV_ANY || goal->args[k] == t[1 + k];
        if (ok && !visit(ctx, view[x]))
            return false;
    }
    return true;
}

static bool goal_ok(const ProvProgram *P, const ProvLit *g) {
    return g->pred < P->npreds && P->derived[g->pred] && P->arity[g->pred] == g->arity;
}

/* ---------- graded: pass 1, items by exact grade ---------- */

static void inside_done(Join *j) {
    Rel *rel = j->ctx;
    uint32_t t[STRIDE], id;
    bool fresh;
    head_tuple(j, t);
    if (!rel_add(rel, t, &id, &fresh))
        j->P->oom = true;
}

static bool inside_grade(ProvProgram *P, uint32_t g) {
    Join j = {.P = P, .done = inside_done, .ctx = &P->grade[g]};
    for (uint32_t r = 0; r < P->nrules && !P->oom; r++) {
        reset_vars(&j);
        join_rule(&j, &P->rules[r], g - 1u);
    }
    if (P->oom || !rel_index(&P->grade[g], P->maxarity))
        return false;
    P->stats.items += P->grade[g].n;
    return true;
}

/* ---------- graded: pass 2, families below the goals ---------- */

static bool fam(ProvProgram *P, uint32_t g, uint32_t id, uint32_t *out);

typedef struct {
    uint32_t self;
    uint32_t *buf;
    size_t buf_cap;
} FamBuild;

typedef struct {
    ProvProgram *P;
    uint32_t self, nchild;
    uint32_t child[MAX_LITS];
    uint32_t from[MAX_LITS], to[MAX_LITS];   /* support ranges per child */
    uint32_t *scratch;
    uint32_t width, K;                         /* K: largest support, 0 none */
    uint64_t added;
} Product;

static void product_level(Product *x, uint32_t level, const uint32_t *prefix, uint32_t np) {
    ProvProgram *P = x->P;
    if (P->oom || (x->K && np > x->K))
        return;
    if (level == x->nchild) {
        uint32_t before = P->fams[x->self].count;
        if (!ss_insert(&P->fams[x->self], prefix, np))
            P->oom = true;
        x->added += P->fams[x->self].count - before;
        return;
    }
    uint32_t *out = x->scratch + (size_t)(level + 1u) * x->width;
    for (uint32_t s = x->from[level]; s < x->to[level] && !P->oom; s++) {
        uint32_t n;
        const uint32_t *l = ss_get(&P->fams[x->child[level]], s, &n);
        uint32_t m = merge(prefix, np, l, n, out);
        product_level(x, level + 1u, out, m);
    }
}

static bool ensure_buf(FamBuild *b, size_t need) {
    if (need <= b->buf_cap)
        return true;
    uint32_t *buf = realloc(b->buf, need * sizeof(*buf));
    if (!buf)
        return false;
    b->buf = buf;
    b->buf_cap = need;
    return true;
}

static void fam_done(Join *j) {
    ProvProgram *P = j->P;
    FamBuild *b = j->ctx;
    const Rule *r = j->r;
    Product x = {.P = P, .self = b->self};
    uint32_t grades[MAX_LITS], ids[MAX_LITS];
    memcpy(grades, j->grades, sizeof(grades));
    memcpy(ids, j->ids, sizeof(ids));
    for (uint32_t i = 0; i < r->nlits; i++) {
        if (!P->derived[r->lits[i].pred])
            continue;
        uint32_t c;
        if (!fam(P, grades[i], ids[i], &c))
            return;
        x.child[x.nchild++] = c;
    }
    uint32_t width = 1u;
    for (uint32_t i = 0; i < x.nchild; i++) {
        const SupportSet *f = &P->fams[x.child[i]];
        uint32_t widest = 0;
        for (uint32_t s = 0; s < f->count; s++) {
            uint32_t n;
            (void)ss_get(f, s, &n);
            if (n > widest)
                widest = n;
        }
        width += widest;
        x.from[i] = 0u;
        x.to[i] = f->count;
    }
    if (!ensure_buf(b, (size_t)(x.nchild + 1u) * width)) {
        P->oom = true;
        return;
    }
    x.scratch = b->buf;
    x.width = width;
    x.scratch[0] = instance_label(P, r, ids);
    if (x.scratch[0] == PROV_NO_LABEL) {
        P->bad = PROV_BAD_LABEL;
        P->oom = true;
        return;
    }
    product_level(&x, 0u, x.scratch, 1u);
}

static bool fam(ProvProgram *P, uint32_t g, uint32_t id, uint32_t *out) {
    if (map_get(&P->fam_of, g, id, out))
        return true;
    if (P->nfams == P->fams_cap) {
        uint32_t cap = P->fams_cap ? P->fams_cap * 2u : 64u;
        SupportSet *f = realloc(P->fams, cap * sizeof(*f));
        if (!f) {
            P->oom = true;
            return false;
        }
        P->fams = f;
        P->fams_cap = cap;
    }
    uint32_t self = P->nfams++;
    memset(&P->fams[self], 0, sizeof(P->fams[self]));
    if (!map_put(&P->fam_of, g, id, self)) {
        P->oom = true;
        return false;
    }
    P->stats.family_items++;
    uint32_t t[STRIDE];
    memcpy(t, rel_tuple(&P->grade[g], id), sizeof(t));
    FamBuild b = {.self = self};
    Join j = {.P = P, .done = fam_done, .ctx = &b};
    for (uint32_t i = P->rule_start[t[0]]; i < P->rule_start[t[0] + 1u] && !P->oom; i++) {
        const Rule *r = &P->rules[P->rule_index[i]];
        reset_vars(&j);
        if (bind_head(&j, r, t))
            join_rule(&j, r, g - 1u);
    }
    free(b.buf);
    if (P->oom)
        return false;
    P->stats.supports += P->fams[self].count;
    *out = self;
    return true;
}

typedef struct {
    ProvProgram *P;
    uint32_t g;
    SupportSet *answer;
} GoalCollect;

static bool collect_graded(void *ctx, uint32_t id) {
    GoalCollect *c = ctx;
    uint32_t f;
    if (!fam(c->P, c->g, id, &f))
        return false;
    const SupportSet *s = &c->P->fams[f];
    for (uint32_t i = 0; i < s->count; i++) {
        uint32_t n;
        const uint32_t *l = ss_get(s, i, &n);
        if (!ss_insert(c->answer, l, n)) {
            c->P->oom = true;
            return false;
        }
    }
    return true;
}

static ProvStatus emit_all(SupportSet *answers, uint32_t ngroups, ProvGroupEmit emit, void *ctx) {
    for (uint32_t g = 0; g < ngroups; g++)
        for (uint32_t i = 0; i < answers[g].count; i++) {
            uint32_t n;
            const uint32_t *l = ss_get(&answers[g], i, &n);
            if (!emit(ctx, g, l, n))
                return PROV_STOPPED;
        }
    return PROV_OK;
}

static void free_answers(SupportSet *answers, uint32_t ngroups) {
    for (uint32_t g = 0; answers && g < ngroups; g++)
        ss_free(&answers[g]);
    free(answers);
}

static ProvStatus supports_graded(ProvProgram *P, const ProvLit *goals, const uint32_t *group,
                                  uint32_t ngoals, uint32_t ngroups, uint32_t K,
                                  ProvGroupEmit emit, void *ctx) {
    ProvStatus st = prepare(P, K);
    if (st != PROV_OK)
        return st;
    for (uint32_t g = 1u; g <= K; g++)
        if (!inside_grade(P, g))
            return PROV_OOM;
    SupportSet *answers = calloc(ngroups ? ngroups : 1u, sizeof(SupportSet));
    if (!answers)
        return PROV_OOM;
    for (uint32_t gi = 0; gi < ngoals; gi++) {
        if (!goal_ok(P, &goals[gi]))
            continue;
        for (uint32_t g = 1u; g <= K; g++) {
            GoalCollect c = {P, g, &answers[group ? group[gi] : 0u]};
            if (!goal_items(&P->grade[g], &goals[gi], collect_graded, &c)) {
                free_answers(answers, ngroups);
                return PROV_OOM;
            }
        }
    }
    st = emit_all(answers, ngroups, emit, ctx);
    free_answers(answers, ngroups);
    return st;
}

/* ---------- with labels: a least fixpoint below the goals ---------- */

static void reuse_inside_done(Join *j) {
    bool *grew = j->ctx;
    uint32_t t[STRIDE], id;
    bool fresh;
    head_tuple(j, t);
    if (!rel_add(&j->P->cur, t, &id, &fresh))
        j->P->oom = true;
    else if (fresh)
        *grew = true;
}

typedef struct { uint32_t head, label, nchild, first; } Hyper;

typedef struct {
    ProvProgram *P;
    Hyper *edges;
    uint32_t nedges, edges_cap;
    uint32_t *pool;
    uint32_t npool, pool_cap;
    uint8_t *relevant;
    uint32_t *queue;
    uint32_t qtail;
    uint32_t self;
} Relevance;

static bool relevance_enqueue(Relevance *R, uint32_t id) {
    if (!R->relevant[id]) {
        R->relevant[id] = 1u;
        R->queue[R->qtail++] = id;
    }
    return true;
}

static bool relevance_goal(void *ctx, uint32_t id) {
    return relevance_enqueue(ctx, id);
}

static void relevance_done(Join *j) {
    Relevance *R = j->ctx;
    ProvProgram *P = j->P;
    const Rule *r = j->r;
    if (R->nedges == R->edges_cap) {
        uint32_t cap = R->edges_cap ? R->edges_cap * 2u : 64u;
        Hyper *e = realloc(R->edges, cap * sizeof(*e));
        if (!e) { P->oom = true; return; }
        R->edges = e;
        R->edges_cap = cap;
    }
    if (R->npool + r->nlits > R->pool_cap) {
        uint32_t cap = R->pool_cap ? R->pool_cap * 2u : 256u;
        while (cap < R->npool + r->nlits)
            cap *= 2u;
        uint32_t *pool = realloc(R->pool, cap * sizeof(*pool));
        if (!pool) { P->oom = true; return; }
        R->pool = pool;
        R->pool_cap = cap;
    }
    Hyper *h = &R->edges[R->nedges++];
    uint32_t label = instance_label(P, r, j->ids);
    if (label == PROV_NO_LABEL) {
        P->bad = PROV_BAD_LABEL;
        P->oom = true;
        return;
    }
    *h = (Hyper){R->self, label, 0u, R->npool};
    for (uint32_t i = 0; i < r->nlits; i++) {
        if (!P->derived[r->lits[i].pred])
            continue;
        R->pool[R->npool++] = j->ids[i];
        h->nchild++;
        relevance_enqueue(R, j->ids[i]);
    }
}

typedef struct {
    ProvProgram *P;
    SupportSet *answer;
} ReuseCollect;

static bool collect_reuse(void *ctx, uint32_t id) {
    ReuseCollect *c = ctx;
    const SupportSet *s = &c->P->fams[id];
    for (uint32_t i = 0; i < s->count; i++) {
        uint32_t n;
        const uint32_t *l = ss_get(s, i, &n);
        if (!ss_insert(c->answer, l, n)) {
            c->P->oom = true;
            return false;
        }
    }
    return true;
}

static ProvStatus supports_reuse(ProvProgram *P, const ProvLit *goals, const uint32_t *group,
                                 uint32_t ngoals, uint32_t ngroups, uint32_t K,
                                 ProvGroupEmit emit, void *ctx) {
    ProvStatus st = prepare(P, 0u);
    if (st != PROV_OK)
        return st;
    P->reuse = true;
    Relevance R = {.P = P};
    uint32_t *lo = NULL, *hi = NULL, *scratch = NULL;
    SupportSet *answers = calloc(ngroups ? ngroups : 1u, sizeof(SupportSet));
    st = PROV_OOM;
    if (!answers)
        goto done;
    /* 1. the derivable items, ungraded: rounds until none is new */
    bool grew;
    do {
        grew = false;
        if (!rel_index(&P->cur, P->maxarity))
            goto done;
        Join j = {.P = P, .done = reuse_inside_done, .ctx = &grew};
        for (uint32_t r = 0; r < P->nrules && !P->oom; r++) {
            reset_vars(&j);
            join_rule(&j, &P->rules[r], 0u);
        }
        if (P->oom)
            goto done;
    } while (grew);
    uint32_t n = P->cur.n;
    P->stats.items = n;
    /* 2. the hyperedges below the goals */
    R.relevant = calloc(n ? n : 1u, 1u);
    R.queue = malloc(sizeof(uint32_t) * (n ? n : 1u));
    if (!R.relevant || !R.queue)
        goto done;
    for (uint32_t gi = 0; gi < ngoals; gi++)
        if (goal_ok(P, &goals[gi]))
            goal_items(&P->cur, &goals[gi], relevance_goal, &R);
    for (uint32_t q = 0; q < R.qtail && !P->oom; q++) {
        uint32_t id = R.queue[q];
        uint32_t t[STRIDE];
        memcpy(t, rel_tuple(&P->cur, id), sizeof(t));
        R.self = id;
        Join j = {.P = P, .done = relevance_done, .ctx = &R};
        for (uint32_t i = P->rule_start[t[0]]; i < P->rule_start[t[0] + 1u] && !P->oom; i++) {
            const Rule *r = &P->rules[P->rule_index[i]];
            reset_vars(&j);
            if (bind_head(&j, r, t))
                join_rule(&j, r, 0u);
        }
    }
    if (P->oom)
        goto done;
    P->stats.family_items = R.qtail;
    /* 3. families, semi-naively: each round combines at least one support the
     * previous round added */
    P->fams = calloc(n ? n : 1u, sizeof(SupportSet));
    lo = calloc(n ? n : 1u, sizeof(uint32_t));
    hi = calloc(n ? n : 1u, sizeof(uint32_t));
    uint32_t width = 2u * K + 2u;
    scratch = malloc(sizeof(uint32_t) * (size_t)(MAX_LITS + 1u) * width);
    if (!P->fams || !lo || !hi || !scratch)
        goto done;
    P->nfams = P->fams_cap = n;
    bool first = true;
    for (;;) {
        Product x = {.P = P, .K = K, .scratch = scratch, .width = width};
        for (uint32_t e = 0; e < R.nedges && !P->oom; e++) {
            const Hyper *h = &R.edges[e];
            x.self = h->head;
            x.nchild = h->nchild;
            scratch[0] = h->label;
            if (h->nchild == 0u) {
                if (first)
                    product_level(&x, 0u, scratch, 1u);
                continue;
            }
            for (uint32_t i = 0; i < h->nchild; i++)
                x.child[i] = R.pool[h->first + i];
            for (uint32_t d = 0; d < h->nchild; d++) {
                uint32_t cd = x.child[d];
                if (lo[cd] == hi[cd])
                    continue;
                for (uint32_t i = 0; i < h->nchild; i++) {
                    uint32_t c = x.child[i];
                    x.from[i] = i == d ? lo[c] : 0u;
                    x.to[i] = i < d ? lo[c] : hi[c];
                }
                product_level(&x, 0u, scratch, 1u);
            }
        }
        if (P->oom)
            goto done;
        first = false;
        bool changed = false;
        for (uint32_t f = 0; f < n; f++) {
            lo[f] = hi[f];
            hi[f] = P->fams[f].count;
            changed |= lo[f] != hi[f];
        }
        if (!changed)
            break;
    }
    for (uint32_t f = 0; f < n; f++)
        P->stats.supports += P->fams[f].count;
    /* 4. the goals' supports */
    for (uint32_t gi = 0; gi < ngoals; gi++) {
        ReuseCollect c = {P, &answers[group ? group[gi] : 0u]};
        if (goal_ok(P, &goals[gi]) && !goal_items(&P->cur, &goals[gi], collect_reuse, &c))
            goto done;
    }
    st = emit_all(answers, ngroups, emit, ctx);
done:
    free_answers(answers, ngroups);
    free(scratch);
    free(lo);
    free(hi);
    free(R.edges);
    free(R.pool);
    free(R.relevant);
    free(R.queue);
    return st;
}

/* one group: the plain emitter */
typedef struct { ProvEmit emit; void *ctx; } OneGroup;
static bool one_group_emit(void *ctx, uint32_t group, const uint32_t *labels, uint32_t n) {
    (void)group;
    OneGroup *o = ctx;
    return o->emit(o->ctx, labels, n);
}

/* a malformed program found while computing is reported as such */
static ProvStatus reported(const ProvProgram *P, ProvStatus st) {
    return st != PROV_OK && P->bad != PROV_OK ? P->bad : st;
}

ProvStatus prov_supports_graded(ProvProgram *P, const ProvLit *goals,
                                uint32_t ngoals, uint32_t K,
                                ProvEmit emit, void *ctx) {
    OneGroup o = {emit, ctx};
    return reported(P, supports_graded(P, goals, NULL, ngoals, 1u, K, one_group_emit, &o));
}

ProvStatus prov_supports_reuse(ProvProgram *P, const ProvLit *goals,
                               uint32_t ngoals, uint32_t K,
                               ProvEmit emit, void *ctx) {
    OneGroup o = {emit, ctx};
    return reported(P, supports_reuse(P, goals, NULL, ngoals, 1u, K, one_group_emit, &o));
}

ProvStatus prov_supports_graded_groups(ProvProgram *P, const ProvLit *goals, const uint32_t *group,
                                       uint32_t ngoals, uint32_t ngroups, uint32_t K,
                                       ProvGroupEmit emit, void *ctx) {
    for (uint32_t i = 0; i < ngoals; i++)
        if (group[i] >= ngroups)
            return PROV_LIMIT;
    return reported(P, supports_graded(P, goals, group, ngoals, ngroups, K, emit, ctx));
}

ProvStatus prov_supports_reuse_groups(ProvProgram *P, const ProvLit *goals, const uint32_t *group,
                                      uint32_t ngoals, uint32_t ngroups, uint32_t K,
                                      ProvGroupEmit emit, void *ctx) {
    for (uint32_t i = 0; i < ngoals; i++)
        if (group[i] >= ngroups)
            return PROV_LIMIT;
    return reported(P, supports_reuse(P, goals, group, ngoals, ngroups, K, emit, ctx));
}

void prov_stats(const ProvProgram *p, ProvStats *out) {
    *out = p->stats;
}
