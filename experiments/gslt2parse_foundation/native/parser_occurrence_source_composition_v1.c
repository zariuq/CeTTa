#include "parser_occurrence_source_composition_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void pposc_v1_set_error(
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

static const PPRelationalStateActionV1 *pposc_v1_source_action(
    const PPRelationalStateProgramV1Plan *plan,
    uint32_t operation_id,
    bool *invalid_out) {
    const PPRelationalStateOperationV1 *operation;
    const PPRelationalStateActionV1 *source_action = NULL;
    uint32_t index;

    *invalid_out = false;
    if (!plan || operation_id >= plan->operation_len) {
        *invalid_out = true;
        return NULL;
    }
    operation = &plan->operations[operation_id];
    /* Empty authored programs carry no action slice to validate.  Generated
     * plans use UINT32_MAX as their action_begin sentinel, so validate the
     * begin offset only when there is at least one action to index. */
    if (operation->action_len == 0u)
        return NULL;
    if (operation->action_begin > plan->action_len ||
        operation->action_len >
            plan->action_len - operation->action_begin) {
        *invalid_out = true;
        return NULL;
    }
    for (index = 0u; index < operation->action_len; index++) {
        const PPRelationalStateActionV1 *action =
            &plan->actions[operation->action_begin + index];
        if (action->kind !=
                PPRELATIONAL_STATE_ACTION_V1_RESOLVE_SOURCE) {
            continue;
        }
        if (source_action) {
            *invalid_out = true;
            return NULL;
        }
        source_action = action;
    }
    /* Consuming a source operation is compositional only when resolution is
     * its entire authored action program.  Mixed action programs need an
     * explicit ordering construction rather than silently dropping actions. */
    if (source_action && operation->action_len != 1u) {
        *invalid_out = true;
        return NULL;
    }
    return source_action;
}

static bool pposc_v1_apply(
    void *context,
    const PPOccurrenceFoldV1Step *step,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceSourceCompositionV1 *composition = context;
    const PPRelationalStateActionV1 *source_action;
    bool invalid;

    if (!composition || !composition->active || composition->committed ||
        composition->aborted || !step ||
        step->operation_id >= composition->state_plan->operation_len) {
        pposc_v1_set_error(
            error_buf, error_buf_size,
            "source composition received an invalid occurrence");
        return false;
    }
    source_action = pposc_v1_source_action(
        composition->state_plan, step->operation_id, &invalid);
    if (invalid) {
        pposc_v1_set_error(
            error_buf, error_buf_size,
            "source-resolution operation has a mixed or invalid action program");
        return false;
    }
    if (source_action) {
        const PPOccurrenceFoldV1Value *source_path = NULL;
        PPOccurrenceSourceResolutionV1 resolution;
        PPOccurrenceFoldV1Backend nested_backend;
        uint32_t index;

        for (index = 0u; index < step->value_len; index++) {
            if (step->values[index].role_id != source_action->role_id)
                continue;
            if (source_path) {
                pposc_v1_set_error(
                    error_buf, error_buf_size,
                    "source-resolution occurrence has repeated path role");
                return false;
            }
            source_path = &step->values[index];
        }
        if (!source_path) {
            pposc_v1_set_error(
                error_buf, error_buf_size,
                "source-resolution occurrence has no path role");
            return false;
        }
        if (composition->nested_run_depth == UINT32_MAX) {
            pposc_v1_set_error(
                error_buf, error_buf_size,
                "source-composition nesting counter overflow");
            return false;
        }
        nested_backend = ppoccurrence_source_composition_v1_backend(
            composition);
        composition->nested_run_depth++;
        resolution = composition->resolver.resolve(
            composition->resolver.context, source_path,
            source_action->skip_completed_sources,
            source_action->reject_active_source_cycles,
            &nested_backend, error_buf, error_buf_size);
        composition->nested_run_depth--;
        if (resolution != PPOCCURRENCE_SOURCE_RESOLUTION_V1_ACCEPTED) {
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
                pposc_v1_set_error(
                    error_buf, error_buf_size,
                    "authored source resolution rejected the occurrence");
            }
            return false;
        }
        if (composition->resolution_len == UINT32_MAX) {
            pposc_v1_set_error(
                error_buf, error_buf_size,
                "source-resolution receipt overflow");
            return false;
        }
        composition->resolution_len++;
        return true;
    }
    {
        PPOccurrenceFoldV1Step tagged = *step;
        if (!composition->resolver.current(
                composition->resolver.context, &tagged.source_id,
                error_buf, error_buf_size) ||
            !composition->downstream.apply(
                composition->downstream.context, &tagged,
                error_buf, error_buf_size)) {
            return false;
        }
    }
    if (composition->forwarded_step_len == UINT32_MAX) {
        pposc_v1_set_error(
            error_buf, error_buf_size,
            "source-composition occurrence receipt overflow");
        return false;
    }
    composition->forwarded_step_len++;
    return true;
}

static bool pposc_v1_commit(
    void *context,
    char *error_buf,
    size_t error_buf_size) {
    PPOccurrenceSourceCompositionV1 *composition = context;

    if (!composition || !composition->active || composition->aborted ||
        composition->committed) {
        pposc_v1_set_error(
            error_buf, error_buf_size,
            "source composition cannot commit");
        return false;
    }
    if (composition->nested_run_depth > 0u)
        return true;
    if (!composition->resolver.digest(
            composition->resolver.context,
            composition->source_digest,
            error_buf, error_buf_size) ||
        !composition->downstream.commit(
            composition->downstream.context,
            error_buf, error_buf_size)) {
        return false;
    }
    composition->active = false;
    composition->committed = true;
    return true;
}

static void pposc_v1_abort(void *context) {
    PPOccurrenceSourceCompositionV1 *composition = context;

    if (!composition || !composition->active)
        return;
    if (composition->nested_run_depth > 0u)
        return;
    composition->downstream.abort(composition->downstream.context);
    composition->active = false;
    composition->aborted = true;
}

bool ppoccurrence_source_composition_v1_init(
    PPOccurrenceSourceCompositionV1 *composition,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPRelationalStateProgramV1Plan *state_plan,
    const PPOccurrenceSourceResolverV1 *resolver,
    const PPOccurrenceFoldV1Backend *downstream,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t operation_id;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!composition || !occurrence_plan || !state_plan || !resolver ||
        !resolver->context || !resolver->resolve || !resolver->digest ||
        !resolver->current || !downstream || !downstream->context ||
        !downstream->apply || !downstream->commit || !downstream->abort ||
        !pprelational_state_program_v1_plan_validate(
            occurrence_plan, state_plan, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            pposc_v1_set_error(
                error_buf, error_buf_size,
                "bad source-composition configuration");
        }
        return false;
    }
    for (operation_id = 0u;
         operation_id < state_plan->operation_len;
         operation_id++) {
        bool invalid;
        (void)pposc_v1_source_action(
            state_plan, operation_id, &invalid);
        if (invalid) {
            pposc_v1_set_error(
                error_buf, error_buf_size,
                "source-composition plan contains an invalid source operation");
            return false;
        }
    }
    memset(composition, 0, sizeof(*composition));
    composition->occurrence_plan = occurrence_plan;
    composition->state_plan = state_plan;
    composition->resolver = *resolver;
    composition->downstream = *downstream;
    composition->active = true;
    return true;
}

PPOccurrenceFoldV1Backend ppoccurrence_source_composition_v1_backend(
    PPOccurrenceSourceCompositionV1 *composition) {
    return (PPOccurrenceFoldV1Backend){
        .context = composition,
        .apply = pposc_v1_apply,
        .commit = pposc_v1_commit,
        .abort = pposc_v1_abort,
    };
}
