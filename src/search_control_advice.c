#define _POSIX_C_SOURCE 200809L

#include "search_control_advice.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool cetta_atom_homeomorphically_embedded_bounded(
        Atom *ancestor, Atom *descendant,
        size_t *work_remaining, size_t depth_remaining) {
    if (!ancestor || !descendant || !work_remaining ||
        *work_remaining == 0u || depth_remaining == 0u) {
        return false;
    }
    (*work_remaining)--;
    if (descendant->kind == ATOM_EXPR) {
        for (CettaExprLen i = 0; i < descendant->expr.len; i++) {
            if (cetta_atom_homeomorphically_embedded_bounded(
                    ancestor, descendant->expr.elems[i],
                    work_remaining, depth_remaining - 1u)) {
                return true;
            }
            if (*work_remaining == 0u)
                return false;
        }
    }
    if (ancestor->kind != descendant->kind)
        return false;
    switch (ancestor->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_SYMBOL:
    case ATOM_GROUNDED:
        return atom_eq(ancestor, descendant);
    case ATOM_EXPR:
        if (ancestor->expr.len != descendant->expr.len)
            return false;
        for (CettaExprLen i = 0; i < ancestor->expr.len; i++) {
            if (!cetta_atom_homeomorphically_embedded_bounded(
                    ancestor->expr.elems[i],
                    descendant->expr.elems[i], work_remaining,
                    depth_remaining - 1u)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool cetta_atom_homeomorphically_embedded(
    Atom *ancestor, Atom *descendant) {
    size_t work_remaining = CETTA_WHISTLE_EMBEDDING_WORK_LIMIT;
    return cetta_atom_homeomorphically_embedded_bounded(
        ancestor, descendant, &work_remaining,
        CETTA_WHISTLE_EMBEDDING_DEPTH_LIMIT);
}

bool cetta_whistle_init(CettaWhistle *whistle) {
    if (!whistle)
        return false;
    memset(whistle, 0, sizeof(*whistle));
    if (pthread_mutex_init(&whistle->mutex, NULL) != 0) {
        memset(whistle, 0, sizeof(*whistle));
        return false;
    }
    whistle->initialized = true;
    return true;
}

void cetta_whistle_destroy(CettaWhistle *whistle) {
    if (!whistle || !whistle->initialized)
        return;
    pthread_mutex_destroy(&whistle->mutex);
    for (size_t i = 0u; i < CETTA_WHISTLE_ANCESTOR_CAPACITY; i++) {
        if (whistle->term_arenas[i]) {
            arena_free(whistle->term_arenas[i]);
            free(whistle->term_arenas[i]);
        }
    }
    memset(whistle, 0, sizeof(*whistle));
}

static bool cetta_whistle_find_occurrence_locked(
        CettaWhistle *whistle, uint64_t occurrence, size_t *slot_out) {
    for (size_t i = 0; i < whistle->term_length; i++) {
        size_t slot = (whistle->term_begin + i) %
            CETTA_WHISTLE_ANCESTOR_CAPACITY;
        if (whistle->term_occurrences[slot] == occurrence) {
            if (slot_out)
                *slot_out = slot;
            return true;
        }
    }
    return false;
}

static void cetta_whistle_demote_locked(
        CettaWhistle *whistle, uint64_t occurrence) {
    for (size_t i = 0; i < whistle->demoted_length; i++) {
        if (whistle->demoted[i] == occurrence)
            return;
    }
    if (whistle->demoted_length < CETTA_WHISTLE_DEMOTION_CAPACITY)
        whistle->demoted[whistle->demoted_length++] = occurrence;
}

bool cetta_whistle_observe(
    CettaWhistle *whistle, Atom *term, uint64_t occurrence,
    uint64_t parent_occurrence) {
    if (!whistle || !whistle->initialized || !term || occurrence == 0u)
        return false;
    Arena *owned_arena = malloc(sizeof(*owned_arena));
    if (!owned_arena)
        return false;
    arena_init(owned_arena);
    Atom *owned = atom_deep_copy(owned_arena, term);
    pthread_mutex_lock(&whistle->mutex);
    if (cetta_whistle_find_occurrence_locked(
            whistle, occurrence, NULL)) {
        pthread_mutex_unlock(&whistle->mutex);
        arena_free(owned_arena);
        free(owned_arena);
        return false;
    }
    bool whistled = false;
    uint64_t ancestor_occurrence = parent_occurrence;
    for (size_t depth = 0u;
         ancestor_occurrence != 0u &&
         depth < CETTA_WHISTLE_ANCESTOR_CAPACITY;
         depth++) {
        size_t ancestor_slot = 0u;
        if (!cetta_whistle_find_occurrence_locked(
                whistle, ancestor_occurrence, &ancestor_slot)) {
            break;
        }
        if (cetta_atom_homeomorphically_embedded(
                whistle->terms[ancestor_slot], term)) {
            whistled = true;
            break;
        }
        ancestor_occurrence =
            whistle->term_parents[ancestor_slot];
    }
    if (whistled) {
        whistle->whistles++;
        cetta_whistle_demote_locked(whistle, occurrence);
    }
    size_t slot;
    if (whistle->term_length < CETTA_WHISTLE_ANCESTOR_CAPACITY) {
        slot = (whistle->term_begin + whistle->term_length) %
            CETTA_WHISTLE_ANCESTOR_CAPACITY;
        whistle->term_length++;
    } else {
        slot = whistle->term_begin;
        whistle->term_begin = (whistle->term_begin + 1u) %
            CETTA_WHISTLE_ANCESTOR_CAPACITY;
        arena_free(whistle->term_arenas[slot]);
        free(whistle->term_arenas[slot]);
    }
    whistle->term_arenas[slot] = owned_arena;
    whistle->terms[slot] = owned;
    whistle->term_occurrences[slot] = occurrence;
    whistle->term_parents[slot] = parent_occurrence;
    pthread_mutex_unlock(&whistle->mutex);
    return whistled;
}

bool cetta_whistle_demoted(
    CettaWhistle *whistle, uint64_t occurrence) {
    if (!whistle || !whistle->initialized || occurrence == 0u)
        return false;
    pthread_mutex_lock(&whistle->mutex);
    bool demoted = false;
    for (size_t i = 0; i < whistle->demoted_length; i++) {
        if (whistle->demoted[i] == occurrence) {
            demoted = true;
            break;
        }
    }
    pthread_mutex_unlock(&whistle->mutex);
    return demoted;
}

uint64_t cetta_whistle_count(CettaWhistle *whistle) {
    if (!whistle || !whistle->initialized)
        return 0u;
    pthread_mutex_lock(&whistle->mutex);
    uint64_t count = whistle->whistles;
    pthread_mutex_unlock(&whistle->mutex);
    return count;
}

static void cetta_whistle_observation_destroy(
        CettaWhistleObservation *observation) {
    if (!observation || !observation->arena)
        return;
    arena_free(observation->arena);
    free(observation->arena);
    *observation = (CettaWhistleObservation){0};
}

static void *cetta_whistle_monitor_worker(void *opaque) {
    CettaWhistleMonitor *monitor = opaque;
    pthread_mutex_lock(&monitor->mutex);
    for (;;) {
        while (monitor->ring_length == 0u && monitor->running)
            pthread_cond_wait(&monitor->ready, &monitor->mutex);
        if (monitor->ring_length == 0u && !monitor->running)
            break;
        size_t slot = monitor->ring_begin;
        CettaWhistleObservation observation = monitor->ring[slot];
        monitor->ring[slot] = (CettaWhistleObservation){0};
        monitor->ring_begin = (monitor->ring_begin + 1u) %
            CETTA_WHISTLE_MONITOR_RING_CAPACITY;
        monitor->ring_length--;
        pthread_mutex_unlock(&monitor->mutex);
        cetta_whistle_observe(
            monitor->whistle, observation.term,
            observation.occurrence, observation.parent_occurrence);
        cetta_whistle_observation_destroy(&observation);
        pthread_mutex_lock(&monitor->mutex);
    }
    pthread_mutex_unlock(&monitor->mutex);
    return NULL;
}

bool cetta_whistle_monitor_start(
    CettaWhistleMonitor *monitor, CettaWhistle *whistle) {
    if (!monitor || !whistle || !whistle->initialized)
        return false;
    memset(monitor, 0, sizeof(*monitor));
    monitor->whistle = whistle;
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0)
        return false;
    if (pthread_cond_init(&monitor->ready, NULL) != 0) {
        pthread_mutex_destroy(&monitor->mutex);
        memset(monitor, 0, sizeof(*monitor));
        return false;
    }
    monitor->running = true;
    monitor->initialized = true;
    if (pthread_create(&monitor->thread, NULL,
                       cetta_whistle_monitor_worker, monitor) != 0) {
        pthread_cond_destroy(&monitor->ready);
        pthread_mutex_destroy(&monitor->mutex);
        memset(monitor, 0, sizeof(*monitor));
        return false;
    }
    return true;
}

bool cetta_whistle_monitor_submit(
    CettaWhistleMonitor *monitor, Atom *term, uint64_t occurrence,
    uint64_t parent_occurrence) {
    if (!monitor || !monitor->initialized || !term || occurrence == 0u)
        return false;
    Arena *owned_arena = malloc(sizeof(*owned_arena));
    if (!owned_arena)
        return false;
    arena_init(owned_arena);
    Atom *owned_term = atom_deep_copy(owned_arena, term);
    pthread_mutex_lock(&monitor->mutex);
    if (!monitor->running ||
        monitor->ring_length >= CETTA_WHISTLE_MONITOR_RING_CAPACITY) {
        pthread_mutex_unlock(&monitor->mutex);
        arena_free(owned_arena);
        free(owned_arena);
        return false;
    }
    size_t slot = (monitor->ring_begin + monitor->ring_length) %
        CETTA_WHISTLE_MONITOR_RING_CAPACITY;
    monitor->ring[slot] = (CettaWhistleObservation){
        .arena = owned_arena,
        .term = owned_term,
        .occurrence = occurrence,
        .parent_occurrence = parent_occurrence,
    };
    monitor->ring_length++;
    pthread_cond_signal(&monitor->ready);
    pthread_mutex_unlock(&monitor->mutex);
    return true;
}

void cetta_whistle_monitor_stop(CettaWhistleMonitor *monitor) {
    if (!monitor || !monitor->initialized)
        return;
    pthread_mutex_lock(&monitor->mutex);
    monitor->running = false;
    pthread_cond_signal(&monitor->ready);
    pthread_mutex_unlock(&monitor->mutex);
    pthread_join(monitor->thread, NULL);
    pthread_cond_destroy(&monitor->ready);
    pthread_mutex_destroy(&monitor->mutex);
    memset(monitor, 0, sizeof(*monitor));
}

static bool cetta_act_key_equal(
        const CettaActKey *left, const CettaActKey *right) {
    return left && right &&
        left->language_id == right->language_id &&
        left->profile_id == right->profile_id &&
        left->space_revision == right->space_revision &&
        left->program_hash == right->program_hash;
}

static bool cetta_act_profile_path(
        const char *directory, const CettaActKey *key,
        char *path, size_t capacity) {
    if (!directory || directory[0] == '\0' || !key || !path)
        return false;
    int written = snprintf(
        path, capacity,
        "%s/%" PRIu32 "-%" PRIu32 "-%" PRIu64 "-%08" PRIx32 ".act",
        directory, key->language_id, key->profile_id,
        key->space_revision, key->program_hash);
    return written > 0 && (size_t)written < capacity;
}

static bool cetta_act_outcome_valid(CettaActOutcome outcome) {
    return outcome >= CETTA_ACT_OUTCOME_COMPLETE &&
        outcome <= CETTA_ACT_OUTCOME_REFUSED;
}

static bool cetta_act_write_all(
    int descriptor, const uint8_t *bytes, size_t length);

bool cetta_act_profile_store(
    const char *directory, const CettaActRecord *record) {
    char path[4096];
    if (!record || !cetta_act_outcome_valid(record->outcome) ||
        !memchr(record->policy, '\0', sizeof(record->policy)) ||
        !cetta_act_profile_path(directory, &record->key,
                                path, sizeof(path))) {
        return false;
    }
    CettaSearchControllerPolicy parsed_policy;
    if (!cetta_search_controller_policy_parse(
            record->policy, &parsed_policy)) {
        return false;
    }
    (void)parsed_policy;
    char line[512];
    int length = snprintf(
        line, sizeof(line),
        "act-v1\t%" PRIu32 "\t%" PRIu32 "\t%" PRIu64
        "\t%" PRIu32 "\t%s\t%u\t%" PRIu64 "\t%" PRIu64
        "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
        record->key.language_id, record->key.profile_id,
        record->key.space_revision, record->key.program_hash,
        record->policy, (unsigned)record->outcome,
        record->transitions, record->answers, record->expansions,
        record->max_frontier, record->whistles);
    if (length <= 0 || (size_t)length >= sizeof(line))
        return false;
    char temporary[4096];
    int temporary_length = snprintf(
        temporary, sizeof(temporary), "%s/.act-XXXXXX", directory);
    if (temporary_length <= 0 ||
        (size_t)temporary_length >= sizeof(temporary)) {
        return false;
    }
    int descriptor = mkstemp(temporary);
    if (descriptor < 0)
        return false;
    bool stored = fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
        cetta_act_write_all(
            descriptor, (const uint8_t *)line, (size_t)length) &&
        fsync(descriptor) == 0;
    if (close(descriptor) != 0)
        stored = false;
    if (stored)
        stored = rename(temporary, path) == 0;
    if (!stored)
        unlink(temporary);
    return stored;
}

bool cetta_act_profile_lookup(
    const char *directory, const CettaActKey *key,
    CettaActRecord *record) {
    char path[4096];
    if (!record || !cetta_act_profile_path(
            directory, key, path, sizeof(path))) {
        return false;
    }
    FILE *file = fopen(path, "r");
    if (!file)
        return false;
    char line[512];
    bool one_line = fgets(line, sizeof(line), file) != NULL;
    bool no_extra = one_line && fgetc(file) == EOF;
    bool closed = fclose(file) == 0;
    if (!one_line || !no_extra || !closed)
        return false;
    CettaActRecord parsed = {0};
    char schema[16] = {0};
    unsigned outcome = 0u;
    int consumed = 0;
    int matched = sscanf(
        line,
        "%15[^\t]\t%" SCNu32 "\t%" SCNu32 "\t%" SCNu64
        "\t%" SCNu32 "\t%23[^\t]\t%u\t%" SCNu64
        "\t%" SCNu64 "\t%" SCNu64 "\t%" SCNu64 "\t%" SCNu64
        "%n",
        schema, &parsed.key.language_id, &parsed.key.profile_id,
        &parsed.key.space_revision, &parsed.key.program_hash,
        parsed.policy, &outcome, &parsed.transitions, &parsed.answers,
        &parsed.expansions, &parsed.max_frontier, &parsed.whistles,
        &consumed);
    parsed.outcome = (CettaActOutcome)outcome;
    CettaSearchControllerPolicy parsed_policy;
    if (matched != 12 || strcmp(schema, "act-v1") != 0 ||
        !cetta_act_key_equal(&parsed.key, key) ||
        !cetta_act_outcome_valid(parsed.outcome) ||
        consumed <= 0 ||
        (line[consumed] != '\0' &&
         !(line[consumed] == '\n' && line[consumed + 1] == '\0')) ||
        !cetta_search_controller_policy_parse(
            parsed.policy, &parsed_policy)) {
        return false;
    }
    (void)parsed_policy;
    *record = parsed;
    return true;
}

bool cetta_act_profile_choose(
    const char *directory, const CettaActKey *key,
    CettaSearchControllerPolicy *policy,
    CettaSelectionAutomaton *automaton) {
    CettaActRecord record;
    if (!policy || !automaton ||
        !cetta_act_profile_lookup(directory, key, &record)) {
        return false;
    }
    if (record.outcome == CETTA_ACT_OUTCOME_REFUSED)
        return false;
    if (record.answers > 0u) {
        CettaSearchControllerPolicy recorded;
        if (!cetta_search_controller_policy_parse(
                record.policy, &recorded) ||
            recorded == CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST ||
            !cetta_selection_automaton_parse(
                record.policy, automaton)) {
            return false;
        }
        *policy = recorded;
        return true;
    }
    if (record.outcome != CETTA_ACT_OUTCOME_INCOMPLETE)
        return false;
    *policy = CETTA_SEARCH_CONTROLLER_FIFO;
    return cetta_selection_automaton_fifo(automaton);
}

static bool cetta_act_compression_path(
        const char *directory, const CettaActKey *key,
        char *path, size_t capacity) {
    if (!directory || directory[0] == '\0' || !key || !path)
        return false;
    int written = snprintf(
        path, capacity,
        "%s/%" PRIu32 "-%" PRIu32 "-%" PRIu64
        "-%08" PRIx32 ".compression.act",
        directory, key->language_id, key->profile_id,
        key->space_revision, key->program_hash);
    return written > 0 && (size_t)written < capacity;
}

static uint64_t cetta_act_compression_digest_bytes(
        uint64_t hash, const void *bytes, size_t length) {
    const uint8_t *cursor = bytes;
    for (size_t i = 0u; i < length; i++) {
        hash ^= cursor[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t cetta_act_compression_digest_u64(
        uint64_t hash, uint64_t value) {
    uint8_t bytes[8];
    for (size_t i = 0u; i < sizeof(bytes); i++)
        bytes[i] = (uint8_t)(value >> (8u * i));
    return cetta_act_compression_digest_bytes(
        hash, bytes, sizeof(bytes));
}

static uint64_t cetta_act_compression_model_digest(
        const CettaActCompressionModel *model) {
    if (!model)
        return 0u;
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = cetta_act_compression_digest_u64(
        hash, model->key.language_id);
    hash = cetta_act_compression_digest_u64(
        hash, model->key.profile_id);
    hash = cetta_act_compression_digest_u64(
        hash, model->key.space_revision);
    hash = cetta_act_compression_digest_u64(
        hash, model->key.program_hash);
    hash = cetta_act_compression_digest_u64(
        hash, model->projection_identity);
    hash = cetta_act_compression_digest_u64(
        hash, model->revision);
    hash = cetta_act_compression_digest_u64(
        hash, model->observations);
    hash = cetta_act_compression_digest_u64(
        hash, model->dictionary_length);
    return cetta_act_compression_digest_bytes(
        hash, model->dictionary, model->dictionary_length);
}

static bool cetta_act_compression_model_valid(
        const CettaActCompressionModel *model) {
    return model && model->projection_identity != 0u &&
        model->revision != 0u && model->observations != 0u &&
        model->dictionary_length != 0u &&
        model->dictionary_length <=
            CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY &&
        model->digest == cetta_act_compression_model_digest(model);
}

bool cetta_act_compression_model_init(
        CettaActCompressionModel *model, const CettaActKey *key) {
    if (!model || !key)
        return false;
    *model = (CettaActCompressionModel){0};
    model->key = *key;
    return true;
}

bool cetta_act_compression_model_observe_success(
        CettaActCompressionModel *model,
        const CettaContinuationTrace *trace) {
    bool empty_model = model && model->projection_identity == 0u &&
        model->revision == 0u && model->observations == 0u &&
        model->digest == 0u && model->dictionary_length == 0u;
    if (!model || (!empty_model &&
                   !cetta_act_compression_model_valid(model)) ||
        !trace || !trace->bytes || trace->length == 0u ||
        trace->length > CETTA_CONTINUATION_TRACE_BYTE_LIMIT ||
        trace->projection_identity == 0u ||
        model->revision == UINT64_MAX ||
        model->observations == UINT64_MAX ||
        (model->projection_identity != 0u &&
         model->projection_identity != trace->projection_identity)) {
        return false;
    }
    if (model->projection_identity == 0u)
        model->projection_identity = trace->projection_identity;
    size_t incoming = trace->length;
    if (incoming >= CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY) {
        memcpy(
            model->dictionary,
            &trace->bytes[incoming -
                CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY],
            CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY);
        model->dictionary_length =
            CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY;
    } else {
        size_t retained = model->dictionary_length;
        size_t available =
            CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY - incoming;
        if (retained > available)
            retained = available;
        if (retained != 0u) {
            memmove(
                model->dictionary,
                &model->dictionary[model->dictionary_length - retained],
                retained);
        }
        memcpy(&model->dictionary[retained], trace->bytes, incoming);
        model->dictionary_length = retained + incoming;
    }
    model->revision++;
    model->observations++;
    model->digest = cetta_act_compression_model_digest(model);
    return true;
}

void cetta_act_compression_lineage_init(
    CettaActCompressionLineage *lineage) {
    if (lineage)
        *lineage = (CettaActCompressionLineage){0};
}

void cetta_act_compression_lineage_destroy(
        CettaActCompressionLineage *lineage) {
    if (!lineage)
        return;
    for (size_t i = 0u; i < CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY;
         i++) {
        cetta_continuation_trace_destroy(&lineage->entries[i].trace);
    }
    *lineage = (CettaActCompressionLineage){0};
}

size_t cetta_act_compression_lineage_resident_bytes(
        const CettaActCompressionLineage *lineage) {
    if (!lineage)
        return 0u;
    size_t bytes = sizeof(*lineage);
    for (size_t offset = 0u; offset < lineage->length; offset++) {
        size_t index = (lineage->begin + offset) %
            CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY;
        size_t trace_length = lineage->entries[index].trace.length;
        if (trace_length > SIZE_MAX - bytes)
            return SIZE_MAX;
        bytes += trace_length;
    }
    return bytes;
}

static bool cetta_act_compression_lineage_find(
        const CettaActCompressionLineage *lineage,
        uint64_t occurrence_id, size_t *slot) {
    if (!lineage || occurrence_id == 0u)
        return false;
    for (size_t offset = 0u; offset < lineage->length; offset++) {
        size_t reverse = lineage->length - offset - 1u;
        size_t index = (lineage->begin + reverse) %
            CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY;
        if (lineage->entries[index].occurrence_id == occurrence_id) {
            if (slot)
                *slot = index;
            return true;
        }
    }
    return false;
}

bool cetta_act_compression_lineage_record(
        CettaActCompressionLineage *lineage,
        uint64_t occurrence_id,
        uint64_t parent_occurrence_id,
        CettaContinuationTrace *trace) {
    if (!lineage || occurrence_id == 0u ||
        parent_occurrence_id >= occurrence_id || !trace ||
        !trace->bytes || trace->length == 0u ||
        trace->length > CETTA_CONTINUATION_TRACE_BYTE_LIMIT ||
        trace->projection_identity == 0u ||
        cetta_act_compression_lineage_find(
            lineage, occurrence_id, NULL)) {
        return false;
    }
    size_t slot;
    if (lineage->length < CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY) {
        slot = (lineage->begin + lineage->length) %
            CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY;
        lineage->length++;
    } else {
        slot = lineage->begin;
        lineage->begin = (lineage->begin + 1u) %
            CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY;
        cetta_continuation_trace_destroy(
            &lineage->entries[slot].trace);
    }
    lineage->entries[slot] = (CettaActCompressionLineageEntry){
        .occurrence_id = occurrence_id,
        .parent_occurrence_id = parent_occurrence_id,
        .trace = *trace,
    };
    *trace = (CettaContinuationTrace){0};
    return true;
}

bool cetta_act_compression_lineage_credit_answer(
        CettaActCompressionLineage *lineage,
        uint64_t occurrence_id,
        CettaActCompressionModel *model,
        size_t *updates) {
    if (updates)
        *updates = 0u;
    if (!lineage || !model || occurrence_id == 0u)
        return false;
    size_t path[CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY];
    size_t path_length = 0u;
    uint64_t cursor = occurrence_id;
    while (cursor != 0u &&
           path_length < CETTA_ACT_COMPRESSION_LINEAGE_CAPACITY) {
        size_t slot = 0u;
        if (!cetta_act_compression_lineage_find(
                lineage, cursor, &slot)) {
            break;
        }
        for (size_t prior = 0u; prior < path_length; prior++) {
            if (path[prior] == slot)
                return false;
        }
        const CettaActCompressionLineageEntry *entry =
            &lineage->entries[slot];
        if (!entry->trace.bytes || entry->trace.length == 0u ||
            entry->trace.projection_identity == 0u ||
            (model->projection_identity != 0u &&
             model->projection_identity !=
                 entry->trace.projection_identity)) {
            return false;
        }
        path[path_length++] = slot;
        cursor = entry->parent_occurrence_id;
    }
    if (cursor != 0u || path_length == 0u ||
        path_length > UINT64_MAX - model->revision ||
        path_length > UINT64_MAX - model->observations) {
        return false;
    }
    size_t learned = 0u;
    for (size_t reverse = path_length; reverse != 0u; reverse--) {
        const CettaActCompressionLineageEntry *entry =
            &lineage->entries[path[reverse - 1u]];
        if (!cetta_act_compression_model_observe_success(
                model, &entry->trace)) {
            return false;
        }
        learned++;
    }
    if (updates)
        *updates = learned;
    return true;
}

static bool cetta_act_write_all(
        int descriptor, const uint8_t *bytes, size_t length) {
    size_t written = 0u;
    while (written < length) {
        ssize_t count = write(
            descriptor, &bytes[written], length - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        written += (size_t)count;
    }
    return true;
}

bool cetta_act_compression_model_store(
        const char *directory,
        const CettaActCompressionModel *model) {
    char path[4096];
    if (!cetta_act_compression_model_valid(model) ||
        !cetta_act_compression_path(
            directory, &model->key, path, sizeof(path))) {
        return false;
    }
    size_t capacity = 512u + 2u * model->dictionary_length;
    char *line = malloc(capacity);
    if (!line)
        return false;
    int header = snprintf(
        line, capacity,
        "act-compression-v1\t%" PRIu32 "\t%" PRIu32
        "\t%" PRIu64 "\t%" PRIu32 "\t%016" PRIx64
        "\t%" PRIu64 "\t%" PRIu64 "\t%016" PRIx64
        "\t%zu\t",
        model->key.language_id, model->key.profile_id,
        model->key.space_revision, model->key.program_hash,
        model->projection_identity, model->revision,
        model->observations, model->digest,
        model->dictionary_length);
    if (header <= 0 || (size_t)header >= capacity) {
        free(line);
        return false;
    }
    static const char hex[] = "0123456789abcdef";
    size_t length = (size_t)header;
    for (size_t i = 0u; i < model->dictionary_length; i++) {
        line[length++] = hex[model->dictionary[i] >> 4u];
        line[length++] = hex[model->dictionary[i] & 0x0fu];
    }
    line[length++] = '\n';
    char temporary[4096];
    int temporary_length = snprintf(
        temporary, sizeof(temporary),
        "%s/.compression-act-XXXXXX", directory);
    if (temporary_length <= 0 ||
        (size_t)temporary_length >= sizeof(temporary)) {
        free(line);
        return false;
    }
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        free(line);
        return false;
    }
    bool stored = fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 &&
        cetta_act_write_all(
            descriptor, (const uint8_t *)line, length) &&
        fsync(descriptor) == 0;
    if (close(descriptor) != 0)
        stored = false;
    if (stored)
        stored = rename(temporary, path) == 0;
    if (!stored)
        unlink(temporary);
    free(line);
    return stored;
}

static int cetta_act_hex_value(unsigned char value) {
    if (value >= '0' && value <= '9')
        return (int)(value - '0');
    if (value >= 'a' && value <= 'f')
        return (int)(value - 'a') + 10;
    if (value >= 'A' && value <= 'F')
        return (int)(value - 'A') + 10;
    return -1;
}

bool cetta_act_compression_model_lookup(
        const char *directory, const CettaActKey *key,
        CettaActCompressionModel *model) {
    char path[4096];
    if (!model || !cetta_act_compression_path(
            directory, key, path, sizeof(path))) {
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file)
        return false;
    size_t capacity =
        512u + 2u * CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY;
    char *line = malloc(capacity + 1u);
    if (!line) {
        fclose(file);
        return false;
    }
    size_t length = fread(line, 1u, capacity + 1u, file);
    bool read_ok = !ferror(file) && length <= capacity;
    if (read_ok)
        read_ok = fgetc(file) == EOF && !ferror(file);
    bool closed = fclose(file) == 0;
    read_ok = read_ok && closed;
    if (!read_ok) {
        free(line);
        return false;
    }
    line[length] = '\0';
    CettaActCompressionModel parsed = {0};
    char schema[32] = {0};
    size_t dictionary_length = 0u;
    int consumed = 0;
    int matched = sscanf(
        line,
        "%31[^\t]\t%" SCNu32 "\t%" SCNu32 "\t%" SCNu64
        "\t%" SCNu32 "\t%" SCNx64 "\t%" SCNu64
        "\t%" SCNu64 "\t%" SCNx64 "\t%zu\t%n",
        schema, &parsed.key.language_id, &parsed.key.profile_id,
        &parsed.key.space_revision, &parsed.key.program_hash,
        &parsed.projection_identity, &parsed.revision,
        &parsed.observations, &parsed.digest,
        &dictionary_length, &consumed);
    bool ok = matched == 10 &&
        strcmp(schema, "act-compression-v1") == 0 &&
        cetta_act_key_equal(&parsed.key, key) && consumed > 0 &&
        dictionary_length != 0u &&
        dictionary_length <=
            CETTA_ACT_COMPRESSION_DICTIONARY_CAPACITY &&
        (size_t)consumed + 2u * dictionary_length + 1u == length &&
        line[length - 1u] == '\n';
    for (size_t i = 0u; ok && i < dictionary_length; i++) {
        int high = cetta_act_hex_value(
            (unsigned char)line[consumed + 2u * i]);
        int low = cetta_act_hex_value(
            (unsigned char)line[consumed + 2u * i + 1u]);
        if (high < 0 || low < 0) {
            ok = false;
        } else {
            parsed.dictionary[i] = (uint8_t)((high << 4) | low);
        }
    }
    parsed.dictionary_length = dictionary_length;
    ok = ok && cetta_act_compression_model_valid(&parsed);
    free(line);
    if (!ok)
        return false;
    *model = parsed;
    return true;
}

#define CETTA_ACT_COMPRESSION_HASH_BUCKETS ((size_t)4096u)
#define CETTA_ACT_COMPRESSION_MIN_MATCH ((size_t)5u)
#define CETTA_ACT_COMPRESSION_MAX_MATCH ((size_t)255u)
#define CETTA_ACT_COMPRESSION_CHAIN_LIMIT ((size_t)64u)

typedef struct {
    const CettaActCompressionModel *model;
    int32_t *heads;
    int32_t *previous;
} CettaActCompressionPrepared;

static uint32_t cetta_act_compression_hash4(const uint8_t *bytes) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0u; i < 4u; i++) {
        hash ^= bytes[i];
        hash *= UINT32_C(16777619);
    }
    return hash &
        (uint32_t)(CETTA_ACT_COMPRESSION_HASH_BUCKETS - 1u);
}

static bool cetta_act_compression_prepare(
        const CettaActCompressionModel *model,
        CettaActCompressionPrepared *prepared) {
    if (!prepared || !cetta_act_compression_model_valid(model))
        return false;
    *prepared = (CettaActCompressionPrepared){.model = model};
    size_t slots = CETTA_ACT_COMPRESSION_HASH_BUCKETS +
        model->dictionary_length;
    if (slots > SIZE_MAX / sizeof(int32_t))
        return false;
    int32_t *storage = malloc(slots * sizeof(*storage));
    if (!storage)
        return false;
    prepared->heads = storage;
    prepared->previous =
        &storage[CETTA_ACT_COMPRESSION_HASH_BUCKETS];
    for (size_t i = 0u; i < CETTA_ACT_COMPRESSION_HASH_BUCKETS; i++)
        prepared->heads[i] = -1;
    for (size_t i = 0u; i < model->dictionary_length; i++)
        prepared->previous[i] = -1;
    for (size_t i = 0u; i + 4u <= model->dictionary_length; i++) {
        uint32_t hash = cetta_act_compression_hash4(
            &model->dictionary[i]);
        prepared->previous[i] = prepared->heads[hash];
        prepared->heads[hash] = (int32_t)i;
    }
    return true;
}

static void cetta_act_compression_prepared_destroy(
        CettaActCompressionPrepared *prepared) {
    if (!prepared)
        return;
    free(prepared->heads);
    *prepared = (CettaActCompressionPrepared){0};
}

static size_t cetta_act_compression_longest_match(
        const CettaActCompressionPrepared *prepared,
        const uint8_t *bytes, size_t remaining) {
    if (!prepared || !prepared->model || !bytes || remaining < 4u)
        return 0u;
    uint32_t hash = cetta_act_compression_hash4(bytes);
    int32_t position = prepared->heads[hash];
    size_t best = 0u;
    for (size_t visited = 0u;
         position >= 0 && visited < CETTA_ACT_COMPRESSION_CHAIN_LIMIT;
         visited++) {
        size_t start = (size_t)position;
        size_t available = prepared->model->dictionary_length - start;
        size_t limit = remaining < available ? remaining : available;
        if (limit > CETTA_ACT_COMPRESSION_MAX_MATCH)
            limit = CETTA_ACT_COMPRESSION_MAX_MATCH;
        size_t length = 0u;
        while (length < limit &&
               bytes[length] ==
                   prepared->model->dictionary[start + length]) {
            length++;
        }
        if (length > best)
            best = length;
        if (best == CETTA_ACT_COMPRESSION_MAX_MATCH ||
            best == remaining) {
            break;
        }
        position = prepared->previous[start];
    }
    return best;
}

static bool cetta_act_compression_add_literal_cost(
        size_t literals, size_t *encoded) {
    if (!encoded)
        return false;
    while (literals != 0u) {
        size_t run = literals > 255u ? 255u : literals;
        if (*encoded > SIZE_MAX - run - 2u)
            return false;
        *encoded += run + 2u;
        literals -= run;
    }
    return true;
}

static bool cetta_act_compression_measure_prepared(
        const CettaActCompressionPrepared *prepared,
        const CettaContinuationTrace *trace,
        CettaActCompressionMeasurement *measurement) {
    if (!prepared || !prepared->model || !trace || !trace->bytes ||
        trace->length == 0u ||
        trace->length > CETTA_CONTINUATION_TRACE_BYTE_LIMIT ||
        trace->projection_identity !=
            prepared->model->projection_identity || !measurement ||
        trace->length > UINT64_MAX / 8u) {
        return false;
    }
    size_t encoded = 1u; /* conditional-dictionary format tag */
    size_t literals = 0u;
    size_t cursor = 0u;
    while (cursor < trace->length) {
        size_t match = cetta_act_compression_longest_match(
            prepared, &trace->bytes[cursor], trace->length - cursor);
        if (match >= CETTA_ACT_COMPRESSION_MIN_MATCH) {
            if (!cetta_act_compression_add_literal_cost(
                    literals, &encoded) ||
                encoded > SIZE_MAX - 4u) {
                return false;
            }
            literals = 0u;
            encoded += 4u; /* tag + dictionary offset + match length */
            cursor += match;
        } else {
            literals++;
            cursor++;
        }
    }
    if (!cetta_act_compression_add_literal_cost(literals, &encoded))
        return false;
    size_t residual = encoded < trace->length
        ? encoded : trace->length;
    *measurement = (CettaActCompressionMeasurement){
        .ordinary_bits = (uint64_t)trace->length * 8u,
        /* The checked model digest is the shared condition, not a
         * per-candidate feature payload. */
        .feature_bits = 0u,
        .residual_bits = (uint64_t)residual * 8u,
        .saved_bits = (uint64_t)(trace->length - residual) * 8u,
    };
    return true;
}

bool cetta_act_compression_measure(
        const CettaActCompressionModel *model,
        const CettaContinuationTrace *trace,
        CettaActCompressionMeasurement *measurement) {
    CettaActCompressionPrepared prepared;
    if (!cetta_act_compression_prepare(model, &prepared))
        return false;
    bool measured = cetta_act_compression_measure_prepared(
        &prepared, trace, measurement);
    cetta_act_compression_prepared_destroy(&prepared);
    return measured;
}

static int cetta_act_compression_measurement_compare(
        const CettaActCompressionMeasurement *left,
        const CettaActCompressionMeasurement *right,
        bool *valid) {
    if (!left || !right || !valid ||
        left->ordinary_bits == 0u || right->ordinary_bits == 0u ||
        (left->saved_bits != 0u &&
         right->ordinary_bits > UINT64_MAX / left->saved_bits) ||
        (right->saved_bits != 0u &&
         left->ordinary_bits > UINT64_MAX / right->saved_bits)) {
        if (valid)
            *valid = false;
        return 0;
    }
    uint64_t left_cross =
        left->saved_bits * right->ordinary_bits;
    uint64_t right_cross =
        right->saved_bits * left->ordinary_bits;
    if (left_cross > right_cross)
        return -1;
    if (left_cross < right_cross)
        return 1;
    return 0;
}

static CettaControllerRankStatus cetta_act_compression_rank(
        void *opaque,
        const CettaControllerCandidateView *candidates,
        size_t length, size_t *permutation) {
    CettaActCompressionRankContext *context = opaque;
    if (!context || !candidates || !permutation || length == 0u) {
        return CETTA_CONTROLLER_RANK_INVALIDATED;
    }
    context->attempts++;
    CettaActCompressionPrepared prepared;
    if (!cetta_act_compression_prepare(context->model, &prepared)) {
        context->deferred++;
        return CETTA_CONTROLLER_RANK_DEFERRED;
    }
    if (length > SIZE_MAX / sizeof(CettaActCompressionMeasurement) ||
        length > SIZE_MAX / sizeof(size_t) ||
        length >
            (SIZE_MAX -
             (CETTA_ACT_COMPRESSION_HASH_BUCKETS +
              context->model->dictionary_length) * sizeof(int32_t)) /
            (sizeof(CettaActCompressionMeasurement) + sizeof(size_t))) {
        cetta_act_compression_prepared_destroy(&prepared);
        context->invalidated++;
        return CETTA_CONTROLLER_RANK_INVALIDATED;
    }
    CettaActCompressionMeasurement *measurements =
        calloc(length, sizeof(*measurements));
    size_t *temporary = malloc(length * sizeof(*temporary));
    if (!measurements || !temporary) {
        free(measurements);
        free(temporary);
        cetta_act_compression_prepared_destroy(&prepared);
        context->deferred++;
        return CETTA_CONTROLLER_RANK_DEFERRED;
    }
    size_t working_bytes =
        (CETTA_ACT_COMPRESSION_HASH_BUCKETS +
         context->model->dictionary_length) * sizeof(int32_t) +
        length * sizeof(*measurements) +
        length * sizeof(*temporary);
    size_t peak_working_bytes = working_bytes;
    bool any_signal = false;
    CettaControllerRankStatus status = CETTA_CONTROLLER_RANK_READY;
    for (size_t i = 0u; i < length; i++) {
        CettaContinuationTrace trace;
        cetta_continuation_trace_init(&trace);
        CettaContinuationStatus traced =
            cetta_owned_continuation_trace(
                candidates[i].continuation, &trace);
        if (traced != CETTA_CONTINUATION_READY) {
            status = traced == CETTA_CONTINUATION_INVALIDATED
                ? CETTA_CONTROLLER_RANK_INVALIDATED
                : CETTA_CONTROLLER_RANK_DEFERRED;
        } else if (trace.projection_identity !=
                       context->model->projection_identity ||
                   !cetta_act_compression_measure_prepared(
                       &prepared, &trace, &measurements[i])) {
            status = CETTA_CONTROLLER_RANK_INVALIDATED;
        } else if (measurements[i].saved_bits != 0u) {
            any_signal = true;
        }
        if (trace.length <= SIZE_MAX - working_bytes &&
            working_bytes + trace.length > peak_working_bytes) {
            peak_working_bytes = working_bytes + trace.length;
        }
        cetta_continuation_trace_destroy(&trace);
        if (status != CETTA_CONTROLLER_RANK_READY)
            break;
        permutation[i] = i;
    }
    bool valid = true;
    if (status == CETTA_CONTROLLER_RANK_READY && !any_signal)
        status = CETTA_CONTROLLER_RANK_DEFERRED;
    for (size_t width = 1u;
         status == CETTA_CONTROLLER_RANK_READY && width < length;) {
        size_t step = width > SIZE_MAX / 2u
            ? length : 2u * width;
        for (size_t left = 0u; left < length;) {
            size_t middle = width < length - left
                ? left + width : length;
            size_t right = width < length - middle
                ? middle + width : length;
            size_t first = left;
            size_t second = middle;
            size_t out = left;
            while (first < middle && second < right) {
                int ordering = cetta_act_compression_measurement_compare(
                    &measurements[permutation[first]],
                    &measurements[permutation[second]], &valid);
                if (!valid)
                    break;
                /* Stable: the left run wins exact compression-rate ties. */
                temporary[out++] = ordering <= 0
                    ? permutation[first++] : permutation[second++];
            }
            while (valid && first < middle)
                temporary[out++] = permutation[first++];
            while (valid && second < right)
                temporary[out++] = permutation[second++];
            if (!valid)
                break;
            memcpy(&permutation[left], &temporary[left],
                   (right - left) * sizeof(*permutation));
            if (step >= length - left)
                break;
            left += step;
        }
        if (!valid) {
            status = CETTA_CONTROLLER_RANK_INVALIDATED;
            break;
        }
        if (width > length / 2u)
            break;
        width *= 2u;
    }
    free(measurements);
    free(temporary);
    cetta_act_compression_prepared_destroy(&prepared);
    if (peak_working_bytes > context->maximum_working_bytes)
        context->maximum_working_bytes = peak_working_bytes;
    if (status == CETTA_CONTROLLER_RANK_READY)
        context->applied++;
    else if (status == CETTA_CONTROLLER_RANK_DEFERRED)
        context->deferred++;
    else
        context->invalidated++;
    return status;
}

void cetta_act_compression_ranker_init(
        const CettaActCompressionModel *model,
        CettaActCompressionRankContext *context,
        CettaControllerBatchRanker *ranker) {
    if (!context || !ranker)
        return;
    *context = (CettaActCompressionRankContext){
        .model = model,
    };
    *ranker = (CettaControllerBatchRanker){
        .rank = cetta_act_compression_rank,
        .context = context,
        .scorer_identity = CETTA_ACT_COMPRESSION_SCORER_IDENTITY,
        .model_revision = model ? model->revision : 0u,
    };
}
