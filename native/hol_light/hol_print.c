/* hol_print.c - canonical text for types, terms and theorems.
 *
 * Canonical syntax (also documented in docs/STREAM.md):
 *   name  ::= '"' bytes '"'   with \" \\ and \xHH for bytes outside 0x20..0x7e
 *   type  ::= (V name) | (T name type*)
 *   term  ::= (v name type) | (c name type) | (A term term) | (L term term)
 *   thm   ::= (thm (term*) term)        hypotheses in kernel order
 * Every variable and constant carries its full type, so the text determines
 * the object exactly (names, not declaration identities: comparisons of
 * printed text are meaningful within one environment history).
 * The same routines feed FNV-1a 64 when HolStr.hash_only is set; the
 * OCaml oracle (oracle/hol_oracle.ml) implements the identical text.
 * The text repeats shared sub-terms, so its size (and the time to print or
 * digest it) can be exponential in the size of a DAG; with a kernel in
 * HolStr.k, printing observes that kernel's fuel and interrupt flag. */
#include "hol_internal.h"

#include <stdio.h>

static void str_grow(HolStr *s, size_t need) {
    if (s->failed) return;
    size_t ncap = s->cap ? s->cap : 256;
    while (ncap < need) ncap *= 2;
    char *nb = s->k ? (char *)hk_realloc(s->k, s->buf, s->cap, ncap) : (char *)realloc(s->buf, ncap);
    if (!nb) { s->failed = true; return; }
    s->buf = nb;
    s->cap = ncap;
}

void hk_str_put(HolStr *s, const char *p, size_t n) {
    if (s->hash_only) { s->fnv = hk_fnv1a(p, n, s->fnv); return; }
    if (s->failed) return;
    if (s->len + n + 1 > s->cap) str_grow(s, s->len + n + 1);
    if (s->failed) return;
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = 0;
}

static void put_c(HolStr *s, const char *lit) { hk_str_put(s, lit, strlen(lit)); }

/* One node printed: false (and the text marked failed) when the attached
 * kernel's fuel is exhausted or its interrupt flag is set. */
static bool print_tick(HolStr *s) {
    if (s->failed) return false;
    if (!s->k || hk_tick(s->k)) return true;
    s->failed = true;
    return false;
}

void hk_print_name(HolStr *s, const HolNameRec *n) {
    static const char hex[] = "0123456789abcdef";
    hk_str_put(s, "\"", 1);
    size_t start = 0;
    for (size_t i = 0; i < n->len; i++) {
        unsigned char c = (unsigned char)n->bytes[i];
        if (c >= 0x20 && c <= 0x7e && c != '"' && c != '\\') continue;
        hk_str_put(s, n->bytes + start, i - start);
        if (c == '"') hk_str_put(s, "\\\"", 2);
        else if (c == '\\') hk_str_put(s, "\\\\", 2);
        else {
            char e[4] = {'\\', 'x', hex[c >> 4], hex[c & 15]};
            hk_str_put(s, e, 4);
        }
        start = i + 1;
    }
    hk_str_put(s, n->bytes + start, n->len - start);
    hk_str_put(s, "\"", 1);
}

int hk_print_type(HolKernel *k, HolStr *s, HolType *t) {
    if (!print_tick(s)) return -1;
    if (t->kind == HOL_TY_VAR) {
        put_c(s, "(V ");
        hk_print_name(s, &k->names[t->name]);
        put_c(s, ")");
    } else {
        put_c(s, "(T ");
        hk_print_name(s, &k->names[t->name]);
        for (uint32_t i = 0; i < t->nargs; i++) {
            put_c(s, " ");
            hk_print_type(k, s, t->args[i]);
        }
        put_c(s, ")");
    }
    return s->failed ? -1 : 0;
}

int hk_print_term(HolKernel *k, HolStr *s, HolTerm *t) {
    if (!print_tick(s)) return -1;
    switch (t->kind) {
    case HOL_TM_VAR:
    case HOL_TM_CONST:
        put_c(s, t->kind == HOL_TM_VAR ? "(v " : "(c ");
        hk_print_name(s, &k->names[t->u.leaf.name]);
        put_c(s, " ");
        hk_print_type(k, s, t->type);
        put_c(s, ")");
        break;
    default:
        put_c(s, t->kind == HOL_TM_COMB ? "(A " : "(L ");
        hk_print_term(k, s, t->u.node.l);
        put_c(s, " ");
        hk_print_term(k, s, t->u.node.r);
        put_c(s, ")");
    }
    return s->failed ? -1 : 0;
}

int hk_print_thm(HolKernel *k, HolStr *s, HolThm *th) {
    put_c(s, "(thm (");
    for (uint32_t i = 0; i < th->nhyps; i++) {
        if (i) put_c(s, " ");
        hk_print_term(k, s, th->hyps[i]);
    }
    put_c(s, ") ");
    hk_print_term(k, s, th->concl);
    put_c(s, ")");
    return s->failed ? -1 : 0;
}

/* Alpha-normal text: binders print only their type, a bound occurrence
 * prints its de Bruijn index (distance to the innermost enclosing binder
 * equal to it), free variables and constants print as above.  Two terms
 * have equal alpha-normal text iff alphaorder says they are equal. */
typedef struct { HolTerm **v; size_t n, cap; bool failed; } BStack;

static void alpha_term(HolKernel *k, HolStr *s, HolTerm *t, BStack *bs) {
    char num[32];
    if (!print_tick(s)) return;
    if (t->kind == HOL_TM_VAR)
        for (size_t i = bs->n; i-- > 0;)
            if (bs->v[i] == t) {
                int n = snprintf(num, sizeof num, "(b %zu)", bs->n - 1 - i);
                hk_str_put(s, num, (size_t)n);
                return;
            }
    switch (t->kind) {
    case HOL_TM_VAR: /* free */
    case HOL_TM_CONST:
        put_c(s, t->kind == HOL_TM_VAR ? "(v " : "(c ");
        hk_print_name(s, &k->names[t->u.leaf.name]);
        put_c(s, " ");
        hk_print_type(k, s, t->type);
        put_c(s, ")");
        return;
    case HOL_TM_COMB:
        put_c(s, "(A ");
        alpha_term(k, s, t->u.node.l, bs);
        put_c(s, " ");
        alpha_term(k, s, t->u.node.r, bs);
        put_c(s, ")");
        return;
    default:
        if (bs->n == bs->cap) {
            size_t ncap = bs->cap ? bs->cap * 2 : 32;
            HolTerm **nv = (HolTerm **)realloc(bs->v, ncap * sizeof(HolTerm *));
            if (!nv) { bs->failed = true; s->failed = true; return; }
            bs->v = nv;
            bs->cap = ncap;
        }
        put_c(s, "(L ");
        hk_print_type(k, s, t->u.node.l->type);
        put_c(s, " ");
        bs->v[bs->n++] = t->u.node.l;
        alpha_term(k, s, t->u.node.r, bs);
        bs->n--;
        put_c(s, ")");
    }
}

int hk_print_thm_alpha(HolKernel *k, HolStr *s, HolThm *th) {
    BStack bs = {0};
    put_c(s, "(thm (");
    for (uint32_t i = 0; i < th->nhyps; i++) {
        if (i) put_c(s, " ");
        alpha_term(k, s, th->hyps[i], &bs);
    }
    put_c(s, ") ");
    alpha_term(k, s, th->concl, &bs);
    put_c(s, ")");
    free(bs.v);
    return s->failed || bs.failed ? -1 : 0;
}

int hk_print_term_alpha(HolKernel *k, HolStr *s, HolTerm *t) {
    BStack bs = {0};
    alpha_term(k, s, t, &bs);
    free(bs.v);
    return s->failed || bs.failed ? -1 : 0;
}

/* ------------------------------------------------------------------------ */
/* Reader                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    HolKernel *k;
    HolDecl *env;
    const char *p, *end;
    uint32_t depth;
} Rd;

static void *rd_malformed(Rd *r) { return hk_fail(r->k, HOL_MALFORMED, HOL_R_STREAM_VALUE); }

static void rd_ws(Rd *r) {
    while (r->p < r->end && (*r->p == ' ' || *r->p == '\n' || *r->p == '\t' || *r->p == '\r'))
        r->p++;
}

static bool rd_lit(Rd *r, char c) {
    rd_ws(r);
    if (r->p < r->end && *r->p == c) { r->p++; return true; }
    return false;
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static HolName rd_name(Rd *r) {
    rd_ws(r);
    if (r->p >= r->end || *r->p != '"') { rd_malformed(r); return 0; }
    r->p++;
    size_t cap = 64, n = 0;
    char *buf = (char *)hk_alloc(r->k, cap);
    if (!buf) return 0;
    HolName out = 0;
    for (;;) {
        if (r->p >= r->end) { rd_malformed(r); goto done; }
        char c = *r->p++;
        if (c == '"') break;
        if (c == '\\') {
            if (r->p >= r->end) { rd_malformed(r); goto done; }
            char e = *r->p++;
            if (e == '"' || e == '\\') c = e;
            else if (e == 'x' && r->end - r->p >= 2 && hexv(r->p[0]) >= 0 && hexv(r->p[1]) >= 0) {
                c = (char)(hexv(r->p[0]) * 16 + hexv(r->p[1]));
                r->p += 2;
            } else { rd_malformed(r); goto done; }
        }
        if (n + 1 >= cap) {
            char *nb = (char *)hk_realloc(r->k, buf, cap, cap * 2);
            if (!nb) goto done;
            buf = nb;
            cap *= 2;
        }
        buf[n++] = c;
    }
    out = hk_name_intern(r->k, buf, n);
done:
    hk_dealloc(r->k, buf, cap);
    return out;
}

static HolType *rd_type(Rd *r);

static HolType *rd_type(Rd *r) {
    if (++r->depth > r->k->max_depth) { r->depth--; return hk_fail(r->k, HOL_LIMIT, HOL_R_DEPTH); }
    HolType *res = NULL;
    if (!rd_lit(r, '(')) { rd_malformed(r); goto out; }
    rd_ws(r);
    if (r->p >= r->end) { rd_malformed(r); goto out; }
    char tag = *r->p++;
    HolName name = rd_name(r);
    if (!name) goto out;
    if (tag == 'V') {
        if (!rd_lit(r, ')')) { rd_malformed(r); goto out; }
        res = hk_ty_var(r->k, name);
    } else if (tag == 'T') {
        HolVec args = {0};
        for (;;) {
            rd_ws(r);
            if (r->p < r->end && *r->p == ')') { r->p++; break; }
            HolType *a = rd_type(r);
            if (!a || hk_vec_push(r->k, &args, a) < 0) { hk_vec_free(r->k, &args); goto out; }
        }
        res = hk_mk_type_in(r->k, r->env, name, (HolType *const *)args.v, (uint32_t)args.n);
        hk_vec_free(r->k, &args);
    } else rd_malformed(r);
out:
    r->depth--;
    return res;
}

static HolTerm *rd_term(Rd *r) {
    if (++r->depth > r->k->max_depth) { r->depth--; return hk_fail(r->k, HOL_LIMIT, HOL_R_DEPTH); }
    HolTerm *res = NULL;
    if (!rd_lit(r, '(')) { rd_malformed(r); goto out; }
    rd_ws(r);
    if (r->p >= r->end) { rd_malformed(r); goto out; }
    char tag = *r->p++;
    if (tag == 'v' || tag == 'c') {
        HolName name = rd_name(r);
        if (!name) goto out;
        HolType *ty = rd_type(r);
        if (!ty) goto out;
        if (!rd_lit(r, ')')) { rd_malformed(r); goto out; }
        res = tag == 'v' ? hk_tm_var(r->k, name, ty) : hk_mk_const_at(r->k, r->env, name, ty);
    } else if (tag == 'A' || tag == 'L') {
        HolTerm *a = rd_term(r);
        if (!a) goto out;
        HolTerm *b = rd_term(r);
        if (!b) goto out;
        if (!rd_lit(r, ')')) { rd_malformed(r); goto out; }
        res = tag == 'A' ? hk_tm_comb(r->k, a, b) : hk_tm_abs(r->k, a, b);
    } else rd_malformed(r);
out:
    r->depth--;
    return res;
}

HolType *hk_parse_type(HolKernel *k, HolDecl *env, const char *text, size_t len) {
    Rd r = {k, env, text, text + len, 0};
    HolType *t = rd_type(&r);
    if (t) {
        rd_ws(&r);
        if (r.p != r.end) return rd_malformed(&r);
    }
    return t;
}

HolTerm *hk_parse_term(HolKernel *k, HolDecl *env, const char *text, size_t len) {
    Rd r = {k, env, text, text + len, 0};
    HolTerm *t = rd_term(&r);
    if (t) {
        rd_ws(&r);
        if (r.p != r.end) return rd_malformed(&r);
    }
    return t;
}
