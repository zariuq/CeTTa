#define _POSIX_C_SOURCE 200809L
/* ===========================================================================
 * gslt2parse.c -- a generic tabled Horn chart engine.
 *
 * The presentation is the ONLY authority.  This file contains no grammar
 * combinator, no lexer, no token table, no start-category field, and no guest
 * language.  It reads (rule <id> (head <H>) (body <G>...)) forms and evaluates
 * them by unification and tabled fixpoint saturation.
 *
 * The answer table IS the chart.  Each answer node carries the set of
 * derivations that produced it (its "packings"), which is the SPPF: duplicate
 * derivations of the same value share one node, distinct values stay distinct.
 *
 * Build:  cc -O2 -o gslt2parse gslt2parse.c
 * ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ------------------------------------------------------------------ terms */
enum { T_SYM, T_INT, T_VAR, T_APP };

typedef struct {
    int tag;
    int sym;      /* symbol id, integer value, or variable id */
    int nkids;
    int *kids;
} Node;

static Node *nodes = NULL;
static int nnodes = 0, capnodes = 0;

static int newnode(int tag, int sym, int nkids, int *kids) {
    if (nnodes == capnodes) {
        capnodes = capnodes ? capnodes * 2 : 4096;
        nodes = realloc(nodes, (size_t)capnodes * sizeof(Node));
        if (!nodes) { fprintf(stderr, "oom nodes\n"); exit(2); }
    }
    nodes[nnodes].tag = tag;
    nodes[nnodes].sym = sym;
    nodes[nnodes].nkids = nkids;
    nodes[nnodes].kids = kids;
    return nnodes++;
}

/* -------------------------------------------------------------- symbols */
static char **symtab = NULL;
static int nsyms = 0, capsyms = 0;

static int intern(const char *s, int len) {
    for (int i = 0; i < nsyms; i++)
        if ((int)strlen(symtab[i]) == len && !strncmp(symtab[i], s, (size_t)len))
            return i;
    if (nsyms == capsyms) {
        capsyms = capsyms ? capsyms * 2 : 1024;
        symtab = realloc(symtab, (size_t)capsyms * sizeof(char *));
    }
    symtab[nsyms] = malloc((size_t)len + 1);
    memcpy(symtab[nsyms], s, (size_t)len);
    symtab[nsyms][len] = 0;
    return nsyms++;
}
static const char *symname(int i) { return symtab[i]; }

/* ------------------------------------------------------- s-expression read */
typedef struct { const char *p, *end; } Reader;

static void skipws(Reader *r) {
    for (;;) {
        while (r->p < r->end && (unsigned char)*r->p <= ' ') r->p++;
        if (r->p < r->end && *r->p == ';') {
            while (r->p < r->end && *r->p != '\n') r->p++;
            continue;
        }
        break;
    }
}

/* variable naming: ?x in source becomes a fresh T_VAR per rule */
static int rd_term(Reader *r, int *varmap, int *nvars);

static int rd_atomtok(Reader *r, int *varmap, int *nvars) {
    const char *s = r->p;
    while (r->p < r->end && (unsigned char)*r->p > ' ' && *r->p != '(' && *r->p != ')')
        r->p++;
    int len = (int)(r->p - s);
    if (len == 0) return -1;
    /* integer? */
    int isint = 1, i0 = (s[0] == '-' && len > 1) ? 1 : 0;
    for (int i = i0; i < len; i++) if (s[i] < '0' || s[i] > '9') { isint = 0; break; }
    if (isint) {
        char buf[64];
        int n = len < 63 ? len : 63;
        memcpy(buf, s, (size_t)n); buf[n] = 0;
        return newnode(T_INT, atoi(buf), 0, NULL);
    }
    if (s[0] == '?') {                      /* source variable */
        int id = intern(s, len);
        for (int i = 0; i < *nvars; i++) if (varmap[i] == id) return newnode(T_VAR, i, 0, NULL);
        varmap[*nvars] = id;
        return newnode(T_VAR, (*nvars)++, 0, NULL);
    }
    return newnode(T_SYM, intern(s, len), 0, NULL);
}

static int rd_term(Reader *r, int *varmap, int *nvars) {
    skipws(r);
    if (r->p >= r->end) return -1;
    if (*r->p == '(') {
        r->p++;
        int cap = 8, n = 0;
        int *kids = malloc((size_t)cap * sizeof(int));
        for (;;) {
            skipws(r);
            if (r->p >= r->end) break;
            if (*r->p == ')') { r->p++; break; }
            int t = rd_term(r, varmap, nvars);
            if (t < 0) break;
            if (n == cap) { cap *= 2; kids = realloc(kids, (size_t)cap * sizeof(int)); }
            kids[n++] = t;
        }
        if (n == 0) return newnode(T_SYM, intern("()", 2), 0, NULL);
        /* (f a b) -> APP with sym = head if head is a symbol */
        if (nodes[kids[0]].tag == T_SYM && nodes[kids[0]].nkids == 0) {
            int h = nodes[kids[0]].sym;
            int *rest = malloc((size_t)(n - 1) * sizeof(int) + 1);
            for (int i = 1; i < n; i++) rest[i - 1] = kids[i];
            free(kids);
            return newnode(T_APP, h, n - 1, rest);
        }
        return newnode(T_APP, intern("#list", 5), n, kids);
    }
    if (*r->p == ')') { r->p++; return -1; }
    return rd_atomtok(r, varmap, nvars);
}

/* ------------------------------------------------------------ bindings */
#define MAXVAR 4096
static int *bind_;            /* binding array, indexed by global var slot */
static int bindcap = 0;
static int *trail; static int ntrail = 0, captrail = 0;

static void ensure_bind(int n) {
    if (n <= bindcap) return;
    int old = bindcap;
    bindcap = bindcap ? bindcap * 2 : 65536;
    while (bindcap < n) bindcap *= 2;
    bind_ = realloc(bind_, (size_t)bindcap * sizeof(int));
    for (int i = old; i < bindcap; i++) bind_[i] = -1;
}
static void tpush(int v) {
    if (ntrail == captrail) { captrail = captrail ? captrail * 2 : 4096;
        trail = realloc(trail, (size_t)captrail * sizeof(int)); }
    trail[ntrail++] = v;
}
static void undo(int mark) { while (ntrail > mark) bind_[trail[--ntrail]] = -1; }

static int deref(int t) {
    while (t >= 0 && nodes[t].tag == T_VAR) {
        int b = bind_[nodes[t].sym];
        if (b < 0) return t;
        t = b;
    }
    return t;
}

static int unify(int a, int b) {
    a = deref(a); b = deref(b);
    if (a == b) return 1;
    if (nodes[a].tag == T_VAR) { ensure_bind(nodes[a].sym + 1); bind_[nodes[a].sym] = b; tpush(nodes[a].sym); return 1; }
    if (nodes[b].tag == T_VAR) { ensure_bind(nodes[b].sym + 1); bind_[nodes[b].sym] = a; tpush(nodes[b].sym); return 1; }
    if (nodes[a].tag != nodes[b].tag) return 0;
    if (nodes[a].tag == T_SYM || nodes[a].tag == T_INT) return nodes[a].sym == nodes[b].sym;
    if (nodes[a].sym != nodes[b].sym || nodes[a].nkids != nodes[b].nkids) return 0;
    for (int i = 0; i < nodes[a].nkids; i++)
        if (!unify(nodes[a].kids[i], nodes[b].kids[i])) return 0;
    return 1;
}

/* resolve a term under the current bindings into a fresh ground-ish term */
static int resolve(int t) {
    t = deref(t);
    if (nodes[t].tag != T_APP) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    int changed = 0;
    for (int i = 0; i < n; i++) {
        kids[i] = resolve(nodes[t].kids[i]);
        if (kids[i] != nodes[t].kids[i]) changed = 1;
    }
    if (!changed) { free(kids); return t; }
    return newnode(T_APP, nodes[t].sym, n, kids);
}

/* rename a rule's variables into a fresh global block */
static int varbase = 0;
static int rename_t(int t, int base) {
    if (nodes[t].tag == T_VAR) return newnode(T_VAR, base + nodes[t].sym, 0, NULL);
    if (nodes[t].tag != T_APP) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    for (int i = 0; i < n; i++) kids[i] = rename_t(nodes[t].kids[i], base);
    return newnode(T_APP, nodes[t].sym, n, kids);
}

/* ------------------------------------------------------------- printing */
static void sprt(char **buf, int *len, int *cap, int t) {
    char tmp[64];
    t = deref(t);
    const char *s = NULL; int sl = 0;
    switch (nodes[t].tag) {
    case T_SYM: s = symname(nodes[t].sym); sl = (int)strlen(s); break;
    case T_INT: snprintf(tmp, sizeof tmp, "%d", nodes[t].sym); s = tmp; sl = (int)strlen(tmp); break;
    case T_VAR: snprintf(tmp, sizeof tmp, "_%d", nodes[t].sym); s = tmp; sl = (int)strlen(tmp); break;
    case T_APP: {
        while (*len + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
        (*buf)[(*len)++] = '(';
        const char *h = symname(nodes[t].sym); int hl = (int)strlen(h);
        while (*len + hl + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
        memcpy(*buf + *len, h, (size_t)hl); *len += hl;
        for (int i = 0; i < nodes[t].nkids; i++) {
            while (*len + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
            (*buf)[(*len)++] = ' ';
            sprt(buf, len, cap, nodes[t].kids[i]);
        }
        while (*len + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
        (*buf)[(*len)++] = ')';
        return; }
    }
    while (*len + sl + 1 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
    memcpy(*buf + *len, s, (size_t)sl); *len += sl;
}
static char *tostr(int t) {
    char *b = NULL; int len = 0, cap = 0;
    sprt(&b, &len, &cap, t);
    while (len + 1 >= cap) { cap = cap ? cap * 2 : 64; b = realloc(b, (size_t)cap); }
    b[len] = 0;
    return b;
}


/* canonical variant key: variables renumbered by first appearance, so that two
 * goals differing only in variable identity share one chart entry.  This is the
 * variant-tabling condition that makes the chart finite. */
static int ck_map[1 << 16]; static int ck_n;
static void ckwalk(char **buf, int *len, int *cap, int t) {
    t = deref(t);
    char tmp[64]; const char *s = NULL; int sl = 0;
    switch (nodes[t].tag) {
    case T_SYM: s = symname(nodes[t].sym); sl = (int)strlen(s); break;
    case T_INT: snprintf(tmp, sizeof tmp, "#%d", nodes[t].sym); s = tmp; sl = (int)strlen(tmp); break;
    case T_VAR: {
        int v = nodes[t].sym, slot = -1;
        for (int i = 0; i < ck_n; i++) if (ck_map[i] == v) { slot = i; break; }
        if (slot < 0) { if (ck_n < (1 << 16)) { ck_map[ck_n] = v; slot = ck_n++; } else slot = 0; }
        snprintf(tmp, sizeof tmp, "?%d", slot); s = tmp; sl = (int)strlen(tmp); break; }
    case T_APP: {
        while (*len + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
        (*buf)[(*len)++] = '(';
        const char *h = symname(nodes[t].sym); int hl = (int)strlen(h);
        while (*len + hl + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
        memcpy(*buf + *len, h, (size_t)hl); *len += hl;
        for (int i = 0; i < nodes[t].nkids; i++) {
            while (*len + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
            (*buf)[(*len)++] = ' ';
            ckwalk(buf, len, cap, nodes[t].kids[i]);
        }
        while (*len + 2 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
        (*buf)[(*len)++] = ')';
        return; }
    }
    while (*len + sl + 1 >= *cap) { *cap = *cap ? *cap * 2 : 256; *buf = realloc(*buf, (size_t)*cap); }
    memcpy(*buf + *len, s, (size_t)sl); *len += sl;
}
static char *variantkey(int t) {
    char *b = NULL; int len = 0, cap = 0; ck_n = 0;
    ckwalk(&b, &len, &cap, t);
    while (len + 1 >= cap) { cap = cap ? cap * 2 : 64; b = realloc(b, (size_t)cap); }
    b[len] = 0; return b;
}

/* ---------------------------------------------------------------- rules */
typedef struct {
    int id;
    int head;
    int nbody;
    int *body;
    int nvars;
    int hsym;
    int harity;
    int next_same_head;
} Rule;
static Rule *rules = NULL; static int nrules = 0, caprules = 0;
static int *rule_id_seen = NULL, rule_id_seen_cap = 0;
static char presentation_error[512];

static void set_presentation_error(const char *message) {
    if (!presentation_error[0]) {
        snprintf(presentation_error, sizeof presentation_error, "%s", message);
    }
}

static int presentation_text_well_formed(const char *text, size_t len) {
    int depth = 0, quoted = 0, escaped = 0, comment = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (comment) { if (c == '\n') comment = 0; continue; }
        if (quoted) {
            if (escaped) { escaped = 0; continue; }
            if (c == '\\') { escaped = 1; continue; }
            if (c == '"') quoted = 0;
            continue;
        }
        if (c == ';') { comment = 1; continue; }
        if (c == '"') { quoted = 1; continue; }
        if (c == '(') depth++;
        else if (c == ')' && --depth < 0) return 0;
    }
    return depth == 0 && !quoted;
}

/* Generic rule-head index.  The presentation still supplies all semantics;
 * this merely avoids trying every unrelated Horn rule for every chart goal. */
#define RULE_HSZ (1u << 16)
static int *rule_heads = NULL;
static unsigned rule_hash(int sym, int arity) {
    uint32_t x = (uint32_t)sym * 2654435761u;
    x ^= (uint32_t)arity * 2246822519u;
    return x & (RULE_HSZ - 1u);
}
static void rule_index_init(void) {
    rule_heads = malloc(RULE_HSZ * sizeof(int));
    if (!rule_heads) { fprintf(stderr, "oom rule index\n"); exit(2); }
    for (unsigned i = 0; i < RULE_HSZ; i++) rule_heads[i] = -1;
}
static void addrule(int id, int head, int nbody, int *body, int nvars) {
    if (id < 0) { set_presentation_error("rule id is not a symbol"); return; }
    if (id >= rule_id_seen_cap) {
        int old = rule_id_seen_cap;
        rule_id_seen_cap = rule_id_seen_cap ? rule_id_seen_cap * 2 : 2048;
        while (rule_id_seen_cap <= id) rule_id_seen_cap *= 2;
        rule_id_seen = realloc(rule_id_seen,
                               (size_t)rule_id_seen_cap * sizeof(int));
        if (!rule_id_seen) { fprintf(stderr, "oom rule-id index\n"); exit(2); }
        for (int i = old; i < rule_id_seen_cap; i++) rule_id_seen[i] = -1;
    }
    if (rule_id_seen[id] >= 0) {
        char message[512];
        snprintf(message, sizeof message, "duplicate rule id: %s", symname(id));
        set_presentation_error(message);
        return;
    }
    rule_id_seen[id] = nrules;
    if (nrules == caprules) { caprules = caprules ? caprules * 2 : 256;
        rules = realloc(rules, (size_t)caprules * sizeof(Rule)); }
    int hsym = -1, harity = -1;
    if (nodes[head].tag == T_APP) {
        hsym = nodes[head].sym;
        harity = nodes[head].nkids;
    } else if (nodes[head].tag == T_SYM) {
        hsym = nodes[head].sym;
        harity = 0;
    }
    rules[nrules].id = id; rules[nrules].head = head;
    rules[nrules].nbody = nbody; rules[nrules].body = body;
    rules[nrules].nvars = nvars;
    rules[nrules].hsym = hsym; rules[nrules].harity = harity;
    rules[nrules].next_same_head = -1;
    if (hsym >= 0) {
        unsigned slot = rule_hash(hsym, harity);
        rules[nrules].next_same_head = rule_heads[slot];
        rule_heads[slot] = nrules;
    }
    nrules++;
}

/* Compact a rule's variables into a local 0..n-1 range.  Source variables are
 * scoped to their own rule; numbering them per file overflows and wastes slots. */
static int cv_from[65536], cv_to[65536]; static int cv_n;
static int compact(int t) {
    if (nodes[t].tag == T_VAR) {
        for (int i = 0; i < cv_n; i++) if (cv_from[i] == nodes[t].sym) return cv_to[i];
        if (cv_n < 65536) { cv_from[cv_n] = nodes[t].sym;
            cv_to[cv_n] = newnode(T_VAR, cv_n, 0, NULL); return cv_to[cv_n++]; }
        return t;
    }
    if (nodes[t].tag != T_APP) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    for (int i = 0; i < n; i++) kids[i] = compact(nodes[t].kids[i]);
    return newnode(T_APP, nodes[t].sym, n, kids);
}

/* ------------------------------------------------------- chart / answers */
typedef struct Deriv { int ruleid; int nkids; int *kids; struct Deriv *next; } Deriv;

typedef struct {
    char *key;       /* canonical string of the instantiated goal */
    int term;        /* the answer term */
    int goalidx;     /* owning goal */
    Deriv *derivs;   /* packings: every derivation that produced this answer */
} Answer;

typedef struct {
    char *key;       /* canonical string of the call pattern */
    int pattern;     /* the goal term (with free output vars) */
    int *answers;    /* indices into ans[] */
    int nans, capans;
    int dirty;       /* needs re-expansion (semi-naive) */
    int *cons; int ncons, capcons;   /* goals that consume this one */
} Goal;

static Goal *goals = NULL; static int ngoals = 0, capgoals = 0;
static Answer *ans = NULL; static int nans = 0, capans = 0;

/* simple hash index */
#define HSZ (1 << 20)
static int *gh, *ah;
static unsigned long hashs(const char *s) {
    unsigned long h = 1469598103934665603UL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211UL; }
    return h;
}

static int find_goal(const char *key) {
    unsigned long h = hashs(key) & (HSZ - 1);
    for (int i = 0; i < HSZ; i++) {
        int idx = gh[(h + (unsigned long)i) & (HSZ - 1)];
        if (idx < 0) return -1;
        if (!strcmp(goals[idx].key, key)) return idx;
    }
    return -1;
}
static void put_goal(const char *key, int idx) {
    unsigned long h = hashs(key) & (HSZ - 1);
    for (int i = 0; i < HSZ; i++) {
        unsigned long s = (h + (unsigned long)i) & (HSZ - 1);
        if (gh[s] < 0) { gh[s] = idx; return; }
    }
}
static int find_ans(const char *key) {
    unsigned long h = hashs(key) & (HSZ - 1);
    for (int i = 0; i < HSZ; i++) {
        int idx = ah[(h + (unsigned long)i) & (HSZ - 1)];
        if (idx < 0) return -1;
        if (!strcmp(ans[idx].key, key)) return idx;
    }
    return -1;
}
static void put_ans(const char *key, int idx) {
    unsigned long h = hashs(key) & (HSZ - 1);
    for (int i = 0; i < HSZ; i++) {
        unsigned long s = (h + (unsigned long)i) & (HSZ - 1);
        if (ah[s] < 0) { ah[s] = idx; return; }
    }
}

static int changed = 0;
static void add_consumer(int producer, int consumer) {
    for (int i = 0; i < goals[producer].ncons; i++)
        if (goals[producer].cons[i] == consumer) return;
    if (goals[producer].ncons == goals[producer].capcons) {
        goals[producer].capcons = goals[producer].capcons ? goals[producer].capcons * 2 : 4;
        goals[producer].cons = realloc(goals[producer].cons,
                                       (size_t)goals[producer].capcons * sizeof(int));
    }
    goals[producer].cons[goals[producer].ncons++] = consumer;
}
static void mark_consumers(int g) {
    for (int i = 0; i < goals[g].ncons; i++) goals[goals[g].cons[i]].dirty = 1;
}
static long derivations = 0;

static int demand(int pattern) {           /* returns goal index */
    char *k = variantkey(pattern);
    int g = find_goal(k);
    if (g >= 0) { free(k); return g; }
    if (ngoals == capgoals) { capgoals = capgoals ? capgoals * 2 : 1024;
        goals = realloc(goals, (size_t)capgoals * sizeof(Goal)); }
    goals[ngoals].key = k;
    goals[ngoals].pattern = resolve(pattern);
    goals[ngoals].answers = NULL; goals[ngoals].nans = goals[ngoals].capans = 0;
    goals[ngoals].dirty = 1;
    goals[ngoals].cons = NULL; goals[ngoals].ncons = goals[ngoals].capcons = 0;
    put_goal(k, ngoals);
    changed = 1;
    return ngoals++;
}

static void add_answer(int g, int term, int ruleid, int nk, int *kids) {
    int rt = resolve(term);
    char *k = tostr(rt);
    int a = find_ans(k);
    if (a < 0) {
        if (nans == capans) { capans = capans ? capans * 2 : 4096;
            ans = realloc(ans, (size_t)capans * sizeof(Answer)); }
        ans[nans].key = k; ans[nans].term = rt; ans[nans].goalidx = g; ans[nans].derivs = NULL;
        put_ans(k, nans);
        a = nans++;
        if (goals[g].nans == goals[g].capans) {
            goals[g].capans = goals[g].capans ? goals[g].capans * 2 : 4;
            goals[g].answers = realloc(goals[g].answers, (size_t)goals[g].capans * sizeof(int));
        }
        goals[g].answers[goals[g].nans++] = a;
        changed = 1; mark_consumers(g);
    } else {
        free(k);
        /* already known: record an additional packing only if this goal owns it */
        int seen = 0;
        for (int i = 0; i < goals[g].nans; i++) if (goals[g].answers[i] == a) { seen = 1; break; }
        if (!seen) {
            if (goals[g].nans == goals[g].capans) {
                goals[g].capans = goals[g].capans ? goals[g].capans * 2 : 4;
                goals[g].answers = realloc(goals[g].answers, (size_t)goals[g].capans * sizeof(int));
            }
            goals[g].answers[goals[g].nans++] = a;
            changed = 1; mark_consumers(g);
        }
    }
    /* record the derivation (packing) if new */
    for (Deriv *d = ans[a].derivs; d; d = d->next) {
        if (d->ruleid == ruleid && d->nkids == nk) {
            int same = 1;
            for (int i = 0; i < nk; i++) if (d->kids[i] != kids[i]) { same = 0; break; }
            if (same) { free(kids); return; }
        }
    }
    Deriv *d = malloc(sizeof(Deriv));
    d->ruleid = ruleid; d->nkids = nk; d->kids = kids; d->next = ans[a].derivs;
    ans[a].derivs = d;
}

/* solve body goals left-to-right against the table */
static void solve_body(int g, int headterm, int ruleid, Rule *r, int i, int *kids) {
    if (i == r->nbody) {
        int *kc = malloc((size_t)(r->nbody ? r->nbody : 1) * sizeof(int));
        for (int j = 0; j < r->nbody; j++) kc[j] = kids[j];
        derivations++;
        add_answer(g, headterm, ruleid, r->nbody, kc);
        return;
    }
    int sub = resolve(r->body[i]);
    int sg = demand(sub);
    add_consumer(sg, g);
    int n = goals[sg].nans;
    for (int j = 0; j < n; j++) {
        int a = goals[sg].answers[j];
        int mark = ntrail;
        if (unify(sub, ans[a].term)) {
            kids[i] = a;
            solve_body(g, headterm, ruleid, r, i + 1, kids);
        }
        undo(mark);
    }
}

static long node_limit = 40000000;
static int oom_hit = 0;
static void round_once(void) {
    int ng = ngoals;                    /* snapshot: new goals handled next round */
    for (int g = 0; g < ng; g++) {
        if (!goals[g].dirty) continue;
        goals[g].dirty = 0;
        if (nnodes > node_limit) { oom_hit = 1; return; }
        int pat = goals[g].pattern;
        int psym = -1, parity = -1;
        if (nodes[pat].tag == T_APP) { psym = nodes[pat].sym; parity = nodes[pat].nkids; }
        else if (nodes[pat].tag == T_SYM) { psym = nodes[pat].sym; parity = 0; }
        int ri = (psym >= 0) ? rule_heads[rule_hash(psym, parity)] : -1;
        for (; ri >= 0; ri = rules[ri].next_same_head) {
            Rule *r = &rules[ri];
            if (r->hsym != psym || r->harity != parity) continue; /* hash collision */
            int mark = ntrail;
            int base = varbase; varbase += r->nvars + 1;
            ensure_bind(varbase + 1);
            int h = rename_t(r->head, base);
            if (unify(h, pat)) {
                Rule rr = *r;
                int *bodyr = malloc((size_t)(r->nbody ? r->nbody : 1) * sizeof(int));
                for (int i = 0; i < r->nbody; i++) bodyr[i] = rename_t(r->body[i], base);
                rr.body = bodyr;
                int *kids = malloc((size_t)(r->nbody ? r->nbody : 1) * sizeof(int));
                if (!kids) { fprintf(stderr, "oom derivation kids\n"); exit(2); }
                solve_body(g, h, r->id, &rr, 0, kids);
                free(kids);
                free(bodyr);
            }
            undo(mark);
        }
    }
}

/* ------------------------------------------------------------ presentation */
static int sym_rule, sym_head, sym_body;

static void load_forms(const char *text, size_t len) {
    Reader r = { text, text + len };
    for (;;) {
        skipws(&r);
        if (r.p >= r.end) break;
        static int *varmap = NULL; if (!varmap) varmap = malloc(sizeof(int) * 4000000);
        int nv = 0;
        int t = rd_term(&r, varmap, &nv);
        if (t < 0) break;
        /* walk for (rule id (head H) (body G...)) anywhere in the form */
        /* iterative stack walk */
        static int *stack = NULL; static int stkcap = 0;
        int sp = 0;
        if (!stack) { stkcap = 1 << 16; stack = malloc((size_t)stkcap * sizeof(int)); }
        stack[sp++] = t;
        while (sp) {
            int c = stack[--sp];
            if (nodes[c].tag == T_APP) {
                if (nodes[c].sym == sym_rule) {
                    if (nodes[c].nkids != 3) {
                        set_presentation_error("rule must have id, head, and body fields");
                        return;
                    }
                    int idt = nodes[c].kids[0];
                    int hd = nodes[c].kids[1], bd = nodes[c].kids[2];
                    if (nodes[idt].tag != T_SYM ||
                        nodes[hd].tag != T_APP || nodes[hd].sym != sym_head ||
                        nodes[hd].nkids != 1 || nodes[bd].tag != T_APP ||
                        nodes[bd].sym != sym_body) {
                        set_presentation_error("malformed rule id/head/body field");
                        return;
                    }
                    int nb = nodes[bd].nkids;
                    int *body = malloc((size_t)(nb ? nb : 1) * sizeof(int));
                    cv_n = 0;
                    int chead = compact(nodes[hd].kids[0]);
                    for (int i = 0; i < nb; i++) body[i] = compact(nodes[bd].kids[i]);
                    addrule(nodes[idt].sym, chead, nb, body, cv_n);
                    if (presentation_error[0]) return;
                    continue;
                }
                for (int i = 0; i < nodes[c].nkids; i++) {
                    if (sp + 2 >= stkcap) { stkcap *= 2; stack = realloc(stack, (size_t)stkcap * sizeof(int)); }
                    stack[sp++] = nodes[c].kids[i];
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- input */
/* position of each input suffix node, so every answer carries an exact span */
static int *suffix_node = NULL; static int nsuffix = 0;
static char **suffix_key = NULL;
/* Compare canonically: resolve() creates fresh nodes for the same suffix, so
 * node identity is not a valid position test. */
static int posof(int t) {
    if (!suffix_key) return -1;
    char *k = tostr(t);
    for (int i = 0; i < nsuffix; i++)
        if (suffix_key[i] && !strcmp(suffix_key[i], k)) { free(k); return i; }
    free(k); return -1;
}
/* The admitted presentation currently chooses UTF-8 code units as its finite
 * scannerless alphabet.  Preserve every unit exactly; do not claim that this
 * byte-oriented boundary has decoded Unicode scalar values. */
static int encode_code_units(const char *s, size_t n) {
    suffix_node = malloc((n + 2) * sizeof(int)); nsuffix = (int)n + 1;
    int lst = newnode(T_SYM, intern("nil", 3), 0, NULL);
    suffix_node[n] = lst;
    for (size_t i = n; i > 0; i--) {
        int *cpk = malloc(sizeof(int));
        cpk[0] = newnode(T_INT, (unsigned char)s[i - 1], 0, NULL);
        int cp = newnode(T_APP, intern("cp", 2), 1, cpk);
        int *ck = malloc(2 * sizeof(int));
        ck[0] = cp; ck[1] = lst;
        lst = newnode(T_APP, intern("cons", 4), 2, ck);
        suffix_node[i - 1] = lst;
    }
    suffix_key = malloc((n + 2) * sizeof(char *));
    for (size_t i = 0; i <= n; i++) suffix_key[i] = tostr(suffix_node[i]);
    return lst;
}

/* --------------------------------------------------------- certificates */
static void span_of(int a, int *s, int *e) {
    int g = ans[a].term; *s = -1; *e = -1;
    if (nodes[g].tag == T_APP && nodes[g].nkids >= 4) {
        *s = posof(nodes[g].kids[1]);
        *e = posof(nodes[g].kids[3]);
    }
}
static void print_cert(FILE *f, int a, int depth) {
    Deriv *d = ans[a].derivs;
    int s, e; span_of(a, &s, &e);
    if (!d) { fputs("(cert ? -1 -1 ())", f); return; }
    if (depth > 16384) {
        fprintf(stderr, "certificate depth exceeds 16384; refusing a truncated certificate\n");
        exit(2);
    }
    fprintf(f, "(cert %s %d %d (", symname(d->ruleid), s, e);
    for (int i = 0; i < d->nkids; i++) {
        if (i) fputc(' ', f);
        print_cert(f, d->kids[i], depth + 1);
    }
    fputs("))", f);
}

/* --------------------------------------------------------------- replay
 * Re-derive a certificate from the admitted rules.  This does not compare
 * strings: it re-applies each cited rule, checks the span it claims, and
 * recurses into the body goals.  Any mutated field breaks one of those checks.
 */
static int sym_cert;
static int replay(int cert, int goal, char *why, size_t wn) {
    cert = deref(cert);
    if (nodes[cert].tag != T_APP || nodes[cert].sym != sym_cert || nodes[cert].nkids != 4) {
        snprintf(why, wn, "malformed certificate node"); return 0; }
    int idn = deref(nodes[cert].kids[0]);
    int stn = deref(nodes[cert].kids[1]);
    int enn = deref(nodes[cert].kids[2]);
    int kidsn = deref(nodes[cert].kids[3]);
    if (nodes[idn].tag != T_SYM) { snprintf(why, wn, "rule id not a symbol"); return 0; }
    int want_start = (nodes[stn].tag == T_INT) ? nodes[stn].sym : -1;
    int want_end   = (nodes[enn].tag == T_INT) ? nodes[enn].sym : -1;

    int nk = (nodes[kidsn].tag == T_APP) ? nodes[kidsn].nkids : 0;
    int *kidv = NULL;
    if (nk) {
        kidv = malloc((size_t)nk * sizeof(int));
        /* the kids list is parsed as (#list c1 c2 ...) or (cert ...) if single */
        if (nodes[kidsn].sym == sym_cert) { nk = 1; kidv[0] = kidsn; }
        else for (int i = 0; i < nk; i++) kidv[i] = nodes[kidsn].kids[i];
    }

    for (int ri = 0; ri < nrules; ri++) {
        if (rules[ri].id != nodes[idn].sym) continue;
        int mark = ntrail;
        int base = varbase; varbase += rules[ri].nvars + 1;
        ensure_bind(varbase + 1);
        int h = rename_t(rules[ri].head, base);
        if (!unify(h, goal)) { undo(mark); continue; }
        /* A Horn rule may bind its output/remainder only through its body.
         * Check child proofs first, then verify the span of the fully
         * instantiated conclusion.  Checking here, before the premises, would
         * reject honest certificates whose end position is still a rule-local
         * variable at rule entry. */
        if (nk != rules[ri].nbody) {
            snprintf(why, wn, "arity mismatch for %s: certificate has %d children, rule has %d",
                     symname(nodes[idn].sym), nk, rules[ri].nbody);
            undo(mark); free(kidv); return 0; }
        int ok = 1;
        for (int i = 0; i < rules[ri].nbody && ok; i++) {
            int sub = resolve(rename_t(rules[ri].body[i], base));
            if (!replay(kidv[i], sub, why, wn)) ok = 0;
        }
        if (ok) {
            int gg = resolve(goal);
            int s = -1, e = -1;
            if (nodes[gg].tag == T_APP && nodes[gg].nkids >= 4) {
                s = posof(nodes[gg].kids[1]);
                e = posof(nodes[gg].kids[3]);
            }
            /* Spans are checked for EVERY node.  A goal with no input span
             * must claim -1/-1; otherwise an unchecked field could be forged. */
            if (s != want_start || e != want_end) {
                snprintf(why, wn,
                         "span mismatch: claimed %d..%d, actual %d..%d",
                         want_start, want_end, s, e);
                ok = 0;
            }
        }
        if (ok) { free(kidv); return 1; }
        undo(mark);
        free(kidv);
        return 0;
    }
    snprintf(why, wn, "no admitted rule named %s applies here", symname(nodes[idn].sym));
    free(kidv);
    return 0;
}


/* ------------------------------------------------------ direct interpreter
 * A deliberately simple, non-tabled Horn interpreter used only as an
 * independent semantic falsifier.  It reads the same admitted rules as the
 * packed chart but does not consult Goal/Answer tables or packed derivations.
 * Productive left recursion is therefore outside this mode's completeness
 * envelope; input-consuming reader presentations terminate in this mode.
 */
static char **direct_keys = NULL;
static int ndirect = 0, capdirect = 0;
static long direct_steps = 0, direct_step_limit = 10000000;
static int direct_depth_limit = 20000;
static double direct_timeout = 600.0;
static clock_t direct_t0;
static int direct_stop = 0; /* 0 none, 1 timeout, 2 step, 3 depth */

static void direct_record(int topgoal) {
    int at = resolve(topgoal);
    int val = (nodes[at].tag == T_APP && nodes[at].nkids >= 3) ? nodes[at].kids[2] : at;
    char *key = tostr(val);
    for (int i = 0; i < ndirect; i++) {
        if (!strcmp(direct_keys[i], key)) { free(key); return; }
    }
    if (ndirect == capdirect) {
        capdirect = capdirect ? capdirect * 2 : 16;
        direct_keys = realloc(direct_keys, (size_t)capdirect * sizeof(char *));
        if (!direct_keys) { fprintf(stderr, "oom direct answers\n"); exit(2); }
    }
    direct_keys[ndirect++] = key;
}

static void direct_solve_goals(int *gs, int ng, int depth, int topgoal) {
    if (direct_stop) return;
    if (++direct_steps > direct_step_limit) { direct_stop = 2; return; }
    if (depth > direct_depth_limit) { direct_stop = 3; return; }
    if ((double)(clock() - direct_t0) / CLOCKS_PER_SEC > direct_timeout) {
        direct_stop = 1; return;
    }
    if (ng == 0) { direct_record(topgoal); return; }

    int goal = deref(gs[0]);
    int psym = -1, parity = -1;
    if (nodes[goal].tag == T_APP) { psym = nodes[goal].sym; parity = nodes[goal].nkids; }
    else if (nodes[goal].tag == T_SYM) { psym = nodes[goal].sym; parity = 0; }
    int ri = (psym >= 0) ? rule_heads[rule_hash(psym, parity)] : -1;
    for (; ri >= 0 && !direct_stop; ri = rules[ri].next_same_head) {
        Rule *r = &rules[ri];
        if (r->hsym != psym || r->harity != parity) continue;
        int mark = ntrail;
        int base = varbase; varbase += r->nvars + 1;
        ensure_bind(varbase + 1);
        int h = rename_t(r->head, base);
        if (unify(h, goal)) {
            int total = r->nbody + ng - 1;
            int *next = malloc((size_t)(total ? total : 1) * sizeof(int));
            if (!next) { fprintf(stderr, "oom direct goals\n"); exit(2); }
            for (int i = 0; i < r->nbody; i++) next[i] = rename_t(r->body[i], base);
            for (int i = 1; i < ng; i++) next[r->nbody + i - 1] = gs[i];
            direct_solve_goals(next, total, depth + 1, topgoal);
            free(next);
        }
        undo(mark);
    }
}

static void direct_sort(void) {
    for (int i = 1; i < ndirect; i++) {
        char *v = direct_keys[i]; int j = i - 1;
        while (j >= 0 && strcmp(direct_keys[j], v) > 0) {
            direct_keys[j + 1] = direct_keys[j]; j--;
        }
        direct_keys[j + 1] = v;
    }
}

/* ------------------------------------------------------------------ main */
static char *slurp(const char *path, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(3); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t rd = fread(b, 1, (size_t)n, f);
    b[rd] = 0; *outlen = rd; fclose(f);
    return b;
}

int main(int argc, char **argv) {
    const char *root = NULL, *infile = NULL, *intext = NULL, *replay_arg = NULL;
    const char *pres[64]; int npres = 0;
    int show_certs = 0, summary_only = 0, interpreted = 0, max_rounds = 100000;
    double timeout = 600.0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--input-file") && i + 1 < argc) infile = argv[++i];
        else if (!strcmp(argv[i], "--input-text") && i + 1 < argc) intext = argv[++i];
        else if (!strcmp(argv[i], "--certs")) show_certs = 1;
        else if (!strcmp(argv[i], "--summary")) summary_only = 1;
        else if (!strcmp(argv[i], "--interpreted")) interpreted = 1;
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay_arg = argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout = atof(argv[++i]);
        else if (!strcmp(argv[i], "--max-rounds") && i + 1 < argc) max_rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-nodes") && i + 1 < argc) node_limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--max-depth") && i + 1 < argc) direct_depth_limit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-steps") && i + 1 < argc) direct_step_limit = atol(argv[++i]);
        else if (npres < 64) pres[npres++] = argv[i];
    }
    if (!npres || !root) {
        fprintf(stderr,
            "usage: gslt2parse <presentation-file>... --root <name>\n"
            "                  (--input-file <f> | --input-text <s>) [--certs] [--summary]\n"
            "                  [--interpreted] [--timeout SEC] [--max-rounds N]\n"
            "                  [--max-nodes N] [--max-depth N] [--max-steps N]\n");
        return 1;
    }

    gh = malloc(sizeof(int) * HSZ); ah = malloc(sizeof(int) * HSZ);
    for (int i = 0; i < HSZ; i++) { gh[i] = -1; ah[i] = -1; }
    rule_index_init();
    ensure_bind(65536);

    sym_rule = intern("rule", 4); sym_head = intern("head", 4); sym_body = intern("body", 4);
    sym_cert = intern("cert", 4);

    for (int i = 0; i < npres; i++) {
        size_t n; char *t = slurp(pres[i], &n);
        if (!presentation_text_well_formed(t, n)) {
            set_presentation_error("unbalanced or unterminated presentation syntax");
            free(t);
            break;
        }
        load_forms(t, n);
        free(t);
        if (presentation_error[0]) break;
    }
    if (presentation_error[0]) {
        printf("{\"outcome\":\"MalformedPresentation\",\"reason\":\"%s\"}\n",
               presentation_error);
        return 0;
    }
    if (nrules == 0) { printf("{\"outcome\":\"MalformedPresentation\",\"reason\":\"no rules\"}\n"); return 0; }

    /* input */
    char *text = NULL; size_t tlen = 0;
    if (infile) text = slurp(infile, &tlen);
    else if (intext) { text = strdup(intext); tlen = strlen(text); }
    else { text = strdup(""); tlen = 0; }
    int input = encode_code_units(text, tlen);

    /* top goal: (parse (ref ROOT) INPUT ?v nil)  -- built from ordinary terms.
       The relation name and shape come from the presentation's own rules; we
       only supply the root name and the input the user asked about. */
    int rootsym = intern(root, (int)strlen(root));
    int *refk = malloc(sizeof(int)); refk[0] = newnode(T_SYM, rootsym, 0, NULL);
    int refroot = newnode(T_APP, intern("ref", 3), 1, refk);
    int vvar = newnode(T_VAR, 1000000, 0, NULL);
    ensure_bind(1000002);
    int nilN = newnode(T_SYM, intern("nil", 3), 0, NULL);
    int *gk = malloc(4 * sizeof(int));
    gk[0] = refroot; gk[1] = input; gk[2] = vvar; gk[3] = nilN;
    int topgoal = newnode(T_APP, intern("parse", 5), 4, gk);

    varbase = 2000000;
    if (replay_arg) {
        size_t cn; char *ct = slurp(replay_arg, &cn);
        static int *vm = NULL; if (!vm) vm = malloc(sizeof(int) * 65536);
        int nv2 = 0; Reader rr = { ct, ct + cn };
        int cert = rd_term(&rr, vm, &nv2);
        char why[512]; why[0] = 0;
        int ok = (cert >= 0) && replay(cert, topgoal, why, sizeof why);
        printf("{\"replay\":\"%s\"", ok ? "ACCEPT" : "REJECT");
        if (!ok) printf(",\"reason\":\"%s\"", why[0] ? why : "certificate did not re-derive");
        printf("}\n");
        return ok ? 0 : 1;
    }
    if (interpreted) {
        direct_timeout = timeout;
        direct_t0 = clock();
        int only[1] = { topgoal };
        direct_solve_goals(only, 1, 0, topgoal);
        double elapsed = (double)(clock() - direct_t0) / CLOCKS_PER_SEC;
        direct_sort();
        const char *outcome = direct_stop == 1 ? (ndirect ? "TimeoutWithResults" : "Timeout")
                            : direct_stop == 2 ? (ndirect ? "StepLimitWithResults" : "StepLimit")
                            : direct_stop == 3 ? (ndirect ? "DepthLimitWithResults" : "DepthLimit")
                            : ndirect == 0 ? "NoParse" : (ndirect == 1 ? "Unique" : "Ambiguous");
        printf("{\"mode\":\"interpreted\",\"outcome\":\"%s\",\"trees\":%d,"
               "\"rules\":%d,\"steps\":%ld,\"bytes\":%zu,\"seconds\":%.4f",
               outcome, ndirect, nrules, direct_steps, tlen, elapsed);
        if (!summary_only) {
            printf(",\"asts\":[");
            for (int i = 0; i < ndirect; i++) {
                printf("%s\"", i ? "," : "");
                for (char *q = direct_keys[i]; *q; q++) {
                    if (*q == '"' || *q == '\\') putchar('\\');
                    putchar(*q);
                }
                putchar('"');
            }
            putchar(']');
        }
        printf("}\n");
        return 0;
    }

    int tg = demand(topgoal);

    clock_t t0 = clock();
    enum { STOP_NONE, STOP_TIMEOUT, STOP_ROUNDS, STOP_RESOURCE } stop_reason = STOP_NONE;
    int rounds = 0;
    do {
        changed = 0;
        round_once();
        rounds++;
        double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (oom_hit) { stop_reason = STOP_RESOURCE; break; }
        if (el > timeout) { stop_reason = STOP_TIMEOUT; break; }
        if (rounds >= max_rounds) { stop_reason = STOP_ROUNDS; break; }
    } while (changed);
    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;

    int n = goals[tg].nans;
    const char *outcome = NULL;
    if (stop_reason == STOP_RESOURCE) outcome = n ? "ResourceExhaustionWithResults" : "ResourceExhaustion";
    else if (stop_reason == STOP_TIMEOUT) outcome = n ? "TimeoutWithResults" : "Timeout";
    else if (stop_reason == STOP_ROUNDS) outcome = n ? "RoundLimitWithResults" : "RoundLimit";
    else outcome = n == 0 ? "NoParse" : (n == 1 ? "Unique" : "Ambiguous");

    /* deterministic canonical ordering: sort answers by canonical string */
    int *idx = malloc((size_t)(n ? n : 1) * sizeof(int));
    for (int i = 0; i < n; i++) idx[i] = goals[tg].answers[i];
    for (int i = 1; i < n; i++) {          /* insertion sort on key */
        int v = idx[i], j = i - 1;
        while (j >= 0 && strcmp(ans[idx[j]].key, ans[v].key) > 0) { idx[j + 1] = idx[j]; j--; }
        idx[j + 1] = v;
    }

    printf("{\"outcome\":\"%s\",\"trees\":%d,\"rules\":%d,\"goals\":%d,\"answers\":%d,"
           "\"derivations\":%ld,\"rounds\":%d,\"bytes\":%zu,\"seconds\":%.4f",
           outcome, n, nrules, ngoals, nans, derivations, rounds, tlen, elapsed);
    if (!summary_only) {
            printf(",\"asts\":[");
            for (int i = 0; i < n; i++) {
                int a = idx[i];
                /* extract just the value slot (3rd arg) of the answer goal */
                int at = ans[a].term;
                int val = (nodes[at].tag == T_APP && nodes[at].nkids >= 3) ? nodes[at].kids[2] : at;
                char *s = tostr(val);
                printf("%s\"", i ? "," : "");
                for (char *p = s; *p; p++) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); }
                printf("\"");
                free(s);
            }
            printf("]");
    }
    if (show_certs) {
        printf(",\"certs\":[");
        for (int i = 0; i < n; i++) {
            printf("%s\"", i ? "," : "");
            /* certificate to a temp buffer */
            FILE *mf = tmpfile();
            print_cert(mf, idx[i], 0);
            fflush(mf); long sz = ftell(mf); fseek(mf, 0, SEEK_SET);
            char *cb = malloc((size_t)sz + 1);
            size_t rr = fread(cb, 1, (size_t)sz, mf); cb[rr] = 0; fclose(mf);
            for (char *p = cb; *p; p++) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); }
            free(cb);
            printf("\"");
        }
        printf("]");
    }
    printf("}\n");
    return 0;
}
