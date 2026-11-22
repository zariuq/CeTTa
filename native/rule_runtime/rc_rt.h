/* rc_rt.h - runtime of procedures compiled from rule modules by rulec.
 *
 * Terms are the module's constructor data.  A heap term is a header (constructor id, space, flags,
 * number of children) followed by its children; a nullary constructor is one static header per
 * constructor.  A value whose low bit is set is an immediate: the generated code reads it only
 * through the view of a sort that the compiler was told is represented natively (rc_rep.h: the record
 * cursor, native naturals).  Byte strings on the heap are objects whose payload is bytes.
 *
 * Memory: allocation is a bump pointer into the young space.  At a safepoint (the entry of the
 * fold over the records, where the only live terms are the procedure's arguments) the young terms
 * reachable from those arguments are copied to the old space and the young space is reset; the old
 * space is copied when it has doubled since its last copy.  Terms are immutable and a term is
 * always younger than its children, so no old term points to a young one.
 *
 * Trace: when on, one 16-bit event per rule application (the rule's id, in pre-order of the
 * derivation), per native operation, per failure and per record; no trace is ever copied.
 */
#ifndef RC_RT_H
#define RC_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rc_rep.h"
/* The heap's state is one per process, or one per thread where the instance defines RC_THREAD_HEAP
 * (several threads each running a compiled module's procedures, as a reader does on worker
 * threads); a host that keeps several runs alive saves and loads each one's heap (rc_heap_swap). */
#ifdef RC_THREAD_HEAP
#define RC_HEAP_LOCAL _Thread_local
#else
#define RC_HEAP_LOCAL
#endif

typedef struct T { uint16_t tag; uint8_t space; uint8_t flags; uint32_t n; struct T *k[]; } T;
typedef struct { uint16_t tag; uint8_t space; uint8_t flags; uint32_t n; } TH;

enum { RC_SPACE_STATIC = 0, RC_SPACE_YOUNG = 1, RC_SPACE_OLD0 = 2, RC_SPACE_OLD1 = 3 };
#define RC_FLAG_FWD 1u
#define RC_FLAG_BYTES 2u

#define RC_IS_IMM(x) ((((uintptr_t)(x)) & 1u) != 0)
#define RC_IMM_VAL(x) ((uint64_t)(((uintptr_t)(x)) >> 1))
#define RC_IMM(v) ((T *)((((uintptr_t)(v)) << 1) | 1u))
/* A foreign value: a pointer to an object of a native store that a sort is represented by (for
 * example interned terms of a native kernel), tagged in its low bits (2, 4 or 6; the object is
 * 8-byte aligned).  The collector never moves or scans it; the native store keeps it alive (see
 * rc_foreign_hook).  Two foreign values are equal exactly when they are the same pointer, which a
 * representation may use only where its store interns (equal structure gives the same object). */
#define RC_IS_FOREIGN(x) ((((uintptr_t)(x)) & 7u) != 0 && (((uintptr_t)(x)) & 1u) == 0)
#define RC_IS_HEAP(x) ((x) && (((uintptr_t)(x)) & 7u) == 0)
#define RC_FOREIGN_TAG(x) ((unsigned)(((uintptr_t)(x)) & 7u))
#define RC_FOREIGN_PTR(x) ((void *)(((uintptr_t)(x)) & ~(uintptr_t)7u))
#define RC_FOREIGN(p, tag) ((T *)(((uintptr_t)(p)) | (uintptr_t)(tag)))

/* ---------------------------------------------------------------- allocation */
extern RC_HEAP_LOCAL char *rc_hp, *rc_hlim;
T *rc_alloc_slow(size_t bytes);
#ifdef RC_PROFILE
extern uint64_t rc_prof_objects;
#define RC_PROF_OBJ() (rc_prof_objects++)
#else
#define RC_PROF_OBJ() ((void)0)
#endif

static inline T *rc_alloc_bytes(size_t b) {
    char *p = rc_hp;
    if ((size_t)(rc_hlim - p) < b) return rc_alloc_slow(b);
    rc_hp = p + b;
    return (T *)p;
}
static inline T *rc_mk(uint16_t c, uint32_t n) {
    T *t = rc_alloc_bytes(sizeof(TH) + (size_t)n * sizeof(T *));
    t->tag = c; t->space = RC_SPACE_YOUNG; t->flags = 0; t->n = n;
    RC_PROF_OBJ();
    return t;
}
static inline T *rc_mk1(uint16_t c, T *a) { T *t = rc_mk(c, 1); t->k[0] = a; return t; }
static inline T *rc_mk2(uint16_t c, T *a, T *b) { T *t = rc_mk(c, 2); t->k[0] = a; t->k[1] = b; return t; }
static inline T *rc_mk3(uint16_t c, T *a, T *b, T *d) { T *t = rc_mk(c, 3); t->k[0] = a; t->k[1] = b; t->k[2] = d; return t; }
static inline T *rc_mk4(uint16_t c, T *a, T *b, T *d, T *e) { T *t = rc_mk(c, 4); t->k[0] = a; t->k[1] = b; t->k[2] = d; t->k[3] = e; return t; }
static inline T *rc_mk5(uint16_t c, T *a, T *b, T *d, T *e, T *f) { T *t = rc_mk(c, 5); t->k[0] = a; t->k[1] = b; t->k[2] = d; t->k[3] = e; t->k[4] = f; return t; }
static inline T *rc_mk6(uint16_t c, T *a, T *b, T *d, T *e, T *f, T *g) { T *t = rc_mk(c, 6); t->k[0] = a; t->k[1] = b; t->k[2] = d; t->k[3] = e; t->k[4] = f; t->k[5] = g; return t; }
static inline T *rc_mk7(uint16_t c, T *a, T *b, T *d, T *e, T *f, T *g, T *h) { T *t = rc_mk(c, 7); t->k[0] = a; t->k[1] = b; t->k[2] = d; t->k[3] = e; t->k[4] = f; t->k[5] = g; t->k[6] = h; return t; }
static inline T *rc_mk8(uint16_t c, T *a, T *b, T *d, T *e, T *f, T *g, T *h, T *i) { T *t = rc_mk(c, 8); t->k[0] = a; t->k[1] = b; t->k[2] = d; t->k[3] = e; t->k[4] = f; t->k[5] = g; t->k[6] = h; t->k[7] = i; return t; }
/* any arity, the children in an array */
static inline T *rc_mkv(uint16_t c, uint32_t n, T *const *k) { T *t = rc_mk(c, n); memcpy(t->k, k, (size_t)n * sizeof(T *)); return t; }

/* symbolic representation (the default for every sort) */
extern TH rc_nul[];
#define RC_TAG_symbolic(x) ((x)->tag)
#define RC_KID_symbolic(x, i) ((x)->k[(i)])
#define RC_NUL_symbolic(c) ((T *)&rc_nul[(c)])
#define RC_MK1_symbolic(c, a) rc_mk1((c), (a))
#define RC_MK2_symbolic(c, a, b) rc_mk2((c), (a), (b))
#define RC_MK3_symbolic(c, a, b, d) rc_mk3((c), (a), (b), (d))
#define RC_MK4_symbolic(c, a, b, d, e) rc_mk4((c), (a), (b), (d), (e))
#define RC_MK5_symbolic(c, a, b, d, e, f) rc_mk5((c), (a), (b), (d), (e), (f))
#define RC_MK6_symbolic(c, a, b, d, e, f, g) rc_mk6((c), (a), (b), (d), (e), (f), (g))
#define RC_MK7_symbolic(c, a, b, d, e, f, g, h) rc_mk7((c), (a), (b), (d), (e), (f), (g), (h))
#define RC_MK8_symbolic(c, a, b, d, e, f, g, h, i) rc_mk8((c), (a), (b), (d), (e), (f), (g), (h), (i))
#define RC_MKV_symbolic(c, n, k) rc_mkv((c), (n), (k))

/* byte strings on the heap (the atoms representation's strings, symbols and wide integers, told
 * apart by their tags): flags RC_FLAG_BYTES, n = length, payload = bytes */
T *rc_bytes_new(const uint8_t *b, size_t n);
static inline const uint8_t *rc_bytes(const T *x) { return (const uint8_t *)x->k; }
extern TH rc_empty_bytes;

/* Promised values, for a sort declared (rep SORT promise): a value is a symbolic term, or a promise, a
 * foreign value (tag 6) holding a number the host gave it.  Reading a promise (its constructor or a
 * child) forces it: rc_promise_hook(number) gives the term, never a promise, and the same module data
 * each time it is asked (the host may build it anew).  A promise holds no term of the spaces, so the
 * collector leaves it as it is; only a host that makes promises sets the hook. */
#define RC_PROMISE_TAG 6u
#define RC_IS_PROMISE(x) ((((uintptr_t)(x)) & 7u) == RC_PROMISE_TAG)
#define RC_PROMISE(id) ((T *)((((uintptr_t)(id)) << 3) | RC_PROMISE_TAG))
#define RC_PROMISE_ID(x) ((uint64_t)(((uintptr_t)(x)) >> 3))
extern RC_HEAP_LOCAL T *(*rc_promise_hook)(uint64_t id);
T *rc_promise_force(T *x);
static inline T *rc_promised(T *x) { return RC_IS_PROMISE(x) ? rc_promise_force(x) : x; }
#define RC_TAG_promise(x) (rc_promised(x)->tag)
#define RC_KID_promise(x, i) (rc_promised(x)->k[(i)])
#define RC_NUL_promise(c) RC_NUL_symbolic(c)
#define RC_MK1_promise RC_MK1_symbolic
#define RC_MK2_promise RC_MK2_symbolic
#define RC_MK3_promise RC_MK3_symbolic
#define RC_MK4_promise RC_MK4_symbolic
#define RC_MK5_promise RC_MK5_symbolic
#define RC_MK6_promise RC_MK6_symbolic
#define RC_MK7_promise RC_MK7_symbolic
#define RC_MK8_promise RC_MK8_symbolic
#define RC_MKV_promise RC_MKV_symbolic

/* ------------------------------------------------------------------ failures */
#ifdef RC_COUNT
extern RC_HEAP_LOCAL uint64_t *rc_fail_count;
#define RC_COUNT_FAIL(j) (rc_fail_count[(j)]++)
#else
#define RC_COUNT_FAIL(j) ((void)0)
#endif

/* ---------------------------------------------------------------------- trace */
enum { RC_EV_CONST = 0xE000u, RC_EV_MEMO = 0xE800u, RC_EV_NATIVE = 0xF000u, RC_EV_LAW = 0xF800u, RC_EV_NO_ROW = 0xFFFCu, RC_EV_NO_RULE = 0xFFFDu, RC_EV_TARGET = 0xFFFEu, RC_EV_RECORD = 0xFFFFu };
extern int rc_trace_on;
extern uint16_t *rc_trace_buf;
extern size_t rc_trace_len, rc_trace_cap;
void rc_trace_flush(void);
static inline void rc_trace_push(uint16_t e) {
    if (rc_trace_len == rc_trace_cap) rc_trace_flush();
    rc_trace_buf[rc_trace_len++] = e;
}
#ifdef RC_COUNT
extern RC_HEAP_LOCAL uint64_t *rc_rule_count;
#define RC_COUNT_RULE(r) (rc_rule_count[(r)]++)
#else
#define RC_COUNT_RULE(r) ((void)0)
#endif
#define RC_RULE(r) do { RC_COUNT_RULE(r); if (rc_trace_on) rc_trace_push((uint16_t)(r)); } while (0)
static inline T *rc_no_rule(int j) { (void)j; RC_COUNT_FAIL(j); if (rc_trace_on) rc_trace_push(RC_EV_NO_RULE); return (T *)0; }
static inline T *rc_fail_target(int j) { (void)j; RC_COUNT_FAIL(j); if (rc_trace_on) rc_trace_push(RC_EV_TARGET); return (T *)0; }
static inline T *rc_no_row(int j) { (void)j; RC_COUNT_FAIL(j); if (rc_trace_on) rc_trace_push(RC_EV_NO_ROW); return (T *)0; }
/* a trial leaf (several pairwise exclusive candidate rules): the rule event is reserved at the start
 * and filled with the rule that applied; while a trial is open the buffer is not flushed (it grows),
 * so the events of a candidate that failed can be dropped */
extern int rc_trace_hold;
static inline size_t rc_trial_begin(void) { rc_trace_hold++; size_t m = rc_trace_len; rc_trace_push(0); return m; }
static inline void rc_trial_end(size_t m, int rule) { rc_trace_buf[m] = (uint16_t)rule; rc_trace_hold--; }
/* Pending wrappers (tail recursion modulo a wrapper).  A rule whose last two steps are t := J(...)
 * and then the tail call W(..., t, ...) with t nowhere else, J in the rule's own recursion, pushes
 * the frame (W, the position of t, W's other arguments) and tail-calls J, so the recursion needs no C
 * stack.  Whoever called the chain (every call that is not a tail call, and the host's entry rc_call)
 * applies the frames pushed above its mark to the result, the newest first; a failure (no result)
 * drops them.  The frames' values are roots of the collector and of the native stores. */
/* the value a stopped run returns to the host's entry (see rc_safepoint) */
#define RC_STOPPED_BITS 24u
#define RC_STOPPED ((T *)(uintptr_t)RC_STOPPED_BITS)
#define RC_PEND_ARGS 8
typedef struct RcPend { uint16_t w; uint8_t k, n; T *a[RC_PEND_ARGS]; } RcPend;
extern RC_HEAP_LOCAL RcPend *rc_pend;
extern RC_HEAP_LOCAL size_t rc_pend_depth, rc_pend_cap;
void rc_pend_grow(void);
T *rc_pend_apply(size_t mark, T *v);
static inline void rc_pend_push(uint16_t w, uint8_t k, uint8_t n, T *const *a) {
    if (rc_pend_depth == rc_pend_cap) rc_pend_grow();
    RcPend *f = &rc_pend[rc_pend_depth++];
    f->w = w; f->k = k; f->n = n;
    for (uint8_t i = 0; i < n; i++) f->a[i] = a[i];
}
static inline __attribute__((always_inline)) T *rc_pend_unwind(size_t mark, T *v) {
    if (v == RC_STOPPED) return v;   /* a stopped run keeps its frames for rc_resume */
    return rc_pend_depth > mark ? rc_pend_apply(mark, v) : v;
}
/* relations the rules query: their rows are host data; a query with every argument bound holds when
 * the host has the row (no host: no rows) */
extern const int rc_nrelations;
extern const char *const rc_relation_names[];
extern bool (*rc_relation_hook)(int rel, int n, T *const *args);
static inline bool rc_relation_holds(int rel, int n, T *const *args) { return rc_relation_hook && rc_relation_hook(rel, n, args); }

/* A success continuation.  A procedure that may give several results (nondet) passes each result to
 * its continuation k, as k->fn(k, value), and returns 0 when it has given them all; the continuation's
 * own state follows the RcK in the object k points to.  rc_solve (generated) gives every result of any
 * judgment this way. */
typedef struct RcK RcK;
struct RcK { void (*fn)(const RcK *k, T *answer); };

/* Tail calls are guaranteed where GCC's musttail is available (GCC 15; clang's needs identical
 * prototypes, which these calls do not have).  Without it a tail call is an ordinary call in tail
 * position: the result is the same, only the stack grows with the records. */
#if !defined(RC_NO_MUSTTAIL) && defined(__has_attribute) && !defined(__clang__)
#if __has_attribute(musttail)
#define RC_TAIL __attribute__((musttail))
#endif
#endif
#ifndef RC_TAIL
#define RC_TAIL
#endif

/* ------------------------------------------------------------------ safepoint */
extern RC_HEAP_LOCAL T *rc_roots[8];
/* the safepoint: a minor collection when the young space has grown, the native store's hook, and the
 * host's stop: when rc_stop_hook says so, the run stops here and returns RC_STOPPED to the host's
 * entry, its state kept (the safepoint's arguments in rc_roots, the pending frames); rc_resume (the
 * generated code) continues it from the safepoint */
bool rc_safepoint(int nroots);
extern RC_HEAP_LOCAL bool (*rc_stop_hook)(void);
extern RC_HEAP_LOCAL size_t rc_stop_mark;   /* the pending depth at the stopped run's entry */
/* Called at the end of every safepoint, after the young space was collected (so the young space is
 * empty and rc_roots hold the live state): a native store can collect here.  rc_major_now copies
 * the old space at once (it then holds exactly the live terms); rc_walk_live calls fn on every
 * child slot of every term in the old space, of every constant made by rc_make_static, on every
 * constant that is itself a foreign value, and on the roots. */
extern void (*rc_foreign_hook)(int nroots);
/* Slots outside the spaces that hold terms (a native representation's cells): called by every
 * copy after the roots, with major = 0 for a young copy (evacuate the slots written since the last
 * one) and 1 for an old copy (evacuate all); evac returns the slot's new value. */
extern void (*rc_extra_roots_hook)(int major, T *(*evac)(T *x));
/* a host's slots that hold values of the module (NULL or a value): roots of every collection, and
 * updated by it, while rc_root_nslots says so */
extern RC_HEAP_LOCAL T **rc_root_slots;
extern RC_HEAP_LOCAL size_t rc_root_nslots;
void rc_major_now(int nroots);
void rc_walk_live(int nroots, void (*fn)(T *child, void *ctx), void *ctx);

/* a zeroed table for a component's lookups (freed with free); a large one is backed by large pages
 * where the system has them (random lookups: fewer translation misses).  NULL when out of memory. */
void *rc_alloc_table(size_t bytes);

/* refinement violations (a native representation cannot hold a value the rules built) */
_Noreturn void rc_refinement_fail(const char *what);
/* Where a refinement violation goes: unset, the process stops (exit 5) after printing it; a host that
 * runs the module inside a longer-lived process sets a function that leaves the run (for example by
 * longjmp) and reports the violation as the run's result; if it returns, the process stops. */
extern void (*rc_fail_hook)(const char *what);

/* ------------------------------------------------------------ runtime control */
typedef struct {
    uint64_t young_bytes;        /* allocated in the young space (all of it, summed at safepoints) */
    uint64_t promoted_bytes;     /* copied young -> old */
    uint64_t major_copied_bytes; /* copied old -> old */
    uint64_t minor_gcs, major_gcs;
    uint64_t old_live_peak;      /* old space in use, at most */
    uint64_t young_peak;         /* young space in use at a safepoint, at most */
    uint64_t gc_ns;
    uint64_t trace_events, trace_flushes;
} RcStats;
extern RC_HEAP_LOCAL RcStats rc_stats;
void rc_runtime_init(size_t young_chunk);
/* A heap of a run kept alive between calls: its young and old spaces, and what points into them
 * from outside: the safepoint's roots, the pending frames and the stop mark over them, the host's
 * root slots.  rc_heap_swap exchanges the runtime's heap (of this thread, with RC_THREAD_HEAP) with
 * *h: a host keeping several runs loads a run's heap before working in it and saves it after; a
 * collection in one run so never meets another's values, and a stopped run resumes where it was.  The
 * hooks are the host's, not the run's: it sets them before it works in a run.  A zeroed RcHeap is an
 * empty heap; rc_heap_free frees a saved one. */
typedef struct RcHeap {
    void *young_first, *young_cur, *old_first, *old_cur;
    char *hp, *hlim, *old_hp;
    uint8_t cur_old;
    uint64_t old_bytes_in_use, old_live_after_major, major_threshold;
    T *roots[8];
    RcPend *pend;
    size_t pend_depth, pend_cap, stop_mark;
    T **root_slots;
    size_t root_nslots;
} RcHeap;
void rc_heap_swap(RcHeap *h);
void rc_heap_free(RcHeap *h);
void rc_runtime_reset(void);       /* frees every term; statics stay */
/* the young space's chunks are kept from run to run up to BYTES in all (default 256 MiB): a run that
 * fills as much as an earlier one writes memory already mapped */
void rc_runtime_retain(size_t bytes);
uint64_t rc_young_in_use(void);
uint64_t rc_old_in_use(void);
uint64_t rc_alloc_total(void);

/* the sink of a full trace buffer; by default the events are counted and digested */
typedef void (*RcTraceSink)(const uint16_t *ev, size_t n, void *ctx);
void rc_trace_set_sink(RcTraceSink sink, void *ctx);
void rc_trace_start(size_t buffer_events);
void rc_trace_stop(void);

/* term utilities */
bool rc_term_equal(const T *a, const T *b);   /* structure; byte strings by their tag and bytes */
/* a deep copy outside the collected spaces (constants); a foreign value (an interned node) is kept as
 * it is and becomes a root of its store; rc_free_statics frees every such copy and forgets those roots
 * (when the constants are recomputed, for example because the native store they point into was
 * replaced) */
T *rc_make_static(T *x);
void rc_free_statics(void);
/* defined by the generated code: forget the cached constants (they are recomputed at their next
 * use), for example when the store their values point into is freed */
void rc_reset_constants(void);
/* defined by the generated code: equality of two values of a sort as module data, through the views */
bool rc_value_equal(int sort, T *a, T *b);
/* defined by the generated code: the host's entry, a judgment called on an argument vector (the
 * frames its run leaves pending are applied before it returns); RC_STOPPED when the run stopped at the
 * safepoint (rc_stop_hook), its frames kept above rc_stop_mark */
T *rc_call(int judgment, T **a);
/* defined by the generated code: as rc_call, but the frames the run leaves pending stay above
 * rc_stop_mark whether it stops or ends (for a host that takes them, as after rc_resume) */
T *rc_start(int judgment, T **a);
/* defined by the generated code: a run stopped at the safepoint continued from its saved state; it may
 * stop again (RC_STOPPED).  When it ends, its value is returned with the frames above rc_stop_mark
 * still pending: the host takes what it wants from them (rc_pend[i]) and applies the rest
 * (rc_pend_apply(rc_stop_mark, v)) */
T *rc_resume(void);
/* one item of a value read in bulk by a host (rc_value_items, rc_list_items of the generated code):
 * ctor >= 0 a constructor with n children, whose items follow; -1 the number v; -2 the n bytes at p */
typedef struct RcItem { const uint8_t *p; uint64_t v; uint32_t n; int32_t ctor; } RcItem;
#endif
