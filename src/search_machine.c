#include "search_machine.h"

#include <stdlib.h>
#include <string.h>

static bool cetta_branch_capture_capacity_valid(
    CettaBranchCaptureCapacity capacity) {
    return capacity >= CETTA_BRANCH_CAPTURE_INLINE_ONLY &&
        capacity <= CETTA_BRANCH_CAPTURE_MULTI_SHOT;
}

static bool cetta_branch_storage_mode_valid(
    CettaBranchStorageMode mode) {
    return mode >= CETTA_BRANCH_STORAGE_INLINE &&
        mode <= CETTA_BRANCH_STORAGE_OWNED_FRONTIER;
}

const char *cetta_search_controller_policy_name(
    CettaSearchControllerPolicy policy) {
    switch (policy) {
    case CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST:
        return "inline-depth-first";
    case CETTA_SEARCH_CONTROLLER_FIFO:
        return "fifo";
    }
    return NULL;
}

bool cetta_search_controller_policy_parse(
    const char *name, CettaSearchControllerPolicy *policy) {
    if (!name || !policy)
        return false;
    if (strcmp(name, "inline-depth-first") == 0) {
        *policy = CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST;
        return true;
    }
    if (strcmp(name, "fifo") == 0) {
        *policy = CETTA_SEARCH_CONTROLLER_FIFO;
        return true;
    }
    return false;
}

CettaBranchCaptureCapacity cetta_branch_capture_weakest(
    CettaBranchCaptureCapacity first,
    CettaBranchCaptureCapacity second) {
    if (!cetta_branch_capture_capacity_valid(first) ||
        !cetta_branch_capture_capacity_valid(second)) {
        return CETTA_BRANCH_CAPTURE_INLINE_ONLY;
    }
    return first < second ? first : second;
}

bool cetta_branch_capture_admits(
    CettaBranchCaptureCapacity available,
    CettaBranchStorageMode requested) {
    if (!cetta_branch_capture_capacity_valid(available) ||
        !cetta_branch_storage_mode_valid(requested)) {
        return false;
    }
    CettaBranchCaptureCapacity required =
        requested == CETTA_BRANCH_STORAGE_OWNED_FRONTIER
            ? CETTA_BRANCH_CAPTURE_MULTI_SHOT
            : requested == CETTA_BRANCH_STORAGE_EXCLUSIVE_DEPTH_FIRST
                ? CETTA_BRANCH_CAPTURE_ONE_SHOT
                : CETTA_BRANCH_CAPTURE_INLINE_ONLY;
    return available >= required;
}

CettaBranchStorageMode cetta_branch_select_storage(
    CettaBranchCaptureCapacity available,
    CettaBranchStorageMode requested) {
    if (cetta_branch_capture_admits(available, requested))
        return requested;
    if (cetta_branch_capture_admits(
            available,
            CETTA_BRANCH_STORAGE_EXCLUSIVE_DEPTH_FIRST)) {
        return CETTA_BRANCH_STORAGE_EXCLUSIVE_DEPTH_FIRST;
    }
    return CETTA_BRANCH_STORAGE_INLINE;
}

bool cetta_branch_authority_token_equal(
    const CettaBranchAuthorityToken *left,
    const CettaBranchAuthorityToken *right) {
    if (!left || !right || left->length != right->length ||
        left->length > CETTA_BRANCH_AUTHORITY_TOKEN_WORD_CAPACITY) {
        return false;
    }
    return left->length == 0u ||
        memcmp(left->words, right->words,
               (size_t)left->length * sizeof(left->words[0])) == 0;
}

static bool cetta_continuation_backend_valid(
    const CettaContinuationBackend *backend) {
    return backend && backend->capture && backend->restore &&
        backend->destroy && backend->storage_bytes;
}

void cetta_owned_continuation_init(
    CettaOwnedContinuation *continuation) {
    if (continuation)
        *continuation = (CettaOwnedContinuation){0};
}

void cetta_owned_continuation_destroy(
    CettaOwnedContinuation *continuation) {
    if (!continuation)
        return;
    if (continuation->payload &&
        cetta_continuation_backend_valid(continuation->backend)) {
        continuation->backend->destroy(continuation->payload);
    }
    *continuation = (CettaOwnedContinuation){0};
}

CettaContinuationStatus cetta_continuation_capture(
    CettaContinuationMachine machine,
    CettaOwnedContinuation *continuation) {
    if (!machine.machine ||
        !cetta_continuation_backend_valid(machine.backend) ||
        !continuation || continuation->payload || continuation->backend) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    void *payload = NULL;
    CettaContinuationStatus status =
        machine.backend->capture(machine.machine, &payload);
    if (status != CETTA_CONTINUATION_READY) {
        if (payload)
            machine.backend->destroy(payload);
        return status;
    }
    if (!payload)
        return CETTA_CONTINUATION_UNSUPPORTED;
    continuation->payload = payload;
    continuation->backend = machine.backend;
    return CETTA_CONTINUATION_READY;
}

CettaContinuationStatus cetta_continuation_restore(
    CettaContinuationMachine machine,
    CettaOwnedContinuation *continuation) {
    if (!machine.machine ||
        !cetta_continuation_backend_valid(machine.backend) ||
        !continuation || !continuation->payload ||
        continuation->backend != machine.backend) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    CettaContinuationStatus status =
        machine.backend->restore(
            machine.machine, &continuation->payload);
    if (status == CETTA_CONTINUATION_READY) {
        if (continuation->payload)
            return CETTA_CONTINUATION_UNSUPPORTED;
        continuation->backend = NULL;
    }
    return status;
}

bool cetta_owned_continuation_storage_bytes(
    const CettaOwnedContinuation *continuation,
    size_t *atom_bytes,
    size_t *exclusive_vector_bytes) {
    return continuation && continuation->payload &&
        cetta_continuation_backend_valid(continuation->backend) &&
        atom_bytes && exclusive_vector_bytes &&
        continuation->backend->storage_bytes(
            continuation->payload,
            atom_bytes, exclusive_vector_bytes);
}

void cetta_continuation_frontier_init(
    CettaContinuationFrontier *frontier) {
    if (frontier)
        *frontier = (CettaContinuationFrontier){0};
}

void cetta_continuation_frontier_destroy(
    CettaContinuationFrontier *frontier) {
    if (!frontier)
        return;
    for (size_t i = 0u; i < frontier->length; i++)
        cetta_owned_continuation_destroy(&frontier->items[i]);
    free(frontier->items);
    *frontier = (CettaContinuationFrontier){0};
}

CettaContinuationStatus cetta_continuation_expand(
    CettaContinuationMachine machine,
    CettaContinuationFrontier *frontier) {
    if (!machine.machine ||
        !cetta_continuation_backend_valid(machine.backend) ||
        !machine.backend->expand || !frontier ||
        frontier->items || frontier->length != 0u) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }

    void **payloads = NULL;
    size_t length = 0u;
    CettaContinuationStatus status = machine.backend->expand(
        machine.machine, &payloads, &length);
    if (status != CETTA_CONTINUATION_READY ||
        !payloads || length == 0u ||
        length > SIZE_MAX / sizeof(*frontier->items)) {
        if (payloads) {
            for (size_t i = 0u; i < length; i++) {
                if (payloads[i])
                    machine.backend->destroy(payloads[i]);
            }
            free(payloads);
        }
        return status == CETTA_CONTINUATION_READY
            ? CETTA_CONTINUATION_UNSUPPORTED : status;
    }
    for (size_t i = 0u; i < length; i++) {
        if (payloads[i])
            continue;
        for (size_t j = 0u; j < length; j++) {
            if (payloads[j])
                machine.backend->destroy(payloads[j]);
        }
        free(payloads);
        return CETTA_CONTINUATION_UNSUPPORTED;
    }

    CettaOwnedContinuation *items =
        cetta_malloc(length * sizeof(*items));
    for (size_t i = 0u; i < length; i++) {
        items[i] = (CettaOwnedContinuation){
            .payload = payloads[i],
            .backend = machine.backend,
        };
    }
    free(payloads);
    frontier->items = items;
    frontier->length = length;
    return CETTA_CONTINUATION_READY;
}

void cetta_continuation_queue_init(
    CettaContinuationQueue *queue) {
    if (queue)
        *queue = (CettaContinuationQueue){0};
}

void cetta_continuation_queue_destroy(
    CettaContinuationQueue *queue) {
    if (!queue)
        return;
    for (size_t i = queue->begin; i < queue->end; i++)
        cetta_owned_continuation_destroy(&queue->items[i]);
    free(queue->items);
    *queue = (CettaContinuationQueue){0};
}

size_t cetta_continuation_queue_length(
    const CettaContinuationQueue *queue) {
    return queue && queue->end >= queue->begin
        ? queue->end - queue->begin : 0u;
}

static bool cetta_continuation_queue_reserve(
    CettaContinuationQueue *queue, size_t additional) {
    if (!queue || queue->begin > queue->end ||
        queue->end > queue->capacity ||
        (queue->capacity != 0u && !queue->items) ||
        additional > SIZE_MAX -
            cetta_continuation_queue_length(queue)) {
        return false;
    }
    size_t length = cetta_continuation_queue_length(queue);
    if (additional <= queue->capacity - queue->end)
        return true;
    if (queue->begin != 0u) {
        if (length != 0u) {
            memmove(
                queue->items, &queue->items[queue->begin],
                length * sizeof(*queue->items));
        }
        queue->begin = 0u;
        queue->end = length;
        if (additional <= queue->capacity - queue->end)
            return true;
    }
    size_t required = length + additional;
    size_t next = queue->capacity ? queue->capacity : 8u;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(*queue->items))
        return false;
    queue->items = cetta_realloc(
        queue->items, next * sizeof(*queue->items));
    queue->capacity = next;
    return true;
}

bool cetta_continuation_queue_push(
    CettaContinuationQueue *queue,
    CettaOwnedContinuation *continuation) {
    if (!queue || !continuation || !continuation->payload ||
        !continuation->backend ||
        !cetta_continuation_queue_reserve(queue, 1u)) {
        return false;
    }
    queue->items[queue->end++] = *continuation;
    *continuation = (CettaOwnedContinuation){0};
    return true;
}

bool cetta_continuation_queue_push_frontier(
    CettaContinuationQueue *queue,
    CettaContinuationFrontier *frontier) {
    if (!queue || !frontier || !frontier->items ||
        frontier->length == 0u) {
        return false;
    }
    for (size_t i = 0u; i < frontier->length; i++) {
        if (!frontier->items[i].payload ||
            !cetta_continuation_backend_valid(
                frontier->items[i].backend)) {
            return false;
        }
    }
    if (!cetta_continuation_queue_reserve(
            queue, frontier->length)) {
        return false;
    }
    memcpy(
        &queue->items[queue->end], frontier->items,
        frontier->length * sizeof(*frontier->items));
    queue->end += frontier->length;
    free(frontier->items);
    *frontier = (CettaContinuationFrontier){0};
    return true;
}

bool cetta_continuation_queue_pop(
    CettaContinuationQueue *queue,
    CettaOwnedContinuation *continuation) {
    if (!queue || !continuation || continuation->payload ||
        continuation->backend || queue->begin == queue->end) {
        return false;
    }
    *continuation = queue->items[queue->begin];
    queue->items[queue->begin++] = (CettaOwnedContinuation){0};
    if (queue->begin == queue->end)
        queue->begin = queue->end = 0u;
    return true;
}

bool cetta_continuation_queue_storage_bytes(
    const CettaContinuationQueue *queue,
    size_t *atom_bytes,
    size_t *exclusive_vector_bytes) {
    if (!queue || !atom_bytes || !exclusive_vector_bytes)
        return false;
    size_t atoms = 0u;
    size_t vectors = 0u;
    for (size_t i = queue->begin; i < queue->end; i++) {
        size_t item_atoms = 0u;
        size_t item_vectors = 0u;
        if (!cetta_owned_continuation_storage_bytes(
                &queue->items[i], &item_atoms, &item_vectors) ||
            item_atoms > SIZE_MAX - atoms ||
            item_vectors > SIZE_MAX - vectors) {
            return false;
        }
        atoms += item_atoms;
        vectors += item_vectors;
    }
    *atom_bytes = atoms;
    *exclusive_vector_bytes = vectors;
    return true;
}

bool search_context_init(SearchContext *ctx, const Bindings *base,
                         Arena *scratch_arena) {
    ctx->scratch_arena = scratch_arena;
    ctx->owns_scratch_arena = false;
    if (!ctx->scratch_arena) {
        arena_init(&ctx->owned_scratch_arena);
        arena_set_runtime_kind(&ctx->owned_scratch_arena,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        ctx->scratch_arena = &ctx->owned_scratch_arena;
        ctx->owns_scratch_arena = true;
    }
    if (bindings_builder_init(&ctx->bindings, base))
        return true;
    if (ctx->owns_scratch_arena) {
        arena_free(&ctx->owned_scratch_arena);
        ctx->scratch_arena = NULL;
        ctx->owns_scratch_arena = false;
    }
    return false;
}

void search_context_init_owned(SearchContext *ctx, Bindings *owned,
                               Arena *scratch_arena) {
    ctx->scratch_arena = scratch_arena;
    ctx->owns_scratch_arena = false;
    if (!ctx->scratch_arena) {
        arena_init(&ctx->owned_scratch_arena);
        arena_set_runtime_kind(&ctx->owned_scratch_arena,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        ctx->scratch_arena = &ctx->owned_scratch_arena;
        ctx->owns_scratch_arena = true;
    }
    bindings_builder_init_owned(&ctx->bindings, owned);
}

void search_context_free(SearchContext *ctx) {
    bindings_builder_free(&ctx->bindings);
    if (ctx->owns_scratch_arena) {
        arena_free(&ctx->owned_scratch_arena);
        ctx->scratch_arena = NULL;
        ctx->owns_scratch_arena = false;
    }
}

ChoicePoint search_context_save(const SearchContext *ctx) {
    ChoicePoint point = {
        .bindings_mark = bindings_builder_save(&ctx->bindings),
        .has_scratch_mark = ctx->scratch_arena != NULL,
    };
    if (point.has_scratch_mark)
        point.scratch_mark = arena_mark(ctx->scratch_arena);
    return point;
}

void search_context_rollback(SearchContext *ctx, ChoicePoint point) {
    bindings_builder_rollback(&ctx->bindings, point.bindings_mark);
    if (point.has_scratch_mark && ctx->scratch_arena)
        arena_reset(ctx->scratch_arena, point.scratch_mark);
}

Arena *search_context_scratch(SearchContext *ctx) {
    return ctx->scratch_arena;
}

BindingsBuilder *search_context_builder(SearchContext *ctx) {
    return &ctx->bindings;
}

const Bindings *search_context_bindings(const SearchContext *ctx) {
    return bindings_builder_bindings(&ctx->bindings);
}

void search_context_take(SearchContext *ctx, Bindings *out) {
    bindings_builder_take(&ctx->bindings, out);
}
