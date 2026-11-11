#ifndef CETTA_FOREIGN_PROVIDER_H
#define CETTA_FOREIGN_PROVIDER_H

#include "owned_execution.h"
struct Space;

/* A foreign provider owns an entered producer, independently of its host
 * language. Open consumes ready values, never source syntax. On success the
 * execution must own every borrowed operand and context it will use later.
 * Returned payloads live until its next pull or release. */
typedef struct {
    void *context;
    /* Run on the host entry thread, without implicit worker/fork dispatch.
     * A later host entry, including close, may use a different thread. */
    bool owner_thread;
    void (*retain)(void *context);
    void (*release)(void *context);
    bool (*open)(void *context, const CettaInvocation *invocation,
                 CettaOwnedExecution *execution, CettaEvalCompletion *completion);
} CettaForeignProviderSpec;

Atom *cetta_foreign_provider_atom(Arena *, const CettaForeignProviderSpec *);
bool cetta_foreign_provider_callable(const Atom *);
bool cetta_foreign_provider_callable_at(struct Space *, const Atom *);
bool cetta_foreign_provider_thread_affine(const Atom *);
/* False means the head is not a provider. A recognized refusal records its
 * incomplete cause and cannot authorize retrying entered host code. */
bool cetta_foreign_provider_open(struct Space *, const CettaInvocation *, CettaOwnedExecution *,
                                 CettaEvalCompletion *);
/* Both roots of a borrowed packet must survive acceptance of its loan. */
bool cetta_foreign_provider_copy_return(Arena *, CettaCallOutcome, CettaCallOutcome *);

#endif
