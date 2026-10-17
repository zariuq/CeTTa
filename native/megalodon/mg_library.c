#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_library.h"

#include "mg_hash.h"
#include "mg_profiles.h"
#include "mg_sexpr.h"

#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* ── evidence ─────────────────────────────────────────────────────────── */

typedef struct {
    MgRow *rows;
    uint64_t *poff;
    uint32_t len, cap;
    uint64_t *parents;
    uint64_t plen, pcap;
    bool nomem;
} EvBuilder;

static uint64_t ev_add(EvBuilder *b, MgJudgment j, const MgWitness *w,
                       const uint64_t *ps, uint32_t n) {
    if (b->nomem || b->len == UINT32_MAX)
        return UINT64_MAX;
    if (b->len == b->cap) {
        uint32_t cap = b->cap ? b->cap * 2u : 64u;
        MgRow *rows = mg_sys_realloc(b->rows, sizeof(MgRow) * cap);
        if (!rows) {
            b->nomem = true;
            return UINT64_MAX;
        }
        b->rows = rows;
        uint64_t *poff = mg_sys_realloc(b->poff, sizeof(uint64_t) * cap);
        if (!poff) {
            b->nomem = true;
            return UINT64_MAX;
        }
        b->poff = poff;
        b->cap = cap;
    }
    if (b->plen + n > b->pcap) {
        uint64_t cap = b->pcap ? b->pcap * 2u : 128u;
        while (cap < b->plen + n)
            cap *= 2u;
        uint64_t *grown = mg_sys_realloc(b->parents, sizeof(uint64_t) * (size_t)cap);
        if (!grown) {
            b->nomem = true;
            return UINT64_MAX;
        }
        b->parents = grown;
        b->pcap = cap;
    }
    uint64_t address = b->len;
    MgRow *row = &b->rows[b->len];
    memset(row, 0, sizeof(*row));
    row->conclusion = j;
    row->witness = *w;
    row->parent_count = n;
    row->origin = address;
    b->poff[b->len] = b->plen;
    if (n)
        memcpy(b->parents + b->plen, ps, sizeof(uint64_t) * n);
    b->plen += n;
    ++b->len;
    return address;
}

static uint64_t ev_record(void *context, MgJudgment j, const MgWitness *w,
                          const uint64_t *ps, uint32_t n) {
    return ev_add(context, j, w, ps, n);
}

static void ev_builder_free(EvBuilder *b) {
    mg_sys_free(b->rows);
    mg_sys_free(b->poff);
    mg_sys_free(b->parents);
    memset(b, 0, sizeof(*b));
}

static MgStatus ev_finish(EvBuilder *b, uint64_t root, MgEvidence *out) {
    memset(out, 0, sizeof(*out));
    if (b->nomem) {
        ev_builder_free(b);
        return MG_NOMEM;
    }
    out->addresses = mg_sys_alloc(sizeof(uint64_t) * (b->len ? b->len : 1u));
    if (!out->addresses) {
        ev_builder_free(b);
        return MG_NOMEM;
    }
    for (uint32_t i = 0; i < b->len; ++i) {
        out->addresses[i] = i;
        b->rows[i].parents = b->parents + b->poff[i];
    }
    out->rows = b->rows;
    out->parent_store = b->parents;
    out->row_count = b->len;
    out->root = root;
    mg_sys_free(b->poff);
    memset(b, 0, sizeof(*b));
    return MG_OK;
}

void mg_evidence_free(MgEvidence *ev) {
    if (!ev)
        return;
    mg_sys_free(ev->rows);
    mg_sys_free(ev->addresses);
    mg_sys_free(ev->parent_store);
    memset(ev, 0, sizeof(*ev));
}

void mg_evidence_destroy(MgEvidence *ev) {
    if (!ev)
        return;
    mg_evidence_free(ev);
    mg_sys_free(ev);
}

MgEvidence *mg_evidence_clone(const MgEvidence *ev) {
    MgEvidence *copy = mg_sys_alloc(sizeof(*copy));
    if (!copy)
        return NULL;
    memset(copy, 0, sizeof(*copy));
    uint64_t parents = 0;
    for (uint32_t i = 0; i < ev->row_count; ++i)
        parents += ev->rows[i].parent_count;
    size_t n = ev->row_count ? ev->row_count : 1u;
    copy->rows = mg_sys_alloc(sizeof(MgRow) * n);
    copy->addresses = mg_sys_alloc(sizeof(uint64_t) * n);
    copy->parent_store = mg_sys_alloc(sizeof(uint64_t) * (size_t)(parents ? parents : 1u));
    if (!copy->rows || !copy->addresses || !copy->parent_store) {
        mg_evidence_destroy(copy);
        return NULL;
    }
    uint64_t used = 0;
    for (uint32_t i = 0; i < ev->row_count; ++i) {
        copy->rows[i] = ev->rows[i];
        copy->rows[i].parents = copy->parent_store + used;
        if (ev->rows[i].parent_count)
            memcpy(copy->rows[i].parents, ev->rows[i].parents,
                   sizeof(uint64_t) * ev->rows[i].parent_count);
        used += ev->rows[i].parent_count;
        copy->addresses[i] = ev->addresses[i];
    }
    copy->row_count = ev->row_count;
    copy->root = ev->root;
    copy->lazy = ev->lazy;
    return copy;
}

/* Append another evidence's rows, renumbered after the current ones. */
static uint64_t ev_append(EvBuilder *b, const MgEvidence *ev) {
    uint64_t base = b->len;
    uint64_t *ps = NULL;
    uint32_t cap = 0;
    for (uint32_t i = 0; i < ev->row_count && !b->nomem; ++i) {
        const MgRow *r = &ev->rows[i];
        if (r->parent_count > cap) {
            uint64_t *grown = mg_sys_realloc(ps, sizeof(uint64_t) * r->parent_count);
            if (!grown) {
                b->nomem = true;
                break;
            }
            ps = grown;
            cap = r->parent_count;
        }
        for (uint32_t k = 0; k < r->parent_count; ++k)
            ps[k] = base + r->parents[k];
        if (ev_add(b, r->conclusion, &r->witness, ps, r->parent_count) == UINT64_MAX)
            break;
    }
    mg_sys_free(ps);
    return b->nomem ? UINT64_MAX : base + ev->root;
}

/* The rows of a checked proof tree, rechecked at its snapshot: in source
 * form for a lazy theorem (whose reference form may be infeasible to
 * build), else exact. */
static uint64_t ev_tree(EvBuilder *b, const MgThm *t, MgStatus *st) {
    MgBudget budget;
    mg_budget_init(&budget, 0);
    MgCheck c;
    mg_check_init(&c, (MgEnv){t->theory, t->rev}, &budget, NULL);
    MgRowRecorder rec = {b, ev_record};
    uint64_t root = 0;
    if (t->lazy) {
        MgTm *prop = NULL;
        *st = mg_infer_proof_lazy_recorded(&c, t->judgment.ctx, t->proof, &rec, &prop, &root);
    } else {
        MgJudgment j;
        *st = mg_infer_proof_recorded(&c, t->judgment.ctx, t->proof, &rec, &j, &root);
    }
    return *st == MG_OK ? root : UINT64_MAX;
}

typedef struct {
    const MgThm *thm;
    uint64_t address;
} ThmSlot;

typedef struct {
    ThmSlot *slots;
    size_t cap, len;
} ThmMemo;

static size_t thm_slot(const MgThm *t, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)t * UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 31;
    return (size_t)h & (cap - 1u);
}

static bool thm_memo_get(const ThmMemo *m, const MgThm *t, uint64_t *out) {
    if (!m->cap)
        return false;
    for (size_t i = thm_slot(t, m->cap);; i = (i + 1u) & (m->cap - 1u)) {
        if (!m->slots[i].thm)
            return false;
        if (m->slots[i].thm == t) {
            *out = m->slots[i].address;
            return true;
        }
    }
}

static bool thm_memo_put(ThmMemo *m, const MgThm *t, uint64_t address) {
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 64u;
        ThmSlot *slots = mg_sys_alloc(sizeof(ThmSlot) * cap);
        if (!slots)
            return false;
        memset(slots, 0, sizeof(ThmSlot) * cap);
        for (size_t j = 0; j < m->cap; ++j) {
            if (!m->slots[j].thm)
                continue;
            size_t i = thm_slot(m->slots[j].thm, cap);
            while (slots[i].thm)
                i = (i + 1u) & (cap - 1u);
            slots[i] = m->slots[j];
        }
        mg_sys_free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    size_t i = thm_slot(t, m->cap);
    while (m->slots[i].thm)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (ThmSlot){t, address};
    ++m->len;
    return true;
}

/* One non-step theorem's rows. */
static uint64_t ev_leaf(EvBuilder *b, const MgThm *t, MgStatus *st) {
    *st = MG_OK;
    switch (t->origin) {
    case MG_THM_TREE:
        return ev_tree(b, t, st);
    case MG_THM_SESSION: {
        uint64_t a = t->evidence ? ev_append(b, t->evidence) : UINT64_MAX;
        if (a == UINT64_MAX)
            *st = t->evidence ? MG_NOMEM : MG_MALFORMED;
        return a;
    }
    case MG_THM_DECLARATION: {
        MgWitness w = {.rule = MG_RULE_KNOWN, .name = t->decl ? t->decl->name : 0u};
        uint64_t a = ev_add(b, t->judgment, &w, NULL, 0);
        if (a == UINT64_MAX)
            *st = MG_NOMEM;
        return a;
    }
    case MG_THM_STEP:
        break;
    }
    *st = MG_MALFORMED;
    return UINT64_MAX;
}

MgStatus mg_evidence_of(const MgThm *root, MgEvidence *out) {
    EvBuilder b = {0};
    ThmMemo memo = {0};
    typedef struct {
        const MgThm *thm;
        bool expanded;
    } Frame;
    Frame *stack = NULL;
    size_t len = 0, cap = 0;
    MgStatus st = MG_OK;
    uint64_t root_address = 0;
#define PUSH_FRAME(t) do { \
        if (len == cap) { \
            size_t nc = cap ? cap * 2u : 64u; \
            Frame *grown = mg_sys_realloc(stack, sizeof(Frame) * nc); \
            if (!grown) { st = MG_NOMEM; break; } \
            stack = grown; cap = nc; \
        } \
        stack[len++] = (Frame){(t), false}; \
    } while (0)
    PUSH_FRAME(root);
    while (st == MG_OK && len) {
        Frame *f = &stack[len - 1u];
        uint64_t address;
        if (thm_memo_get(&memo, f->thm, &address)) {
            --len;
            continue;
        }
        const MgThm *t = f->thm;
        if (t->origin == MG_THM_STEP && !f->expanded) {
            f->expanded = true;
            for (uint32_t i = t->premise_count; i > 0u && st == MG_OK; --i)
                PUSH_FRAME(t->premises[i - 1u]);
            continue;
        }
        if (t->origin == MG_THM_STEP) {
            uint64_t ps[2];
            for (uint32_t i = 0; i < t->premise_count; ++i)
                thm_memo_get(&memo, t->premises[i], &ps[i]);
            address = ev_add(&b, t->judgment, &t->witness, ps, t->premise_count);
            if (address == UINT64_MAX)
                st = MG_NOMEM;
        } else {
            address = ev_leaf(&b, t, &st);
        }
        if (st == MG_OK && !thm_memo_put(&memo, t, address))
            st = MG_NOMEM;
        if (t == root)
            root_address = address;
        --len;
    }
#undef PUSH_FRAME
    mg_sys_free(stack);
    mg_sys_free(memo.slots);
    if (st != MG_OK) {
        ev_builder_free(&b);
        memset(out, 0, sizeof(*out));
        return st;
    }
    st = ev_finish(&b, root_address, out);
    /* A lazy theorem's derivation has a lazy step somewhere (laziness
     * propagates to every conclusion drawn from a lazy premise). */
    out->lazy = root->lazy;
    return st;
}

/* A row source over evidence rows (address = index). */
static uint64_t ev_source_revision(void *context) {
    (void)context;
    return 0;
}

static MgStatus ev_source_read(void *context, MgTheory *theory, uint64_t address,
                               MgRow *out, const char **reason) {
    (void)theory;
    const MgEvidence *ev = context;
    for (uint32_t i = 0; i < ev->row_count; ++i)
        if (ev->addresses[i] == address) {
            *out = ev->rows[i];
            return MG_OK;
        }
    *reason = "no evidence row at this address";
    return MG_MALFORMED;
}

static void ev_source_release(void *context) { (void)context; }

MgStatus mg_evidence_check(MgTheory *theory, const MgEvidence *ev,
                           MgBudget *budget, const char **reason, MgThm **out) {
    *out = NULL;
    MgStatus st;
    MgSession *s = mg_session_begin_mode(theory,
        (MgRowSource){ev_source_revision, ev_source_read, ev_source_release,
                      (void *)ev}, ev->lazy, &st);
    if (!s)
        return st;
    MgSessionReport r;
    st = mg_session_resume(s, ev->addresses, ev->row_count, 0, budget, &r);
    if (st == MG_OK && r.status != MG_SESSION_FINISHED) {
        if (reason)
            *reason = r.reason;
        switch (r.status) {
        case MG_SESSION_REFUSED: st = MG_REFUSED; break;
        case MG_SESSION_EXHAUSTED: st = MG_EXHAUSTED; break;
        case MG_SESSION_INTERRUPTED: st = MG_INTERRUPTED; break;
        case MG_SESSION_NOMEM: st = MG_NOMEM; break;
        case MG_SESSION_CAPACITY: st = MG_CAPACITY; break;
        default: st = MG_MALFORMED; break;
        }
    }
    if (st == MG_OK)
        st = mg_session_theorem(s, ev->root, reason, out);
    mg_session_release(s);
    return st;
}

/* ── printers ─────────────────────────────────────────────────────────── */

enum { P_TP = 0, P_TM = 1, P_PF = 2, P_TEXT = 3 };

typedef struct {
    const void *node;
    uint8_t kind;
} PTask;

typedef struct {
    PTask *items;
    size_t len, cap;
    bool nomem;
} PStack;

static void ppush(PStack *st, const void *node, int kind) {
    if (st->nomem)
        return;
    if (st->len == st->cap) {
        size_t cap = st->cap ? st->cap * 2u : 128u;
        PTask *grown = mg_sys_realloc(st->items, sizeof(PTask) * cap);
        if (!grown) {
            st->nomem = true;
            return;
        }
        st->items = grown;
        st->cap = cap;
    }
    st->items[st->len++] = (PTask){node, (uint8_t)kind};
}

static void print_string(FILE *out, const char *text, size_t len) {
    fputc('"', out);
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '"' || text[i] == '\\')
            fputc('\\', out);
        fputc(text[i], out);
    }
    fputc('"', out);
}

static void print_name(FILE *out, const MgStore *s, uint32_t id) {
    const MgName *n = s ? mg_name_get(s, id) : NULL;
    if (n)
        print_string(out, n->text, n->len);
    else
        fputs("\"\"", out);
}

/* Push "(HEAD" now, then the children and the closing text in order. */
static void open_node(FILE *out, PStack *st, const char *head,
                      const void *a, int ka, const void *b, int kb) {
    fputc('(', out);
    fputs(head, out);
    ppush(st, ")", P_TEXT);
    if (b) {
        ppush(st, b, kb);
        ppush(st, " ", P_TEXT);
    }
    if (a) {
        ppush(st, a, ka);
        ppush(st, " ", P_TEXT);
    }
}

static bool print_any(FILE *out, const MgStore *s, const void *root, int kind) {
    PStack st = {0};
    ppush(&st, root, kind);
    while (st.len && !st.nomem) {
        PTask t = st.items[--st.len];
        switch (t.kind) {
        case P_TEXT:
            fputs(t.node, out);
            break;
        case P_TP: {
            const MgTp *x = t.node;
            switch (x->tag) {
            case MG_TP_VAR: fprintf(out, "(Tp.var %u)", x->n); break;
            case MG_TP_PROP: fputs("(Tp.prop)", out); break;
            case MG_TP_BASE: fprintf(out, "(Tp.base %u)", x->n); break;
            case MG_TP_ARR: open_node(out, &st, "Tp.arr", x->a, P_TP, x->b, P_TP); break;
            case MG_TP_ALL: open_node(out, &st, "Tp.all", x->a, P_TP, NULL, 0); break;
            }
            break;
        }
        case P_TM: {
            const MgTm *m = t.node;
            switch (m->tag) {
            case MG_TM_DB: fprintf(out, "(Tm.db %u)", m->n); break;
            case MG_TM_NAME:
                fputs("(Tm.named ", out);
                print_name(out, s, m->n);
                fputc(')', out);
                break;
            case MG_TM_PRIM: fprintf(out, "(Tm.prim %u)", m->n); break;
            case MG_TM_AP: open_node(out, &st, "Tm.app", m->f, P_TM, m->x, P_TM); break;
            case MG_TM_LAM: open_node(out, &st, "Tm.lam", m->tp, P_TP, m->f, P_TM); break;
            case MG_TM_IMP: open_node(out, &st, "Tm.imp", m->f, P_TM, m->x, P_TM); break;
            case MG_TM_ALL: open_node(out, &st, "Tm.all", m->tp, P_TP, m->f, P_TM); break;
            case MG_TM_TAP: open_node(out, &st, "Tm.typeApp", m->f, P_TM, m->tp, P_TP); break;
            case MG_TM_TLAM: open_node(out, &st, "Tm.typeLam", m->f, P_TM, NULL, 0); break;
            case MG_TM_TALL: open_node(out, &st, "Tm.typeAll", m->f, P_TM, NULL, 0); break;
            }
            break;
        }
        case P_PF: {
            const MgPf *p = t.node;
            switch (p->tag) {
            case MG_PF_GPA:
                fputs("(Pf.gpa ", out);
                print_name(out, s, p->n);
                fputc(')', out);
                break;
            case MG_PF_HYP: fprintf(out, "(Pf.hyp %u)", p->n); break;
            case MG_PF_KNOWN:
                fputs("(Pf.known ", out);
                print_name(out, s, p->n);
                fputc(')', out);
                break;
            case MG_PF_TMAP: open_node(out, &st, "Pf.termApp", p->p, P_PF, p->t, P_TM); break;
            case MG_PF_PFAP: open_node(out, &st, "Pf.proofApp", p->p, P_PF, p->q, P_PF); break;
            case MG_PF_PLAM: open_node(out, &st, "Pf.proofLam", p->t, P_TM, p->p, P_PF); break;
            case MG_PF_TLAM: open_node(out, &st, "Pf.termLam", p->tp, P_TP, p->p, P_PF); break;
            case MG_PF_TPAP: open_node(out, &st, "Pf.typeApp", p->p, P_PF, p->tp, P_TP); break;
            case MG_PF_TPLAM: open_node(out, &st, "Pf.typeLam", p->p, P_PF, NULL, 0); break;
            }
            break;
        }
        }
    }
    bool ok = !st.nomem;
    mg_sys_free(st.items);
    return ok;
}

bool mg_print_tp(FILE *out, const MgTp *t) { return print_any(out, NULL, t, P_TP); }
bool mg_print_tm(FILE *out, const MgStore *s, const MgTm *m) { return print_any(out, s, m, P_TM); }
bool mg_print_pf(FILE *out, const MgStore *s, const MgPf *p) { return print_any(out, s, p, P_PF); }

static bool print_context(FILE *out, const MgStore *s, const MgCtx *ctx) {
    bool ok = true;
    fprintf(out, "(Megalodon.context %u (types", ctx->depth);
    for (const MgTpList *l = ctx->terms; ok && l; l = l->tail) {
        fputc(' ', out);
        ok = mg_print_tp(out, l->head);
    }
    fputs(") (hypotheses", out);
    for (const MgTmList *l = ctx->hyps; ok && l; l = l->tail) {
        fputc(' ', out);
        ok = mg_print_tm(out, s, l->head);
    }
    fputs("))", out);
    return ok;
}

static bool print_witness(FILE *out, const MgStore *s, const MgWitness *w) {
    bool ok = true;
    switch (w->rule) {
    case MG_RULE_HYP: fprintf(out, "(Megalodon.hyp %u)", w->index); break;
    case MG_RULE_KNOWN:
        fputs("(Megalodon.known ", out);
        print_name(out, s, w->name);
        fputc(')', out);
        break;
    case MG_RULE_TERM_APP:
        fputs("(Megalodon.termApp ", out);
        ok = mg_print_tm(out, s, w->arg);
        fputc(' ', out);
        ok = ok && mg_print_tp(out, w->type);
        fputc(' ', out);
        ok = ok && mg_print_tm(out, s, w->body);
        fputc(')', out);
        break;
    case MG_RULE_PROOF_APP:
        fputs("(Megalodon.proofApp ", out);
        ok = mg_print_tm(out, s, w->domain);
        fputc(')', out);
        break;
    case MG_RULE_TERM_LAM: fputs("(Megalodon.termLam)", out); break;
    case MG_RULE_PROOF_LAM:
        fputs("(Megalodon.proofLam ", out);
        ok = mg_print_tm(out, s, w->source);
        fputc(')', out);
        break;
    case MG_RULE_TYPE_APP:
        fputs("(Megalodon.typeApp ", out);
        ok = mg_print_tp(out, w->type);
        fputc(' ', out);
        ok = ok && mg_print_tm(out, s, w->body);
        fputc(')', out);
        break;
    case MG_RULE_TYPE_LAM: fputs("(Megalodon.typeLam)", out); break;
    default: return false;
    }
    return ok;
}

bool mg_print_row(FILE *out, const MgStore *s, const MgRow *row) {
    fputs("(Megalodon.row (Megalodon.judgment ", out);
    bool ok = print_context(out, s, row->conclusion.ctx);
    fputc(' ', out);
    ok = ok && mg_print_tm(out, s, row->conclusion.prop);
    fputs(") ", out);
    ok = ok && print_witness(out, s, &row->witness);
    fputs(" (parents", out);
    for (uint32_t i = 0; i < row->parent_count; ++i)
        fprintf(out, " %llu", (unsigned long long)row->parents[i]);
    fprintf(out, ") %llu)", (unsigned long long)row->origin);
    return ok;
}

/* ── reading files ────────────────────────────────────────────────────── */

static char *read_file(const char *path, size_t *len, char *message, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(message, cap, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }
    size_t size = 0, alloc = 1u << 20;
    char *buffer = mg_sys_alloc(alloc);
    while (buffer) {
        if (size == alloc) {
            char *grown = mg_sys_realloc(buffer, alloc * 2u);
            if (!grown) {
                mg_sys_free(buffer);
                buffer = NULL;
                break;
            }
            buffer = grown;
            alloc *= 2u;
        }
        size_t got = fread(buffer + size, 1, alloc - size, f);
        size += got;
        if (got == 0u)
            break;
    }
    fclose(f);
    if (!buffer) {
        snprintf(message, cap, "out of memory reading %s", path);
        return NULL;
    }
    *len = size;
    return buffer;
}

/* ── upstream -sexprinfo import ───────────────────────────────────────── */

typedef struct {
    MgTheory *t;
    MgStore *s;
    const MgImportPolicy *policy;
    MgBudget *budget;
    MgImportReport *r;
    const char *origin;
    /* the theorem awaiting its proof */
    bool pending;
    char *pending_name;
    uint32_t pending_hash;
    uint32_t pending_levels;
    MgTm *pending_prop;
    uint64_t pending_line;
    /* names of theorems without authority (ADMITTED, QEDWITHADMITS, and
     * proofs citing one), open addressing over name + 1 */
    uint32_t *unproved;
    size_t unproved_cap, unproved_len;
} Importer;

static size_t unproved_slot(uint32_t name, size_t cap) {
    return (size_t)(((uint64_t)name + 1u) * UINT64_C(0x9e3779b97f4a7c15) >> 17) & (cap - 1u);
}

static bool unproved_has(const Importer *im, uint32_t name) {
    if (!im->unproved_cap)
        return false;
    for (size_t i = unproved_slot(name, im->unproved_cap);; i = (i + 1u) & (im->unproved_cap - 1u)) {
        if (!im->unproved[i])
            return false;
        if (im->unproved[i] == name + 1u)
            return true;
    }
}

static bool unproved_add(Importer *im, uint32_t name) {
    if (unproved_has(im, name))
        return true;
    if ((im->unproved_len + 1u) * 2u > im->unproved_cap) {
        size_t cap = im->unproved_cap ? im->unproved_cap * 2u : 64u;
        uint32_t *grown = mg_sys_alloc(sizeof(uint32_t) * cap);
        if (!grown)
            return false;
        memset(grown, 0, sizeof(uint32_t) * cap);
        for (size_t i = 0; i < im->unproved_cap; ++i) {
            uint32_t v = im->unproved[i];
            if (!v)
                continue;
            size_t j = unproved_slot(v - 1u, cap);
            while (grown[j])
                j = (j + 1u) & (cap - 1u);
            grown[j] = v;
        }
        mg_sys_free(im->unproved);
        im->unproved = grown;
        im->unproved_cap = cap;
    }
    size_t j = unproved_slot(name, im->unproved_cap);
    while (im->unproved[j])
        j = (j + 1u) & (im->unproved_cap - 1u);
    im->unproved[j] = name + 1u;
    ++im->unproved_len;
    return true;
}

static bool stop(Importer *im, uint64_t line, MgStatus st, const char *fmt,
                 const char *a, const char *b) {
    im->r->stopped = true;
    im->r->line = line;
    im->r->status = st;
    snprintf(im->r->message, sizeof im->r->message, fmt, a ? a : "", b ? b : "");
    return false;
}

static const char *sx_string(const MgSx *x) {
    return x && x->kind == MG_SX_STRING ? x->text : NULL;
}

static bool sx_nat(const MgSx *x, uint64_t *out) {
    if (!x || x->kind != MG_SX_INTEGER)
        return false;
    *out = x->value;
    return true;
}

static uint32_t intern(Importer *im, const char *text) {
    return text ? mg_name_intern(im->s, text, strlen(text)) : 0u;
}

/* The export names each object by its content identity; recompute it from
 * the decoded term and require agreement (PARAM hashes are references to
 * objects defined elsewhere and cannot be recomputed). */
static bool verify_identity(Importer *im, const char *what, const char *name,
                            const char *hash, MgTm *term, uint32_t levels, bool lam,
                            uint64_t line) {
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(im->t), im->budget, NULL);
    MgHash got, want;
    MgStatus st = mg_hash_identity(&c, term, levels, lam, &got);
    if (st != MG_OK)
        return stop(im, line, st, "%s %s: computing its identity", what, name);
    if (!mg_hash_parse(hash, strlen(hash), &want) || memcmp(got.b, want.b, 32) != 0) {
        char hex[65];
        mg_hash_hex(&got, hex);
        char label[160];
        snprintf(label, sizeof label, "%s %s", what, name);
        return stop(im, line, MG_REFUSED, "%s: identity mismatch (computed %s)", label, hex);
    }
    ++im->r->identities;
    return true;
}

static MgTp *poly_tp(MgStore *s, MgTp *t, uint32_t levels) {
    for (uint32_t i = 0; t && i < levels; ++i)
        t = mg_tp_all(s, t);
    return t;
}

static MgTm *poly_tm(MgStore *s, MgTm *m, uint32_t levels, bool lam) {
    for (uint32_t i = 0; m && i < levels; ++i)
        m = lam ? mg_tm_tlam(s, m) : mg_tm_tall(s, m);
    return m;
}

static MgPf *poly_pf(MgStore *s, MgPf *p, uint32_t levels) {
    for (uint32_t i = 0; p && i < levels; ++i)
        p = mg_pf_tplam(s, p);
    return p;
}

static const char *check_reason(MgTheory *t, const MgItem *item) {
    MgBudget b;
    mg_budget_init(&b, 0);
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(t), &b, NULL);
    mg_item_check(&c, item);
    return c.reason ? c.reason : "refused";
}

static bool admit_out(Importer *im, MgItem *item, uint64_t line, const char *what,
                      const char *source, const MgDecl **out) {
    item->origin = im->origin;
    item->line = line;
    item->source_name = source;
    MgStatus st = mg_theory_admit(im->t, im->budget, item, 0, out);
    if (st == MG_OK)
        return true;
    const char *why = (st == MG_REFUSED || st == MG_MALFORMED)
        ? check_reason(im->t, item) : mg_status_name(st);
    char label[256];
    snprintf(label, sizeof label, "%s %s", what, source ? source : "");
    return stop(im, line, st, "%s: %s", label, why);
}

static bool admit(Importer *im, MgItem *item, uint64_t line, const char *what,
                  const char *source) {
    return admit_out(im, item, line, what, source, NULL);
}

static bool axiom_accepted(const Importer *im, const char *name, const char *hash) {
    if (im->policy && im->policy->all_axioms)
        return true;
    for (uint32_t i = 0; im->policy && i < im->policy->axiom_count; ++i)
        if ((name && strcmp(im->policy->axioms[i], name) == 0) ||
            (hash && strcmp(im->policy->axioms[i], hash) == 0))
            return true;
    return false;
}

/* An already-declared name with this exact content: a repetition. */
static const MgDecl *existing(Importer *im, uint32_t name, MgNamespace ns) {
    return mg_env_lookup_quiet(mg_theory_snapshot(im->t), name, ns);
}

static uint32_t type_levels(const MgTp *t) {
    uint32_t n = 0;
    while (t && t->tag == MG_TP_ALL) {
        t = t->a;
        ++n;
    }
    return n;
}

static bool import_prim(Importer *im, const MgSx *x) {
    uint64_t j, line;
    const char *name = x->len == 6u ? sx_string(x->items[2]) : NULL;
    const char *hash = name ? sx_string(x->items[3]) : NULL;
    if (!hash || !sx_nat(x->items[1], &j) || !sx_nat(x->items[5], &line))
        return stop(im, x->line, MG_MALFORMED, "malformed PRIM%s%s", NULL, NULL);
    MgTheory *t = im->t;
    if (j >= t->prim_len || !t->prims[j])
        return stop(im, line, MG_REFUSED, "PRIM %s is not a primitive of the selected profile%s",
                    name, NULL);
    uint32_t levels = type_levels(t->prims[j]);
    MgTp *body = mg_sx_upstream_tp(im->s, x->items[4], levels);
    MgTp *type = poly_tp(im->s, body, levels);
    if (type != t->prims[j])
        return stop(im, line, MG_REFUSED,
                    "PRIM %s: the export's type disagrees with the selected profile%s",
                    name, NULL);
    if (!verify_identity(im, "PRIM", name, hash, mg_tm_prim(im->s, (uint32_t)j), 0, true, line))
        return false;
    uint32_t h = intern(im, hash);
    const MgDecl *d = existing(im, h, MG_NS_TERM);
    if (d) {
        if (d->type == type && d->def && d->def->tag == MG_TM_PRIM && d->def->n == j) {
            ++im->r->repeated;
            return true;
        }
        return stop(im, line, MG_CONFLICT, "PRIM %s: hash already names another term%s", name, NULL);
    }
    MgItem item = {.kind = MG_ITEM_DEFINITION, .name = h, .type = type,
                   .body = mg_tm_prim(im->s, (uint32_t)j)};
    if (!admit(im, &item, line, "PRIM", name))
        return false;
    ++im->r->primitives;
    return true;
}

static bool import_param(Importer *im, const MgSx *x) {
    uint64_t levels, line;
    const char *name = x->len == 6u ? sx_string(x->items[1]) : NULL;
    const char *hash = name ? sx_string(x->items[2]) : NULL;
    if (!hash || !sx_nat(x->items[3], &levels) || !sx_nat(x->items[5], &line) ||
        levels > 64u)
        return stop(im, x->line, MG_MALFORMED, "malformed PARAM%s%s", NULL, NULL);
    MgTp *type = poly_tp(im->s, mg_sx_upstream_tp(im->s, x->items[4], (uint32_t)levels),
                         (uint32_t)levels);
    if (!type)
        return stop(im, line, MG_MALFORMED, "PARAM %s: malformed type%s", name, NULL);
    uint32_t h = intern(im, hash);
    const MgDecl *d = existing(im, h, MG_NS_TERM);
    if (d) {
        if (d->type == type && !d->def) {
            ++im->r->repeated;
            return true;
        }
        return stop(im, line, MG_CONFLICT, "PARAM %s: hash already names another term%s", name, NULL);
    }
    MgStatus st = mg_theory_associate_parameter(im->t, h, type);
    if (st != MG_OK)
        return stop(im, line, st, "PARAM %s: %s", name, mg_status_name(st));
    MgItem item = {.kind = MG_ITEM_PARAMETER, .name = h, .type = type};
    if (!admit(im, &item, line, "PARAM", name))
        return false;
    ++im->r->parameters;
    return true;
}

static bool import_def(Importer *im, const MgSx *x) {
    uint64_t levels, line;
    const char *name = x->len == 7u ? sx_string(x->items[1]) : NULL;
    const char *hash = name ? sx_string(x->items[2]) : NULL;
    if (!hash || !sx_nat(x->items[3], &levels) || !sx_nat(x->items[6], &line) ||
        levels > 64u)
        return stop(im, x->line, MG_MALFORMED, "malformed DEF%s%s", NULL, NULL);
    uint32_t l = (uint32_t)levels;
    MgTp *type = poly_tp(im->s, mg_sx_upstream_tp(im->s, x->items[4], l), l);
    MgTm *body = poly_tm(im->s, mg_sx_upstream_tm(im->s, x->items[5], l), l, true);
    if (!type || !body)
        return stop(im, line, MG_MALFORMED, "DEF %s: malformed type or body%s", name, NULL);
    /* The front end admits non-normal bodies; native admission requires
     * the beta-eta normal form, which defines the same conversion (every
     * use normalizes after expansion, and normal forms are unique). */
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(im->t), im->budget, NULL);
    MgTm *normal = mg_tm_nf(&c, body);
    if (!normal)
        return stop(im, line, c.status == MG_OK ? MG_NOMEM : c.status,
                    "DEF %s: normalizing the body: %s", name,
                    c.reason ? c.reason : mg_status_name(c.status));
    if (!verify_identity(im, "DEF", name, hash, body, l, true, line))
        return false;
    uint32_t h = intern(im, hash);
    const MgDecl *d = existing(im, h, MG_NS_TERM);
    bool realizes = false;
    if (d) {
        /* The same identity is the same object, whatever its spelling. */
        if (d->type == type && d->def) {
            ++im->r->repeated;
            return true;
        }
        /* A parameter declared by content hash, now given the body that
         * hash names: the kernel verifies the identity. */
        realizes = d->kind == MG_DECL_PARAMETER && d->type == type;
        if (!realizes)
            return stop(im, line, MG_CONFLICT, "DEF %s: hash already names another term%s",
                        name, NULL);
    }
    MgItem item = {.kind = MG_ITEM_DEFINITION, .name = h, .type = type, .body = normal};
    const MgDecl *made = NULL;
    if (!admit_out(im, &item, line, "DEF", name, &made))
        return false;
    if (normal != body) {
        ((MgDecl *)made)->source_def = body;
        ++im->r->normalized;
    }
    if (realizes)
        ++im->r->realized;
    ++im->r->definitions;
    return true;
}

static bool import_axiom(Importer *im, const MgSx *x) {
    uint64_t levels, line;
    const char *name = x->len == 6u ? sx_string(x->items[1]) : NULL;
    const char *hash = name ? sx_string(x->items[2]) : NULL;
    if (!hash || !sx_nat(x->items[3], &levels) || !sx_nat(x->items[5], &line) ||
        levels > 64u)
        return stop(im, x->line, MG_MALFORMED, "malformed AXIOM%s%s", NULL, NULL);
    uint32_t l = (uint32_t)levels;
    MgTm *prop = poly_tm(im->s, mg_sx_upstream_tm(im->s, x->items[4], l), l, false);
    if (!prop)
        return stop(im, line, MG_MALFORMED, "AXIOM %s: malformed proposition%s", name, NULL);
    if (!verify_identity(im, "AXIOM", name, hash, prop, l, false, line))
        return false;
    uint32_t h = intern(im, hash);
    const MgDecl *d = existing(im, h, MG_NS_KNOWN);
    if (d) {
        ++im->r->repeated; /* the same proposition, already known */
        return true;
    }
    if (!axiom_accepted(im, name, hash))
        return stop(im, line, MG_REFUSED,
                    "AXIOM %s is not accepted by the import policy (hash %s)", name, hash);
    MgStatus st = mg_theory_associate_known(im->t, h, prop);
    if (st != MG_OK)
        return stop(im, line, st, "AXIOM %s: %s", name, mg_status_name(st));
    MgItem item = {.kind = MG_ITEM_KNOWN, .name = h, .prop = prop};
    if (!admit(im, &item, line, "AXIOM", name))
        return false;
    ++im->r->axioms;
    return true;
}

static void clear_pending(Importer *im) {
    mg_sys_free(im->pending_name);
    im->pending_name = NULL;
    im->pending = false;
}

static bool import_thm(Importer *im, const MgSx *x) {
    uint64_t levels, line;
    const char *name = x->len == 7u ? sx_string(x->items[1]) : NULL;
    const char *hash = name ? sx_string(x->items[2]) : NULL;
    if (!hash || !sx_nat(x->items[4], &levels) || !sx_nat(x->items[6], &line) ||
        levels > 64u)
        return stop(im, x->line, MG_MALFORMED, "malformed THM%s%s", NULL, NULL);
    if (im->pending)
        return stop(im, line, MG_MALFORMED, "THM %s: the previous theorem has no proof%s",
                    name, NULL);
    uint32_t l = (uint32_t)levels;
    MgTm *prop = poly_tm(im->s, mg_sx_upstream_tm(im->s, x->items[5], l), l, false);
    if (!prop)
        return stop(im, line, MG_MALFORMED, "THM %s: malformed proposition%s", name, NULL);
    if (!verify_identity(im, "THM", name, hash, prop, l, false, line))
        return false;
    size_t n = strlen(name);
    im->pending_name = mg_sys_alloc(n + 1u);
    if (!im->pending_name)
        return stop(im, line, MG_NOMEM, "THM %s: out of memory%s", name, NULL);
    memcpy(im->pending_name, name, n + 1u);
    im->pending = true;
    im->pending_hash = intern(im, hash);
    im->pending_levels = l;
    im->pending_prop = prop;
    im->pending_line = line;
    return true;
}

/* Whether a proof cites, as KNOWN, a theorem without authority that is not
 * known otherwise.  A walk over the distinct nodes of the proof DAG. */
static MgStatus cites_unproved(Importer *im, MgPf *root, bool *out) {
    *out = false;
    if (!im->unproved_len)
        return MG_OK;
    size_t cap = 256u, len = 0, scap = 64u, sl = 0;
    MgPf **seen = mg_sys_alloc(sizeof(MgPf *) * cap);
    MgPf **stack = mg_sys_alloc(sizeof(MgPf *) * scap);
    MgStatus st = MG_OK;
    if (!seen || !stack) {
        st = MG_NOMEM;
        goto done;
    }
    memset(seen, 0, sizeof(MgPf *) * cap);
    stack[sl++] = root;
    while (sl && !*out) {
        MgPf *p = stack[--sl];
        size_t h = (size_t)(((uintptr_t)p >> 4) * UINT64_C(0x9e3779b97f4a7c15) >> 20) & (cap - 1u);
        bool dup = false;
        while (seen[h]) {
            if (seen[h] == p) {
                dup = true;
                break;
            }
            h = (h + 1u) & (cap - 1u);
        }
        if (dup)
            continue;
        seen[h] = p;
        if (++len * 2u > cap) {
            size_t ncap = cap * 2u;
            MgPf **grown = mg_sys_alloc(sizeof(MgPf *) * ncap);
            if (!grown) {
                st = MG_NOMEM;
                goto done;
            }
            memset(grown, 0, sizeof(MgPf *) * ncap);
            for (size_t i = 0; i < cap; ++i) {
                if (!seen[i])
                    continue;
                size_t j = (size_t)(((uintptr_t)seen[i] >> 4) * UINT64_C(0x9e3779b97f4a7c15) >> 20) &
                           (ncap - 1u);
                while (grown[j])
                    j = (j + 1u) & (ncap - 1u);
                grown[j] = seen[i];
            }
            mg_sys_free(seen);
            seen = grown;
            cap = ncap;
        }
        if (p->tag == MG_PF_KNOWN && unproved_has(im, p->n) && !existing(im, p->n, MG_NS_KNOWN))
            *out = true;
        if (sl + 2u > scap) {
            MgPf **grown = mg_sys_realloc(stack, sizeof(MgPf *) * scap * 2u);
            if (!grown) {
                st = MG_NOMEM;
                goto done;
            }
            stack = grown;
            scap *= 2u;
        }
        if (p->p)
            stack[sl++] = p->p;
        if (p->q)
            stack[sl++] = p->q;
    }
done:
    mg_sys_free(seen);
    mg_sys_free(stack);
    return st;
}

static bool import_proof(Importer *im, const MgSx *x) {
    const char *name = x->len == 3u ? sx_string(x->items[1]) : NULL;
    if (!name || !im->pending || strcmp(name, im->pending_name) != 0)
        return stop(im, x->line, MG_MALFORMED, "PROOF %s does not follow its THM%s",
                    name, NULL);
    MgPf *proof = poly_pf(im->s, mg_sx_upstream_pf(im->s, x->items[2], im->pending_levels),
                          im->pending_levels);
    uint64_t line = im->pending_line;
    if (!proof)
        return stop(im, line, MG_MALFORMED, "PROOF %s: malformed proof%s", name, NULL);
    const MgDecl *d = existing(im, im->pending_hash, MG_NS_KNOWN);
    if (d) {
        /* A known proposition (same identity) proved again: check this
         * proof against the statement as written here, all the same. */
        MgCheck c;
        mg_check_init(&c, mg_theory_snapshot(im->t), im->budget, NULL);
        MgStatus st = mg_check_proposition(&c, 0, NULL, im->pending_prop)
            ? mg_check_theorem_proof(&c, im->pending_prop, proof)
            : (c.status == MG_OK ? MG_REFUSED : c.status);
        if (st != MG_OK)
            return stop(im, line, st, "THM %s (repeated): %s", name,
                        c.reason ? c.reason : "proof does not prove the stated proposition");
        ++im->r->reproved;
        return true;
    }
    bool dependent = false;
    MgStatus walk = cites_unproved(im, proof, &dependent);
    if (walk != MG_OK)
        return stop(im, line, walk, "THM %s: out of memory%s", name, NULL);
    if (dependent) {
        if (!unproved_add(im, im->pending_hash))
            return stop(im, line, MG_NOMEM, "THM %s: out of memory%s", name, NULL);
        ++im->r->dependent;
        return true;
    }
    MgItem item = {.kind = MG_ITEM_THEOREM, .name = im->pending_hash,
                   .prop = im->pending_prop, .proof = proof};
    if (!admit(im, &item, line, "THM", name))
        return false;
    ++im->r->theorems;
    return true;
}

/* MG_IMPORT_TRACE=1 prints each theorem's checking time and store growth to
 * stderr (a measurement aid; it changes nothing). */
static double trace_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

MgStatus mg_import_sexprinfo(MgTheory *t, const char *text, size_t len,
                             const char *origin, const MgImportPolicy *policy,
                             MgBudget *budget, MgImportReport *r) {
    memset(r, 0, sizeof(*r));
    const char *trace_env = getenv("MG_IMPORT_TRACE");
    bool trace = trace_env && trace_env[0] == '1';
    Importer im = {.t = t, .s = &t->store, .policy = policy, .budget = budget,
                   .r = r, .origin = origin};
    MgSxArena *arena = mg_sx_arena_new();
    if (!arena) {
        r->status = MG_NOMEM;
        return MG_NOMEM;
    }
    MgSxReader reader;
    mg_sx_reader_init(&reader, text, len);
    bool ok = true;
    while (ok) {
        mg_sx_arena_reset(arena);
        uint64_t before = t->store.resource_failures;
        uint64_t capacity_before = t->store.capacity_failures;
        MgSx *x = mg_sx_read(&reader, arena);
        if (!x) {
            if (reader.error)
                ok = stop(&im, reader.line,
                          strcmp(reader.error, "out of memory") == 0 ? MG_NOMEM : MG_MALFORMED,
                          "unreadable export: %s%s", reader.error, NULL);
            break;
        }
        if (x->kind != MG_SX_LIST || x->len == 0u || x->items[0]->kind != MG_SX_SYMBOL) {
            ++r->unsupported;
            ok = stop(&im, x->line, MG_MALFORMED, "not an export item: %s%s",
                      x->kind == MG_SX_SYMBOL ? x->text : "(value)", NULL);
            break;
        }
        const char *head = x->items[0]->text;
        if (strcmp(head, "PRIM") == 0) ok = import_prim(&im, x);
        else if (strcmp(head, "PARAM") == 0) ok = import_param(&im, x);
        else if (strcmp(head, "DEF") == 0) ok = import_def(&im, x);
        else if (strcmp(head, "AXIOM") == 0) ok = import_axiom(&im, x);
        else if (strcmp(head, "THM") == 0) ok = import_thm(&im, x);
        else if (strcmp(head, "PROOF") == 0) {
            double t0 = trace ? trace_now() : 0.0;
            uint64_t nodes0 = t->store.nodes;
            char *label = trace && im.pending_name ? im.pending_name : NULL;
            if (label)
                fprintf(stderr, "[mg] %s ...", label);
            ok = import_proof(&im, x);
            if (label)
                fprintf(stderr, " %s %.3fs +%llu nodes\n", ok ? "ok" : "stopped",
                        trace_now() - t0, (unsigned long long)(t->store.nodes - nodes0));
        }
        else if (strcmp(head, "QED") == 0) clear_pending(&im);
        else if (strcmp(head, "QEDWITHADMITS") == 0 || strcmp(head, "ADMITTED") == 0) {
            if (im.pending) {
                ++r->unproved;
                if (!unproved_add(&im, im.pending_hash))
                    ok = stop(&im, im.pending_line, MG_NOMEM, "THM %s: out of memory%s",
                              im.pending_name, NULL);
            }
            clear_pending(&im);
        } else if (strcmp(head, "DELTA") == 0 || strcmp(head, "USESKNOWN") == 0) {
            /* informational: the delta set and the knowns the front end
             * used; the kernel recomputes what it needs */
        } else {
            ++r->unsupported;
            ok = stop(&im, x->line, MG_MALFORMED, "unsupported export form %s%s", head, NULL);
        }
        /* A decoding failure caused by resources is undecided, not bad data. */
        if (!ok && (r->status == MG_REFUSED || r->status == MG_MALFORMED)) {
            if (t->store.capacity_failures != capacity_before)
                r->status = MG_CAPACITY;
            else if (t->store.resource_failures != before)
                r->status = MG_NOMEM;
        }
    }
    if (ok && im.pending) {
        ++r->unproved;
        ok = stop(&im, im.pending_line, MG_UNFINISHED, "THM %s has no proof%s",
                  im.pending_name, NULL);
    }
    clear_pending(&im);
    mg_sys_free(im.unproved);
    mg_sx_arena_free(arena);
    r->revision = t->rev;
    return ok ? MG_OK : (r->status ? r->status : MG_MALFORMED);
}

MgStatus mg_import_sexprinfo_file(MgTheory *t, const char *path,
                                  const MgImportPolicy *policy, MgBudget *budget,
                                  MgImportReport *r) {
    memset(r, 0, sizeof(*r));
    size_t len = 0;
    char *text = read_file(path, &len, r->message, sizeof r->message);
    if (!text) {
        r->stopped = true;
        r->status = MG_MALFORMED;
        return MG_MALFORMED;
    }
    MgStatus st = mg_import_sexprinfo(t, text, len, path, policy, budget, r);
    mg_sys_free(text);
    return st;
}

/* ── CeTTa-native libraries ───────────────────────────────────────────── */

static void print_provenance(FILE *out, const MgDecl *d) {
    if (d->source_name) {
        fputs(" (source ", out);
        print_string(out, d->source_name, strlen(d->source_name));
        fputc(')', out);
    }
    if (d->origin) {
        fputs(" (origin ", out);
        print_string(out, d->origin, strlen(d->origin));
        fprintf(out, " %llu)", (unsigned long long)d->line);
    }
}

MgStatus mg_library_save(MgTheory *t, FILE *out, const char **reason) {
    MgStore *s = &t->store;
    MgEnv env = mg_theory_snapshot(t);
    fputs("(Megalodon.library (format 1) (profile ", out);
    const char *profile = mg_profile_name(t->profile);
    print_string(out, profile, strlen(profile));
    fputs(") (label ", out);
    print_string(out, t->revision_label ? t->revision_label : "",
                 t->revision_label ? strlen(t->revision_label) : 0u);
    fputs("))\n", out);
    bool ok = true;
    for (uint64_t i = 0; ok && i < t->decl_len; ++i) {
        const MgDecl *d = t->decls[i];
        if (!mg_env_decl_visible(env, d))
            continue;
        switch (d->kind) {
        case MG_DECL_PARAMETER:
            fputs("(Megalodon.parameter ", out);
            print_name(out, s, d->name);
            fputc(' ', out);
            ok = mg_print_tp(out, d->type);
            break;
        case MG_DECL_DEFINITION:
            fputs("(Megalodon.definition ", out);
            print_name(out, s, d->name);
            fputc(' ', out);
            ok = mg_print_tp(out, d->type);
            fputc(' ', out);
            ok = ok && mg_print_tm(out, s, d->def);
            break;
        case MG_DECL_AXIOM:
            fputs("(Megalodon.axiom ", out);
            print_name(out, s, d->name);
            fputc(' ', out);
            ok = mg_print_tm(out, s, d->prop);
            break;
        case MG_DECL_THEOREM:
            fputs("(Megalodon.theorem ", out);
            print_name(out, s, d->name);
            fputc(' ', out);
            ok = mg_print_tm(out, s, d->prop);
            if (ok && d->proof) {
                fputs(" (tree ", out);
                ok = mg_print_pf(out, s, d->proof);
                fputc(')', out);
            } else if (ok && d->evidence) {
                fprintf(out, " (dag %llu", (unsigned long long)d->evidence->root);
                if (d->evidence->lazy)
                    fputs(" (conversion lazy)", out);
                for (uint32_t k = 0; ok && k < d->evidence->row_count; ++k) {
                    fprintf(out, "\n  (Megalodon.node %llu ",
                            (unsigned long long)d->evidence->addresses[k]);
                    ok = mg_print_row(out, s, &d->evidence->rows[k]);
                    fputc(')', out);
                }
                fputc(')', out);
            } else if (ok) {
                if (reason)
                    *reason = "a theorem without retained evidence";
                return MG_MALFORMED;
            }
            break;
        }
        print_provenance(out, d);
        fputs(")\n", out);
    }
    if (!ok) {
        if (reason)
            *reason = "out of memory while printing";
        return MG_NOMEM;
    }
    if (ferror(out)) {
        if (reason)
            *reason = "write error";
        return MG_DENIED;
    }
    return MG_OK;
}

MgStatus mg_library_save_file(MgTheory *t, const char *path, const char **reason) {
    /* Never replace an existing file: exclusive creation (C11 "x"). */
    FILE *out = fopen(path, "wbx");
    if (!out) {
        if (reason)
            *reason = errno == EEXIST ? "refusing to overwrite an existing file"
                                      : "cannot create the library file";
        return MG_DENIED;
    }
    MgStatus st = mg_library_save(t, out, reason);
    if (fclose(out) != 0 && st == MG_OK) {
        if (reason)
            *reason = "write error";
        st = MG_DENIED;
    }
    if (st != MG_OK)
        remove(path);
    return st;
}

typedef struct {
    MgTheory *t;
    MgStore *s;
    const MgImportPolicy *policy;
    MgBudget *budget;
    MgImportReport *r;
    const char *origin;
} Loader;

static const char *find_text(const MgSx *x, const char *key) {
    for (uint32_t i = 0; x && i < x->len; ++i) {
        const MgSx *e = x->items[i];
        if (mg_sx_head(e, key) && e->len >= 2u && e->items[1]->kind == MG_SX_STRING)
            return e->items[1]->text;
    }
    return NULL;
}

static uint64_t find_line(const MgSx *x) {
    for (uint32_t i = 0; x && i < x->len; ++i) {
        const MgSx *e = x->items[i];
        if (mg_sx_head(e, "origin") && e->len == 3u && e->items[2]->kind == MG_SX_INTEGER)
            return e->items[2]->value;
    }
    return 0;
}

static bool load_stop(Loader *ld, uint64_t line, MgStatus st, const char *what,
                      const char *name, const char *why) {
    ld->r->stopped = true;
    ld->r->line = line;
    ld->r->status = st;
    snprintf(ld->r->message, sizeof ld->r->message, "%s %s: %s", what,
             name ? name : "", why ? why : mg_status_name(st));
    return false;
}

static bool load_admit(Loader *ld, MgItem *item, const MgSx *x, const char *what,
                       const char *name) {
    item->source_name = find_text(x, "source");
    item->origin = find_text(x, "origin");
    item->line = find_line(x);
    MgStatus st = mg_theory_admit(ld->t, ld->budget, item, 0, NULL);
    if (st == MG_OK)
        return true;
    return load_stop(ld, x->line, st, what, name,
                     (st == MG_REFUSED || st == MG_MALFORMED) ? check_reason(ld->t, item)
                                                              : NULL);
}

static bool load_dag_theorem(Loader *ld, const MgSx *x, const char *name, uint32_t id,
                             MgTm *prop, const MgSx *dag) {
    uint64_t root;
    if (dag->len < 2u || dag->items[1]->kind != MG_SX_INTEGER)
        return load_stop(ld, x->line, MG_MALFORMED, "theorem", name, "malformed dag");
    root = dag->items[1]->value;
    /* (dag ROOT [(conversion lazy|exact)] NODE ...) */
    uint32_t first = 2u;
    MgEvidence ev = {0};
    if (dag->len > 2u && mg_sx_head(dag->items[2], "conversion")) {
        const MgSx *mode = dag->items[2];
        if (mode->len != 2u || !(mg_sx_is(mode->items[1], "lazy") || mg_sx_is(mode->items[1], "exact")))
            return load_stop(ld, x->line, MG_MALFORMED, "theorem", name, "malformed dag");
        ev.lazy = mg_sx_is(mode->items[1], "lazy");
        first = 3u;
    }
    uint32_t n = dag->len - first;
    ev.rows = mg_sys_alloc(sizeof(MgRow) * (n ? n : 1u));
    ev.addresses = mg_sys_alloc(sizeof(uint64_t) * (n ? n : 1u));
    uint64_t **parents = mg_sys_alloc(sizeof(uint64_t *) * (n ? n : 1u));
    bool ok = ev.rows && ev.addresses && parents;
    if (parents)
        memset(parents, 0, sizeof(uint64_t *) * (n ? n : 1u));
    const char *why = "malformed evidence row";
    MgStatus st = MG_MALFORMED;
    for (uint32_t i = 0; ok && i < n; ++i) {
        const MgSx *node = dag->items[i + first];
        uint32_t cap = 0;
        bool nomem = false;
        ok = mg_sx_head(node, "Megalodon.node") && node->len == 3u &&
             node->items[1]->kind == MG_SX_INTEGER &&
             mg_sx_wire_row(ld->s, node->items[2], &ev.rows[i], &parents[i], &cap, &nomem);
        if (ok)
            ev.addresses[i] = node->items[1]->value;
        if (nomem)
            st = MG_NOMEM;
    }
    ev.row_count = n;
    ev.root = root;
    MgThm *thm = NULL;
    if (ok) {
        why = NULL;
        st = mg_evidence_check(ld->t, &ev, ld->budget, &why, &thm);
    }
    if (st == MG_OK) {
        const MgDecl *d = NULL;
        st = mg_thm_publish(ld->t, thm, id, prop, ld->budget, &why, &d);
        if (st == MG_OK) {
            ((MgDecl *)d)->line = find_line(x);
            const char *src = find_text(x, "source");
            if (src) {
                size_t k = strlen(src);
                char *copy = mg_sys_alloc(k + 1u);
                if (copy) {
                    memcpy(copy, src, k + 1u);
                    ((MgDecl *)d)->source_name = copy;
                }
            }
        }
    }
    mg_thm_release(thm);
    for (uint32_t i = 0; parents && i < n; ++i)
        mg_sys_free(parents[i]);
    mg_sys_free(parents);
    mg_sys_free(ev.rows);
    mg_sys_free(ev.addresses);
    if (st != MG_OK)
        return load_stop(ld, x->line, st, "theorem", name, why);
    return true;
}

static bool load_item(Loader *ld, const MgSx *x) {
    const char *name = x->len >= 2u ? sx_string(x->items[1]) : NULL;
    uint32_t id = name ? mg_name_intern(ld->s, name, strlen(name)) : 0u;
    if (!id)
        return load_stop(ld, x->line, MG_MALFORMED, "item", NULL, "missing name");
    if (mg_sx_head(x, "Megalodon.parameter") && x->len >= 3u) {
        MgTp *type = mg_sx_wire_tp(ld->s, x->items[2]);
        if (!type)
            return load_stop(ld, x->line, MG_MALFORMED, "parameter", name, "type");
        MgStatus st = mg_theory_associate_parameter(ld->t, id, type);
        if (st != MG_OK)
            return load_stop(ld, x->line, st, "parameter", name, NULL);
        MgItem item = {.kind = MG_ITEM_PARAMETER, .name = id, .type = type};
        if (!load_admit(ld, &item, x, "parameter", name))
            return false;
        ++ld->r->parameters;
        return true;
    }
    if (mg_sx_head(x, "Megalodon.definition") && x->len >= 4u) {
        MgTp *type = mg_sx_wire_tp(ld->s, x->items[2]);
        MgTm *body = type ? mg_sx_wire_tm(ld->s, x->items[3]) : NULL;
        if (!body)
            return load_stop(ld, x->line, MG_MALFORMED, "definition", name, "type or body");
        MgItem item = {.kind = MG_ITEM_DEFINITION, .name = id, .type = type, .body = body};
        if (!load_admit(ld, &item, x, "definition", name))
            return false;
        if (body->tag == MG_TM_PRIM)
            ++ld->r->primitives;
        else
            ++ld->r->definitions;
        return true;
    }
    if (mg_sx_head(x, "Megalodon.axiom") && x->len >= 3u) {
        MgTm *prop = mg_sx_wire_tm(ld->s, x->items[2]);
        if (!prop)
            return load_stop(ld, x->line, MG_MALFORMED, "axiom", name, "proposition");
        const char *source = find_text(x, "source");
        bool accepted = ld->policy && ld->policy->all_axioms;
        for (uint32_t i = 0; !accepted && ld->policy && i < ld->policy->axiom_count; ++i)
            accepted = strcmp(ld->policy->axioms[i], name) == 0 ||
                       (source && strcmp(ld->policy->axioms[i], source) == 0);
        if (!accepted)
            return load_stop(ld, x->line, MG_REFUSED, "axiom", name,
                             "not accepted by the load policy");
        MgStatus st = mg_theory_associate_known(ld->t, id, prop);
        if (st != MG_OK)
            return load_stop(ld, x->line, st, "axiom", name, NULL);
        MgItem item = {.kind = MG_ITEM_KNOWN, .name = id, .prop = prop};
        if (!load_admit(ld, &item, x, "axiom", name))
            return false;
        ++ld->r->axioms;
        return true;
    }
    if (mg_sx_head(x, "Megalodon.theorem") && x->len >= 4u) {
        MgTm *prop = mg_sx_wire_tm(ld->s, x->items[2]);
        const MgSx *ev = x->items[3];
        if (!prop)
            return load_stop(ld, x->line, MG_MALFORMED, "theorem", name, "proposition");
        if (mg_sx_head(ev, "tree") && ev->len == 2u) {
            MgPf *proof = mg_sx_wire_pf(ld->s, ev->items[1]);
            if (!proof)
                return load_stop(ld, x->line, MG_MALFORMED, "theorem", name, "proof tree");
            MgItem item = {.kind = MG_ITEM_THEOREM, .name = id, .prop = prop, .proof = proof};
            if (!load_admit(ld, &item, x, "theorem", name))
                return false;
        } else if (mg_sx_head(ev, "dag")) {
            if (!load_dag_theorem(ld, x, name, id, prop, ev))
                return false;
        } else {
            return load_stop(ld, x->line, MG_MALFORMED, "theorem", name, "unknown evidence");
        }
        ++ld->r->theorems;
        return true;
    }
    ++ld->r->unsupported;
    return load_stop(ld, x->line, MG_MALFORMED, "unsupported library item", name, NULL);
}

MgStatus mg_library_load(MgTheory *t, const char *text, size_t len,
                         const char *origin, const MgImportPolicy *policy,
                         MgBudget *budget, MgImportReport *r) {
    memset(r, 0, sizeof(*r));
    Loader ld = {.t = t, .s = &t->store, .policy = policy, .budget = budget, .r = r,
                 .origin = origin};
    MgSxArena *arena = mg_sx_arena_new();
    if (!arena) {
        r->status = MG_NOMEM;
        return MG_NOMEM;
    }
    MgSxReader reader;
    mg_sx_reader_init(&reader, text, len);
    MgSx *header = mg_sx_read(&reader, arena);
    bool ok = true;
    if (!mg_sx_head(header, "Megalodon.library")) {
        ok = load_stop(&ld, 1, MG_MALFORMED, "library", NULL, "missing Megalodon.library header");
    } else {
        const char *profile = NULL;
        for (uint32_t i = 1; i < header->len; ++i)
            if (mg_sx_head(header->items[i], "profile") && header->items[i]->len == 2u)
                profile = sx_string(header->items[i]->items[1]);
        MgProfileTag tag;
        if (!profile || !mg_profile_from_name(profile, &tag) || tag != t->profile)
            ok = load_stop(&ld, header->line, MG_REFUSED, "library", profile,
                           "profile differs from the theory's selected profile");
    }
    while (ok) {
        mg_sx_arena_reset(arena);
        MgSx *x = mg_sx_read(&reader, arena);
        if (!x) {
            if (reader.error)
                ok = load_stop(&ld, reader.line, MG_MALFORMED, "library", NULL, reader.error);
            break;
        }
        ok = load_item(&ld, x);
    }
    mg_sx_arena_free(arena);
    r->revision = t->rev;
    return ok ? MG_OK : (r->status ? r->status : MG_MALFORMED);
}

MgStatus mg_library_load_file(MgTheory *t, const char *path,
                              const MgImportPolicy *policy, MgBudget *budget,
                              MgImportReport *r) {
    memset(r, 0, sizeof(*r));
    size_t len = 0;
    char *text = read_file(path, &len, r->message, sizeof r->message);
    if (!text) {
        r->stopped = true;
        r->status = MG_MALFORMED;
        return MG_MALFORMED;
    }
    MgStatus st = mg_library_load(t, text, len, path, policy, budget, r);
    mg_sys_free(text);
    return st;
}
