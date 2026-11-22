/* util.c - see rulec.h */
#include "rulec.h"

/* ------------------------------------------------------------------ utilities */

jmp_buf *rulec_die_jump;
void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("rulec: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    if (rulec_die_jump) longjmp(*rulec_die_jump, 1);
    exit(2);
}

/* a string of the S-expression files rulec writes: quoted, " and \ escaped */
void fput_quoted(FILE *o, const char *s) {
    fputc('"', o);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', o);
        if (*s == '\n') { fputs("\\n", o); continue; }
        fputc(*s, o);
    }
    fputc('"', o);
}

/* a C string literal: " \ and control bytes escaped */
void fput_cstring(FILE *o, const char *s) {
    fputc('"', o);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', o); fputc(c, o); }
        else if (c < 32 || c == 127) fprintf(o, "\\%03o", c);
        else fputc(c, o);
    }
    fputc('"', o);
}

void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("out of memory"); return p; }
void *xcalloc(size_t a, size_t b) { void *p = calloc(a ? a : 1, b ? b : 1); if (!p) die("out of memory"); return p; }
void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) die("out of memory"); return p; }
char *xstrdup(const char *s) { char *d = strdup(s); if (!d) die("out of memory"); return d; }

uint64_t hash_str(const char *s) { uint64_t h = 1469598103934665603ull; while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ull; } return h; }
int map_get(const Map *m, const char *k) {
    if (!m->cap) return -1;
    for (size_t i = hash_str(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
        if (!m->keys[i]) return -1;
        if (!strcmp(m->keys[i], k)) return m->vals[i];
    }
}
void map_put(Map *m, const char *k, int v) {
    if (2 * (m->len + 1) > m->cap) {
        size_t oc = m->cap; char **ok = m->keys; int *ov = m->vals;
        m->cap = oc ? 2 * oc : 64; m->keys = xcalloc(m->cap, sizeof(char *)); m->vals = xcalloc(m->cap, sizeof(int)); m->len = 0;
        for (size_t i = 0; i < oc; i++) if (ok[i]) map_put(m, ok[i], ov[i]);
        free(ok); free(ov);
    }
    size_t i = hash_str(k) & (m->cap - 1);
    while (m->keys[i]) { if (!strcmp(m->keys[i], k)) { m->vals[i] = v; return; } i = (i + 1) & (m->cap - 1); }
    m->keys[i] = xstrdup(k); m->vals[i] = v; m->len++;
}

/* ------------------------------------------------------------- S-expressions */


Sx *sx_read(const char **pp) {
    const char *p = *pp;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) { *pp = p; return NULL; }
    Sx *x = xcalloc(1, sizeof *x);
    if (*p == '(') {
        p++;
        int cap = 0;
        x->k = SX_LIST;
        for (;;) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) die("unterminated list");
            if (*p == ')') { p++; break; }
            *pp = p;
            Sx *e = sx_read(pp);
            p = *pp;
            VEC_PUSH(x->v, x->n, cap, e);
        }
    } else if (*p == '"') {
        p++;
        size_t cap = 16, n = 0;
        char *b = xmalloc(cap);
        while (*p != '"') {
            if (!*p) die("unterminated string");
            char c = *p++;
            if (c == '\\') {
                c = *p++;
                if (c == 'n') c = '\n'; else if (c == 't') c = '\t';
                else if (c != '"' && c != '\\') die("unsupported escape \\%c", c);
            }
            if (n + 2 > cap) { cap *= 2; b = xrealloc(b, cap); }
            b[n++] = c;
        }
        p++;
        b[n] = 0;
        x->k = SX_STR; x->s = b; x->n = (int)n;
    } else if (*p == ')') {
        die("unexpected )");
    } else {
        const char *q = p;
        while (*q && !isspace((unsigned char)*q) && *q != '(' && *q != ')' && *q != '"') q++;
        x->k = SX_SYM; x->s = strndup(p, (size_t)(q - p)); x->n = (int)(q - p);
        p = q;
    }
    *pp = p;
    return x;
}

bool sx_is_sym(const Sx *x, const char *s) { return x && x->k == SX_SYM && !strcmp(x->s, s); }
bool sx_head(const Sx *x, const char *s) { return x && x->k == SX_LIST && x->n > 0 && sx_is_sym(x->v[0], s); }
const char *sx_str(const Sx *x, const char *what) {
    if (!x || x->k != SX_STR) die("expected a string for %s", what);
    return x->s;
}
/* a symbol or a string */
const char *sx_atom(const Sx *x, const char *what) {
    if (!x || x->k == SX_LIST) die("expected a symbol or a string for %s", what);
    return x->s;
}
/* LCons/LNil list -> items */
int sx_items(const Sx *x, Sx ***out) {
    int n = 0, cap = 0;
    Sx **items = NULL;
    while (sx_head(x, "LCons")) {
        if (x->n != 3) die("malformed LCons");
        VEC_PUSH(items, n, cap, x->v[1]);
        x = x->v[2];
    }
    if (!sx_is_sym(x, "LNil")) die("malformed list (expected LNil)");
    *out = items;
    return n;
}

