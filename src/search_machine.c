#include "search_machine.h"

bool search_context_init(SearchContext *ctx, const Bindings *base,
                         Arena *scratch_arena) {
    ctx->scratch_arena = scratch_arena;
    ctx->owns_scratch_arena = false;
    if (!ctx->scratch_arena) {
        arena_init(&ctx->owned_scratch_arena);
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
