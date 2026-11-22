/* rc_instance_api.h - a compiled module's instance as seen from other native code in the same
 * binary: the instance is one translation unit whose global names are its own (prefixed), and it
 * exports one table of entry points under an unprefixed name.  A host (for example another compiled
 * module's instance that reads its input through this one, rc_feed.h) calls the module's judgments, and
 * reads their results, only through this table, so values of different instances never meet: each
 * instance's values are read by its own views.
 *
 * A run: run_begin starts one (the young space reset, the interning store begun); the values of the
 * run stay valid until run_end, and while the run is stopped or ended: a resumed run may collect, after
 * which only the values it holds (its safepoint's arguments, its pending frames) are valid.  A value of
 * a slices sort is valid while the input given to bytes_input is unchanged and the run lasts.
 *
 * A stopped run (rc_rt.h: rc_stop_hook, rc_resume): call or start returns RC_STOPPED_VALUE when the hook
 * set by set_stop_hook stopped the run at the module's safepoint; its state is the safepoint's arguments
 * (root) and the frames pending above stop_mark (read in place); resume continues it.  A run begun by
 * start, or continued by resume, ends with its value and those frames still pending, for the host to
 * take (pend_drop) or apply (pend_apply).
 */
#ifndef RC_INSTANCE_API_H
#define RC_INSTANCE_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct T;
struct RcItem;   /* rc_rt.h: one item of a value read in bulk */
struct RcPend;   /* rc_rt.h: a pending frame (its wrapper, the hole's position, the arguments) */

/* the value of a stopped run (rc_rt.h: RC_STOPPED) */
#define RC_STOPPED_VALUE_BITS 24u
#define RC_STOPPED_VALUE ((struct T *)(uintptr_t)RC_STOPPED_VALUE_BITS)

typedef struct RcInstanceApi {
    const char *module, *theory;
    void (*init)(void);                                  /* once, before the first run */
    int (*ctor_named)(const char *name);                 /* a constructor's id, or -1 */
    int (*sort_named)(const char *name);                 /* a sort's id, or -1 */
    int (*ctor_arity)(int ctor);
    int (*ctor_sort)(int ctor);                          /* a constructor's sort (a judgment's: its value's) */
    int (*ctor_arg_sort)(int ctor, int i);               /* the sort of a constructor's child i */
    void (*run_begin)(void);
    void (*run_end)(void);
    void (*free_store)(void);                            /* free the interning store and the constants */
    void (*retain)(size_t bytes);                        /* young memory kept from run to run (rc_runtime_retain) */
    struct T *(*call)(int judgment, struct T **args);    /* NULL: no result */
    /* as call, but the frames the run leaves stay pending above stop_mark (stopped or ended) */
    struct T *(*start)(int judgment, struct T **args);
    int (*view)(int sort, struct T *x, struct T **kids); /* the value's constructor and children */
    bool (*value_u64)(int sort, struct T *x, uint64_t *v);
    bool (*value_bytes)(int sort, struct T *x, const uint8_t **p, size_t *n);
    struct T *(*bytes_input)(const uint8_t *b, size_t n); /* the run's input as a slices value */
    /* the items of a value (rc_value_items), or of whole consecutive elements of a list
     * (rc_list_items, the cursor moved past them) */
    size_t (*value_items)(int sort, struct T *x, struct RcItem *buf, size_t cap);
    size_t (*list_items)(int sort, struct T **cursor, struct RcItem *buf, size_t cap);
    /* where a refinement violation of the module goes (rc_fail_hook) */
    void (*set_fail_hook)(void (*hook)(const char *what));
    /* stopping a run at the safepoint, and continuing it */
    void (*set_stop_hook)(bool (*hook)(void));
    struct T *(*resume)(void);
    struct T *(*root)(int i);                            /* the stopped run's safepoint argument i */
    size_t (*stop_mark)(void);                           /* the frames of the stopped run lie above it */
    /* the pending frames, read in place (rc_rt.h: RcPend, the same layout in every instance): the
     * array (it moves when it grows) and the number of frames; argument i of a frame is the
     * instance's value, except at the hole's position */
    struct RcPend *const *frames;
    const size_t *depth;
    void (*pend_drop)(size_t depth);                     /* forget the frames above depth */
    struct T *(*pend_apply)(size_t mark, struct T *v);   /* apply the frames above mark to v, the newest first */
} RcInstanceApi;

#endif
