/* The provenance kernel on recorded programs.
 *   prov_kernel_main_v1 FILE
 * FILE holds programs as S-expressions, each
 *   (program NAME) (label T) ... (fact P A ...) ...
 *   (rule T (lit HEAD V ...) ((lit P V ...) ...)) ...
 *   (goal P A ...) ... (bound steps K) or (bound labels K) (end)
 * A goal argument that starts with $ is open.  Terms are interned by their
 * printed form; a predicate is its name and its arity, as in Prolog.  For each program it prints (program NAME), its supports one
 * per line in byte order, and (end NAME COUNT); a support lists its labels in
 * the order of the label lines (which a front end writes in the standard order
 * of terms). */
#include "prov_kernel_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Term { char *sym; struct Term **kids; int n; } Term;

static const char *src;

static void skip(void) {
    for (;;) {
        while (*src == ' ' || *src == '\n' || *src == '\t' || *src == '\r') src++;
        if (*src == ';') { while (*src && *src != '\n') src++; continue; }
        return;
    }
}

static Term *parse(void) {
    skip();
    Term *t = calloc(1, sizeof(*t));
    if (*src == '(') {
        src++;
        int cap = 4;
        t->kids = malloc(sizeof(Term *) * cap);
        for (;;) {
            skip();
            if (*src == ')') { src++; break; }
            if (!*src) { fprintf(stderr, "unclosed\n"); exit(2); }
            if (t->n == cap) { cap *= 2; t->kids = realloc(t->kids, sizeof(Term *) * cap); }
            t->kids[t->n++] = parse();
        }
        return t;
    }
    const char *s = src;
    if (*src == '"') {
        src++;
        while (*src && *src != '"') { if (*src == '\\') src++; src++; }
        src++;
    } else {
        while (*src && *src != ' ' && *src != '\n' && *src != '(' && *src != ')' && *src != '\t' && *src != '\r') src++;
    }
    size_t len = (size_t)(src - s);
    t->sym = malloc(len + 1);
    memcpy(t->sym, s, len);
    t->sym[len] = 0;
    t->n = -1;
    return t;
}

static void print_term(const Term *t, char **buf, size_t *len, size_t *cap) {
    #define PUT(str, l) do { while (*len + (l) + 1 > *cap) { *cap *= 2; *buf = realloc(*buf, *cap); } memcpy(*buf + *len, str, l); *len += l; (*buf)[*len] = 0; } while (0)
    if (t->n < 0) { PUT(t->sym, strlen(t->sym)); return; }
    PUT("(", 1);
    for (int i = 0; i < t->n; i++) {
        if (i) PUT(" ", 1);
        print_term(t->kids[i], buf, len, cap);
    }
    PUT(")", 1);
    #undef PUT
}

static char *show(const Term *t) {
    size_t cap = 64, len = 0;
    char *b = malloc(cap);
    b[0] = 0;
    print_term(t, &b, &len, &cap);
    return b;
}

/* string interning */
typedef struct { char **keys; unsigned *ids; unsigned cap, n; char **names; unsigned ncap; } Intern;

static unsigned long hstr(const char *s) { unsigned long h = 1469598103934665603UL; while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211UL; return h; }

static unsigned intern(Intern *in, char *s, int create) {
    if (!in->cap) { in->cap = 1024; in->keys = calloc(in->cap, sizeof(char *)); in->ids = calloc(in->cap, sizeof(unsigned)); }
    if ((in->n + 1) * 2 > in->cap) {
        unsigned cap = in->cap * 2;
        char **k = calloc(cap, sizeof(char *)); unsigned *v = calloc(cap, sizeof(unsigned));
        for (unsigned i = 0; i < in->cap; i++) if (in->keys[i]) {
            unsigned j = hstr(in->keys[i]) & (cap - 1);
            while (k[j]) j = (j + 1) & (cap - 1);
            k[j] = in->keys[i]; v[j] = in->ids[i];
        }
        free(in->keys); free(in->ids); in->keys = k; in->ids = v; in->cap = cap;
    }
    unsigned j = hstr(s) & (in->cap - 1);
    while (in->keys[j]) {
        if (!strcmp(in->keys[j], s)) return in->ids[j];
        j = (j + 1) & (in->cap - 1);
    }
    if (!create) return UINT32_MAX;
    in->keys[j] = s; in->ids[j] = in->n;
    if (in->n == in->ncap) { in->ncap = in->ncap ? in->ncap * 2 : 64; in->names = realloc(in->names, sizeof(char *) * in->ncap); }
    in->names[in->n] = s;
    return in->n++;
}

static Intern labels, nodes, preds;

static void intern_reset(Intern *in) {
    free(in->keys); free(in->ids); free(in->names);
    memset(in, 0, sizeof(*in));
}

typedef struct { char **lines; size_t n, cap; } Lines;

static bool emit(void *ctx, const uint32_t *l, uint32_t n) {
    Lines *out = ctx;
    size_t len = 2;
    for (uint32_t i = 0; i < n; i++) len += strlen(labels.names[l[i]]) + 1;
    char *b = malloc(len + 1), *w = b;
    *w++ = '(';
    for (uint32_t i = 0; i < n; i++) {
        if (i) *w++ = ' ';
        size_t m = strlen(labels.names[l[i]]);
        memcpy(w, labels.names[l[i]], m);
        w += m;
    }
    *w++ = ')';
    *w = 0;
    if (out->n == out->cap) { out->cap = out->cap ? out->cap * 2 : 64; out->lines = realloc(out->lines, sizeof(char *) * out->cap); }
    out->lines[out->n++] = b;
    return true;
}

static int cmp_line(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int run_program(const char *name, ProvProgram *P, const ProvLit *goals,
                       uint32_t ngoals, uint32_t K, int reuse) {
    Lines out = {0};
    ProvStatus st = reuse ? prov_supports_reuse(P, goals, ngoals, K, emit, &out)
                          : prov_supports_graded(P, goals, ngoals, K, emit, &out);
    printf("(program %s)\n", name);
    if (st != PROV_OK) {
        printf("(status %d)\n", st);
    } else {
        qsort(out.lines, out.n, sizeof(char *), cmp_line);
        for (size_t i = 0; i < out.n; i++) puts(out.lines[i]);
    }
    printf("(end %s %zu)\n", name, out.n);
    for (size_t i = 0; i < out.n; i++) free(out.lines[i]);
    free(out.lines);
    return st == PROV_OK ? 0 : 1;
}

/* a literal (lit P V ...) of variable indices */
/* a predicate is its name and its arity, as in Prolog */
static uint32_t pred_id(Term *name, int arity, int create) {
    char *s = show(name);
    char *k = malloc(strlen(s) + 16);
    sprintf(k, "%s/%d", s, arity);
    return intern(&preds, k, create);
}

static int read_lit(Term *l, ProvLit *out, uint32_t *nv) {
    if (l->n < 3 || l->n - 2 > (int)PROV_MAX_ARITY) return 0;
    out->pred = pred_id(l->kids[1], l->n - 2, 1);
    out->arity = (uint32_t)(l->n - 2);
    for (int k = 2; k < l->n; k++) {
        uint32_t v = (uint32_t)atoi(l->kids[k]->sym);
        out->args[k - 2] = v;
        if (v + 1 > *nv) *nv = v + 1;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: prov_kernel_main_v1 FILE\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *text = malloc(sz + 1);
    if (fread(text, 1, sz, f) != (size_t)sz) { perror(argv[1]); return 2; }
    text[sz] = 0; fclose(f);
    src = text;
    ProvProgram *P = NULL;
    ProvLit goals[64]; uint32_t ngoals = 0, K = 0; int reuse = 0, failed = 0;
    char *name = NULL;
    for (;;) {
        skip();
        if (!*src) break;
        Term *t = parse();
        if (t->n < 1 || t->kids[0]->n >= 0) continue;
        const char *h = t->kids[0]->sym;
        if (!strcmp(h, "program") && t->n == 2) {
            P = prov_new(); ngoals = 0; K = 0; reuse = 0;
            name = show(t->kids[1]);
            intern_reset(&labels); intern_reset(&nodes); intern_reset(&preds);
        } else if (!P) {
            fprintf(stderr, "a statement outside a program\n"); return 2;
        } else if (!strcmp(h, "end")) {
            failed |= run_program(name, P, goals, ngoals, K, reuse);
            prov_free(P); P = NULL;
        } else if (!strcmp(h, "label") && t->n == 2) {
            intern(&labels, show(t->kids[1]), 1);
        } else if (!strcmp(h, "fact") && t->n >= 3 && t->n - 2 <= (int)PROV_MAX_ARITY) {
            uint32_t p = pred_id(t->kids[1], t->n - 2, 1), args[PROV_MAX_ARITY];
            for (int k = 2; k < t->n; k++) args[k - 2] = intern(&nodes, show(t->kids[k]), 1);
            if (!prov_add_fact(P, p, (uint32_t)(t->n - 2), args)) { fprintf(stderr, "bad fact\n"); return 2; }
        } else if (!strcmp(h, "rule") && t->n == 4) {
            uint32_t lab = intern(&labels, show(t->kids[1]), 0);
            if (lab == UINT32_MAX) { fprintf(stderr, "unknown label %s\n", show(t->kids[1])); return 2; }
            ProvLit head, lits[32]; uint32_t nv = 0;
            Term *ls = t->kids[3];
            if (!read_lit(t->kids[2], &head, &nv) || ls->n > 32) { fprintf(stderr, "bad rule\n"); return 2; }
            for (int i = 0; i < ls->n; i++)
                if (!read_lit(ls->kids[i], &lits[i], &nv)) { fprintf(stderr, "bad literal\n"); return 2; }
            if (!prov_add_rule(P, lab, &head, nv, (uint32_t)ls->n, lits)) { fprintf(stderr, "bad rule\n"); return 2; }
        } else if (!strcmp(h, "goal") && t->n >= 3 && t->n - 2 <= (int)PROV_MAX_ARITY && ngoals < 64) {
            ProvLit *g = &goals[ngoals++];
            g->pred = pred_id(t->kids[1], t->n - 2, 1);
            g->arity = (uint32_t)(t->n - 2);
            for (int k = 2; k < t->n; k++) {
                char *a = show(t->kids[k]);
                g->args[k - 2] = a[0] == '$' ? PROV_ANY : intern(&nodes, a, 1);
            }
        } else if (!strcmp(h, "bound") && t->n == 3) {
            reuse = !strcmp(t->kids[1]->sym, "labels");
            K = (uint32_t)atoi(t->kids[2]->sym);
        }
    }
    if (P) { fprintf(stderr, "a program without (end)\n"); return 2; }
    return failed;
}
