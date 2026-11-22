/* rc_rt.c - runtime of procedures compiled by rulec (see rc_rt.h). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rc_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

RC_HEAP_LOCAL char *rc_hp, *rc_hlim;
RC_HEAP_LOCAL RcStats rc_stats;
RC_HEAP_LOCAL T *rc_roots[8];
TH rc_empty_bytes = {0, RC_SPACE_STATIC, RC_FLAG_BYTES, 0};
#ifdef RC_PROFILE
uint64_t rc_prof_objects;
#endif

/* --------------------------------------------------------------------- chunks */
typedef struct Chunk { struct Chunk *next; char *start, *end, *top; } Chunk;

static Chunk *chunk_new(size_t bytes) {
    Chunk *c = malloc(sizeof *c);
    if (!c) { fputs("rc: out of memory (chunk)\n", stderr); exit(3); }
    c->start = aligned_alloc(64, (bytes + 63) & ~(size_t)63);
    if (!c->start) { fputs("rc: out of memory (chunk)\n", stderr); exit(3); }
    c->end = c->start + bytes;
    c->top = c->start;
    c->next = NULL;
    return c;
}
static void chunks_free(Chunk *c) { while (c) { Chunk *n = c->next; free(c->start); free(c); c = n; } }

static size_t young_chunk_bytes = (size_t)8 << 20;
static RC_HEAP_LOCAL Chunk *young_first, *young_cur;
/* the young chunks after the first are kept for the next run (or the next fill after a minor
 * collection) up to this many bytes in all, so that a run that fills as much as an earlier one
 * writes memory already mapped instead of faulting in fresh pages; the rest is freed */
static size_t young_retain_bytes = (size_t)256 << 20;
void rc_runtime_retain(size_t bytes) { young_retain_bytes = bytes; }
static void young_rewind(void) {
    size_t kept = (size_t)(young_first->end - young_first->start);
    Chunk *c = young_first;
    while (c->next && kept + (size_t)(c->next->end - c->next->start) <= young_retain_bytes) {
        c = c->next;
        kept += (size_t)(c->end - c->start);
    }
    chunks_free(c->next);
    c->next = NULL;
    young_cur = young_first;
    rc_hp = young_first->start;
    rc_hlim = young_first->end;
}
static RC_HEAP_LOCAL Chunk *old_first, *old_cur;
static RC_HEAP_LOCAL char *old_hp;
static RC_HEAP_LOCAL uint8_t cur_old = RC_SPACE_OLD0;
static RC_HEAP_LOCAL uint64_t old_bytes_in_use, old_live_after_major;
static RC_HEAP_LOCAL uint64_t major_threshold = (uint64_t)64 << 20;

void rc_runtime_init(size_t young_chunk) {
    if (young_chunk) young_chunk_bytes = young_chunk;
    if (!young_first) {
        young_first = young_cur = chunk_new(young_chunk_bytes);
        rc_hp = young_cur->start;
        rc_hlim = young_cur->end;
    }
}

void rc_heap_swap(RcHeap *h) {
    RcHeap cur = {young_first, young_cur, old_first, old_cur, rc_hp, rc_hlim, old_hp, cur_old,
                  old_bytes_in_use, old_live_after_major, major_threshold,
                  {0}, rc_pend, rc_pend_depth, rc_pend_cap, rc_stop_mark, rc_root_slots, rc_root_nslots};
    memcpy(cur.roots, rc_roots, sizeof cur.roots);
    if (young_cur) young_cur->top = rc_hp;
    young_first = h->young_first; young_cur = h->young_cur;
    old_first = h->old_first; old_cur = h->old_cur;
    rc_hp = h->hp; rc_hlim = h->hlim; old_hp = h->old_hp;
    cur_old = h->cur_old ? h->cur_old : RC_SPACE_OLD0;
    old_bytes_in_use = h->old_bytes_in_use; old_live_after_major = h->old_live_after_major;
    major_threshold = h->major_threshold ? h->major_threshold : (uint64_t)64 << 20;
    memcpy(rc_roots, h->roots, sizeof cur.roots);
    rc_pend = h->pend; rc_pend_depth = h->pend_depth; rc_pend_cap = h->pend_cap;
    rc_stop_mark = h->stop_mark;
    rc_root_slots = h->root_slots; rc_root_nslots = h->root_nslots;
    *h = cur;
}
void rc_heap_free(RcHeap *h) {
    chunks_free(h->young_first);
    chunks_free(h->old_first);
    free(h->pend);
    *h = (RcHeap){0};
}

uint64_t rc_young_in_use(void) {
    uint64_t n = 0;
    for (Chunk *c = young_first; c; c = c->next) {
        if (c == young_cur) { n += (uint64_t)(rc_hp - c->start); break; }
        n += (uint64_t)(c->top - c->start);
    }
    return n;
}
uint64_t rc_old_in_use(void) { return old_bytes_in_use; }

T *rc_alloc_slow(size_t b) {
    if (!young_cur) {   /* an empty heap: its first young chunk */
        young_first = young_cur = chunk_new(b > young_chunk_bytes ? b : young_chunk_bytes);
        young_cur->top = young_cur->start;
        rc_hp = young_cur->start + b;
        rc_hlim = young_cur->end;
        return (T *)young_cur->start;
    }
    young_cur->top = rc_hp;
    if (young_cur->next && (size_t)(young_cur->next->end - young_cur->next->start) >= b) {
        young_cur = young_cur->next;
    } else {
        Chunk *c = chunk_new(b > young_chunk_bytes ? b : young_chunk_bytes);
        c->next = young_cur->next;
        young_cur->next = c;
        young_cur = c;
    }
    young_cur->top = young_cur->start;
    rc_hp = young_cur->start + b;
    rc_hlim = young_cur->end;
    return (T *)young_cur->start;
}

static inline size_t obj_size(const T *x) {
    if (x->flags & RC_FLAG_BYTES) {
        size_t pay = ((size_t)x->n + 7u) & ~(size_t)7u;
        return sizeof(TH) + (pay < 8 ? 8 : pay);
    }
    return sizeof(TH) + (size_t)x->n * sizeof(T *);
}

T *rc_bytes_new(const uint8_t *b, size_t n) {
    if (n == 0) return (T *)&rc_empty_bytes;
    if (n > UINT32_MAX) rc_refinement_fail("byte string too long");
    size_t pay = (n + 7u) & ~(size_t)7u;
    T *t = rc_alloc_bytes(sizeof(TH) + (pay < 8 ? 8 : pay));
    t->tag = 0; t->space = RC_SPACE_YOUNG; t->flags = RC_FLAG_BYTES; t->n = (uint32_t)n;
    if (b) memcpy(t->k, b, n);
    RC_PROF_OBJ();
    return t;
}

#include <sys/mman.h>
void *rc_alloc_table(size_t bytes) {
    size_t huge = (size_t)2 << 20;
    if (bytes < huge) return calloc(1, bytes ? bytes : 1);
    size_t rounded = (bytes + huge - 1) & ~(huge - 1);
    void *t = aligned_alloc(huge, rounded);
    if (!t) return NULL;
#ifdef MADV_HUGEPAGE
    madvise(t, rounded, MADV_HUGEPAGE);
#endif
    memset(t, 0, bytes);
    return t;
}

void (*rc_fail_hook)(const char *what);
_Noreturn void rc_refinement_fail(const char *what) {
    if (rc_fail_hook) rc_fail_hook(what);
    fprintf(stderr, "rc: refinement violation: %s\n", what);
    exit(5);
}

/* --------------------------------------------------------------- old space */
static RC_HEAP_LOCAL Chunk *to_first, *to_cur;  /* the space being filled during a copy */
static RC_HEAP_LOCAL char *to_hp;

static T *to_alloc(size_t b) {
    if (!to_cur || (size_t)(to_cur->end - to_hp) < b) {
        if (to_cur) to_cur->top = to_hp;
        size_t want = (size_t)32 << 20;
        Chunk *c = chunk_new(b > want ? b : want);
        if (to_cur) to_cur->next = c; else to_first = c;
        to_cur = c;
        to_hp = c->start;
    }
    T *r = (T *)to_hp;
    to_hp += b;
    return r;
}

static RC_HEAP_LOCAL uint8_t to_space_id;
static RC_HEAP_LOCAL uint64_t copied_bytes;

static inline T *evacuate(T *x, bool major) {
    if (!RC_IS_HEAP(x)) return x;                         /* immediate or foreign */
    if (x->space == RC_SPACE_STATIC) return x;
    if (!major && x->space != RC_SPACE_YOUNG) return x;  /* old: no old term points to a young one */
    if (major && x->space == to_space_id) return x;
    if (x->flags & RC_FLAG_FWD) return x->k[0];
    size_t sz = obj_size(x);
    T *y = to_alloc(sz);
    memcpy(y, x, sz);
    y->space = to_space_id;
    x->flags |= RC_FLAG_FWD;
    x->k[0] = y;
    copied_bytes += sz;
    return y;
}

/* Cheney scan from (chunk, pointer) to the end of the to-space */
static void scan_from(Chunk *c, char *p, bool major) {
    for (;;) {
        char *lim = c == to_cur ? to_hp : c->top;
        while (p < lim) {
            T *y = (T *)p;
            if (!(y->flags & RC_FLAG_BYTES))
                for (uint32_t i = 0; i < y->n; i++) y->k[i] = evacuate(y->k[i], major);
            p += obj_size(y);
            lim = c == to_cur ? to_hp : c->top;
        }
        if (c == to_cur) break;
        c = c->next;
        p = c->start;
    }
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void (*rc_extra_roots_hook)(int major, T *(*evac)(T *x));
RC_HEAP_LOCAL T **rc_root_slots;
RC_HEAP_LOCAL size_t rc_root_nslots;
static T *evac_minor_fn(T *x) { return evacuate(x, false); }
static T *evac_major_fn(T *x) { return evacuate(x, true); }

static void minor(int nroots) {
    /* promote into the current old space: continue its last chunk */
    to_first = old_first; to_cur = old_cur; to_hp = old_hp; to_space_id = cur_old;
    Chunk *scan_c = to_cur; char *scan_p = to_hp;
    copied_bytes = 0;
    for (int i = 0; i < nroots; i++) rc_roots[i] = evacuate(rc_roots[i], false);
    for (size_t f = 0; f < rc_pend_depth; f++)
        for (uint8_t i = 0; i < rc_pend[f].n; i++) if (i != rc_pend[f].k) rc_pend[f].a[i] = evacuate(rc_pend[f].a[i], false);
    for (size_t i = 0; i < rc_root_nslots; i++) rc_root_slots[i] = evacuate(rc_root_slots[i], false);
    if (rc_extra_roots_hook) rc_extra_roots_hook(0, evac_minor_fn);
    if (!scan_c) { scan_c = to_first; scan_p = scan_c ? scan_c->start : NULL; }
    if (scan_c) scan_from(scan_c, scan_p, false);
    old_first = to_first; old_cur = to_cur; old_hp = to_hp;
    old_bytes_in_use += copied_bytes;
    rc_stats.promoted_bytes += copied_bytes;
    rc_stats.minor_gcs++;
    /* reset the young space (its chunks kept up to the retention bound) */
    young_rewind();
}

static void major(int nroots) {
    Chunk *from = old_first;
    to_first = to_cur = NULL; to_hp = NULL;
    to_space_id = cur_old == RC_SPACE_OLD0 ? RC_SPACE_OLD1 : RC_SPACE_OLD0;
    copied_bytes = 0;
    for (int i = 0; i < nroots; i++) rc_roots[i] = evacuate(rc_roots[i], true);
    for (size_t f = 0; f < rc_pend_depth; f++)
        for (uint8_t i = 0; i < rc_pend[f].n; i++) if (i != rc_pend[f].k) rc_pend[f].a[i] = evacuate(rc_pend[f].a[i], true);
    for (size_t i = 0; i < rc_root_nslots; i++) rc_root_slots[i] = evacuate(rc_root_slots[i], true);
    if (rc_extra_roots_hook) rc_extra_roots_hook(1, evac_major_fn);
    if (to_first) scan_from(to_first, to_first->start, true);
    chunks_free(from);
    old_first = to_first; old_cur = to_cur; old_hp = to_hp;
    cur_old = to_space_id;
    old_bytes_in_use = copied_bytes;
    old_live_after_major = copied_bytes;
    rc_stats.major_copied_bytes += copied_bytes;
    rc_stats.major_gcs++;
    major_threshold = 2 * old_live_after_major > ((uint64_t)64 << 20) ? 2 * old_live_after_major : ((uint64_t)64 << 20);
}

/* a safepoint collects the young space once it holds this much: small enough that the young space
 * stays in the processor's second-level cache (measured: 0.5 to 2 MiB about 4% fewer cycles than
 * 4 MiB on the HOL Light module and its reader, 16 MiB 15% more) */
#ifndef RC_YOUNG_GC_KB
#define RC_YOUNG_GC_KB 1024
#endif
static uint64_t young_gc_threshold = (uint64_t)RC_YOUNG_GC_KB << 10;

void (*rc_foreign_hook)(int nroots);

RC_HEAP_LOCAL bool (*rc_stop_hook)(void);
RC_HEAP_LOCAL size_t rc_stop_mark;
bool rc_safepoint(int nroots) {
    if (rc_trace_on) rc_trace_push(RC_EV_RECORD);
    uint64_t used = rc_young_in_use();
    if (used >= young_gc_threshold) {
        uint64_t t0 = now_ns();
        rc_stats.young_bytes += used;
        if (used > rc_stats.young_peak) rc_stats.young_peak = used;
        minor(nroots);
        if (old_bytes_in_use > rc_stats.old_live_peak) rc_stats.old_live_peak = old_bytes_in_use;
        if (old_bytes_in_use > major_threshold) major(nroots);
        rc_stats.gc_ns += now_ns() - t0;
    }
    if (rc_foreign_hook) rc_foreign_hook(nroots);
    return rc_stop_hook && rc_stop_hook();
}

void rc_major_now(int nroots) {
    uint64_t t0 = now_ns();
    uint64_t used = rc_young_in_use();
    rc_stats.young_bytes += used;
    if (used > rc_stats.young_peak) rc_stats.young_peak = used;
    minor(nroots);
    if (old_bytes_in_use > rc_stats.old_live_peak) rc_stats.old_live_peak = old_bytes_in_use;
    major(nroots);
    rc_stats.gc_ns += now_ns() - t0;
}

/* ------------------------------------------------------------- pending wrappers */
RC_HEAP_LOCAL RcPend *rc_pend;
RC_HEAP_LOCAL size_t rc_pend_depth, rc_pend_cap;
void rc_pend_grow(void) {
    size_t n = rc_pend_cap ? 2 * rc_pend_cap : 1024;
    RcPend *p = realloc(rc_pend, n * sizeof *p);
    if (!p) { fputs("rc: out of memory (pending wrappers)\n", stderr); exit(3); }
    rc_pend = p;
    rc_pend_cap = n;
}
/* the frames above mark applied to v, the newest first: each a call of its wrapper (rc_call, which
 * applies whatever that call leaves pending); after a failure the rest are dropped */
T *rc_pend_apply(size_t mark, T *v) {
    while (rc_pend_depth > mark) {
        RcPend f = rc_pend[--rc_pend_depth];
        if (!v) continue;
        f.a[f.k] = v;
        v = rc_call(f.w, f.a);
    }
    return v;
}

/* constants made by rc_make_static: their children are roots of a native store too, and so is a
 * constant that is itself a value of a native store (a foreign value: an interned node) */
static T **statics; static size_t nstatics, capstatics;
static T **static_vals; static size_t nstatic_vals, capstatic_vals;
void rc_free_statics(void) {
    for (size_t s = 0; s < nstatics; s++) free(statics[s]);
    nstatics = 0;
    nstatic_vals = 0;
}

void rc_walk_live(int nroots, void (*fn)(T *child, void *ctx), void *ctx) {
    for (int i = 0; i < nroots; i++) fn(rc_roots[i], ctx);
    for (Chunk *c = old_first; c; c = c->next) {
        char *p = c->start, *lim = c == old_cur ? old_hp : c->top;
        while (p < lim) {
            T *y = (T *)p;
            if (!(y->flags & RC_FLAG_BYTES))
                for (uint32_t i = 0; i < y->n; i++) fn(y->k[i], ctx);
            p += obj_size(y);
        }
    }
    for (size_t s = 0; s < nstatics; s++) {
        T *y = statics[s];
        if (!(y->flags & RC_FLAG_BYTES))
            for (uint32_t i = 0; i < y->n; i++) fn(y->k[i], ctx);
    }
    for (size_t s = 0; s < nstatic_vals; s++) fn(static_vals[s], ctx);
    for (size_t f = 0; f < rc_pend_depth; f++)
        for (uint8_t i = 0; i < rc_pend[f].n; i++) if (i != rc_pend[f].k) fn(rc_pend[f].a[i], ctx);
    for (size_t i = 0; i < rc_root_nslots; i++) fn(rc_root_slots[i], ctx);
}

void rc_runtime_reset(void) {
    rc_pend_depth = 0;   /* a stopped run may leave frames */
    uint64_t used = rc_young_in_use();
    rc_stats.young_bytes += used;
    if (used > rc_stats.young_peak) rc_stats.young_peak = used;
    if (young_first) young_rewind();
    chunks_free(old_first);
    old_first = old_cur = NULL;
    old_hp = NULL;
    old_bytes_in_use = 0;
    old_live_after_major = 0;
    major_threshold = (uint64_t)64 << 20;
}

/* ----------------------------------------------------------------------- trace */
int rc_trace_on;
uint16_t *rc_trace_buf;
size_t rc_trace_len, rc_trace_cap;
static RcTraceSink trace_sink;
static void *trace_ctx;

void rc_trace_set_sink(RcTraceSink sink, void *ctx) { trace_sink = sink; trace_ctx = ctx; }
int rc_trace_hold;
bool (*rc_relation_hook)(int rel, int n, T *const *args);
void rc_trace_flush(void) {
    if (rc_trace_hold) {
        /* a trial is open: its events may still be dropped, so the buffer grows instead */
        size_t cap = 2 * rc_trace_cap;
        uint16_t *b = realloc(rc_trace_buf, cap * sizeof(uint16_t));
        if (!b) { fputs("rc: out of memory (trace)\n", stderr); exit(3); }
        rc_trace_buf = b; rc_trace_cap = cap;
        return;
    }
    if (trace_sink) trace_sink(rc_trace_buf, rc_trace_len, trace_ctx);
    rc_stats.trace_events += rc_trace_len;
    rc_stats.trace_flushes++;
    rc_trace_len = 0;
}
void rc_trace_start(size_t buffer_events) {
    if (rc_trace_cap != buffer_events) {
        free(rc_trace_buf);
        rc_trace_buf = malloc(buffer_events * sizeof(uint16_t));
        if (!rc_trace_buf) { fputs("rc: out of memory (trace)\n", stderr); exit(3); }
        rc_trace_cap = buffer_events;
    }
    rc_trace_len = 0;
    rc_trace_hold = 0;
    rc_trace_on = 1;
}
void rc_trace_stop(void) {
    if (rc_trace_on && rc_trace_len) rc_trace_flush();
    rc_trace_on = 0;
}

/* ------------------------------------------------------------------ promises */
RC_HEAP_LOCAL T *(*rc_promise_hook)(uint64_t id);
T *rc_promise_force(T *x) {
    if (!rc_promise_hook) rc_refinement_fail("a promise read with no host to keep it");
    T *v = rc_promise_hook(RC_PROMISE_ID(x));
    if (!v || RC_IS_PROMISE(v)) rc_refinement_fail("a promise the host does not keep");
    return v;
}

/* ------------------------------------------------------------- term equality */
bool rc_term_equal(const T *a, const T *b) {
    size_t cap = 256, sp = 0;
    const T **st = malloc(2 * cap * sizeof(T *));
    if (!st) { fputs("rc: out of memory\n", stderr); exit(3); }
    st[sp++] = a; st[sp++] = b;
    bool eq = true;
    while (sp && eq) {
        const T *y = st[--sp], *x = st[--sp];
        if (x == y) continue;
        /* a promise is the term it promises */
        if (RC_IS_PROMISE(x) || RC_IS_PROMISE(y)) {
            x = rc_promised((T *)x); y = rc_promised((T *)y);
            if (x == y) continue;
        }
        /* distinct immediates, or distinct foreign values (their stores intern: different objects are
         * different values) */
        if (!RC_IS_HEAP(x) || !RC_IS_HEAP(y)) { eq = false; break; }
        if ((x->flags & RC_FLAG_BYTES) != (y->flags & RC_FLAG_BYTES) || x->n != y->n) { eq = false; break; }
        if (x->flags & RC_FLAG_BYTES) { if (x->tag != y->tag || memcmp(x->k, y->k, x->n)) eq = false; continue; }
        if (x->tag != y->tag) { eq = false; break; }
        for (uint32_t i = 0; i < x->n; i++) {
            if (sp + 2 > 2 * cap) { cap *= 2; st = realloc(st, 2 * cap * sizeof(T *)); if (!st) { fputs("rc: out of memory\n", stderr); exit(3); } }
            st[sp++] = x->k[i]; st[sp++] = y->k[i];
        }
    }
    free(st);
    return eq;
}

/* ------------------------------------------------------------------ constants */
T *rc_make_static(T *x) {
    if (RC_IS_FOREIGN(x)) {
        if (nstatic_vals == capstatic_vals) {
            capstatic_vals = capstatic_vals ? 2 * capstatic_vals : 64;
            static_vals = realloc(static_vals, capstatic_vals * sizeof(T *));
            if (!static_vals) { fputs("rc: out of memory (constant)\n", stderr); exit(3); }
        }
        static_vals[nstatic_vals++] = x;
        return x;
    }
    if (!RC_IS_HEAP(x) || x->space == RC_SPACE_STATIC) return x;
    size_t sz = obj_size(x);
    T *y = malloc(sz);
    if (!y) { fputs("rc: out of memory (constant)\n", stderr); exit(3); }
    memcpy(y, x, sz);
    y->space = RC_SPACE_STATIC;
    y->flags &= (uint8_t)~RC_FLAG_FWD;
    if (!(y->flags & RC_FLAG_BYTES))
        for (uint32_t i = 0; i < y->n; i++) y->k[i] = rc_make_static(y->k[i]);
    if (nstatics == capstatics) {
        capstatics = capstatics ? 2 * capstatics : 64;
        statics = realloc(statics, capstatics * sizeof(T *));
        if (!statics) { fputs("rc: out of memory (constant)\n", stderr); exit(3); }
    }
    statics[nstatics++] = y;
    return y;
}

/* bytes allocated in the young space since the statistics were cleared, including what no
 * safepoint has collected yet */
uint64_t rc_alloc_total(void) { return rc_stats.young_bytes + rc_young_in_use(); }
