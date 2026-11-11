#include <lean/lean.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* One Lean runtime for every in-process source compiler provider. Lean
 * registers options and environment extensions while modules initialize,
 * once per name and only before initialization ends, so providers share one
 * copy of their modules. This library holds that copy and initializes every
 * provider root, then ends initialization exactly once. The build supplies
 * the roots as X(initializer, force_constants) entries. */

#include "lean_provider_roots_v1.h"

extern void lean_initialize(void);
extern void lean_finalize_thread(void);
extern void __lsan_disable(void) __attribute__((weak));
extern void __lsan_enable(void) __attribute__((weak));

#define CETTA_DECLARE_ROOT(initializer, force) \
    extern lean_object *initializer(uint8_t); \
    extern lean_object *force(uint64_t);
CETTA_LEAN_PROVIDER_ROOTS(CETTA_DECLARE_ROOT)

static pthread_once_t once = PTHREAD_ONCE_INIT;
static bool ready;

static void initialize_all(void) {
    /* Module constants and the embedded runtime live for the process. Lean
     * creates some closed constants on first use, separately from module
     * initializers; they are forced here, before leak accounting resumes. */
    bool leak_scope = __lsan_disable && __lsan_enable;
    if (leak_scope) __lsan_disable();
    lean_initialize();
    bool ok = true;
#define CETTA_RUN_ROOT(initializer, force) \
    if (ok) { \
        lean_object *result = initializer(1); \
        ok = lean_io_result_is_ok(result); \
        lean_dec(result); \
    } \
    if (ok) { \
        lean_object *forced = force(UINT64_MAX); \
        lean_dec(forced); \
    }
    CETTA_LEAN_PROVIDER_ROOTS(CETTA_RUN_ROOT)
    lean_io_mark_end_initialization();
    /* Every provider call opens and closes its own thread scope. */
    lean_finalize_thread();
    if (leak_scope) __lsan_enable();
    ready = ok;
}

__attribute__((visibility("default")))
bool cetta_lean_providers_initialize_v1(void) {
    pthread_once(&once, initialize_all);
    return ready;
}
