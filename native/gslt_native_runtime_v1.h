#ifndef CETTA_GSLT_NATIVE_RUNTIME_V1_H
#define CETTA_GSLT_NATIVE_RUNTIME_V1_H

/*
 * The runtime of GSLT relations compiled to C (gslt_native_direct_v1.c).
 *
 * A value is a constructor application (an operator of the program and
 * its children), an integer, a cursor or a span.  A cursor is a suffix of
 * a source's Unicode scalars, read as the list (cons (cp N) ...) ending in
 * nil that langdef makes of a file.  A span is a nonempty stretch of a
 * source's scalars read as a list of the program's own constructors,
 * (CONS (cp N) ... NIL): a compiled scan returns the text it took as a span
 * instead of building it cell by cell.  Every operation (matching,
 * equality, hashing, printing, copying out) reads a cursor or a span as the
 * list it stands for.  A source is the file's bytes, decoded as the cursor
 * moves: UTF-8, a byte that is not UTF-8 read as U+DC80..U+DCFF (surrogate
 * escape), so every text reads back to its bytes.  Values are immutable and live in the context's arena
 * until the context is freed.  Running out of memory jumps to the jump
 * buffer the caller set with GN_TRY.
 */

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *name;
    uint32_t arity;
} GnOpV1;

typedef struct {
    const GnOpV1 *ops;      /* operator op - 2 is ops[op - 2] */
    uint32_t op_count;
    uint32_t op_cons, op_nil, op_cp;
} GnProgramV1;

/* Value kinds, then the program's operators from GN_FIRST_OP on: a
 * compiled program's operator table starts with the input list's cons, nil
 * and cp (gn_ctx_new checks). */
enum { GN_INT = 0, GN_SPAN = 1, GN_CURSOR = 2, GN_FIRST_OP = 3,
       GN_OP_CONS = 3, GN_OP_NIL = 4, GN_OP_CP = 5 };

typedef struct {
    const uint8_t *bytes;
    size_t len;
} GnSourceV1;

/* The scalar at byte POS of SRC (POS < len, at a scalar's start) and the
 * bytes it takes: UTF-8, or one byte read as U+DC80..U+DCFF where the bytes
 * are not UTF-8 (overlong forms and surrogates included). */
static inline size_t gn_utf8_next(const GnSourceV1 *src, size_t pos, uint32_t *scalar) {
    const uint8_t *b = src->bytes + pos;
    size_t n = src->len - pos;
    uint8_t c = b[0];
    if (c < 0x80u) {
        *scalar = c;
        return 1u;
    }
    size_t w = (c >> 5) == 6u ? 2u : (c >> 4) == 14u ? 3u : (c >> 3) == 30u ? 4u : 0u;
    uint32_t v = w ? c & (0x3Fu >> (w - 1u)) : c;
    bool ok = w > 1u && w <= n;
    for (size_t j = 1; ok && j < w; j++) {
        ok = (b[j] & 0xC0u) == 0x80u;
        v = (v << 6) | (b[j] & 0x3Fu);
    }
    if (ok && ((w == 2u && v < 0x80u) || (w == 3u && v < 0x800u) || (w == 4u && v < 0x10000u) ||
               v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)))
        ok = false;
    if (!ok) {
        *scalar = 0xDC00u + c;
        return 1u;
    }
    *scalar = v;
    return w;
}

typedef struct GnVal GnVal;
struct GnVal {
    uint32_t op;
    uint16_t n;               /* children */
    uint16_t space;           /* the arena holding it (the runtime's own) */
    int64_t i;                /* GN_INT: the integer; GN_CURSOR, GN_SPAN: the byte
                                 position; a constructor: its hash once known */
    const GnSourceV1 *src;    /* GN_CURSOR, GN_SPAN */
    GnVal *kid[];             /* a span keeps a GnSpanTailV1 here */
};

/* A span's other half: it reads bytes I to END of SRC as the list of CONS
 * and NIL, and keeps that list's hash once known (0 before). */
typedef struct {
    size_t end;
    uint32_t cons, nil;
    uint64_t hash;
} GnSpanTailV1;

static inline GnSpanTailV1 *gn_span_tail(const GnVal *v) {
    return (GnSpanTailV1 *)(void *)&((GnVal *)v)->kid[0];
}

typedef struct GnCtx GnCtx;

/* A compiled program's entry in the uniform calling form: inputs and
 * outputs in arrays (PREFIX_entries, PREFIX_entry_count). */
typedef struct {
    const char *name;         /* RELATION:BITS */
    uint32_t inputs, outputs;
    bool (*call)(GnCtx *cx, GnVal **in, GnVal **out);
} GnEntryV1;

GnCtx *gn_ctx_new(const GnProgramV1 *program);
void gn_ctx_free(GnCtx *cx);
jmp_buf *gn_ctx_oom(GnCtx *cx);
/* Run `body` with out-of-memory caught: false when memory ran out. */
#define GN_TRY(cx, body) (setjmp(*gn_ctx_oom(cx)) == 0 ? ((body), true) : false)

/* Lifetimes (gslt_native_runtime_v1.c): a reading step allocates in
 * scratch; gn_scratch_reset frees it all.  What outlives the step is copied
 * out first, sharing preserved: the reader state with gn_keep_state (the
 * previous kept state is freed), a reading the caller keeps with
 * gn_keep_result. */
void gn_scratch_reset(GnCtx *cx);
GnVal *gn_keep_state(GnCtx *cx, const GnVal *v);
GnVal *gn_keep_result(GnCtx *cx, const GnVal *v);

GnVal *gn_cursor(GnCtx *cx, const GnSourceV1 *src, size_t pos);
/* (cp N) for the scalar N. */
GnVal *gn_scalar(GnCtx *cx, uint32_t scalar);
GnVal *gn_int(GnCtx *cx, int64_t i);
GnVal *gn_const(GnCtx *cx, uint32_t op);
GnVal *gn_appv(GnCtx *cx, uint32_t op, uint32_t n, ...);
GnVal *gn_app1(GnCtx *cx, uint32_t op, GnVal *a);
GnVal *gn_app2(GnCtx *cx, uint32_t op, GnVal *a, GnVal *b);
GnVal *gn_app3(GnCtx *cx, uint32_t op, GnVal *a, GnVal *b, GnVal *c);
GnVal *gn_app4(GnCtx *cx, uint32_t op, GnVal *a, GnVal *b, GnVal *c, GnVal *d);
#define gn_app5(cx, op, ...) gn_appv(cx, op, 5u, __VA_ARGS__)
#define gn_app6(cx, op, ...) gn_appv(cx, op, 6u, __VA_ARGS__)
#define gn_app7(cx, op, ...) gn_appv(cx, op, 7u, __VA_ARGS__)
#define gn_app8(cx, op, ...) gn_appv(cx, op, 8u, __VA_ARGS__)
#define gn_app9(cx, op, ...) gn_appv(cx, op, 9u, __VA_ARGS__)
#define gn_app10(cx, op, ...) gn_appv(cx, op, 10u, __VA_ARGS__)
#define gn_app11(cx, op, ...) gn_appv(cx, op, 11u, __VA_ARGS__)
#define gn_app12(cx, op, ...) gn_appv(cx, op, 12u, __VA_ARGS__)
#define gn_app13(cx, op, ...) gn_appv(cx, op, 13u, __VA_ARGS__)
#define gn_app14(cx, op, ...) gn_appv(cx, op, 14u, __VA_ARGS__)
#define gn_app15(cx, op, ...) gn_appv(cx, op, 15u, __VA_ARGS__)
#define gn_app16(cx, op, ...) gn_appv(cx, op, 16u, __VA_ARGS__)

/* Structural equality; a cursor or a span equals the list it reads. */
bool gn_eq(GnCtx *cx, const GnVal *a, const GnVal *b);

/* The scalars of SRC from byte START to byte END as the list (CONS (cp N)
 * ... NIL): a span, or NIL itself when the stretch is empty. */
GnVal *gn_span(GnCtx *cx, const GnSourceV1 *src, size_t start, size_t end, uint32_t cons, uint32_t nil);
/* (CONS H T) for CP, a (cp N), and T: when T is a span of CONS that N's own
 * bytes in the source precede, the span from those bytes; otherwise a cell. */
GnVal *gn_cons_cp(GnCtx *cx, uint32_t cons, GnVal *cp, GnVal *t);
/* The first scalar of span V, as (cp N), and the rest, a span or NIL. */
void gn_uncons_span(GnCtx *cx, GnVal *v, GnVal **head, GnVal **tail);
/* Whether V is the list of the N scalars S under CONS and NIL. */
bool gn_is_scalars(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil, const uint32_t *s, size_t n);
/* The same, for a literal whose BYTES (LEN of them) read back as S: a span
 * is compared by its bytes, its length first (the decoding is one to one). */
static inline bool gn_is_text(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil, const uint32_t *s, size_t n,
                              const char *bytes, size_t len);

/* A text's key: FNV-1a over the bytes its scalars stand for in a source
 * (an escaped byte as itself, the others as UTF-8), so equal texts have
 * equal keys whether spans or cells.  The C target computes literals' keys
 * with the same function. */
static inline uint64_t gn_text_hash(uint64_t h, const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++)
        h = (h ^ bytes[i]) * 0x100000001b3ull;
    return h;
}
#define GN_TEXT_HASH_BASIS 0xcbf29ce484222325ull
/* V's key as a text of CONS and NIL; false when V is no such text.  A span
 * hashes its bytes here; cells walk in gn_text_key_cells. */
bool gn_text_key_cells(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil, uint64_t *key);
static inline bool gn_text_key(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil, uint64_t *key) {
    if (v->op == GN_SPAN) {
        const GnSpanTailV1 *t = gn_span_tail(v);
        if (t->cons != cons || t->nil != nil)
            return false;
        *key = gn_text_hash(GN_TEXT_HASH_BASIS, v->src->bytes + v->i, t->end - (size_t)v->i);
        return true;
    }
    return gn_text_key_cells(cx, v, cons, nil, key);
}

/* V's operator as a pattern sees it: a span's is its CONS. */
static inline uint32_t gn_op_of(const GnVal *v) {
    return v->op == GN_SPAN ? gn_span_tail(v)->cons : v->op;
}
/* (OP h t) for a binary operator OP: from an application of OP, or from a
 * span of OP. */
static inline bool gn_uncons_op(GnCtx *cx, GnVal *v, uint32_t op, GnVal **head, GnVal **tail) {
    if (v->op == op) {
        *head = v->kid[0];
        *tail = v->kid[1];
        return true;
    }
    if (v->op == GN_SPAN && gn_span_tail(v)->cons == op) {
        gn_uncons_span(cx, v, head, tail);
        return true;
    }
    return false;
}

static inline __attribute__((always_inline)) bool gn_is_text(GnCtx *cx, const GnVal *v, uint32_t cons, uint32_t nil,
                                                             const uint32_t *s, size_t n, const char *bytes,
                                                             size_t len) {
    if (v->op == GN_SPAN) {
        const GnSpanTailV1 *t = gn_span_tail(v);
        return t->end - (size_t)v->i == len && v->src->bytes[v->i] == (uint8_t)bytes[0] && t->cons == cons &&
               t->nil == nil && __builtin_memcmp(v->src->bytes + v->i, bytes, len) == 0;
    }
    if (v->op != cons)
        return n == 0u && v->op == nil;
    /* a cell: its first scalar at once, then the rest */
    const GnVal *h = v->kid[0];
    if (h->op != GN_OP_CP || h->kid[0]->op != GN_INT || h->kid[0]->i != (int64_t)s[0])
        return false;
    return gn_is_scalars(cx, v, cons, nil, s, n);
}

/* A class of scalars: those below 128 by bit, the others by sorted
 * disjoint closed ranges. */
typedef struct {
    uint64_t ascii[2];
    const uint32_t (*ranges)[2];
    size_t range_count;
} GnClassV1;

static inline bool gn_class_has(const GnClassV1 *k, uint32_t c) {
    if (c < 128u)
        return (k->ascii[c >> 6] >> (c & 63u)) & 1u;
    size_t lo = 0, hi = k->range_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (c < k->ranges[mid][0])
            hi = mid;
        else if (c > k->ranges[mid][1])
            lo = mid + 1u;
        else
            return true;
    }
    return false;
}

/* A maximal run of class K at the start of the source list V (a cursor, or
 * the input list's nil): *run is the scalars taken, a span of CONS and NIL
 * (NIL when none), and *after the list after them.  False when V is not a
 * source list. */
bool gn_scan_class(GnCtx *cx, GnVal *v, const GnClassV1 *k, uint32_t cons, uint32_t nil,
                   GnVal **run, GnVal **after);
bool gn_uncons_cursor(GnCtx *cx, GnVal *v, GnVal **head, GnVal **tail);

static inline bool gn_is_int(const GnVal *v, int64_t i) { return v->op == GN_INT && v->i == i; }

static inline bool gn_is_nil(GnCtx *cx, const GnVal *v) {
    (void)cx;
    if (v->op == GN_CURSOR)
        return (size_t)v->i >= v->src->len;
    return v->op == GN_OP_NIL && v->n == 0u;
}

/* (cons h t): from a cons application or a cursor not at its end. */
static inline bool gn_uncons(GnCtx *cx, GnVal *v, GnVal **head, GnVal **tail) {
    if (v->op == GN_OP_CONS && v->n == 2u) {
        *head = v->kid[0];
        *tail = v->kid[1];
        return true;
    }
    return v->op == GN_CURSOR && gn_uncons_cursor(cx, v, head, tail);
}

/* The value in PeTTa's spelling. */
void gn_print(GnCtx *cx, FILE *out, const GnVal *v);
/* A value from that spelling: integers, constructors of the program by name
 * and arity.  NULL when the text is not one value of the program; *end is
 * set past it. */
GnVal *gn_parse(GnCtx *cx, const char *text, const char **end);
const GnProgramV1 *gn_ctx_program(const GnCtx *cx);

/* A trace of a run, chosen when the compiled relations are built: with
 * -DGN_TRACE, each mode entered (with its inputs), each rule that answers
 * or misses, and each mode without an answer is written to the context's
 * trace stream (stderr unless gn_ctx_set_trace names another), indented by
 * call depth; a rule that answers by a jump is followed by the mode it
 * jumps to.  Values are shown to GN_TRACE_DEPTH levels and cursors by
 * position.  A trace is a reading aid: nothing checks it.  Without
 * GN_TRACE the hooks are no code at all. */
#ifdef GN_TRACE
#ifndef GN_TRACE_DEPTH
#define GN_TRACE_DEPTH 4
#endif
void gn_ctx_set_trace(GnCtx *cx, FILE *out);
void gn_trace_call(GnCtx *cx, const char *mode, size_t n, GnVal *const *args);
void gn_trace_rule(GnCtx *cx, const char *rule, bool answered);
void gn_trace_fail(GnCtx *cx, const char *mode);
#define GN_TRACE_CALL(cx, mode, n, ...) gn_trace_call((cx), (mode), (n), (GnVal *const[]){__VA_ARGS__})
#define GN_TRACE_RULE(cx, rule) gn_trace_rule((cx), (rule), true)
#define GN_TRACE_MISS(cx, rule) gn_trace_rule((cx), (rule), false)
#define GN_TRACE_FAIL(cx, mode) gn_trace_fail((cx), (mode))
#else
#define GN_TRACE_CALL(cx, mode, n, ...) ((void)0)
#define GN_TRACE_RULE(cx, rule) ((void)0)
#define GN_TRACE_MISS(cx, rule) ((void)0)
#define GN_TRACE_FAIL(cx, mode) ((void)0)
#endif

#endif
