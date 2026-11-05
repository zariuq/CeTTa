#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_core.h"

#include <stdlib.h>
#include <string.h>

/* Under AddressSanitizer, store memory not handed out (a chunk's tail, the
 * nodes a release returns) is poisoned, so a read of a released node is
 * reported. */
#if defined(__SANITIZE_ADDRESS__)
#define MG_STORE_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define MG_STORE_ASAN 1
#endif
#endif
#ifdef MG_STORE_ASAN
#include <sanitizer/asan_interface.h>
#define MG_POISON(p, n) ASAN_POISON_MEMORY_REGION((p), (n))
#define MG_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define MG_POISON(p, n) ((void)(p), (void)(n))
#define MG_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

/*
 * Interning store.  Every constructor returns the canonical node for its
 * structure: equality of nodes is pointer equality, decided at interning
 * time by comparing the tag, scalars and (already canonical) children.
 * Derived facts are computed once, here, from the children: they are never
 * read from input data.
 */

/* ── system allocation with test injection ───────────────────────────── */

#ifdef MG_TEST_HOOKS
static uint64_t g_mg_alloc_count;
static uint64_t g_mg_alloc_fail_after = UINT64_MAX;
void mg_test_fail_allocation_after(uint64_t successful) {
    g_mg_alloc_count = 0;
    g_mg_alloc_fail_after = successful;
}
uint64_t mg_test_allocation_count(void) { return g_mg_alloc_count; }
static bool mg_alloc_injected_failure(void) {
    if (g_mg_alloc_count >= g_mg_alloc_fail_after)
        return true;
    ++g_mg_alloc_count;
    return false;
}
#else
static inline bool mg_alloc_injected_failure(void) { return false; }
#endif

void *mg_sys_alloc(size_t size) {
    if (mg_alloc_injected_failure())
        return NULL;
    return malloc(size ? size : 1u);
}

void *mg_sys_realloc(void *pointer, size_t size) {
    if (mg_alloc_injected_failure())
        return NULL;
    return realloc(pointer, size ? size : 1u);
}

void mg_sys_free(void *pointer) { free(pointer); }

/* ── chunks ───────────────────────────────────────────────────────────── */

struct MgChunk {
    MgChunk *next;
    size_t used;
    size_t capacity;
    _Alignas(16) unsigned char data[];
};

#define MG_CHUNK_BYTES (1u << 20)

void *mg_store_alloc(MgStore *s, size_t size) {
    size = (size + 15u) & ~(size_t)15u;
    MgChunk *chunk = s->chunks;
    if (!chunk || chunk->capacity - chunk->used < size) {
        size_t capacity = size > MG_CHUNK_BYTES ? size : MG_CHUNK_BYTES;
        MgChunk *fresh = mg_sys_alloc(sizeof(MgChunk) + capacity);
        if (!fresh) {
            ++s->resource_failures;
            return NULL;
        }
        fresh->next = s->chunks;
        fresh->used = 0;
        fresh->capacity = capacity;
        MG_POISON(fresh->data, capacity);
        s->chunks = fresh;
        s->bytes += sizeof(MgChunk) + capacity;
        chunk = fresh;
    }
    void *p = chunk->data + chunk->used;
    chunk->used += size;
    MG_UNPOISON(p, size);
    memset(p, 0, size);
    return p;
}

/* ── hashing ──────────────────────────────────────────────────────────── */

static inline uint64_t mg_mix64(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static inline uint32_t mg_hash4(uint64_t a, uint64_t b, uint64_t c,
                                uint64_t d) {
    uint64_t h = mg_mix64(a + UINT64_C(0x9e3779b97f4a7c15));
    h = mg_mix64(h ^ b);
    h = mg_mix64(h ^ c);
    h = mg_mix64(h ^ d);
    return (uint32_t)(h ^ (h >> 32));
}

#define PTR(p) ((uint64_t)(uintptr_t)(p))

/* ── intern tables (open addressing, linear probing) ──────────────────── */

static bool table_init(MgInternTable *t, uint32_t cap) {
    t->slots = mg_sys_alloc(sizeof(MgInternSlot) * cap);
    if (!t->slots)
        return false;
    memset(t->slots, 0, sizeof(MgInternSlot) * cap);
    t->cap = cap;
    t->len = 0;
    return true;
}

static void table_free(MgInternTable *t) {
    mg_sys_free(t->slots);
    t->slots = NULL;
    t->cap = t->len = 0;
}

static bool table_grow_slow(MgInternTable *t) {
    if (t->cap > (UINT32_MAX >> 1))
        return false;
    uint32_t cap = t->cap * 2u;
    MgInternSlot *slots = mg_sys_alloc(sizeof(MgInternSlot) * cap);
    if (!slots)
        return false;
    memset(slots, 0, sizeof(MgInternSlot) * cap);
    for (uint32_t i = 0; i < t->cap; ++i) {
        if (!t->slots[i].node)
            continue;
        uint32_t j = t->slots[i].hash & (cap - 1u);
        while (slots[j].node)
            j = (j + 1u) & (cap - 1u);
        slots[j] = t->slots[i];
    }
    mg_sys_free(t->slots);
    t->slots = slots;
    t->cap = cap;
    return true;
}

/* Room for one more node; a failure is counted on the store. */
#define table_grow(t) \
    (((uint64_t)((t)->len + 1u) * 10u <= (uint64_t)(t)->cap * 7u) || \
     table_grow_slow(t) || (++s->resource_failures, false))

/* ── marks: the logs ──────────────────────────────────────────────────── */

typedef struct {
    MgInternTable *table;
    uint32_t slot;
} MgInterned;

typedef struct {
    void *field;
    uint64_t old;
} MgCacheWrite;

struct MgStoreLog {
    MgInterned *interned;
    uint32_t interned_len, interned_cap;
    MgCacheWrite *cached;
    uint32_t cached_len, cached_cap;
    bool lost;               /* an entry could not be logged: no release until the marks close */
};

static bool log_room(void **items, uint32_t *cap, uint32_t len, size_t size) {
    if (len < *cap)
        return true;
    if (*cap > (UINT32_MAX >> 1))
        return false;
    uint32_t ncap = *cap ? *cap * 2u : 1024u;
    void *grown = mg_sys_realloc(*items, size * ncap);
    if (!grown)
        return false;
    *items = grown;
    *cap = ncap;
    return true;
}

static void log_interned(MgStore *s, MgInternTable *t, uint32_t slot) {
    struct MgStoreLog *l = s->log;
    if (!log_room((void **)&l->interned, &l->interned_cap, l->interned_len, sizeof *l->interned)) {
        l->lost = true;
        return;
    }
    l->interned[l->interned_len++] = (MgInterned){t, slot};
}

void mg_store_log_cache(MgStore *s, void *field) {
    struct MgStoreLog *l = s->log;
    if (!log_room((void **)&l->cached, &l->cached_cap, l->cached_len, sizeof *l->cached)) {
        l->lost = true;
        return;
    }
    MgCacheWrite w = {field, 0};
    memcpy(&w.old, field, sizeof w.old);
    l->cached[l->cached_len++] = w;
}

/* Names live for the store: their table is never logged. */
static void table_insert_at(MgStore *s, MgInternTable *t, uint32_t slot, void *node, uint32_t hash) {
    t->slots[slot] = (MgInternSlot){node, hash};
    ++t->len;
    if (s->marks && t != &s->name_index)
        log_interned(s, t, slot);
}

/* ── store lifecycle ──────────────────────────────────────────────────── */


bool mg_store_init(MgStore *s) {
    memset(s, 0, sizeof(*s));
    if (!table_init(&s->tps, 1024u) || !table_init(&s->tms, 4096u) ||
        !table_init(&s->pfs, 1024u) || !table_init(&s->tplists, 256u) ||
        !table_init(&s->tmlists, 256u) || !table_init(&s->ctxs, 256u) ||
        !table_init(&s->name_index, 256u)) {
        mg_store_free(s);
        return false;
    }
    s->names = mg_sys_alloc(sizeof(MgName *) * 64u);
    if (!s->names) {
        mg_store_free(s);
        return false;
    }
    s->name_cap = 64u;
    s->name_len = 1u; /* id 0 is "no name" */
    s->names[0] = NULL;
    s->tp_prop = mg_tp_prop(s);
    s->ctx_empty = s->tp_prop ? mg_ctx(s, 0, NULL, NULL) : NULL;
    if (!s->tp_prop || !s->ctx_empty) {
        mg_store_free(s);
        return false;
    }
    return true;
}

void mg_store_free(MgStore *s) {
    if (!s)
        return;
    table_free(&s->tps);
    table_free(&s->tms);
    table_free(&s->pfs);
    table_free(&s->tplists);
    table_free(&s->tmlists);
    table_free(&s->ctxs);
    table_free(&s->name_index);
    for (uint32_t i = 1; s->names && i < s->name_len; ++i)
        mg_sys_free(s->names[i]);
    mg_sys_free(s->names);
    s->names = NULL;
    mg_sys_free(s->subst_memo);
    s->subst_memo = NULL;
    if (s->log) {
        mg_sys_free(s->log->interned);
        mg_sys_free(s->log->cached);
        mg_sys_free(s->log);
        s->log = NULL;
    }
    MgChunk *chunk = s->chunks;
    while (chunk) {
        MgChunk *next = chunk->next;
        MG_UNPOISON(chunk->data, chunk->capacity);
        mg_sys_free(chunk);
        chunk = next;
    }
    s->chunks = NULL;
}

/* ── marks ────────────────────────────────────────────────────────────── */

static MgInternTable *node_table(MgStore *s, int k) {
    MgInternTable *tables[6] = {&s->tps, &s->tms, &s->pfs, &s->tplists, &s->tmlists, &s->ctxs};
    return tables[k];
}

bool mg_store_mark(MgStore *s, MgStoreMark *m) {
    memset(m, 0, sizeof *m);
    if (!s->log) {
        s->log = mg_sys_alloc(sizeof *s->log);
        if (!s->log)
            return false;
        memset(s->log, 0, sizeof *s->log);
    }
    m->chunk = s->chunks;
    m->used = s->chunks ? s->chunks->used : 0u;
    m->bytes = s->bytes;
    m->nodes = s->nodes;
    m->interned = s->log->interned_len;
    m->cached = s->log->cached_len;
    for (int k = 0; k < 6; ++k)
        m->caps[k] = node_table(s, k)->cap;
    m->open = true;
    ++s->marks;
    return true;
}

static void marks_close(MgStore *s, MgStoreMark *m) {
    m->open = false;
    if (--s->marks == 0u) {
        s->log->interned_len = s->log->cached_len = 0u;
        s->log->lost = false;
    }
}

void mg_store_keep(MgStore *s, MgStoreMark *m) {
    if (m->open)
        marks_close(s, m);
}

/* The memory a release returns: the chunks made since the mark, and the
 * marked chunk's tail, as address intervals.  ISO C orders pointers only
 * within one object, and these belong to separate allocations, so they are
 * compared as integer addresses: the store assumes a flat address space, in
 * which uintptr_t keeps the order of addresses (the platforms CeTTa
 * supports). */
typedef struct {
    uintptr_t lo, hi;
} MgRange;

static MgRange address_range(const void *start, size_t len) {
    uintptr_t lo = (uintptr_t)start;
    return (MgRange){lo, lo + len};
}

static int range_cmp(const void *a, const void *b) {
    const MgRange *x = a, *y = b;
    return x->lo < y->lo ? -1 : x->lo > y->lo ? 1 : 0;
}

static bool released(const MgRange *r, size_t n, const void *p) {
    uintptr_t q = (uintptr_t)p;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (q < r[mid].lo)
            hi = mid;
        else if (q >= r[mid].hi)
            lo = mid + 1u;
        else
            return true;
    }
    return false;
}

/* A table that grew inside the mark: its nodes that stay, put in SLOTS (as
 * many as it has, all empty). */
static void table_refill(MgInternTable *t, MgInternSlot *slots, const MgRange *r, size_t n) {
    uint32_t len = 0;
    for (uint32_t i = 0; i < t->cap; ++i) {
        void *node = t->slots[i].node;
        if (!node || released(r, n, node))
            continue;
        uint32_t j = t->slots[i].hash & (t->cap - 1u);
        while (slots[j].node)
            j = (j + 1u) & (t->cap - 1u);
        slots[j] = t->slots[i];
        ++len;
    }
    mg_sys_free(t->slots);
    t->slots = slots;
    t->len = len;
}

/* The memory a release of M would return, sorted: NULL when the mark could
 * not log everything, or without room. */
static MgRange *release_ranges(MgStore *s, const MgStoreMark *m, size_t *count) {
    size_t n = 1u;
    for (MgChunk *c = s->chunks; c && c != m->chunk; c = c->next)
        ++n;
    MgRange *r = s->log->lost ? NULL : mg_sys_alloc(sizeof *r * n);
    if (!r)
        return NULL;
    size_t k = 0;
    for (MgChunk *c = s->chunks; c && c != m->chunk; c = c->next)
        r[k++] = address_range(c->data, c->capacity);
    if (m->chunk)
        r[k++] = address_range(m->chunk->data + m->used, m->chunk->capacity - m->used);
    qsort(r, k, sizeof *r, range_cmp);
    *count = k;
    return r;
}

/* Whole or not at all: everything a release needs is allocated before
 * anything changes, and without it the nodes stay, which is always sound.
 * True when the release happened. */
bool mg_store_release(MgStore *s, MgStoreMark *m) {
    if (!m->open)
        return false;
    struct MgStoreLog *l = s->log;
    size_t k = 0;
    MgRange *r = release_ranges(s, m, &k);
    MgInternSlot *fresh[6] = {0};
    bool ok = r != NULL;
    for (int t = 0; t < 6 && ok; ++t) {
        MgInternTable *tab = node_table(s, t);
        if (tab->cap != m->caps[t]) {
            fresh[t] = mg_sys_alloc(sizeof(MgInternSlot) * tab->cap);
            ok = fresh[t] != NULL;
            if (ok)
                memset(fresh[t], 0, sizeof(MgInternSlot) * tab->cap);
        }
    }
    if (!ok) {
        for (int t = 0; t < 6; ++t)
            mg_sys_free(fresh[t]);
        mg_sys_free(r);
        marks_close(s, m);
        return false;
    }
    /* caches first, newest write first: each field outside the released
     * memory gets back the value it had at the mark */
    for (uint32_t i = l->cached_len; i-- > m->cached;)
        if (!released(r, k, l->cached[i].field))
            memcpy(l->cached[i].field, &l->cached[i].old, sizeof l->cached[i].old);
    /* interning, newest first: in a linear-probing table that only grew by
     * insertion, emptying the slots in reverse order of insertion gives back
     * the table at the mark; a table that grew is refilled instead */
    for (uint32_t i = l->interned_len; i-- > m->interned;) {
        MgInternTable *t = l->interned[i].table;
        int which = 0;
        while (which < 6 && node_table(s, which) != t)
            ++which;
        if (which == 6 || fresh[which])
            continue;
        t->slots[l->interned[i].slot] = (MgInternSlot){NULL, 0u};
        --t->len;
    }
    for (int t = 0; t < 6; ++t)
        if (fresh[t])
            table_refill(node_table(s, t), fresh[t], r, k);
    for (uint32_t i = 0; i < MG_DB_SMALL; ++i)
        if (s->db_small[i] && released(r, k, s->db_small[i]))
            s->db_small[i] = NULL;
    mg_sys_free(r);
    while (s->chunks && s->chunks != m->chunk) {
        MgChunk *next = s->chunks->next;
        MG_UNPOISON(s->chunks->data, s->chunks->capacity);
        mg_sys_free(s->chunks);
        s->chunks = next;
    }
    if (m->chunk) {
        MG_POISON(m->chunk->data + m->used, m->chunk->capacity - m->used);
        m->chunk->used = m->used;
    }
    s->bytes = m->bytes;
    s->nodes = m->nodes;
    l->interned_len = m->interned;
    l->cached_len = m->cached;
    /* every substitution memo entry may name a released node */
    ++s->memo_epoch;
    marks_close(s, m);
    return true;
}

/* ── a release that keeps a proof ─────────────────────────────────────── */

enum { K_TP = 1, K_TM, K_PF };

/* A child of a copied node: NULL, a node that stays, or a copied one (its
 * index + 1). */
typedef struct {
    const void *stays;
    uint32_t copy;
} MgRef;

/* A node made since the mark, as the constructors take it. */
typedef struct {
    uint8_t kind, tag;
    uint32_t n;
    MgRef kid[4];          /* TP: a b; TM: f x tp; PF: p q t tp */
} MgCopy;

typedef struct {
    MgCopy *copies;
    uint32_t len, cap;
    const void **seen;     /* open addressing: node -> copy index + 1 */
    uint32_t *at;
    uint32_t seen_cap, seen_len;
    const MgRange *r;
    size_t nr;
    bool failed;
} MgCopier;

static uint32_t seen_slot(const MgCopier *c, const void *node) {
    uint64_t h = PTR(node) * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(h >> 32) & (c->seen_cap - 1u);
}

static uint32_t seen_get(const MgCopier *c, const void *node) {
    if (!c->seen_cap)
        return 0u;
    for (uint32_t i = seen_slot(c, node); c->seen[i]; i = (i + 1u) & (c->seen_cap - 1u))
        if (c->seen[i] == node)
            return c->at[i];
    return 0u;
}

static bool seen_put(MgCopier *c, const void *node, uint32_t index) {
    if ((c->seen_len + 1u) * 2u > c->seen_cap) {
        uint32_t cap = c->seen_cap ? c->seen_cap * 2u : 1024u;
        const void **seen = mg_sys_alloc(sizeof *seen * cap);
        uint32_t *at = mg_sys_alloc(sizeof *at * cap);
        if (!seen || !at) {
            mg_sys_free(seen);
            mg_sys_free(at);
            return false;
        }
        memset(seen, 0, sizeof *seen * cap);
        const void **old = c->seen;
        uint32_t *old_at = c->at, old_cap = c->seen_cap;
        c->seen = seen;
        c->at = at;
        c->seen_cap = cap;
        for (uint32_t i = 0; i < old_cap; ++i)
            if (old[i]) {
                uint32_t j = seen_slot(c, old[i]);
                while (c->seen[j])
                    j = (j + 1u) & (cap - 1u);
                c->seen[j] = old[i];
                c->at[j] = old_at[i];
            }
        mg_sys_free(old);
        mg_sys_free(old_at);
    }
    uint32_t i = seen_slot(c, node);
    while (c->seen[i])
        i = (i + 1u) & (c->seen_cap - 1u);
    c->seen[i] = node;
    c->at[i] = index;
    ++c->seen_len;
    return true;
}

/* NODE's copy, its children first (a node made since the mark), or the node
 * itself as one that stays; iterative, so a deep proof takes no C stack. */
static MgRef copy_node(MgCopier *c, uint8_t kind, const void *node) {
    if (!node || c->failed || !released(c->r, c->nr, node))
        return (MgRef){node, 0u};
    uint32_t done = seen_get(c, node);
    if (done)
        return (MgRef){NULL, done};
    typedef struct {
        uint8_t kind;
        const void *node;
        uint8_t next;          /* the next child to copy */
        MgCopy copy;
    } Frame;
    Frame *st = NULL;
    uint32_t len = 0, cap = 0;
    MgRef out = {NULL, 0u};
#define COPY_PUSH(k_, n_) do {         if (len == cap) {             uint32_t ncap = cap ? cap * 2u : 64u;             Frame *g = mg_sys_realloc(st, sizeof *g * ncap);             if (!g) { c->failed = true; goto done; }             st = g;             cap = ncap;         }         st[len++] = (Frame){(k_), (n_), 0u, {0}};     } while (0)
    COPY_PUSH(kind, node);
    while (len) {
        Frame *f = &st[len - 1u];
        const void *kids[4] = {0};
        uint8_t kinds[4] = {0}, nk = 0;
        if (f->kind == K_TP) {
            const MgTp *t = f->node;
            f->copy.tag = t->tag;
            f->copy.n = t->n;
            kids[0] = t->a; kinds[0] = K_TP;
            kids[1] = t->b; kinds[1] = K_TP;
            nk = 2;
        } else if (f->kind == K_TM) {
            const MgTm *t = f->node;
            f->copy.tag = t->tag;
            f->copy.n = t->n;
            kids[0] = t->f; kinds[0] = K_TM;
            kids[1] = t->x; kinds[1] = K_TM;
            kids[2] = t->tp; kinds[2] = K_TP;
            nk = 3;
        } else {
            const MgPf *t = f->node;
            f->copy.tag = t->tag;
            f->copy.n = t->n;
            kids[0] = t->p; kinds[0] = K_PF;
            kids[1] = t->q; kinds[1] = K_PF;
            kids[2] = t->t; kinds[2] = K_TM;
            kids[3] = t->tp; kinds[3] = K_TP;
            nk = 4;
        }
        f->copy.kind = f->kind;
        if (f->next < nk) {
            const void *kid = kids[f->next];
            uint32_t seen = kid ? seen_get(c, kid) : 0u;
            if (!kid || !released(c->r, c->nr, kid)) {
                f->copy.kid[f->next++] = (MgRef){kid, 0u};
            } else if (seen) {
                f->copy.kid[f->next++] = (MgRef){NULL, seen};
            } else {
                uint8_t kk = kinds[f->next];
                COPY_PUSH(kk, kid);
            }
            continue;
        }
        /* every child copied: the node's own copy */
        if (c->len == c->cap) {
            uint32_t ncap = c->cap ? c->cap * 2u : 256u;
            MgCopy *g = mg_sys_realloc(c->copies, sizeof *g * ncap);
            if (!g) {
                c->failed = true;
                goto done;
            }
            c->copies = g;
            c->cap = ncap;
        }
        c->copies[c->len++] = f->copy;
        uint32_t index = c->len;
        if (!seen_put(c, f->node, index)) {
            c->failed = true;
            goto done;
        }
        --len;
        if (len)
            st[len - 1u].copy.kid[st[len - 1u].next++] = (MgRef){NULL, index};
        else
            out = (MgRef){NULL, index};
    }
done:
#undef COPY_PUSH
    mg_sys_free(st);
    return out;
}

static const void *ref_node(void *const *made, MgRef r) { return r.copy ? made[r.copy - 1u] : r.stays; }

/* The copy interned again; NULL without room (or a node outside the
 * kinds a proof is made of). */
static void *remake(MgStore *s, void *const *made, const MgCopy *c) {
    const void *k0 = ref_node(made, c->kid[0]), *k1 = ref_node(made, c->kid[1]),
               *k2 = ref_node(made, c->kid[2]), *k3 = ref_node(made, c->kid[3]);
    if (c->kind == K_TP) {
        MgTp *a = (MgTp *)k0, *b = (MgTp *)k1;
        switch (c->tag) {
        case MG_TP_VAR: return mg_tp_var(s, c->n);
        case MG_TP_PROP: return mg_tp_prop(s);
        case MG_TP_BASE: return mg_tp_base(s, c->n);
        case MG_TP_ARR: return a && b ? mg_tp_arr(s, a, b) : NULL;
        case MG_TP_ALL: return a ? mg_tp_all(s, a) : NULL;
        default: return NULL;
        }
    }
    if (c->kind == K_TM) {
        MgTm *f = (MgTm *)k0, *x = (MgTm *)k1;
        MgTp *tp = (MgTp *)k2;
        switch (c->tag) {
        case MG_TM_DB: return mg_tm_db(s, c->n);
        case MG_TM_NAME: return mg_tm_name(s, c->n);
        case MG_TM_PRIM: return mg_tm_prim(s, c->n);
        case MG_TM_AP: return f && x ? mg_tm_ap(s, f, x) : NULL;
        case MG_TM_LAM: return tp && f ? mg_tm_lam(s, tp, f) : NULL;
        case MG_TM_IMP: return f && x ? mg_tm_imp(s, f, x) : NULL;
        case MG_TM_ALL: return tp && f ? mg_tm_all(s, tp, f) : NULL;
        case MG_TM_TAP: return f && tp ? mg_tm_tap(s, f, tp) : NULL;
        case MG_TM_TLAM: return f ? mg_tm_tlam(s, f) : NULL;
        case MG_TM_TALL: return f ? mg_tm_tall(s, f) : NULL;
        default: return NULL;
        }
    }
    MgPf *p = (MgPf *)k0, *q = (MgPf *)k1;
    MgTm *t = (MgTm *)k2;
    MgTp *tp = (MgTp *)k3;
    switch (c->tag) {
    case MG_PF_GPA: return mg_pf_gpa(s, c->n);
    case MG_PF_HYP: return mg_pf_hyp(s, c->n);
    case MG_PF_KNOWN: return mg_pf_known(s, c->n);
    case MG_PF_TMAP: return mg_pf_tmap(s, p, t);
    case MG_PF_PFAP: return mg_pf_pfap(s, p, q);
    case MG_PF_PLAM: return mg_pf_plam(s, t, p);
    case MG_PF_TLAM: return mg_pf_tlam(s, tp, p);
    case MG_PF_TPAP: return mg_pf_tpap(s, p, tp);
    case MG_PF_TPLAM: return mg_pf_tplam(s, p);
    default: return NULL;
    }
}

bool mg_store_release_keeping(MgStore *s, MgStoreMark *m, MgPf **proof) {
    if (!m->open)
        return false;
    size_t nr = 0;
    MgRange *r = release_ranges(s, m, &nr);
    if (!r) {
        marks_close(s, m);
        return false;
    }
    MgCopier c = {.r = r, .nr = nr};
    MgRef root = copy_node(&c, K_PF, *proof);
    void **made = c.failed ? NULL : mg_sys_alloc(sizeof *made * (c.len ? c.len : 1u));
    mg_sys_free(r);
    bool ok = made && mg_store_release(s, m);
    if (!made)
        marks_close(s, m);
    if (ok) {
        /* the proof made again, children first, as the canonical nodes of
         * the same structure */
        for (uint32_t i = 0; i < c.len && made; ++i)
            if (!(made[i] = remake(s, made, &c.copies[i]))) {
                *proof = NULL;
                break;
            }
        if (*proof)
            *proof = (MgPf *)ref_node(made, root);
    }
    mg_sys_free(made);
    mg_sys_free(c.copies);
    mg_sys_free(c.seen);
    mg_sys_free(c.at);
    return ok;
}

/* ── types ────────────────────────────────────────────────────────────── */

#define MASK_EXACT_BIT UINT64_C(0x8000000000000000)
/* The masks hold indices 0..62; bit 63 marks "every free index is below
 * 63", making the mask an exact set. */
static inline uint64_t mask_singleton(uint32_t k) {
    return k < 63u ? (UINT64_C(1) << k) | MASK_EXACT_BIT : 0u;
}
static inline uint64_t mask_union(uint64_t a, uint64_t b) {
    uint64_t exact = a & b & MASK_EXACT_BIT;
    return ((a | b) & ~MASK_EXACT_BIT) | exact;
}
/* Remove index 0 and shift down (a binder). */
static inline uint64_t mask_bind(uint64_t a) {
    if (!(a & MASK_EXACT_BIT))
        return 0u;
    return ((a & ~MASK_EXACT_BIT) >> 1) | MASK_EXACT_BIT;
}
static const uint64_t MASK_EMPTY = MASK_EXACT_BIT;

static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline uint32_t bind_bound(uint32_t f) { return f ? f - 1u : 0u; }

static MgTp *tp_intern(MgStore *s, uint8_t tag, uint32_t n, MgTp *a, MgTp *b) {
    uint32_t hash = mg_hash4(tag, n, PTR(a), PTR(b));
    MgInternTable *t = &s->tps;
    if (!table_grow(t))
        return NULL;
    uint32_t i = hash & (t->cap - 1u);
    for (MgTp *node; (node = t->slots[i].node) != NULL; i = (i + 1u) & (t->cap - 1u))
        if (t->slots[i].hash == hash && node->tag == tag && node->n == n &&
            node->a == a && node->b == b)
            return node;
    MgTp *node = mg_store_alloc(s, sizeof(*node));
    if (!node)
        return NULL;
    node->tag = tag;
    node->n = n;
    node->a = a;
    node->b = b;
    node->hash = hash;
    switch (tag) {
    case MG_TP_VAR:
        node->plain = true;
        node->ftv = n + 1u;
        node->tvmask = mask_singleton(n);
        node->height = 1u;
        break;
    case MG_TP_PROP:
    case MG_TP_BASE:
        node->plain = true;
        node->tvmask = MASK_EMPTY;
        node->height = 1u;
        break;
    case MG_TP_ARR:
        node->plain = a->plain && b->plain;
        node->ftv = max_u32(a->ftv, b->ftv);
        node->tvmask = mask_union(a->tvmask, b->tvmask);
        node->height = 1u + max_u32(a->height, b->height);
        break;
    case MG_TP_ALL:
        node->plain = false;
        node->ftv = bind_bound(a->ftv);
        node->tvmask = mask_bind(a->tvmask);
        node->height = 1u + a->height;
        break;
    }
    table_insert_at(s, t, i, node, hash);
    ++s->nodes;
    return node;
}

MgTp *mg_tp_var(MgStore *s, uint32_t n) { return tp_intern(s, MG_TP_VAR, n, NULL, NULL); }
MgTp *mg_tp_prop(MgStore *s) { return tp_intern(s, MG_TP_PROP, 0, NULL, NULL); }
MgTp *mg_tp_base(MgStore *s, uint32_t n) { return tp_intern(s, MG_TP_BASE, n, NULL, NULL); }
MgTp *mg_tp_arr(MgStore *s, MgTp *a, MgTp *b) {
    return a && b ? tp_intern(s, MG_TP_ARR, 0, a, b) : NULL;
}
MgTp *mg_tp_all(MgStore *s, MgTp *a) {
    return a ? tp_intern(s, MG_TP_ALL, 0, a, NULL) : NULL;
}

bool mg_tp_poly_wf(const MgTp *a, uint32_t depth) {
    while (a->tag == MG_TP_ALL) {
        a = a->a;
        ++depth;
    }
    return mg_tp_plain_wf(a, depth);
}

bool mg_tp_free_tv(const MgTp *a, uint32_t k) {
    if (k >= a->ftv)
        return false;
    if ((a->tvmask & MASK_EXACT_BIT) && k < 63u)
        return (a->tvmask >> k) & 1u;
    switch (a->tag) {
    case MG_TP_VAR: return a->n == k;
    case MG_TP_ARR: return mg_tp_free_tv(a->a, k) || mg_tp_free_tv(a->b, k);
    case MG_TP_ALL: return mg_tp_free_tv(a->a, k + 1u);
    default: return false;
    }
}

/* ── terms ────────────────────────────────────────────────────────────── */

static MgTm *tm_intern(MgStore *s, uint8_t tag, uint32_t n, MgTm *f, MgTm *x,
                       MgTp *tp) {
    uint32_t hash = mg_hash4((uint64_t)tag << 32 | n, PTR(f), PTR(x), PTR(tp));
    MgInternTable *t = &s->tms;
    if (!table_grow(t))
        return NULL;
    uint32_t i = hash & (t->cap - 1u);
    for (MgTm *node; (node = t->slots[i].node) != NULL; i = (i + 1u) & (t->cap - 1u))
        if (t->slots[i].hash == hash && node->tag == tag && node->n == n &&
            node->f == f && node->x == x && node->tp == tp)
            return node;
    MgTm *node = mg_store_alloc(s, sizeof(*node));
    if (!node)
        return NULL;
    node->tag = tag;
    node->n = n;
    node->f = f;
    node->x = x;
    node->tp = tp;
    node->hash = hash;
    switch (tag) {
    case MG_TM_DB:
        node->fv = n + 1u;
        node->dbmask = mask_singleton(n);
        node->tvmask = MASK_EMPTY;
        node->facts = MG_TM_PROTECTED;
        node->height = 1u;
        break;
    case MG_TM_NAME:
        node->dbmask = MASK_EMPTY;
        node->tvmask = MASK_EMPTY;
        node->facts = MG_TM_PROTECTED | MG_TM_HAS_NAME;
        node->height = 1u;
        break;
    case MG_TM_PRIM:
        node->dbmask = MASK_EMPTY;
        node->tvmask = MASK_EMPTY;
        node->facts = MG_TM_PROTECTED;
        node->height = 1u;
        break;
    case MG_TM_AP:
    case MG_TM_IMP:
        node->fv = max_u32(f->fv, x->fv);
        node->ftv = max_u32(f->ftv, x->ftv);
        node->dbmask = mask_union(f->dbmask, x->dbmask);
        node->tvmask = mask_union(f->tvmask, x->tvmask);
        node->facts = (uint8_t)((f->facts & x->facts & MG_TM_PROTECTED) |
                                ((f->facts | x->facts) &
                                 (MG_TM_HAS_NAME | MG_TM_HAS_TYPEREG)));
        node->height = 1u + max_u32(f->height, x->height);
        break;
    case MG_TM_LAM:
    case MG_TM_ALL:
        node->fv = bind_bound(f->fv);
        node->ftv = max_u32(f->ftv, tp->ftv);
        node->dbmask = mask_bind(f->dbmask);
        node->tvmask = mask_union(f->tvmask, tp->tvmask);
        node->facts = f->facts;
        node->height = 1u + max_u32(f->height, tp->height);
        break;
    case MG_TM_TAP:
        node->fv = f->fv;
        node->ftv = max_u32(f->ftv, tp->ftv);
        node->dbmask = f->dbmask;
        node->tvmask = mask_union(f->tvmask, tp->tvmask);
        /* Protected: the type region is closed in term variables. */
        node->facts = (uint8_t)(((f->fv == 0u && (f->facts & MG_TM_PROTECTED))
                                     ? MG_TM_PROTECTED : 0u) |
                                (f->facts & MG_TM_HAS_NAME) | MG_TM_HAS_TYPEREG);
        node->height = 1u + max_u32(f->height, tp->height);
        break;
    case MG_TM_TLAM:
    case MG_TM_TALL:
        node->fv = f->fv;
        node->ftv = bind_bound(f->ftv);
        node->dbmask = f->dbmask;
        node->tvmask = mask_bind(f->tvmask);
        node->facts = (uint8_t)(((f->fv == 0u && (f->facts & MG_TM_PROTECTED))
                                     ? MG_TM_PROTECTED : 0u) |
                                (f->facts & MG_TM_HAS_NAME) | MG_TM_HAS_TYPEREG);
        node->height = 1u + f->height;
        break;
    }
    table_insert_at(s, t, i, node, hash);
    ++s->nodes;
    return node;
}

MgTm *mg_tm_db(MgStore *s, uint32_t n) {
    if (n >= MG_DB_SMALL)
        return tm_intern(s, MG_TM_DB, n, NULL, NULL, NULL);
    if (!s->db_small[n])
        s->db_small[n] = tm_intern(s, MG_TM_DB, n, NULL, NULL, NULL);
    return s->db_small[n];
}
MgTm *mg_tm_name(MgStore *s, uint32_t name) {
    return name ? tm_intern(s, MG_TM_NAME, name, NULL, NULL, NULL) : NULL;
}
MgTm *mg_tm_prim(MgStore *s, uint32_t n) { return tm_intern(s, MG_TM_PRIM, n, NULL, NULL, NULL); }
MgTm *mg_tm_ap(MgStore *s, MgTm *f, MgTm *x) {
    return f && x ? tm_intern(s, MG_TM_AP, 0, f, x, NULL) : NULL;
}
MgTm *mg_tm_lam(MgStore *s, MgTp *a, MgTm *body) {
    return a && body ? tm_intern(s, MG_TM_LAM, 0, body, NULL, a) : NULL;
}
MgTm *mg_tm_imp(MgStore *s, MgTm *p, MgTm *q) {
    return p && q ? tm_intern(s, MG_TM_IMP, 0, p, q, NULL) : NULL;
}
MgTm *mg_tm_all(MgStore *s, MgTp *a, MgTm *body) {
    return a && body ? tm_intern(s, MG_TM_ALL, 0, body, NULL, a) : NULL;
}
MgTm *mg_tm_tap(MgStore *s, MgTm *f, MgTp *a) {
    return f && a ? tm_intern(s, MG_TM_TAP, 0, f, NULL, a) : NULL;
}
MgTm *mg_tm_tlam(MgStore *s, MgTm *body) {
    return body ? tm_intern(s, MG_TM_TLAM, 0, body, NULL, NULL) : NULL;
}
MgTm *mg_tm_tall(MgStore *s, MgTm *body) {
    return body ? tm_intern(s, MG_TM_TALL, 0, body, NULL, NULL) : NULL;
}

/* Exact free-index queries.  The 63-bit masks answer directly when every
 * free index of the node is below 63.  Otherwise an iterative walk visits
 * each (node, relative index) pair at most once, so a shared DAG costs its
 * distinct pairs, not its tree size. */
typedef struct {
    const void *node;
    uint32_t k;
    uint8_t type;   /* 1 when node is an MgTp */
} FvKey;

typedef struct {
    FvKey *slots;
    size_t cap, len;
    bool full;       /* could not grow: stop memoizing, stay exact */
} FvSet;

static size_t fv_index(const void *node, uint32_t k, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)node * UINT64_C(0x9e3779b97f4a7c15) ^
                 (uint64_t)k * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 29;
    return (size_t)h & (cap - 1u);
}

/* True when (node, k) is new (and is now recorded). */
static bool fv_first_visit(FvSet *set, const void *node, uint32_t k) {
    if (set->full)
        return true;
    if ((set->len + 1u) * 2u > set->cap) {
        size_t cap = set->cap ? set->cap * 2u : 128u;
        FvKey *slots = mg_sys_alloc(sizeof(FvKey) * cap);
        if (!slots) {
            set->full = true;
            return true;
        }
        memset(slots, 0, sizeof(FvKey) * cap);
        for (size_t j = 0; j < set->cap; ++j) {
            FvKey e = set->slots[j];
            if (!e.node)
                continue;
            size_t i = fv_index(e.node, e.k, cap);
            while (slots[i].node)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e;
        }
        mg_sys_free(set->slots);
        set->slots = slots;
        set->cap = cap;
    }
    size_t i = fv_index(node, k, set->cap);
    while (set->slots[i].node) {
        if (set->slots[i].node == node && set->slots[i].k == k)
            return false;
        i = (i + 1u) & (set->cap - 1u);
    }
    set->slots[i] = (FvKey){node, k, 0};
    ++set->len;
    return true;
}

typedef struct {
    FvKey *items;
    size_t len, cap;
    bool nomem;
} FvStack;

static void fv_push(FvStack *st, const void *node, uint32_t k, uint8_t type) {
    if (st->len == st->cap) {
        size_t cap = st->cap ? st->cap * 2u : 64u;
        FvKey *grown = mg_sys_realloc(st->items, sizeof(FvKey) * cap);
        if (!grown) {
            st->nomem = true;
            return;
        }
        st->items = grown;
        st->cap = cap;
    }
    st->items[st->len++] = (FvKey){node, k, type};
}

/* Walk for a term index (tv = false) or a type variable (tv = true).  On
 * allocation failure the answer falls back to plain recursion. */
static bool free_walk(const MgTm *root, uint32_t k, bool tv);

static bool free_db_rec(const MgTm *m, uint32_t k) {
    if (k >= m->fv)
        return false;
    if ((m->dbmask & MASK_EXACT_BIT) && k < 63u)
        return (m->dbmask >> k) & 1u;
    switch (m->tag) {
    case MG_TM_DB: return m->n == k;
    case MG_TM_AP:
    case MG_TM_IMP: return free_db_rec(m->f, k) || free_db_rec(m->x, k);
    case MG_TM_LAM:
    case MG_TM_ALL: return free_db_rec(m->f, k + 1u);
    case MG_TM_TAP:
    case MG_TM_TLAM:
    case MG_TM_TALL: return free_db_rec(m->f, k);
    default: return false;
    }
}

static bool free_tv_rec(const MgTm *m, uint32_t k) {
    if (k >= m->ftv)
        return false;
    if ((m->tvmask & MASK_EXACT_BIT) && k < 63u)
        return (m->tvmask >> k) & 1u;
    switch (m->tag) {
    case MG_TM_AP:
    case MG_TM_IMP: return free_tv_rec(m->f, k) || free_tv_rec(m->x, k);
    case MG_TM_LAM:
    case MG_TM_ALL: return mg_tp_free_tv(m->tp, k) || free_tv_rec(m->f, k);
    case MG_TM_TAP: return free_tv_rec(m->f, k) || mg_tp_free_tv(m->tp, k);
    case MG_TM_TLAM:
    case MG_TM_TALL: return free_tv_rec(m->f, k + 1u);
    default: return false;
    }
}

static bool free_walk(const MgTm *root, uint32_t k, bool tv) {
    FvSet seen = {0};
    FvStack st = {0};
    bool found = false;
    fv_push(&st, root, k, 0);
    while (!found && st.len && !st.nomem) {
        FvKey f = st.items[--st.len];
        if (f.type) {
            if (mg_tp_free_tv(f.node, f.k))
                found = true;
            continue;
        }
        const MgTm *m = f.node;
        uint32_t q = f.k;
        if (q >= (tv ? m->ftv : m->fv))
            continue;
        uint64_t mask = tv ? m->tvmask : m->dbmask;
        if ((mask & MASK_EXACT_BIT) && q < 63u) {
            found = (mask >> q) & 1u;
            continue;
        }
        if (!fv_first_visit(&seen, m, q))
            continue;
        switch (m->tag) {
        case MG_TM_DB:
            found = !tv && m->n == q;
            break;
        case MG_TM_AP:
        case MG_TM_IMP:
            fv_push(&st, m->x, q, 0);
            fv_push(&st, m->f, q, 0);
            break;
        case MG_TM_LAM:
        case MG_TM_ALL:
            fv_push(&st, m->f, tv ? q : q + 1u, 0);
            if (tv)
                fv_push(&st, m->tp, q, 1);
            break;
        case MG_TM_TAP:
            fv_push(&st, m->f, q, 0);
            if (tv)
                fv_push(&st, m->tp, q, 1);
            break;
        case MG_TM_TLAM:
        case MG_TM_TALL:
            fv_push(&st, m->f, tv ? q + 1u : q, 0);
            break;
        default:
            break;
        }
    }
    bool nomem = st.nomem;
    mg_sys_free(st.items);
    mg_sys_free(seen.slots);
    if (nomem)
        return tv ? free_tv_rec(root, k) : free_db_rec(root, k);
    return found;
}

bool mg_tm_free_db(const MgTm *m, uint32_t k) {
    if (k >= m->fv)
        return false;
    if ((m->dbmask & MASK_EXACT_BIT) && k < 63u)
        return (m->dbmask >> k) & 1u;
    if (k == 0u && (m->facts & MG_TM_FREE0_KNOWN))
        return (m->facts & MG_TM_FREE0) != 0u;
    bool r = free_walk(m, k, false);
    if (k == 0u)
        ((MgTm *)m)->facts |= (uint8_t)(MG_TM_FREE0_KNOWN | (r ? MG_TM_FREE0 : 0u));
    return r;
}

bool mg_tm_free_tv(const MgTm *m, uint32_t k) {
    if (k >= m->ftv)
        return false;
    if ((m->tvmask & MASK_EXACT_BIT) && k < 63u)
        return (m->tvmask >> k) & 1u;
    return free_walk(m, k, true);
}

/* ── proofs ───────────────────────────────────────────────────────────── */

static inline uint64_t sat_add(uint64_t a, uint64_t b) {
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static MgPf *pf_intern(MgStore *s, uint8_t tag, uint32_t n, MgPf *p, MgPf *q,
                       MgTm *t, MgTp *tp) {
    uint32_t hash = mg_hash4((uint64_t)tag << 32 | n, PTR(p) ^ (PTR(q) << 1),
                             PTR(t), PTR(tp));
    MgInternTable *tab = &s->pfs;
    if (!table_grow(tab))
        return NULL;
    uint32_t i = hash & (tab->cap - 1u);
    for (MgPf *node; (node = tab->slots[i].node) != NULL; i = (i + 1u) & (tab->cap - 1u))
        if (tab->slots[i].hash == hash && node->tag == tag && node->n == n &&
            node->p == p && node->q == q && node->t == t && node->tp == tp)
            return node;
    MgPf *node = mg_store_alloc(s, sizeof(*node));
    if (!node)
        return NULL;
    node->tag = tag;
    node->n = n;
    node->p = p;
    node->q = q;
    node->t = t;
    node->tp = tp;
    node->hash = hash;
    bool safe = true;
    uint32_t height = 0;
    uint64_t size = 1u;
    if (p) {
        safe = safe && p->payload_protected;
        height = max_u32(height, p->height);
        size = sat_add(size, p->size);
    }
    if (q) {
        safe = safe && q->payload_protected;
        height = max_u32(height, q->height);
        size = sat_add(size, q->size);
    }
    if (t)
        safe = safe && mg_tm_protected(t);
    node->payload_protected = safe;
    node->height = height + 1u;
    node->size = size;
    table_insert_at(s, tab, i, node, hash);
    ++s->nodes;
    return node;
}

MgPf *mg_pf_gpa(MgStore *s, uint32_t name) { return name ? pf_intern(s, MG_PF_GPA, name, NULL, NULL, NULL, NULL) : NULL; }
MgPf *mg_pf_hyp(MgStore *s, uint32_t n) { return pf_intern(s, MG_PF_HYP, n, NULL, NULL, NULL, NULL); }
MgPf *mg_pf_known(MgStore *s, uint32_t name) { return name ? pf_intern(s, MG_PF_KNOWN, name, NULL, NULL, NULL, NULL) : NULL; }
MgPf *mg_pf_tmap(MgStore *s, MgPf *p, MgTm *t) { return p && t ? pf_intern(s, MG_PF_TMAP, 0, p, NULL, t, NULL) : NULL; }
MgPf *mg_pf_pfap(MgStore *s, MgPf *p, MgPf *q) { return p && q ? pf_intern(s, MG_PF_PFAP, 0, p, q, NULL, NULL) : NULL; }
MgPf *mg_pf_plam(MgStore *s, MgTm *t, MgPf *p) { return p && t ? pf_intern(s, MG_PF_PLAM, 0, p, NULL, t, NULL) : NULL; }
MgPf *mg_pf_tlam(MgStore *s, MgTp *a, MgPf *p) { return p && a ? pf_intern(s, MG_PF_TLAM, 0, p, NULL, NULL, a) : NULL; }
MgPf *mg_pf_tpap(MgStore *s, MgPf *p, MgTp *a) { return p && a ? pf_intern(s, MG_PF_TPAP, 0, p, NULL, NULL, a) : NULL; }
MgPf *mg_pf_tplam(MgStore *s, MgPf *p) { return p ? pf_intern(s, MG_PF_TPLAM, 0, p, NULL, NULL, NULL) : NULL; }

/* ── lists and contexts ───────────────────────────────────────────────── */

MgTpList *mg_tplist_cons(MgStore *s, MgTp *head, MgTpList *tail) {
    if (!head)
        return NULL;
    uint32_t hash = mg_hash4(11u, PTR(head), PTR(tail), 0u);
    MgInternTable *t = &s->tplists;
    if (!table_grow(t))
        return NULL;
    uint32_t i = hash & (t->cap - 1u);
    for (MgTpList *node; (node = t->slots[i].node) != NULL; i = (i + 1u) & (t->cap - 1u))
        if (t->slots[i].hash == hash && node->head == head && node->tail == tail)
            return node;
    MgTpList *node = mg_store_alloc(s, sizeof(*node));
    if (!node)
        return NULL;
    node->head = head;
    node->tail = tail;
    node->len = 1u + mg_tplist_len(tail);
    node->hash = hash;
    table_insert_at(s, t, i, node, hash);
    return node;
}

MgTmList *mg_tmlist_cons(MgStore *s, MgTm *head, MgTmList *tail) {
    if (!head)
        return NULL;
    uint32_t hash = mg_hash4(13u, PTR(head), PTR(tail), 0u);
    MgInternTable *t = &s->tmlists;
    if (!table_grow(t))
        return NULL;
    uint32_t i = hash & (t->cap - 1u);
    for (MgTmList *node; (node = t->slots[i].node) != NULL; i = (i + 1u) & (t->cap - 1u))
        if (t->slots[i].hash == hash && node->head == head && node->tail == tail)
            return node;
    MgTmList *node = mg_store_alloc(s, sizeof(*node));
    if (!node)
        return NULL;
    node->head = head;
    node->tail = tail;
    node->len = 1u + mg_tmlist_len(tail);
    node->hash = hash;
    node->fv = max_u32(head->fv, tail ? tail->fv : 0u);
    node->protected_all = mg_tm_protected(head) && (!tail || tail->protected_all);
    table_insert_at(s, t, i, node, hash);
    return node;
}

MgCtx *mg_ctx(MgStore *s, uint32_t depth, MgTpList *terms, MgTmList *hyps) {
    uint32_t hash = mg_hash4(17u, depth, PTR(terms), PTR(hyps));
    MgInternTable *t = &s->ctxs;
    if (!table_grow(t))
        return NULL;
    uint32_t i = hash & (t->cap - 1u);
    for (MgCtx *node; (node = t->slots[i].node) != NULL; i = (i + 1u) & (t->cap - 1u))
        if (t->slots[i].hash == hash && node->depth == depth && node->terms == terms &&
            node->hyps == hyps)
            return node;
    MgCtx *node = mg_store_alloc(s, sizeof(*node));
    if (!node)
        return NULL;
    node->depth = depth;
    node->terms = terms;
    node->hyps = hyps;
    node->hash = hash;
    /* ContextSafe: every hypothesis is scoped by the term context and
     * protected. */
    node->safe = !hyps || (hyps->fv <= mg_tplist_len(terms) && hyps->protected_all);
    node->formed = false;
    node->formed_rev = 0;
    table_insert_at(s, t, i, node, hash);
    return node;
}

/* ── names ────────────────────────────────────────────────────────────── */

static uint32_t text_hash(const char *text, size_t len) {
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned char)text[i];
        h *= UINT64_C(1099511628211);
    }
    h = mg_mix64(h);
    return (uint32_t)(h ^ (h >> 32));
}

uint32_t mg_name_intern(MgStore *s, const char *text, size_t len) {
    if (!text || len == 0u || len > UINT32_MAX - 1u)
        return 0;
    uint32_t hash = text_hash(text, len);
    MgInternTable *t = &s->name_index;
    if (!table_grow(t))
        return 0;
    uint32_t i = hash & (t->cap - 1u);
    for (MgName *node; (node = t->slots[i].node) != NULL; i = (i + 1u) & (t->cap - 1u))
        if (t->slots[i].hash == hash && node->len == len &&
            memcmp(node->text, text, len) == 0)
            return node->id;
    if (s->name_len == UINT32_MAX ||
        (s->name_len == s->name_cap && s->name_cap > (UINT32_MAX >> 1))) {
        ++s->resource_failures;
        return 0;
    }
    if (s->name_len == s->name_cap) {
        uint32_t cap = s->name_cap * 2u;
        MgName **names = mg_sys_realloc(s->names, sizeof(MgName *) * cap);
        if (!names) {
            ++s->resource_failures;
            return 0;
        }
        s->names = names;
        s->name_cap = cap;
    }
    MgName *node = mg_sys_alloc(sizeof(MgName) + len + 1u);
    if (!node) {
        ++s->resource_failures;
        return 0;
    }
    memset(node, 0, sizeof(*node));
    node->id = s->name_len;
    node->len = (uint32_t)len;
    node->hash = hash;
    memcpy(node->text, text, len);
    node->text[len] = '\0';
    s->names[s->name_len++] = node;
    table_insert_at(s, t, i, node, hash);
    return node->id;
}

const MgName *mg_name_get(const MgStore *s, uint32_t id) {
    return id && id < s->name_len ? s->names[id] : NULL;
}
