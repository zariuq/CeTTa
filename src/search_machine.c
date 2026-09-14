#include "search_machine.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cetta_ratio_share_parse(
        const char *name, uint32_t *newest_share) {
    if (!name || !newest_share)
        return false;
    if (strcmp(name, "ratio") == 0) {
        *newest_share = 8u;
        return true;
    }
    if (strncmp(name, "ratio:", 6) != 0 || name[6] == '\0')
        return false;
    uint32_t parsed = 0u;
    for (const unsigned char *cursor =
             (const unsigned char *)name + 6;
         *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9')
            return false;
        uint32_t digit = (uint32_t)(*cursor - '0');
        if (parsed >
            (CETTA_SELECTION_AUTOMATON_STATE_CAPACITY - 1u - digit) /
                10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }
    if (parsed >= CETTA_SELECTION_AUTOMATON_STATE_CAPACITY)
        return false;
    *newest_share = parsed;
    return true;
}

static bool cetta_branch_capture_capacity_valid(
    CettaBranchCaptureCapacity capacity) {
    return capacity >= CETTA_BRANCH_CAPTURE_INLINE_ONLY &&
        capacity <= CETTA_BRANCH_CAPTURE_MULTI_SHOT;
}

static bool cetta_branch_storage_mode_valid(
    CettaBranchStorageMode mode) {
    return mode >= CETTA_BRANCH_STORAGE_INLINE &&
        mode <= CETTA_BRANCH_STORAGE_OWNED_MULTI_SHOT;
}

const char *cetta_search_controller_policy_name(
    CettaSearchControllerPolicy policy) {
    switch (policy) {
    case CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST:
        return "inline-depth-first";
    case CETTA_SEARCH_CONTROLLER_FIFO:
        return "fifo";
    case CETTA_SEARCH_CONTROLLER_RATIO:
        return "ratio";
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
    uint32_t newest_share = 0u;
    if (cetta_ratio_share_parse(name, &newest_share)) {
        *policy = CETTA_SEARCH_CONTROLLER_RATIO;
        return true;
    }
    return false;
}

bool cetta_selection_automaton_valid(
    const CettaSelectionAutomaton *automaton) {
    if (!automaton || automaton->state_count == 0u ||
        automaton->state_count >
            CETTA_SELECTION_AUTOMATON_STATE_CAPACITY ||
        automaton->state >= automaton->state_count) {
        return false;
    }
    for (uint32_t i = 0; i < automaton->state_count; i++) {
        if (automaton->next[i] >= automaton->state_count)
            return false;
        if (automaton->lane[i] != CETTA_SELECTION_LANE_OLDEST &&
            automaton->lane[i] != CETTA_SELECTION_LANE_NEWEST)
            return false;
    }
    return true;
}

bool cetta_selection_automaton_fifo(
    CettaSelectionAutomaton *automaton) {
    if (!automaton)
        return false;
    memset(automaton, 0, sizeof(*automaton));
    automaton->state_count = 1u;
    automaton->lane[0] = CETTA_SELECTION_LANE_OLDEST;
    automaton->next[0] = 0u;
    return true;
}

bool cetta_selection_automaton_lifo(
    CettaSelectionAutomaton *automaton) {
    if (!automaton)
        return false;
    memset(automaton, 0, sizeof(*automaton));
    automaton->state_count = 1u;
    automaton->lane[0] = CETTA_SELECTION_LANE_NEWEST;
    automaton->next[0] = 0u;
    return true;
}

bool cetta_selection_automaton_ratio(
    uint32_t newest_share,
    CettaSelectionAutomaton *automaton) {
    if (!automaton ||
        newest_share >=
            CETTA_SELECTION_AUTOMATON_STATE_CAPACITY) {
        return false;
    }
    memset(automaton, 0, sizeof(*automaton));
    automaton->state_count = newest_share + 1u;
    for (uint32_t i = 0; i < newest_share; i++) {
        automaton->lane[i] = CETTA_SELECTION_LANE_NEWEST;
        automaton->next[i] = i + 1u;
    }
    automaton->lane[newest_share] = CETTA_SELECTION_LANE_OLDEST;
    automaton->next[newest_share] = 0u;
    return true;
}

bool cetta_selection_automaton_has_recurrent_oldest(
    const CettaSelectionAutomaton *automaton) {
    if (!cetta_selection_automaton_valid(automaton))
        return false;
    /* A deterministic tick automaton has one cycle reachable from the
     * current state.  Walk state_count steps to enter it, then inspect one
     * complete lap for an OLDEST state. */
    uint32_t state = automaton->state;
    for (uint32_t i = 0; i < automaton->state_count; i++)
        state = automaton->next[state];
    uint32_t lap = state;
    do {
        if (automaton->lane[lap] == CETTA_SELECTION_LANE_OLDEST)
            return true;
        lap = automaton->next[lap];
    } while (lap != state);
    return false;
}

bool cetta_selection_automaton_select(
    CettaSelectionAutomaton *automaton,
    size_t length,
    size_t *index) {
    if (!cetta_selection_automaton_valid(automaton) ||
        length == 0u || !index) {
        return false;
    }
    if (length == 1u) {
        *index = 0u;
        return true;
    }
    CettaSelectionLane lane =
        (CettaSelectionLane)automaton->lane[automaton->state];
    automaton->state = automaton->next[automaton->state];
    *index = lane == CETTA_SELECTION_LANE_OLDEST ? 0u : length - 1u;
    return true;
}

bool cetta_selection_automaton_parse(
    const char *name,
    CettaSelectionAutomaton *automaton) {
    if (!name || !automaton)
        return false;
    if (strcmp(name, "fifo") == 0)
        return cetta_selection_automaton_fifo(automaton);
    if (strcmp(name, "lifo") == 0)
        return cetta_selection_automaton_lifo(automaton);
    uint32_t newest_share = 0u;
    return cetta_ratio_share_parse(name, &newest_share) &&
        cetta_selection_automaton_ratio(newest_share, automaton);
}

static bool cetta_observation_demand_valid(
        CettaObservationDemand demand) {
    if (demand.completion < CETTA_OBSERVATION_FIRST ||
        demand.completion > CETTA_OBSERVATION_UNDETERMINED) {
        return false;
    }
    return demand.completion == CETTA_OBSERVATION_FINITE_PREFIX ||
        demand.prefix_limit == 0u;
}

static bool cetta_control_batch_authority_valid(
        CettaControlBatchAuthority authority) {
    return authority >= CETTA_CONTROL_BATCH_SINGLETON_ONLY &&
        authority <= CETTA_CONTROL_BATCH_SERIALIZABLE;
}

static bool cetta_control_branch_authority_valid(
        CettaControlBranchAuthority authority) {
    return authority >= CETTA_CONTROL_BRANCH_GENERAL &&
        authority <= CETTA_CONTROL_BRANCH_SINGLE_PATH;
}

static bool cetta_control_plan_valid(const CettaControlPlan *plan) {
    return plan &&
        plan->readout >= CETTA_OBSERVATION_FIRST &&
        plan->readout <= CETTA_OBSERVATION_UNDETERMINED &&
        plan->activation >= CETTA_CONTROL_ACTIVATE_NONE &&
        plan->activation <= CETTA_CONTROL_ACTIVATE_BULK &&
        (plan->readout == CETTA_OBSERVATION_FINITE_PREFIX ||
         plan->prefix_limit == 0u);
}

bool cetta_control_plan_derive(
        CettaObservationDemand demand,
        CettaControlBranchAuthority branch_authority,
        CettaControlBatchAuthority batch_authority,
        CettaControlPlan *plan) {
    if (!plan || !cetta_observation_demand_valid(demand) ||
        !cetta_control_branch_authority_valid(branch_authority) ||
        !cetta_control_batch_authority_valid(batch_authority)) {
        return false;
    }
    CettaControlPlan prepared = {
        .readout = demand.completion,
        .activation = CETTA_CONTROL_ACTIVATE_CONTROLLED,
        .prefix_limit =
            demand.completion == CETTA_OBSERVATION_FINITE_PREFIX
                ? demand.prefix_limit : 0u,
    };
    if (demand.completion == CETTA_OBSERVATION_FINITE_PREFIX &&
        demand.prefix_limit == 0u) {
        prepared.activation = CETTA_CONTROL_ACTIVATE_NONE;
    } else if (branch_authority ==
               CETTA_CONTROL_BRANCH_SINGLE_PATH) {
        prepared.activation = CETTA_CONTROL_ACTIVATE_SINGLE_PATH;
    } else if (demand.completion == CETTA_OBSERVATION_COMPLETE_BAG &&
               batch_authority == CETTA_CONTROL_BATCH_SERIALIZABLE) {
        prepared.activation = CETTA_CONTROL_ACTIVATE_BULK;
    }
    *plan = prepared;
    return true;
}

bool cetta_control_plan_observation_satisfied(
        const CettaControlPlan *plan, uint64_t observed) {
    if (!cetta_control_plan_valid(plan)) {
        return false;
    }
    if (plan->readout == CETTA_OBSERVATION_FIRST)
        return observed != 0u;
    if (plan->readout == CETTA_OBSERVATION_FINITE_PREFIX)
        return observed >= plan->prefix_limit;
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
        requested == CETTA_BRANCH_STORAGE_OWNED_MULTI_SHOT
            ? CETTA_BRANCH_CAPTURE_MULTI_SHOT
            : requested == CETTA_BRANCH_STORAGE_EXCLUSIVE_ONE_SHOT
                ? CETTA_BRANCH_CAPTURE_ONE_SHOT
                : CETTA_BRANCH_CAPTURE_INLINE_ONLY;
    return available >= required;
}

bool cetta_branch_storage_admit_exact(
    CettaBranchCaptureCapacity available,
    CettaBranchStorageMode requested,
    CettaBranchStorageMode *admitted) {
    if (!admitted ||
        !cetta_branch_capture_admits(available, requested)) {
        return false;
    }
    *admitted = requested;
    return true;
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

static bool cetta_continuation_provider_valid(
    const CettaContinuationProvider *provider) {
    return provider && provider->representation_name &&
        provider->representation_name[0] != '\0' &&
        provider->ownership.capture && provider->ownership.restore &&
        provider->ownership.destroy && provider->ownership.storage;
}

const char *cetta_continuation_machine_representation_name(
    CettaContinuationMachine machine) {
    return cetta_continuation_provider_valid(machine.provider)
        ? machine.provider->representation_name : NULL;
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
        cetta_continuation_provider_valid(continuation->provider)) {
        continuation->provider->ownership.destroy(continuation->payload);
    }
    *continuation = (CettaOwnedContinuation){0};
}

CettaContinuationStatus cetta_continuation_capture(
    CettaContinuationMachine machine,
    CettaOwnedContinuation *continuation) {
    if (!machine.machine ||
        !cetta_continuation_provider_valid(machine.provider) ||
        !continuation || continuation->payload || continuation->provider ||
        continuation->occurrence_id != 0u ||
        continuation->parent_occurrence_id != 0u) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    void *payload = NULL;
    CettaContinuationStatus status =
        machine.provider->ownership.capture(machine.machine, &payload);
    if (status != CETTA_CONTINUATION_READY) {
        if (payload)
            machine.provider->ownership.destroy(payload);
        return status;
    }
    if (!payload)
        return CETTA_CONTINUATION_UNSUPPORTED;
    continuation->payload = payload;
    continuation->provider = machine.provider;
    return CETTA_CONTINUATION_READY;
}

CettaContinuationStatus cetta_continuation_restore(
    CettaContinuationMachine machine,
    CettaOwnedContinuation *continuation) {
    if (!machine.machine ||
        !cetta_continuation_provider_valid(machine.provider) ||
        !continuation || !continuation->payload ||
        continuation->provider != machine.provider) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    CettaContinuationStatus status =
        machine.provider->ownership.restore(
            machine.machine, &continuation->payload);
    if (status == CETTA_CONTINUATION_READY) {
        if (continuation->payload)
            return CETTA_CONTINUATION_UNSUPPORTED;
        *continuation = (CettaOwnedContinuation){0};
    }
    return status;
}

bool cetta_owned_continuation_storage(
    const CettaOwnedContinuation *continuation,
    CettaContinuationStorage *storage) {
    return continuation && continuation->payload &&
        cetta_continuation_provider_valid(continuation->provider) &&
        storage && continuation->provider->ownership.storage(
            continuation->payload, storage) &&
        (storage->shared_bytes == 0u || storage->shared_identity);
}

static bool cetta_continuation_component_valid(
        CettaContinuationComponent component) {
    return component >= CETTA_CONTINUATION_COMPONENT_AUTHORITY &&
        component < CETTA_CONTINUATION_COMPONENT_COUNT;
}

const char *cetta_owned_continuation_component_representation(
        const CettaOwnedContinuation *continuation,
        CettaContinuationComponent component) {
    if (!continuation || !continuation->payload ||
        !cetta_continuation_provider_valid(continuation->provider) ||
        !cetta_continuation_component_valid(component)) {
        return NULL;
    }
    const char *name =
        continuation->provider->components.representation[component];
    return name && name[0] != '\0' ? name : NULL;
}

bool cetta_owned_continuation_component_storage(
        const CettaOwnedContinuation *continuation,
        CettaContinuationComponent component,
        CettaContinuationStorage *storage) {
    if (!continuation || !continuation->payload ||
        !cetta_continuation_provider_valid(continuation->provider) ||
        !cetta_continuation_component_valid(component) || !storage ||
        !cetta_owned_continuation_component_representation(
            continuation, component) ||
        !continuation->provider->components.storage) {
        return false;
    }
    *storage = (CettaContinuationStorage){0};
    return continuation->provider->components.storage(
            continuation->payload, component, storage) &&
        (storage->shared_bytes == 0u || storage->shared_identity);
}

void cetta_continuation_trace_init(
    CettaContinuationTrace *trace) {
    if (trace)
        *trace = (CettaContinuationTrace){0};
}

void cetta_continuation_trace_destroy(
    CettaContinuationTrace *trace) {
    if (!trace)
        return;
    free(trace->bytes);
    *trace = (CettaContinuationTrace){0};
}

CettaContinuationStatus cetta_owned_continuation_trace(
        const CettaOwnedContinuation *continuation,
        CettaContinuationTrace *trace) {
    if (!continuation || !continuation->payload ||
        !cetta_continuation_provider_valid(continuation->provider) ||
        !continuation->provider->projection.trace || !trace || trace->bytes ||
        trace->length != 0u || trace->projection_identity != 0u) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    CettaContinuationStatus status = continuation->provider->projection.trace(
        continuation->payload, trace);
    if (status != CETTA_CONTINUATION_READY || !trace->bytes ||
        trace->length == 0u || trace->projection_identity == 0u) {
        cetta_continuation_trace_destroy(trace);
        return status == CETTA_CONTINUATION_READY
            ? CETTA_CONTINUATION_UNSUPPORTED : status;
    }
    return CETTA_CONTINUATION_READY;
}

static void cetta_controller_identity_permutation(
        size_t *permutation, size_t length) {
    for (size_t i = 0u; i < length; i++)
        permutation[i] = i;
}

static bool cetta_controller_permutation_complete(
        const size_t *permutation, size_t length) {
    if (length == 0u)
        return true;
    if (!permutation)
        return false;
    uint8_t inline_seen[64] = {0};
    uint8_t *seen = length <= sizeof(inline_seen)
        ? inline_seen : calloc(length, sizeof(*seen));
    if (!seen)
        return false;
    bool complete = true;
    for (size_t i = 0u; i < length; i++) {
        size_t selected = permutation[i];
        if (selected >= length || seen[selected]) {
            complete = false;
            break;
        }
        seen[selected] = 1u;
    }
    if (seen != inline_seen)
        free(seen);
    return complete;
}

CettaControllerRankingDecision cetta_controller_rank_complete(
        const CettaControllerBatchRanker *ranker,
        const CettaControllerCandidateView *candidates,
        size_t length,
        size_t *permutation,
        CettaControllerRankingReceipt *receipt) {
    CettaControllerRankingDecision decision =
        CETTA_CONTROLLER_RANKING_IDENTITY_INVALID;
    if (receipt) {
        *receipt = (CettaControllerRankingReceipt){
            .decision = decision,
            .scorer_identity = ranker ? ranker->scorer_identity : 0u,
            .model_revision = ranker ? ranker->model_revision : 0u,
            .candidates = length,
        };
    }
    if (length != 0u && (!candidates || !permutation)) {
        return decision;
    }
    cetta_controller_identity_permutation(permutation, length);
    if (length == 0u) {
        decision = CETTA_CONTROLLER_RANKING_IDENTITY_DEFAULT;
        if (receipt)
            receipt->decision = decision;
        return decision;
    }
    for (size_t i = 0u; i < length; i++) {
        if (candidates[i].occurrence_id == 0u ||
            !candidates[i].continuation) {
            return decision;
        }
    }
    if (!ranker || !ranker->rank) {
        decision = CETTA_CONTROLLER_RANKING_IDENTITY_DEFAULT;
    } else {
        CettaControllerRankStatus status = ranker->rank(
            ranker->context, candidates, length, permutation);
        if (status == CETTA_CONTROLLER_RANK_READY &&
            cetta_controller_permutation_complete(
                permutation, length)) {
            decision = CETTA_CONTROLLER_RANKING_APPLIED;
        } else {
            cetta_controller_identity_permutation(permutation, length);
            decision = status == CETTA_CONTROLLER_RANK_DEFERRED
                ? CETTA_CONTROLLER_RANKING_IDENTITY_DEFERRED
                : CETTA_CONTROLLER_RANKING_IDENTITY_INVALID;
        }
    }
    if (receipt)
        receipt->decision = decision;
    return decision;
}

void cetta_continuation_batch_init(
    CettaContinuationBatch *frontier) {
    if (frontier)
        *frontier = (CettaContinuationBatch){0};
}

void cetta_continuation_batch_destroy(
    CettaContinuationBatch *frontier) {
    if (!frontier)
        return;
    for (size_t i = 0u; i < frontier->length; i++)
        cetta_owned_continuation_destroy(&frontier->items[i]);
    free(frontier->items);
    *frontier = (CettaContinuationBatch){0};
}

CettaContinuationStatus cetta_continuation_expand(
    CettaContinuationMachine machine,
    CettaContinuationBatch *frontier) {
    if (!machine.machine ||
        !cetta_continuation_provider_valid(machine.provider) ||
        !machine.provider->branching.expand || !frontier ||
        frontier->items || frontier->length != 0u) {
        return CETTA_CONTINUATION_UNSUPPORTED;
    }

    void **payloads = NULL;
    size_t length = 0u;
    CettaContinuationStatus status = machine.provider->branching.expand(
        machine.machine, &payloads, &length);
    if (status != CETTA_CONTINUATION_READY ||
        !payloads || length == 0u ||
        length > SIZE_MAX / sizeof(*frontier->items)) {
        if (payloads) {
            for (size_t i = 0u; i < length; i++) {
                if (payloads[i])
                    machine.provider->ownership.destroy(payloads[i]);
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
                machine.provider->ownership.destroy(payloads[j]);
        }
        free(payloads);
        return CETTA_CONTINUATION_UNSUPPORTED;
    }

    CettaOwnedContinuation *items =
        cetta_malloc(length * sizeof(*items));
    for (size_t i = 0u; i < length; i++) {
        items[i] = (CettaOwnedContinuation){
            .payload = payloads[i],
            .provider = machine.provider,
        };
    }
    free(payloads);
    frontier->items = items;
    frontier->length = length;
    return CETTA_CONTINUATION_READY;
}

void cetta_continuation_store_init(
    CettaContinuationStore *queue) {
    if (queue) {
        *queue = (CettaContinuationStore){0};
        queue->next_occurrence_id = 1u;
    }
}

void cetta_continuation_store_destroy(
    CettaContinuationStore *queue) {
    if (!queue)
        return;
    for (size_t i = queue->begin; i < queue->end; i++)
        cetta_owned_continuation_destroy(&queue->items[i]);
    free(queue->items);
    *queue = (CettaContinuationStore){0};
}

size_t cetta_continuation_store_length(
    const CettaContinuationStore *queue) {
    return queue && queue->end >= queue->begin
        ? queue->end - queue->begin : 0u;
}

const CettaOwnedContinuation *cetta_continuation_store_at(
        const CettaContinuationStore *queue, size_t index) {
    if (!queue || index >= cetta_continuation_store_length(queue))
        return NULL;
    return &queue->items[queue->begin + index];
}

static bool cetta_continuation_store_reserve(
    CettaContinuationStore *queue, size_t additional) {
    if (!queue || queue->begin > queue->end ||
        queue->end > queue->capacity ||
        (queue->capacity != 0u && !queue->items) ||
        additional > SIZE_MAX -
            cetta_continuation_store_length(queue)) {
        return false;
    }
    size_t length = cetta_continuation_store_length(queue);
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

bool cetta_continuation_store_append(
    CettaContinuationStore *queue,
    CettaOwnedContinuation *continuation) {
    if (!queue || !continuation || !continuation->payload ||
        !continuation->provider || continuation->occurrence_id != 0u ||
        queue->next_occurrence_id == 0u ||
        !cetta_continuation_store_reserve(queue, 1u)) {
        return false;
    }
    continuation->occurrence_id = queue->next_occurrence_id;
    queue->next_occurrence_id = queue->next_occurrence_id == UINT64_MAX
        ? 0u : queue->next_occurrence_id + 1u;
    queue->items[queue->end++] = *continuation;
    *continuation = (CettaOwnedContinuation){0};
    return true;
}

bool cetta_continuation_store_append_batch(
    CettaContinuationStore *queue,
    CettaContinuationBatch *frontier) {
    if (!queue || !frontier || !frontier->items ||
        frontier->length == 0u) {
        return false;
    }
    for (size_t i = 0u; i < frontier->length; i++) {
        if (!frontier->items[i].payload ||
            frontier->items[i].occurrence_id != 0u ||
            !cetta_continuation_provider_valid(
                frontier->items[i].provider)) {
            return false;
        }
    }
    if (queue->next_occurrence_id == 0u ||
        frontier->length - 1u >
            UINT64_MAX - queue->next_occurrence_id) {
        return false;
    }
    if (!cetta_continuation_store_reserve(
            queue, frontier->length)) {
        return false;
    }
    for (size_t i = 0u; i < frontier->length; i++) {
        frontier->items[i].occurrence_id =
            queue->next_occurrence_id + (uint64_t)i;
    }
    memcpy(&queue->items[queue->end], frontier->items,
           frontier->length * sizeof(*frontier->items));
    queue->end += frontier->length;
    queue->next_occurrence_id =
        frontier->length - 1u ==
                UINT64_MAX - queue->next_occurrence_id
            ? 0u
            : queue->next_occurrence_id +
                (uint64_t)frontier->length;
    free(frontier->items);
    *frontier = (CettaContinuationBatch){0};
    return true;
}

bool cetta_continuation_store_take(
    CettaContinuationStore *queue, size_t index,
    CettaOwnedContinuation *continuation) {
    if (!queue || !continuation || continuation->payload ||
        continuation->provider || continuation->occurrence_id != 0u ||
        continuation->parent_occurrence_id != 0u ||
        index >= cetta_continuation_store_length(queue)) {
        return false;
    }
    size_t selected = queue->begin + index;
    *continuation = queue->items[selected];
    if (selected == queue->begin) {
        queue->items[queue->begin++] = (CettaOwnedContinuation){0};
    } else {
        size_t remaining = queue->end - selected - 1u;
        if (remaining != 0u) {
            memmove(
                &queue->items[selected], &queue->items[selected + 1u],
                remaining * sizeof(*queue->items));
        }
        queue->items[--queue->end] = (CettaOwnedContinuation){0};
    }
    if (queue->begin == queue->end)
        queue->begin = queue->end = 0u;
    return true;
}

bool cetta_continuation_store_storage(
    const CettaContinuationStore *queue,
    size_t *shared_bytes,
    size_t *exclusive_bytes) {
    if (!queue || !shared_bytes || !exclusive_bytes)
        return false;
    size_t shared = 0u;
    size_t exclusive = 0u;
    for (size_t i = queue->begin; i < queue->end; i++) {
        CettaContinuationStorage item = {0};
        if (!cetta_owned_continuation_storage(
                &queue->items[i], &item) ||
            item.exclusive_bytes > SIZE_MAX - exclusive) {
            return false;
        }
        exclusive += item.exclusive_bytes;
        bool first_shared_identity = item.shared_bytes != 0u;
        for (size_t prior = queue->begin;
             first_shared_identity && prior < i; prior++) {
            CettaContinuationStorage earlier = {0};
            if (!cetta_owned_continuation_storage(
                    &queue->items[prior], &earlier)) {
                return false;
            }
            if (earlier.shared_identity == item.shared_identity)
                first_shared_identity = false;
        }
        if (first_shared_identity) {
            if (item.shared_bytes > SIZE_MAX - shared)
                return false;
            shared += item.shared_bytes;
        }
    }
    *shared_bytes = shared;
    *exclusive_bytes = exclusive;
    return true;
}

bool cetta_continuation_store_component_storage(
        const CettaContinuationStore *store,
        CettaContinuationComponent component,
        size_t *shared_bytes,
        size_t *exclusive_bytes) {
    if (!store || !cetta_continuation_component_valid(component) ||
        !shared_bytes || !exclusive_bytes) {
        return false;
    }
    size_t shared = 0u;
    size_t exclusive = 0u;
    for (size_t i = store->begin; i < store->end; i++) {
        CettaContinuationStorage item = {0};
        if (!cetta_owned_continuation_component_storage(
                &store->items[i], component, &item) ||
            item.exclusive_bytes > SIZE_MAX - exclusive) {
            return false;
        }
        exclusive += item.exclusive_bytes;
        bool first_shared_identity = item.shared_bytes != 0u;
        for (size_t prior = store->begin;
             first_shared_identity && prior < i; prior++) {
            CettaContinuationStorage earlier = {0};
            if (!cetta_owned_continuation_component_storage(
                    &store->items[prior], component, &earlier)) {
                return false;
            }
            if (earlier.shared_identity == item.shared_identity)
                first_shared_identity = false;
        }
        if (first_shared_identity) {
            if (item.shared_bytes > SIZE_MAX - shared)
                return false;
            shared += item.shared_bytes;
        }
    }
    *shared_bytes = shared;
    *exclusive_bytes = exclusive;
    return true;
}

bool cetta_continuation_hub_init(
        CettaContinuationHub *hub,
        const CettaSelectionAutomaton *schedule,
        CettaSelectionDuty selection_duty,
        const CettaControlPlan *plan) {
    if (!hub || !schedule ||
        !cetta_selection_automaton_valid(schedule) ||
        (selection_duty != CETTA_SELECTION_DUTY_NONE &&
         selection_duty !=
             CETTA_SELECTION_DUTY_RECURRENT_OLDEST) ||
        (selection_duty ==
             CETTA_SELECTION_DUTY_RECURRENT_OLDEST &&
         !cetta_selection_automaton_has_recurrent_oldest(
             schedule)) ||
        !cetta_control_plan_valid(plan) ||
        plan->activation != CETTA_CONTROL_ACTIVATE_CONTROLLED) {
        return false;
    }
    *hub = (CettaContinuationHub){
        .schedule = *schedule,
        .selection_duty = selection_duty,
        .plan = *plan,
        .initialized = true,
    };
    hub->schedule.state = 0u;
    cetta_continuation_store_init(&hub->store);
    return true;
}

void cetta_continuation_hub_destroy(CettaContinuationHub *hub) {
    if (!hub)
        return;
    cetta_continuation_store_destroy(&hub->store);
    *hub = (CettaContinuationHub){0};
}

size_t cetta_continuation_hub_length(
        const CettaContinuationHub *hub) {
    return hub && hub->initialized
        ? cetta_continuation_store_length(&hub->store) : 0u;
}

const CettaOwnedContinuation *cetta_continuation_hub_at(
        const CettaContinuationHub *hub, size_t index) {
    return hub && hub->initialized
        ? cetta_continuation_store_at(&hub->store, index) : NULL;
}

bool cetta_continuation_hub_select(
        CettaContinuationHub *hub,
        CettaSelectionLane *lane,
        size_t *index) {
    if (!hub || !hub->initialized || !lane || !index ||
        hub->schedule.state >= hub->schedule.state_count) {
        return false;
    }
    *lane = (CettaSelectionLane)
        hub->schedule.lane[hub->schedule.state];
    return cetta_selection_automaton_select(
        &hub->schedule, cetta_continuation_hub_length(hub), index);
}

bool cetta_continuation_hub_switch_schedule(
        CettaContinuationHub *hub,
        const CettaSelectionAutomaton *schedule) {
    if (!hub || !hub->initialized || !schedule ||
        !cetta_selection_automaton_valid(schedule) ||
        (hub->selection_duty ==
             CETTA_SELECTION_DUTY_RECURRENT_OLDEST &&
         !cetta_selection_automaton_has_recurrent_oldest(
             schedule))) {
        return false;
    }
    hub->schedule = *schedule;
    hub->schedule.state = 0u;
    return true;
}

bool cetta_continuation_hub_append(
        CettaContinuationHub *hub,
        CettaOwnedContinuation *continuation) {
    return hub && hub->initialized &&
        cetta_continuation_store_append(&hub->store, continuation);
}

bool cetta_continuation_hub_append_batch(
        CettaContinuationHub *hub,
        CettaContinuationBatch *batch) {
    return hub && hub->initialized &&
        cetta_continuation_store_append_batch(&hub->store, batch);
}

bool cetta_continuation_hub_take(
        CettaContinuationHub *hub, size_t index,
        CettaOwnedContinuation *continuation) {
    return hub && hub->initialized &&
        cetta_continuation_store_take(
            &hub->store, index, continuation);
}

bool cetta_continuation_hub_storage(
        const CettaContinuationHub *hub,
        size_t *shared_bytes,
        size_t *exclusive_bytes) {
    return hub && hub->initialized &&
        cetta_continuation_store_storage(
            &hub->store, shared_bytes, exclusive_bytes);
}

bool cetta_continuation_hub_component_storage(
        const CettaContinuationHub *hub,
        CettaContinuationComponent component,
        size_t *shared_bytes,
        size_t *exclusive_bytes) {
    return hub && hub->initialized &&
        cetta_continuation_store_component_storage(
            &hub->store, component, shared_bytes, exclusive_bytes);
}

size_t cetta_continuation_hub_control_capacity_bytes(
        const CettaContinuationHub *hub) {
    if (!hub || !hub->initialized ||
        hub->store.capacity >
            SIZE_MAX / sizeof(CettaOwnedContinuation)) {
        return 0u;
    }
    return hub->store.capacity * sizeof(CettaOwnedContinuation);
}

static void cetta_continuation_replacement_payloads_destroy(
        const CettaContinuationProvider *provider,
        void **payloads, size_t length) {
    if (!payloads)
        return;
    if (cetta_continuation_provider_valid(provider)) {
        for (size_t i = 0u; i < length; i++) {
            if (payloads[i])
                provider->ownership.destroy(payloads[i]);
        }
    }
    free(payloads);
}

CettaContinuationStatus cetta_continuation_hub_reclaim(
        CettaContinuationHub *hub,
        CettaContinuationReclamationReceipt *receipt) {
    if (receipt)
        *receipt = (CettaContinuationReclamationReceipt){0};
    if (!hub || !hub->initialized)
        return CETTA_CONTINUATION_UNSUPPORTED;
    size_t length = cetta_continuation_hub_length(hub);
    if (length == 0u || length > SIZE_MAX / sizeof(void *))
        return CETTA_CONTINUATION_DEFERRED;
    CettaOwnedContinuation *first = &hub->store.items[hub->store.begin];
    const CettaContinuationProvider *provider = first->provider;
    if (!first->payload || !cetta_continuation_provider_valid(provider) ||
        !provider->maintenance.reclaim) {
        return CETTA_CONTINUATION_DEFERRED;
    }
    if (provider->maintenance.reclaim_due &&
        !provider->maintenance.reclaim_due(first->payload)) {
        return CETTA_CONTINUATION_DEFERRED;
    }
    const void **sources = malloc(length * sizeof(*sources));
    if (!sources)
        return CETTA_CONTINUATION_CAPACITY;
    for (size_t i = 0u; i < length; i++) {
        CettaOwnedContinuation *item =
            &hub->store.items[hub->store.begin + i];
        if (!item->payload || item->provider != provider) {
            free(sources);
            return CETTA_CONTINUATION_UNSUPPORTED;
        }
        sources[i] = item->payload;
    }

    void **replacements = NULL;
    CettaContinuationReclamationReceipt prepared = {0};
    CettaContinuationStatus status = provider->maintenance.reclaim(
        sources, length, &replacements, &prepared);
    free(sources);
    if (status != CETTA_CONTINUATION_READY) {
        cetta_continuation_replacement_payloads_destroy(
            provider, replacements, length);
        return status;
    }
    if (!replacements || prepared.live_occurrences != length) {
        cetta_continuation_replacement_payloads_destroy(
            provider, replacements, length);
        return CETTA_CONTINUATION_UNSUPPORTED;
    }
    for (size_t i = 0u; i < length; i++) {
        CettaOwnedContinuation prepared_item = {
            .payload = replacements[i],
            .provider = provider,
        };
        CettaContinuationStorage prepared_storage = {0};
        if (cetta_owned_continuation_storage(
                &prepared_item, &prepared_storage)) {
            continue;
        }
        cetta_continuation_replacement_payloads_destroy(
            provider, replacements, length);
        return CETTA_CONTINUATION_UNSUPPORTED;
    }

    for (size_t i = 0u; i < length; i++) {
        CettaOwnedContinuation *item =
            &hub->store.items[hub->store.begin + i];
        provider->ownership.destroy(item->payload);
        item->payload = replacements[i];
    }
    free(replacements);
    if (receipt)
        *receipt = prepared;
    return CETTA_CONTINUATION_READY;
}

bool cetta_continuation_hub_observe(
        CettaContinuationHub *hub, uint64_t occurrences) {
    if (!hub || !hub->initialized)
        return false;
    hub->observed_occurrences = occurrences >
            UINT64_MAX - hub->observed_occurrences
        ? UINT64_MAX
        : hub->observed_occurrences + occurrences;
    return cetta_control_plan_observation_satisfied(
        &hub->plan, hub->observed_occurrences);
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

bool search_context_init_bindings_only(
        SearchContext *ctx, const Bindings *base) {
    if (!ctx)
        return false;
    ctx->scratch_arena = NULL;
    ctx->owns_scratch_arena = false;
    ctx->owned_scratch_arena = (Arena){0};
    return bindings_builder_init(&ctx->bindings, base);
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

ChoicePoint search_context_save(SearchContext *ctx) {
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
