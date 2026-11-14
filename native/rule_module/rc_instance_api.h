/* rc_instance_api.h - a compiled module's instance as seen from other native code in the same
 * binary: the instance is one translation unit whose global names are its own (prefixed), and it
 * exports one table of entry points under an unprefixed name.  A host (for example the bridge of
 * another compiled module) calls the module's judgments, and reads their results, only through this
 * table, so values of different instances never meet: each instance's values are read by its own
 * views.
 *
 * A run: run_begin starts one (the young space reset, the interning store begun); the values of the
 * run stay valid until run_end.  A value of a slices sort is valid while the input given to
 * bytes_input is unchanged and the run lasts.
 */
#ifndef RC_INSTANCE_API_H
#define RC_INSTANCE_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct T;
struct RcItem;   /* rc_rt.h: one item of a value read in bulk */

typedef struct RcInstanceApi {
    const char *module, *theory;
    void (*init)(void);                                  /* once, before the first run */
    int (*ctor_named)(const char *name);                 /* a constructor's id, or -1 */
    int (*sort_named)(const char *name);                 /* a sort's id, or -1 */
    int (*ctor_arity)(int ctor);
    void (*run_begin)(void);
    void (*run_end)(void);
    void (*free_store)(void);                            /* free the interning store and the constants */
    void (*retain)(size_t bytes);                        /* young memory kept from run to run (rc_runtime_retain) */
    struct T *(*call)(int judgment, struct T **args);    /* NULL: no result */
    int (*view)(int sort, struct T *x, struct T **kids); /* the value's constructor and children */
    bool (*value_u64)(int sort, struct T *x, uint64_t *v);
    bool (*value_bytes)(int sort, struct T *x, const uint8_t **p, size_t *n);
    struct T *(*bytes_input)(const uint8_t *b, size_t n); /* the run's input as a slices value */
    /* the items of whole consecutive elements of a list (rc_list_items), the cursor moved past them */
    size_t (*list_items)(int sort, struct T **cursor, struct RcItem *buf, size_t cap);
} RcInstanceApi;

#endif
