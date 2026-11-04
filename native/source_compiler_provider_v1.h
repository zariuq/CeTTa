#ifndef CETTA_SOURCE_COMPILER_PROVIDER_V1_H
#define CETTA_SOURCE_COMPILER_PROVIDER_V1_H

#include <stddef.h>
#include <stdint.h>

/* Pure source/IR preparation. Arguments and replies are counted UTF-8, not
 * evaluated MeTTa. The package declares the meaning and arity of each
 * operation. A compiler provider has no Space/evaluator callbacks. */
/* The fixed `admit` operation receives source, core, parser-plan, execution
 * and compiler component data, in that order. Acceptance is the exact UTF-8
 * receipt SourceBindingAcceptedV1. Only the loader can turn a receipt from
 * this live provider call into an owning language value. */
typedef struct {
    uint32_t abi_version;
    const char *name;
    int (*call)(const char *operation, const char *const *arguments,
                const size_t *lengths, size_t count,
                char **response, size_t *response_length);
    void (*release)(void *response);
} CettaSourceCompilerProviderV1;

typedef const CettaSourceCompilerProviderV1 *(*CettaSourceCompilerEntryV1)(void);

#endif
