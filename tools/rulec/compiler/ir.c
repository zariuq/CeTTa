/* ir.c - see rulec.h */
#include "rulec.h"

/* ---------------------------------------------------------------- Rule IR */


Sort *sorts; int nsorts, sortcap;
Ctor *ctors; int nctors, ctorcap;
Rule *rules; int nrules, rulecap;
Map sort_map, ctor_map;
char **declared_judgments; int ndeclared_judgments, declared_judgment_cap;
int nerrors;
char *module_name = "?", *theory_name = "?";

void err(const Rule *r, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "rulec: check failed%s%s%s: ", r ? " in rule \"" : "", r ? r->label : "", r ? "\"" : "");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    nerrors++;
}

int sort_id(const char *s) { return map_get(&sort_map, s); }

Pat *pat_from(Sx *x, Rule *r) {
    Pat *p = xcalloc(1, sizeof *p);
    if (sx_head(x, "FVar")) {
        const char *v = sx_str(x->v[1], "variable");
        p->var = -1;
        for (int i = 0; i < r->nv; i++) if (!strcmp(r->vn[i], v)) p->var = i;
        if (p->var < 0) { err(r, "undeclared variable %s", v); p->var = 0; p->sort = -1; return p; }
        p->sort = r->vs[p->var];
        return p;
    }
    if (!sx_head(x, "PApp")) die("unknown pattern form in rule %s", r->label);
    const char *c = sx_str(x->v[1], "constructor");
    p->var = -1;
    p->ctor = map_get(&ctor_map, c);
    Sx **items;
    p->n = sx_items(x->v[2], &items);
    p->k = xcalloc((size_t)p->n, sizeof(Pat *));
    for (int i = 0; i < p->n; i++) p->k[i] = pat_from(items[i], r);
    free(items);
    if (p->ctor < 0) { err(r, "undeclared constructor %s", c); p->sort = -1; return p; }
    Ctor *k = &ctors[p->ctor];
    p->sort = k->sort;
    if (p->n != k->arity) { err(r, "constructor %s has arity %d, used with %d", c, k->arity, p->n); return p; }
    for (int i = 0; i < p->n; i++)
        if (p->k[i]->sort >= 0 && p->k[i]->sort != k->asort[i])
            err(r, "argument %d of %s has sort %s, expected %s", i, c, sorts[p->k[i]->sort].name, sorts[k->asort[i]].name);
    return p;
}

/* identical repeated declarations (a sort or constructor shared by joined theories appears once per
 * theory) are one declaration; a repeated name with a different row is refused */
static Map decl_text;
static bool repeated_decl(const char *kind, const char *name, const char *row) {
    char key[1024];
    snprintf(key, sizeof key, "%s %s", kind, name);
    int at = map_get(&decl_text, key);
    static char **texts; static int ntexts, textcap;
    if (at >= 0) {
        if (strcmp(texts[at], row)) die("%s %s declared twice, differently", kind, name);
        return true;
    }
    map_put(&decl_text, key, ntexts);
    char *t = xstrdup(row);
    VEC_PUSH(texts, ntexts, textcap, t);
    return false;
}

void load_wire(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open %s", path);
    size_t cap = 1 << 20, n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n + 65536 > cap) { cap *= 2; buf = xrealloc(buf, cap); }
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        if (!r) break;
        n += r;
    }
    buf[n] = 0;
    fclose(f);
    int ends = 0, type_rows = 0, term_rows = 0;
    for (char *line = buf; line && *line;) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        const char *p = line;
        while (*p == ' ') p++;
        if (*p == '(') {
            const char *row = p;
            Sx *x = sx_read(&p);
            if (sx_head(x, "wire-module")) {
                module_name = xstrdup(sx_str(x->v[1], "module name"));
                theory_name = xstrdup(sx_str(x->v[2], "theory name"));
            } else if (sx_head(x, "TypeDecl")) {
                type_rows++;
                if (repeated_decl("sort", sx_str(x->v[1], "sort"), row)) { line = nl ? nl + 1 : NULL; continue; }
                Sort s = {0};
                s.name = xstrdup(sx_str(x->v[1], "sort"));
                s.native = 0;
                if (sort_id(s.name) >= 0) die("sort %s declared twice", s.name);
                map_put(&sort_map, s.name, nsorts);
                VEC_PUSH(sorts, nsorts, sortcap, s);
            } else if (sx_head(x, "GrammarRuleV2")) {
                term_rows++;
                if (repeated_decl("constructor", sx_str(x->v[1], "constructor"), row)) { line = nl ? nl + 1 : NULL; continue; }
                Ctor c = {0};
                c.name = xstrdup(sx_str(x->v[1], "constructor"));
                c.sort = sort_id(sx_str(x->v[2], "constructor sort"));
                if (c.sort < 0) die("constructor %s of undeclared sort %s", c.name, x->v[2]->s);
                Sx **ps;
                c.arity = sx_items(x->v[3], &ps);
                c.asort = xcalloc((size_t)c.arity, sizeof(int));
                c.aname = xcalloc((size_t)c.arity, sizeof(char *));
                for (int i = 0; i < c.arity; i++) {
                    if (!sx_head(ps[i], "TermSimple") || !sx_head(ps[i]->v[2], "TBase"))
                        die("constructor %s: unsupported parameter form", c.name);
                    c.aname[i] = xstrdup(sx_str(ps[i]->v[1], "parameter"));
                    c.asort[i] = sort_id(sx_str(ps[i]->v[2]->v[1], "parameter sort"));
                    if (c.asort[i] < 0) die("constructor %s: undeclared parameter sort", c.name);
                }
                free(ps);
                if (!sx_is_sym(x->v[5], "EvalNone") || !sx_is_sym(x->v[6], "AlgebraNone"))
                    die("constructor %s carries an evaluation or algebra (unsupported)", c.name);
                if (map_get(&ctor_map, c.name) >= 0) die("constructor %s declared twice", c.name);
                c.dag_root = -1; c.native_op = -1; c.det = 0;
                map_put(&ctor_map, c.name, nctors);
                VEC_PUSH(ctors, nctors, ctorcap, c);
            } else if (sx_head(x, "ScopedRewriteRuleV1")) {
                Rule r = {0};
                r.id = nrules;
                r.label = xstrdup(sx_str(x->v[1], "rule label"));
                Sx **vs;
                r.nv = sx_items(x->v[2], &vs);
                r.vn = xcalloc((size_t)r.nv, sizeof(char *));
                r.vs = xcalloc((size_t)r.nv, sizeof(int));
                for (int i = 0; i < r.nv; i++) {
                    if (!sx_head(vs[i], "TypeBinding") || !sx_head(vs[i]->v[2], "TBase"))
                        die("rule %s: unsupported variable binding", r.label);
                    r.vn[i] = xstrdup(sx_str(vs[i]->v[1], "variable"));
                    r.vs[i] = sort_id(sx_str(vs[i]->v[2]->v[1], "variable sort"));
                    if (r.vs[i] < 0) die("rule %s: variable %s of undeclared sort", r.label, r.vn[i]);
                    for (int j = 0; j < i; j++) if (!strcmp(r.vn[j], r.vn[i])) die("rule %s: variable %s declared twice", r.label, r.vn[i]);
                }
                free(vs);
                Sx **ps;
                r.np = sx_items(x->v[3], &ps);
                r.p = xcalloc((size_t)r.np, sizeof(Prem));
                for (int i = 0; i < r.np; i++) {
                    if (sx_head(ps[i], "RelationQuery") && ps[i]->n == 3) {
                        r.p[i].relation = xstrdup(sx_str(ps[i]->v[1], "relation"));
                        Sx **as;
                        r.p[i].nrel = sx_items(ps[i]->v[2], &as);
                        r.p[i].rel = xcalloc((size_t)(r.p[i].nrel ? r.p[i].nrel : 1), sizeof(Pat *));
                        for (int a = 0; a < r.p[i].nrel; a++) r.p[i].rel[a] = pat_from(as[a], &r);
                        free(as);
                        continue;
                    }
                    if (!sx_head(ps[i], "Congruence") || ps[i]->n != 3) die("rule %s: unsupported premise form", r.label);
                    r.p[i].src = pat_from(ps[i]->v[1], &r);
                    r.p[i].tgt = pat_from(ps[i]->v[2], &r);
                }
                free(ps);
                r.lhs = pat_from(x->v[4], &r);
                r.rhs = pat_from(x->v[5], &r);
                if (!sx_is_sym(x->v[6], "RuleBindingNone")) die("rule %s: unsupported binding annotation", r.label);
                VEC_PUSH(rules, nrules, rulecap, r);
            } else if (sx_head(x, "Judgment")) {
                /* a constructor that is a judgment whether or not a rule's left side is headed by it
                 * (a judgment without rules has no result) */
                VEC_PUSH(declared_judgments, ndeclared_judgments, declared_judgment_cap, xstrdup(sx_str(x->v[1], "judgment")));
            } else if (sx_head(x, "wire-end")) {
                ends++;
                if (x->n != 5 || atoi(x->v[1]->s) != type_rows || atoi(x->v[2]->s) != term_rows ||
                    atoi(x->v[3]->s) != 0 || atoi(x->v[4]->s) != nrules)
                    die("wire-end counts do not match the rows read (%d sort rows, %d constructor rows, %d rules)", type_rows, term_rows, nrules);
            } else if (sx_head(x, "wire-refused")) {
                die("the front end refused the module");
            } else {
                die("unknown wire row");
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    if (ends != 1) die("missing wire-end row");
    free(buf);
}


/* ---- rules in functional form ----
 * A call (an application of a judgment) nested in a rule's right side, or in the arguments of a
 * premise's source or relation query, stands for the call's value: the rule is read call by value.
 * flatten_calls rewrites such a rule into premise form, innermost first and left to right: each
 * nested call becomes a premise J(args) ~> v with a fresh variable v of J's result sort, placed
 * before the premise it occurs in (for the right side: after every premise), and the call's
 * occurrence becomes v.  A right side that is itself a call becomes the last premise, whose value is
 * the result (a tail call).  Left sides and premise targets are patterns: a call there is refused by
 * check_rules.  The judgments are the constructors heading a left side. */
static int fresh_var(Rule *r, int sort) {
    char name[32];
    snprintf(name, sizeof name, "%%call%d", r->nv);
    r->vn = xrealloc(r->vn, (size_t)(r->nv + 1) * sizeof(char *));
    r->vs = xrealloc(r->vs, (size_t)(r->nv + 1) * sizeof(int));
    r->vn[r->nv] = xstrdup(name);
    r->vs[r->nv] = sort;
    return r->nv++;
}
static Pat *var_pat(int v, int sort) {
    Pat *p = xcalloc(1, sizeof *p);
    p->var = v; p->sort = sort;
    return p;
}
static Prem *flat_out; static int nflat_out, flat_cap;
static Pat *flatten_term(Rule *r, Pat *p, bool *judgment) {
    if (p->var != -1 || p->ctor < 0) return p;
    for (int i = 0; i < p->n; i++) p->k[i] = flatten_term(r, p->k[i], judgment);
    if (!judgment[p->ctor]) return p;
    int v = fresh_var(r, ctors[p->ctor].sort);
    Prem q = {0};
    q.src = p;
    q.tgt = var_pat(v, ctors[p->ctor].sort);
    VEC_PUSH(flat_out, nflat_out, flat_cap, q);
    return var_pat(v, ctors[p->ctor].sort);
}
int n_flattened_rules, n_flattened_calls;
void flatten_calls(void) {
    bool *judgment = xcalloc((size_t)(nctors ? nctors : 1), sizeof(bool));
    for (int i = 0; i < nrules; i++)
        if (rules[i].lhs->var == -1 && rules[i].lhs->ctor >= 0) judgment[rules[i].lhs->ctor] = true;
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        nflat_out = 0;
        for (int j = 0; j < r->np; j++) {
            Prem *p = &r->p[j];
            if (p->relation) for (int a = 0; a < p->nrel; a++) p->rel[a] = flatten_term(r, p->rel[a], judgment);
            else for (int a = 0; a < p->src->n; a++) p->src->k[a] = flatten_term(r, p->src->k[a], judgment);
            VEC_PUSH(flat_out, nflat_out, flat_cap, *p);
        }
        int before = nflat_out;
        r->rhs = flatten_term(r, r->rhs, judgment);
        (void)before;
        if (nflat_out != r->np) {
            n_flattened_rules++;
            n_flattened_calls += nflat_out - r->np;
            free(r->p);
            r->p = xmalloc((size_t)nflat_out * sizeof(Prem));
            memcpy(r->p, flat_out, (size_t)nflat_out * sizeof(Prem));
            r->np = nflat_out;
        }
    }
    free(judgment);
}
