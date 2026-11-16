/* lam_tcalc.c - the lambda client of rulec (tests/rulec/lambda) against its reading by demand in tcalc.
 * For each case of the cases file (the grammar of tests/rulec/lambda/lam.c):
 *   - the call is built as module data and run through the module rulec compiled (rules as written);
 *   - PROGRAM (the module read by demand), with (demand CALL), runs under `CETTA --lang tcalc`, and
 *     every row (value CALL V) of its outcome is collected;
 *   - rulec's result, or none, must be tcalc's one value, or none.
 * A case with two values or more, or whose run did not finish, is reported as such.
 *   lam_tcalc CETTA PROGRAM.tcalc CASES WORKDIR
 */
#include "rc_rt.h"
#include "lambda_ids.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int nib[16] = {C_lc_x0, C_lc_x1, C_lc_x2, C_lc_x3, C_lc_x4, C_lc_x5, C_lc_x6, C_lc_x7,
                            C_lc_x8, C_lc_x9, C_lc_xa, C_lc_xb, C_lc_xc, C_lc_xd, C_lc_xe, C_lc_xf};
static T *mk(int c, T *a, T *b, T *d) { T *k[3] = {a, b, d}; return rc_construct(c, k); }
static T *mk_nat(uint64_t v) {
    if (!v) return mk(C_lc_nz, 0, 0, 0);
    return mk(v & 1 ? C_lc_nb1 : C_lc_nb0, mk_nat(v >> 1), 0, 0);
}
static T *mk_name(const char *s, size_t n) {
    T *r = mk(C_lc_nnil, 0, 0, 0);
    while (n--) {
        unsigned char b = (unsigned char)s[n];
        r = mk(C_lc_ncons, mk(C_lc_byte, mk(nib[b >> 4], 0, 0, 0), mk(nib[b & 15], 0, 0, 0), 0), r, 0);
    }
    return r;
}

/* ---- text that grows ---- */
typedef struct { char *s; size_t len, cap; } Text;
static void put(Text *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = 2 * (b->len + n + 1);
        b->s = realloc(b->s, b->cap);
        if (!b->s) { perror("lam_tcalc"); exit(3); }
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}
static void puts_text(Text *b, const char *s) { put(b, s, strlen(s)); }

/* module data as text, through the generated views and signature tables; a view writes every child,
 * so its buffer holds as many as the signature's widest constructor */
static int widest(void) {
    int w = 1;
    for (int c = 0; c < rc_nctors; c++) if (rc_ctor_arity[c] > w) w = rc_ctor_arity[c];
    return w;
}
static void text(Text *b, int sort, T *x) {
    T **k = malloc((size_t)widest() * sizeof *k);
    if (!k) { perror("lam_tcalc"); exit(3); }
    int c = rc_view(sort, x, k);
    int n = rc_ctor_arity[c];
    puts_text(b, "("); puts_text(b, rc_ctor_name[c]);
    for (int i = 0; i < n; i++) { puts_text(b, " "); text(b, rc_ctor_asort[rc_ctor_asort_off[c] + i], k[i]); }
    puts_text(b, ")");
    free(k);
}

/* ---- parsing (lam.c's grammar) ---- */
static const char *p;
static void ws(void) { while (*p == ' ' || *p == '\t') p++; }
static bool name_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static T *term(void);
static T *atom(void) {
    ws();
    if (*p == '(') { p++; T *t = term(); ws(); if (*p == ')') p++; return t; }
    if (*p == '\\') {
        p++; ws();
        const char *s = p;
        while (isalnum((unsigned char)*p) || *p == '_' || *p == '\'') p++;
        T *n = mk_name(s, (size_t)(p - s));
        ws();
        if (*p == '.') p++;
        return mk(C_lc_Lam, n, term(), 0);
    }
    const char *s = p;
    while (isalnum((unsigned char)*p) || *p == '_' || *p == '\'') p++;
    return mk(C_lc_Var, mk_name(s, (size_t)(p - s)), 0, 0);
}
static T *term(void) {
    T *t = atom();
    for (;;) {
        ws();
        if (!*p || *p == ')' || *p == ';' || *p == '\n' || *p == '#') return t;
        if (!name_start(*p) && *p != '(' && *p != '\\') return t;
        t = mk(C_lc_App, t, atom(), 0);
    }
}

/* ---- tcalc's values of a call: each balanced term V after "(value CALL " ---- */
static char *slurp(FILE *f) {
    Text b = {0}; char chunk[1 << 14]; size_t r;
    puts_text(&b, "");
    while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) put(&b, chunk, r);
    return b.s;
}
static int values(const char *out, const char *call, char **first) {
    Text key = {0};
    puts_text(&key, "(value "); puts_text(&key, call); puts_text(&key, " ");
    int count = 0;
    for (const char *s = strstr(out, key.s); s; s = strstr(s + 1, key.s)) {
        const char *v = s + key.len, *e = v;
        int depth = 0;
        do { if (*e == '(') depth++; else if (*e == ')') depth--; e++; } while (depth > 0 && *e);
        if (count == 0) *first = strndup(v, (size_t)(e - v));
        count++;
    }
    free(key.s);
    return count;
}

int main(int argc, char **argv) {
    if (argc != 5) { fputs("usage: lam_tcalc CETTA PROGRAM.tcalc CASES WORKDIR\n", stderr); return 2; }
    const char *cetta = argv[1], *work = argv[4];
    FILE *pf = fopen(argv[2], "r");
    if (!pf) { perror(argv[2]); return 2; }
    char *prog = slurp(pf);
    fclose(pf);
    FILE *f = fopen(argv[3], "r");
    if (!f) { perror(argv[3]); return 2; }
    rc_runtime_init(0);
    rc_components_init();
    char *line = NULL; size_t lcap = 0;
    int cases = 0, agree = 0, differ = 0, several = 0, unfinished = 0;
    while (getline(&line, &lcap, f) > 0) {
        p = line;
        ws();
        if (!*p || *p == '#' || *p == '\n') continue;
        int j; T *a[3];
        if (!strncmp(p, "norm ", 5)) {
            p += 5;
            uint64_t limit = strtoull(p, (char **)&p, 10);
            a[0] = mk_nat(limit); a[1] = mk_nat(0); a[2] = term(); j = C_lc_normalize;
        } else if (!strncmp(p, "alpha ", 6)) {
            p += 6;
            a[0] = term(); ws(); if (*p == ';') p++; a[1] = term(); j = C_lc_alpha_eq;
        } else if (!strncmp(p, "fv ", 3)) {
            p += 3;
            a[0] = term(); j = C_lc_fv;
        } else continue;
        cases++;
        Text call = {0};
        puts_text(&call, "("); puts_text(&call, rc_ctor_name[j]);
        for (int i = 0; i < rc_ctor_arity[j]; i++) {
            puts_text(&call, " ");
            text(&call, rc_ctor_asort[rc_ctor_asort_off[j] + i], a[i]);
        }
        puts_text(&call, ")");
        T *r = rc_call(j, a);
        Text expect = {0};
        if (r) text(&expect, rc_ctor_sort[j], r);
        Text path = {0}, cmd = {0};
        puts_text(&path, work); puts_text(&path, "/case.tcalc");
        FILE *o = fopen(path.s, "w");
        if (!o) { perror(path.s); return 2; }
        fprintf(o, "%s(budget 100000000)\n(demand %s)\n", prog, call.s);
        fclose(o);
        puts_text(&cmd, cetta); puts_text(&cmd, " --lang tcalc --profile observe=outcomes ");
        puts_text(&cmd, path.s); puts_text(&cmd, " 2>&1");
        FILE *q = popen(cmd.s, "r");
        if (!q) { perror("popen"); return 2; }
        char *out = slurp(q);
        pclose(q);
        char *got = NULL;
        int k = values(out, call.s, &got);
        line[strcspn(line, "\n")] = 0;
        if (!strstr(out, "(finished true)")) { unfinished++; printf("UNFINISHED %s\n", line); }
        else if (k > 1) { several++; printf("SEVERAL (%d) %s\n", k, line); }
        else if ((k == 0 && !r) || (k == 1 && r && !strcmp(got, expect.s))) agree++;
        else {
            differ++;
            printf("DIFFER %s\n  rulec: %s\n  tcalc: %s\n", line, r ? expect.s : "(no result)", k ? got : "(no value)");
        }
        free(out); free(got); free(call.s); free(expect.s); free(path.s); free(cmd.s);
        rc_runtime_reset();
    }
    free(line);
    fclose(f);
    free(prog);
    printf("lambda: cases %d: agree %d, differ %d, several values %d, unfinished %d\n",
           cases, agree, differ, several, unfinished);
    return differ || several || unfinished;
}
