/*
 * EXPERIMENTAL Vibe-ITP certificate reader for the NIK kernel authority.
 *
 * Status: hand-written stepping stone, NOT production authority.  The
 * certificate grammar and the checker protocol are specified in Lean
 * (Mettapedia.Languages.VibeITP.Spec: Instr, Protocol); the generic generator
 * cannot yet produce a reader for length-prefixed binary grammars or the slot
 * protocol, so this program realizes both by hand.
 *
 * Division of trust:
 *  - every accepted logical fact (theorem, term formation, symbol declaration
 *    use, definition admission) is checked by the generic NIK checker against
 *    the admitted VIBE-ITP authority (the kernel rule package exported from
 *    Lean); this program only produces the derivations;
 *  - the protocol (slots, phases, challenge bookkeeping, error classes) and
 *    the byte decoding are enforced here and are trusted as experimental code;
 *  - rejections are decided here by computing the kernel operations; they are
 *    checked against the reference checker by differential testing.
 *
 * The execution primitive THM_JIT is outside the hosted fragment: reaching it
 * ends the run with verdict "unsupported".
 */
#include "atom.h"
#include "inference_checker.h"
#include "parser.h"
#include "symbol.h"
#include "vibe_itp_authorities_v1.generated.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef unsigned __int128 u128;
#define WORD_BOUND ((u128)1 << 64)

/* ------------------------------------------------------------------ */
/* Diagnostics and verdicts                                           */
/* ------------------------------------------------------------------ */

static char g_fault_buf[2048];

static void fault(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_fault_buf, sizeof g_fault_buf, fmt, ap);
    va_end(ap);
    printf("VERDICT fault detail=\"%s\"\n", g_fault_buf);
    fflush(stdout);
    exit(3);
}

/* ------------------------------------------------------------------ */
/* Persistent pattern construction (hash-consed)                      */
/* ------------------------------------------------------------------ */

static Arena g_persist;     /* patterns, admitted rules: whole run */
static Arena g_scratch;     /* trace instantiations: reset per step */
static Atom *A_PApp, *A_LCons, *A_LNil, *A_GRuleV1;

enum {
    H_VP1, H_VPO, H_VPI, H_VN0, H_VNPos, H_VU0, H_VUS, H_VFalse, H_VTrue, H_VNil,
    H_VCons, H_VKConst, H_VKFvar, H_VSym, H_VAnn, H_VBVar, H_VLit, H_VApp,
    H_VSymDecl, H_VThm, H_VWf, H_VDepth, H_VDefStmt, H_COUNT
};
static const char *g_head_names[H_COUNT] = {
    "VP1", "VPO", "VPI", "VN0", "VNPos", "VU0", "VUS", "VFalse", "VTrue", "VNil",
    "VCons", "VKConst", "VKFvar", "VSym", "VAnn", "VBVar", "VLit", "VApp",
    "VSymDecl", "VThm", "VWf", "VDepth", "VDefStmt"};
static Atom *g_head_atoms[H_COUNT];

/* Hash-consing of 3-element expressions (head, a, b). */
typedef struct { Atom *e0, *e1, *e2, *value; } ConsEntry;
static ConsEntry *g_cons;
static size_t g_cons_cap, g_cons_count;

static size_t cons_hash(Atom *a, Atom *b, Atom *c) {
    uint64_t h = (uint64_t)(uintptr_t)a * 0x9e3779b97f4a7c15ull;
    h ^= (uint64_t)(uintptr_t)b + 0x7f4a7c159e3779b9ull + (h << 6) + (h >> 2);
    h ^= (uint64_t)(uintptr_t)c + 0x94d049bb133111ebull + (h << 6) + (h >> 2);
    h ^= h >> 31;
    return (size_t)h;
}

static Atom *expr3(Atom *a, Atom *b, Atom *c) {
    if (g_cons_count * 2 >= g_cons_cap) {
        size_t nc = g_cons_cap ? g_cons_cap * 2 : (1u << 20);
        ConsEntry *nt = calloc(nc, sizeof *nt);
        if (!nt) fault("out of memory (pattern table)");
        for (size_t i = 0; i < g_cons_cap; i++) {
            if (!g_cons[i].value) continue;
            size_t s = cons_hash(g_cons[i].e0, g_cons[i].e1, g_cons[i].e2) & (nc - 1);
            while (nt[s].value) s = (s + 1) & (nc - 1);
            nt[s] = g_cons[i];
        }
        free(g_cons);
        g_cons = nt;
        g_cons_cap = nc;
    }
    size_t s = cons_hash(a, b, c) & (g_cons_cap - 1);
    while (g_cons[s].value) {
        if (g_cons[s].e0 == a && g_cons[s].e1 == b && g_cons[s].e2 == c)
            return g_cons[s].value;
        s = (s + 1) & (g_cons_cap - 1);
    }
    Atom *v = atom_expr3(&g_persist, a, b, c);
    g_cons[s] = (ConsEntry){a, b, c, v};
    g_cons_count++;
    return v;
}

static Atom *plist(Atom **items, size_t n) {
    Atom *list = A_LNil;
    for (size_t i = n; i > 0; i--) list = expr3(A_LCons, items[i - 1], list);
    return list;
}

static Atom *papp(int head, Atom **args, size_t n) {
    return expr3(A_PApp, g_head_atoms[head], plist(args, n));
}
static Atom *papp0(int head) { return papp(head, NULL, 0); }
static Atom *papp1(int head, Atom *a) { return papp(head, &a, 1); }
static Atom *papp2(int head, Atom *a, Atom *b) { Atom *x[2] = {a, b}; return papp(head, x, 2); }
static Atom *papp3(int head, Atom *a, Atom *b, Atom *c) { Atom *x[3] = {a, b, c}; return papp(head, x, 3); }

static Atom *papp1_named(const char *head, Atom *a) {
    Atom *items[1] = {a};
    return expr3(A_PApp, atom_string(&g_persist, head), plist(items, 1));
}

static Atom *P_P1, *P_N0, *P_U0, *P_False, *P_True, *P_Nil, *P_KConst, *P_KFvar;

static Atom *pat_cons(Atom *x, Atom *xs) { return papp2(H_VCons, x, xs); }
static Atom *pat_bool(bool b) { return b ? P_True : P_False; }

/* Numeral cache. */
typedef struct { u128 key; Atom *pos; bool used; } NumEntry;
static NumEntry *g_num;
static size_t g_num_cap, g_num_count;

static Atom *pat_pos(u128 v);

static Atom *pat_pos_build(u128 v) {
    if (v <= 1) return P_P1;
    Atom *inner = pat_pos(v / 2);
    return papp1((v & 1) ? H_VPI : H_VPO, inner);
}

static Atom *pat_pos(u128 v) {
    if (v == 0) fault("internal: positive numeral of zero");
    if (v == 1) return P_P1;
    if (g_num_count * 2 >= g_num_cap) {
        size_t nc = g_num_cap ? g_num_cap * 2 : 4096;
        NumEntry *nt = calloc(nc, sizeof *nt);
        for (size_t i = 0; i < g_num_cap; i++) {
            if (!g_num[i].used) continue;
            size_t s = (size_t)((uint64_t)g_num[i].key * 0x9e3779b97f4a7c15ull ^ (uint64_t)(g_num[i].key >> 64)) & (nc - 1);
            while (nt[s].used) s = (s + 1) & (nc - 1);
            nt[s] = g_num[i];
        }
        free(g_num);
        g_num = nt;
        g_num_cap = nc;
    }
    size_t s = (size_t)((uint64_t)v * 0x9e3779b97f4a7c15ull ^ (uint64_t)(v >> 64)) & (g_num_cap - 1);
    while (g_num[s].used) {
        if (g_num[s].key == v) return g_num[s].pos;
        s = (s + 1) & (g_num_cap - 1);
    }
    Atom *p = pat_pos_build(v);
    /* the table may have been reorganized by the recursive build */
    s = (size_t)((uint64_t)v * 0x9e3779b97f4a7c15ull ^ (uint64_t)(v >> 64)) & (g_num_cap - 1);
    while (g_num[s].used) s = (s + 1) & (g_num_cap - 1);
    g_num[s] = (NumEntry){v, p, true};
    g_num_count++;
    return p;
}

static Atom *pat_nat(u128 v) { return v == 0 ? P_N0 : papp1(H_VNPos, pat_pos(v)); }

static Atom *g_unary[70];
static Atom *pat_unary(unsigned k) {
    if (k >= 70) fault("internal: unary numeral too large");
    return g_unary[k];
}

/* ------------------------------------------------------------------ */
/* Symbols and terms (hash-consed)                                    */
/* ------------------------------------------------------------------ */

typedef struct Sym {
    int kind;             /* 0 constant, 1 fvar, 2 bvar marker, 3 literal marker */
    uint64_t arity;
    uint64_t *binders;
    u128 id;              /* numeric identity: slot for built-ins, 13 + n for fresh */
    Atom *pat;            /* VSym(id, kind, binders) */
    Atom *binders_pat;
    CettaInferenceRuleHandle decl_rule;
} Sym;

enum { T_BVAR, T_LIT, T_APP };
typedef struct Term Term;
struct Term {
    uint8_t tag;
    bool hasfv;
    uint64_t depth;
    uint64_t hash;
    Sym *sym;
    uint64_t n;           /* number of args, or literal length */
    Term **args;
    uint8_t *bytes;
    uint64_t bvar;
    Term *next;
    Atom *pat;            /* lazily built */
    Atom *args_pat;       /* list of argument patterns */
    int64_t wf_saved;     /* saved index of VWf(pat), or -1 */
    uint32_t visit_epoch;
};

static Term **g_terms;
static uint64_t g_terms_cap = 1u << 22, g_terms_count;

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

static Term *intern(Term *t) {
    uint64_t i = t->hash & (g_terms_cap - 1);
    for (Term *u = g_terms[i]; u; u = u->next) {
        if (u->hash != t->hash || u->tag != t->tag) continue;
        if (t->tag == T_BVAR) { if (u->bvar == t->bvar) return u; }
        else if (t->tag == T_LIT) {
            if (u->n == t->n && (t->n == 0 || memcmp(u->bytes, t->bytes, t->n) == 0)) return u;
        } else if (u->sym == t->sym && u->n == t->n &&
                   (t->n == 0 || memcmp(u->args, t->args, t->n * sizeof(Term *)) == 0))
            return u;
    }
    Term *c = malloc(sizeof *c);
    *c = *t;
    c->pat = NULL;
    c->args_pat = NULL;
    c->wf_saved = -1;
    c->visit_epoch = 0;
    if (t->tag == T_LIT) {
        c->bytes = malloc(t->n ? t->n : 1);
        if (t->n) memcpy(c->bytes, t->bytes, t->n);
    }
    if (t->tag == T_APP && t->n) {
        c->args = malloc(t->n * sizeof(Term *));
        memcpy(c->args, t->args, t->n * sizeof(Term *));
    }
    c->next = g_terms[i];
    g_terms[i] = c;
    g_terms_count++;
    if (g_terms_count > g_terms_cap) {
        uint64_t nc = g_terms_cap * 2;
        Term **nt = calloc(nc, sizeof *nt);
        for (uint64_t k = 0; k < g_terms_cap; k++)
            for (Term *u = g_terms[k], *nx; u; u = nx) {
                nx = u->next;
                uint64_t j = u->hash & (nc - 1);
                u->next = nt[j];
                nt[j] = u;
            }
        free(g_terms);
        g_terms = nt;
        g_terms_cap = nc;
    }
    return c;
}

static Term *mk_bvar(uint64_t i) {
    Term t = {0};
    t.tag = T_BVAR;
    t.bvar = i;
    t.depth = i + 1;
    t.hash = mix(1, i);
    return intern(&t);
}

static Term *mk_lit(const uint8_t *b, uint64_t n) {
    Term t = {0};
    t.tag = T_LIT;
    t.n = n;
    t.bytes = (uint8_t *)b;
    uint64_t h = mix(2, n);
    for (uint64_t i = 0; i < n; i++) h = mix(h, b[i]);
    t.hash = h;
    return intern(&t);
}

static Term *mk_app(Sym *s, Term **args) {
    Term t = {0};
    t.tag = T_APP;
    t.sym = s;
    t.n = s->arity;
    t.args = args;
    uint64_t h = mix(3, (uint64_t)(uintptr_t)s);
    t.hasfv = s->kind == 1;
    for (uint64_t i = 0; i < t.n; i++) {
        h = mix(h, (uint64_t)(uintptr_t)args[i]);
        if (args[i]->hasfv) t.hasfv = true;
        uint64_t b = s->binders[i];
        if (args[i]->depth > b && args[i]->depth - b > t.depth) t.depth = args[i]->depth - b;
    }
    t.hash = h;
    return intern(&t);
}

static Term *mk_natlit(uint64_t v) {
    uint8_t b[8];
    if (v < 256) { b[0] = (uint8_t)v; return mk_lit(b, 1); }
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
    return mk_lit(b, 8);
}

static Atom *bytes_pat(const uint8_t *b, uint64_t n) {
    Atom *list = P_Nil;
    for (uint64_t i = n; i > 0; i--) list = pat_cons(pat_nat(b[i - 1]), list);
    return list;
}

static Atom *term_pat(Term *t);

static Atom *term_args_pat(Term *t) {
    if (t->args_pat) return t->args_pat;
    Atom *list = P_Nil;
    for (uint64_t i = t->n; i > 0; i--) list = pat_cons(term_pat(t->args[i - 1]), list);
    t->args_pat = list;
    return list;
}

static Atom *ann_pat(uint64_t depth, bool fv) { return papp2(H_VAnn, pat_nat(depth), pat_bool(fv)); }

static Atom *term_pat(Term *t) {
    if (t->pat) return t->pat;
    if (t->tag == T_BVAR) t->pat = papp1(H_VBVar, pat_nat(t->bvar));
    else if (t->tag == T_LIT) t->pat = papp1(H_VLit, bytes_pat(t->bytes, t->n));
    else t->pat = papp3(H_VApp, t->sym->pat, term_args_pat(t), ann_pat(t->depth, t->hasfv));
    return t->pat;
}

/* suffix list of binders starting at index i */
static Atom *binders_suffix_pat(Sym *s, uint64_t i) {
    Atom *list = P_Nil;
    for (uint64_t k = s->arity; k > i; k--) list = pat_cons(pat_nat(s->binders[k - 1]), list);
    return list;
}

static Atom *terms_suffix_pat(Term **ts, uint64_t n, uint64_t i) {
    Atom *list = P_Nil;
    for (uint64_t k = n; k > i; k--) list = pat_cons(term_pat(ts[k - 1]), list);
    return list;
}

static uint64_t g_next_id = 13;
static Sym g_builtin[13];
static uint64_t g_zero_binders[8];

static void sym_finish(Sym *s) {
    s->binders_pat = binders_suffix_pat(s, 0);
    s->pat = papp3(H_VSym, pat_nat(s->id), s->kind == 1 ? P_KFvar : P_KConst, s->binders_pat);
}

/* ------------------------------------------------------------------ */
/* Checker, rules, trace                                              */
/* ------------------------------------------------------------------ */

static CettaInferenceChecker *g_checker;
static CettaInferenceTrace g_trace;
static char g_err[4096];
static uint64_t g_actions, g_steps;

#define RULES(X) \
    X(psucc_1, "vibe-psucc-1") X(psucc_o, "vibe-psucc-o") X(psucc_i, "vibe-psucc-i") \
    X(padd_1l, "vibe-padd-1l") X(padd_1r, "vibe-padd-1r") X(padd_oo, "vibe-padd-oo") \
    X(padd_oi, "vibe-padd-oi") X(padd_io, "vibe-padd-io") X(padd_ii, "vibe-padd-ii") \
    X(paddc_11, "vibe-paddc-11") X(paddc_1o, "vibe-paddc-1o") X(paddc_1i, "vibe-paddc-1i") \
    X(paddc_o1, "vibe-paddc-o1") X(paddc_i1, "vibe-paddc-i1") X(paddc_oo, "vibe-paddc-oo") \
    X(paddc_oi, "vibe-paddc-oi") X(paddc_io, "vibe-paddc-io") X(paddc_ii, "vibe-paddc-ii") \
    X(nadd_0l, "vibe-nadd-0l") X(nadd_0r, "vibe-nadd-0r") X(nadd_pp, "vibe-nadd-pp") \
    X(nlt, "vibe-nlt") X(nle, "vibe-nle") X(nmonus_le, "vibe-nmonus-le") \
    X(nmonus_gt, "vibe-nmonus-gt") X(nmax_le, "vibe-nmax-le") X(nmax_gt, "vibe-nmax-gt") \
    X(bor_ff, "vibe-bor-ff") X(bor_ft, "vibe-bor-ft") X(bor_tf, "vibe-bor-tf") \
    X(bor_tt, "vibe-bor-tt") X(pbits_1, "vibe-pbits-1") X(pbits_o, "vibe-pbits-o") \
    X(pbits_i, "vibe-pbits-i") X(nword_0, "vibe-nword-0") X(nword_p, "vibe-nword-p") \
    X(pmul_1, "vibe-pmul-1") X(pmul_o, "vibe-pmul-o") X(pmul_i, "vibe-pmul-i") \
    X(nmul_0l, "vibe-nmul-0l") X(nmul_0r, "vibe-nmul-0r") X(nmul_pp, "vibe-nmul-pp") \
    X(ndivmod, "vibe-ndivmod") X(nmod64, "vibe-nmod64") \
    X(len_nil, "vibe-len-nil") X(len_cons, "vibe-len-cons") X(nth_0, "vibe-nth-0") \
    X(nth_s, "vibe-nth-s") X(bytes_nil, "vibe-bytes-nil") X(bytes_cons, "vibe-bytes-cons") \
    X(wf_bvar, "vibe-wf-bvar") X(wf_lit, "vibe-wf-lit") X(wf_app, "vibe-wf-app") \
    X(wfargs_nil, "vibe-wfargs-nil") X(wfargs_cons, "vibe-wfargs-cons") \
    X(kindfv_const, "vibe-kindfv-const") X(kindfv_fvar, "vibe-kindfv-fvar") \
    X(depth_bvar, "vibe-depth-bvar") X(depth_lit, "vibe-depth-lit") X(depth_app, "vibe-depth-app") \
    X(hasfv_bvar, "vibe-hasfv-bvar") X(hasfv_lit, "vibe-hasfv-lit") X(hasfv_app, "vibe-hasfv-app") \
    X(annargs_nil, "vibe-annargs-nil") X(annargs_cons, "vibe-annargs-cons") \
    X(shift_zero, "vibe-shift-zero") X(shift_low, "vibe-shift-low") X(shift_bvar, "vibe-shift-bvar") \
    X(shift_app, "vibe-shift-app") X(shiftargs_nil, "vibe-shiftargs-nil") \
    X(shiftargs_cons, "vibe-shiftargs-cons") \
    X(subst_low, "vibe-subst-low") X(subst_param, "vibe-subst-param") \
    X(subst_above, "vibe-subst-above") X(subst_app, "vibe-subst-app") \
    X(substargs_nil, "vibe-substargs-nil") X(substargs_cons, "vibe-substargs-cons") \
    X(substtop_0, "vibe-substtop-0") X(substtop_p, "vibe-substtop-p") \
    X(inst_nofv, "vibe-inst-nofv") X(inst_const, "vibe-inst-const") \
    X(inst_other_lt, "vibe-inst-other-lt") X(inst_other_gt, "vibe-inst-other-gt") \
    X(inst_hit, "vibe-inst-hit") X(instargs_nil, "vibe-instargs-nil") \
    X(instargs_cons, "vibe-instargs-cons") \
    X(natlit_small, "vibe-natlit-small") X(natlit_large, "vibe-natlit-large") \
    X(thm_mp, "vibe-mp") X(thm_inst, "vibe-inst") X(lit_isnat, "vibe-lit-isnat") \
    X(lit_lt, "vibe-lit-lt") X(lit_add, "vibe-lit-add") X(lit_mul, "vibe-lit-mul") \
    X(lit_div, "vibe-lit-div") X(lit_length, "vibe-lit-length") X(lit_get, "vibe-lit-get") \
    X(arities_nil, "vibe-arities-nil") X(arities_cons, "vibe-arities-cons") \
    X(hintsin_nil, "vibe-hintsin-nil") X(hintsin_cons, "vibe-hintsin-cons") \
    X(occ_bvar, "vibe-occ-bvar") X(occ_lit, "vibe-occ-lit") X(occ_const, "vibe-occ-const") \
    X(occ_fvar, "vibe-occ-fvar") X(occargs_nil, "vibe-occargs-nil") \
    X(occargs_cons, "vibe-occargs-cons") X(desc_0, "vibe-desc-0") X(desc_s, "vibe-desc-s") \
    X(etas_nil, "vibe-etas-nil") X(etas_cons, "vibe-etas-cons") X(defstmt, "vibe-defstmt")

#define DECL_RULE(field, name) static CettaInferenceRuleHandle R_##field;
RULES(DECL_RULE)
#undef DECL_RULE

static void lookup_rules(void) {
#define FIND_RULE(field, name) \
    R_##field = cetta_inference_checker_find_rule(g_checker, name); \
    if (R_##field == CETTA_INFERENCE_RULE_HANDLE_NONE) fault("authority lacks rule %s", name);
    RULES(FIND_RULE)
#undef FIND_RULE
}

static uint64_t g_rule_uses[512];
static uint64_t g_ref_uses;

/* Apply a rule to the premises on top of the trace stack. */
static void ap(CettaInferenceRuleHandle rule, size_t n, Atom **args) {
    g_actions++;
    if (rule < 512) g_rule_uses[rule]++;
    CettaInferenceStatus st = cetta_inference_trace_apply(&g_trace, rule, args, n, g_err, sizeof g_err);
    if (st != CETTA_INFERENCE_OK)
        fault("NIK checker refused a produced step (%s): %s", cetta_inference_status_name(st), g_err);
}
#define AP(rule, ...) do { Atom *_a[] = {__VA_ARGS__}; ap(R_##rule, sizeof _a / sizeof _a[0], _a); } while (0)
#define AP0(rule) ap(R_##rule, 0, NULL)

static void ref_saved(size_t index) {
    g_actions++;
    g_ref_uses++;
    if (cetta_inference_trace_reference(&g_trace, index, g_err, sizeof g_err) != CETTA_INFERENCE_OK)
        fault("saved fact unavailable: %s", g_err);
}

/* One scoped step: a derivation whose single conclusion is kept. */
static CettaInferenceTraceScope g_scope;
static bool g_in_scope;

static void scope_begin(void) {
    if (g_in_scope) fault("internal: nested scope");
    if (cetta_inference_trace_scope_begin(&g_trace, &g_scope, g_err, sizeof g_err) != CETTA_INFERENCE_OK)
        fault("scope begin: %s", g_err);
    g_in_scope = true;
}

static size_t scope_commit(Atom *goal) {
    size_t index;
    if (cetta_inference_trace_scope_commit(&g_trace, &g_scope, goal, &index, g_err, sizeof g_err) !=
        CETTA_INFERENCE_OK)
        fault("step conclusion rejected: %s", g_err);
    g_in_scope = false;
    g_steps++;
    return index;
}

static void scope_abort(void) {
    if (!g_in_scope) return;
    cetta_inference_trace_scope_abort(&g_trace, &g_scope);
    g_in_scope = false;
}

/* ------------------------------------------------------------------ */
/* Arithmetic producers: each pushes exactly one judgment.            */
/* ------------------------------------------------------------------ */

/* Small-number facts are derived once, each in its own checked scope, and
   then referenced as saved conclusions. */
#define SMALL 48
static int64_t g_cache_nadd[SMALL][SMALL], g_cache_nle[SMALL][SMALL], g_cache_nlt[SMALL][SMALL];
static int64_t g_cache_nmonus[SMALL][SMALL], g_cache_nmax[SMALL][SMALL], g_cache_nword[SMALL * 2];
static bool g_cache_ready;
static void prove_nadd(u128 a, u128 b);
static void prove_nlt(u128 a, u128 b);
static void prove_nle(u128 a, u128 b);
static u128 prove_nmonus(u128 a, u128 b);
static u128 prove_nmax(u128 a, u128 b);
static void prove_nword(u128 a);

static void prove_psucc(u128 p) {
    if (p == 1) { AP0(psucc_1); return; }
    u128 x = p / 2;
    if ((p & 1) == 0) { AP(psucc_o, pat_pos(x)); return; }
    prove_psucc(x);
    AP(psucc_i, pat_pos(x), pat_pos(x + 1));
}

static void prove_paddc(u128 p, u128 q);

static void prove_padd(u128 p, u128 q) {
    if (p == 1) { prove_psucc(q); AP(padd_1l, pat_pos(q), pat_pos(q + 1)); return; }
    if (q == 1) { prove_psucc(p); AP(padd_1r, pat_pos(p), pat_pos(p + 1)); return; }
    u128 x = p / 2, y = q / 2;
    bool po = p & 1, qo = q & 1;
    if (!po && !qo) { prove_padd(x, y); AP(padd_oo, pat_pos(x), pat_pos(y), pat_pos(x + y)); }
    else if (!po && qo) { prove_padd(x, y); AP(padd_oi, pat_pos(x), pat_pos(y), pat_pos(x + y)); }
    else if (po && !qo) { prove_padd(x, y); AP(padd_io, pat_pos(x), pat_pos(y), pat_pos(x + y)); }
    else { prove_paddc(x, y); AP(padd_ii, pat_pos(x), pat_pos(y), pat_pos(x + y + 1)); }
}

static void prove_paddc(u128 p, u128 q) {
    if (p == 1 && q == 1) { AP0(paddc_11); return; }
    if (p == 1) {
        u128 y = q / 2;
        prove_psucc(y);
        if ((q & 1) == 0) AP(paddc_1o, pat_pos(y), pat_pos(y + 1));
        else AP(paddc_1i, pat_pos(y), pat_pos(y + 1));
        return;
    }
    if (q == 1) {
        u128 x = p / 2;
        prove_psucc(x);
        if ((p & 1) == 0) AP(paddc_o1, pat_pos(x), pat_pos(x + 1));
        else AP(paddc_i1, pat_pos(x), pat_pos(x + 1));
        return;
    }
    u128 x = p / 2, y = q / 2;
    bool po = p & 1, qo = q & 1;
    if (!po && !qo) { prove_padd(x, y); AP(paddc_oo, pat_pos(x), pat_pos(y), pat_pos(x + y)); }
    else if (!po && qo) { prove_paddc(x, y); AP(paddc_oi, pat_pos(x), pat_pos(y), pat_pos(x + y + 1)); }
    else if (po && !qo) { prove_paddc(x, y); AP(paddc_io, pat_pos(x), pat_pos(y), pat_pos(x + y + 1)); }
    else { prove_paddc(x, y); AP(paddc_ii, pat_pos(x), pat_pos(y), pat_pos(x + y + 1)); }
}

static void prove_nadd_raw(u128 a, u128 b) {
    if (a == 0) { AP(nadd_0l, pat_nat(b)); return; }
    if (b == 0) { AP(nadd_0r, pat_pos(a)); return; }
    prove_padd(a, b);
    AP(nadd_pp, pat_pos(a), pat_pos(b), pat_pos(a + b));
}

static void prove_nlt_raw(u128 a, u128 b) {
    if (!(a < b)) fault("internal: order witness for %llu !< %llu", (unsigned long long)a, (unsigned long long)b);
    prove_nadd(a, b - a);
    AP(nlt, pat_nat(a), pat_pos(b - a), pat_nat(b));
}

static void prove_nle_raw(u128 a, u128 b) {
    if (!(a <= b)) fault("internal: order witness");
    prove_nadd(a, b - a);
    AP(nle, pat_nat(a), pat_nat(b - a), pat_nat(b));
}

static u128 prove_nmonus_raw(u128 a, u128 b) {
    if (a <= b) { prove_nle(a, b); AP(nmonus_le, pat_nat(a), pat_nat(b)); return 0; }
    prove_nadd(b, a - b);
    AP(nmonus_gt, pat_nat(a), pat_nat(b), pat_pos(a - b));
    return a - b;
}

static u128 prove_nmax_raw(u128 a, u128 b) {
    if (a <= b) { prove_nle(a, b); AP(nmax_le, pat_nat(a), pat_nat(b)); return b; }
    prove_nlt(b, a);
    AP(nmax_gt, pat_nat(a), pat_nat(b));
    return a;
}

static bool prove_bor(bool x, bool y) {
    if (!x && !y) AP0(bor_ff);
    else if (!x && y) AP0(bor_ft);
    else if (x && !y) AP0(bor_tf);
    else AP0(bor_tt);
    return x || y;
}

static void prove_pbits(u128 p, unsigned u) {
    if (u == 0) fault("internal: bit bound");
    if (p == 1) { AP(pbits_1, pat_unary(u - 1)); return; }
    prove_pbits(p / 2, u - 1);
    if ((p & 1) == 0) AP(pbits_o, pat_pos(p / 2), pat_unary(u - 1));
    else AP(pbits_i, pat_pos(p / 2), pat_unary(u - 1));
}

/* Pushes NWord(a); the caller checks a < 2^64 first. */
static void prove_nword_raw(u128 a) {
    if (a >= WORD_BOUND) fault("internal: word witness");
    if (a == 0) { AP0(nword_0); return; }
    prove_pbits(a, 64);
    AP(nword_p, pat_pos(a));
}

static void prove_nadd(u128 a, u128 b) {
    if (g_cache_ready && a < SMALL && b < SMALL) { ref_saved((size_t)g_cache_nadd[a][b]); return; }
    prove_nadd_raw(a, b);
}
/* Order lemmas between larger numbers (symbol identities), established in
   their own scopes before the step that needs them. */
typedef struct { u128 a, b; int64_t saved; } PairEntry;
static PairEntry *g_pairs;
static size_t g_pairs_cap, g_pairs_count;
static size_t pair_slot(u128 a, u128 b) {
    uint64_t h = (uint64_t)a * 0x9e3779b97f4a7c15ull ^ ((uint64_t)b + 0x7f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
    return (size_t)(h ^ (h >> 29));
}
static int64_t pair_find(u128 a, u128 b) {
    if (!g_pairs_cap) return -1;
    size_t s = pair_slot(a, b) & (g_pairs_cap - 1);
    while (g_pairs[s].saved >= 0) {
        if (g_pairs[s].a == a && g_pairs[s].b == b) return g_pairs[s].saved;
        s = (s + 1) & (g_pairs_cap - 1);
    }
    return -1;
}
static void pair_insert(u128 a, u128 b, int64_t saved) {
    if (g_pairs_count * 2 >= g_pairs_cap) {
        size_t nc = g_pairs_cap ? g_pairs_cap * 2 : 4096;
        PairEntry *nt = malloc(nc * sizeof *nt);
        for (size_t i = 0; i < nc; i++) nt[i].saved = -1;
        for (size_t i = 0; i < g_pairs_cap; i++) {
            if (g_pairs[i].saved < 0) continue;
            size_t s = pair_slot(g_pairs[i].a, g_pairs[i].b) & (nc - 1);
            while (nt[s].saved >= 0) s = (s + 1) & (nc - 1);
            nt[s] = g_pairs[i];
        }
        free(g_pairs);
        g_pairs = nt;
        g_pairs_cap = nc;
    }
    size_t s = pair_slot(a, b) & (g_pairs_cap - 1);
    while (g_pairs[s].saved >= 0) s = (s + 1) & (g_pairs_cap - 1);
    g_pairs[s] = (PairEntry){a, b, saved};
    g_pairs_count++;
}

static void prove_nlt(u128 a, u128 b) {
    if (g_cache_ready && a < SMALL && b < SMALL) { if (!(a < b)) fault("internal: order witness"); ref_saved((size_t)g_cache_nlt[a][b]); return; }
    int64_t saved = pair_find(a, b);
    if (saved >= 0) { ref_saved((size_t)saved); return; }
    prove_nlt_raw(a, b);
}
static void prove_nle(u128 a, u128 b) {
    if (g_cache_ready && a < SMALL && b < SMALL) { if (!(a <= b)) fault("internal: order witness"); ref_saved((size_t)g_cache_nle[a][b]); return; }
    prove_nle_raw(a, b);
}
static u128 prove_nmonus(u128 a, u128 b) {
    if (g_cache_ready && a < SMALL && b < SMALL) { ref_saved((size_t)g_cache_nmonus[a][b]); return a > b ? a - b : 0; }
    return prove_nmonus_raw(a, b);
}
static u128 prove_nmax(u128 a, u128 b) {
    if (g_cache_ready && a < SMALL && b < SMALL) { ref_saved((size_t)g_cache_nmax[a][b]); return a > b ? a : b; }
    return prove_nmax_raw(a, b);
}
static void prove_nword(u128 a) {
    if (g_cache_ready && a < SMALL * 2) { ref_saved((size_t)g_cache_nword[a]); return; }
    prove_nword_raw(a);
}

static Atom *jpat2(const char *head, Atom *x, Atom *y) {
    Atom *h = atom_string(&g_persist, head);
    Atom *items[2] = {x, y};
    return expr3(A_PApp, h, plist(items, 2));
}
static Atom *jpat3(const char *head, Atom *x, Atom *y, Atom *z) {
    Atom *h = atom_string(&g_persist, head);
    Atom *items[3] = {x, y, z};
    return expr3(A_PApp, h, plist(items, 3));
}
static void scope_begin(void);
static size_t scope_commit(Atom *goal);

static void build_small_cache(void) {
    for (u128 a = 0; a < SMALL; a++)
        for (u128 b = 0; b < SMALL; b++) {
            scope_begin(); prove_nadd_raw(a, b);
            g_cache_nadd[a][b] = (int64_t)scope_commit(jpat3("VNAdd", pat_nat(a), pat_nat(b), pat_nat(a + b)));
            if (a <= b) {
                scope_begin(); prove_nle_raw(a, b);
                g_cache_nle[a][b] = (int64_t)scope_commit(jpat2("VNLe", pat_nat(a), pat_nat(b)));
            }
            if (a < b) {
                scope_begin(); prove_nlt_raw(a, b);
                g_cache_nlt[a][b] = (int64_t)scope_commit(jpat2("VNLt", pat_nat(a), pat_nat(b)));
            }
            scope_begin(); u128 c = prove_nmonus_raw(a, b);
            g_cache_nmonus[a][b] = (int64_t)scope_commit(jpat3("VNMonus", pat_nat(a), pat_nat(b), pat_nat(c)));
            scope_begin(); u128 m = prove_nmax_raw(a, b);
            g_cache_nmax[a][b] = (int64_t)scope_commit(jpat3("VNMax", pat_nat(a), pat_nat(b), pat_nat(m)));
        }
    for (u128 a = 0; a < SMALL * 2; a++) {
        scope_begin(); prove_nword_raw(a);
        g_cache_nword[a] = (int64_t)scope_commit(papp1_named("VNWord", pat_nat(a)));
    }
    g_cache_ready = true;
}

static void prove_pmul(u128 p, u128 q) {
    if (p == 1) { AP(pmul_1, pat_pos(q)); return; }
    u128 x = p / 2, r = x * q;
    if ((p & 1) == 0) { prove_pmul(x, q); AP(pmul_o, pat_pos(x), pat_pos(q), pat_pos(r)); return; }
    prove_pmul(x, q);
    prove_padd(2 * r, q);
    AP(pmul_i, pat_pos(x), pat_pos(q), pat_pos(r), pat_pos(2 * r + q));
}

static void prove_nmul(u128 a, u128 b) {
    if (a == 0) { AP(nmul_0l, pat_nat(b)); return; }
    if (b == 0) { AP(nmul_0r, pat_pos(a)); return; }
    prove_pmul(a, b);
    AP(nmul_pp, pat_pos(a), pat_pos(b), pat_pos(a * b));
}

static void prove_ndivmod(u128 a, u128 b, u128 *q_out, u128 *r_out) {
    if (b == 0) fault("internal: division witness");
    u128 q = a / b, r = a % b, m = b * q;
    prove_nmul(b, q);
    prove_nadd(m, r);
    prove_nlt(r, b);
    AP(ndivmod, pat_nat(a), pat_nat(b), pat_nat(q), pat_nat(r), pat_nat(m));
    *q_out = q;
    *r_out = r;
}

static u128 prove_nmod64(u128 s) {
    u128 q, c;
    prove_ndivmod(s, WORD_BOUND, &q, &c);
    AP(nmod64, pat_nat(s), pat_nat(q), pat_nat(c));
    return c;
}

/* ------------------------------------------------------------------ */
/* Lists                                                              */
/* ------------------------------------------------------------------ */

static Atom *vlist(Atom **items, uint64_t n, uint64_t from) {
    Atom *list = P_Nil;
    for (uint64_t k = n; k > from; k--) list = pat_cons(items[k - 1], list);
    return list;
}

static void prove_len(Atom **items, uint64_t n, uint64_t from) {
    if (from == n) { AP0(len_nil); return; }
    prove_len(items, n, from + 1);
    prove_nadd(n - from - 1, 1);
    AP(len_cons, items[from], vlist(items, n, from + 1), pat_nat(n - from - 1), pat_nat(n - from));
}

static Atom *prove_nth(Atom **items, uint64_t n, uint64_t from, uint64_t i) {
    if (from >= n) fault("internal: list index");
    if (i == 0) { AP(nth_0, items[from], vlist(items, n, from + 1)); return items[from]; }
    prove_nadd(i - 1, 1);
    Atom *y = prove_nth(items, n, from + 1, i - 1);
    AP(nth_s, items[from], vlist(items, n, from + 1), pat_nat(i), pat_nat(i - 1), y);
    return y;
}

static void prove_bytes(const uint8_t *b, uint64_t n, uint64_t from) {
    if (from == n) { AP0(bytes_nil); return; }
    prove_nlt(b[from], 256);
    prove_bytes(b, n, from + 1);
    AP(bytes_cons, pat_nat(b[from]), bytes_pat(b + from + 1, n - from - 1));
}

/* ------------------------------------------------------------------ */
/* Term judgments                                                     */
/* ------------------------------------------------------------------ */

static void prove_depth(Term *t) {
    if (t->tag == T_BVAR) { prove_nadd(t->bvar, 1); AP(depth_bvar, pat_nat(t->bvar), pat_nat((u128)t->bvar + 1)); }
    else if (t->tag == T_LIT) AP(depth_lit, bytes_pat(t->bytes, t->n));
    else AP(depth_app, t->sym->pat, term_args_pat(t), pat_nat(t->depth), pat_bool(t->hasfv));
}

static void prove_hasfv(Term *t) {
    if (t->tag == T_BVAR) AP(hasfv_bvar, pat_nat(t->bvar));
    else if (t->tag == T_LIT) AP(hasfv_lit, bytes_pat(t->bytes, t->n));
    else AP(hasfv_app, t->sym->pat, term_args_pat(t), pat_nat(t->depth), pat_bool(t->hasfv));
}

static bool prove_kindfv(Sym *s, bool f) {
    if (s->kind == 1) { AP(kindfv_fvar, pat_bool(f)); return true; }
    AP(kindfv_const, pat_bool(f));
    return f;
}

/* VAnnArgs(binders[from..], args[from..], d, f); returns d and f. */
static void prove_annargs(Sym *s, Term **args, uint64_t n, uint64_t from, uint64_t *d_out, bool *f_out) {
    if (from == n) { AP0(annargs_nil); *d_out = 0; *f_out = false; return; }
    Term *t = args[from];
    uint64_t b = s->binders[from];
    prove_depth(t);
    u128 e = prove_nmonus(t->depth, b);
    prove_hasfv(t);
    uint64_t dr; bool fr;
    prove_annargs(s, args, n, from + 1, &dr, &fr);
    u128 d = prove_nmax(e, dr);
    bool f = prove_bor(t->hasfv, fr);
    AP(annargs_cons, pat_nat(b), binders_suffix_pat(s, from + 1), term_pat(t),
       terms_suffix_pat(args, n, from + 1), pat_nat(t->depth), pat_nat(e), pat_nat(dr),
       pat_bool(fr), pat_bool(t->hasfv), pat_nat(d), pat_bool(f));
    *d_out = (uint64_t)d;
    *f_out = f;
}

static void push_symdecl(Sym *s) {
    g_actions++;
    if (cetta_inference_trace_apply(&g_trace, s->decl_rule, NULL, 0, g_err, sizeof g_err) != CETTA_INFERENCE_OK)
        fault("symbol declaration unavailable: %s", g_err);
}

static void prove_wfargs(Sym *s, Term **args, uint64_t n, uint64_t from, uint64_t *d_out, bool *f_out);

/* Establish VWf(t) as a saved fact, bottom-up, outside any scope. */
static void ensure_wf(Term *t) {
    if (t->wf_saved >= 0) return;
    if (t->tag == T_APP)
        for (uint64_t i = 0; i < t->n; i++) ensure_wf(t->args[i]);
    scope_begin();
    if (t->tag == T_BVAR) {
        prove_nadd(t->bvar, 1);
        prove_nword((u128)t->bvar + 1);
        AP(wf_bvar, pat_nat(t->bvar), pat_nat((u128)t->bvar + 1));
    } else if (t->tag == T_LIT) {
        Atom *bp = bytes_pat(t->bytes, t->n);
        prove_bytes(t->bytes, t->n, 0);
        /* VLen over the byte list */
        Atom **items = malloc((t->n ? t->n : 1) * sizeof *items);
        for (uint64_t i = 0; i < t->n; i++) items[i] = pat_nat(t->bytes[i]);
        prove_len(items, t->n, 0);
        free(items);
        prove_nadd(t->n, 8);
        prove_nword((u128)t->n + 8);
        AP(wf_lit, bp, pat_nat(t->n), pat_nat((u128)t->n + 8));
    } else {
        Sym *s = t->sym;
        push_symdecl(s);
        uint64_t d; bool f;
        prove_wfargs(s, t->args, t->n, 0, &d, &f);
        bool g = prove_kindfv(s, f);
        AP(wf_app, pat_nat(s->id), s->kind == 1 ? P_KFvar : P_KConst, s->binders_pat,
           term_args_pat(t), pat_nat(d), pat_bool(f), pat_bool(g));
    }
    t->wf_saved = (int64_t)scope_commit(papp1(H_VWf, term_pat(t)));
}

static void prove_wfargs(Sym *s, Term **args, uint64_t n, uint64_t from, uint64_t *d_out, bool *f_out) {
    if (from == n) { AP0(wfargs_nil); *d_out = 0; *f_out = false; return; }
    Term *t = args[from];
    uint64_t b = s->binders[from];
    ref_saved((size_t)t->wf_saved);
    prove_depth(t);
    u128 e = prove_nmonus(t->depth, b);
    prove_hasfv(t);
    uint64_t dr; bool fr;
    prove_wfargs(s, args, n, from + 1, &dr, &fr);
    u128 d = prove_nmax(e, dr);
    bool f = prove_bor(t->hasfv, fr);
    AP(wfargs_cons, pat_nat(b), binders_suffix_pat(s, from + 1), term_pat(t),
       terms_suffix_pat(args, n, from + 1), pat_nat(t->depth), pat_nat(e), pat_nat(dr),
       pat_bool(fr), pat_bool(t->hasfv), pat_nat(d), pat_bool(f));
    *d_out = (uint64_t)d;
    *f_out = f;
}

/* ------------------------------------------------------------------ */
/* Kernel operations with derivations.  NULL result = kernel refusal. */
/* ------------------------------------------------------------------ */

static const u128 MAXWORD = WORD_BOUND - 1;

static Term *prove_shift(u128 a, u128 c, Term *t);

static bool prove_shiftargs(u128 a, u128 c, Sym *s, Term **args, uint64_t n, uint64_t from, Term **out) {
    if (from == n) { AP(shiftargs_nil, pat_nat(a), pat_nat(c)); return true; }
    u128 k = c + s->binders[from];
    if (k >= WORD_BOUND) return false;
    prove_nadd(c, s->binders[from]);
    prove_nword(k);
    Term *u = prove_shift(a, k, args[from]);
    if (!u) return false;
    out[from] = u;
    if (!prove_shiftargs(a, c, s, args, n, from + 1, out)) return false;
    AP(shiftargs_cons, pat_nat(a), pat_nat(c), pat_nat(s->binders[from]), binders_suffix_pat(s, from + 1),
       term_pat(args[from]), terms_suffix_pat(args, n, from + 1), term_pat(u), terms_suffix_pat(out, n, from + 1),
       pat_nat(k));
    return true;
}

static Term *prove_shift(u128 a, u128 c, Term *t) {
    if (a == 0) { AP(shift_zero, pat_nat(c), term_pat(t)); return t; }
    if ((u128)t->depth <= c) {
        prove_depth(t);
        prove_nle(t->depth, c);
        AP(shift_low, pat_nat(a), pat_nat(c), term_pat(t), pat_nat(t->depth));
        return t;
    }
    if (t->tag == T_BVAR) {
        u128 r = (u128)t->bvar + a;
        if (r >= MAXWORD) return NULL;
        prove_nle(c, t->bvar);
        prove_nadd(t->bvar, a);
        prove_nlt(r, MAXWORD);
        AP(shift_bvar, pat_pos(a), pat_nat(c), pat_nat(t->bvar), pat_nat(r));
        return mk_bvar((uint64_t)r);
    }
    /* application (literals have depth 0) */
    Sym *s = t->sym;
    uint64_t n = t->n;
    Term **out = malloc((n ? n : 1) * sizeof *out);
    prove_nlt(c, t->depth);
    if (!prove_shiftargs(a, c, s, t->args, n, 0, out)) { free(out); return NULL; }
    Term *r = mk_app(s, out);
    uint64_t e; bool g;
    prove_annargs(s, out, n, 0, &e, &g);
    bool h = prove_kindfv(s, g);
    AP(shift_app, pat_pos(a), pat_nat(c), pat_nat(s->id), s->kind == 1 ? P_KFvar : P_KConst, s->binders_pat,
       term_args_pat(t), pat_nat(t->depth), pat_bool(t->hasfv), term_args_pat(r), pat_nat(e), pat_bool(g),
       pat_bool(h));
    free(out);
    return r;
}

static Term *prove_subst(u128 n, Term **av, Atom *args_pat, Atom **arg_items, u128 off, Term *t);

static bool prove_substargs(u128 nn, Term **av, Atom *args_pat, Atom **items, u128 off, Sym *s, Term **args,
                            uint64_t n, uint64_t from, Term **out) {
    if (from == n) { AP(substargs_nil, pat_nat(nn), args_pat, pat_nat(off)); return true; }
    u128 k = off + s->binders[from];
    if (k >= WORD_BOUND) return false;
    prove_nadd(off, s->binders[from]);
    prove_nword(k);
    Term *u = prove_subst(nn, av, args_pat, items, k, args[from]);
    if (!u) return false;
    out[from] = u;
    if (!prove_substargs(nn, av, args_pat, items, off, s, args, n, from + 1, out)) return false;
    AP(substargs_cons, pat_nat(nn), args_pat, pat_nat(off), pat_nat(s->binders[from]),
       binders_suffix_pat(s, from + 1), term_pat(args[from]), terms_suffix_pat(args, n, from + 1),
       term_pat(u), terms_suffix_pat(out, n, from + 1), pat_nat(k));
    return true;
}

static Term *prove_subst(u128 nn, Term **av, Atom *args_pat, Atom **items, u128 off, Term *t) {
    if ((u128)t->depth <= off) {
        prove_depth(t);
        prove_nle(t->depth, off);
        AP(subst_low, pat_nat(nn), args_pat, pat_nat(off), term_pat(t), pat_nat(t->depth));
        return t;
    }
    if (t->tag == T_BVAR) {
        u128 b = t->bvar, k = b - off;
        if (k < nn) {
            prove_nadd(off, k);
            prove_nlt(k, nn);
            prove_nadd(k, 1);
            u128 i = nn - 1 - k;
            prove_nadd(i, k + 1);
            prove_nth(items, (uint64_t)nn, 0, (uint64_t)i);
            Term *a = av[(uint64_t)i];
            Term *r = prove_shift(off, 0, a);
            if (!r) return NULL;
            AP(subst_param, pat_nat(nn), args_pat, pat_nat(off), pat_nat(b), pat_nat(k), pat_nat(k + 1),
               pat_nat(i), term_pat(a), term_pat(r));
            return r;
        }
        if (k + 1 >= WORD_BOUND) return NULL;
        prove_nadd(off, k);
        prove_nle(nn, k);
        prove_nadd(k, 1);
        prove_nword(k + 1);
        AP(subst_above, pat_nat(nn), args_pat, pat_nat(off), pat_nat(b), pat_nat(k), pat_nat(k + 1));
        return mk_bvar((uint64_t)k);
    }
    Sym *s = t->sym;
    uint64_t n = t->n;
    Term **out = malloc((n ? n : 1) * sizeof *out);
    prove_nlt(off, t->depth);
    if (!prove_substargs(nn, av, args_pat, items, off, s, t->args, n, 0, out)) { free(out); return NULL; }
    Term *r = mk_app(s, out);
    uint64_t e; bool g;
    prove_annargs(s, out, n, 0, &e, &g);
    bool h = prove_kindfv(s, g);
    AP(subst_app, pat_nat(nn), args_pat, pat_nat(off), pat_nat(s->id), s->kind == 1 ? P_KFvar : P_KConst,
       s->binders_pat, term_args_pat(t), pat_nat(t->depth), pat_bool(t->hasfv), term_args_pat(r), pat_nat(e),
       pat_bool(g), pat_bool(h));
    free(out);
    return r;
}

static Term *prove_substtop(u128 nn, Term **av, Term *body) {
    Atom **items = malloc((nn ? (size_t)nn : 1) * sizeof *items);
    for (uint64_t i = 0; i < (uint64_t)nn; i++) items[i] = term_pat(av[i]);
    Atom *args_pat = vlist(items, (uint64_t)nn, 0);
    if (nn == 0) {
        AP(substtop_0, args_pat, term_pat(body));
        free(items);
        return body;
    }
    Term *r = prove_subst(nn, av, args_pat, items, 0, body);
    if (r) AP(substtop_p, pat_pos(nn), args_pat, term_pat(body), term_pat(r));
    free(items);
    return r;
}

static Term *prove_inst(Sym *F, Term *v, u128 off, Term *t);

static bool prove_instargs(Sym *F, Term *v, u128 off, Sym *s, Term **args, uint64_t n, uint64_t from, Term **out) {
    if (from == n) { AP(instargs_nil, F->pat, term_pat(v), pat_nat(F->arity), pat_nat(off)); return true; }
    u128 k = off + s->binders[from];
    if (k >= WORD_BOUND) return false;
    prove_nadd(off, s->binders[from]);
    prove_nword(k);
    Term *u = prove_inst(F, v, k, args[from]);
    if (!u) return false;
    out[from] = u;
    if (!prove_instargs(F, v, off, s, args, n, from + 1, out)) return false;
    AP(instargs_cons, F->pat, term_pat(v), pat_nat(F->arity), pat_nat(off), pat_nat(s->binders[from]),
       binders_suffix_pat(s, from + 1), term_pat(args[from]), terms_suffix_pat(args, n, from + 1),
       term_pat(u), terms_suffix_pat(out, n, from + 1), pat_nat(k));
    return true;
}

static Term *prove_inst(Sym *F, Term *v, u128 off, Term *t) {
    if (!t->hasfv) {
        prove_hasfv(t);
        AP(inst_nofv, F->pat, term_pat(v), pat_nat(F->arity), pat_nat(off), term_pat(t));
        return t;
    }
    Sym *s = t->sym;
    uint64_t n = t->n;
    Term **out = malloc((n ? n : 1) * sizeof *out);
    Term *r;
    if (s == F) {
        if (!prove_instargs(F, v, off, s, t->args, n, 0, out)) { free(out); return NULL; }
        Term *w = prove_shift(off, F->arity, v);
        if (!w) { free(out); return NULL; }
        r = prove_substtop(F->arity, out, w);
        if (!r) { free(out); return NULL; }
        AP(inst_hit, pat_nat(F->id), F->binders_pat, term_pat(v), pat_nat(F->arity), pat_nat(off),
           term_args_pat(t), pat_nat(t->depth), terms_suffix_pat(out, n, 0), term_pat(w), term_pat(r));
        free(out);
        return r;
    }
    if (s->kind == 1) {
        if (s->id < F->id) prove_nlt(s->id, F->id); else prove_nlt(F->id, s->id);
    }
    if (!prove_instargs(F, v, off, s, t->args, n, 0, out)) { free(out); return NULL; }
    r = mk_app(s, out);
    uint64_t e; bool g;
    prove_annargs(s, out, n, 0, &e, &g);
    if (s->kind == 0)
        AP(inst_const, F->pat, term_pat(v), pat_nat(F->arity), pat_nat(off), pat_nat(s->id), s->binders_pat,
           term_args_pat(t), pat_nat(t->depth), term_args_pat(r), pat_nat(e), pat_bool(g));
    else if (s->id < F->id)
        AP(inst_other_lt, pat_nat(F->id), F->binders_pat, term_pat(v), pat_nat(F->arity), pat_nat(off),
           pat_nat(s->id), s->binders_pat, term_args_pat(t), pat_nat(t->depth), term_args_pat(r), pat_nat(e),
           pat_bool(g));
    else
        AP(inst_other_gt, pat_nat(F->id), F->binders_pat, term_pat(v), pat_nat(F->arity), pat_nat(off),
           pat_nat(s->id), s->binders_pat, term_args_pat(t), pat_nat(t->depth), term_args_pat(r), pat_nat(e),
           pat_bool(g));
    free(out);
    return r;
}

/* Canonical number literal bytes (<256: one byte, else eight LE bytes). */
static Atom *prove_natlit(u128 n) {
    if (n < 256) {
        prove_nadd(n, 256 - n);
        AP(natlit_small, pat_nat(n), pat_pos(256 - n));
        return pat_cons(pat_nat(n), P_Nil);
    }
    prove_nle(256, n);
    u128 q[9], r[9];
    q[0] = n;
    for (int i = 1; i <= 8; i++) prove_ndivmod(q[i - 1], 256, &q[i], &r[i]);
    Atom *bytes = P_Nil;
    for (int i = 8; i >= 1; i--) bytes = pat_cons(pat_nat(r[i]), bytes);
    Atom *args[17];
    args[0] = pat_nat(n);
    for (int i = 1; i <= 8; i++) args[i] = pat_nat(q[i]);
    for (int i = 1; i <= 8; i++) args[8 + i] = pat_nat(r[i]);
    ap(R_natlit_large, 17, args);
    return bytes;
}

/* ------------------------------------------------------------------ */
/* Admission of theory facts (zero-premise rules)                     */
/* ------------------------------------------------------------------ */

static uint64_t g_admitted;

static CettaInferenceRuleHandle admit_fact(const char *prefix, uint64_t index, Atom *conclusion) {
    char name[64];
    snprintf(name, sizeof name, "%s-%" PRIu64, prefix, index);
    Atom *elems[6] = {A_GRuleV1, atom_string(&g_persist, name), A_LNil, A_LNil, conclusion, A_LNil};
    Atom *rule = atom_expr(&g_persist, elems, 6);
    CettaInferenceRuleHandle handle;
    if (cetta_inference_checker_add_rule(g_checker, rule, &handle, g_err, sizeof g_err) != CETTA_INFERENCE_OK)
        fault("admission of %s refused: %s", name, g_err);
    g_admitted++;
    return handle;
}

static void admit_symbol(Sym *s) {
    sym_finish(s);
    s->decl_rule = admit_fact("vibe-symbol", (uint64_t)s->id, papp1(H_VSymDecl, s->pat));
}

/* ------------------------------------------------------------------ */
/* Protocol state (mirrors Spec.Protocol)                              */
/* ------------------------------------------------------------------ */

typedef struct { Term *stmt; bool by_rule; size_t index; } Thm;

typedef struct { void **v; uint64_t cap; } Slots;
static void *sget(Slots *s, uint64_t i) { return i < s->cap ? s->v[i] : NULL; }
static void sset(Slots *s, uint64_t i, void *p) {
    if (i >= s->cap) {
        uint64_t nc = s->cap ? s->cap : 1024;
        while (nc <= i) {
            if (nc > (UINT64_MAX >> 1)) fault("slot index beyond addressable range");
            nc *= 2;
        }
        void **nv = realloc(s->v, nc * sizeof(void *));
        if (!nv) {
            printf("VERDICT incomplete reason=\"slot table allocation\"\n");
            exit(4);
        }
        memset(nv + s->cap, 0, (nc - s->cap) * sizeof(void *));
        s->v = nv;
        s->cap = nc;
    }
    s->v[i] = p;
}

static Slots S_sym, S_term, S_thm, S_ch;
static int g_phase_proofs;
static uint64_t g_open_ch, g_setup_ch, g_proof_ch;
static uint64_t g_axioms, g_definitions;
static Sym g_marker_bvar = {2, 0, NULL, 0, NULL, NULL, 0};
static Sym g_marker_lit = {3, 0, NULL, 1, NULL, NULL, 0};

/* Error classes of Spec.StepError. */
static const char *g_error;
#define STEP_FAIL(cls) do { g_error = (cls); return false; } while (0)

static void push_thm(Thm *h) {
    if (h->by_rule) {
        g_actions++;
        if (cetta_inference_trace_apply(&g_trace, (CettaInferenceRuleHandle)h->index, NULL, 0, g_err, sizeof g_err) !=
            CETTA_INFERENCE_OK)
            fault("admitted theorem unavailable: %s", g_err);
    } else {
        ref_saved(h->index);
    }
}

/* ------------------------------------------------------------------ */
/* Byte decoding                                                       */
/* ------------------------------------------------------------------ */

static const uint8_t *p, *pend;
static const char *g_syntax;   /* decode error class, or NULL */

static bool rd(uint64_t *out) {
    if (p >= pend) { g_syntax = "malformed.truncatedInteger"; return false; }
    unsigned n = *p++;
    if (n <= 247) { *out = n; return true; }
    n -= 247;
    uint64_t r = 0;
    for (unsigned i = 0; i < n; i++) {
        if (p >= pend) { g_syntax = "malformed.truncatedInteger"; return false; }
        r |= ((uint64_t)*p++) << (8 * i);
    }
    *out = r;
    return true;
}

#define RD(v) do { if (!rd(&(v))) return false; } while (0)

/* Operand shapes: w = word, c = counted words, b = raw bytes. */
static const char *g_shape[26] = {
    "ww", "cw", "ww", "w", "ww", "bw", "wcw", "ww", "w", "ww", "ww", "w", "ww", "ww", "ww",
    "www", "wwww", "ccwww", "ww", "www", "www", "www", "www", "ww", "www", "www"};

/* Syntax pass: decode one file without executing. */
static bool syntax_file(const uint8_t *base, size_t size) {
    p = base; pend = base + size;
    while (p < pend) {
        unsigned op = *p++;
        if (op > 25) { static char buf[64]; snprintf(buf, sizeof buf, "malformed.unknownOpcode"); g_syntax = buf; return false; }
        for (const char *f = g_shape[op]; *f; f++) {
            uint64_t v, n;
            if (*f == 'w') RD(v);
            else if (*f == 'c') { RD(n); for (uint64_t i = 0; i < n; i++) RD(v); }
            else { RD(n); if ((uint64_t)(pend - p) < n) { g_syntax = "malformed.truncatedBytes"; return false; } p += n; }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Execution                                                           */
/* ------------------------------------------------------------------ */

static Sym *need_sym(uint64_t i) { return sget(&S_sym, i); }
static Term *need_term(uint64_t i) { return sget(&S_term, i); }
static Thm *need_thm(uint64_t i) { return sget(&S_thm, i); }

static bool place_thm(uint64_t dst, Term *stmt, bool by_rule, size_t index) {
    if (sget(&S_thm, dst)) STEP_FAIL("occupied.theorem");
    Thm *h = malloc(sizeof *h);
    h->stmt = stmt; h->by_rule = by_rule; h->index = index;
    sset(&S_thm, dst, h);
    return true;
}

static Atom *thm_goal(Term *stmt) { return papp1(H_VThm, term_pat(stmt)); }

/* Build the statement of a literal theorem as a Term. */
static Term *app1(int b, Term *x) { Term *a[1] = {x}; return mk_app(&g_builtin[b], a); }
static Term *app2(int b, Term *x, Term *y) { Term *a[2] = {x, y}; return mk_app(&g_builtin[b], a); }

static Atom *lit_bytes_pat(Term *lit) { return bytes_pat(lit->bytes, lit->n); }

static bool exec_lit(unsigned op, uint64_t a, uint64_t b, uint64_t dst, Term *lt) {
    Term *stmt;
    if (sget(&S_thm, dst)) {
        /* the kernel builds the theorem before the destination check; a
           refused theorem is reported first */
    }
    switch (op) {
    case 18: {
        stmt = app1(4, mk_natlit(a));
        scope_begin();
        prove_nword(a);
        Atom *bn = prove_natlit(a);
        AP(lit_isnat, pat_nat(a), bn);
        break;
    }
    case 19: {
        if (!(a < b)) STEP_FAIL("theoremFailed");
        stmt = app2(5, mk_natlit(a), mk_natlit(b));
        scope_begin();
        prove_nlt(a, b);
        prove_nword(b);
        Atom *ba = prove_natlit(a);
        Atom *bb = prove_natlit(b);
        AP(lit_lt, pat_nat(a), pat_nat(b), ba, bb);
        break;
    }
    case 20: case 21: {
        u128 s = op == 20 ? (u128)a + b : (u128)a * b;
        u128 c = s % WORD_BOUND;
        stmt = app2(3, app2(op == 20 ? 6 : 7, mk_natlit(a), mk_natlit(b)), mk_natlit((uint64_t)c));
        scope_begin();
        prove_nword(a);
        prove_nword(b);
        if (op == 20) prove_nadd(a, b); else prove_nmul(a, b);
        prove_nmod64(s);
        Atom *ba = prove_natlit(a), *bb = prove_natlit(b), *bc = prove_natlit(c);
        if (op == 20) AP(lit_add, pat_nat(a), pat_nat(b), pat_nat(s), pat_nat(c), ba, bb, bc);
        else AP(lit_mul, pat_nat(a), pat_nat(b), pat_nat(s), pat_nat(c), ba, bb, bc);
        break;
    }
    case 22: {
        if (b == 0) STEP_FAIL("theoremFailed");
        stmt = app2(3, app2(8, mk_natlit(a), mk_natlit(b)), mk_natlit(a / b));
        scope_begin();
        prove_nword(a);
        prove_nword(b);
        u128 q, r;
        prove_ndivmod(a, b, &q, &r);
        Atom *ba = prove_natlit(a), *bb = prove_natlit(b), *bq = prove_natlit(q);
        AP(lit_div, pat_nat(a), pat_nat(b), pat_nat(q), pat_nat(r), ba, bb, bq);
        break;
    }
    case 23: {
        if (lt->tag != T_LIT) STEP_FAIL("theoremFailed");
        stmt = app2(3, app1(9, lt), mk_natlit(lt->n));
        ensure_wf(lt);
        scope_begin();
        ref_saved((size_t)lt->wf_saved);
        Atom **items = malloc((lt->n ? lt->n : 1) * sizeof *items);
        for (uint64_t i = 0; i < lt->n; i++) items[i] = pat_nat(lt->bytes[i]);
        prove_len(items, lt->n, 0);
        free(items);
        Atom *bn = prove_natlit(lt->n);
        AP(lit_length, lit_bytes_pat(lt), pat_nat(lt->n), bn);
        break;
    }
    default: { /* 24 */
        if (lt->tag != T_LIT || !(b < lt->n)) STEP_FAIL("theoremFailed");
        uint8_t x = lt->bytes[b];
        stmt = app2(3, app2(10, lt, mk_natlit(b)), mk_natlit(x));
        ensure_wf(lt);
        scope_begin();
        ref_saved((size_t)lt->wf_saved);
        Atom **items = malloc((lt->n ? lt->n : 1) * sizeof *items);
        for (uint64_t i = 0; i < lt->n; i++) items[i] = pat_nat(lt->bytes[i]);
        prove_nth(items, lt->n, 0, b);
        free(items);
        Atom *bi = prove_natlit(b), *bx = prove_natlit(x);
        AP(lit_get, lit_bytes_pat(lt), pat_nat(b), pat_nat(x), bi, bx);
        break;
    }
    }
    size_t idx = scope_commit(thm_goal(stmt));
    return place_thm(dst, stmt, false, idx);
}

static bool exec_mp(uint64_t ia, uint64_t ib, uint64_t dst) {
    Thm *hi = need_thm(ia);
    if (!hi) STEP_FAIL("missing.theorem");
    Thm *hp = need_thm(ib);
    if (!hp) STEP_FAIL("missing.theorem");
    Term *im = hi->stmt;
    if (im->tag != T_APP || im->sym != &g_builtin[2] || im->args[0] != hp->stmt) STEP_FAIL("theoremFailed");
    scope_begin();
    push_thm(hi);
    push_thm(hp);
    AP(thm_mp, term_pat(im->args[0]), term_pat(im->args[1]), ann_pat(im->depth, im->hasfv));
    size_t idx = scope_commit(thm_goal(im->args[1]));
    return place_thm(dst, im->args[1], false, idx);
}

static uint32_t g_mark_epoch;

static Atom *jpat2(const char *head, Atom *x, Atom *y);

/* Establish NLt lemmas between F and every other free-variable head that the
   instantiation traversal of phi meets. */
static void ensure_order_lemmas(Sym *F, Term *phi) {
    Term **stack = malloc(256 * sizeof *stack);
    size_t sp = 0, cap = 256;
    stack[sp++] = phi;
    g_mark_epoch++;
    while (sp) {
        Term *t = stack[--sp];
        if (!t->hasfv || t->tag != T_APP) continue;
        if (t->visit_epoch == g_mark_epoch) continue;
        t->visit_epoch = g_mark_epoch;
        Sym *s = t->sym;
        if (s->kind == 1 && s != F) {
            u128 a = s->id < F->id ? s->id : F->id, b = s->id < F->id ? F->id : s->id;
            if (!(a < SMALL && b < SMALL) && pair_find(a, b) < 0) {
                scope_begin();
                prove_nlt_raw(a, b);
                pair_insert(a, b, (int64_t)scope_commit(jpat2("VNLt", pat_nat(a), pat_nat(b))));
            }
        }
        for (uint64_t i = 0; i < t->n; i++) {
            if (sp == cap) { cap *= 2; stack = realloc(stack, cap * sizeof *stack); }
            stack[sp++] = t->args[i];
        }
    }
    free(stack);
}

static bool exec_inst(uint64_t ih, uint64_t is, uint64_t iv, uint64_t dst) {
    Thm *h = need_thm(ih);
    if (!h) STEP_FAIL("missing.theorem");
    Sym *F = need_sym(is);
    if (!F) STEP_FAIL("missing.symbol");
    Term *v = need_term(iv);
    if (!v) STEP_FAIL("missing.term");
    if (F->kind != 1 || v->depth > F->arity) STEP_FAIL("theoremFailed");
    ensure_wf(v);
    ensure_order_lemmas(F, h->stmt);
    scope_begin();
    push_thm(h);
    push_symdecl(F);
    Atom **items = malloc((F->arity ? F->arity : 1) * sizeof *items);
    for (uint64_t i = 0; i < F->arity; i++) items[i] = pat_nat(0);
    prove_len(items, F->arity, 0);
    free(items);
    ref_saved((size_t)v->wf_saved);
    prove_depth(v);
    prove_nle(v->depth, F->arity);
    Term *psi = prove_inst(F, v, 0, h->stmt);
    if (!psi || psi->depth != 0) { scope_abort(); STEP_FAIL("theoremFailed"); }
    prove_depth(psi);
    AP(thm_inst, term_pat(h->stmt), pat_nat(F->id), F->binders_pat, pat_nat(F->arity), term_pat(v),
       pat_nat(v->depth), term_pat(psi));
    size_t idx = scope_commit(thm_goal(psi));
    return place_thm(dst, psi, false, idx);
}

/* ---- definitions ---- */

static Atom *fvars_pat(Sym **fv, uint64_t n, uint64_t from) {
    Atom *list = P_Nil;
    for (uint64_t k = n; k > from; k--) list = pat_cons(fv[k - 1]->pat, list);
    return list;
}

static void prove_arities(Sym **fv, uint64_t n, uint64_t from, Sym *c) {
    if (from == n) { AP0(arities_nil); return; }
    Sym *F = fv[from];
    push_symdecl(F);
    Atom **items = malloc((F->arity ? F->arity : 1) * sizeof *items);
    for (uint64_t i = 0; i < F->arity; i++) items[i] = pat_nat(0);
    prove_len(items, F->arity, 0);
    free(items);
    prove_arities(fv, n, from + 1, c);
    AP(arities_cons, pat_nat(F->id), F->binders_pat, fvars_pat(fv, n, from + 1), pat_nat(F->arity),
       binders_suffix_pat(c, from + 1));
}

static Atom *nums_pat(uint64_t *xs, uint64_t n, uint64_t from) {
    Atom *list = P_Nil;
    for (uint64_t k = n; k > from; k--) list = pat_cons(pat_nat(xs[k - 1]), list);
    return list;
}

static void prove_hintsin(uint64_t *hs, uint64_t nh, uint64_t from, uint64_t n) {
    if (from == nh) { AP(hintsin_nil, pat_nat(n)); return; }
    prove_nlt(hs[from], n);
    prove_hintsin(hs, nh, from + 1, n);
    AP(hintsin_cons, pat_nat(hs[from]), nums_pat(hs, nh, from + 1), pat_nat(n));
}

typedef struct { Sym **fv; uint64_t n; Atom *fvp; Atom **fitems; uint64_t *hs; uint64_t nh; uint64_t pos; } OccCtx;

static void prove_occ(OccCtx *cx, Term *t);

static void prove_occargs(OccCtx *cx, Term **args, uint64_t n, uint64_t from) {
    uint64_t start = cx->pos;
    if (from == n) { AP(occargs_nil, cx->fvp, nums_pat(cx->hs, cx->nh, start)); return; }
    prove_occ(cx, args[from]);
    uint64_t mid = cx->pos;
    prove_occargs(cx, args, n, from + 1);
    AP(occargs_cons, cx->fvp, term_pat(args[from]), terms_suffix_pat(args, n, from + 1),
       nums_pat(cx->hs, cx->nh, start), nums_pat(cx->hs, cx->nh, mid), nums_pat(cx->hs, cx->nh, cx->pos));
}

static void prove_occ(OccCtx *cx, Term *t) {
    uint64_t start = cx->pos;
    Atom *hs = nums_pat(cx->hs, cx->nh, start);
    if (t->tag == T_BVAR) { AP(occ_bvar, cx->fvp, pat_nat(t->bvar), hs); return; }
    if (t->tag == T_LIT) { AP(occ_lit, cx->fvp, bytes_pat(t->bytes, t->n), hs); return; }
    Sym *s = t->sym;
    Atom *ann = ann_pat(t->depth, t->hasfv);
    if (s->kind == 0) {
        prove_occargs(cx, t->args, t->n, 0);
        AP(occ_const, cx->fvp, pat_nat(s->id), s->binders_pat, term_args_pat(t), ann, hs,
           nums_pat(cx->hs, cx->nh, cx->pos));
        return;
    }
    uint64_t h = cx->hs[cx->pos++];
    prove_nth(cx->fitems, cx->n, 0, h);
    prove_occargs(cx, t->args, t->n, 0);
    AP(occ_fvar, cx->fvp, pat_nat(s->id), s->binders_pat, term_args_pat(t), ann, pat_nat(h),
       nums_pat(cx->hs, cx->nh, start + 1), nums_pat(cx->hs, cx->nh, cx->pos));
}

static Term *eta_term(Sym *F) {
    Term **a = malloc((F->arity ? F->arity : 1) * sizeof *a);
    for (uint64_t i = 0; i < F->arity; i++) a[i] = mk_bvar(F->arity - 1 - i);
    Term *t = mk_app(F, a);
    free(a);
    return t;
}

static void prove_desc(uint64_t n) {
    if (n == 0) { AP0(desc_0); return; }
    prove_nadd(n - 1, 1);
    prove_desc(n - 1);
    Atom *vars = P_Nil;
    for (uint64_t k = 0; k < n - 1; k++) vars = pat_cons(pat_nat(0), vars); /* replaced below */
    (void)vars;
    /* VDescBVars(n-1, [bvar n-2 .. bvar 0]) */
    Atom *tail = P_Nil;
    for (uint64_t k = 0; k + 1 < n; k++) tail = pat_cons(term_pat(mk_bvar(k)), tail);
    AP(desc_s, pat_nat(n), pat_nat(n - 1), tail);
}

static void prove_etas(Sym **fv, uint64_t n, uint64_t from, Term **etas) {
    if (from == n) { AP0(etas_nil); return; }
    Sym *F = fv[from];
    Atom **items = malloc((F->arity ? F->arity : 1) * sizeof *items);
    for (uint64_t i = 0; i < F->arity; i++) items[i] = pat_nat(0);
    prove_len(items, F->arity, 0);
    free(items);
    prove_desc(F->arity);
    prove_etas(fv, n, from + 1, etas);
    AP(etas_cons, pat_nat(F->id), F->binders_pat, fvars_pat(fv, n, from + 1), pat_nat(F->arity),
       term_args_pat(etas[from]), terms_suffix_pat(etas, n, from + 1));
}

static bool exec_define(uint64_t *fvi, uint64_t nf, uint64_t *hs, uint64_t nh, uint64_t iv, uint64_t dsym,
                        uint64_t dthm) {
    Sym **fv = malloc((nf ? nf : 1) * sizeof *fv);
    for (uint64_t i = 0; i < nf; i++) {
        fv[i] = need_sym(fvi[i]);
        if (!fv[i]) { free(fv); STEP_FAIL("missing.symbol"); }
    }
    Term *value = need_term(iv);
    if (!value) { free(fv); STEP_FAIL("missing.term"); }
    /* kernel admission conditions */
    bool ok = value->depth == 0;
    for (uint64_t i = 0; ok && i < nf; i++) if (fv[i]->kind != 1) ok = false;
    for (uint64_t i = 0; ok && i < nh; i++) if (hs[i] >= nf) ok = false;
    if (ok) {
        /* preorder free-variable occurrences against hints */
        uint64_t pos = 0;
        Term **stack = malloc(64 * sizeof *stack);
        size_t sp = 0, cap = 64;
        stack[sp++] = value;
        while (ok && sp) {
            Term *t = stack[--sp];
            if (t->tag != T_APP) continue;
            if (t->sym->kind == 1) {
                if (pos >= nh || fv[hs[pos]] != t->sym) ok = false;
                pos++;
            }
            for (uint64_t i = t->n; i > 0; i--) {
                if (sp == cap) { cap *= 2; stack = realloc(stack, cap * sizeof *stack); }
                stack[sp++] = t->args[i - 1];
            }
        }
        free(stack);
    }
    if (!ok) { free(fv); STEP_FAIL("definitionFailed"); }
    Sym *c = calloc(1, sizeof *c);
    c->kind = 0;
    c->arity = nf;
    c->binders = calloc(nf ? nf : 1, sizeof(uint64_t));
    for (uint64_t i = 0; i < nf; i++) c->binders[i] = fv[i]->arity;
    c->id = g_next_id++;
    admit_symbol(c);
    if (sget(&S_sym, dsym)) { free(fv); STEP_FAIL("occupied.symbol"); }
    sset(&S_sym, dsym, c);
    if (sget(&S_thm, dthm)) { free(fv); STEP_FAIL("occupied.theorem"); }
    /* statement */
    Term **etas = malloc((nf ? nf : 1) * sizeof *etas);
    for (uint64_t i = 0; i < nf; i++) etas[i] = eta_term(fv[i]);
    Term *lhs = mk_app(c, etas);
    Term *stmt = app2(3, lhs, value);
    /* admission derivation */
    ensure_wf(value);
    scope_begin();
    prove_arities(fv, nf, 0, c);
    Atom **fitems = malloc((nf ? nf : 1) * sizeof *fitems);
    for (uint64_t i = 0; i < nf; i++) fitems[i] = fv[i]->pat;
    prove_len(fitems, nf, 0);
    prove_hintsin(hs, nh, 0, nf);
    ref_saved((size_t)value->wf_saved);
    prove_depth(value);
    OccCtx cx = {fv, nf, fvars_pat(fv, nf, 0), fitems, hs, nh, 0};
    prove_occ(&cx, value);
    prove_etas(fv, nf, 0, etas);
    uint64_t dl; bool fl;
    prove_annargs(c, etas, nf, 0, &dl, &fl);
    uint64_t de; bool fe;
    Term *pair[2] = {lhs, value};
    prove_annargs(&g_builtin[3], pair, 2, 0, &de, &fe);
    AP(defstmt, pat_nat(c->id), fvars_pat(fv, nf, 0), c->binders_pat, pat_nat(nf), nums_pat(hs, nh, 0),
       term_pat(value), nums_pat(hs, nh, cx.pos), term_args_pat(lhs), pat_nat(dl), pat_bool(fl), pat_nat(de),
       pat_bool(fe));
    Atom *goal = papp(H_VDefStmt, (Atom *[]){c->pat, fvars_pat(fv, nf, 0), nums_pat(hs, nh, 0), term_pat(value),
                                            term_pat(stmt)}, 5);
    scope_commit(goal);
    free(fitems);
    free(etas);
    free(fv);
    CettaInferenceRuleHandle rh = admit_fact("vibe-definition", ++g_definitions, thm_goal(stmt));
    return place_thm(dthm, stmt, true, rh);
}

static bool exec_axiom(uint64_t it, uint64_t dst) {
    if (g_phase_proofs) STEP_FAIL("axiomInProofs");
    Term *t = need_term(it);
    if (!t) STEP_FAIL("missing.term");
    if (t->depth != 0) STEP_FAIL("theoremFailed");
    if (sget(&S_thm, dst)) STEP_FAIL("occupied.theorem");
    ensure_wf(t);
    scope_begin();
    prove_depth(t);
    scope_commit(papp2(H_VDepth, term_pat(t), P_N0));
    CettaInferenceRuleHandle rh = admit_fact("vibe-axiom", ++g_axioms, thm_goal(t));
    return place_thm(dst, t, true, rh);
}

/* Execute one instruction.  Returns false on a protocol/kernel error
   (g_error set); sets *jit when the execution primitive is reached. */
static bool exec_one(bool *jit) {
    unsigned op = *p++;
    uint64_t a, b, c, d, n;
    switch (op) {
    case 0: {
        RD(a); RD(d);
        if (sget(&S_sym, d)) STEP_FAIL("occupied.symbol");
        Sym *s = calloc(1, sizeof *s);
        s->kind = 1; s->arity = a;
        s->binders = calloc(a ? a : 1, sizeof(uint64_t));
        if (!s->binders) { printf("VERDICT incomplete reason=\"symbol allocation\"\n"); exit(4); }
        s->id = g_next_id++;
        admit_symbol(s);
        sset(&S_sym, d, s);
        return true;
    }
    case 1: {
        RD(n);
        Sym *s = calloc(1, sizeof *s);
        s->kind = 0; s->arity = n;
        s->binders = calloc(n ? n : 1, sizeof(uint64_t));
        if (!s->binders) { printf("VERDICT incomplete reason=\"symbol allocation\"\n"); exit(4); }
        for (uint64_t i = 0; i < n; i++) RD(s->binders[i]);
        RD(d);
        if (sget(&S_sym, d)) STEP_FAIL("occupied.symbol");
        s->id = g_next_id++;
        admit_symbol(s);
        sset(&S_sym, d, s);
        return true;
    }
    case 2: { RD(a); RD(b); void *x = sget(&S_sym, a), *y = sget(&S_sym, b); sset(&S_sym, a, y); sset(&S_sym, b, x); return true; }
    case 3: {
        RD(a);
        if (a < 13) STEP_FAIL("protectedSymbol");
        if (!sget(&S_sym, a)) STEP_FAIL("alreadyEmpty.symbol");
        sset(&S_sym, a, NULL);
        return true;
    }
    case 4: {
        RD(a); RD(d);
        if (a == UINT64_MAX) STEP_FAIL("termBuildFailed");
        if (sget(&S_term, d)) STEP_FAIL("occupied.term");
        sset(&S_term, d, mk_bvar(a));
        return true;
    }
    case 5: {
        RD(n);
        const uint8_t *bs = p;
        p += n;
        RD(d);
        if (sget(&S_term, d)) STEP_FAIL("occupied.term");
        sset(&S_term, d, mk_lit(bs, n));
        return true;
    }
    case 6: {
        RD(a);
        Sym *s = need_sym(a);
        if (!s) STEP_FAIL("missing.symbol");
        if (s->kind > 1) STEP_FAIL("notApplicable");
        RD(n);
        if (n != s->arity) STEP_FAIL("arityMismatch");
        Term **args = malloc((n ? n : 1) * sizeof *args);
        for (uint64_t i = 0; i < n; i++) {
            RD(c);
            args[i] = need_term(c);
            if (!args[i]) { free(args); STEP_FAIL("missing.term"); }
        }
        RD(d);
        Term *t = mk_app(s, args);
        free(args);
        if (sget(&S_term, d)) STEP_FAIL("occupied.term");
        sset(&S_term, d, t);
        return true;
    }
    case 7: { RD(a); RD(b); void *x = sget(&S_term, a), *y = sget(&S_term, b); sset(&S_term, a, y); sset(&S_term, b, x); return true; }
    case 8: { RD(a); if (!sget(&S_term, a)) STEP_FAIL("alreadyEmpty.term"); sset(&S_term, a, NULL); return true; }
    case 9: { RD(a); RD(d); if (g_phase_proofs) STEP_FAIL("axiomInProofs"); return exec_axiom(a, d); }
    case 10: {
        RD(a); RD(b);
        Thm *h = need_thm(a);
        if (!h) STEP_FAIL("missing.theorem");
        Term *t = need_term(b);
        if (!t) STEP_FAIL("missing.term");
        if (h->stmt != t) STEP_FAIL("exchangeMismatch");
        return true;
    }
    case 11: { RD(a); if (!sget(&S_thm, a)) STEP_FAIL("alreadyEmpty.theorem"); sset(&S_thm, a, NULL); return true; }
    case 12: { RD(a); RD(b); void *x = sget(&S_thm, a), *y = sget(&S_thm, b); sset(&S_thm, a, y); sset(&S_thm, b, x); return true; }
    case 13: {
        RD(a); RD(d);
        Term *t = need_term(a);
        if (!t) STEP_FAIL("missing.term");
        if (sget(&S_ch, d)) STEP_FAIL("occupied.challenge");
        sset(&S_ch, d, t);
        g_open_ch++;
        if (g_phase_proofs) g_proof_ch++;
        return true;
    }
    case 14: {
        if (!g_phase_proofs) STEP_FAIL("satisfyInSetup");
        RD(a); RD(b);
        Thm *h = need_thm(b);
        if (!h) STEP_FAIL("missing.theorem");
        Term *ch = sget(&S_ch, a);
        if (!ch) STEP_FAIL("noChallenge");
        if (ch != h->stmt) STEP_FAIL("challengeMismatch");
        sset(&S_ch, a, NULL);
        g_open_ch--;
        return true;
    }
    case 15: {
        if (!g_phase_proofs) STEP_FAIL("inferenceInSetup.modusPonens");
        RD(a); RD(b); RD(d);
        Thm *hi = need_thm(a);
        if (!hi) STEP_FAIL("missing.theorem");
        Thm *hp = need_thm(b);
        if (!hp) STEP_FAIL("missing.theorem");
        return exec_mp(a, b, d);
    }
    case 16: {
        if (!g_phase_proofs) STEP_FAIL("inferenceInSetup.instantiate");
        RD(a); RD(b); RD(c); RD(d);
        return exec_inst(a, b, c, d);
    }
    case 17: {
        RD(n);
        uint64_t *fvi = malloc((n ? n : 1) * sizeof *fvi);
        for (uint64_t i = 0; i < n; i++) RD(fvi[i]);
        uint64_t nh;
        RD(nh);
        uint64_t *hs = malloc((nh ? nh : 1) * sizeof *hs);
        for (uint64_t i = 0; i < nh; i++) RD(hs[i]);
        RD(a); RD(b); RD(c);
        bool r = exec_define(fvi, n, hs, nh, a, b, c);
        free(fvi); free(hs);
        return r;
    }
    case 18: case 19: case 20: case 21: case 22: {
        static const char *names[] = {"inferenceInSetup.litIsNat", "inferenceInSetup.litLt",
                                      "inferenceInSetup.litAdd", "inferenceInSetup.litMul",
                                      "inferenceInSetup.litDiv"};
        if (!g_phase_proofs) STEP_FAIL(names[op - 18]);
        RD(a);
        if (op != 18) RD(b); else b = 0;
        RD(d);
        return exec_lit(op, a, b, d, NULL);
    }
    case 23: {
        if (!g_phase_proofs) STEP_FAIL("inferenceInSetup.litLength");
        RD(a); RD(d);
        Term *t = need_term(a);
        if (!t) STEP_FAIL("missing.term");
        return exec_lit(23, 0, 0, d, t);
    }
    case 24: {
        if (!g_phase_proofs) STEP_FAIL("inferenceInSetup.litGet");
        RD(a); RD(b); RD(d);
        Term *t = need_term(a);
        if (!t) STEP_FAIL("missing.term");
        return exec_lit(24, 0, b, d, t);
    }
    default: /* 25 */
        *jit = true;
        return true;
    }
}

typedef struct { const char *path; const uint8_t *base; size_t size; } File;

static File map_file(const char *path) {
    File f = {path, NULL, 0};
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("VERDICT io path=\"%s\"\n", path); exit(5); }
    struct stat st;
    fstat(fd, &st);
    f.size = (size_t)st.st_size;
    f.base = f.size ? mmap(NULL, f.size, PROT_READ, MAP_PRIVATE, fd, 0) : (const uint8_t *)"";
    close(fd);
    return f;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    VarInternTable variable_names;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&g_persist);
    arena_init(&g_scratch);
    double t0 = now();

    /* authority */
    const CettaNikAuthorityV1 *auth = NULL;
    for (size_t i = 0; i < cetta_vibe_itp_authorities_v1_count; i++)
        if (strcmp(cetta_vibe_itp_authorities_v1[i].alias, "VIBE-ITP") == 0) auth = &cetta_vibe_itp_authorities_v1[i];
    if (!auth) fault("VIBE-ITP authority absent from the experiment catalog");
    size_t pos = 0;
    Atom *presentation = parse_sexpr(&g_persist, auth->presentation_metta, &pos);
    if (!presentation) fault("VIBE-ITP presentation cannot be decoded");
    if (cetta_inference_checker_create(presentation, &g_checker, g_err, sizeof g_err) != CETTA_INFERENCE_OK)
        fault("VIBE-ITP presentation refused: %s", g_err);
    lookup_rules();
    cetta_inference_trace_init(&g_trace, g_checker, &g_scratch);
    /* every argument this reader passes lives in the persistent arena */
    cetta_inference_trace_retain_foreign_validation(&g_trace, true);

    A_PApp = atom_symbol(&g_persist, "PApp");
    A_LCons = atom_symbol(&g_persist, "LCons");
    A_LNil = atom_symbol(&g_persist, "LNil");
    A_GRuleV1 = atom_symbol(&g_persist, "GRuleV1");
    for (int h = 0; h < H_COUNT; h++) g_head_atoms[h] = atom_string(&g_persist, g_head_names[h]);
    g_terms = calloc(g_terms_cap, sizeof *g_terms);
    P_P1 = papp0(H_VP1); P_N0 = papp0(H_VN0); P_U0 = papp0(H_VU0); P_False = papp0(H_VFalse);
    P_True = papp0(H_VTrue); P_Nil = papp0(H_VNil); P_KConst = papp0(H_VKConst); P_KFvar = papp0(H_VKFvar);
    g_unary[0] = P_U0;
    for (int k = 1; k < 70; k++) g_unary[k] = papp1(H_VUS, g_unary[k - 1]);

    /* built-in constants: slots 2..12, declared by the authority's static rules */
    static const uint64_t ar[13] = {0, 0, 2, 2, 1, 2, 2, 2, 2, 1, 2, 4, 4};
    for (int k = 2; k <= 12; k++) {
        g_builtin[k].kind = 0;
        g_builtin[k].arity = ar[k];
        g_builtin[k].binders = g_zero_binders;
        g_builtin[k].id = (u128)k;
        sym_finish(&g_builtin[k]);
        char name[32];
        snprintf(name, sizeof name, "vibe-builtin-%d", k);
        g_builtin[k].decl_rule = cetta_inference_checker_find_rule(g_checker, name);
        if (g_builtin[k].decl_rule == CETTA_INFERENCE_RULE_HANDLE_NONE) fault("authority lacks %s", name);
        sset(&S_sym, (uint64_t)k, &g_builtin[k]);
    }
    sset(&S_sym, 0, &g_marker_bvar);
    sset(&S_sym, 1, &g_marker_lit);
    build_small_cache();

    /* arguments: setup files, --proofs, proof files */
    int proofs_at = -1, nfiles = 0;
    File *files = calloc(argc, sizeof *files);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--proofs") == 0) { proofs_at = nfiles; continue; }
        files[nfiles++] = map_file(argv[i]);
    }
    /* syntax pass over every file, in order */
    for (int i = 0; i < nfiles; i++) {
        if (!syntax_file(files[i].base, files[i].size)) {
            printf("VERDICT malformed file=%d class=%s\n", i, g_syntax);
            return 1;
        }
    }
    /* execution */
    for (int i = 0; i < nfiles; i++) {
        if (i == proofs_at) { g_phase_proofs = 1; g_setup_ch = g_open_ch; }
        p = files[i].base; pend = files[i].base + files[i].size;
        uint64_t instr = 0;
        while (p < pend) {
            bool jit = false;
            if (!exec_one(&jit)) {
                printf("VERDICT rejected file=%d instr=%" PRIu64 " class=%s\n", i, instr, g_error);
                goto stats;
            }
            if (jit) {
                printf("VERDICT unsupported file=%d instr=%" PRIu64 " reason=THM_JIT\n", i, instr);
                goto stats;
            }
            instr++;
        }
    }
    if (!g_phase_proofs) printf("VERDICT setup-only open=%" PRIu64 "\n", g_open_ch);
    else printf("VERDICT proofs setup=%" PRIu64 " proof=%" PRIu64 " open=%" PRIu64 "\n", g_setup_ch, g_proof_ch,
                g_open_ch);
stats:
    if (getenv("VIBE_NIK_PROFILE")) {
        fprintf(stderr, "references %" PRIu64 "\n", g_ref_uses);
        for (int k = 0; k < 20; k++) {
            uint64_t best = 0; int bi = -1;
            for (int r = 0; r < 512; r++) if (g_rule_uses[r] > best) { best = g_rule_uses[r]; bi = r; }
            if (bi < 0) break;
            fprintf(stderr, "rule#%d %" PRIu64 "\n", bi, best);
            g_rule_uses[bi] = 0;
        }
    }
    printf("STATS wall=%.3f steps=%" PRIu64 " actions=%" PRIu64 " admitted=%" PRIu64 " terms=%" PRIu64
           " patterns=%zu\n", now() - t0, g_steps, g_actions, g_admitted, g_terms_count, g_cons_count);
    return 0;
}
