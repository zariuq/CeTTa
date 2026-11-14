/* rulec_validate.c - independent validator of the decision DAGs rulec writes.
 *
 *   rulec_validate WIRE DAG
 *
 * It shares no code with rulec.  It reads the module's core rows (the wire) and the DAG file, and
 * for every judgment enumerates every path of its DAG, with the tests on the path (positive: the
 * subterm at an occurrence has constructor c; negative: it has none of the constructors of a
 * switch's cases).  It checks:
 *   W  well-formed tests: a switch tests an argument, or a child of an occurrence whose
 *      constructor the path has fixed, at an index below that constructor's arity; its cases are
 *      distinct data constructors of the occurrence's sort; a switch without a default lists them all;
 *   S  soundness of a leaf for each rule r it lists: every constructor of r's left side is a positive
 *      test of the path at its occurrence, so every input that reaches the leaf matches r (and the
 *      leaf's reads of r's variables are reads of subterms the path has fixed);
 *   E  exclusivity: no input that matches a rule s the leaf does not list (for a failure leaf: any
 *      rule) satisfies the path's tests, i.e. some test of the path contradicts s's left side;
 *   R  every rule of the judgment is reached by some leaf.
 * Every input reaches exactly one leaf (each switch is exhaustive by W); by S and E that leaf is the
 * unique rule whose left side matches the input, or failure when none does.  Paths whose tests
 * contradict each other are reached by no input and are skipped.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *f, ...) { va_list a; va_start(a, f); fputs("rulec_validate: ", stderr); vfprintf(stderr, f, a); fputc('\n', stderr); va_end(a); exit(2); }

/* minimal reader: atoms are symbols, strings or lists */
typedef struct X { int list; char *s; int n; struct X **v; } X;
static X *rd(const char **pp) {
    const char *p = *pp;
    while (isspace((unsigned char)*p)) p++;
    if (!*p) return NULL;
    X *x = calloc(1, sizeof *x);
    if (*p == '(') {
        p++; x->list = 1;
        int cap = 0;
        for (;;) {
            while (isspace((unsigned char)*p)) p++;
            if (*p == ')') { p++; break; }
            if (!*p) die("unterminated list");
            *pp = p; X *e = rd(pp); p = *pp;
            if (x->n == cap) { cap = cap ? 2 * cap : 8; x->v = realloc(x->v, (size_t)cap * sizeof(X *)); }
            x->v[x->n++] = e;
        }
    } else if (*p == '"') {
        p++;
        char *b = malloc(strlen(p) + 1); int n = 0;
        while (*p != '"') { if (!*p) die("unterminated string"); if (*p == '\\') p++; b[n++] = *p++; }
        p++; b[n] = 0; x->s = b;
    } else {
        const char *q = p;
        while (*q && !isspace((unsigned char)*q) && *q != '(' && *q != ')') q++;
        x->s = strndup(p, (size_t)(q - p)); p = q;
    }
    *pp = p;
    return x;
}
static bool is(const X *x, const char *s) { return x && !x->list && !strcmp(x->s, s); }
static bool hd(const X *x, const char *s) { return x && x->list && x->n && is(x->v[0], s); }

/* signature */
#define MAXC 4096
static char *cname[MAXC]; static int csort[MAXC], carity[MAXC], *cargs[MAXC], nc;
static bool judgment[MAXC];
static char *sname[1024]; static int ns;
static int sort_of(const char *s) { for (int i = 0; i < ns; i++) if (!strcmp(sname[i], s)) return i; die("unknown sort %s", s); return -1; }
static int ctor_of(const char *s) { for (int i = 0; i < nc; i++) if (!strcmp(cname[i], s)) return i; return -1; }

/* left sides */
typedef struct P { int var; int c; int n; struct P **k; } P;   /* var: 1 variable, 0 constructor */
typedef struct { char *label; int head; P *lhs; } R;
static R rules[MAXC]; static int nr;

static int list_items(X *x, X ***out) {
    int n = 0, cap = 0; X **v = NULL;
    while (hd(x, "LCons")) { if (n == cap) { cap = cap ? 2 * cap : 8; v = realloc(v, (size_t)cap * sizeof(X *)); } v[n++] = x->v[1]; x = x->v[2]; }
    if (!is(x, "LNil")) die("bad list");
    *out = v; return n;
}
static P *pat(X *x) {
    P *p = calloc(1, sizeof *p);
    if (hd(x, "FVar")) { p->var = 1; return p; }
    if (!hd(x, "PApp")) die("bad pattern");
    p->c = ctor_of(x->v[1]->s);
    if (p->c < 0) die("unknown constructor %s", x->v[1]->s);
    X **it; p->n = list_items(x->v[2], &it);
    p->k = calloc((size_t)(p->n ? p->n : 1), sizeof(P *));
    for (int i = 0; i < p->n; i++) p->k[i] = pat(it[i]);
    free(it);
    return p;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r"); if (!f) die("cannot open %s", path);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1); if (fread(b, 1, (size_t)n, f) != (size_t)n) die("read %s", path); b[n] = 0; fclose(f); return b;
}

/* DAG */
typedef struct { int kind; int rule; int occ[64]; int olen; int ncase; int *cc; int *ch; int dflt; int ncand; int cand[64]; } N;   /* kind 0 fail 1 leaf 2 switch */
static N *nodes; static int nn;
static int root_of[MAXC];

/* path tests */
typedef struct { int occ[64]; int olen; int pos; int c; int nneg; int *neg; } Test;   /* pos: 1 positive (c), 0 negative (neg[]) */
static Test path[512]; static int depth;
static long npaths, nerr, nleaves;
static bool *rule_reached;
static int cur_j;

static bool occ_eq(const int *a, int al, const int *b, int bl) { return al == bl && !memcmp(a, b, (size_t)al * sizeof(int)); }

/* the constraint a left side puts on an occurrence: -1 none (a variable at or above it), else its constructor */
static int lhs_at(const P *lhs, const int *occ, int olen) {
    const P *p = lhs->k[occ[0]];
    for (int l = 1; l < olen; l++) {
        if (p->var) return -1;
        if (occ[l] >= p->n) return -2;   /* not a subterm of this constructor: contradiction */
        p = p->k[occ[l]];
    }
    return p->var ? -1 : p->c;
}
/* is some test of the path contradicted by rule s's left side? */
static bool contradicts(const P *lhs) {
    for (int t = 0; t < depth; t++) {
        /* a test below a constructor of s that differs from the path's test on the parent is already
           caught at the parent; a test whose parent is not fixed by s leaves s unconstrained */
        int c = lhs_at(lhs, path[t].occ, path[t].olen);
        if (c == -1) continue;
        if (c == -2) return true;
        if (path[t].pos) { if (c != path[t].c) return true; }
        else for (int i = 0; i < path[t].nneg; i++) if (path[t].neg[i] == c) return true;
    }
    return false;
}
/* does the path fix the constructor at an occurrence? -1 no */
static int fixed_at(const int *occ, int olen) {
    for (int t = 0; t < depth; t++) if (path[t].pos && occ_eq(path[t].occ, path[t].olen, occ, olen)) return path[t].c;
    return -1;
}
static bool excluded_at(const int *occ, int olen, int c) {
    for (int t = 0; t < depth; t++) if (!path[t].pos && occ_eq(path[t].occ, path[t].olen, occ, olen))
        for (int i = 0; i < path[t].nneg; i++) if (path[t].neg[i] == c) return true;
    return false;
}
/* sort of an occurrence along the path (its parents must be fixed) */
static int occ_sort(const int *occ, int olen) {
    int s = cargs[cur_j][occ[0]];
    for (int l = 1; l < olen; l++) {
        int c = fixed_at(occ, l);
        if (c < 0) return -1;
        if (occ[l] >= carity[c]) return -2;
        s = cargs[c][occ[l]];
    }
    return s;
}
static void report(const char *what) {
    nerr++;
    if (nerr <= 40) {
        fprintf(stderr, "rulec_validate: %s: %s; path:", cname[cur_j], what);
        for (int t = 0; t < depth; t++) {
            fputc(' ', stderr);
            for (int l = 0; l < path[t].olen; l++) fprintf(stderr, "%s%d", l ? "." : "", path[t].occ[l]);
            if (path[t].pos) fprintf(stderr, "=%s", cname[path[t].c]); else fprintf(stderr, "!=(%d ctors)", path[t].nneg);
        }
        fputc('\n', stderr);
    }
}

static void walk(int id) {
    N *x = &nodes[id];
    if (x->kind == 2) {
        int s = occ_sort(x->occ, x->olen);
        if (x->occ[0] >= carity[cur_j] || s < 0) { report("switch on an occurrence the path has not fixed"); return; }
        /* W: cases distinct data constructors of the sort; complete when no default */
        int ndata = 0;
        for (int c = 0; c < nc; c++) if (!judgment[c] && csort[c] == s) ndata++;
        for (int i = 0; i < x->ncase; i++) {
            if (judgment[x->cc[i]] || csort[x->cc[i]] != s) report("case is not a data constructor of the occurrence's sort");
            for (int j = 0; j < i; j++) if (x->cc[j] == x->cc[i]) report("case listed twice");
        }
        if (x->dflt < 0 && x->ncase != ndata) report("switch without default does not list every constructor");
        int fixed = fixed_at(x->occ, x->olen);
        for (int i = 0; i < x->ncase; i++) {
            if (fixed >= 0 && fixed != x->cc[i]) continue;                  /* no input takes this edge */
            if (excluded_at(x->occ, x->olen, x->cc[i])) continue;
            Test *t = &path[depth++];
            memcpy(t->occ, x->occ, sizeof t->occ); t->olen = x->olen; t->pos = 1; t->c = x->cc[i]; t->nneg = 0;
            walk(x->ch[i]);
            depth--;
        }
        if (x->dflt >= 0) {
            bool feasible = true;
            if (fixed >= 0) for (int i = 0; i < x->ncase; i++) if (x->cc[i] == fixed) feasible = false;
            if (feasible) {
                Test *t = &path[depth++];
                memcpy(t->occ, x->occ, sizeof t->occ); t->olen = x->olen; t->pos = 0; t->nneg = x->ncase; t->neg = x->cc;
                walk(x->dflt);
                depth--;
            }
        }
        return;
    }
    npaths++;
    int ncand = x->kind == 1 ? x->ncand : 0;
    for (int ci = 0; ci < ncand; ci++) {
        int r = x->cand[ci];
        nleaves++;
        if (rules[r].head != cur_j) { report("leaf names a rule of another judgment"); return; }
        rule_reached[r] = true;
        /* S: every constructor of r's left side is fixed by the path */
        struct { const P *p; int occ[64]; int olen; } st[1024]; int sp = 0;
        for (int a = 0; a < rules[r].lhs->n; a++) { st[sp].p = rules[r].lhs->k[a]; st[sp].occ[0] = a; st[sp].olen = 1; sp++; }
        while (sp) {
            sp--;
            const P *p = st[sp].p; int occ[64]; int olen = st[sp].olen;
            memcpy(occ, st[sp].occ, sizeof occ);
            if (p->var) continue;
            if (fixed_at(occ, olen) != p->c) { char m[256]; snprintf(m, sizeof m, "leaf of rule \"%s\" can be reached by an input it does not match", rules[r].label); report(m); break; }
            for (int i = 0; i < p->n; i++) { st[sp].p = p->k[i]; memcpy(st[sp].occ, occ, sizeof occ); st[sp].occ[olen] = i; st[sp].olen = olen + 1; sp++; }
        }
    }
    /* E: every rule the leaf does not list is contradicted by the path */
    for (int s = 0; s < nr; s++) {
        if (rules[s].head != cur_j) continue;
        bool listed = false;
        for (int ci = 0; ci < ncand; ci++) if (x->cand[ci] == s) listed = true;
        if (listed) continue;
        if (!contradicts(rules[s].lhs)) {
            char m[256];
            snprintf(m, sizeof m, "an input matching rule \"%s\" reaches %s%s%s", rules[s].label, ncand ? "the leaf of rule \"" : "a failure", ncand ? rules[x->cand[0]].label : "", ncand ? "\"" : "");
            report(m);
        }
    }
}

/* ------------------------------------------------- generated C against the DAG
 * The dispatch code rulec writes is regular: per judgment J a procedure p<J> (p<J>_rules when J has a
 * native operation) whose first statement jumps to the root, one line
 *   n<id>: switch (RC_TAG_<rep>(<occurrence>)) { case <ctor>: goto <label>; ... default: goto <label>; }
 * per switch node, and one block "r<id>: { /" "* <label> *" "/" per rule containing RC_RULE(<id>).
 * Labels: r<rule> a leaf, f a failure, n<id> a switch.  This checks that the code is the DAG. */
static int parse_occ(const char *s, int *occ, int *olen) {   /* returns characters consumed or -1 */
    const char *p = s;
    if (!strncmp(p, "RC_KID_", 7)) {
        p += 7;
        while (isalnum((unsigned char)*p) || *p == '_') p++;
        if (*p++ != '(') return -1;
        int n = parse_occ(p, occ, olen);
        if (n < 0) return -1;
        p += n;
        if (*p++ != ',' || *p++ != ' ') return -1;
        char *e; long k = strtol(p, &e, 10);
        if (e == p || *e != ')') return -1;
        occ[(*olen)++] = (int)k;
        p = e + 1;
        return (int)(p - s);
    }
    if (*p != 'a') return -1;
    char *e; long a = strtol(p + 1, &e, 10);
    if (e == p + 1) return -1;
    *olen = 0;
    occ[(*olen)++] = (int)a;
    return (int)(e - s);
}
static bool label_is(const char *lab, int child) {
    N *x = &nodes[child];
    char want[32];
    if (x->kind == 1) snprintf(want, sizeof want, "r%d", x->rule);
    else if (x->kind == 0) snprintf(want, sizeof want, "f");
    else snprintf(want, sizeof want, "n%d", child);
    return !strcmp(lab, want);
}
static void reach(int id, bool *seen) {
    if (seen[id]) return;
    seen[id] = true;
    if (nodes[id].kind == 2) { for (int i = 0; i < nodes[id].ncase; i++) reach(nodes[id].ch[i], seen); if (nodes[id].dflt >= 0) reach(nodes[id].dflt, seen); }
}
static long check_c(const char *cpath) {
    char *src = slurp(cpath);
    long bad = 0, nsw = 0, nblk = 0;
    int cur = -1;            /* judgment of the current procedure body (the one with the DAG) */
    bool expect_root = false;
    bool *seen_sw = calloc((size_t)nn, 1), *seen_blk = calloc((size_t)nr, 1), *proc_seen = calloc(MAXC, 1);
    bool *reachable = calloc((size_t)nn, 1);
    int blk_rule = -1;
#define BAD(...) do { bad++; if (bad <= 20) { fprintf(stderr, "rulec_validate: generated C: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
    for (char *line = strtok(src, "\n"); line; line = strtok(NULL, "\n")) {
        int j; char tail[64];
        if (sscanf(line, "static T *p%d%63[^(]", &j, tail) >= 1 && strchr(line, '{')) {
            bool rules_version = !strncmp(tail, "_rules", 6);
            if (j < 0 || j >= nc || !judgment[j]) { BAD("procedure p%d is not a judgment", j); cur = -1; continue; }
            /* the body with the DAG: p<J>_rules when it exists, else p<J> */
            bool has_rules_version = strstr(src + (line - src) + strlen(line) + 1, "") != NULL && false;
            (void)has_rules_version;
            if (rules_version || !proc_seen[j]) {
                cur = j; expect_root = true;
                memset(reachable, 0, (size_t)nn);
                if (root_of[j] >= 0) reach(root_of[j], reachable);
            } else cur = -1;
            if (rules_version) proc_seen[j] = true;
            continue;
        }
        if (cur < 0) continue;
        const char *p = line;
        while (*p == ' ') p++;
        if (expect_root && !strncmp(p, "goto ", 5)) {
            char lab[32];
            if (sscanf(p, "goto %31[^;];", lab) == 1 && !label_is(lab, root_of[cur])) BAD("p%d (%s) starts at %s, not at its DAG root", cur, cname[cur], lab);
            expect_root = false;
            continue;
        }
        if (expect_root && strncmp(p, "rc_roots", 8) && strncmp(p, "rc_safepoint", 12) && strncmp(p, "a", 1) && strncmp(p, "if (rc_trace_on)", 16) && strncmp(p, "#", 1))
            { /* native-operation wrapper or safepoint prelude lines are skipped until the root jump */ }
        int id;
        if (line[0] == 'n' && sscanf(line, "n%d: switch (RC_TAG_", &id) == 1) {
            nsw++;
            if (id < 0 || id >= nn || nodes[id].kind != 2) { BAD("n%d is not a switch node", id); continue; }
            if (!reachable[id]) { BAD("n%d in p%d is not reachable from %s's root", id, cur, cname[cur]); continue; }
            if (seen_sw[id] && 0) {}
            seen_sw[id] = true;
            N *x = &nodes[id];
            const char *q = strchr(line, '(');                /* "(RC_TAG_rep(" */
            q = strchr(q + 1, '(') + 1;                       /* start of the occurrence */
            int occ[64], olen = 0;
            int used = parse_occ(q, occ, &olen);
            if (used < 0 || !occ_eq(occ, olen, x->occ, x->olen)) { BAD("n%d tests another occurrence than the DAG", id); continue; }
            /* cases */
            int ncase = 0; const char *c = strstr(line, "{");
            bool ok = true; char dlab[32] = "";
            for (const char *t = c; (t = strstr(t, "case ")); ) {
                int ctor; char lab[32];
                if (sscanf(t, "case %d: goto %31[^;];", &ctor, lab) != 2) { ok = false; break; }
                if (ncase >= x->ncase || x->cc[ncase] != ctor || !label_is(lab, x->ch[ncase])) ok = false;
                ncase++;
                t += 5;
            }
            const char *dd = strstr(line, "default: goto ");
            if (!dd || sscanf(dd, "default: goto %31[^;];", dlab) != 1) ok = false;
            else if (x->dflt >= 0 ? !label_is(dlab, x->dflt) : strcmp(dlab, "f")) ok = false;
            if (!ok || ncase != x->ncase) BAD("n%d's cases differ from the DAG", id);
            continue;
        }
        if (line[0] == 'r' && sscanf(line, "r%d: { /*", &id) == 1) {
            nblk++;
            if (id < 0 || id >= nr) { BAD("block r%d names no rule", id); blk_rule = -1; continue; }
            const char *lb = strstr(line, "/* ");
            char label[256];
            if (!lb || sscanf(lb, "/* %255s", label) != 1 || strcmp(label, rules[id].label)) BAD("block r%d is not labelled %s", id, rules[id].label);
            if (rules[id].head != cur) BAD("block of rule %s is in p%d, not in its judgment's procedure", rules[id].label, cur);
            seen_blk[id] = true;
            blk_rule = id;
            continue;
        }
        int rr;
        if (blk_rule >= 0 && sscanf(p, "RC_RULE(%d);", &rr) == 1) {
            if (rr != blk_rule) BAD("block r%d records rule %d", blk_rule, rr);
            blk_rule = -1;
        }
    }
    for (int c = 0; c < nc; c++) {
        if (!judgment[c] || root_of[c] < 0) continue;
        bool *rch = calloc((size_t)nn, 1);
        reach(root_of[c], rch);
        for (int i = 0; i < nn; i++) if (rch[i] && nodes[i].kind == 2 && !seen_sw[i]) BAD("switch node %d of %s is not in the code", i, cname[c]);
        free(rch);
    }
    for (int r = 0; r < nr; r++) if (!seen_blk[r]) BAD("rule %s has no block", rules[r].label);
    printf("rulec_validate: generated C: %ld switch lines, %ld rule blocks checked against the DAG: %ld problems\n", nsw, nblk, bad);
    free(src);
    return bad;
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) die("usage: rulec_validate WIRE DAG [GENERATED.c]");
    char *w = slurp(argv[1]);
    for (char *line = strtok(w, "\n"); line; line = strtok(NULL, "\n")) {
        const char *p = line;
        X *x = rd(&p);
        if (!x) continue;
        if (hd(x, "TypeDecl")) sname[ns++] = x->v[1]->s;
        else if (hd(x, "GrammarRuleV2")) {
            if (nc == MAXC) die("too many constructors");
            cname[nc] = x->v[1]->s; csort[nc] = sort_of(x->v[2]->s);
            X **ps; carity[nc] = list_items(x->v[3], &ps);
            cargs[nc] = calloc((size_t)(carity[nc] ? carity[nc] : 1), sizeof(int));
            for (int i = 0; i < carity[nc]; i++) cargs[nc][i] = sort_of(ps[i]->v[2]->v[1]->s);
            free(ps);
            nc++;
        } else if (hd(x, "ScopedRewriteRuleV1")) {
            rules[nr].label = x->v[1]->s;
            rules[nr].lhs = pat(x->v[4]);
            rules[nr].head = rules[nr].lhs->c;
            judgment[rules[nr].head] = true;
            nr++;
        }
    }
    char *d = slurp(argv[2]);
    for (int i = 0; i < MAXC; i++) root_of[i] = -1;
    int declared_nodes = -1;
    for (char *line = strtok(d, "\n"); line; line = strtok(NULL, "\n")) {
        const char *p = line;
        X *x = rd(&p);
        if (!x) continue;
        if (hd(x, "dag-file")) {
            if (atoi(x->v[3]->s) != nr) die("the DAG file was written for %s rules, the wire has %d", x->v[3]->s, nr);
            declared_nodes = atoi(x->v[4]->s);
            nodes = calloc((size_t)declared_nodes, sizeof(N));
        } else if (hd(x, "node")) {
            int id = atoi(x->v[1]->s);
            if (!nodes || id < 0 || id >= declared_nodes) die("node id out of range");
            N *n = &nodes[id];
            if (id >= nn) nn = id + 1;
            if (is(x->v[2], "fail")) n->kind = 0;
            else if (is(x->v[2], "leaf")) {
                n->kind = 1; n->rule = -1; n->ncand = 0;
                for (int c = 3; c < x->n; c++) {
                    int found = -1;
                    for (int r = 0; r < nr; r++) if (!strcmp(rules[r].label, x->v[c]->s)) found = r;
                    if (found < 0) die("leaf names an unknown rule %s", x->v[c]->s);
                    if (n->ncand == 64) die("leaf lists too many rules");
                    n->cand[n->ncand++] = found;
                }
                if (!n->ncand) die("leaf lists no rule");
                n->rule = n->cand[0];
            } else if (is(x->v[2], "switch")) {
                n->kind = 2;
                X *o = x->v[3]; n->olen = o->n - 1;
                for (int i = 1; i < o->n; i++) n->occ[i - 1] = atoi(o->v[i]->s);
                X *cs = x->v[4]; n->ncase = cs->n - 1;
                n->cc = calloc((size_t)(n->ncase ? n->ncase : 1), sizeof(int)); n->ch = calloc((size_t)(n->ncase ? n->ncase : 1), sizeof(int));
                for (int i = 1; i < cs->n; i++) {
                    n->cc[i - 1] = ctor_of(cs->v[i]->v[0]->s);
                    if (n->cc[i - 1] < 0) die("case names an unknown constructor");
                    n->ch[i - 1] = atoi(cs->v[i]->v[1]->s);
                }
                n->dflt = atoi(x->v[5]->v[1]->s);
            } else die("unknown node kind");
        } else if (hd(x, "root")) {
            int c = ctor_of(x->v[1]->s);
            if (c < 0 || !judgment[c]) die("root for %s, which heads no rule", x->v[1]->s);
            root_of[c] = atoi(x->v[2]->s);
        }
    }
    rule_reached = calloc((size_t)nr, 1);
    int nj = 0;
    for (int c = 0; c < nc; c++) {
        if (!judgment[c]) continue;
        nj++;
        if (root_of[c] < 0) { cur_j = c; report("judgment has no DAG"); continue; }
        cur_j = c;
        depth = 0;
        walk(root_of[c]);
    }
    for (int r = 0; r < nr; r++) if (!rule_reached[r]) { cur_j = rules[r].head; char m[256]; snprintf(m, sizeof m, "rule \"%s\" is reached by no leaf", rules[r].label); report(m); }
    printf("rulec_validate: %d judgments, %d rules, %d nodes, %ld paths (%ld rule leaves): %ld problems\n", nj, nr, nn, npaths, nleaves, nerr);
    long cbad = argc == 4 ? check_c(argv[3]) : 0;
    return nerr || cbad ? 1 : 0;
}
