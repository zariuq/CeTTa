#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_atom.h"

#include "symbol.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* ── constructor table (NativeSyntaxWire) ─────────────────────────────── */

enum { SORT_TP = 0, SORT_TM = 1, SORT_PF = 2 };

typedef enum {
    C_TP_VAR, C_TP_PROP, C_TP_BASE, C_TP_ARR, C_TP_ALL,
    C_TM_DB, C_TM_NAMED, C_TM_PRIM, C_TM_APP, C_TM_LAM, C_TM_IMP, C_TM_ALL,
    C_TM_TAPP, C_TM_TLAM, C_TM_TALL,
    C_PF_GPA, C_PF_HYP, C_PF_KNOWN, C_PF_TMAP, C_PF_PFAP, C_PF_PLAM,
    C_PF_TLAM, C_PF_TPAP, C_PF_TPLAM,
    C_COUNT
} Ctor;

enum { LEAF_NONE = 0, LEAF_NATURAL, LEAF_STRING, LEAF_EMPTY };

typedef struct {
    const char *name;
    uint8_t sort;
    uint8_t arity;
    uint8_t leaf;
    uint8_t child[2];
} CtorInfo;

static const CtorInfo CTORS[C_COUNT] = {
    [C_TP_VAR] = {"Tp.var", SORT_TP, 1, LEAF_NATURAL, {0, 0}},
    [C_TP_PROP] = {"Tp.prop", SORT_TP, 0, LEAF_EMPTY, {0, 0}},
    [C_TP_BASE] = {"Tp.base", SORT_TP, 1, LEAF_NATURAL, {0, 0}},
    [C_TP_ARR] = {"Tp.arr", SORT_TP, 2, LEAF_NONE, {SORT_TP, SORT_TP}},
    [C_TP_ALL] = {"Tp.all", SORT_TP, 1, LEAF_NONE, {SORT_TP, 0}},
    [C_TM_DB] = {"Tm.db", SORT_TM, 1, LEAF_NATURAL, {0, 0}},
    [C_TM_NAMED] = {"Tm.named", SORT_TM, 1, LEAF_STRING, {0, 0}},
    [C_TM_PRIM] = {"Tm.prim", SORT_TM, 1, LEAF_NATURAL, {0, 0}},
    [C_TM_APP] = {"Tm.app", SORT_TM, 2, LEAF_NONE, {SORT_TM, SORT_TM}},
    [C_TM_LAM] = {"Tm.lam", SORT_TM, 2, LEAF_NONE, {SORT_TP, SORT_TM}},
    [C_TM_IMP] = {"Tm.imp", SORT_TM, 2, LEAF_NONE, {SORT_TM, SORT_TM}},
    [C_TM_ALL] = {"Tm.all", SORT_TM, 2, LEAF_NONE, {SORT_TP, SORT_TM}},
    [C_TM_TAPP] = {"Tm.typeApp", SORT_TM, 2, LEAF_NONE, {SORT_TM, SORT_TP}},
    [C_TM_TLAM] = {"Tm.typeLam", SORT_TM, 1, LEAF_NONE, {SORT_TM, 0}},
    [C_TM_TALL] = {"Tm.typeAll", SORT_TM, 1, LEAF_NONE, {SORT_TM, 0}},
    [C_PF_GPA] = {"Pf.gpa", SORT_PF, 1, LEAF_STRING, {0, 0}},
    [C_PF_HYP] = {"Pf.hyp", SORT_PF, 1, LEAF_NATURAL, {0, 0}},
    [C_PF_KNOWN] = {"Pf.known", SORT_PF, 1, LEAF_STRING, {0, 0}},
    [C_PF_TMAP] = {"Pf.termApp", SORT_PF, 2, LEAF_NONE, {SORT_PF, SORT_TM}},
    [C_PF_PFAP] = {"Pf.proofApp", SORT_PF, 2, LEAF_NONE, {SORT_PF, SORT_PF}},
    [C_PF_PLAM] = {"Pf.proofLam", SORT_PF, 2, LEAF_NONE, {SORT_TM, SORT_PF}},
    [C_PF_TLAM] = {"Pf.termLam", SORT_PF, 2, LEAF_NONE, {SORT_TP, SORT_PF}},
    [C_PF_TPAP] = {"Pf.typeApp", SORT_PF, 2, LEAF_NONE, {SORT_PF, SORT_TP}},
    [C_PF_TPLAM] = {"Pf.typeLam", SORT_PF, 1, LEAF_NONE, {SORT_PF, 0}},
};

enum {
    S_CONTEXT, S_TYPES, S_HYPOTHESES, S_JUDGMENT, S_ROW, S_PARENTS,
    S_W_HYP, S_W_KNOWN, S_W_TERM_APP, S_W_PROOF_APP, S_W_TERM_LAM,
    S_W_PROOF_LAM, S_W_TYPE_APP, S_W_TYPE_LAM,
    S_COUNT
};

static const char *const SYMBOL_TEXT[S_COUNT] = {
    [S_CONTEXT] = "Megalodon.context",
    [S_TYPES] = "types",
    [S_HYPOTHESES] = "hypotheses",
    [S_JUDGMENT] = "Megalodon.judgment",
    [S_ROW] = "Megalodon.row",
    [S_PARENTS] = "parents",
    [S_W_HYP] = "Megalodon.hyp",
    [S_W_KNOWN] = "Megalodon.known",
    [S_W_TERM_APP] = "Megalodon.termApp",
    [S_W_PROOF_APP] = "Megalodon.proofApp",
    [S_W_TERM_LAM] = "Megalodon.termLam",
    [S_W_PROOF_LAM] = "Megalodon.proofLam",
    [S_W_TYPE_APP] = "Megalodon.typeApp",
    [S_W_TYPE_LAM] = "Megalodon.typeLam",
};

static pthread_once_t g_symbols_once = PTHREAD_ONCE_INIT;
static SymbolId g_ctor_sym[C_COUNT];
static SymbolId g_sym[S_COUNT];

static void symbols_init(void) {
    for (int c = 0; c < C_COUNT; ++c)
        g_ctor_sym[c] = symbol_intern_cstr(g_symbols, CTORS[c].name);
    for (int k = 0; k < S_COUNT; ++k)
        g_sym[k] = symbol_intern_cstr(g_symbols, SYMBOL_TEXT[k]);
}

static inline void symbols_ready(void) {
    pthread_once(&g_symbols_once, symbols_init);
}

static inline bool is_sym(const Atom *a, SymbolId id) {
    return a && a->kind == ATOM_SYMBOL && a->sym_id == id;
}

static inline bool headed(const Atom *a, int symbol, uint32_t len) {
    return a && a->kind == ATOM_EXPR && a->expr.len == len &&
           is_sym(a->expr.elems[0], g_sym[symbol]);
}

bool mg_atom_natural(const Atom *a, uint64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT ||
        a->ground.ival < 0)
        return false;
    *out = (uint64_t)a->ground.ival;
    return true;
}

static bool atom_index(const Atom *a, uint32_t *out) {
    uint64_t v;
    if (!mg_atom_natural(a, &v) || v > UINT32_MAX - 1u)
        return false;
    *out = (uint32_t)v;
    return true;
}

const char *mg_atom_text(const Atom *a, size_t *len) {
    if (a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING) {
        *len = a->ground.slen;
        return a->ground.sval;
    }
    if (a && a->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr((Atom *)a);
        if (name) {
            *len = strlen(name);
            return name;
        }
    }
    return NULL;
}

static bool atom_string_name(MgStore *s, const Atom *a, uint32_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_STRING ||
        a->ground.slen == 0u)
        return false;
    *out = mg_name_intern(s, a->ground.sval, a->ground.slen);
    return true;
}

/* ── decoding ─────────────────────────────────────────────────────────── */

typedef struct {
    const Atom *atom;
    uintptr_t sort;
    void *node;
} MemoSlot;

typedef struct {
    MemoSlot *slots;
    size_t cap, len;
} Memo;

static size_t memo_index(const Atom *atom, uintptr_t sort, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)atom * UINT64_C(0x9e3779b97f4a7c15) ^ sort;
    h ^= h >> 29;
    return (size_t)h & (cap - 1u);
}

static void *memo_get(const Memo *m, const Atom *atom, uintptr_t sort) {
    if (!m->cap)
        return NULL;
    for (size_t i = memo_index(atom, sort, m->cap);; i = (i + 1u) & (m->cap - 1u)) {
        if (!m->slots[i].atom)
            return NULL;
        if (m->slots[i].atom == atom && m->slots[i].sort == sort)
            return m->slots[i].node;
    }
}

/* The memo only saves work: when it cannot grow, entries are dropped. */
static void memo_put(Memo *m, const Atom *atom, uintptr_t sort, void *node) {
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 256u;
        MemoSlot *slots = mg_sys_alloc(sizeof(MemoSlot) * cap);
        if (!slots)
            return;
        memset(slots, 0, sizeof(MemoSlot) * cap);
        for (size_t j = 0; j < m->cap; ++j) {
            MemoSlot e = m->slots[j];
            if (!e.atom)
                continue;
            size_t i = memo_index(e.atom, e.sort, cap);
            while (slots[i].atom)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e;
        }
        mg_sys_free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    size_t i = memo_index(atom, sort, m->cap);
    while (m->slots[i].atom)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (MemoSlot){atom, sort, node};
    ++m->len;
}

static int classify(const Atom *a, int sort) {
    if (!a || a->kind != ATOM_EXPR || a->expr.len == 0u)
        return -1;
    const Atom *head = a->expr.elems[0];
    if (head->kind != ATOM_SYMBOL)
        return -1;
    for (int c = 0; c < C_COUNT; ++c)
        if (CTORS[c].sort == sort && head->sym_id == g_ctor_sym[c])
            return a->expr.len == (uint32_t)CTORS[c].arity + 1u ? c : -1;
    return -1;
}

typedef struct {
    const Atom *atom;
    int8_t ctor;    /* -1: not yet classified */
    uint8_t sort;
} Frame;

typedef struct {
    MgStore *s;
    Memo memo;
    Frame *frames;
    size_t frame_len, frame_cap;
    void **results;
    size_t result_len, result_cap;
    MgStatus status;
    uint64_t resource_before, capacity_before;
} Decoder;

static bool push_frame(Decoder *d, const Atom *atom, int sort) {
    if (d->frame_len == d->frame_cap) {
        size_t cap = d->frame_cap ? d->frame_cap * 2u : 64u;
        Frame *grown = mg_sys_realloc(d->frames, sizeof(Frame) * cap);
        if (!grown)
            return false;
        d->frames = grown;
        d->frame_cap = cap;
    }
    d->frames[d->frame_len++] = (Frame){atom, -1, (uint8_t)sort};
    return true;
}

static bool push_result(Decoder *d, void *node) {
    if (d->result_len == d->result_cap) {
        size_t cap = d->result_cap ? d->result_cap * 2u : 64u;
        void **grown = mg_sys_realloc(d->results, sizeof(void *) * cap);
        if (!grown)
            return false;
        d->results = grown;
        d->result_cap = cap;
    }
    d->results[d->result_len++] = node;
    return true;
}

static void *build_leaf(MgStore *s, int c, const Atom *a, bool *bad) {
    uint32_t n = 0;
    const Atom *arg = a->expr.len > 1u ? a->expr.elems[1] : NULL;
    if (CTORS[c].leaf == LEAF_NATURAL && !atom_index(arg, &n)) {
        *bad = true;
        return NULL;
    }
    if (CTORS[c].leaf == LEAF_STRING && !atom_string_name(s, arg, &n)) {
        *bad = true;
        return NULL;
    }
    switch (c) {
    case C_TP_VAR: return mg_tp_var(s, n);
    case C_TP_PROP: return mg_tp_prop(s);
    case C_TP_BASE: return mg_tp_base(s, n);
    case C_TM_DB: return mg_tm_db(s, n);
    case C_TM_NAMED: return mg_tm_name(s, n);
    case C_TM_PRIM: return mg_tm_prim(s, n);
    case C_PF_GPA: return mg_pf_gpa(s, n);
    case C_PF_HYP: return mg_pf_hyp(s, n);
    case C_PF_KNOWN: return mg_pf_known(s, n);
    default: break;
    }
    *bad = true;
    return NULL;
}

static void *build_node(MgStore *s, int c, void *x, void *y) {
    switch (c) {
    case C_TP_ARR: return mg_tp_arr(s, x, y);
    case C_TP_ALL: return mg_tp_all(s, x);
    case C_TM_APP: return mg_tm_ap(s, x, y);
    case C_TM_LAM: return mg_tm_lam(s, x, y);
    case C_TM_IMP: return mg_tm_imp(s, x, y);
    case C_TM_ALL: return mg_tm_all(s, x, y);
    case C_TM_TAPP: return mg_tm_tap(s, x, y);
    case C_TM_TLAM: return mg_tm_tlam(s, x);
    case C_TM_TALL: return mg_tm_tall(s, x);
    case C_PF_TMAP: return mg_pf_tmap(s, x, y);
    case C_PF_PFAP: return mg_pf_pfap(s, x, y);
    case C_PF_PLAM: return mg_pf_plam(s, x, y);
    case C_PF_TLAM: return mg_pf_tlam(s, x, y);
    case C_PF_TPAP: return mg_pf_tpap(s, x, y);
    case C_PF_TPLAM: return mg_pf_tplam(s, x);
    default: return NULL;
    }
}

static MgStatus resource_outcome(const Decoder *d) {
    if (d->s->capacity_failures != d->capacity_before)
        return MG_CAPACITY;
    return MG_NOMEM;
}

static void *decode(Decoder *d, const Atom *root, int sort) {
    if (!push_frame(d, root, sort)) {
        d->status = MG_NOMEM;
        return NULL;
    }
    while (d->frame_len && d->status == MG_OK) {
        Frame *f = &d->frames[d->frame_len - 1u];
        if (f->ctor < 0) {
            void *hit = memo_get(&d->memo, f->atom, f->sort);
            if (hit) {
                --d->frame_len;
                if (!push_result(d, hit))
                    d->status = MG_NOMEM;
                continue;
            }
            int c = classify(f->atom, f->sort);
            if (c < 0) {
                d->status = MG_MALFORMED;
                break;
            }
            if (CTORS[c].leaf != LEAF_NONE) {
                bool bad = false;
                const Atom *atom = f->atom;
                int fsort = f->sort;
                void *node = build_leaf(d->s, c, atom, &bad);
                --d->frame_len;
                if (!node) {
                    d->status = bad ? MG_MALFORMED : resource_outcome(d);
                    break;
                }
                memo_put(&d->memo, atom, (uintptr_t)fsort, node);
                if (!push_result(d, node))
                    d->status = MG_NOMEM;
                continue;
            }
            f->ctor = (int8_t)c;
            const Atom *atom = f->atom;
            /* children right to left, so the first is decoded first */
            for (int k = CTORS[c].arity; k >= 1; --k)
                if (!push_frame(d, atom->expr.elems[k], CTORS[c].child[k - 1])) {
                    d->status = MG_NOMEM;
                    break;
                }
            continue;
        }
        int c = f->ctor;
        uint32_t arity = CTORS[c].arity;
        void *x = d->results[d->result_len - arity];
        void *y = arity > 1u ? d->results[d->result_len - arity + 1u] : NULL;
        d->result_len -= arity;
        void *node = build_node(d->s, c, x, y);
        const Atom *atom = f->atom;
        int fsort = f->sort;
        --d->frame_len;
        if (!node) {
            d->status = resource_outcome(d);
            break;
        }
        memo_put(&d->memo, atom, (uintptr_t)fsort, node);
        if (!push_result(d, node))
            d->status = MG_NOMEM;
    }
    void *out = d->status == MG_OK && d->result_len ? d->results[--d->result_len] : NULL;
    d->frame_len = 0;
    d->result_len = 0;
    return out;
}

static void decoder_init(Decoder *d, MgStore *s) {
    symbols_ready();
    memset(d, 0, sizeof(*d));
    d->s = s;
    d->status = MG_OK;
    d->resource_before = s->resource_failures;
    d->capacity_before = s->capacity_failures;
}

static void decoder_free(Decoder *d) {
    mg_sys_free(d->memo.slots);
    mg_sys_free(d->frames);
    mg_sys_free(d->results);
}

static void *decode_one(MgStore *s, Atom *atom, int sort, MgStatus *status) {
    Decoder d;
    decoder_init(&d, s);
    void *node = decode(&d, atom, sort);
    if (status)
        *status = d.status;
    decoder_free(&d);
    return node;
}

MgTp *mg_atom_tp(MgStore *s, Atom *atom, MgStatus *status) {
    return decode_one(s, atom, SORT_TP, status);
}

MgTm *mg_atom_tm(MgStore *s, Atom *atom, MgStatus *status) {
    return decode_one(s, atom, SORT_TM, status);
}

MgPf *mg_atom_pf(MgStore *s, Atom *atom, MgStatus *status) {
    return decode_one(s, atom, SORT_PF, status);
}

static MgCtx *decode_context(Decoder *d, Atom *atom) {
    uint32_t depth;
    if (!headed(atom, S_CONTEXT, 4u) || !atom_index(atom->expr.elems[1], &depth)) {
        d->status = MG_MALFORMED;
        return NULL;
    }
    Atom *types = atom->expr.elems[2], *hyps = atom->expr.elems[3];
    if (!types || types->kind != ATOM_EXPR || types->expr.len == 0u ||
        !is_sym(types->expr.elems[0], g_sym[S_TYPES]) ||
        !hyps || hyps->kind != ATOM_EXPR || hyps->expr.len == 0u ||
        !is_sym(hyps->expr.elems[0], g_sym[S_HYPOTHESES])) {
        d->status = MG_MALFORMED;
        return NULL;
    }
    MgTpList *terms = NULL;
    for (uint32_t i = types->expr.len; i > 1u && d->status == MG_OK; --i) {
        MgTp *t = decode(d, types->expr.elems[i - 1u], SORT_TP);
        if (!t)
            break;
        terms = mg_tplist_cons(d->s, t, terms);
        if (!terms)
            d->status = resource_outcome(d);
    }
    MgTmList *hl = NULL;
    for (uint32_t i = hyps->expr.len; i > 1u && d->status == MG_OK; --i) {
        MgTm *h = decode(d, hyps->expr.elems[i - 1u], SORT_TM);
        if (!h)
            break;
        hl = mg_tmlist_cons(d->s, h, hl);
        if (!hl)
            d->status = resource_outcome(d);
    }
    if (d->status != MG_OK)
        return NULL;
    MgCtx *ctx = mg_ctx(d->s, depth, terms, hl);
    if (!ctx)
        d->status = resource_outcome(d);
    return ctx;
}

MgCtx *mg_atom_context(MgStore *s, Atom *atom, MgStatus *status) {
    Decoder d;
    decoder_init(&d, s);
    MgCtx *ctx = decode_context(&d, atom);
    if (status)
        *status = d.status;
    decoder_free(&d);
    return ctx;
}

static bool decode_judgment(Decoder *d, Atom *atom, MgJudgment *out) {
    if (!headed(atom, S_JUDGMENT, 3u)) {
        d->status = MG_MALFORMED;
        return false;
    }
    MgCtx *ctx = decode_context(d, atom->expr.elems[1]);
    MgTm *prop = ctx ? decode(d, atom->expr.elems[2], SORT_TM) : NULL;
    if (!prop)
        return false;
    *out = (MgJudgment){ctx, prop};
    return true;
}

bool mg_atom_judgment(MgStore *s, Atom *atom, MgJudgment *out, MgStatus *status) {
    Decoder d;
    decoder_init(&d, s);
    bool ok = decode_judgment(&d, atom, out);
    if (status)
        *status = d.status;
    decoder_free(&d);
    return ok;
}

static bool decode_witness(Decoder *d, Atom *a, MgWitness *w) {
    memset(w, 0, sizeof(*w));
    if (!a || a->kind != ATOM_EXPR || a->expr.len == 0u) {
        d->status = MG_MALFORMED;
        return false;
    }
    Atom **e = a->expr.elems;
    uint32_t n = a->expr.len;
    if (headed(a, S_W_HYP, 2u) && atom_index(e[1], &w->index)) {
        w->rule = MG_RULE_HYP;
        return true;
    }
    if (headed(a, S_W_KNOWN, 2u) && atom_string_name(d->s, e[1], &w->name)) {
        w->rule = MG_RULE_KNOWN;
        if (!w->name)
            d->status = resource_outcome(d);
        return w->name != 0u;
    }
    if (headed(a, S_W_TERM_APP, 4u)) {
        w->rule = MG_RULE_TERM_APP;
        w->arg = decode(d, e[1], SORT_TM);
        w->type = w->arg ? decode(d, e[2], SORT_TP) : NULL;
        w->body = w->type ? decode(d, e[3], SORT_TM) : NULL;
        return w->body != NULL;
    }
    if (headed(a, S_W_PROOF_APP, 2u)) {
        w->rule = MG_RULE_PROOF_APP;
        w->domain = decode(d, e[1], SORT_TM);
        return w->domain != NULL;
    }
    if (headed(a, S_W_TERM_LAM, 1u)) {
        w->rule = MG_RULE_TERM_LAM;
        return true;
    }
    if (headed(a, S_W_PROOF_LAM, 2u)) {
        w->rule = MG_RULE_PROOF_LAM;
        w->source = decode(d, e[1], SORT_TM);
        return w->source != NULL;
    }
    if (headed(a, S_W_TYPE_APP, 3u)) {
        w->rule = MG_RULE_TYPE_APP;
        w->type = decode(d, e[1], SORT_TP);
        w->body = w->type ? decode(d, e[2], SORT_TM) : NULL;
        return w->body != NULL;
    }
    if (headed(a, S_W_TYPE_LAM, 1u)) {
        w->rule = MG_RULE_TYPE_LAM;
        return true;
    }
    (void)n;
    d->status = MG_MALFORMED;
    return false;
}

bool mg_atom_witness(MgStore *s, Atom *atom, MgWitness *out, MgStatus *status) {
    Decoder d;
    decoder_init(&d, s);
    bool ok = decode_witness(&d, atom, out);
    if (status)
        *status = d.status;
    decoder_free(&d);
    return ok;
}

bool mg_atom_row(MgStore *s, Atom *atom, MgRow *out, uint64_t **parents,
                 uint32_t *parents_cap, MgStatus *status) {
    Decoder d;
    decoder_init(&d, s);
    memset(out, 0, sizeof(*out));
    bool ok = headed(atom, S_ROW, 5u) &&
              mg_atom_natural(atom->expr.elems[4], &out->origin);
    Atom *ps = ok ? atom->expr.elems[3] : NULL;
    ok = ok && ps && ps->kind == ATOM_EXPR && ps->expr.len >= 1u &&
         is_sym(ps->expr.elems[0], g_sym[S_PARENTS]);
    if (!ok)
        d.status = MG_MALFORMED;
    uint32_t count = ok ? ps->expr.len - 1u : 0u;
    if (ok && count > *parents_cap) {
        uint64_t *grown = mg_sys_realloc(*parents, sizeof(uint64_t) * count);
        if (!grown) {
            d.status = MG_NOMEM;
            ok = false;
        } else {
            *parents = grown;
            *parents_cap = count;
        }
    }
    for (uint32_t i = 0; ok && i < count; ++i)
        if (!mg_atom_natural(ps->expr.elems[i + 1u], &(*parents)[i])) {
            d.status = MG_MALFORMED;
            ok = false;
        }
    if (ok) {
        out->parents = *parents;
        out->parent_count = count;
        ok = decode_judgment(&d, atom->expr.elems[1], &out->conclusion) &&
             decode_witness(&d, atom->expr.elems[2], &out->witness);
    }
    if (status)
        *status = d.status;
    decoder_free(&d);
    return ok && d.status == MG_OK;
}

/* ── encoding ─────────────────────────────────────────────────────────── */

typedef struct {
    const void *node;
    uint8_t sort;
    uint8_t stage;
} EncFrame;

typedef struct {
    const void *node;
    Atom *atom;
} EncSlot;

typedef struct {
    Arena *a;
    const MgStore *s;
    EncFrame *frames;
    size_t frame_len, frame_cap;
    Atom **results;
    size_t result_len, result_cap;
    EncSlot *memo;
    size_t memo_cap, memo_len;
    bool nomem;
} Encoder;

static size_t enc_index(const void *node, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)node * UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 31;
    return (size_t)h & (cap - 1u);
}

static Atom *enc_memo_get(const Encoder *e, const void *node) {
    if (!e->memo_cap)
        return NULL;
    for (size_t i = enc_index(node, e->memo_cap);; i = (i + 1u) & (e->memo_cap - 1u)) {
        if (!e->memo[i].node)
            return NULL;
        if (e->memo[i].node == node)
            return e->memo[i].atom;
    }
}

static void enc_memo_put(Encoder *e, const void *node, Atom *atom) {
    if ((e->memo_len + 1u) * 2u > e->memo_cap) {
        size_t cap = e->memo_cap ? e->memo_cap * 2u : 256u;
        EncSlot *slots = mg_sys_alloc(sizeof(EncSlot) * cap);
        if (!slots)
            return;
        memset(slots, 0, sizeof(EncSlot) * cap);
        for (size_t j = 0; j < e->memo_cap; ++j) {
            if (!e->memo[j].node)
                continue;
            size_t i = enc_index(e->memo[j].node, cap);
            while (slots[i].node)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e->memo[j];
        }
        mg_sys_free(e->memo);
        e->memo = slots;
        e->memo_cap = cap;
    }
    size_t i = enc_index(node, e->memo_cap);
    while (e->memo[i].node)
        i = (i + 1u) & (e->memo_cap - 1u);
    e->memo[i] = (EncSlot){node, atom};
    ++e->memo_len;
}

static bool enc_push(Encoder *e, const void *node, int sort) {
    if (e->frame_len == e->frame_cap) {
        size_t cap = e->frame_cap ? e->frame_cap * 2u : 64u;
        EncFrame *grown = mg_sys_realloc(e->frames, sizeof(EncFrame) * cap);
        if (!grown) {
            e->nomem = true;
            return false;
        }
        e->frames = grown;
        e->frame_cap = cap;
    }
    e->frames[e->frame_len++] = (EncFrame){node, (uint8_t)sort, 0u};
    return true;
}

static bool enc_result(Encoder *e, Atom *atom) {
    if (e->result_len == e->result_cap) {
        size_t cap = e->result_cap ? e->result_cap * 2u : 64u;
        Atom **grown = mg_sys_realloc(e->results, sizeof(Atom *) * cap);
        if (!grown) {
            e->nomem = true;
            return false;
        }
        e->results = grown;
        e->result_cap = cap;
    }
    e->results[e->result_len++] = atom;
    return true;
}

static Atom *head_sym(Arena *a, int c) { return atom_symbol_id(a, g_ctor_sym[c]); }

static Atom *expr2(Arena *a, int c, Atom *x) {
    Atom *items[2] = {head_sym(a, c), x};
    return atom_expr(a, items, 2u);
}

static Atom *expr3(Arena *a, int c, Atom *x, Atom *y) {
    Atom *items[3] = {head_sym(a, c), x, y};
    return atom_expr(a, items, 3u);
}

static Atom *name_atom(Arena *a, const MgStore *s, uint32_t name) {
    const MgName *n = mg_name_get(s, name);
    return n ? atom_string_n(a, n->text, n->len) : atom_string(a, "");
}

/* Children of a kernel node, in wire order, with their sorts. */
static uint32_t enc_children(const void *node, int sort, const void *child[2],
                             int csort[2]) {
    if (sort == SORT_TP) {
        const MgTp *t = node;
        switch (t->tag) {
        case MG_TP_ARR: child[0] = t->a; child[1] = t->b; csort[0] = csort[1] = SORT_TP; return 2;
        case MG_TP_ALL: child[0] = t->a; csort[0] = SORT_TP; return 1;
        default: return 0;
        }
    }
    if (sort == SORT_TM) {
        const MgTm *m = node;
        switch (m->tag) {
        case MG_TM_AP:
        case MG_TM_IMP:
            child[0] = m->f; child[1] = m->x; csort[0] = csort[1] = SORT_TM; return 2;
        case MG_TM_LAM:
        case MG_TM_ALL:
            child[0] = m->tp; child[1] = m->f; csort[0] = SORT_TP; csort[1] = SORT_TM; return 2;
        case MG_TM_TAP:
            child[0] = m->f; child[1] = m->tp; csort[0] = SORT_TM; csort[1] = SORT_TP; return 2;
        case MG_TM_TLAM:
        case MG_TM_TALL:
            child[0] = m->f; csort[0] = SORT_TM; return 1;
        default: return 0;
        }
    }
    const MgPf *p = node;
    switch (p->tag) {
    case MG_PF_TMAP: child[0] = p->p; child[1] = p->t; csort[0] = SORT_PF; csort[1] = SORT_TM; return 2;
    case MG_PF_PFAP: child[0] = p->p; child[1] = p->q; csort[0] = csort[1] = SORT_PF; return 2;
    case MG_PF_PLAM: child[0] = p->t; child[1] = p->p; csort[0] = SORT_TM; csort[1] = SORT_PF; return 2;
    case MG_PF_TLAM: child[0] = p->tp; child[1] = p->p; csort[0] = SORT_TP; csort[1] = SORT_PF; return 2;
    case MG_PF_TPAP: child[0] = p->p; child[1] = p->tp; csort[0] = SORT_PF; csort[1] = SORT_TP; return 2;
    case MG_PF_TPLAM: child[0] = p->p; csort[0] = SORT_PF; return 1;
    default: return 0;
    }
}

static Atom *enc_build(Encoder *e, const void *node, int sort, Atom **kids) {
    Arena *a = e->a;
    if (sort == SORT_TP) {
        const MgTp *t = node;
        switch (t->tag) {
        case MG_TP_VAR: return expr2(a, C_TP_VAR, atom_int(a, t->n));
        case MG_TP_PROP: {
            Atom *items[1] = {head_sym(a, C_TP_PROP)};
            return atom_expr(a, items, 1u);
        }
        case MG_TP_BASE: return expr2(a, C_TP_BASE, atom_int(a, t->n));
        case MG_TP_ARR: return expr3(a, C_TP_ARR, kids[0], kids[1]);
        case MG_TP_ALL: return expr2(a, C_TP_ALL, kids[0]);
        }
        return NULL;
    }
    if (sort == SORT_TM) {
        const MgTm *m = node;
        switch (m->tag) {
        case MG_TM_DB: return expr2(a, C_TM_DB, atom_int(a, m->n));
        case MG_TM_NAME: return expr2(a, C_TM_NAMED, name_atom(a, e->s, m->n));
        case MG_TM_PRIM: return expr2(a, C_TM_PRIM, atom_int(a, m->n));
        case MG_TM_AP: return expr3(a, C_TM_APP, kids[0], kids[1]);
        case MG_TM_LAM: return expr3(a, C_TM_LAM, kids[0], kids[1]);
        case MG_TM_IMP: return expr3(a, C_TM_IMP, kids[0], kids[1]);
        case MG_TM_ALL: return expr3(a, C_TM_ALL, kids[0], kids[1]);
        case MG_TM_TAP: return expr3(a, C_TM_TAPP, kids[0], kids[1]);
        case MG_TM_TLAM: return expr2(a, C_TM_TLAM, kids[0]);
        case MG_TM_TALL: return expr2(a, C_TM_TALL, kids[0]);
        }
        return NULL;
    }
    const MgPf *p = node;
    switch (p->tag) {
    case MG_PF_GPA: return expr2(a, C_PF_GPA, name_atom(a, e->s, p->n));
    case MG_PF_HYP: return expr2(a, C_PF_HYP, atom_int(a, p->n));
    case MG_PF_KNOWN: return expr2(a, C_PF_KNOWN, name_atom(a, e->s, p->n));
    case MG_PF_TMAP: return expr3(a, C_PF_TMAP, kids[0], kids[1]);
    case MG_PF_PFAP: return expr3(a, C_PF_PFAP, kids[0], kids[1]);
    case MG_PF_PLAM: return expr3(a, C_PF_PLAM, kids[0], kids[1]);
    case MG_PF_TLAM: return expr3(a, C_PF_TLAM, kids[0], kids[1]);
    case MG_PF_TPAP: return expr3(a, C_PF_TPAP, kids[0], kids[1]);
    case MG_PF_TPLAM: return expr2(a, C_PF_TPLAM, kids[0]);
    }
    return NULL;
}

static Atom *encode(Encoder *e, const void *root, int sort) {
    if (!root || !enc_push(e, root, sort))
        return NULL;
    while (e->frame_len && !e->nomem) {
        EncFrame *f = &e->frames[e->frame_len - 1u];
        const void *child[2] = {NULL, NULL};
        int csort[2] = {0, 0};
        uint32_t n = enc_children(f->node, f->sort, child, csort);
        if (f->stage == 0u) {
            Atom *hit = enc_memo_get(e, f->node);
            if (hit) {
                --e->frame_len;
                enc_result(e, hit);
                continue;
            }
            if (n) {
                f->stage = 1u;
                for (uint32_t k = n; k >= 1u; --k)
                    if (!enc_push(e, child[k - 1u], csort[k - 1u]))
                        break;
                continue;
            }
        }
        Atom *kids[2] = {NULL, NULL};
        for (uint32_t k = 0; k < n; ++k)
            kids[k] = e->results[e->result_len - n + k];
        e->result_len -= n;
        const void *node = f->node;
        Atom *atom = enc_build(e, node, f->sort, kids);
        --e->frame_len;
        enc_memo_put(e, node, atom);
        enc_result(e, atom);
    }
    Atom *out = !e->nomem && e->result_len ? e->results[--e->result_len] : NULL;
    e->frame_len = e->result_len = 0;
    return out;
}

static void encoder_init(Encoder *e, Arena *a, const MgStore *s) {
    symbols_ready();
    memset(e, 0, sizeof(*e));
    e->a = a;
    e->s = s;
}

static void encoder_free(Encoder *e) {
    mg_sys_free(e->frames);
    mg_sys_free(e->results);
    mg_sys_free(e->memo);
}

static Atom *encode_one(Arena *a, const MgStore *s, const void *node, int sort) {
    Encoder e;
    encoder_init(&e, a, s);
    Atom *out = encode(&e, node, sort);
    encoder_free(&e);
    return out;
}

Atom *mg_atom_of_tp(Arena *a, const MgTp *t) { return encode_one(a, NULL, t, SORT_TP); }
Atom *mg_atom_of_tm(Arena *a, const MgStore *s, const MgTm *m) {
    return encode_one(a, s, m, SORT_TM);
}
Atom *mg_atom_of_pf(Arena *a, const MgStore *s, const MgPf *p) {
    return encode_one(a, s, p, SORT_PF);
}

static Atom *encode_context(Encoder *e, const MgCtx *ctx) {
    Arena *a = e->a;
    uint32_t nt = mg_tplist_len(ctx->terms), nh = mg_tmlist_len(ctx->hyps);
    Atom **types = mg_sys_alloc(sizeof(Atom *) * ((size_t)nt + 1u));
    Atom **hyps = mg_sys_alloc(sizeof(Atom *) * ((size_t)nh + 1u));
    Atom *out = NULL;
    if (types && hyps) {
        types[0] = atom_symbol_id(a, g_sym[S_TYPES]);
        hyps[0] = atom_symbol_id(a, g_sym[S_HYPOTHESES]);
        uint32_t i = 1;
        bool ok = true;
        for (const MgTpList *l = ctx->terms; ok && l; l = l->tail)
            ok = (types[i++] = encode(e, l->head, SORT_TP)) != NULL;
        i = 1;
        for (const MgTmList *l = ctx->hyps; ok && l; l = l->tail)
            ok = (hyps[i++] = encode(e, l->head, SORT_TM)) != NULL;
        if (ok) {
            Atom *items[4] = {atom_symbol_id(a, g_sym[S_CONTEXT]),
                              atom_int(a, ctx->depth),
                              atom_expr(a, types, nt + 1u),
                              atom_expr(a, hyps, nh + 1u)};
            out = atom_expr(a, items, 4u);
        }
    }
    mg_sys_free(types);
    mg_sys_free(hyps);
    return out;
}

Atom *mg_atom_of_context(Arena *a, const MgStore *s, const MgCtx *ctx) {
    Encoder e;
    encoder_init(&e, a, s);
    Atom *out = encode_context(&e, ctx);
    encoder_free(&e);
    return out;
}

static Atom *encode_judgment(Encoder *e, MgJudgment j) {
    Atom *ctx = encode_context(e, j.ctx);
    Atom *prop = ctx ? encode(e, j.prop, SORT_TM) : NULL;
    if (!prop)
        return NULL;
    Atom *items[3] = {atom_symbol_id(e->a, g_sym[S_JUDGMENT]), ctx, prop};
    return atom_expr(e->a, items, 3u);
}

Atom *mg_atom_of_judgment(Arena *a, const MgStore *s, MgJudgment j) {
    Encoder e;
    encoder_init(&e, a, s);
    Atom *out = encode_judgment(&e, j);
    encoder_free(&e);
    return out;
}

static Atom *encode_witness(Encoder *e, const MgWitness *w) {
    Arena *a = e->a;
    Atom *items[4];
    uint32_t n = 1;
    switch (w->rule) {
    case MG_RULE_HYP:
        items[0] = atom_symbol_id(a, g_sym[S_W_HYP]);
        items[n++] = atom_int(a, w->index);
        break;
    case MG_RULE_KNOWN:
        items[0] = atom_symbol_id(a, g_sym[S_W_KNOWN]);
        items[n++] = name_atom(a, e->s, w->name);
        break;
    case MG_RULE_TERM_APP:
        items[0] = atom_symbol_id(a, g_sym[S_W_TERM_APP]);
        items[n++] = encode(e, w->arg, SORT_TM);
        items[n++] = encode(e, w->type, SORT_TP);
        items[n++] = encode(e, w->body, SORT_TM);
        break;
    case MG_RULE_PROOF_APP:
        items[0] = atom_symbol_id(a, g_sym[S_W_PROOF_APP]);
        items[n++] = encode(e, w->domain, SORT_TM);
        break;
    case MG_RULE_TERM_LAM:
        items[0] = atom_symbol_id(a, g_sym[S_W_TERM_LAM]);
        break;
    case MG_RULE_PROOF_LAM:
        items[0] = atom_symbol_id(a, g_sym[S_W_PROOF_LAM]);
        items[n++] = encode(e, w->source, SORT_TM);
        break;
    case MG_RULE_TYPE_APP:
        items[0] = atom_symbol_id(a, g_sym[S_W_TYPE_APP]);
        items[n++] = encode(e, w->type, SORT_TP);
        items[n++] = encode(e, w->body, SORT_TM);
        break;
    case MG_RULE_TYPE_LAM:
        items[0] = atom_symbol_id(a, g_sym[S_W_TYPE_LAM]);
        break;
    default:
        return NULL;
    }
    for (uint32_t i = 1; i < n; ++i)
        if (!items[i])
            return NULL;
    return atom_expr(a, items, n);
}

Atom *mg_atom_of_witness(Arena *a, const MgStore *s, const MgWitness *w) {
    Encoder e;
    encoder_init(&e, a, s);
    Atom *out = encode_witness(&e, w);
    encoder_free(&e);
    return out;
}

Atom *mg_atom_of_row(Arena *a, const MgStore *s, const MgRow *row) {
    Encoder e;
    encoder_init(&e, a, s);
    Atom *j = encode_judgment(&e, row->conclusion);
    Atom *w = j ? encode_witness(&e, &row->witness) : NULL;
    Atom *out = NULL;
    Atom **ps = w ? mg_sys_alloc(sizeof(Atom *) * ((size_t)row->parent_count + 1u)) : NULL;
    if (ps) {
        ps[0] = atom_symbol_id(a, g_sym[S_PARENTS]);
        for (uint32_t i = 0; i < row->parent_count; ++i)
            ps[i + 1u] = atom_int(a, (int64_t)row->parents[i]);
        Atom *items[5] = {atom_symbol_id(a, g_sym[S_ROW]), j, w,
                          atom_expr(a, ps, row->parent_count + 1u),
                          atom_int(a, (int64_t)row->origin)};
        out = atom_expr(a, items, 5u);
    }
    mg_sys_free(ps);
    encoder_free(&e);
    return out;
}
