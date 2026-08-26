#include "search_machine.h"

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
