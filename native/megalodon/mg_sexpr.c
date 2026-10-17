#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_sexpr.h"

#include <string.h>

/* ── arena ────────────────────────────────────────────────────────────── */

typedef struct SxBlock SxBlock;
struct SxBlock {
    SxBlock *next;
    size_t used, cap;
    _Alignas(16) unsigned char data[];
};

struct MgSxArena {
    SxBlock *blocks;
    SxBlock *spare;
};

MgSxArena *mg_sx_arena_new(void) {
    MgSxArena *a = mg_sys_alloc(sizeof(*a));
    if (a)
        memset(a, 0, sizeof(*a));
    return a;
}

static void blocks_free(SxBlock *b) {
    while (b) {
        SxBlock *next = b->next;
        mg_sys_free(b);
        b = next;
    }
}

void mg_sx_arena_reset(MgSxArena *a) {
    /* Keep one block for reuse. */
    while (a->blocks) {
        SxBlock *b = a->blocks;
        a->blocks = b->next;
        if (!a->spare || b->cap > a->spare->cap) {
            if (a->spare)
                mg_sys_free(a->spare);
            b->next = NULL;
            b->used = 0;
            a->spare = b;
        } else {
            mg_sys_free(b);
        }
    }
}

void mg_sx_arena_free(MgSxArena *a) {
    if (!a)
        return;
    blocks_free(a->blocks);
    blocks_free(a->spare);
    mg_sys_free(a);
}

static void *sx_alloc(MgSxArena *a, size_t size) {
    size = (size + 15u) & ~(size_t)15u;
    SxBlock *b = a->blocks;
    if (!b || b->cap - b->used < size) {
        if (a->spare && a->spare->cap >= size) {
            b = a->spare;
            a->spare = NULL;
        } else {
            size_t cap = size > (1u << 20) ? size : (1u << 20);
            b = mg_sys_alloc(sizeof(SxBlock) + cap);
            if (!b)
                return NULL;
            b->cap = cap;
        }
        b->used = 0;
        b->next = a->blocks;
        a->blocks = b;
    }
    void *p = b->data + b->used;
    b->used += size;
    return p;
}

/* ── reader ───────────────────────────────────────────────────────────── */

void mg_sx_reader_init(MgSxReader *r, const char *text, size_t len) {
    r->text = text;
    r->len = len;
    r->pos = 0;
    r->line = 1;
    r->error = NULL;
}

static void skip_space(MgSxReader *r) {
    while (r->pos < r->len) {
        char c = r->text[r->pos];
        if (c == '\n') {
            ++r->line;
            ++r->pos;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            ++r->pos;
        } else if (c == ';') {
            while (r->pos < r->len && r->text[r->pos] != '\n')
                ++r->pos;
        } else {
            break;
        }
    }
}

typedef struct {
    MgSx **items;
    uint32_t len, cap;
} SxVec;

static bool vec_push(MgSxArena *a, SxVec *v, MgSx *x) {
    if (v->len == v->cap) {
        uint32_t cap = v->cap ? v->cap * 2u : 4u;
        MgSx **items = sx_alloc(a, sizeof(MgSx *) * cap);
        if (!items)
            return false;
        if (v->len)
            memcpy(items, v->items, sizeof(MgSx *) * v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = x;
    return true;
}

/* Iterative: deep data does not consume C stack. */
MgSx *mg_sx_read(MgSxReader *r, MgSxArena *a) {
    typedef struct {
        SxVec vec;
        uint64_t line;
    } Frame;
    Frame *stack = NULL;
    uint32_t depth = 0, cap = 0;
    MgSx *result = NULL;
    skip_space(r);
    if (r->pos >= r->len)
        return NULL;
    for (;;) {
        skip_space(r);
        if (r->pos >= r->len) {
            r->error = depth ? "unterminated list" : NULL;
            break;
        }
        char c = r->text[r->pos];
        MgSx *atom = NULL;
        if (c == '(') {
            ++r->pos;
            if (depth == cap) {
                uint32_t nc = cap ? cap * 2u : 64u;
                Frame *ns = mg_sys_realloc(stack, sizeof(Frame) * nc);
                if (!ns) {
                    r->error = "out of memory";
                    break;
                }
                stack = ns;
                cap = nc;
            }
            stack[depth].vec = (SxVec){0};
            stack[depth].line = r->line;
            ++depth;
            continue;
        }
        if (c == ')') {
            ++r->pos;
            if (!depth) {
                r->error = "unexpected )";
                break;
            }
            --depth;
            atom = sx_alloc(a, sizeof(MgSx));
            if (!atom) {
                r->error = "out of memory";
                break;
            }
            *atom = (MgSx){.kind = MG_SX_LIST, .len = stack[depth].vec.len,
                           .items = stack[depth].vec.items,
                           .line = stack[depth].line};
        } else if (c == '"') {
            size_t start = ++r->pos;
            size_t extra = 0;
            while (r->pos < r->len && r->text[r->pos] != '"') {
                if (r->text[r->pos] == '\\' && r->pos + 1u < r->len) {
                    ++r->pos;
                    ++extra;
                }
                if (r->text[r->pos] == '\n')
                    ++r->line;
                ++r->pos;
            }
            if (r->pos >= r->len) {
                r->error = "unterminated string";
                break;
            }
            size_t raw = r->pos - start;
            char *text = sx_alloc(a, raw - extra + 1u);
            atom = text ? sx_alloc(a, sizeof(MgSx)) : NULL;
            if (!atom) {
                r->error = "out of memory";
                break;
            }
            size_t k = 0;
            for (size_t i = start; i < r->pos; ++i) {
                if (r->text[i] == '\\' && i + 1u < r->pos)
                    ++i;
                text[k++] = r->text[i];
            }
            text[k] = '\0';
            ++r->pos;
            *atom = (MgSx){.kind = MG_SX_STRING, .len = (uint32_t)k, .text = text,
                           .line = r->line};
        } else {
            size_t start = r->pos;
            while (r->pos < r->len) {
                char d = r->text[r->pos];
                if (d == '(' || d == ')' || d == '"' || d == ' ' || d == '\t' ||
                    d == '\n' || d == '\r' || d == ';')
                    break;
                ++r->pos;
            }
            size_t n = r->pos - start;
            bool digits = n > 0u && n <= 19u;
            uint64_t value = 0;
            for (size_t i = 0; digits && i < n; ++i) {
                char d = r->text[start + i];
                if (d < '0' || d > '9')
                    digits = false;
                else
                    value = value * 10u + (uint64_t)(d - '0');
            }
            char *text = sx_alloc(a, n + 1u);
            atom = text ? sx_alloc(a, sizeof(MgSx)) : NULL;
            if (!atom) {
                r->error = "out of memory";
                break;
            }
            memcpy(text, r->text + start, n);
            text[n] = '\0';
            *atom = (MgSx){.kind = digits ? MG_SX_INTEGER : MG_SX_SYMBOL,
                           .len = (uint32_t)n, .text = text, .value = value,
                           .line = r->line};
        }
        if (!depth) {
            result = atom;
            break;
        }
        if (!vec_push(a, &stack[depth - 1u].vec, atom)) {
            r->error = "out of memory";
            break;
        }
    }
    mg_sys_free(stack);
    return r->error ? NULL : result;
}

/* ── decoders ─────────────────────────────────────────────────────────── */

/* Decoding recursion follows the thread's kernel depth guard; reaching it
 * is counted as a capacity failure on the store. */
#define MG_DECODE_DEPTH_LIMIT mg_thread_depth_limit()

static bool sx_index(const MgSx *x, uint32_t *out) {
    if (!x || x->kind != MG_SX_INTEGER || x->value > UINT32_MAX - 1u)
        return false;
    *out = (uint32_t)x->value;
    return true;
}

static uint32_t sx_name(MgStore *s, const MgSx *x) {
    if (!x || (x->kind != MG_SX_STRING && x->kind != MG_SX_SYMBOL))
        return 0;
    return mg_name_intern(s, x->text, x->len);
}

static MgTp *wire_tp(MgStore *s, const MgSx *x, uint32_t guard) {
    uint32_t n;
    if (!guard) {
        ++s->capacity_failures;
        return NULL;
    }
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return NULL;
    if (mg_sx_head(x, "Tp.var") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_tp_var(s, n);
    if (mg_sx_head(x, "Tp.prop") && x->len == 1u)
        return mg_tp_prop(s);
    if (mg_sx_head(x, "Tp.base") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_tp_base(s, n);
    if (mg_sx_head(x, "Tp.arr") && x->len == 3u)
        return mg_tp_arr(s, wire_tp(s, x->items[1], guard - 1u),
                         wire_tp(s, x->items[2], guard - 1u));
    if (mg_sx_head(x, "Tp.all") && x->len == 2u)
        return mg_tp_all(s, wire_tp(s, x->items[1], guard - 1u));
    return NULL;
}

MgTp *mg_sx_wire_tp(MgStore *s, const MgSx *x) {
    return wire_tp(s, x, MG_DECODE_DEPTH_LIMIT);
}

static MgTm *wire_tm(MgStore *s, const MgSx *x, uint32_t guard) {
    uint32_t n;
    if (!guard) {
        ++s->capacity_failures;
        return NULL;
    }
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return NULL;
    --guard;
    if (mg_sx_head(x, "Tm.db") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_tm_db(s, n);
    if (mg_sx_head(x, "Tm.named") && x->len == 2u &&
        x->items[1]->kind == MG_SX_STRING)
        return mg_tm_name(s, sx_name(s, x->items[1]));
    if (mg_sx_head(x, "Tm.prim") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_tm_prim(s, n);
    if (mg_sx_head(x, "Tm.app") && x->len == 3u)
        return mg_tm_ap(s, wire_tm(s, x->items[1], guard),
                        wire_tm(s, x->items[2], guard));
    if (mg_sx_head(x, "Tm.lam") && x->len == 3u)
        return mg_tm_lam(s, wire_tp(s, x->items[1], guard),
                         wire_tm(s, x->items[2], guard));
    if (mg_sx_head(x, "Tm.imp") && x->len == 3u)
        return mg_tm_imp(s, wire_tm(s, x->items[1], guard),
                         wire_tm(s, x->items[2], guard));
    if (mg_sx_head(x, "Tm.all") && x->len == 3u)
        return mg_tm_all(s, wire_tp(s, x->items[1], guard),
                         wire_tm(s, x->items[2], guard));
    if (mg_sx_head(x, "Tm.typeApp") && x->len == 3u)
        return mg_tm_tap(s, wire_tm(s, x->items[1], guard),
                         wire_tp(s, x->items[2], guard));
    if (mg_sx_head(x, "Tm.typeLam") && x->len == 2u)
        return mg_tm_tlam(s, wire_tm(s, x->items[1], guard));
    if (mg_sx_head(x, "Tm.typeAll") && x->len == 2u)
        return mg_tm_tall(s, wire_tm(s, x->items[1], guard));
    return NULL;
}

MgTm *mg_sx_wire_tm(MgStore *s, const MgSx *x) {
    return wire_tm(s, x, MG_DECODE_DEPTH_LIMIT);
}

static MgPf *wire_pf(MgStore *s, const MgSx *x, uint32_t guard) {
    uint32_t n;
    if (!guard) {
        ++s->capacity_failures;
        return NULL;
    }
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return NULL;
    --guard;
    if (mg_sx_head(x, "Pf.gpa") && x->len == 2u &&
        x->items[1]->kind == MG_SX_STRING)
        return mg_pf_gpa(s, sx_name(s, x->items[1]));
    if (mg_sx_head(x, "Pf.hyp") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_pf_hyp(s, n);
    if (mg_sx_head(x, "Pf.known") && x->len == 2u &&
        x->items[1]->kind == MG_SX_STRING)
        return mg_pf_known(s, sx_name(s, x->items[1]));
    if (mg_sx_head(x, "Pf.termApp") && x->len == 3u)
        return mg_pf_tmap(s, wire_pf(s, x->items[1], guard),
                          wire_tm(s, x->items[2], guard));
    if (mg_sx_head(x, "Pf.proofApp") && x->len == 3u)
        return mg_pf_pfap(s, wire_pf(s, x->items[1], guard),
                          wire_pf(s, x->items[2], guard));
    if (mg_sx_head(x, "Pf.proofLam") && x->len == 3u)
        return mg_pf_plam(s, wire_tm(s, x->items[1], guard),
                          wire_pf(s, x->items[2], guard));
    if (mg_sx_head(x, "Pf.termLam") && x->len == 3u)
        return mg_pf_tlam(s, wire_tp(s, x->items[1], guard),
                          wire_pf(s, x->items[2], guard));
    if (mg_sx_head(x, "Pf.typeApp") && x->len == 3u)
        return mg_pf_tpap(s, wire_pf(s, x->items[1], guard),
                          wire_tp(s, x->items[2], guard));
    if (mg_sx_head(x, "Pf.typeLam") && x->len == 2u)
        return mg_pf_tplam(s, wire_pf(s, x->items[1], guard));
    return NULL;
}

MgPf *mg_sx_wire_pf(MgStore *s, const MgSx *x) {
    return wire_pf(s, x, MG_DECODE_DEPTH_LIMIT);
}

/* ── NativeCursorWire ─────────────────────────────────────────────────── */

static bool sx_natural(const MgSx *x, uint64_t *out) {
    if (!x || x->kind != MG_SX_INTEGER)
        return false;
    *out = x->value;
    return true;
}

MgCtx *mg_sx_wire_context(MgStore *s, const MgSx *x) {
    uint32_t depth;
    if (!mg_sx_head(x, "Megalodon.context") || x->len != 4u ||
        !sx_index(x->items[1], &depth) || !mg_sx_head(x->items[2], "types") ||
        !mg_sx_head(x->items[3], "hypotheses"))
        return NULL;
    const MgSx *types = x->items[2], *hyps = x->items[3];
    MgTpList *terms = NULL;
    for (uint32_t i = types->len; i > 1u; --i) {
        MgTp *t = mg_sx_wire_tp(s, types->items[i - 1u]);
        terms = t ? mg_tplist_cons(s, t, terms) : NULL;
        if (!terms)
            return NULL;
    }
    MgTmList *hl = NULL;
    for (uint32_t i = hyps->len; i > 1u; --i) {
        MgTm *h = mg_sx_wire_tm(s, hyps->items[i - 1u]);
        hl = h ? mg_tmlist_cons(s, h, hl) : NULL;
        if (!hl)
            return NULL;
    }
    return mg_ctx(s, depth, terms, hl);
}

bool mg_sx_wire_judgment(MgStore *s, const MgSx *x, MgJudgment *out) {
    if (!mg_sx_head(x, "Megalodon.judgment") || x->len != 3u)
        return false;
    MgCtx *ctx = mg_sx_wire_context(s, x->items[1]);
    MgTm *prop = ctx ? mg_sx_wire_tm(s, x->items[2]) : NULL;
    if (!prop)
        return false;
    *out = (MgJudgment){ctx, prop};
    return true;
}

bool mg_sx_wire_witness(MgStore *s, const MgSx *x, MgWitness *w) {
    memset(w, 0, sizeof(*w));
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return false;
    uint32_t n;
    if (mg_sx_head(x, "Megalodon.hyp") && x->len == 2u &&
        sx_index(x->items[1], &n)) {
        w->rule = MG_RULE_HYP;
        w->index = n;
        return true;
    }
    if (mg_sx_head(x, "Megalodon.known") && x->len == 2u &&
        x->items[1]->kind == MG_SX_STRING) {
        w->rule = MG_RULE_KNOWN;
        w->name = sx_name(s, x->items[1]);
        return w->name != 0u;
    }
    if (mg_sx_head(x, "Megalodon.termApp") && x->len == 4u) {
        w->rule = MG_RULE_TERM_APP;
        w->arg = mg_sx_wire_tm(s, x->items[1]);
        w->type = mg_sx_wire_tp(s, x->items[2]);
        w->body = mg_sx_wire_tm(s, x->items[3]);
        return w->arg && w->type && w->body;
    }
    if (mg_sx_head(x, "Megalodon.proofApp") && x->len == 2u) {
        w->rule = MG_RULE_PROOF_APP;
        w->domain = mg_sx_wire_tm(s, x->items[1]);
        return w->domain != NULL;
    }
    if (mg_sx_head(x, "Megalodon.termLam") && x->len == 1u) {
        w->rule = MG_RULE_TERM_LAM;
        return true;
    }
    if (mg_sx_head(x, "Megalodon.proofLam") && x->len == 2u) {
        w->rule = MG_RULE_PROOF_LAM;
        w->source = mg_sx_wire_tm(s, x->items[1]);
        return w->source != NULL;
    }
    if (mg_sx_head(x, "Megalodon.typeApp") && x->len == 3u) {
        w->rule = MG_RULE_TYPE_APP;
        w->type = mg_sx_wire_tp(s, x->items[1]);
        w->body = mg_sx_wire_tm(s, x->items[2]);
        return w->type && w->body;
    }
    if (mg_sx_head(x, "Megalodon.typeLam") && x->len == 1u) {
        w->rule = MG_RULE_TYPE_LAM;
        return true;
    }
    return false;
}

bool mg_sx_wire_row(MgStore *s, const MgSx *x, MgRow *out,
                    uint64_t **parents, uint32_t *parents_cap, bool *nomem) {
    *nomem = false;
    memset(out, 0, sizeof(*out));
    if (!mg_sx_head(x, "Megalodon.row") || x->len != 5u ||
        !mg_sx_head(x->items[3], "parents") ||
        !sx_natural(x->items[4], &out->origin))
        return false;
    const MgSx *ps = x->items[3];
    uint32_t count = ps->len - 1u;
    if (count > *parents_cap) {
        uint64_t *grown = mg_sys_realloc(*parents, sizeof(uint64_t) * count);
        if (!grown) {
            *nomem = true;
            return false;
        }
        *parents = grown;
        *parents_cap = count;
    }
    for (uint32_t i = 0; i < count; ++i)
        if (!sx_natural(ps->items[i + 1u], &(*parents)[i]))
            return false;
    out->parents = *parents;
    out->parent_count = count;
    return mg_sx_wire_judgment(s, x->items[1], &out->conclusion) &&
           mg_sx_wire_witness(s, x->items[2], &out->witness);
}

/* Upstream front-end spelling (syntax.ml tp/tm/pf_to_sexpr). */
static MgTp *up_tp(MgStore *s, const MgSx *x, uint32_t levels, uint32_t guard) {
    uint32_t n;
    if (!guard) {
        ++s->capacity_failures;
        return NULL;
    }
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return NULL;
    --guard;
    if (mg_sx_head(x, "PROP") && x->len == 1u)
        return mg_tp_prop(s);
    if (mg_sx_head(x, "SET") && x->len == 1u)
        return mg_tp_base(s, 0);
    if (mg_sx_head(x, "TPVAR") && x->len == 2u && sx_index(x->items[1], &n))
        return n < levels ? mg_tp_var(s, levels - n - 1u) : NULL;
    if (mg_sx_head(x, "AR") && x->len == 3u)
        return mg_tp_arr(s, up_tp(s, x->items[1], levels, guard),
                         up_tp(s, x->items[2], levels, guard));
    return NULL;
}

MgTp *mg_sx_upstream_tp(MgStore *s, const MgSx *x, uint32_t levels) {
    return up_tp(s, x, levels, MG_DECODE_DEPTH_LIMIT);
}

static MgTm *up_tm(MgStore *s, const MgSx *x, uint32_t levels, uint32_t guard) {
    uint32_t n;
    if (!guard) {
        ++s->capacity_failures;
        return NULL;
    }
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return NULL;
    --guard;
    if (mg_sx_head(x, "DB") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_tm_db(s, n);
    if (mg_sx_head(x, "TMH") && x->len == 2u && x->items[1]->kind == MG_SX_STRING)
        return mg_tm_name(s, sx_name(s, x->items[1]));
    if (mg_sx_head(x, "PRIM") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_tm_prim(s, n);
    if (mg_sx_head(x, "TPAP") && x->len == 3u)
        return mg_tm_tap(s, up_tm(s, x->items[1], levels, guard),
                         up_tp(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "AP") && x->len == 3u)
        return mg_tm_ap(s, up_tm(s, x->items[1], levels, guard),
                        up_tm(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "LAM") && x->len == 3u)
        return mg_tm_lam(s, up_tp(s, x->items[1], levels, guard),
                         up_tm(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "IMP") && x->len == 3u)
        return mg_tm_imp(s, up_tm(s, x->items[1], levels, guard),
                         up_tm(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "ALL") && x->len == 3u)
        return mg_tm_all(s, up_tp(s, x->items[1], levels, guard),
                         up_tm(s, x->items[2], levels, guard));
    return NULL;
}

MgTm *mg_sx_upstream_tm(MgStore *s, const MgSx *x, uint32_t levels) {
    return up_tm(s, x, levels, MG_DECODE_DEPTH_LIMIT);
}

static MgPf *up_pf(MgStore *s, const MgSx *x, uint32_t levels, uint32_t guard) {
    uint32_t n;
    if (!guard) {
        ++s->capacity_failures;
        return NULL;
    }
    if (!x || x->kind != MG_SX_LIST || x->len == 0u)
        return NULL;
    --guard;
    if (mg_sx_head(x, "HYP") && x->len == 2u && sx_index(x->items[1], &n))
        return mg_pf_hyp(s, n);
    if (mg_sx_head(x, "KNOWN") && x->len == 2u && x->items[1]->kind == MG_SX_STRING)
        return mg_pf_known(s, sx_name(s, x->items[1]));
    if (mg_sx_head(x, "PTPAP") && x->len == 3u)
        return mg_pf_tpap(s, up_pf(s, x->items[1], levels, guard),
                          up_tp(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "PTMAP") && x->len == 3u)
        return mg_pf_tmap(s, up_pf(s, x->items[1], levels, guard),
                          up_tm(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "PPFAP") && x->len == 3u)
        return mg_pf_pfap(s, up_pf(s, x->items[1], levels, guard),
                          up_pf(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "PLAM") && x->len == 3u)
        return mg_pf_plam(s, up_tm(s, x->items[1], levels, guard),
                          up_pf(s, x->items[2], levels, guard));
    if (mg_sx_head(x, "TLAM") && x->len == 3u)
        return mg_pf_tlam(s, up_tp(s, x->items[1], levels, guard),
                          up_pf(s, x->items[2], levels, guard));
    return NULL;
}

MgPf *mg_sx_upstream_pf(MgStore *s, const MgSx *x, uint32_t levels) {
    return up_pf(s, x, levels, MG_DECODE_DEPTH_LIMIT);
}
