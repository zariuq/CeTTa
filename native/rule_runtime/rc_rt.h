/* rc_rt.h - runtime of procedures compiled from rule modules by rulec.
 *
 * Terms are the module's constructor data.  A heap term is a header (constructor id, space, flags,
 * number of children) followed by its children; a nullary constructor is one static header per
 * constructor.  A value whose low bit is set is an immediate: the generated code reads it only
 * through the view of a sort that the compiler was told is represented natively (the record cursor,
 * native naturals).  Native byte strings are heap objects whose payload is bytes.
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
extern char *rc_hp, *rc_hlim;
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

/* native byte strings: flags RC_FLAG_BYTES, n = length, payload = bytes */
T *rc_bytes_new(const uint8_t *b, size_t n);
static inline const uint8_t *rc_bytes(const T *x) { return (const uint8_t *)x->k; }
extern TH rc_empty_bytes;

/* ------------------------------------------------------------------ failures */
#ifdef RC_COUNT
extern uint64_t *rc_fail_count;
#define RC_COUNT_FAIL(j) (rc_fail_count[(j)]++)
#else
#define RC_COUNT_FAIL(j) ((void)0)
#endif

/* ---------------------------------------------------------------------- trace */
enum { RC_EV_CONST = 0xE000u, RC_EV_NATIVE = 0xF000u, RC_EV_LAW = 0xF800u, RC_EV_NO_RULE = 0xFFFDu, RC_EV_TARGET = 0xFFFEu, RC_EV_RECORD = 0xFFFFu };
extern int rc_trace_on;
extern uint16_t *rc_trace_buf;
extern size_t rc_trace_len, rc_trace_cap;
void rc_trace_flush(void);
static inline void rc_trace_push(uint16_t e) {
    if (rc_trace_len == rc_trace_cap) rc_trace_flush();
    rc_trace_buf[rc_trace_len++] = e;
}
#ifdef RC_COUNT
extern uint64_t *rc_rule_count;
#define RC_COUNT_RULE(r) (rc_rule_count[(r)]++)
#else
#define RC_COUNT_RULE(r) ((void)0)
#endif
#define RC_RULE(r) do { RC_COUNT_RULE(r); if (rc_trace_on) rc_trace_push((uint16_t)(r)); } while (0)
static inline T *rc_no_rule(int j) { (void)j; RC_COUNT_FAIL(j); if (rc_trace_on) rc_trace_push(RC_EV_NO_RULE); return (T *)0; }
static inline T *rc_fail_target(int j) { (void)j; RC_COUNT_FAIL(j); if (rc_trace_on) rc_trace_push(RC_EV_TARGET); return (T *)0; }

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
extern T *rc_roots[8];
void rc_safepoint(int nroots);
/* Called at the end of every safepoint, after the young space was collected (so the young space is
 * empty and rc_roots hold the live state): a native store can collect here.  rc_major_now copies
 * the old space at once (it then holds exactly the live terms); rc_walk_live calls fn on every
 * child slot of every term in the old space, of every constant made by rc_make_static, and on the
 * roots. */
extern void (*rc_foreign_hook)(int nroots);
/* Equality, as module data, of two different foreign values with the same tag (rc_term_equal); unset,
 * or false, where the native store interns them (different objects are different values). */
extern bool (*rc_foreign_equal_hook)(const T *a, const T *b);
/* Slots outside the spaces that hold terms (a native representation's cells): called by every
 * copy after the roots, with major = 0 for a young copy (evacuate the slots written since the last
 * one) and 1 for an old copy (evacuate all); evac returns the slot's new value. */
extern void (*rc_extra_roots_hook)(int major, T *(*evac)(T *x));
void rc_major_now(int nroots);
void rc_walk_live(int nroots, void (*fn)(T *child, void *ctx), void *ctx);

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
extern RcStats rc_stats;
void rc_runtime_init(size_t young_chunk);
void rc_runtime_reset(void);       /* frees every term; statics stay */
uint64_t rc_young_in_use(void);
uint64_t rc_old_in_use(void);
uint64_t rc_alloc_total(void);

/* the sink of a full trace buffer; by default the events are counted and digested */
typedef void (*RcTraceSink)(const uint16_t *ev, size_t n, void *ctx);
void rc_trace_set_sink(RcTraceSink sink, void *ctx);
void rc_trace_start(size_t buffer_events);
void rc_trace_stop(void);

/* term utilities */
bool rc_term_equal(const T *a, const T *b);
/* a deep copy outside the collected spaces (constants); rc_free_statics frees every such copy (when
 * the constants are recomputed, for example because the native store they point into was replaced) */
T *rc_make_static(T *x);
void rc_free_statics(void);
#endif
