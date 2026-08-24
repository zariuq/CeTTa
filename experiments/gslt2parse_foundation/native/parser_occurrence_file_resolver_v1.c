#define _XOPEN_SOURCE 700

#include "parser_occurrence_file_resolver_v1.h"

#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *canonical_path;
} PPOccurrenceFileResolverV1Source;

typedef struct {
    PPOccurrenceFileResolverV1 *owner;
    const PPGuardedLexCursorV1Program *program;
    const PPOccurrenceFoldV1Plan *occurrence_plan;
    const PPSourceResolutionControlV1Plan *control_plan;
    PPOccurrenceFileResolverV1Source *sources;
    uint32_t source_len;
    uint32_t source_cap;
    uint32_t *stack;
    uint32_t stack_len;
    uint32_t stack_cap;
    PPGuardedLexCursorV1Observation observation;
    uint64_t minimum_work_limit;
    uint64_t work_per_byte;
    uint32_t depth_limit;
    CettaNativeSha256 source_trace;
} PPOccurrenceFileResolverV1Impl;

static void ppofr_v1_set_error(
    char *buf,
    size_t size,
    const char *format,
    ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppofr_v1_array_fits(size_t count, size_t item_size) {
    return item_size == 0u || count <= SIZE_MAX / item_size;
}

static bool ppofr_v1_grow(
    void **data,
    uint32_t *cap,
    uint32_t needed,
    size_t item_size) {
    uint32_t next_cap;
    void *next;

    if (needed <= *cap)
        return true;
    next_cap = *cap ? *cap : 8u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if (!ppofr_v1_array_fits(next_cap, item_size))
        return false;
    next = realloc(*data, (size_t)next_cap * item_size);
    if (!next)
        return false;
    *data = next;
    *cap = next_cap;
    return true;
}

static void ppofr_v1_sha_u32(
    CettaNativeSha256 *sha,
    uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppofr_v1_sha_u64(
    CettaNativeSha256 *sha,
    uint64_t value) {
    uint8_t bytes[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        bytes[7u - index] = (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppofr_v1_sha_bytes(
    CettaNativeSha256 *sha,
    const uint8_t *bytes,
    size_t len) {
    ppofr_v1_sha_u64(sha, (uint64_t)len);
    if (len > 0u)
        cetta_native_sha256_update(sha, bytes, len);
}

static void ppofr_v1_trace_root(
    PPOccurrenceFileResolverV1Impl *impl,
    const uint8_t *bytes,
    size_t len) {
    const uint8_t tag = 0u;

    cetta_native_sha256_update(&impl->source_trace, &tag, 1u);
    ppofr_v1_sha_u32(&impl->source_trace, 0u);
    ppofr_v1_sha_bytes(
        &impl->source_trace, (const uint8_t *)"root", 4u);
    ppofr_v1_sha_bytes(&impl->source_trace, bytes, len);
}

static void ppofr_v1_trace_edge(
    PPOccurrenceFileResolverV1Impl *impl,
    uint8_t tag,
    const uint8_t *requested,
    size_t requested_len,
    uint32_t target_id,
    const uint8_t *bytes,
    size_t len) {
    cetta_native_sha256_update(&impl->source_trace, &tag, 1u);
    ppofr_v1_sha_bytes(
        &impl->source_trace, requested, requested_len);
    ppofr_v1_sha_u32(&impl->source_trace, target_id);
    if (tag == 1u)
        ppofr_v1_sha_bytes(&impl->source_trace, bytes, len);
}

static bool ppofr_v1_read_file(
    const char *path,
    uint8_t **out,
    size_t *out_len) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    if (!path || !out || !out_len)
        return false;
    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input)
        goto done;
    for (;;) {
        size_t amount;

        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;

            if (next_cap < cap || next_cap > UINT32_MAX)
                goto done;
            next = realloc(bytes, next_cap);
            if (!next)
                goto done;
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount == 0u) {
            if (ferror(input))
                goto done;
            break;
        }
    }
    *out = bytes;
    *out_len = len;
    bytes = NULL;
    ok = true;

done:
    if (input)
        (void)fclose(input);
    free(bytes);
    return ok;
}

static char *ppofr_v1_join_parent(
    const char *parent,
    const uint8_t *requested,
    size_t requested_len) {
    const char *slash;
    size_t prefix_len;
    bool separator;
    char *result;

    if (!parent || !requested || requested_len == 0u ||
        memchr(requested, '\0', requested_len)) {
        return NULL;
    }
    if (requested[0] == (uint8_t)'/') {
        result = malloc(requested_len + 1u);
        if (!result)
            return NULL;
        memcpy(result, requested, requested_len);
        result[requested_len] = '\0';
        return result;
    }
    slash = strrchr(parent, '/');
    if (!slash)
        prefix_len = 0u;
    else if (slash == parent)
        prefix_len = 1u;
    else
        prefix_len = (size_t)(slash - parent);
    separator =
        prefix_len > 0u && parent[prefix_len - 1u] != '/';
    if (prefix_len > SIZE_MAX - requested_len - 2u)
        return NULL;
    result = malloc(
        prefix_len + (separator ? 1u : 0u) +
            requested_len + 1u);
    if (!result)
        return NULL;
    if (prefix_len > 0u)
        memcpy(result, parent, prefix_len);
    if (separator)
        result[prefix_len++] = '/';
    memcpy(result + prefix_len, requested, requested_len);
    result[prefix_len + requested_len] = '\0';
    return result;
}

static int32_t ppofr_v1_find_source(
    const PPOccurrenceFileResolverV1Impl *impl,
    const char *canonical_path) {
    uint32_t index;

    for (index = 0u; index < impl->source_len; index++) {
        if (strcmp(
                impl->sources[index].canonical_path,
                canonical_path) == 0) {
            return (int32_t)index;
        }
    }
    return -1;
}

static bool ppofr_v1_source_is_active(
    const PPOccurrenceFileResolverV1Impl *impl,
    uint32_t source_id) {
    uint32_t index;

    for (index = 0u; index < impl->stack_len; index++) {
        if (impl->stack[index] == source_id)
            return true;
    }
    return false;
}

static uint64_t ppofr_v1_work_limit(
    const PPOccurrenceFileResolverV1Impl *impl,
    size_t byte_len) {
    if (impl->work_per_byte != 0u &&
        byte_len >
            (UINT64_MAX - impl->minimum_work_limit) /
                impl->work_per_byte)
        return UINT64_MAX;
    return impl->minimum_work_limit +
           (uint64_t)byte_len * impl->work_per_byte;
}

static PPOccurrenceSourceResolutionV1 ppofr_v1_control_outcome(
    PPSourceControlOutcomeV1 outcome) {
    switch (outcome) {
    case PPSOURCE_CONTROL_OUTCOME_V1_ACCEPTED:
        return PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED;
    case PPSOURCE_CONTROL_OUTCOME_V1_REFUSED:
        return PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED;
    case PPSOURCE_CONTROL_OUTCOME_V1_RESOURCE:
        return PPOCCURRENCE_SOURCE_RESOLUTION_V1_RESOURCE;
    case PPSOURCE_CONTROL_OUTCOME_V1_INVALID:
    default:
        return PPOCCURRENCE_SOURCE_RESOLUTION_V1_INVALID;
    }
}

static bool ppofr_v1_control_decide(
    const PPOccurrenceFileResolverV1Impl *impl,
    PPSourceControlProbeV1 probe,
    bool skip_completed_sources,
    bool reject_active_source_cycles,
    PPSourceControlDecisionV1 *decision_out,
    char *error_buf,
    size_t error_buf_size) {
    return impl && impl->control_plan &&
        ppsource_resolution_control_v1_decide(
            impl->control_plan, probe,
            skip_completed_sources, reject_active_source_cycles,
            decision_out, error_buf, error_buf_size);
}

static bool ppofr_v1_control_finish(
    const PPOccurrenceFileResolverV1Impl *impl,
    PPSourceControlDecisionV1 decision,
    PPSourceControlChildV1 child,
    PPOccurrenceSourceResolutionV1 *result_out,
    char *error_buf,
    size_t error_buf_size) {
    PPSourceControlOutcomeV1 outcome;

    if (!impl || !result_out || !impl->control_plan ||
        !ppsource_resolution_control_v1_finish(
            impl->control_plan, decision, child, &outcome,
            error_buf, error_buf_size)) {
        return false;
    }
    *result_out = ppofr_v1_control_outcome(outcome);
    return true;
}

static bool ppofr_v1_control_terminal(
    const PPOccurrenceFileResolverV1Impl *impl,
    PPSourceControlProbeV1 probe,
    bool skip_completed_sources,
    bool reject_active_source_cycles,
    PPOccurrenceSourceResolutionV1 *result_out,
    PPSourceControlDecisionV1 *decision_out,
    char *error_buf,
    size_t error_buf_size) {
    PPSourceControlDecisionV1 decision;

    if (!ppofr_v1_control_decide(
            impl, probe, skip_completed_sources,
            reject_active_source_cycles, &decision,
            error_buf, error_buf_size) ||
        !ppofr_v1_control_finish(
            impl, decision, PPSOURCE_CONTROL_CHILD_V1_NONE,
            result_out, error_buf, error_buf_size)) {
        return false;
    }
    if (decision_out)
        *decision_out = decision;
    return true;
}

static PPOccurrenceSourceResolutionV1 ppofr_v1_resolve(
    void *context,
    const PPOccurrenceFoldV1Value *source_path,
    bool skip_completed_sources,
    bool reject_active_source_cycles,
    const PPOccurrenceFoldV1Backend *nested_backend,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceFileResolverV1Impl *impl = context;
    const char *parent;
    char *candidate = NULL;
    char *canonical = NULL;
    uint8_t *bytes = NULL;
    size_t byte_len = 0u;
    int32_t seen_id;
    uint32_t source_id;
    PPOccurrenceFoldV1Receipt receipt = {0};
    bool run_ok;
    PPOccurrenceSourceResolutionV1 result =
        PPOCCURRENCE_SOURCE_RESOLUTION_V1_INVALID;
    PPSourceControlDecisionV1 fresh_decision =
        PPSOURCE_CONTROL_DECISION_V1_INVALID;

    if (!impl || !impl->owner || !source_path ||
        !nested_backend || !nested_backend->apply ||
        !nested_backend->commit || !nested_backend->abort ||
        impl->stack_len == 0u ||
        source_path->byte_len == 0u) {
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "bad recursive source-resolution request");
        goto done;
    }
    parent = impl->sources[
        impl->stack[impl->stack_len - 1u]].canonical_path;
    candidate = ppofr_v1_join_parent(
        parent, source_path->bytes, source_path->byte_len);
    if (!candidate || !(canonical = realpath(candidate, NULL))) {
        if (impl->control_plan) {
            if (!ppofr_v1_control_terminal(
                    impl, PPSOURCE_CONTROL_PROBE_V1_MISSING,
                    skip_completed_sources,
                    reject_active_source_cycles, &result, NULL,
                    error_buf, error_buf_size)) {
                goto done;
            }
        } else {
            result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED;
        }
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "cannot resolve or read an included source");
        goto done;
    }
    seen_id = ppofr_v1_find_source(impl, canonical);
    if (seen_id >= 0) {
        bool active = ppofr_v1_source_is_active(
            impl, (uint32_t)seen_id);

        if (impl->control_plan) {
            PPSourceControlDecisionV1 decision;

            if (!ppofr_v1_control_terminal(
                    impl,
                    active ? PPSOURCE_CONTROL_PROBE_V1_ACTIVE
                           : PPSOURCE_CONTROL_PROBE_V1_COMPLETED,
                    skip_completed_sources,
                    reject_active_source_cycles, &result, &decision,
                    error_buf, error_buf_size)) {
                goto done;
            }
            if (result != PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED) {
                ppofr_v1_set_error(
                    error_buf, error_buf_size,
                    active
                        ? "an active source-resolution cycle was refused"
                        : "a repeated completed source was refused");
                goto done;
            }
            if ((active &&
                 decision != PPSOURCE_CONTROL_DECISION_V1_SKIP_ACTIVE) ||
                (!active && decision !=
                    PPSOURCE_CONTROL_DECISION_V1_SKIP_COMPLETED)) {
                ppofr_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution control accepted an invalid skip decision");
                result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_INVALID;
                goto done;
            }
        } else if ((active && reject_active_source_cycles) ||
                   (!active && !skip_completed_sources)) {
            ppofr_v1_set_error(
                error_buf, error_buf_size,
                active
                    ? "an active source-resolution cycle was detected"
                    : "a completed source was included more than once");
            result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED;
            goto done;
        }
        ppofr_v1_trace_edge(
            impl, active ? 3u : 2u,
            source_path->bytes, source_path->byte_len,
            (uint32_t)seen_id, NULL, 0u);
        impl->owner->skipped_source_len++;
        result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED;
        goto done;
    }
    if (impl->stack_len >= impl->depth_limit) {
        if (impl->control_plan &&
            !ppofr_v1_control_terminal(
                impl, PPSOURCE_CONTROL_PROBE_V1_RESOURCE,
                skip_completed_sources,
                reject_active_source_cycles, &result, NULL,
                error_buf, error_buf_size)) {
            goto done;
        }
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution depth resource limit exceeded");
        if (!impl->control_plan)
            result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_RESOURCE;
        goto done;
    }
    if (impl->control_plan &&
        (!ppofr_v1_control_decide(
             impl, PPSOURCE_CONTROL_PROBE_V1_FRESH,
             skip_completed_sources, reject_active_source_cycles,
             &fresh_decision, error_buf, error_buf_size) ||
         fresh_decision !=
             PPSOURCE_CONTROL_DECISION_V1_EXPAND_FRESH)) {
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution control did not authorize fresh expansion");
        result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_INVALID;
        goto done;
    }
    if (!ppofr_v1_read_file(canonical, &bytes, &byte_len)) {
        if (impl->control_plan &&
            !ppofr_v1_control_terminal(
                impl, PPSOURCE_CONTROL_PROBE_V1_MISSING,
                skip_completed_sources,
                reject_active_source_cycles, &result, NULL,
                error_buf, error_buf_size)) {
            goto done;
        }
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "cannot read an included source");
        if (!impl->control_plan)
            result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED;
        goto done;
    }
    if (!ppofr_v1_grow(
            (void **)&impl->sources, &impl->source_cap,
            impl->source_len + 1u, sizeof(*impl->sources)) ||
        !ppofr_v1_grow(
            (void **)&impl->stack, &impl->stack_cap,
            impl->stack_len + 1u, sizeof(*impl->stack))) {
        if (impl->control_plan &&
            !ppofr_v1_control_terminal(
                impl, PPSOURCE_CONTROL_PROBE_V1_RESOURCE,
                skip_completed_sources,
                reject_active_source_cycles, &result, NULL,
                error_buf, error_buf_size)) {
            goto done;
        }
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate source-resolution state");
        if (!impl->control_plan)
            result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_RESOURCE;
        goto done;
    }
    source_id = impl->source_len++;
    impl->sources[source_id].canonical_path = canonical;
    canonical = NULL;
    impl->owner->source_len = impl->source_len;
    ppofr_v1_trace_edge(
        impl, 1u, source_path->bytes, source_path->byte_len,
        source_id, bytes, byte_len);
    impl->stack[impl->stack_len++] = source_id;
    if (impl->stack_len > impl->owner->max_depth)
        impl->owner->max_depth = impl->stack_len;
    run_ok = ppoccurrence_fold_v1_run_bytes_prevalidated(
        impl->program, impl->occurrence_plan,
        bytes, byte_len, nested_backend,
        impl->observation, ppofr_v1_work_limit(impl, byte_len),
        &receipt, error_buf, error_buf_size);
    impl->stack_len--;
    if (impl->control_plan) {
        PPSourceControlChildV1 child =
            !run_ok
                ? PPSOURCE_CONTROL_CHILD_V1_INVALID
                : receipt.parser_receipt.outcome ==
                      PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT
                      ? PPSOURCE_CONTROL_CHILD_V1_RESOURCE
                      : receipt.parser_receipt.outcome !=
                                PPGUARDED_LEX_CURSOR_V1_ACCEPTED ||
                            !receipt.committed
                            ? PPSOURCE_CONTROL_CHILD_V1_REFUSED
                            : PPSOURCE_CONTROL_CHILD_V1_ACCEPTED;

        if (!ppofr_v1_control_finish(
                impl, fresh_decision, child, &result,
                error_buf, error_buf_size)) {
            goto done;
        }
        if (result != PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED &&
            error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppofr_v1_set_error(
                error_buf, error_buf_size,
                "included source did not produce an accepted authored outcome");
        }
        goto done;
    }
    if (!run_ok ||
        receipt.parser_receipt.outcome !=
            PPGUARDED_LEX_CURSOR_V1_ACCEPTED ||
        !receipt.committed) {
        if (error_buf && error_buf_size > 0u &&
            error_buf[0] == '\0') {
            ppofr_v1_set_error(
                error_buf, error_buf_size,
                "included source was rejected");
        }
        result = receipt.parser_receipt.outcome ==
                     PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT
                     ? PPOCCURRENCE_SOURCE_RESOLUTION_V1_RESOURCE
                     : receipt.parser_receipt.outcome !=
                               PPGUARDED_LEX_CURSOR_V1_ACCEPTED
                           ? PPOCCURRENCE_SOURCE_RESOLUTION_V1_REJECTED
                           : PPOCCURRENCE_SOURCE_RESOLUTION_V1_INVALID;
        goto done;
    }
    result = PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED;

done:
    free(bytes);
    free(canonical);
    free(candidate);
    return result;
}

static bool ppofr_v1_digest(
    void *context,
    char out[65],
    char *error_buf,
    size_t error_buf_size) {
    const PPOccurrenceFileResolverV1Impl *impl = context;
    CettaNativeSha256 trace;

    if (!impl || !out) {
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "source resolver has no authenticated trace");
        return false;
    }
    trace = impl->source_trace;
    cetta_native_sha256_finish_hex(&trace, out);
    return true;
}

static bool ppofr_v1_current(
    void *context,
    uint32_t *source_id_out,
    char *error_buf,
    size_t error_buf_size) {
    const PPOccurrenceFileResolverV1Impl *impl = context;

    if (!impl || !source_id_out || impl->stack_len == 0u) {
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "source resolver has no active source");
        return false;
    }
    *source_id_out = impl->stack[impl->stack_len - 1u];
    return true;
}

void ppoccurrence_file_resolver_v1_init(
    PPOccurrenceFileResolverV1 *resolver) {
    if (resolver)
        memset(resolver, 0, sizeof(*resolver));
}

void ppoccurrence_file_resolver_v1_free(
    PPOccurrenceFileResolverV1 *resolver) {
    PPOccurrenceFileResolverV1Impl *impl;
    uint32_t index;

    if (!resolver)
        return;
    impl = resolver->implementation;
    if (impl) {
        for (index = 0u; index < impl->source_len; index++)
            free(impl->sources[index].canonical_path);
        free(impl->sources);
        free(impl->stack);
        free(impl);
    }
    memset(resolver, 0, sizeof(*resolver));
}

static bool ppofr_v1_configure(
    PPOccurrenceFileResolverV1 *resolver,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPSourceResolutionControlV1Plan *control_plan,
    const char *root_path,
    const uint8_t *root_bytes,
    size_t root_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t minimum_work_limit,
    uint64_t work_per_byte,
    uint32_t depth_limit,
    char *error_buf,
    size_t error_buf_size) {
    static const char domain[] = "OccurrenceFileSourceDAGV1";
    PPOccurrenceFileResolverV1Impl *impl = NULL;
    char *canonical = NULL;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!resolver || !program || !occurrence_plan || !root_path ||
        (!root_bytes && root_byte_len > 0u) ||
        root_byte_len > UINT32_MAX || minimum_work_limit == 0u ||
        depth_limit == 0u ||
        !ppoccurrence_fold_v1_plan_validate_program(
            program, occurrence_plan, error_buf, error_buf_size) ||
        (control_plan &&
         !ppsource_resolution_control_v1_plan_validate(
             control_plan, error_buf, error_buf_size)) ||
        !(canonical = realpath(root_path, NULL))) {
        if (error_buf && error_buf_size > 0u &&
            error_buf[0] == '\0') {
            ppofr_v1_set_error(
                error_buf, error_buf_size,
                "bad file-source resolver configuration");
        }
        goto done;
    }
    ppoccurrence_file_resolver_v1_free(resolver);
    impl = calloc(1u, sizeof(*impl));
    if (!impl ||
        !ppofr_v1_grow(
            (void **)&impl->sources, &impl->source_cap,
            1u, sizeof(*impl->sources)) ||
        !ppofr_v1_grow(
            (void **)&impl->stack, &impl->stack_cap,
            1u, sizeof(*impl->stack))) {
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate file-source resolver");
        goto done;
    }
    impl->owner = resolver;
    impl->program = program;
    impl->occurrence_plan = occurrence_plan;
    impl->control_plan = control_plan;
    impl->observation = observation;
    impl->minimum_work_limit = minimum_work_limit;
    impl->work_per_byte = work_per_byte;
    impl->depth_limit = depth_limit;
    impl->sources[0].canonical_path = canonical;
    canonical = NULL;
    impl->source_len = 1u;
    impl->stack[0] = 0u;
    impl->stack_len = 1u;
    cetta_native_sha256_init(&impl->source_trace);
    ppofr_v1_sha_bytes(
        &impl->source_trace,
        (const uint8_t *)domain, sizeof(domain) - 1u);
    if (control_plan) {
        ppofr_v1_sha_bytes(
            &impl->source_trace,
            (const uint8_t *)control_plan->plan_digest, 64u);
    }
    ppofr_v1_trace_root(impl, root_bytes, root_byte_len);
    resolver->implementation = impl;
    resolver->source_len = 1u;
    resolver->max_depth = 1u;
    impl = NULL;
    ok = true;

done:
    if (impl) {
        free(impl->sources);
        free(impl->stack);
        free(impl);
    }
    free(canonical);
    return ok;
}

bool ppoccurrence_file_resolver_v1_configure(
    PPOccurrenceFileResolverV1 *resolver,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const char *root_path,
    const uint8_t *root_bytes,
    size_t root_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t minimum_work_limit,
    uint64_t work_per_byte,
    uint32_t depth_limit,
    char *error_buf,
    size_t error_buf_size) {
    return ppofr_v1_configure(
        resolver, program, occurrence_plan, NULL,
        root_path, root_bytes, root_byte_len, observation,
        minimum_work_limit, work_per_byte, depth_limit,
        error_buf, error_buf_size);
}

bool ppoccurrence_file_resolver_v1_configure_controlled(
    PPOccurrenceFileResolverV1 *resolver,
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPSourceResolutionControlV1Plan *control_plan,
    const char *root_path,
    const uint8_t *root_bytes,
    size_t root_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t minimum_work_limit,
    uint64_t work_per_byte,
    uint32_t depth_limit,
    char *error_buf,
    size_t error_buf_size) {
    if (!control_plan) {
        ppofr_v1_set_error(
            error_buf, error_buf_size,
            "controlled source resolver requires an authored control plan");
        return false;
    }
    return ppofr_v1_configure(
        resolver, program, occurrence_plan, control_plan,
        root_path, root_bytes, root_byte_len, observation,
        minimum_work_limit, work_per_byte, depth_limit,
        error_buf, error_buf_size);
}

PPOccurrenceSourceResolverV1
ppoccurrence_file_resolver_v1_interface(
    PPOccurrenceFileResolverV1 *resolver) {
    return (PPOccurrenceSourceResolverV1){
        .context = resolver ? resolver->implementation : NULL,
        .resolve = ppofr_v1_resolve,
        .digest = ppofr_v1_digest,
        .current = ppofr_v1_current,
    };
}
