/* holr_bridge.c - see holr_bridge.h. */
#include "holr_bridge.h"
#include "generated/hol_module_ids.h"
#include "../hol_light/hol_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUL(c) ((T *)&rc_nul[(c)])

static bool rep_nat, rep_str, rep_holname, rep_holterm;

/* ------------------------------------------------------------ native store */
HolKernel *holr_k;
HolName holr_empty_name;
static HolDecl *holr_tip;            /* the private lineage of one declaration per name */
static int holr_gc_nroots;
static uint64_t holr_gc_runs;
_Noreturn void holr_refused(const char *what) {
    if (rc_fail_hook) rc_fail_hook(what);
    fprintf(stderr, "holr: the native kernel refused %s (%s): the module built a value its representation cannot hold\n",
            what, holr_k ? hol_reason_name(holr_k->reason) : "no kernel");
    exit(5);
}
static void holr_mark_child(T *c, void *ctx) {
    (void)ctx;
    if (!RC_IS_FOREIGN(c)) return;
    switch (RC_FOREIGN_TAG(c)) {
    case 2: hk_mark_term(holr_k, (HolTerm *)RC_FOREIGN_PTR(c)); break;
    case 4: hk_mark_type(holr_k, (HolType *)RC_FOREIGN_PTR(c)); break;
    case 6: hk_mark_thm(holr_k, (HolThm *)RC_FOREIGN_PTR(c)); break;
    default: break;
    }
}
static void holr_roots(HolKernel *k, void *ctx) {
    (void)ctx;
    hk_mark_decl(k, holr_tip);
    rc_walk_live(holr_gc_nroots, holr_mark_child, NULL);
}
/* At a safepoint the live module state is rc_roots; when the native store has grown, the module's
 * old space is copied (so it holds exactly the live terms) and the native store is collected from
 * the native values that state holds. */
static void holr_safepoint(int nroots) {
    HolKernel *k = holr_k;
    if (!k) return;
    size_t limit = (size_t)64 << 20;
    if (k->bytes_live > limit) limit = k->bytes_live;
    if (k->bytes_since_gc <= limit) return;
    rc_major_now(nroots);
    holr_gc_nroots = nroots;
    hol_gc(k);
    holr_gc_runs++;
}
bool holr_store_active(void) { return holr_k != NULL; }
void holr_tables_reset(void);
void holr_tables_roots(int major, T *(*evac)(T *x));
static bool holr_foreign_equal(const T *a, const T *b);
/* The store is kept from run to run unless holr_store_fresh(true): it is a cache of interned values
 * and of one declaration per name, while the module's theory is module data, so a run does not see
 * what an earlier run declared.  A run's garbage is collected with the next collection at a
 * safepoint (any run's); at the end of a run, a store above HOLR_STORE_KEEP_BYTES is collected from
 * the constants (made static) and the declarations alone, and freed if it is still above.
 * holr_store_free frees it in any case. */
#define HOLR_STORE_KEEP_BYTES ((size_t)64 << 20)
static bool holr_fresh;
void holr_store_fresh(bool fresh) { holr_fresh = fresh; }
void holr_store_free(void) {
    holr_tables_reset();
    rc_extra_roots_hook = NULL;
    if (!holr_k) return;
    hk_root_remove(holr_k, holr_roots, NULL);
    hol_kernel_free(holr_k);
    holr_k = NULL;
    rc_foreign_hook = NULL;
    rc_foreign_equal_hook = NULL;
    rc_free_statics();
    rc_reset_constants();
}
void holr_store_end(void) {
    holr_tables_reset();
    if (!holr_k) return;
    if (holr_fresh) { holr_store_free(); return; }
    if (holr_k->bytes_live <= HOLR_STORE_KEEP_BYTES) return;
    rc_runtime_reset();
    holr_gc_nroots = 0;
    hol_gc(holr_k);
    holr_gc_runs++;
    if (holr_k->bytes_live > HOLR_STORE_KEEP_BYTES) holr_store_free();
}
void holr_store_begin(void) {
    holr_tables_reset();
    if (holr_k && !holr_fresh) return;
    holr_store_free();
    holr_k = hol_kernel_new(NULL);
    if (!holr_k) holr_refused("a new kernel");
    holr_tip = holr_k->d_eq;
    holr_empty_name = hk_name_intern(holr_k, "", 0);
    if (!holr_empty_name) holr_refused("the empty name");
    if (hk_root_add(holr_k, holr_roots, NULL) < 0) holr_refused("a root");
    holr_k->gc_threshold = SIZE_MAX;   /* collections only at the module's safepoints */
    rc_foreign_hook = holr_safepoint;
    rc_extra_roots_hook = holr_tables_roots;   /* the cells of the id tables' arrays */
    rc_foreign_equal_hook = holr_foreign_equal;
}
/* one declaration per type operator / constant name, made at its first use */
HolType *holr_ty_app(HolName n, T *args) {
    HolKernel *k = holr_k;
    HolType *small[16], **a = small;
    uint32_t na = 0;
    for (T *l = args; l->tag == C_hl_tycons; l = l->k[1]) na++;
    if (na > 16) { a = malloc(na * sizeof(HolType *)); if (!a) holr_refused("memory"); }
    uint32_t i = 0;
    for (T *l = args; l->tag == C_hl_tycons; l = l->k[1]) a[i++] = (HolType *)RC_FOREIGN_PTR(l->k[0]);
    HolDecl *d = hk_lookup_type(k, holr_tip, n);
    if (!d) {
        hk_clear(k);
        d = hk_env_new_type(k, holr_tip, n, na);
        if (!d) holr_refused("a type declaration");
        holr_tip = d;
    }
    hk_clear(k);
    HolType *t = hk_ty_app(k, d, a, na);
    if (a != small) free(a);
    return t;
}
HolTerm *holr_const(HolName n, HolType *ty) {
    HolKernel *k = holr_k;
    HolDecl *d = hk_lookup_const(k, holr_tip, n);
    if (!d) {
        hk_clear(k);
        d = hk_env_new_constant(k, holr_tip, n, ty);
        if (!d) holr_refused("a constant declaration");
        holr_tip = d;
    }
    hk_clear(k);
    return hk_tm_const(k, d, ty);
}
/* theorems as module data: equal when their hypotheses and conclusions are (terms are interned) */
static bool holr_foreign_equal(const T *a, const T *b) {
    if (RC_FOREIGN_TAG(a) != 6) return false;
    const HolThm *x = (const HolThm *)RC_FOREIGN_PTR(a), *y = (const HolThm *)RC_FOREIGN_PTR(b);
    if (x->concl != y->concl || x->nhyps != y->nhyps) return false;
    for (uint32_t i = 0; i < x->nhyps; i++) if (x->hyps[i] != y->hyps[i]) return false;
    return true;
}
T *holr_tys_of(const HolType *t) {
    T *l = (T *)&rc_nul[C_hl_tynil];
    for (uint32_t i = t->nargs; i-- > 0;) l = rc_mk2(C_hl_tycons, RC_FOREIGN(t->args[i], 4), l);
    return l;
}
HolName holr_name_cons(T *byte, HolName rest) {
    static const uint16_t nibs[16] = {C_hl_x0, C_hl_x1, C_hl_x2, C_hl_x3, C_hl_x4, C_hl_x5, C_hl_x6, C_hl_x7,
                                      C_hl_x8, C_hl_x9, C_hl_xa, C_hl_xb, C_hl_xc, C_hl_xd, C_hl_xe, C_hl_xf};
    unsigned hi = 16, lo = 16;
    for (unsigned i = 0; i < 16; i++) { if (byte->k[0]->tag == nibs[i]) hi = i; if (byte->k[1]->tag == nibs[i]) lo = i; }
    if (byte->tag != C_hl_byte || hi > 15 || lo > 15) holr_refused("a byte");
    const HolNameRec *r = &holr_k->names[rest];
    size_t len = r->len;
    char small[256], *buf = len + 1 <= sizeof small ? small : malloc(len + 1);
    if (!buf) holr_refused("memory");
    buf[0] = (char)(16 * hi + lo);
    memcpy(buf + 1, r->bytes, len);
    HolName n = hk_name_intern(holr_k, buf, len + 1);
    if (buf != small) free(buf);
    if (!n) holr_refused("a name");
    return n;
}
static T *byte_terms[256];
static const uint16_t nib_ctor[16] = {C_hl_x0, C_hl_x1, C_hl_x2, C_hl_x3, C_hl_x4, C_hl_x5, C_hl_x6, C_hl_x7,
                                      C_hl_x8, C_hl_x9, C_hl_xa, C_hl_xb, C_hl_xc, C_hl_xd, C_hl_xe, C_hl_xf};

void holr_bridge_init(void) {
    if (byte_terms[0]) return;
    rep_nat = rc_sort_rep[S_Nat] == 2;
    rep_str = rc_sort_rep[S_Str] == 3;
    rep_holname = rc_sort_rep[S_Str] == 4;
    rep_holterm = rc_sort_rep[S_Term] == 4;
    for (unsigned v = 0; v < 256; v++) {
        T *t = malloc(sizeof(TH) + 2 * sizeof(T *));
        if (!t) { fputs("holr_bridge: out of memory\n", stderr); exit(3); }
        t->tag = C_hl_byte; t->space = RC_SPACE_STATIC; t->flags = 0; t->n = 2;
        t->k[0] = NUL(nib_ctor[v >> 4]); t->k[1] = NUL(nib_ctor[v & 15]);
        byte_terms[v] = t;
    }
}

T *rc_byte_term(unsigned v) { return byte_terms[v & 255]; }

static T *nat_term(uint64_t v) {
    if (rep_nat) return RC_IMM(v);
    int bits[64], n = 0;
    while (v) { bits[n++] = (int)(v & 1); v >>= 1; }
    T *t = NUL(C_hl_nz);
    while (n--) t = rc_mk1(bits[n] ? C_hl_nb1 : C_hl_nb0, t);
    return t;
}
static T *str_term(const uint8_t *b, size_t n) {
    if (rep_holname) {
        HolName id = hk_name_intern(holr_k, (const char *)b, n);
        if (!id) holr_refused("a name");
        return RC_IMM(id);
    }
    if (rep_str) return rc_bytes_new(b, n);
    T *t = NUL(C_hl_nnil);
    while (n--) t = rc_mk2(C_hl_ncons, byte_terms[b[n]], t);
    return t;
}
static T *ids_term(const uint64_t *v, uint32_t n) {
    T *t = NUL(C_hl_ids_nil);
    while (n--) t = rc_mk2(C_hl_ids_cons, nat_term(v[n]), t);
    return t;
}
static T *pairs_term(const uint64_t *v, uint32_t n) {
    T *t = NUL(C_hl_idp_nil);
    for (uint32_t i = n / 2; i-- > 0;) t = rc_mk3(C_hl_idp_cons, nat_term(v[2 * i]), nat_term(v[2 * i + 1]), t);
    return t;
}

/* decoded records, owned here; lists and names in one growing pool */
typedef struct { uint8_t op; uint64_t f[4]; uint32_t nl; size_t list; uint32_t nb; size_t bytes; } Rec;
static Rec *recs;
static size_t nrecs, reccap;
static uint8_t *pool;
static size_t pool_len, pool_cap;
uint64_t rc_record_count;

static size_t pool_add(const void *p, size_t n) {
    size_t at = (pool_len + 7u) & ~(size_t)7u;
    if (at + n > pool_cap) {
        size_t cap = pool_cap ? pool_cap : (size_t)1 << 16;
        while (cap < at + n) cap *= 2;
        pool = realloc(pool, cap);
        if (!pool) { fputs("holr_bridge: out of memory\n", stderr); exit(3); }
        pool_cap = cap;
    }
    if (n) memcpy(pool + at, p, n);
    pool_len = at + n;
    return at;
}

static bool scan_keep(void *ctx, const HolScanRecord *r) {
    (void)ctx;
    if (nrecs == reccap) { reccap = reccap ? 2 * reccap : 1024; recs = realloc(recs, reccap * sizeof(Rec)); if (!recs) exit(3); }
    Rec *x = &recs[nrecs++];
    x->op = r->opcode;
    for (int i = 0; i < 4; i++) x->f[i] = r->fixed[i];
    x->nl = (uint32_t)r->nlist;
    x->list = pool_add(r->list, r->nlist * sizeof(uint64_t));
    x->nb = (uint32_t)r->nbytes;
    x->bytes = pool_add(r->bytes, r->nbytes);
    return true;
}

size_t holr_bridge_records(void) { return nrecs; }

T *rc_record_term(uint64_t i) {
    Rec *r = &recs[i];
    const uint64_t *a = r->f;
    const uint64_t *l = (const uint64_t *)(pool + r->list);
    switch (r->op) {
    case HOLR_NAME: return rc_mk1(C_holr_NAME, str_term(pool + r->bytes, r->nb));
    case HOLR_TYVAR: return rc_mk1(C_holr_TYVAR, nat_term(a[0]));
    case HOLR_TYAPP: return rc_mk2(C_holr_TYAPP, nat_term(a[0]), ids_term(l, r->nl));
    case HOLR_VAR: return rc_mk2(C_holr_VAR, nat_term(a[0]), nat_term(a[1]));
    case HOLR_CONST: return rc_mk2(C_holr_CONST, nat_term(a[0]), nat_term(a[1]));
    case HOLR_COMB: return rc_mk2(C_holr_COMB, nat_term(a[0]), nat_term(a[1]));
    case HOLR_ABS: return rc_mk2(C_holr_ABS, nat_term(a[0]), nat_term(a[1]));
    case HOLR_REFL: return rc_mk1(C_holr_REFL, nat_term(a[0]));
    case HOLR_TRANS: return rc_mk2(C_holr_TRANS, nat_term(a[0]), nat_term(a[1]));
    case HOLR_MK_COMB: return rc_mk2(C_holr_MK_COMB, nat_term(a[0]), nat_term(a[1]));
    case HOLR_ABS_RULE: return rc_mk2(C_holr_ABS_RULE, nat_term(a[0]), nat_term(a[1]));
    case HOLR_BETA: return rc_mk1(C_holr_BETA, nat_term(a[0]));
    case HOLR_ASSUME: return rc_mk1(C_holr_ASSUME, nat_term(a[0]));
    case HOLR_EQ_MP: return rc_mk2(C_holr_EQ_MP, nat_term(a[0]), nat_term(a[1]));
    case HOLR_DEDUCT: return rc_mk2(C_holr_DEDUCT_ANTISYM_RULE, nat_term(a[0]), nat_term(a[1]));
    case HOLR_INST_TYPE: return rc_mk2(C_holr_INST_TYPE, pairs_term(l, r->nl), nat_term(a[0]));
    case HOLR_INST: return rc_mk2(C_holr_INST, pairs_term(l, r->nl), nat_term(a[0]));
    case HOLR_NEW_TYPE: return rc_mk2(C_holr_NEW_TYPE, nat_term(a[0]), nat_term(a[1]));
    case HOLR_NEW_CONSTANT: return rc_mk2(C_holr_NEW_CONSTANT, nat_term(a[0]), nat_term(a[1]));
    case HOLR_NEW_AXIOM: return rc_mk1(C_holr_NEW_AXIOM, nat_term(a[0]));
    case HOLR_NEW_DEFINITION: return rc_mk1(C_holr_NEW_DEFINITION, nat_term(a[0]));
    case HOLR_NEW_TYPEDEF: return rc_mk4(C_holr_NEW_TYPEDEF, nat_term(a[0]), nat_term(a[1]), nat_term(a[2]), nat_term(a[3]));
    case HOLR_POLICY: return rc_mk1(C_holr_POLICY, nat_term(a[0]));
    case HOLR_EXPECT: return rc_mk3(C_holr_EXPECT, nat_term(a[0]), ids_term(l, r->nl), nat_term(a[1]));
    case HOLR_EXPECT_ALPHA: return rc_mk3(C_holr_EXPECT_ALPHA, nat_term(a[0]), ids_term(l, r->nl), nat_term(a[1]));
    case HOLR_EXPECT_AXIOMS: return rc_mk2(C_holr_EXPECT_AXIOMS, nat_term(a[0]), ids_term(l, r->nl));
    case HOLR_EXPECT_REFUSED: return NUL(C_holr_EXPECT_REFUSED);
    case HOLR_LABEL: return rc_mk2(C_holr_LABEL, nat_term(a[0]), nat_term(a[1]));
    case HOLR_EMIT: return rc_mk1(C_holr_EMIT, nat_term(a[0]));
    case HOLR_DROP_TYPE: return rc_mk1(C_holr_DROP_TYPE, nat_term(a[0]));
    case HOLR_DROP_TERM: return rc_mk1(C_holr_DROP_TERM, nat_term(a[0]));
    case HOLR_DROP_THM: return rc_mk1(C_holr_DROP_THM, nat_term(a[0]));
    default: rc_refinement_fail("a decoded record has no module form");
    }
}

static int terminal_ctor(HolReason why) {
    switch (why) {
    case HOL_R_STREAM_MAGIC: return C_hl_bad_magic;
    case HOL_R_STREAM_VERSION: return C_hl_bad_version;
    case HOL_R_STREAM_HEADER: return C_hl_bad_header;
    case HOL_R_STREAM_TRUNCATED: return C_hl_truncated;
    case HOL_R_STREAM_INTEGER: return C_hl_bad_integer;
    case HOL_R_STREAM_OPCODE: return C_hl_bad_opcode;
    case HOL_R_STREAM_TRAILING: return C_hl_trailing_bytes;
    case HOL_R_STREAM_CHECKSUM: return C_hl_checksum_mismatch;
    case HOL_R_STREAM_COUNT: return C_hl_count_mismatch;
    case HOL_R_STREAM_LIMIT: return C_hl_limit_exceeded;
    case HOL_R_STREAM_MISSING_END: return C_hl_missing_end;
    case HOL_R_NOMEM: return C_hl_decoder_out_of_memory;
    default: return C_hl_decoder_other;
    }
}

T *holr_bridge_decode(const uint8_t *bytes, size_t len) {
    if (rep_holname || rep_holterm) holr_store_begin();
    nrecs = 0;
    pool_len = 0;
    HolScanHeader hdr;
    memset(&hdr, 0, sizeof hdr);
    HolReason why = HOL_R_NONE;
    HolReplayState st = hol_stream_scan(bytes, len, &hdr, scan_keep, NULL, &why);
    rc_record_count = nrecs;
    T *header = hdr.header_len == 0 && st != HOL_REPLAY_ACCEPTED
        ? NUL(C_hl_header_malformed)
        : rc_mk2(C_hl_header, NUL(hdr.policy == HOL_AXIOMS_OPEN ? C_hl_open : hdr.policy == HOL_AXIOMS_STANDARD ? C_hl_standard : C_hl_sealed),
                 NUL((hdr.flags & HOLR_FLAG_PROBE) ? C_hl_probe : C_hl_strict));
    T *terminal = st == HOL_REPLAY_ACCEPTED ? NUL(C_hl_end_ok) : rc_mk1(C_hl_end_malformed, NUL(terminal_ctor(why)));
    return rc_mk3(C_hl_decoded, RC_IMM(0), header, terminal);
}

T *holr_bridge_policy(HolAxiomPolicy p) {
    return NUL(p == HOL_AXIOMS_OPEN ? C_hl_open : p == HOL_AXIOMS_STANDARD ? C_hl_standard : C_hl_sealed);
}

T *holr_bridge_check(T *host, T *decoded) {
    T *args[2] = {host, decoded};
    return rc_call(C_hl_check, args);
}

/* ---------------------------------------------------------------------- text */
void hb_put(HbText *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->s = realloc(b->s, b->cap);
        if (!b->s) { fputs("holr_bridge: out of memory\n", stderr); exit(3); }
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}
void hb_puts(HbText *b, const char *s) { hb_put(b, s, strlen(s)); }
void hb_nat_text(HbText *b, uint64_t v) {
    int bits[64], n = 0;
    while (v) { bits[n++] = (int)(v & 1); v >>= 1; }
    for (int i = 0; i < n; i++) hb_puts(b, bits[i] ? "(hl:nb1 " : "(hl:nb0 ");
    hb_puts(b, "(hl:nz)");
    for (int i = 0; i < n; i++) hb_puts(b, ")");
}
void hb_str_text(HbText *b, const uint8_t *p, size_t n) {
    static const char hexd[] = "0123456789abcdef";
    char tmp[64];
    for (size_t i = 0; i < n; i++) {
        snprintf(tmp, sizeof tmp, "(hl:ncons (hl:byte (hl:x%c) (hl:x%c)) ", hexd[p[i] >> 4], hexd[p[i] & 15]);
        hb_puts(b, tmp);
    }
    hb_puts(b, "(hl:nnil)");
    for (size_t i = 0; i < n; i++) hb_puts(b, ")");
}
static void hb_name_text(HbText *b, HolName n) {
    const HolNameRec *r = &holr_k->names[n];
    hb_str_text(b, (const uint8_t *)r->bytes, r->len);
}
static void hb_native_type(HbText *b, const HolType *t) {
    if (t->kind == HOL_TY_VAR) { hb_puts(b, "(hl:Tyvar "); hb_name_text(b, t->name); hb_puts(b, ")"); return; }
    hb_puts(b, "(hl:Tyapp "); hb_name_text(b, t->name); hb_puts(b, " ");
    for (uint32_t i = 0; i < t->nargs; i++) { hb_puts(b, "(hl:tycons "); hb_native_type(b, t->args[i]); hb_puts(b, " "); }
    hb_puts(b, "(hl:tynil)");
    for (uint32_t i = 0; i < t->nargs; i++) hb_puts(b, ")");
    hb_puts(b, ")");
}
static void hb_native_term(HbText *b, const HolTerm *t) {
    switch (t->kind) {
    case HOL_TM_VAR: case HOL_TM_CONST:
        hb_puts(b, t->kind == HOL_TM_VAR ? "(hl:Var " : "(hl:Const "); hb_name_text(b, t->u.leaf.name);
        hb_puts(b, " "); hb_native_type(b, t->type); hb_puts(b, ")");
        return;
    default:
        hb_puts(b, t->kind == HOL_TM_COMB ? "(hl:Comb " : "(hl:Abs ");
        hb_native_term(b, t->u.node.l); hb_puts(b, " "); hb_native_term(b, t->u.node.r); hb_puts(b, ")");
        return;
    }
}
static void hb_native_thm(HbText *b, const HolThm *th) {
    hb_puts(b, "(hl:Sequent ");
    for (uint32_t i = 0; i < th->nhyps; i++) { hb_puts(b, "(hl:tmcons "); hb_native_term(b, th->hyps[i]); hb_puts(b, " "); }
    hb_puts(b, "(hl:tmnil)");
    for (uint32_t i = 0; i < th->nhyps; i++) hb_puts(b, ")");
    hb_puts(b, " "); hb_native_term(b, th->concl); hb_puts(b, ")");
}
void hb_term_text(HbText *b, const T *t, int sort) {
    if (RC_IS_FOREIGN(t)) {
        unsigned tag = RC_FOREIGN_TAG(t);
        if (tag == 2) hb_native_term(b, (const HolTerm *)RC_FOREIGN_PTR(t));
        else if (tag == 4) hb_native_type(b, (const HolType *)RC_FOREIGN_PTR(t));
        else if (tag == 6) hb_native_thm(b, (const HolThm *)RC_FOREIGN_PTR(t));
        else hb_puts(b, "(holr-bridge:foreign)");
        return;
    }
    if (RC_IS_IMM(t)) {
        if (sort == S_Nat) { hb_nat_text(b, RC_IMM_VAL(t)); return; }
        if (sort == S_Str && rep_holname) { hb_name_text(b, (HolName)RC_IMM_VAL(t)); return; }
        hb_puts(b, "(holr-bridge:immediate)");
        return;
    }
    if (t->flags & RC_FLAG_BYTES) { hb_str_text(b, rc_bytes(t), t->n); return; }
    hb_puts(b, "(");
    hb_puts(b, rc_ctor_name[t->tag]);
    for (uint32_t i = 0; i < t->n; i++) {
        hb_puts(b, " ");
        hb_term_text(b, t->k[i], rc_ctor_asort[rc_ctor_asort_off[t->tag] + i]);
    }
    hb_puts(b, ")");
}
