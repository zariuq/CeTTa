#include "source_compiler_provider_v1.h"
#include "src/utf8.h"
#include <lean/lean.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* An in-process source compiler provider over checked Lean functions. The
 * build names the exported operation prefix and the provider name. Modules
 * live in the shared provider runtime, which initializes every provider's
 * modules once per process. The operations and their arities are the shared
 * source compiler protocol: read 3, verify 5, prepare 4, observe 4, package
 * 1, admit 5. Every reply is copied out of the Lean runtime. */

#if !defined(CETTA_SOURCE_COMPILER_PREFIX) || !defined(CETTA_SOURCE_COMPILER_NAME)
#error "The build must name the operation prefix and provider name."
#endif

#define CETTA_CAT_(a, b) a##b
#define CETTA_CAT(a, b) CETTA_CAT_(a, b)
#define CETTA_OP(op) CETTA_CAT(CETTA_CAT(CETTA_CAT(cetta_, CETTA_SOURCE_COMPILER_PREFIX), _), CETTA_CAT(op, _v1))

extern bool cetta_lean_providers_initialize_v1(void);
extern lean_object *CETTA_OP(read)(lean_object *, lean_object *, lean_object *);
extern lean_object *CETTA_OP(verify)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
extern lean_object *CETTA_OP(prepare)(lean_object *, lean_object *, lean_object *, lean_object *);
extern lean_object *CETTA_OP(observe)(lean_object *, lean_object *, lean_object *, lean_object *);
extern lean_object *CETTA_OP(package)(lean_object *);
extern lean_object *CETTA_OP(admit)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
extern void lean_initialize_thread(void);
extern void lean_finalize_thread(void);

static pthread_mutex_t compiler_lock = PTHREAD_MUTEX_INITIALIZER;

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
    bool admit = !strcmp(operation, "admit") && count == 5u;
    bool verify = !strcmp(operation, "verify") && count == 5u;
    if (!read && !prepare && !observe && !package && !admit && !verify) return 0;
    for (size_t i = 0; i < count; ++i) if (!utf8(arguments[i], lengths[i])) return 0;
    if (!cetta_lean_providers_initialize_v1()) return 0;
    pthread_mutex_lock(&compiler_lock);
    lean_initialize_thread();
    lean_object *args[5] = {0};
    for (size_t i = 0; i < count; ++i)
        args[i] = lean_mk_string_from_bytes(arguments[i] ? arguments[i] : "", lengths[i]);
    lean_object *result = admit ? CETTA_OP(admit)(args[0], args[1], args[2], args[3], args[4])
        : verify ? CETTA_OP(verify)(args[0], args[1], args[2], args[3], args[4])
        : package ? CETTA_OP(package)(args[0])
        : read ? CETTA_OP(read)(args[0], args[1], args[2])
        : prepare ? CETTA_OP(prepare)(args[0], args[1], args[2], args[3])
        : CETTA_OP(observe)(args[0], args[1], args[2], args[3]);
    size_t length = lean_string_size(result) - 1u;
    char *copy = malloc(length + 1u);
    if (copy) memcpy(copy, lean_string_cstr(result), length + 1u);
    lean_dec(result);
    /* No Lean value escapes a call. Balance each foreign-thread scope. */
    lean_finalize_thread();
    pthread_mutex_unlock(&compiler_lock);
    if (!copy) return 0;
    *response = copy;
    *response_length = length;
    return 1;
}

static const CettaSourceCompilerProviderV1 provider = {1u, CETTA_SOURCE_COMPILER_NAME, call, free};

__attribute__((visibility("default")))
const CettaSourceCompilerProviderV1 *cetta_source_compiler_v1(void) { return &provider; }
