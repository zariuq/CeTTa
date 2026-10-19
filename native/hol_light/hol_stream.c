/* hol_stream.c - HOLR v1 replay engine, writer, proof journal and export.
 *
 * Each record is handled in three phases:
 *   1. decode: pure parsing of the bytes into a Rec (no kernel effects);
 *      an incomplete record yields NEED_INPUT (or TRUNCATED when final);
 *   2. execute: kernel operations on the referenced objects; output table
 *      capacity is reserved first so that
 *   3. commit (table writes, environment update, cursor advance) cannot fail.
 * A kernel resource failure in phase 2 leaves the replay exactly as before
 * the record, so resuming retries it.  Objects created by an uncommitted
 * attempt are unreachable and are reclaimed by the next collection. */
#include "hol_internal.h"
#include "hol_replay.h"

#include <stdio.h>

/* Raw SHA-256 of the pinned fusion.ml; a compatibility identifier only. */
static const uint8_t holr_kernel_sha[32] = {
    0x29, 0x54, 0x4b, 0xe9, 0x2d, 0x9c, 0xc1, 0xe6, 0xb3, 0xb5, 0x9b, 0xa5,
    0xb2, 0x10, 0x60, 0x4b, 0x9a, 0x74, 0x83, 0x66, 0xa6, 0x8b, 0x3b, 0xcd,
    0xc2, 0x61, 0x97, 0x17, 0xd6, 0x8f, 0xd9, 0x8a};

enum { ST_OK = 0, ST_FAILED = 1, ST_DROPPED = 2 };

typedef struct {
    void **obj;
    uint8_t *st;
    uint64_t n, cap;
} Space;

typedef struct { uint64_t thm; HolName name; } Label;

struct HolReplay {
    HolKernel *k;
    HolReplayLimits lim;
    uint8_t *buf;     /* unconsumed input: bytes [pos, len) are pending */
    size_t len, cap, pos;
    uint64_t base;    /* stream offset of buf[0] (consumed bytes are discarded) */
    bool final, header_done, ended;
    uint32_t flags;
    int host_policy;
    HolReplayState state;
    HolStatus status;
    HolReason reason;
    uint64_t records, failed, checks;
    uint64_t fnv; /* FNV-1a of committed bytes */
    HolDecl *env;
    HolName *names;
    size_t nnames, capnames;
    Space ty, tm, th;
    uint64_t *emit;               /* EMIT records: theorem ids, in order */
    size_t nemit, capemit, delivered;
    Label *labels;
    size_t nlabels, caplabels;
    uint64_t *axioms;             /* NEW_AXIOM output ids, increasing */
    size_t naxioms, capaxioms;
    bool last_refused;
    HolReplayObserver obs;
    void *obs_ctx;
    uint64_t sample; /* observe every sample-th record (0, 1: all) */
};

/* ------------------------------------------------------------------------ */
/* GC roots                                                                  */
/* ------------------------------------------------------------------------ */

static void replay_roots(HolKernel *k, void *ctx) {
    HolReplay *r = (HolReplay *)ctx;
    hk_mark_decl(k, r->env);
    for (uint64_t i = 0; i < r->ty.n; i++)
        if (r->ty.st[i] == ST_OK) hk_mark_type(k, (HolType *)r->ty.obj[i]);
    for (uint64_t i = 0; i < r->tm.n; i++)
        if (r->tm.st[i] == ST_OK) hk_mark_term(k, (HolTerm *)r->tm.obj[i]);
    for (uint64_t i = 0; i < r->th.n; i++)
        if (r->th.st[i] == ST_OK) hk_mark_thm(k, (HolThm *)r->th.obj[i]);
}

/* ------------------------------------------------------------------------ */
/* Construction                                                              */
/* ------------------------------------------------------------------------ */

static void limits_default(HolReplayLimits *l, const HolReplayLimits *in) {
    if (in) *l = *in;
    else memset(l, 0, sizeof *l);
    if (!l->max_buffered_bytes) l->max_buffered_bytes = (uint64_t)1 << 30;
    if (!l->max_records) l->max_records = (uint64_t)1 << 40;
    if (!l->max_ids) l->max_ids = (uint64_t)1 << 32;
    if (!l->max_name_bytes) l->max_name_bytes = 1u << 20;
    if (!l->max_operands) l->max_operands = 1u << 20;
}

HolReplay *hol_replay_new(HolKernel *k, const HolReplayLimits *limits, HolAxiomPolicy host_policy) {
    if (!k) return NULL;
    HolReplay *r = (HolReplay *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->k = k;
    limits_default(&r->lim, limits);
    r->host_policy = (int)host_policy;
    r->env = k->d_eq;
    r->fnv = HOL_FNV_OFFSET;
    r->state = HOL_REPLAY_NEED_INPUT;
    if (hk_root_add(k, replay_roots, r) < 0) {
        free(r);
        return NULL;
    }
    return r;
}

void hol_replay_free(HolReplay *r) {
    if (!r) return;
    hk_root_remove(r->k, replay_roots, r);
    free(r->buf);
    free(r->names);
    free(r->ty.obj); free(r->ty.st);
    free(r->tm.obj); free(r->tm.st);
    free(r->th.obj); free(r->th.st);
    free(r->emit);
    free(r->labels);
    free(r->axioms);
    free(r);
}

static bool space_copy(Space *dst, const Space *src) {
    *dst = *src;
    dst->obj = NULL;
    dst->st = NULL;
    if (!src->cap) return true;
    dst->obj = (void **)malloc(src->cap * sizeof(void *));
    dst->st = (uint8_t *)malloc(src->cap);
    if (!dst->obj || !dst->st) return false;
    memcpy(dst->obj, src->obj, src->n * sizeof(void *));
    memcpy(dst->st, src->st, src->n);
    return true;
}

HolReplay *hol_replay_clone(const HolReplay *s) {
    if (!s) return NULL;
    HolReplay *r = (HolReplay *)calloc(1, sizeof *r);
    if (!r) return NULL;
    *r = *s;
    r->buf = NULL;
    r->names = NULL;
    r->ty.obj = r->tm.obj = r->th.obj = NULL;
    r->ty.st = r->tm.st = r->th.st = NULL;
    r->emit = NULL;
    r->labels = NULL;
    r->axioms = NULL;
    bool ok = true;
    if (s->cap) {
        r->buf = (uint8_t *)malloc(s->cap);
        ok = r->buf != NULL;
        if (ok) memcpy(r->buf, s->buf, s->len);
    }
    if (ok && s->capnames) {
        r->names = (HolName *)malloc(s->capnames * sizeof(HolName));
        ok = r->names != NULL;
        if (ok) memcpy(r->names, s->names, s->nnames * sizeof(HolName));
    }
    ok = ok && space_copy(&r->ty, &s->ty) && space_copy(&r->tm, &s->tm) && space_copy(&r->th, &s->th);
    if (ok && s->capemit) {
        r->emit = (uint64_t *)malloc(s->capemit * sizeof(uint64_t));
        ok = r->emit != NULL;
        if (ok) memcpy(r->emit, s->emit, s->nemit * sizeof(uint64_t));
    }
    if (ok && s->caplabels) {
        r->labels = (Label *)malloc(s->caplabels * sizeof(Label));
        ok = r->labels != NULL;
        if (ok) memcpy(r->labels, s->labels, s->nlabels * sizeof(Label));
    }
    if (ok && s->capaxioms) {
        r->axioms = (uint64_t *)malloc(s->capaxioms * sizeof(uint64_t));
        ok = r->axioms != NULL;
        if (ok) memcpy(r->axioms, s->axioms, s->naxioms * sizeof(uint64_t));
    }
    if (ok) ok = hk_root_add(r->k, replay_roots, r) == 0;
    if (!ok) {
        free(r->buf); free(r->names);
        free(r->ty.obj); free(r->ty.st); free(r->tm.obj); free(r->tm.st);
        free(r->th.obj); free(r->th.st); free(r->emit); free(r->labels); free(r->axioms);
        free(r);
        return NULL;
    }
    return r;
}

void hol_replay_set_sampling(HolReplay *r, uint64_t period) { r->sample = period; }

void hol_replay_set_observer(HolReplay *r, HolReplayObserver fn, void *ctx) {
    r->obs = fn;
    r->obs_ctx = ctx;
}

HolReplayState hol_replay_state(const HolReplay *r) { return r->state; }
HolStatus hol_replay_status(const HolReplay *r) { return r->status; }
HolReason hol_replay_reason(const HolReplay *r) { return r->reason; }

void hol_replay_info(const HolReplay *r, HolReplayInfo *out) {
    memset(out, 0, sizeof *out);
    out->records = r->records;
    out->offset = r->base + r->pos;
    out->names = r->nnames;
    out->types = r->ty.n;
    out->terms = r->tm.n;
    out->thms = r->th.n;
    out->emitted = r->nemit;
    out->delivered = r->delivered;
    out->failed = r->failed;
    out->expectations_checked = r->checks;
}

static HolReplayState stop(HolReplay *r, HolReplayState st, HolStatus s, HolReason why) {
    r->state = st;
    r->status = s;
    r->reason = why;
    return st;
}

HolReplayState hol_replay_feed(HolReplay *r, const void *bytes, size_t n, bool final) {
    if (r->state == HOL_REPLAY_MALFORMED || r->state == HOL_REPLAY_REFUSED) return r->state;
    if (r->final && n) return stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_TRAILING);
    if (r->ended && n) return stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_AFTER_END);
    /* Consumed bytes are no longer needed: the checksum is incremental. */
    if (r->header_done && r->pos > 0 && n) {
        memmove(r->buf, r->buf + r->pos, r->len - r->pos);
        r->base += r->pos;
        r->len -= r->pos;
        r->pos = 0;
    }
    if ((uint64_t)r->len + n > r->lim.max_buffered_bytes)
        return stop(r, HOL_REPLAY_MALFORMED, HOL_LIMIT, HOL_R_STREAM_LIMIT);
    if (r->len + n > r->cap) {
        size_t ncap = r->cap ? r->cap : 4096;
        while (ncap < r->len + n) ncap *= 2;
        uint8_t *nb = (uint8_t *)realloc(r->buf, ncap);
        if (!nb) return stop(r, HOL_REPLAY_RESOURCE, HOL_NOMEM, HOL_R_NOMEM);
        r->buf = nb;
        r->cap = ncap;
    }
    if (n) memcpy(r->buf + r->len, bytes, n);
    r->len += n;
    if (final) r->final = true;
    if (r->ended) {
        if (r->final && r->pos != r->len) return stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_TRAILING);
        return r->state;
    }
    if (r->state == HOL_REPLAY_NEED_INPUT) r->state = HOL_REPLAY_RUNNING;
    return r->state;
}

/* ------------------------------------------------------------------------ */
/* Decoding                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *p, *end;
    bool final;
    int err; /* 0 ok, 1 need input, 2 malformed */
    HolReason why;
} Cur;

static void cur_bad(Cur *c, HolReason why) {
    if (!c->err) { c->err = 2; c->why = why; }
}

static void cur_short(Cur *c) {
    if (c->err) return;
    if (c->final) { c->err = 2; c->why = HOL_R_STREAM_TRUNCATED; }
    else c->err = 1;
}

static uint8_t cur_u8(Cur *c) {
    if (c->err) return 0;
    if (c->p >= c->end) { cur_short(c); return 0; }
    return *c->p++;
}

/* Unsigned LEB128 in its minimal form, at most 9 bytes, value below 2^62
 * (so that every value is an OCaml int as well; see docs/STREAM.md). */
#define HOLR_UV_LIMIT ((uint64_t)1 << 62)
static uint64_t cur_uv(Cur *c) {
    uint64_t v = 0;
    for (unsigned i = 0;; i++) {
        if (c->err) return 0;
        if (c->p >= c->end) { cur_short(c); return 0; }
        uint8_t b = *c->p++;
        v |= (uint64_t)(b & 0x7f) << (7 * i);
        if (b & 0x80) {
            if (i == 8) { cur_bad(c, HOL_R_STREAM_INTEGER); return 0; }
            continue;
        }
        if ((i > 0 && b == 0) || v >= HOLR_UV_LIMIT) { cur_bad(c, HOL_R_STREAM_INTEGER); return 0; }
        return v;
    }
}

static const uint8_t *cur_bytes(Cur *c, uint64_t n) {
    if (c->err) return NULL;
    if ((uint64_t)(c->end - c->p) < n) { cur_short(c); return NULL; }
    const uint8_t *p = c->p;
    c->p += n;
    return p;
}

/* ------------------------------------------------------------------------ */
/* Record layouts                                                            */
/* ------------------------------------------------------------------------ */

/* The operands of each record, in encoding order (docs/STREAM.md):
 *   N        name id            Y, T, H   type, term, theorem id
 *   u        unsigned value     y, t, h   id to drop (type, term, theorem)
 *   *X       counted list of X  %XZ       counted list of pairs (X, Z)
 *   b        counted bytes      c         8-byte little-endian checksum
 * A layout holds at most one list.  out/nout: the id space a record defines
 * ids in, and how many. */
typedef struct {
    const char *name;
    const char *layout;
    char out;
    uint8_t nout;
} OpInfo;

static const OpInfo op_table[128] = {
    [HOLR_NAME] = {"NAME", "b", 'N', 1},
    [HOLR_TYVAR] = {"TYVAR", "N", 'Y', 1},
    [HOLR_TYAPP] = {"TYAPP", "N*Y", 'Y', 1},
    [HOLR_VAR] = {"VAR", "NY", 'T', 1},
    [HOLR_CONST] = {"CONST", "NY", 'T', 1},
    [HOLR_COMB] = {"COMB", "TT", 'T', 1},
    [HOLR_ABS] = {"ABS", "TT", 'T', 1},
    [HOLR_REFL] = {"REFL", "T", 'H', 1},
    [HOLR_TRANS] = {"TRANS", "HH", 'H', 1},
    [HOLR_MK_COMB] = {"MK_COMB", "HH", 'H', 1},
    [HOLR_ABS_RULE] = {"ABS_RULE", "TH", 'H', 1},
    [HOLR_BETA] = {"BETA", "T", 'H', 1},
    [HOLR_ASSUME] = {"ASSUME", "T", 'H', 1},
    [HOLR_EQ_MP] = {"EQ_MP", "HH", 'H', 1},
    [HOLR_DEDUCT] = {"DEDUCT_ANTISYM_RULE", "HH", 'H', 1},
    [HOLR_INST_TYPE] = {"INST_TYPE", "%YYH", 'H', 1},
    [HOLR_INST] = {"INST", "%TTH", 'H', 1},
    [HOLR_NEW_TYPE] = {"NEW_TYPE", "Nu", 0, 0},
    [HOLR_NEW_CONSTANT] = {"NEW_CONSTANT", "NY", 0, 0},
    [HOLR_NEW_AXIOM] = {"NEW_AXIOM", "T", 'H', 1},
    [HOLR_NEW_DEFINITION] = {"NEW_DEFINITION", "T", 'H', 1},
    [HOLR_NEW_TYPEDEF] = {"NEW_TYPEDEF", "NNNH", 'H', 2},
    [HOLR_POLICY] = {"POLICY", "u", 0, 0},
    [HOLR_EXPECT] = {"EXPECT", "H*TT", 0, 0},
    [HOLR_LABEL] = {"LABEL", "HN", 0, 0},
    [HOLR_EMIT] = {"EMIT", "H", 0, 0},
    [HOLR_EXPECT_REFUSED] = {"EXPECT_REFUSED", "", 0, 0},
    [HOLR_EXPECT_AXIOMS] = {"EXPECT_AXIOMS", "H*H", 0, 0},
    [HOLR_EXPECT_ALPHA] = {"EXPECT_ALPHA", "H*TT", 0, 0},
    [HOLR_DROP_TYPE] = {"DROP_TYPE", "y", 0, 0},
    [HOLR_DROP_TERM] = {"DROP_TERM", "t", 0, 0},
    [HOLR_DROP_THM] = {"DROP_THM", "h", 0, 0},
    [HOLR_END] = {"END", "uc", 0, 0},
};

static const OpInfo *op_info(uint8_t op) {
    return op < 128 && op_table[op].layout ? &op_table[op] : NULL;
}

const char *hol_replay_opname(uint8_t op) {
    const OpInfo *oi = op_info(op);
    return oi ? oi->name : "?";
}

/* ------------------------------------------------------------------------ */
/* Decoding                                                                  */
/* ------------------------------------------------------------------------ */

#define REC_MAX_FIXED 4
typedef struct {
    uint8_t op;
    const OpInfo *info;
    uint64_t a[REC_MAX_FIXED]; /* non-list operands, in order */
    unsigned na;
    uint64_t *v; /* the list (pairs flattened), if the layout has one */
    uint64_t nv, capv;
    const uint8_t *bytes; /* NAME bytes */
    uint64_t nbytes;
} Rec;

static bool rec_push(Rec *rc, uint64_t x) {
    if (rc->nv == rc->capv) {
        uint64_t ncap = rc->capv ? rc->capv * 2 : 16;
        uint64_t *nv = (uint64_t *)realloc(rc->v, ncap * sizeof(uint64_t));
        if (!nv) return false;
        rc->v = nv;
        rc->capv = ncap;
    }
    rc->v[rc->nv++] = x;
    return true;
}

/* Parses one record following its layout; no kernel effects.
 * Returns 0 decoded, 1 need input, 2 malformed (c->why), 3 out of memory. */
static int decode(HolReplay *r, Cur *c, Rec *rc) {
    rc->op = cur_u8(c);
    rc->na = 0;
    rc->nv = 0;
    rc->bytes = NULL;
    rc->nbytes = 0;
    if (c->err) return c->err;
    rc->info = op_info(rc->op);
    if (!rc->info) { cur_bad(c, HOL_R_STREAM_OPCODE); return 2; }
    for (const char *l = rc->info->layout; *l; l++) {
        if (*l == 'b') {
            uint64_t n = cur_uv(c);
            if (c->err) return c->err;
            if (n > r->lim.max_name_bytes) { cur_bad(c, HOL_R_STREAM_LIMIT); return 2; }
            rc->bytes = cur_bytes(c, n);
            rc->nbytes = n;
        } else if (*l == 'c') {
            const uint8_t *sum = cur_bytes(c, 8);
            if (c->err) return c->err;
            uint64_t f = 0;
            for (int i = 7; i >= 0; i--) f = (f << 8) | sum[i];
            rc->a[rc->na++] = f;
        } else if (*l == '*' || *l == '%') {
            unsigned width = *l == '*' ? 1 : 2;
            uint64_t n = cur_uv(c);
            if (c->err) return c->err;
            if (n > r->lim.max_operands) { cur_bad(c, HOL_R_STREAM_LIMIT); return 2; }
            for (uint64_t i = 0; i < width * n; i++) {
                uint64_t x = cur_uv(c);
                if (c->err) return c->err;
                if (!rec_push(rc, x)) return 3;
            }
            l += width; /* the element kinds */
        } else {
            rc->a[rc->na++] = cur_uv(c);
        }
        if (c->err) return c->err;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Validation                                                                */
/* ------------------------------------------------------------------------ */

/* 0: usable; 2: a FAILED placeholder (dependent failure); 3: malformed. */
static int check_id(const HolReplay *r, char kind, uint64_t id, HolReason *why) {
    const Space *s;
    switch (kind) {
    case 'N':
        if (id >= r->nnames) { *why = HOL_R_STREAM_FUTURE_REF; return 3; }
        return 0;
    case 'Y': case 'y': s = &r->ty; break;
    case 'T': case 't': s = &r->tm; break;
    case 'H': case 'h': s = &r->th; break;
    default: return 0; /* u, c, b: plain values */
    }
    if (id >= s->n) { *why = HOL_R_STREAM_FUTURE_REF; return 3; }
    if (s->st[id] == ST_DROPPED) { *why = HOL_R_STREAM_DROPPED_REF; return 3; }
    if (s->st[id] == ST_FAILED && kind <= 'Z') return 2; /* dropping a FAILED id is fine */
    return 0;
}

/* Checks every id operand in encoding order before anything executes: the
 * first undefined or dropped id makes the record malformed; otherwise any
 * FAILED operand makes it a dependent failure.  The OCaml oracle applies
 * the same rule (oracle/hol_oracle.ml, validate). */
static int validate(const HolReplay *r, const Rec *rc, HolReason *why) {
    unsigned ai = 0;
    bool failed = false;
    for (const char *l = rc->info->layout; *l; l++) {
        int v;
        if (*l == '*' || *l == '%') {
            unsigned width = *l == '*' ? 1 : 2;
            for (uint64_t i = 0; i < rc->nv; i++) {
                v = check_id(r, l[1 + i % width], rc->v[i], why);
                if (v == 3) return 3;
                failed |= v == 2;
            }
            l += width;
            continue;
        }
        if (*l == 'b') continue;
        v = check_id(r, *l, rc->a[ai++], why);
        if (v == 3) return 3;
        failed |= v == 2;
    }
    return failed ? 2 : 0;
}

static bool space_reserve(Space *s, uint64_t extra) {
    if (s->n + extra <= s->cap) return true;
    uint64_t ncap = s->cap ? s->cap * 2 : 1024;
    while (ncap < s->n + extra) ncap *= 2;
    void **no = (void **)realloc(s->obj, ncap * sizeof(void *));
    if (!no) return false;
    s->obj = no;
    uint8_t *ns = (uint8_t *)realloc(s->st, ncap);
    if (!ns) return false;
    s->st = ns;
    s->cap = ncap;
    return true;
}

static void space_put(Space *s, void *obj, uint8_t st) {
    s->obj[s->n] = obj;
    s->st[s->n] = st;
    s->n++;
}

static Space *out_space(HolReplay *r, const Rec *rc) {
    switch (rc->info->out) {
    case 'Y': return &r->ty;
    case 'T': return &r->tm;
    case 'H': return &r->th;
    default: return NULL;
    }
}

static bool grow(void **p, size_t *cap, size_t n, size_t elem) {
    if (n < *cap) return true;
    size_t ncap = *cap ? *cap * 2 : 256;
    void *np = realloc(*p, ncap * elem);
    if (!np) return false;
    *p = np;
    *cap = ncap;
    return true;
}

/* Reserves every table slot the commit may need, so that commit cannot fail. */
static bool reserve_outputs(HolReplay *r, const Rec *rc) {
    Space *s = out_space(r, rc);
    if (s && !space_reserve(s, rc->info->nout)) return false;
    switch (rc->op) {
    case HOLR_NAME: return grow((void **)&r->names, &r->capnames, r->nnames, sizeof(HolName));
    case HOLR_EMIT: return grow((void **)&r->emit, &r->capemit, r->nemit, sizeof(uint64_t));
    case HOLR_LABEL: return grow((void **)&r->labels, &r->caplabels, r->nlabels, sizeof(Label));
    case HOLR_NEW_AXIOM: return grow((void **)&r->axioms, &r->capaxioms, r->naxioms, sizeof(uint64_t));
    default: return true;
    }
}

/* ------------------------------------------------------------------------ */
/* Execution                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    unsigned nout;
    void *out[2];
    HolDecl *env;  /* new environment, if any */
    HolName name;  /* NAME output / LABEL name */
} Exec;

/* Digests for the observer: FNV-1a of the canonical text.  They are
 * diagnostics computed after commit, so they run without fuel (s.k unset);
 * their cost is the size of the text (see hol_replay_set_observer). */
static uint64_t digest_obj(HolKernel *k, char space, void *o) {
    HolStr s = {0};
    s.hash_only = true;
    s.fnv = HOL_FNV_OFFSET;
    if (space == 'Y') hk_print_type(k, &s, (HolType *)o);
    else if (space == 'T') hk_print_term(k, &s, (HolTerm *)o);
    else if (space == 'H') hk_print_thm(k, &s, (HolThm *)o);
    return s.fnv;
}

static int sequent_matches(HolKernel *k, HolThm *th, HolTerm **hyps, uint64_t nh, HolTerm *concl,
                           bool alpha) {
    if (!alpha) {
        if (th->concl != concl || th->nhyps != nh) return 0;
        for (uint64_t i = 0; i < nh; i++)
            if (th->hyps[i] != hyps[i]) return 0;
        return 1;
    }
    int c;
    if (hk_alphaorder(k, th->concl, concl, &c) < 0) return -1;
    if (c) return 0;
    /* hypotheses as sets modulo alpha, both directions */
    for (int dir = 0; dir < 2; dir++) {
        uint64_t na = dir ? nh : th->nhyps, nb = dir ? th->nhyps : nh;
        for (uint64_t i = 0; i < na; i++) {
            HolTerm *x = dir ? hyps[i] : th->hyps[i];
            bool found = false;
            for (uint64_t j = 0; j < nb && !found; j++) {
                HolTerm *y = dir ? th->hyps[j] : hyps[j];
                if (hk_alphaorder(k, x, y, &c) < 0) return -1;
                found = c == 0;
            }
            if (!found) return 0;
        }
    }
    return 1;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* EXPECT_AXIOMS h [a...]: the listed ids, as a set without repetition, are
 * exactly the NEW_AXIOM outputs whose axioms h depends on.  Returns 1 if so,
 * 0 if not, -1 on allocation failure. */
static int axioms_match(HolReplay *r, HolThm *th, const uint64_t *ids, uint64_t n) {
    uint32_t nd = th->deps ? th->deps->n : 0;
    if (n != nd) return 0;
    uint64_t *s = (uint64_t *)malloc((n ? n : 1) * sizeof(uint64_t));
    if (!s) return -1;
    if (n) memcpy(s, ids, n * sizeof(uint64_t));
    qsort(s, n, sizeof(uint64_t), cmp_u64);
    int ok = 1;
    for (uint64_t i = 0; ok && i < n; i++) {
        if (i > 0 && s[i] == s[i - 1]) { ok = 0; break; }
        /* s[i] must be a NEW_AXIOM output (r->axioms is increasing) */
        uint64_t lo = 0, hi = r->naxioms;
        while (lo < hi) {
            uint64_t mid = lo + (hi - lo) / 2;
            if (r->axioms[mid] < s[i]) lo = mid + 1; else hi = mid;
        }
        if (lo == r->naxioms || r->axioms[lo] != s[i]) { ok = 0; break; }
        /* whose axiom is one of th's dependencies (an axiom theorem's
         * dependency set is the singleton of its own declaration) */
        HolDecl *ax = ((HolThm *)r->th.obj[s[i]])->deps->ax[0];
        bool found = false;
        for (uint32_t j = 0; j < nd && !found; j++) found = th->deps->ax[j] == ax;
        ok = found;
    }
    free(s);
    return ok;
}

/* Runs a validated record whose operands are all usable.  Returns:
 * 0 ok, 1 logical refusal, 3 malformed, 4 resource failure, 5 check failed. */
static int execute(HolReplay *r, const Rec *rc, Exec *x, HolReason *why) {
    HolKernel *k = r->k;
    memset(x, 0, sizeof *x);
    const uint64_t *a = rc->a;
#define NM(i) (r->names[a[i]])
#define TY(i) ((HolType *)r->ty.obj[a[i]])
#define TM(i) ((HolTerm *)r->tm.obj[a[i]])
#define TH(i) ((HolThm *)r->th.obj[a[i]])
    x->nout = rc->info->out == 'N' ? 0 : rc->info->nout;
    switch (rc->op) {
    case HOLR_NAME: {
        HolName n = hk_name_intern(k, (const char *)rc->bytes, rc->nbytes);
        if (!n) return 4;
        x->name = n;
        return 0;
    }
    case HOLR_TYVAR:
        x->out[0] = hk_ty_var(k, NM(0));
        break;
    case HOLR_TYAPP: {
        if (rc->nv > UINT32_MAX) { *why = HOL_R_STREAM_LIMIT; return 3; }
        HolType **args = (HolType **)malloc((rc->nv ? rc->nv : 1) * sizeof(HolType *));
        if (!args) return 4;
        for (uint64_t i = 0; i < rc->nv; i++) args[i] = (HolType *)r->ty.obj[rc->v[i]];
        x->out[0] = hk_mk_type_in(k, r->env, NM(0), args, (uint32_t)rc->nv);
        free(args);
        break;
    }
    case HOLR_VAR: x->out[0] = hk_tm_var(k, NM(0), TY(1)); break;
    case HOLR_CONST: x->out[0] = hk_mk_const_at(k, r->env, NM(0), TY(1)); break;
    case HOLR_COMB: x->out[0] = hk_tm_comb(k, TM(0), TM(1)); break;
    case HOLR_ABS: x->out[0] = hk_tm_abs(k, TM(0), TM(1)); break;
    case HOLR_REFL: x->out[0] = hk_REFL(k, TM(0)); break;
    case HOLR_BETA: x->out[0] = hk_BETA(k, TM(0)); break;
    case HOLR_ASSUME: x->out[0] = hk_ASSUME(k, TM(0)); break;
    case HOLR_TRANS: x->out[0] = hk_TRANS(k, TH(0), TH(1)); break;
    case HOLR_MK_COMB: x->out[0] = hk_MK_COMB(k, TH(0), TH(1)); break;
    case HOLR_EQ_MP: x->out[0] = hk_EQ_MP(k, TH(0), TH(1)); break;
    case HOLR_DEDUCT: x->out[0] = hk_DEDUCT_ANTISYM_RULE(k, TH(0), TH(1)); break;
    case HOLR_ABS_RULE: x->out[0] = hk_ABS(k, TM(0), TH(1)); break;
    case HOLR_INST_TYPE:
    case HOLR_INST: {
        bool types = rc->op == HOLR_INST_TYPE;
        uint64_t np = rc->nv / 2;
        void *pairs = malloc((np ? np : 1) * (types ? sizeof(HolTyPair) : sizeof(HolTmPair)));
        if (!pairs) return 4;
        for (uint64_t i = 0; i < np; i++) {
            if (types) {
                HolTyPair *p = (HolTyPair *)pairs + i;
                p->repl = (HolType *)r->ty.obj[rc->v[2 * i]];
                p->key = (HolType *)r->ty.obj[rc->v[2 * i + 1]];
            } else {
                HolTmPair *p = (HolTmPair *)pairs + i;
                p->t = (HolTerm *)r->tm.obj[rc->v[2 * i]];
                p->x = (HolTerm *)r->tm.obj[rc->v[2 * i + 1]];
            }
        }
        x->out[0] = types ? hk_INST_TYPE(k, (HolTyPair *)pairs, np, TH(0))
                          : hk_INST(k, (HolTmPair *)pairs, np, TH(0));
        free(pairs);
        break;
    }
    case HOLR_NEW_TYPE:
        if (a[1] > UINT32_MAX) { *why = HOL_R_STREAM_LIMIT; return 3; }
        x->env = hk_env_new_type(k, r->env, NM(0), (uint32_t)a[1]);
        break;
    case HOLR_NEW_CONSTANT:
        x->env = hk_env_new_constant(k, r->env, NM(0), TY(1));
        break;
    case HOLR_NEW_AXIOM:
    case HOLR_NEW_DEFINITION: {
        HolThm *th = NULL;
        x->env = rc->op == HOLR_NEW_AXIOM ? hk_env_new_axiom(k, r->env, TM(0), &th)
                                          : hk_env_new_definition(k, r->env, TM(0), &th);
        x->out[0] = x->env ? th : NULL;
        break;
    }
    case HOLR_NEW_TYPEDEF: {
        HolThm *t1 = NULL, *t2 = NULL;
        x->env = hk_env_new_type_definition(k, r->env, NM(0), NM(1), NM(2), TH(3), &t1, &t2);
        x->out[0] = x->env ? t1 : NULL;
        x->out[1] = x->env ? t2 : NULL;
        break;
    }
    case HOLR_POLICY:
        if (a[0] > HOL_AXIOMS_SEALED) { *why = HOL_R_STREAM_VALUE; return 3; }
        if ((int)a[0] <= r->env->policy) return 0; /* already at least as tight */
        x->env = hk_env_set_policy(k, r->env, (int)a[0]);
        break;
    case HOLR_EXPECT:
    case HOLR_EXPECT_ALPHA: {
        HolTerm **hyps = (HolTerm **)malloc((rc->nv ? rc->nv : 1) * sizeof(HolTerm *));
        if (!hyps) return 4;
        for (uint64_t i = 0; i < rc->nv; i++) hyps[i] = (HolTerm *)r->tm.obj[rc->v[i]];
        int m = sequent_matches(k, TH(0), hyps, rc->nv, TM(1), rc->op == HOLR_EXPECT_ALPHA);
        free(hyps);
        if (m < 0) return 4;
        if (!m) { *why = HOL_R_STREAM_EXPECT; return 5; }
        return 0;
    }
    case HOLR_EXPECT_AXIOMS: {
        int m = axioms_match(r, TH(0), rc->v, rc->nv);
        if (m < 0) return 4;
        if (!m) { *why = HOL_R_STREAM_EXPECT; return 5; }
        return 0;
    }
    case HOLR_EXPECT_REFUSED:
        if (!r->last_refused) { *why = HOL_R_STREAM_EXPECT; return 5; }
        return 0;
    case HOLR_LABEL:
        x->name = NM(1);
        return 0;
    case HOLR_EMIT:
    case HOLR_DROP_TYPE:
    case HOLR_DROP_TERM:
    case HOLR_DROP_THM:
        return 0;
    default:
        *why = HOL_R_STREAM_OPCODE;
        return 3;
    }
#undef NM
#undef TY
#undef TM
#undef TH
    /* A kernel operation ran: classify its outcome. */
    bool produced = x->nout ? x->out[0] != NULL : x->env != NULL;
    if (produced) return 0;
    *why = k->reason;
    if (k->status == HOL_REFUSED || k->status == HOL_POLICY || k->status == HOL_FOREIGN) return 1;
    if (k->status == HOL_MALFORMED || k->status == HOL_STALE) return 3;
    return 4; /* NOMEM, LIMIT, INTERRUPTED */
}

static void report(HolReplay *r, const Rec *rc, HolReplayOutcome out, HolStatus s, HolReason why,
                   char space, uint64_t first, uint32_t count) {
    if (!r->obs) return;
    if (r->sample > 1 && r->records % r->sample &&
        (out == HOLR_OUT_OK || (out == HOLR_OUT_CHECK && s == HOL_OK)))
        return; /* failures are always observed */
    HolReplayRecord rec;
    memset(&rec, 0, sizeof rec);
    rec.index = r->records;
    rec.opcode = rc->op;
    rec.outcome = out;
    rec.status = s;
    rec.reason = why;
    rec.space = space;
    rec.first_id = first;
    rec.count = count;
    if (out == HOLR_OUT_OK && (space == 'Y' || space == 'T' || space == 'H')) {
        Space *sp = space == 'Y' ? &r->ty : space == 'T' ? &r->tm : &r->th;
        for (uint32_t i = 0; i < count && i < 2; i++) {
            rec.digest[i] = digest_obj(r->k, space, sp->obj[first + i]);
            if (space == 'T' || space == 'H') {
                HolStr a = {0};
                a.hash_only = true;
                a.fnv = HOL_FNV_OFFSET;
                if (space == 'T') hk_print_term_alpha(r->k, &a, (HolTerm *)sp->obj[first + i]);
                else hk_print_thm_alpha(r->k, &a, (HolThm *)sp->obj[first + i]);
                rec.alpha[i] = a.fnv;
            }
        }
    }
    r->obs(r->obs_ctx, &rec);
}

static bool parse_header(HolReplay *r) {
    Cur c = {r->buf, r->buf + r->len, r->final, 0, HOL_R_NONE};
    const uint8_t *m = cur_bytes(&c, 8);
    if (m && memcmp(m, HOLR_MAGIC, 8) != 0) cur_bad(&c, HOL_R_STREAM_MAGIC);
    const uint8_t *sha = cur_bytes(&c, 32);
    if (sha && memcmp(sha, holr_kernel_sha, 32) != 0) cur_bad(&c, HOL_R_STREAM_HEADER);
    uint64_t version = cur_uv(&c);
    if (!c.err && version != HOLR_VERSION) cur_bad(&c, HOL_R_STREAM_VERSION);
    uint64_t profile = cur_uv(&c);
    if (!c.err && profile != 0) cur_bad(&c, HOL_R_STREAM_HEADER);
    uint64_t policy = cur_uv(&c);
    if (!c.err && policy > HOL_AXIOMS_SEALED) cur_bad(&c, HOL_R_STREAM_HEADER);
    uint64_t flags = cur_uv(&c);
    if (!c.err && (flags & ~(uint64_t)HOLR_FLAG_PROBE)) cur_bad(&c, HOL_R_STREAM_HEADER);
    if (c.err == 1) { r->state = HOL_REPLAY_NEED_INPUT; return false; }
    if (c.err == 2) { stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, c.why); return false; }
    r->flags = (uint32_t)flags;
    int pol = (int)policy > r->host_policy ? (int)policy : r->host_policy;
    if (pol > HOL_AXIOMS_OPEN) {
        hk_begin_call(r->k);
        HolDecl *d = hk_env_set_policy(r->k, r->env, pol);
        if (!d) { stop(r, HOL_REPLAY_RESOURCE, r->k->status, r->k->reason); return false; }
        r->env = d;
    }
    size_t hlen = (size_t)(c.p - r->buf);
    r->fnv = hk_fnv1a(r->buf, hlen, HOL_FNV_OFFSET);
    r->pos = hlen;
    r->header_done = true;
    return true;
}

HolReplayState hol_replay_resume(HolReplay *r, uint64_t budget) {
    if (!r) return HOL_REPLAY_MALFORMED;
    if (r->state == HOL_REPLAY_MALFORMED || r->state == HOL_REPLAY_REFUSED) return r->state;
    if (r->ended) return r->state;
    if (r->state == HOL_REPLAY_RESOURCE) r->state = HOL_REPLAY_RUNNING;
    if (!r->header_done && !parse_header(r)) return r->state;
    HolKernel *k = r->k;
    Rec rc;
    memset(&rc, 0, sizeof rc);
    uint64_t done = 0;
    HolReplayState result = HOL_REPLAY_RUNNING;
    for (;;) {
        if (budget && done >= budget) { result = HOL_REPLAY_RUNNING; break; }
        if (r->pos == r->len && !r->final) { result = HOL_REPLAY_NEED_INPUT; break; }
        if (r->pos == r->len && r->final) {
            result = stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_MISSING_END);
            break;
        }
        hk_begin_call(k);
        hk_maybe_gc(k);
        Cur c = {r->buf + r->pos, r->buf + r->len, r->final, 0, HOL_R_NONE};
        int d = decode(r, &c, &rc);
        if (d == 1) { result = HOL_REPLAY_NEED_INPUT; break; }
        if (d == 2) { result = stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, c.why); break; }
        if (d == 3) { result = stop(r, HOL_REPLAY_RESOURCE, HOL_NOMEM, HOL_R_NOMEM); break; }
        size_t rec_len = (size_t)(c.p - (r->buf + r->pos));
        if (rc.op == HOLR_END) {
            if (rc.a[0] != r->records) {
                result = stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_COUNT);
                break;
            }
            if (rc.a[1] != r->fnv) {
                result = stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_CHECKSUM);
                break;
            }
            r->pos += rec_len;
            r->ended = true;
            if (r->final && r->pos != r->len) {
                result = stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, HOL_R_STREAM_TRAILING);
                break;
            }
            result = stop(r, HOL_REPLAY_ACCEPTED, HOL_OK, HOL_R_NONE);
            break;
        }
        if (r->records >= r->lim.max_records) {
            result = stop(r, HOL_REPLAY_MALFORMED, HOL_LIMIT, HOL_R_STREAM_LIMIT);
            break;
        }
        Space *outsp = out_space(r, &rc);
        uint64_t need = rc.info->nout;
        if ((outsp && outsp->n + need > r->lim.max_ids) ||
            (rc.op == HOLR_NAME && r->nnames + 1 > r->lim.max_ids)) {
            result = stop(r, HOL_REPLAY_MALFORMED, HOL_LIMIT, HOL_R_STREAM_LIMIT);
            break;
        }
        if (!reserve_outputs(r, &rc)) { result = stop(r, HOL_REPLAY_RESOURCE, HOL_NOMEM, HOL_R_NOMEM); break; }
        Exec x;
        memset(&x, 0, sizeof x);
        HolReason why = HOL_R_NONE;
        int e = validate(r, &rc, &why);
        if (e == 0) e = execute(r, &rc, &x, &why);
        if (e == 3) { result = stop(r, HOL_REPLAY_MALFORMED, HOL_MALFORMED, why); break; }
        if (e == 4) {
            HolStatus s = k->status == HOL_OK ? HOL_NOMEM : k->status;
            result = stop(r, HOL_REPLAY_RESOURCE, s, why ? why : HOL_R_NOMEM);
            break;
        }
        bool probe = (r->flags & HOLR_FLAG_PROBE) != 0;
        if ((e == 1 || e == 5) && !probe) {
            result = stop(r, HOL_REPLAY_REFUSED, e == 1 ? (k->status ? k->status : HOL_REFUSED) : HOL_REFUSED, why);
            break;
        }
        /* Commit: table writes only (capacity was reserved above). */
        bool is_check = rc.op == HOLR_EXPECT || rc.op == HOLR_EXPECT_ALPHA ||
                        rc.op == HOLR_EXPECT_REFUSED || rc.op == HOLR_EXPECT_AXIOMS;
        if (is_check && (e == 0 || e == 5)) r->checks++;
        uint64_t first = outsp ? outsp->n : (rc.op == HOLR_NAME ? r->nnames : 0);
        HolReplayOutcome outcome = HOLR_OUT_OK;
        HolStatus st = HOL_OK;
        if (rc.op == HOLR_NAME) {
            r->names[r->nnames++] = x.name;
        } else if (e == 0) {
            if (outsp)
                for (unsigned i = 0; i < need; i++) space_put(outsp, x.out[i], ST_OK);
            if (x.env) r->env = x.env;
            if (rc.op == HOLR_NEW_AXIOM) r->axioms[r->naxioms++] = first;
            if (rc.op == HOLR_EMIT) r->emit[r->nemit++] = rc.a[0];
            if (rc.op == HOLR_LABEL) {
                r->labels[r->nlabels].thm = rc.a[0];
                r->labels[r->nlabels].name = x.name;
                r->nlabels++;
            }
            if (rc.op == HOLR_DROP_TYPE) r->ty.st[rc.a[0]] = ST_DROPPED;
            if (rc.op == HOLR_DROP_TERM) r->tm.st[rc.a[0]] = ST_DROPPED;
            if (rc.op == HOLR_DROP_THM) r->th.st[rc.a[0]] = ST_DROPPED;
            if (is_check) outcome = HOLR_OUT_CHECK;
        } else {
            if (outsp)
                for (unsigned i = 0; i < need; i++) space_put(outsp, NULL, ST_FAILED);
            outcome = e == 2 ? HOLR_OUT_DEPFAIL : e == 5 ? HOLR_OUT_CHECK : HOLR_OUT_REFUSED;
            st = e == 5 ? HOL_REFUSED : (e == 1 ? k->status : HOL_OK);
            r->failed++;
        }
        r->last_refused = e == 1 || e == 2;
        report(r, &rc, outcome, st, why, rc.info->out, first, (uint32_t)need);
        r->fnv = hk_fnv1a(r->buf + r->pos, rec_len, r->fnv);
        r->pos += rec_len;
        r->records++;
        done++;
    }
    free(rc.v);
    if (result == HOL_REPLAY_RUNNING || result == HOL_REPLAY_NEED_INPUT) r->state = result;
    return r->state;
}

HolStatus hol_replay_thm(HolReplay *r, uint64_t h, HolRef *out) {
    if (!r || !out) return HOL_MALFORMED;
    hk_begin_call(r->k);
    if (h >= r->th.n || r->th.st[h] != ST_OK) return HOL_STALE;
    HolRef ref = hk_ref_new(r->k, HOL_KIND_THM, r->th.obj[h], ++r->k->serial);
    if (!ref) return r->k->status;
    *out = ref;
    return HOL_OK;
}

HolStatus hol_replay_env(HolReplay *r, HolRef *out) {
    if (!r || !out) return HOL_MALFORMED;
    hk_begin_call(r->k);
    HolRef ref = hk_ref_new(r->k, HOL_KIND_ENV, r->env, r->env->serial);
    if (!ref) return r->k->status;
    *out = ref;
    return HOL_OK;
}

HolStatus hol_replay_next(HolReplay *r, HolRef *out, uint64_t *ordinal) {
    if (!r || !out) return HOL_MALFORMED;
    if (r->delivered >= r->nemit) return HOL_STALE;
    uint64_t h = r->emit[r->delivered];
    HolStatus s = hol_replay_thm(r, h, out);
    if (s != HOL_OK) return s;
    if (ordinal) *ordinal = r->delivered;
    r->delivered++;
    return HOL_OK;
}

size_t hol_replay_nlabels(const HolReplay *r) { return r ? r->nlabels : 0; }

bool hol_replay_label(const HolReplay *r, size_t i, uint64_t *thm, const char **name, size_t *len) {
    if (!r || i >= r->nlabels) return false;
    if (thm) *thm = r->labels[i].thm;
    const HolNameRec *nr = &r->k->names[r->labels[i].name];
    if (name) *name = nr->bytes;
    if (len) *len = nr->len;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Structural scan                                                           */
/* ------------------------------------------------------------------------ */

HolReplayState hol_stream_scan(const uint8_t *bytes, size_t len, HolScanHeader *hdr, HolScanFn fn,
                               void *ctx, HolReason *why) {
    HolReason dummy;
    if (!why) why = &dummy;
    *why = HOL_R_NONE;
    /* the header, through the replay's own parser */
    HolReplay r;
    memset(&r, 0, sizeof r);
    limits_default(&r.lim, NULL);
    r.buf = (uint8_t *)bytes;
    r.len = len;
    r.final = true;
    Cur c = {bytes, bytes + len, true, 0, HOL_R_NONE};
    const uint8_t *m = cur_bytes(&c, 8);
    if (m && memcmp(m, HOLR_MAGIC, 8) != 0) cur_bad(&c, HOL_R_STREAM_MAGIC);
    const uint8_t *sha = cur_bytes(&c, 32);
    if (sha && memcmp(sha, holr_kernel_sha, 32) != 0) cur_bad(&c, HOL_R_STREAM_HEADER);
    uint64_t version = cur_uv(&c);
    if (!c.err && version != HOLR_VERSION) cur_bad(&c, HOL_R_STREAM_VERSION);
    uint64_t profile = cur_uv(&c);
    if (!c.err && profile != 0) cur_bad(&c, HOL_R_STREAM_HEADER);
    uint64_t policy = cur_uv(&c);
    if (!c.err && policy > HOL_AXIOMS_SEALED) cur_bad(&c, HOL_R_STREAM_HEADER);
    uint64_t flags = cur_uv(&c);
    if (!c.err && (flags & ~(uint64_t)HOLR_FLAG_PROBE)) cur_bad(&c, HOL_R_STREAM_HEADER);
    if (c.err) { *why = c.why; return HOL_REPLAY_MALFORMED; }
    if (hdr) {
        hdr->flags = (uint32_t)flags;
        hdr->policy = (HolAxiomPolicy)policy;
        hdr->header_len = (size_t)(c.p - bytes);
    }
    uint64_t fnv = hk_fnv1a(bytes, (size_t)(c.p - bytes), HOL_FNV_OFFSET), index = 0;
    Rec rc;
    memset(&rc, 0, sizeof rc);
    HolReplayState st = HOL_REPLAY_MALFORMED;
    for (;;) {
        const uint8_t *start = c.p;
        if (c.p == c.end) { *why = HOL_R_STREAM_MISSING_END; break; }
        int d = decode(&r, &c, &rc);
        if (d == 3) { *why = HOL_R_NOMEM; st = HOL_REPLAY_RESOURCE; break; }
        if (d) { *why = c.why ? c.why : HOL_R_STREAM_TRUNCATED; break; }
        if (rc.op == HOLR_END) {
            if (rc.a[0] != index) { *why = HOL_R_STREAM_COUNT; break; }
            if (rc.a[1] != fnv) { *why = HOL_R_STREAM_CHECKSUM; break; }
            if (c.p != c.end) { *why = HOL_R_STREAM_TRAILING; break; }
            st = HOL_REPLAY_ACCEPTED;
            break;
        }
        HolScanRecord sr;
        sr.index = index;
        sr.opcode = rc.op;
        sr.layout = rc.info->layout;
        sr.out = rc.info->out;
        sr.nout = rc.info->nout;
        sr.fixed = rc.a;
        sr.nfixed = rc.na;
        sr.list = rc.v;
        sr.nlist = rc.nv;
        sr.bytes = rc.bytes;
        sr.nbytes = rc.nbytes;
        sr.raw = start;
        sr.raw_len = (size_t)(c.p - start);
        fnv = hk_fnv1a(start, sr.raw_len, fnv);
        index++;
        if (fn && !fn(ctx, &sr)) { st = HOL_REPLAY_RUNNING; break; }
    }
    free(rc.v);
    return st;
}

/* ------------------------------------------------------------------------ */
/* Writer                                                                    */
/* ------------------------------------------------------------------------ */

struct HolWriter {
    uint8_t *buf;
    size_t len, cap;
    uint64_t records;
    bool oom, finished;
};

static bool w_put(HolWriter *w, const void *p, size_t n) {
    if (w->oom || w->finished) return false;
    if (w->len + n > w->cap) {
        size_t ncap = w->cap ? w->cap : 4096;
        while (ncap < w->len + n) ncap *= 2;
        uint8_t *nb = (uint8_t *)realloc(w->buf, ncap);
        if (!nb) { w->oom = true; return false; }
        w->buf = nb;
        w->cap = ncap;
    }
    memcpy(w->buf + w->len, p, n);
    w->len += n;
    return true;
}

bool hol_writer_uv(HolWriter *w, uint64_t v) {
    uint8_t b[10];
    size_t n = 0;
    do {
        uint8_t x = v & 0x7f;
        v >>= 7;
        if (v) x |= 0x80;
        b[n++] = x;
    } while (v);
    return w_put(w, b, n);
}

bool hol_writer_bytes(HolWriter *w, const void *p, size_t n) { return w_put(w, p, n); }

bool hol_writer_record(HolWriter *w, const void *p, size_t n) {
    if (!w_put(w, p, n)) return false;
    w->records++;
    return true;
}

bool hol_writer_op(HolWriter *w, uint8_t opcode) {
    if (!w_put(w, &opcode, 1)) return false;
    w->records++;
    return true;
}

HolWriter *hol_writer_new(uint32_t flags, HolAxiomPolicy policy) {
    HolWriter *w = (HolWriter *)calloc(1, sizeof *w);
    if (!w) return NULL;
    w_put(w, HOLR_MAGIC, 8);
    w_put(w, holr_kernel_sha, 32);
    hol_writer_uv(w, HOLR_VERSION);
    hol_writer_uv(w, 0);
    hol_writer_uv(w, (uint64_t)policy);
    hol_writer_uv(w, flags);
    if (w->oom) { free(w->buf); free(w); return NULL; }
    return w;
}

void hol_writer_free(HolWriter *w) {
    if (!w) return;
    free(w->buf);
    free(w);
}

uint64_t hol_writer_records(const HolWriter *w) { return w->records; }

const uint8_t *hol_writer_data(const HolWriter *w, size_t *len) {
    *len = w->len;
    return w->buf;
}

const uint8_t *hol_writer_finish(HolWriter *w, size_t *len) {
    if (!w->finished) {
        uint64_t f = hk_fnv1a(w->buf, w->len, HOL_FNV_OFFSET);
        uint8_t op = HOLR_END;
        w_put(w, &op, 1);
        hol_writer_uv(w, w->records);
        uint8_t sum[8];
        for (int i = 0; i < 8; i++) sum[i] = (uint8_t)(f >> (8 * i));
        w_put(w, sum, 8);
        w->finished = true;
    }
    if (w->oom) return NULL;
    *len = w->len;
    return w->buf;
}
