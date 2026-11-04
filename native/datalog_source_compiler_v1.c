#include "source_compiler_provider_v1.h"
#include "src/utf8.h"
#include <lean/lean.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_DATALOG_COMPILER_INITIALIZER
#error "The build must name the generated module initializer."
#endif

extern void lean_initialize(void);
extern lean_object *CETTA_DATALOG_COMPILER_INITIALIZER(uint8_t);
extern lean_object *cetta_datalog_read_v1(lean_object *, lean_object *, lean_object *);
extern lean_object *cetta_datalog_verify_v1(lean_object *,lean_object *,lean_object *,lean_object *,lean_object *);
extern lean_object *cetta_datalog_prepare_v1(lean_object *, lean_object *, lean_object *, lean_object *);
extern lean_object *cetta_datalog_observe_v1(lean_object *, lean_object *, lean_object *, lean_object *);
extern lean_object *cetta_datalog_package_v1(lean_object *);
extern lean_object *cetta_datalog_admit_v1(lean_object *,lean_object *,lean_object *,lean_object *,lean_object *);
extern uint8_t cetta_datalog_force_integer_bounds_v1(uint64_t);
extern lean_object *cetta_datalog_force_decimal_codec_v1(uint64_t);
extern void lean_initialize_thread(void);
extern void lean_finalize_thread(void);
extern void __lsan_disable(void) __attribute__((weak));
extern void __lsan_enable(void) __attribute__((weak));

static pthread_mutex_t compiler_lock = PTHREAD_MUTEX_INITIALIZER;
static bool initialized;
static bool ready;
static _Thread_local bool thread_ready;

static bool utf8(const char *text, size_t length) {
    if (!text && length) return false;
    for (size_t i = 0; i < length;) {
        uint32_t scalar, width;
        if (!cetta_utf8_decode_one((const uint8_t *)text, length, i, &scalar, &width)) return false;
        i += width;
    }
    return true;
}

static int call(const char *operation, const char *const *arguments,
                const size_t *lengths, size_t count,
                char **response, size_t *response_length) {
    if (!operation || !arguments || !lengths || !response || !response_length) return 0;
    bool read = !strcmp(operation, "read") && count == 3u;
    bool prepare = !strcmp(operation, "prepare") && count == 4u;
    bool observe = !strcmp(operation, "observe") && count == 4u;
    bool package = !strcmp(operation, "package") && count == 1u;
    bool admit = !strcmp(operation,"admit") && count==5u;
    bool verify = !strcmp(operation,"verify") && count==5u;
    if (!read && !prepare && !observe && !package && !admit && !verify) return 0;
    for (size_t i = 0; i < count; ++i) if (!utf8(arguments[i], lengths[i])) return 0;
    pthread_mutex_lock(&compiler_lock);
    if (!initialized) {
        initialized = true;
        /* Module constants and the embedded runtime are process-lifetime
         * allocations: their code is pinned by the loader. Request data is
         * allocated only after this scope, with leak accounting enabled. */
        bool leak_scope=__lsan_disable&&__lsan_enable;
        if(leak_scope)__lsan_disable();
        lean_initialize();
        thread_ready = true;
        lean_object *result = CETTA_DATALOG_COMPILER_INITIALIZER(1);
        ready = lean_io_result_is_ok(result);
        lean_dec(result);
        /* Lean creates closed integer-bound constants on their first use,
         * separately from the module initializer. They share the module's
         * permanent lifetime; force them before leaving that scope. */
        if(ready)ready=cetta_datalog_force_integer_bounds_v1(0u)!=0u;
        if(ready){
            lean_object *decimal=cetta_datalog_force_decimal_codec_v1(UINT64_MAX);
            lean_dec(decimal);
        }
        lean_io_mark_end_initialization();
        if(leak_scope)__lsan_enable();
    }
    if (!ready) {
        if(thread_ready){lean_finalize_thread();thread_ready=false;}
        pthread_mutex_unlock(&compiler_lock);return 0;
    }
    if (!thread_ready) { lean_initialize_thread(); thread_ready = true; }
    lean_object *args[5];
    for (size_t i = 0; i < count; ++i)
        args[i] = lean_mk_string_from_bytes(arguments[i] ? arguments[i] : "", lengths[i]);
    lean_object *result = admit ? cetta_datalog_admit_v1(args[0],args[1],args[2],args[3],args[4])
        : verify ? cetta_datalog_verify_v1(args[0],args[1],args[2],args[3],args[4])
        : package ? cetta_datalog_package_v1(args[0])
        : read ? cetta_datalog_read_v1(args[0], args[1], args[2])
        : prepare ? cetta_datalog_prepare_v1(args[0], args[1], args[2], args[3])
        : cetta_datalog_observe_v1(args[0], args[1], args[2], args[3]);
    size_t length = lean_string_size(result) - 1u;
    char *copy = malloc(length + 1u);
    if (copy) memcpy(copy, lean_string_cstr(result), length + 1u);
    lean_dec(result);
    /* No Lean value escapes a call: the reply is copied above and module
     * constants are persistent. Balance each foreign-thread runtime scope. */
    lean_finalize_thread();
    thread_ready = false;
    pthread_mutex_unlock(&compiler_lock);
    if (!copy) return 0;
    *response = copy;
    *response_length = length;
    return 1;
}

static const CettaSourceCompilerProviderV1 provider = {1u, "DatalogD0", call, free};

__attribute__((visibility("default")))
const CettaSourceCompilerProviderV1 *cetta_source_compiler_v1(void) { return &provider; }
