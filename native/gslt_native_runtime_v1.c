#include "gslt_native_runtime_v1.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct GnChunk GnChunk;
struct GnChunk {
    GnChunk *next;
    size_t used, cap;
    _Alignas(16) unsigned char data[];
};

typedef struct {
    GnChunk *chunks;
    uint16_t id;              /* a value's space */
    size_t bytes;             /* allocated since the last reset */
} GnArena;

enum { GN_SPACE_FIXED = 1, GN_SPACE_SCRATCH, GN_SPACE_RESULTS, GN_SPACE_STATE0, GN_SPACE_STATE1 };

/* The map from values to their copies, kept between copies: an entry is
 * current only with the current generation's stamp. */
typedef struct {
    const GnVal **from;
    GnVal **to;
    uint32_t *stamp;
    size_t cap, len;
    uint32_t gen;
} GnCopyMap;

/* Lifetimes: `fixed` holds the cached constants, integers and scalars and
 * is never reclaimed; `scratch` holds what one reading step allocates and is
 * reset between steps; `state` holds the reader state carried from step to
 * step (two arenas, the live one and the one the next copy goes to);
 * `results` holds readings the caller keeps. */
struct GnCtx {
    const GnProgramV1 *program;
    GnArena fixed, scratch, results, state[2];
    int state_cur;
    GnArena *cur;             /* where allocation goes */
    jmp_buf oom;
    GnVal **consts;           /* 0-ary constructors, by op */
    GnVal *ints[256];
    GnVal *cps[128];          /* (cp N) below 128 */
    size_t *name_len;         /* operator names' lengths, by op - GN_FIRST_OP */
    uint32_t *scalars;        /* a span's scalars while it is hashed, kept between calls */
    size_t scalars_cap;
    const GnVal **pairs;      /* gn_eq's work stack, kept between calls */
    size_t pairs_cap;
    GnCopyMap map;
    size_t state_live;        /* the kept state's size at its last full copy */
    struct GnHashFrame *hash_stack;   /* gn_hash's work stack, kept between calls */
    size_t hash_cap;
#ifdef GN_TRACE
    FILE *trace;
    unsigned trace_depth;
#endif
};

enum { GN_CHUNK_BYTES = 1u << 20 };

static void *gn_alloc_in(GnCtx *cx, GnArena *arena, size_t n) {
    n = (n + 15u) & ~(size_t)15u;
    GnChunk *c = arena->chunks;
    if (!c || c->cap - c->used < n) {
        size_t cap = n > GN_CHUNK_BYTES ? n : GN_CHUNK_BYTES;
        GnChunk *fresh = malloc(sizeof *fresh + cap);
        if (!fresh)
            longjmp(cx->oom, 1);
        fresh->next = c;
        fresh->used = 0;
        fresh->cap = cap;
        arena->chunks = c = fresh;
    }
    void *p = c->data + c->used;
    c->used += n;
    arena->bytes += n;
    return p;
}

static void *gn_alloc(GnCtx *cx, size_t n) { return gn_alloc_in(cx, cx->cur, n); }

static void gn_arena_free(GnArena *arena) {
    for (GnChunk *c = arena->chunks; c;) {
        GnChunk *next = c->next;
        free(c);
        c = next;
    }
    arena->chunks = NULL;
}

/* Empty the arena, keeping one chunk for reuse. */
static void gn_arena_reset(GnArena *arena) {
    GnChunk *keep = arena->chunks;
    if (!keep)
        return;
    for (GnChunk *c = keep->next; c;) {
        GnChunk *next = c->next;
        free(c);
        c = next;
    }
    keep->next = NULL;
    keep->used = 0;
    arena->bytes = 0;
}


GnCtx *gn_ctx_new(const GnProgramV1 *program) {
    GnCtx *cx = calloc(1u, sizeof *cx);
    if (!cx)
        return NULL;
    if (program->op_cons != GN_OP_CONS || program->op_nil != GN_OP_NIL || program->op_cp != GN_OP_CP) {
        free(cx);
        return NULL;
    }
    cx->program = program;
    cx->fixed.id = GN_SPACE_FIXED;
    cx->scratch.id = GN_SPACE_SCRATCH;
    cx->results.id = GN_SPACE_RESULTS;
    cx->state[0].id = GN_SPACE_STATE0;
    cx->state[1].id = GN_SPACE_STATE1;
    cx->cur = &cx->scratch;
    cx->consts = calloc(program->op_count + GN_FIRST_OP, sizeof *cx->consts);
    cx->name_len = malloc((program->op_count ? program->op_count : 1u) * sizeof *cx->name_len);
    if (!cx->consts || !cx->name_len) {
        free(cx->consts);
        free(cx->name_len);
        free(cx);
        return NULL;
    }
    for (uint32_t i = 0; i < program->op_count; i++)
        cx->name_len[i] = strlen(program->ops[i].name);
    return cx;
}

void gn_ctx_free(GnCtx *cx) {
    if (!cx)
        return;
    free(cx->scalars);
    gn_arena_free(&cx->fixed);
    gn_arena_free(&cx->scratch);
    gn_arena_free(&cx->results);
    gn_arena_free(&cx->state[0]);
    gn_arena_free(&cx->state[1]);
    free(cx->pairs);
    free(cx->hash_stack);
    free(cx->name_len);
    free(cx->map.from);
    free(cx->map.to);
    free(cx->map.stamp);
    free(cx->consts);
    free(cx);
}

void gn_scratch_reset(GnCtx *cx) { gn_arena_reset(&cx->scratch); }

/* ── copies out of scratch, sharing preserved ─────────────────────────── */

static size_t gn_slot(const GnVal *v, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)v * UINT64_C(0x9e3779b97f4a7c15);
    return (size_t)(h >> 20) & (cap - 1u);
}

static void gn_map_clear(GnCopyMap *m) {
    m->len = 0;
    if (++m->gen == 0u) {
        if (m->stamp)
            memset(m->stamp, 0, m->cap * sizeof *m->stamp);
        m->gen = 1u;
    }
}

static GnVal *gn_map_get(const GnCopyMap *m, const GnVal *v) {
    if (!m->cap)
        return NULL;
    for (size_t i = gn_slot(v, m->cap);; i = (i + 1u) & (m->cap - 1u)) {
        if (m->stamp[i] != m->gen)
            return NULL;
        if (m->from[i] == v)
            return m->to[i];
    }
}

static bool gn_map_put(GnCopyMap *m, const GnVal *v, GnVal *copy) {
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 1024u;
        const GnVal **from = malloc(cap * sizeof *from);
        GnVal **to = malloc(cap * sizeof *to);
        uint32_t *stamp = calloc(cap, sizeof *stamp);
        if (!from || !to || !stamp) {
            free(from);
            free(to);
            free(stamp);
            return false;
        }
        for (size_t j = 0; j < m->cap; j++) {
            if (m->stamp[j] != m->gen)
                continue;
            size_t i = gn_slot(m->from[j], cap);
            while (stamp[i])
                i = (i + 1u) & (cap - 1u);
            from[i] = m->from[j];
            to[i] = m->to[j];
            stamp[i] = 1u;
        }
        free(m->from);
        free(m->to);
        free(m->stamp);
        m->from = from;
        m->to = to;
        m->stamp = stamp;
        m->cap = cap;
        m->gen = 1u;
    }
    size_t i = gn_slot(v, m->cap);
    while (m->stamp[i] == m->gen)
        i = (i + 1u) & (m->cap - 1u);
    m->from[i] = v;
    m->to[i] = copy;
    m->stamp[i] = m->gen;
    m->len++;
    return true;
}

/* The bytes a value takes: its header and children, or a span's tail. */
static size_t gn_node_size(const GnVal *v) {
    return sizeof *v + (v->op == GN_SPAN ? sizeof(GnSpanTailV1) : (size_t)v->n * sizeof(GnVal *));
}

/* A copy of v into `to`: values in the fixed arena and in `keep` (when
 * given) are shared, every other node copied once. */
static GnVal *gn_copy(GnCtx *cx, GnArena *to, const GnArena *keep, const GnVal *root) {
    typedef struct {
        const GnVal *v;
        GnVal *c;
        uint32_t k;
    } Frame;
#define GN_STAYS(x) ((x)->space == GN_SPACE_FIXED || (keep && (x)->space == keep->id))
    if (GN_STAYS(root))
        return (GnVal *)root;
    GnCopyMap *map = &cx->map;
    gn_map_clear(map);
    Frame *st = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;
    GnVal *top = gn_alloc_in(cx, to, gn_node_size(root));
    memcpy(top, root, gn_node_size(root));
    top->space = to->id;
    ok = gn_map_put(map, root, top);
#define GN_FRAME(val, copy) do { \
        if (len == cap) { \
            cap = cap ? cap * 2u : 64u; \
            Frame *g = realloc(st, cap * sizeof *g); \
            if (!g) { ok = false; break; } \
            st = g; \
        } \
        st[len++] = (Frame){(val), (copy), 0u}; \
    } while (0)
    if (ok)
        GN_FRAME(root, top);
    while (ok && len) {
        Frame *f = &st[len - 1u];
        if (f->k == f->v->n) {
            len--;
            continue;
        }
        uint32_t k = f->k++;
        const GnVal *child = f->v->kid[k];
        GnVal *parent = f->c;
        if (GN_STAYS(child)) {
            parent->kid[k] = (GnVal *)child;
            continue;
        }
        GnVal *seen = gn_map_get(map, child);
        if (seen) {
            parent->kid[k] = seen;
            continue;
        }
        GnVal *copy = gn_alloc_in(cx, to, gn_node_size(child));
        memcpy(copy, child, gn_node_size(child));
        copy->space = to->id;
        parent->kid[k] = copy;
        ok = gn_map_put(map, child, copy);
        if (ok)
            GN_FRAME(child, copy);
    }
#undef GN_FRAME
#undef GN_STAYS
    free(st);
    if (!ok)
        longjmp(cx->oom, 1);
    return top;
}

GnVal *gn_keep_result(GnCtx *cx, const GnVal *v) { return gn_copy(cx, &cx->results, &cx->results, v); }

/* The state is copied into the live state arena, sharing what that arena
 * already holds, so a step costs what it changed.  What the arena holds
 * beyond the state is garbage from earlier states; once it outgrows the
 * state, the state is copied whole into the other arena and the live one
 * emptied. */
GnVal *gn_keep_state(GnCtx *cx, const GnVal *v) {
    GnArena *live = &cx->state[cx->state_cur];
    GnVal *copy = gn_copy(cx, live, live, v);
    size_t slack = cx->state_live * 2u > (size_t)(4u << 20) ? cx->state_live * 2u : (size_t)(4u << 20);
    if (live->bytes > slack) {
        int next = 1 - cx->state_cur;
        gn_arena_reset(&cx->state[next]);
        copy = gn_copy(cx, &cx->state[next], NULL, copy);
        cx->state_live = cx->state[next].bytes;
        gn_arena_reset(live);
        cx->state_cur = next;
    }
    return copy;
}

jmp_buf *gn_ctx_oom(GnCtx *cx) { return &cx->oom; }

const GnProgramV1 *gn_ctx_program(const GnCtx *cx) { return cx->program; }

static GnVal *gn_node(GnCtx *cx, uint32_t op, uint32_t n) {
    GnVal *v = gn_alloc(cx, sizeof *v + (size_t)n * sizeof(GnVal *));
    v->op = op;
    v->n = (uint16_t)n;
    v->space = cx->cur->id;
    v->i = 0;
    v->src = NULL;
    return v;
}

static GnVal *gn_fixed_node(GnCtx *cx, uint32_t op, uint32_t n) {
    GnArena *saved = cx->cur;
    cx->cur = &cx->fixed;
    GnVal *v = gn_node(cx, op, n);
    cx->cur = saved;
    return v;
}

GnVal *gn_int(GnCtx *cx, int64_t i) {
    if (i >= 0 && i < 256) {
        if (!cx->ints[i]) {
            cx->ints[i] = gn_fixed_node(cx, GN_INT, 0u);
            cx->ints[i]->i = i;
        }
        return cx->ints[i];
    }
    GnVal *v = gn_node(cx, GN_INT, 0u);
    v->i = i;
    return v;
}

GnVal *gn_const(GnCtx *cx, uint32_t op) {
    if (op < cx->program->op_count + GN_FIRST_OP) {
        if (!cx->consts[op])
            cx->consts[op] = gn_fixed_node(cx, op, 0u);
        return cx->consts[op];
    }
    return gn_node(cx, op, 0u);
}

GnVal *gn_app1(GnCtx *cx, uint32_t op, GnVal *a) {
    GnVal *v = gn_node(cx, op, 1u);
    v->kid[0] = a;
    return v;
}

GnVal *gn_app2(GnCtx *cx, uint32_t op, GnVal *a, GnVal *b) {
    GnVal *v = gn_node(cx, op, 2u);
    v->kid[0] = a;
    v->kid[1] = b;
    return v;
}

GnVal *gn_app3(GnCtx *cx, uint32_t op, GnVal *a, GnVal *b, GnVal *c) {
    GnVal *v = gn_node(cx, op, 3u);
    v->kid[0] = a;
    v->kid[1] = b;
    v->kid[2] = c;
    return v;
}

GnVal *gn_app4(GnCtx *cx, uint32_t op, GnVal *a, GnVal *b, GnVal *c, GnVal *d) {
    GnVal *v = gn_node(cx, op, 4u);
    v->kid[0] = a;
    v->kid[1] = b;
    v->kid[2] = c;
    v->kid[3] = d;
    return v;
}

GnVal *gn_appv(GnCtx *cx, uint32_t op, uint32_t n, ...) {
    GnVal *v = gn_node(cx, op, n);
    va_list args;
    va_start(args, n);
    for (uint32_t k = 0; k < n; k++)
        v->kid[k] = va_arg(args, GnVal *);
    va_end(args);
    return v;
}

GnVal *gn_cursor(GnCtx *cx, const GnSourceV1 *src, size_t pos) {
    GnVal *v = gn_node(cx, GN_CURSOR, 0u);
    v->i = (int64_t)pos;
    v->src = src;
    return v;
}

static GnVal *gn_cp(GnCtx *cx, uint32_t scalar) {
    if (scalar < 128u) {
        if (!cx->cps[scalar]) {
            GnVal *n = gn_int(cx, scalar);
            GnVal *v = gn_fixed_node(cx, cx->program->op_cp, 1u);
            v->kid[0] = n;
            cx->cps[scalar] = v;
        }
        return cx->cps[scalar];
    }
    return gn_appv(cx, cx->program->op_cp, 1u, gn_int(cx, scalar));
}

GnVal *gn_scalar(GnCtx *cx, uint32_t scalar) { return gn_cp(cx, scalar); }

bool gn_uncons_cursor(GnCtx *cx, GnVal *v, GnVal **head, GnVal **tail) {
    size_t pos = (size_t)v->i;
    if (pos >= v->src->len)
        return false;
    uint32_t scalar;
    size_t w = gn_utf8_next(v->src, pos, &scalar);
    *head = gn_cp(cx, scalar);
    *tail = gn_cursor(cx, v->src, pos + w);
    return true;
}

/* ── spans ─────────────────────────────────────────────────────────── */

GnVal *gn_span(GnCtx *cx, const GnSourceV1 *src, size_t start, size_t end, uint32_t cons, uint32_t nil) {
    if (start >= end)
        return gn_const(cx, nil);
    GnVal *v = gn_alloc(cx, sizeof *v + sizeof(GnSpanTailV1));
    v->op = GN_SPAN;
    v->n = 0u;
    v->space = cx->cur->id;
    v->i = (int64_t)start;
    v->src = src;
    *gn_span_tail(v) = (GnSpanTailV1){end, cons, nil, 0u};
    return v;
}

void gn_uncons_span(GnCtx *cx, GnVal *v, GnVal **head, GnVal **tail) {
    const GnSpanTailV1 *t = gn_span_tail(v);
    size_t pos = (size_t)v->i;
    uint32_t scalar;
    size_t w = gn_utf8_next(v->src, pos, &scalar);
    *head = gn_cp(cx, scalar);
    *tail = gn_span(cx, v->src, pos + w, t->end, t->cons, t->nil);
}

/* The bytes N stands for in a source: one escaped byte, or its UTF-8. */
static size_t gn_scalar_bytes(uint32_t c, uint8_t out[4]) {
    if (c >= 0xDC80u && c <= 0xDCFFu) {
        out[0] = (uint8_t)(c - 0xDC00u);
        return 1u;
    }
    if (c < 0x80u) {
        out[0] = (uint8_t)c;
        return 1u;
    }
    if (c < 0x800u) {
        out[0] = (uint8_t)(0xC0u | c >> 6);
        out[1] = (uint8_t)(0x80u | (c & 0x3Fu));
        return 2u;
    }
    if (c < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | c >> 12);
        out[1] = (uint8_t)(0x80u | (c >> 6 & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (c & 0x3Fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xF0u | c >> 18);
    out[1] = (uint8_t)(0x80u | (c >> 12 & 0x3Fu));
    out[2] = (uint8_t)(0x80u | (c >> 6 & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (c & 0x3Fu));
    return 4u;
}

GnVal *gn_cons_cp(GnCtx *cx, uint32_t cons, GnVal *cp, GnVal *t) {
    if (t->op == GN_SPAN && gn_span_tail(t)->cons == cons && cp->op == cx->program->op_cp &&
        cp->n == 1u && cp->kid[0]->op == GN_INT) {
        /* the scalar's bytes just before the span, read there as that
         * scalar (an escaped byte could begin a longer scalar) */
        uint32_t c = (uint32_t)cp->kid[0]->i, read;
        uint8_t bytes[4];
        size_t w = gn_scalar_bytes(c, bytes), start = (size_t)t->i;
        if (start >= w && memcmp(t->src->bytes + start - w, bytes, w) == 0 &&
            gn_utf8_next(t->src, start - w, &read) == w && read == c) {
            const GnSpanTailV1 *tail = gn_span_tail(t);
            return gn_span(cx, t->src, start - w, tail->end, cons, tail->nil);
        }
    }
    return gn_app2(cx, cons, cp, t);
}

bool gn_is_scalars(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil, const uint32_t *s, size_t n) {
    uint32_t op_cp = cx->program->op_cp;
    for (size_t i = 0;; i++) {
        if (v->op == GN_SPAN) {
            const GnSpanTailV1 *t = gn_span_tail(v);
            size_t pos = (size_t)v->i;
            if (t->cons != cons || t->nil != nil || t->end - pos < n - i)
                return false;
            for (; i < n; i++) {
                uint32_t c;
                if (pos >= t->end)
                    return false;
                pos += gn_utf8_next(v->src, pos, &c);
                if (c != s[i])
                    return false;
            }
            return pos == t->end;
        }
        if (i == n)
            return v->op == nil && v->n == 0u;
        if (v->op != cons || v->n != 2u)
            return false;
        const GnVal *h = v->kid[0];
        if (h->op != op_cp || h->n != 1u || h->kid[0]->op != GN_INT || h->kid[0]->i != (int64_t)s[i])
            return false;
        v = v->kid[1];
    }
}

bool gn_text_key_cells(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil, uint64_t *key) {
    uint64_t h = GN_TEXT_HASH_BASIS;
    uint32_t op_cp = cx->program->op_cp;
    for (;;) {
        if (v->op == GN_SPAN) {
            const GnSpanTailV1 *t = gn_span_tail(v);
            if (t->cons != cons || t->nil != nil)
                return false;
            *key = gn_text_hash(h, v->src->bytes + v->i, t->end - (size_t)v->i);
            return true;
        }
        if (v->op == nil && v->n == 0u) {
            *key = h;
            return true;
        }
        if (v->op != cons || v->n != 2u)
            return false;
        const GnVal *c = v->kid[0];
        if (c->op != op_cp || c->n != 1u || c->kid[0]->op != GN_INT || c->kid[0]->i < 0 ||
            c->kid[0]->i > 0x10FFFF)
            return false;
        uint8_t bytes[4];
        h = gn_text_hash(h, bytes, gn_scalar_bytes((uint32_t)c->kid[0]->i, bytes));
        v = v->kid[1];
    }
}

bool gn_scan_class(GnCtx *cx, GnVal *v, const GnClassV1 *k, uint32_t cons, uint32_t nil,
                   GnVal **run, GnVal **after) {
    if (v->op == GN_OP_NIL && v->n == 0u) {
        *run = gn_const(cx, nil);
        *after = v;
        return true;
    }
    if (v->op != GN_CURSOR)
        return false;
    const GnSourceV1 *src = v->src;
    const uint8_t *b = src->bytes;
    size_t start = (size_t)v->i, pos = start, len = src->len;
    while (pos < len) {
        uint8_t c = b[pos];
        if (c < 0x80u) {
            if (!((k->ascii[c >> 6] >> (c & 63u)) & 1u))
                break;
            pos++;
        } else {
            uint32_t scalar;
            size_t w = gn_utf8_next(src, pos, &scalar);
            if (!gn_class_has(k, scalar))
                break;
            pos += w;
        }
    }
    *run = gn_span(cx, src, start, pos, cons, nil);
    *after = pos == start ? v : gn_cursor(cx, src, pos);
    return true;
}

/* A cursor, cons or nil (operator ids GN_CURSOR to GN_OP_NIL; an operator
 * id fixes the arity, so cons has two children and nil none). */
static inline bool gn_listlike(GnCtx *cx, const GnVal *v) {
    (void)cx;
    return (uint32_t)(v->op - GN_CURSOR) <= (uint32_t)(GN_OP_NIL - GN_CURSOR);
}

/* gn_eq's pairs still to compare, on the context's stack (gn_eq does not
 * nest, so one stack serves every comparison). */
typedef struct {
    const GnVal **items;
    size_t len, cap;
} GnPairs;

static void gn_push(GnCtx *cx, GnPairs *s, const GnVal *a, const GnVal *b) {
    if (s->len + 2u > s->cap) {
        size_t cap = s->cap ? s->cap * 2u : 64u;
        const GnVal **grown = realloc(s->items, cap * sizeof *grown);
        if (!grown)
            longjmp(cx->oom, 1);
        s->items = cx->pairs = grown;
        s->cap = cx->pairs_cap = cap;
    }
    s->items[s->len++] = a;
    s->items[s->len++] = b;
}

static bool gn_eq_walk(GnCtx *cx, const GnVal *a, const GnVal *b);

/* Structural hashes: equal values hash alike, so values whose hashes differ
 * are unequal at once.  A constructor application keeps its hash in its
 * integer slot, unused otherwise (nonzero once known), from the first
 * comparison that needs it; by then the value is complete, since a rule
 * fills its outputs' holes before anything compares them.  A list hashes
 * by its kind alone: a cursor equals the list it reads. */
#define GN_HASH_LIST 0x51ed27a3c2d1e4b5ull
#define GN_HASH_INT 0x2545f4914f6cdd1dull

struct GnHashFrame {
    GnVal *v;
    uint32_t k;
    uint64_t h;
};

static inline uint64_t gn_mix(uint64_t h, uint64_t x) {
    h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

static uint64_t gn_span_hash(GnCtx *cx, const GnVal *v);

/* A child's hash when it needs no walk: an integer, a list, a span, or a
 * constructor whose hash is known; 0 otherwise. */
static inline uint64_t gn_hash_known(GnCtx *cx, const GnVal *v) {
    if (v->op == GN_INT)
        return gn_mix(GN_HASH_INT, (uint64_t)v->i);
    if (gn_listlike(cx, v))
        return GN_HASH_LIST;
    if (v->op == GN_SPAN)
        return gn_span_hash(cx, v);
    return (uint64_t)v->i;
}

/* A span's hash: that of the cells it reads as, (CONS (cp N) T) hashing as
 * gn_hash hashes an application (its operator and arity, then each child,
 * the low bit set), folded from NIL at its end. */
static uint64_t gn_span_hash(GnCtx *cx, const GnVal *v) {
    GnSpanTailV1 *t = gn_span_tail(v);
    if (t->hash)
        return t->hash;
    size_t n = 0;
    for (size_t pos = (size_t)v->i; pos < t->end; n++) {
        uint32_t c;
        if (n == cx->scalars_cap) {
            size_t cap = cx->scalars_cap ? cx->scalars_cap * 2u : 256u;
            uint32_t *grown = realloc(cx->scalars, cap * sizeof *grown);
            if (!grown)
                longjmp(cx->oom, 1);
            cx->scalars = grown;
            cx->scalars_cap = cap;
        }
        pos += gn_utf8_next(v->src, pos, &c);
        cx->scalars[n] = c;
    }
    uint64_t h = gn_mix((uint64_t)t->nil, 0u) | 1u, cp = gn_mix((uint64_t)cx->program->op_cp, 1u),
             cell = gn_mix((uint64_t)t->cons, 2u);
    while (n--)
        h = gn_mix(gn_mix(cell, gn_mix(cp, gn_mix(GN_HASH_INT, (uint64_t)cx->scalars[n])) | 1u), h) | 1u;
    return t->hash = h;
}

/* V's hash, kept in each constructor it computes; 0 when a child is still
 * a hole. */
static uint64_t gn_hash(GnCtx *cx, const GnVal *v) {
    uint64_t known = gn_hash_known(cx, v);
    if (known)
        return known;
    struct GnHashFrame *st = cx->hash_stack;
    size_t len = 0, cap = cx->hash_cap;
    uint64_t out = 0;
#define GN_HASH_PUSH(val) do { \
        if (len == cap) { \
            size_t ncap = cap ? cap * 2u : 64u; \
            struct GnHashFrame *g = realloc(st, ncap * sizeof *g); \
            if (!g) { cx->hash_stack = st; cx->hash_cap = cap; longjmp(cx->oom, 1); } \
            st = g; \
            cap = ncap; \
        } \
        st[len++] = (struct GnHashFrame){(val), 0u, gn_mix((uint64_t)(val)->op, (uint64_t)(val)->n)}; \
    } while (0)
    GN_HASH_PUSH((GnVal *)v);
    while (len) {
        struct GnHashFrame *f = &st[len - 1u];
        if (f->k == f->v->n) {
            uint64_t h = f->h | 1u;
            f->v->i = (int64_t)h;
            if (--len == 0u)
                out = h;
            else
                st[len - 1u].h = gn_mix(st[len - 1u].h, h), st[len - 1u].k++;
            continue;
        }
        const GnVal *kid = f->v->kid[f->k];
        if (!kid)
            break;
        uint64_t h = gn_hash_known(cx, kid);
        if (h) {
            f->h = gn_mix(f->h, h);
            f->k++;
        } else {
            GN_HASH_PUSH((GnVal *)kid);
        }
    }
#undef GN_HASH_PUSH
    cx->hash_stack = st;
    cx->hash_cap = cap;
    return out;
}

/* Equality without the work stack while the terms are small and not
 * lists: children but the last by recursion (to a bounded depth), the last
 * by iteration, so a chain of constructors costs no stack.  Constructors
 * whose hashes differ are unequal. */
static bool gn_eq_fast(GnCtx *cx, const GnVal *x, const GnVal *y, unsigned depth) {
    for (;;) {
        if (x == y)
            return true;
        if (depth > 32u || gn_listlike(cx, x) || gn_listlike(cx, y) || x->op == GN_SPAN ||
            y->op == GN_SPAN)
            return gn_eq_walk(cx, x, y);
        if (x->op != y->op || x->n != y->n || (x->op == GN_INT && x->i != y->i))
            return false;
        if (x->n == 0u)
            return true;
        uint64_t hx = gn_hash(cx, x), hy = gn_hash(cx, y);
        if (hx && hy && hx != hy)
            return false;
        for (uint32_t k = 0; k + 1u < x->n; k++)
            if (!gn_eq_fast(cx, x->kid[k], y->kid[k], depth + 1u))
                return false;
        x = x->kid[x->n - 1u];
        y = y->kid[y->n - 1u];
    }
}

bool gn_eq(GnCtx *cx, const GnVal *a, const GnVal *b) { return gn_eq_fast(cx, a, b, 0u); }

/* Span X against the list Y: a span of the same constructors by its bytes
 * (the decoding is one to one), cells of X's CONS scalar by scalar. */
static bool gn_span_eq(GnCtx *cx, const GnVal *x, const GnVal *y) {
    const GnSpanTailV1 *xt = gn_span_tail(x);
    uint32_t op_cp = cx->program->op_cp;
    size_t pos = (size_t)x->i;
    for (;;) {
        if (pos >= xt->end)
            return y->op == xt->nil && y->n == 0u;
        if (y->op == GN_SPAN) {
            const GnSpanTailV1 *yt = gn_span_tail(y);
            size_t xr = xt->end - pos, yr = yt->end - (size_t)y->i;
            return yt->cons == xt->cons && yt->nil == xt->nil && xr == yr &&
                   memcmp(x->src->bytes + pos, y->src->bytes + y->i, xr) == 0;
        }
        if (y->op != xt->cons || y->n != 2u)
            return false;
        const GnVal *h = y->kid[0];
        uint32_t c;
        pos += gn_utf8_next(x->src, pos, &c);
        if (h->op != op_cp || h->n != 1u || h->kid[0]->op != GN_INT || h->kid[0]->i != (int64_t)c)
            return false;
        y = y->kid[1];
    }
}

static bool gn_eq_walk(GnCtx *cx, const GnVal *a, const GnVal *b) {
    if (a == b)
        return true;
    /* different kinds of constructor, or different integers, at once */
    if (gn_op_of(a) != gn_op_of(b) && a->op != GN_CURSOR && b->op != GN_CURSOR &&
        !gn_listlike(cx, a) && !gn_listlike(cx, b))
        return false;
    GnPairs s = {cx->pairs, 0u, cx->pairs_cap};
    bool equal = true;
    gn_push(cx, &s, a, b);
    while (equal && s.len) {
        const GnVal *y = s.items[--s.len], *x = s.items[--s.len];
        if (x == y)
            continue;
        if (x->op == GN_SPAN || y->op == GN_SPAN) {
            equal = x->op == GN_SPAN ? gn_span_eq(cx, x, y) : gn_span_eq(cx, y, x);
            continue;
        }
        if (gn_listlike(cx, x) || gn_listlike(cx, y)) {
            for (;;) {
                if (x->op == GN_CURSOR && y->op == GN_CURSOR) {
                    /* the same suffix of the same source: equal without a walk */
                    if (x->src == y->src && x->i == y->i)
                        break;
                    /* the decoding is one to one: equal scalars, equal bytes */
                    size_t xr = x->src->len - (size_t)x->i, yr = y->src->len - (size_t)y->i;
                    equal = xr == yr && memcmp(x->src->bytes + x->i, y->src->bytes + y->i, xr) == 0;
                    break;
                }
                bool xn = gn_is_nil(cx, x), yn = gn_is_nil(cx, y);
                if (xn || yn) {
                    equal = xn && yn;
                    break;
                }
                GnVal *xh, *xt, *yh, *yt;
                if (!gn_uncons(cx, (GnVal *)x, &xh, &xt) || !gn_uncons(cx, (GnVal *)y, &yh, &yt)) {
                    equal = false;
                    break;
                }
                gn_push(cx, &s, xh, yh);
                x = xt;
                y = yt;
                if (x == y)
                    break;
            }
            continue;
        }
        if (x->op != y->op || x->n != y->n || (x->op == GN_INT && x->i != y->i) ||
            (x->n && x->i && y->i && x->i != y->i)) {
            equal = false;
            continue;
        }
        for (uint32_t k = 0; k < x->n; k++)
            gn_push(cx, &s, x->kid[k], y->kid[k]);
    }
    return equal;
}

/* The printer's output, written out once at the end. */
typedef struct {
    char *bytes;
    size_t len, cap;
} GnText;

static void gn_text_grow(GnCtx *cx, GnText *t, size_t n);

static inline void gn_text_put(GnCtx *cx, GnText *t, const char *s, size_t n) {
    if (t->len + n > t->cap)
        gn_text_grow(cx, t, n);
    memcpy(t->bytes + t->len, s, n);
    t->len += n;
}

static inline void gn_text_char(GnCtx *cx, GnText *t, char c) {
    if (t->len + 1u > t->cap)
        gn_text_grow(cx, t, 1u);
    t->bytes[t->len++] = c;
}

static void gn_text_grow(GnCtx *cx, GnText *t, size_t n) {
    if (t->len + n > t->cap) {
        size_t cap = t->cap ? t->cap * 2u : 4096u;
        while (cap < t->len + n)
            cap *= 2u;
        char *grown = realloc(t->bytes, cap);
        if (!grown) {
            free(t->bytes);
            longjmp(cx->oom, 1);
        }
        t->bytes = grown;
        t->cap = cap;
    }
}

static void gn_text_str(GnCtx *cx, GnText *t, const char *s) { gn_text_put(cx, t, s, strlen(s)); }

static void gn_text_int(GnCtx *cx, GnText *t, int64_t i) {
    char digits[24];
    size_t n = sizeof digits;
    uint64_t u = i < 0 ? (uint64_t)0 - (uint64_t)i : (uint64_t)i;
    do
        digits[--n] = (char)('0' + u % 10u);
    while (u /= 10u);
    if (i < 0)
        digits[--n] = '-';
    gn_text_put(cx, t, digits + n, sizeof digits - n);
}

void gn_print(GnCtx *cx, FILE *out, const GnVal *v) {
    /* (value, next child to print) frames */
    typedef struct {
        const GnVal *v;
        uint32_t k;
    } Frame;
    Frame *stack = NULL;
    size_t len = 0, cap = 0;
    GnText t = {0};
    const GnProgramV1 *pr = cx->program;
    const char *cons = pr->ops[pr->op_cons - GN_FIRST_OP].name, *cp = pr->ops[pr->op_cp - GN_FIRST_OP].name,
               *nil = pr->ops[pr->op_nil - GN_FIRST_OP].name;
#define GN_PUSH(val) do { \
        if (len == cap) { \
            cap = cap ? cap * 2u : 64u; \
            Frame *g = realloc(stack, cap * sizeof *g); \
            if (!g) { free(stack); free(t.bytes); longjmp(cx->oom, 1); } \
            stack = g; \
        } \
        stack[len++] = (Frame){(val), 0u}; \
    } while (0)
    GN_PUSH(v);
    while (len) {
        Frame *f = &stack[len - 1u];
        const GnVal *x = f->v;
        if (x->op == GN_INT) {
            gn_text_int(cx, &t, x->i);
            len--;
        } else if (x->op == GN_CURSOR) {
            /* the list it reads */
            size_t scalars = 0;
            for (size_t p = (size_t)x->i; p < x->src->len; scalars++) {
                uint32_t c;
                p += gn_utf8_next(x->src, p, &c);
                gn_text_char(cx, &t, '(');
                gn_text_str(cx, &t, cons);
                gn_text_put(cx, &t, " (", 2u);
                gn_text_str(cx, &t, cp);
                gn_text_char(cx, &t, ' ');
                gn_text_int(cx, &t, c);
                gn_text_put(cx, &t, ") ", 2u);
            }
            gn_text_str(cx, &t, nil);
            for (size_t k = 0; k < scalars; k++)
                gn_text_char(cx, &t, ')');
            len--;
        } else if (x->op == GN_SPAN) {
            /* the list it reads, in the program's constructors */
            const GnSpanTailV1 *st = gn_span_tail(x);
            uint32_t sc = st->cons - GN_FIRST_OP, sn = st->nil - GN_FIRST_OP;
            size_t scalars = 0;
            for (size_t p = (size_t)x->i; p < st->end; scalars++) {
                uint32_t c;
                p += gn_utf8_next(x->src, p, &c);
                gn_text_char(cx, &t, '(');
                gn_text_put(cx, &t, pr->ops[sc].name, cx->name_len[sc]);
                gn_text_put(cx, &t, " (", 2u);
                gn_text_str(cx, &t, cp);
                gn_text_char(cx, &t, ' ');
                gn_text_int(cx, &t, c);
                gn_text_put(cx, &t, ") ", 2u);
            }
            gn_text_put(cx, &t, pr->ops[sn].name, cx->name_len[sn]);
            for (size_t k = 0; k < scalars; k++)
                gn_text_char(cx, &t, ')');
            len--;
        } else if (x->n == 0u) {
            gn_text_put(cx, &t, pr->ops[x->op - GN_FIRST_OP].name, cx->name_len[x->op - GN_FIRST_OP]);
            len--;
        } else if (f->k == 0u) {
            gn_text_char(cx, &t, '(');
            gn_text_put(cx, &t, pr->ops[x->op - GN_FIRST_OP].name, cx->name_len[x->op - GN_FIRST_OP]);
            gn_text_char(cx, &t, ' ');
            f->k = 1u;
            GN_PUSH(x->kid[0]);
        } else if (f->k < x->n) {
            uint32_t k = f->k++;
            gn_text_char(cx, &t, ' ');
            GN_PUSH(x->kid[k]);
        } else {
            gn_text_char(cx, &t, ')');
            len--;
        }
    }
#undef GN_PUSH
    free(stack);
    if (t.len)
        fwrite(t.bytes, 1u, t.len, out);
    free(t.bytes);
}

/* ── reading values ───────────────────────────────────────────────────── */

static long gn_op_id(GnCtx *cx, const char *name, size_t len, uint32_t arity) {
    const GnProgramV1 *p = cx->program;
    for (uint32_t i = 0; i < p->op_count; i++)
        if (p->ops[i].arity == arity && strlen(p->ops[i].name) == len && memcmp(p->ops[i].name, name, len) == 0)
            return (long)i + GN_FIRST_OP;
    return -1;
}

static const char *gn_skip(const char *t) {
    while (*t == ' ' || *t == '\t' || *t == '\n' || *t == '\r')
        t++;
    return t;
}

static bool gn_atom_char(char c) {
    return c && c != '(' && c != ')' && c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

GnVal *gn_parse(GnCtx *cx, const char *text, const char **end) {
    const char *t = gn_skip(text);
    if (*t == '(') {
        t = gn_skip(t + 1);
        const char *name = t;
        while (gn_atom_char(*t))
            t++;
        size_t len = (size_t)(t - name);
        GnVal *kids[64];
        uint32_t n = 0;
        for (;;) {
            t = gn_skip(t);
            if (*t == ')' || !*t)
                break;
            if (n == 64u)
                return NULL;
            const char *next;
            kids[n] = gn_parse(cx, t, &next);
            if (!kids[n++])
                return NULL;
            t = next;
        }
        if (*t != ')')
            return NULL;
        long id = len ? gn_op_id(cx, name, len, n) : -1;
        if (id < 0)
            return NULL;
        GnVal *v = gn_node(cx, (uint32_t)id, n);
        for (uint32_t k = 0; k < n; k++)
            v->kid[k] = kids[k];
        *end = t + 1;
        return v;
    }
    const char *start = t;
    while (gn_atom_char(*t))
        t++;
    if (t == start)
        return NULL;
    *end = t;
    char *stop;
    long long i = strtoll(start, &stop, 10);
    if (stop == t)
        return gn_int(cx, (int64_t)i);
    long id = gn_op_id(cx, start, (size_t)(t - start), 0u);
    return id < 0 ? NULL : gn_const(cx, (uint32_t)id);
}

#ifdef GN_TRACE
void gn_ctx_set_trace(GnCtx *cx, FILE *out) { cx->trace = out; }

static FILE *gn_trace_out(GnCtx *cx) { return cx->trace ? cx->trace : stderr; }

static void gn_trace_indent(GnCtx *cx) {
    FILE *out = gn_trace_out(cx);
    for (unsigned i = 0; i < cx->trace_depth && i < 200u; i++)
        fputs("  ", out);
}

/* V to DEPTH levels; cursors by position. */
static void gn_trace_value(GnCtx *cx, FILE *out, const GnVal *v, unsigned depth) {
    const GnProgramV1 *pr = cx->program;
    if (!v) {
        fputs("_", out);
    } else if (v->op == GN_INT) {
        fprintf(out, "%lld", (long long)v->i);
    } else if (v->op == GN_CURSOR) {
        fprintf(out, "(gslt:cursor %lld)", (long long)v->i);
    } else if (v->op == GN_SPAN) {
        fprintf(out, "(gslt:span %lld %zu)", (long long)v->i, gn_span_tail(v)->end);
    } else if (v->op < GN_FIRST_OP || v->op - GN_FIRST_OP >= pr->op_count) {
        fputs("?", out);
    } else if (v->n == 0u) {
        fputs(pr->ops[v->op - GN_FIRST_OP].name, out);
    } else if (depth == 0u) {
        fprintf(out, "(%s ...)", pr->ops[v->op - GN_FIRST_OP].name);
    } else {
        fprintf(out, "(%s", pr->ops[v->op - GN_FIRST_OP].name);
        for (uint32_t k = 0; k < v->n; k++) {
            fputc(' ', out);
            gn_trace_value(cx, out, v->kid[k], depth - 1u);
        }
        fputc(')', out);
    }
}

void gn_trace_call(GnCtx *cx, const char *mode, size_t n, GnVal *const *args) {
    FILE *out = gn_trace_out(cx);
    gn_trace_indent(cx);
    fprintf(out, "(call %s", mode);
    for (size_t i = 0; i < n; i++) {
        fputc(' ', out);
        gn_trace_value(cx, out, args[i], GN_TRACE_DEPTH);
    }
    fputs(")\n", out);
    cx->trace_depth++;
}

void gn_trace_rule(GnCtx *cx, const char *rule, bool answered) {
    if (answered && cx->trace_depth)
        cx->trace_depth--;
    gn_trace_indent(cx);
    fprintf(gn_trace_out(cx), "(%s %s)\n", answered ? "answer" : "miss", rule);
}

void gn_trace_fail(GnCtx *cx, const char *mode) {
    if (cx->trace_depth)
        cx->trace_depth--;
    gn_trace_indent(cx);
    fprintf(gn_trace_out(cx), "(none %s)\n", mode);
}
#endif
