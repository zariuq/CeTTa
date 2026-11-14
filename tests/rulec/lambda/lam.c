/* lam.c - the gate driver of the lambda-calculus client module (Lambda.module): reads cases, builds
 * terms, runs the compiled module, prints results.  It reaches module data only through the
 * generated rc_construct and rc_view, so the same driver runs every build of the module (the
 * symbolic one, which compiles every rule as written, and builds through the generic components).
 *
 *   lam [--check] CASES
 * A case is one line:  norm LIMIT TERM | alpha TERM ; TERM | fv TERM   ('#' starts a comment)
 * TERM: x | \x. TERM | TERM TERM | (TERM); names [A-Za-z_][A-Za-z0-9_']*.
 * --check (a build with -DRC_CHECK_NATIVE): every component and kept result is compared with the
 * rules; the counts are printed at the end and the exit status is 1 on any difference.
 */
#include "rc_rt.h"
#include "lambda_ids.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RC_CHECK_NATIVE
int rc_check_native, rc_check_depth;
static uint64_t checked, differed;
static void compare(const char *what, T *a, T *b) {
    checked++;
    if (!(a == b || (a && b && rc_term_equal(a, b)))) { differed++; fprintf(stderr, "lam: %s differs from its rules\n", what); }
}
void rc_native_checked(int op, T *n, T *s) { compare(rc_native_op_name[op], n, s); }
void rc_law_checked(int law, T *l, T *s) { compare(rc_law_name[law], l, s); }
void rc_memo_checked(int memo, T *k, T *s) { compare(rc_memo_name[memo], k, s); }
#endif

static const int nib[16] = {C_lc_x0, C_lc_x1, C_lc_x2, C_lc_x3, C_lc_x4, C_lc_x5, C_lc_x6, C_lc_x7,
                            C_lc_x8, C_lc_x9, C_lc_xa, C_lc_xb, C_lc_xc, C_lc_xd, C_lc_xe, C_lc_xf};
static T *mk(int c, T *a, T *b, T *d) { T *k[3] = {a, b, d}; return rc_construct(c, k); }
static T *mk_nat(uint64_t v) {
    if (!v) return mk(C_lc_nz, 0, 0, 0);
    return mk(v & 1 ? C_lc_nb1 : C_lc_nb0, mk_nat(v >> 1), 0, 0);
}
static uint64_t nat_value(T *x) {
    T *k[1];
    int c = rc_view(S_Nat, x, k);
    if (c == C_lc_nz) return 0;
    return 2 * nat_value(k[0]) + (c == C_lc_nb1);
}
static T *mk_name(const char *s, size_t n) {
    T *r = mk(C_lc_nnil, 0, 0, 0);
    while (n--) {
        unsigned char b = (unsigned char)s[n];
        r = mk(C_lc_ncons, mk(C_lc_byte, mk(nib[b >> 4], 0, 0, 0), mk(nib[b & 15], 0, 0, 0), 0), r, 0);
    }
    return r;
}
static int nib_value(T *x) {
    int c = rc_view(S_Nib, x, NULL);
    for (int i = 0; i < 16; i++) if (nib[i] == c) return i;
    return -1;
}
static void print_name(T *s) {
    T *k[2], *b[2];
    while (rc_view(S_Str, s, k) == C_lc_ncons) {
        rc_view(S_Byte, k[0], b);
        putchar(16 * nib_value(b[0]) + nib_value(b[1]));
        s = k[1];
    }
}
static void print_term(T *t) {
    T *k[2];
    int c = rc_view(S_Term, t, k);
    if (c == C_lc_Var) { print_name(k[0]); return; }
    if (c == C_lc_App) { putchar('('); print_term(k[0]); putchar(' '); print_term(k[1]); putchar(')'); return; }
    fputs("(\\", stdout); print_name(k[0]); fputs(". ", stdout); print_term(k[1]); putchar(')');
}

/* ---- parsing ---- */
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

int main(int argc, char **argv) {
    int ai = 1;
#ifdef RC_CHECK_NATIVE
    if (ai < argc && !strcmp(argv[ai], "--check")) { rc_check_native = 1; ai++; }
#endif
    if (ai >= argc) { fputs("usage: lam [--check] CASES\n", stderr); return 2; }
    FILE *f = fopen(argv[ai], "r");
    if (!f) { perror(argv[ai]); return 2; }
    rc_runtime_init(0);
    rc_components_init();
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        p = line;
        ws();
        if (!*p || *p == '#' || *p == '\n') continue;
        fputs(line, stdout);
        if (!strncmp(p, "norm ", 5)) {
            p += 5;
            uint64_t limit = strtoull(p, (char **)&p, 10);
            T *t = term();
            T *a[3] = {mk_nat(limit), mk_nat(0), t};
            T *r = rc_call(C_lc_normalize, a), *k[2];
            if (!r) { puts("  => (no result)"); continue; }
            int c = rc_view(S_Result, r, k);
            if (c == C_lc_done) { printf("  => %" PRIu64 " steps: ", nat_value(k[1])); print_term(k[0]); }
            else { fputs("  => out of steps: ", stdout); print_term(k[0]); }
            putchar('\n');
        } else if (!strncmp(p, "alpha ", 6)) {
            p += 6;
            T *x = term();
            ws();
            if (*p == ';') p++;
            T *y = term();
            T *a[2] = {x, y};
            T *r = rc_call(C_lc_alpha_eq, a);
            printf("  => %s\n", !r ? "(no result)" : rc_view(S_Flag, r, NULL) == C_lc_yes ? "yes" : "no");
        } else if (!strncmp(p, "fv ", 3)) {
            p += 3;
            T *t = term(), *a[1] = {t}, *k[2];
            T *s = rc_call(C_lc_fv, a);
            fputs("  =>", stdout);
            while (s && rc_view(S_Names, s, k) == C_lc_names_cons) { putchar(' '); print_name(k[0]); s = k[1]; }
            putchar('\n');
        } else {
            puts("  => (unknown case)");
        }
        rc_runtime_reset();
    }
    fclose(f);
#ifdef RC_CHECK_NATIVE
    if (rc_check_native) { printf("(check %" PRIu64 " compared, %" PRIu64 " differed)\n", checked, differed); return differed ? 1 : 0; }
#endif
    return 0;
}
