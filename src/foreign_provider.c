#include "foreign_provider.h"
#include "foreign.h"
#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    CettaForeignHold hold;
    atomic_uint references;
    CettaForeignProviderSpec spec;
} ForeignProvider;

static void provider_retain(void *pointer) {
    ForeignProvider *provider = pointer;
    atomic_fetch_add_explicit(&provider->references, 1u, memory_order_relaxed);
}
static void provider_release(void *pointer) {
    ForeignProvider *provider = pointer;
    if (atomic_fetch_sub_explicit(&provider->references, 1u, memory_order_acq_rel) != 1u)
        return;
    if (provider->spec.release) provider->spec.release(provider->spec.context);
    free(provider);
}
static void provider_print(const void *pointer, FILE *out) {
    (void)pointer;
    fputs("(ForeignCallable)", out);
}
static ForeignProvider *provider_of(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_FOREIGN ||
        !atom->ground.ptr) return NULL;
    const CettaForeignHold *hold = atom->ground.ptr;
    return hold->retain == provider_retain ? atom->ground.ptr : NULL;
}
Atom *cetta_foreign_provider_atom(Arena *arena, const CettaForeignProviderSpec *spec) {
    if (!arena || !spec || !spec->open || (!!spec->retain != !!spec->release)) return NULL;
    ForeignProvider *provider = calloc(1u, sizeof(*provider));
    if (!provider) return NULL;
    provider->hold = (CettaForeignHold){provider_retain, provider_release, provider_print};
    atomic_init(&provider->references, 1u);
    provider->spec = *spec;
    if (spec->retain) spec->retain(spec->context);
    Atom *atom = atom_foreign(arena, (CettaForeignValue *)provider);
    provider_release(provider);
    return atom;
}
bool cetta_foreign_provider_callable(const Atom *atom) {
    return cetta_foreign_provider_callable_at(NULL, atom);
}
bool cetta_foreign_provider_callable_at(Space *space, const Atom *atom) {
    return provider_of(atom) != NULL || cetta_foreign_object_provider_callable(space, atom);
}
bool cetta_foreign_provider_thread_affine(const Atom *atom) {
    ForeignProvider *provider = provider_of(atom);
    return (provider && provider->spec.owner_thread) || cetta_foreign_object_provider_callable(NULL, atom);
}

bool cetta_foreign_provider_open(Space *space, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    ForeignProvider *provider = invocation ? provider_of(invocation->head.skeleton) : NULL;
    if (!provider) return cetta_foreign_object_provider_open(space, invocation, execution, completion);
    *execution = (CettaOwnedExecution){0};
    *completion = CETTA_EVAL_COMPLETE;
    if (!provider->spec.open(provider->spec.context, invocation, execution, completion) ||
        !execution->computation || !execution->next) {
        cetta_owned_execution_cancel(execution);
        if (*completion == CETTA_EVAL_COMPLETE) *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
    }
    return true;
}

bool cetta_foreign_provider_copy_return(Arena *arena, CettaCallOutcome source,
        CettaCallOutcome *destination) {
    if (!arena || !destination || !cetta_call_outcome_shape_valid(source)) return false;
    CettaCallOutcome copy = source;
    if (source.term && !(copy.term = atom_deep_copy(arena, source.term))) return false;
    if (source.delayed && !(copy.delayed = atom_deep_copy(arena, source.delayed))) return false;
    *destination = copy;
    return true;
}
