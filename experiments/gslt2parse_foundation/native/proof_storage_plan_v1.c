#include "proof_storage_plan_v1.h"

#include "finite_horn_answer_stream_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FHAnswerStreamV1 answers;
} PPProofStoragePlanStorageV1;

typedef enum {
    PPPROOF_STORAGE_RECORD_V1_UNKNOWN = 0,
    PPPROOF_STORAGE_RECORD_V1_TABLE,
    PPPROOF_STORAGE_RECORD_V1_MACHINE,
    PPPROOF_STORAGE_RECORD_V1_READ,
    PPPROOF_STORAGE_RECORD_V1_SEQUENCE,
    PPPROOF_STORAGE_RECORD_V1_CALL
} PPProofStorageRecordV1;

typedef struct {
    uint32_t tables;
    uint32_t machines;
    uint32_t reads;
    uint32_t sequences;
    uint32_t calls;
} PPProofStorageCountsV1;

static bool ppproof_storage_plan_v1_fail(
    char *buffer, size_t size, const char *format, ...) {
    va_list arguments;

    if (buffer && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool ppproof_storage_plan_v1_expr_head(
    const Atom *atom, const char *head, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == arity + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static const char *ppproof_storage_plan_v1_symbol(const Atom *atom) {
    const char *name;

    if (!atom || atom->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr((Atom *)atom);
    return name && name[0] != '\0' ? name : NULL;
}

static bool ppproof_storage_plan_v1_u32(
    const Atom *atom, uint32_t *value_out) {
    int64_t value;

    if (!atom || !value_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT)
        return false;
    value = atom->ground.ival;
    if (value < 0 || (uint64_t)value > UINT32_MAX)
        return false;
    *value_out = (uint32_t)value;
    return true;
}

static PPProofStorageLifetimeV1 ppproof_storage_plan_v1_lifetime(
    const Atom *atom) {
    if (atom_is_symbol((Atom *)atom, "state-persistent-v1"))
        return PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT;
    if (atom_is_symbol((Atom *)atom, "state-scoped-v1"))
        return PPPROOF_STORAGE_LIFETIME_V1_SCOPED;
    if (atom_is_symbol((Atom *)atom, "state-transactional-v1"))
        return PPPROOF_STORAGE_LIFETIME_V1_TRANSACTIONAL;
    return PPPROOF_STORAGE_LIFETIME_V1_INVALID;
}

static bool ppproof_storage_plan_v1_region_matches(
    PPProofStorageLifetimeV1 lifetime, const char *region) {
    if (!region)
        return false;
    if (lifetime == PPPROOF_STORAGE_LIFETIME_V1_PERSISTENT)
        return strcmp(region, "state-run-region-v1") == 0;
    if (lifetime == PPPROOF_STORAGE_LIFETIME_V1_SCOPED)
        return strcmp(region, "state-scope-region-v1") == 0;
    if (lifetime == PPPROOF_STORAGE_LIFETIME_V1_TRANSACTIONAL)
        return strcmp(region, "state-transaction-region-v1") == 0;
    return false;
}

static PPProofStorageRecordV1 ppproof_storage_plan_v1_record(
    const Atom *answer, const Atom **record_out) {
    const Atom *record;

    if (!ppproof_storage_plan_v1_expr_head(
            answer, "compile-proof-storage-plan-v1", 1u))
        return PPPROOF_STORAGE_RECORD_V1_UNKNOWN;
    record = answer->expr.elems[1];
    *record_out = record;
    if (ppproof_storage_plan_v1_expr_head(
            record, "state-table-storage-v1", 5u))
        return PPPROOF_STORAGE_RECORD_V1_TABLE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-machine-binding-v1", 4u))
        return PPPROOF_STORAGE_RECORD_V1_MACHINE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-machine-table-read-v1", 4u))
        return PPPROOF_STORAGE_RECORD_V1_READ;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-sequence-layout-v1", 7u))
        return PPPROOF_STORAGE_RECORD_V1_SEQUENCE;
    if (ppproof_storage_plan_v1_expr_head(
            record, "proof-call-region-plan-v1", 8u))
        return PPPROOF_STORAGE_RECORD_V1_CALL;
    return PPPROOF_STORAGE_RECORD_V1_UNKNOWN;
}

static bool ppproof_storage_plan_v1_count(
    uint32_t *value, char *error, size_t error_size) {
    if (*value == UINT32_MAX)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage plan has too many records");
    (*value)++;
    return true;
}

static bool ppproof_storage_plan_v1_alloc(
    void **output, uint32_t count, size_t item_size,
    char *error, size_t error_size) {
    void *items;

    if (count > 0u && item_size > SIZE_MAX / count)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage plan allocation overflows");
    items = calloc(count ? count : 1u, item_size);
    if (!items)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "cannot allocate proof storage plan");
    *output = items;
    return true;
}

static int ppproof_storage_plan_v1_table_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageTableV1 *left = left_raw;
    const PPProofStorageTableV1 *right = right_raw;
    return strcmp(left->table, right->table);
}

static int ppproof_storage_plan_v1_machine_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageMachineV1 *left = left_raw;
    const PPProofStorageMachineV1 *right = right_raw;
    return strcmp(left->machine, right->machine);
}

static int ppproof_storage_plan_v1_read_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageReadV1 *left = left_raw;
    const PPProofStorageReadV1 *right = right_raw;
    int compared = strcmp(left->machine, right->machine);
    return compared != 0 ? compared : strcmp(left->role, right->role);
}

static int ppproof_storage_plan_v1_sequence_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageSequenceV1 *left = left_raw;
    const PPProofStorageSequenceV1 *right = right_raw;
    int compared = strcmp(left->owner, right->owner);
    return compared != 0 ? compared : strcmp(left->provable, right->provable);
}

static int ppproof_storage_plan_v1_call_compare(
    const void *left_raw, const void *right_raw) {
    const PPProofStorageCallV1 *left = left_raw;
    const PPProofStorageCallV1 *right = right_raw;
    int compared = strcmp(left->operation, right->operation);
    if (compared != 0)
        return compared;
    if (left->action_index < right->action_index)
        return -1;
    if (left->action_index > right->action_index)
        return 1;
    return 0;
}

void ppproof_storage_plan_v1_init(PPProofStoragePlanV1 *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void ppproof_storage_plan_v1_free(PPProofStoragePlanV1 *plan) {
    PPProofStoragePlanStorageV1 *storage;

    if (!plan)
        return;
    storage = plan->storage;
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    free(plan->tables);
    free(plan->machines);
    free(plan->reads);
    free(plan->sequences);
    free(plan->calls);
    memset(plan, 0, sizeof(*plan));
}

const PPProofStorageTableV1 *ppproof_storage_plan_v1_table(
    const PPProofStoragePlanV1 *plan, const char *table) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !table)
        return NULL;
    high = plan->table_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->tables[middle].table, table);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->table_len &&
                   strcmp(plan->tables[low].table, table) == 0
               ? &plan->tables[low]
               : NULL;
}

const PPProofStorageMachineV1 *ppproof_storage_plan_v1_machine(
    const PPProofStoragePlanV1 *plan, const char *machine) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine)
        return NULL;
    high = plan->machine_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int compared = strcmp(plan->machines[middle].machine, machine);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->machine_len &&
                   strcmp(plan->machines[low].machine, machine) == 0
               ? &plan->machines[low]
               : NULL;
}

const PPProofStorageReadV1 *ppproof_storage_plan_v1_read(
    const PPProofStoragePlanV1 *plan,
    const char *machine,
    const char *role) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !machine || !role)
        return NULL;
    high = plan->read_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofStorageReadV1 *read = &plan->reads[middle];
        int compared = strcmp(read->machine, machine);
        if (compared == 0)
            compared = strcmp(read->role, role);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->read_len &&
                   strcmp(plan->reads[low].machine, machine) == 0 &&
                   strcmp(plan->reads[low].role, role) == 0
               ? &plan->reads[low]
               : NULL;
}

const PPProofStorageSequenceV1 *ppproof_storage_plan_v1_sequence(
    const PPProofStoragePlanV1 *plan,
    const char *owner,
    const char *provable) {
    uint32_t low = 0u;
    uint32_t high;

    if (!plan || !owner || !provable)
        return NULL;
    high = plan->sequence_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const PPProofStorageSequenceV1 *sequence = &plan->sequences[middle];
        int compared = strcmp(sequence->owner, owner);
        if (compared == 0)
            compared = strcmp(sequence->provable, provable);
        if (compared < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < plan->sequence_len &&
                   strcmp(plan->sequences[low].owner, owner) == 0 &&
                   strcmp(plan->sequences[low].provable, provable) == 0
               ? &plan->sequences[low]
               : NULL;
}

static bool ppproof_storage_plan_v1_parse_table(
    const Atom *record, PPProofStorageTableV1 *table,
    char *error, size_t error_size) {
    table->table = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    table->lifetime = ppproof_storage_plan_v1_lifetime(
        record->expr.elems[4]);
    table->region = ppproof_storage_plan_v1_symbol(record->expr.elems[5]);
    if (!table->table ||
        !ppproof_storage_plan_v1_u32(record->expr.elems[2], &table->arity) ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[3], &table->key_arity) ||
        table->arity == 0u || table->key_arity > table->arity ||
        !ppproof_storage_plan_v1_region_matches(
            table->lifetime, table->region))
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage table record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_machine(
    const Atom *record, PPProofStorageMachineV1 *machine,
    char *error, size_t error_size) {
    machine->machine = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    machine->owner = ppproof_storage_plan_v1_symbol(record->expr.elems[2]);
    machine->base = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    machine->provable = ppproof_storage_plan_v1_symbol(record->expr.elems[4]);
    if (!machine->machine || !machine->owner || !machine->base ||
        !machine->provable)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage machine record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_read(
    const Atom *record, PPProofStorageReadV1 *read,
    char *error, size_t error_size) {
    read->machine = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    read->role = ppproof_storage_plan_v1_symbol(record->expr.elems[2]);
    read->table = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    read->lifetime = ppproof_storage_plan_v1_lifetime(
        record->expr.elems[4]);
    if (!read->machine || !read->role || !read->table ||
        read->lifetime == PPPROOF_STORAGE_LIFETIME_V1_INVALID)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage table-read record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_sequence(
    const Atom *record, PPProofStorageSequenceV1 *sequence,
    char *error, size_t error_size) {
    sequence->owner = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    sequence->base = ppproof_storage_plan_v1_symbol(record->expr.elems[2]);
    sequence->provable = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    sequence->cons = ppproof_storage_plan_v1_symbol(record->expr.elems[4]);
    sequence->nil = ppproof_storage_plan_v1_symbol(record->expr.elems[5]);
    sequence->layout = ppproof_storage_plan_v1_symbol(record->expr.elems[6]);
    sequence->region = ppproof_storage_plan_v1_symbol(record->expr.elems[7]);
    if (!sequence->owner || !sequence->base || !sequence->provable ||
        !sequence->cons || !sequence->nil || !sequence->layout ||
        !sequence->region)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage sequence record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_parse_call(
    const Atom *record, PPProofStorageCallV1 *call,
    char *error, size_t error_size) {
    call->operation = ppproof_storage_plan_v1_symbol(record->expr.elems[1]);
    call->machine = ppproof_storage_plan_v1_symbol(record->expr.elems[3]);
    call->owner = ppproof_storage_plan_v1_symbol(record->expr.elems[4]);
    call->provable = ppproof_storage_plan_v1_symbol(record->expr.elems[5]);
    call->region = ppproof_storage_plan_v1_symbol(record->expr.elems[6]);
    call->layout = ppproof_storage_plan_v1_symbol(record->expr.elems[7]);
    call->observation = ppproof_storage_plan_v1_symbol(record->expr.elems[8]);
    if (!call->operation ||
        !ppproof_storage_plan_v1_u32(
            record->expr.elems[2], &call->action_index) ||
        !call->machine || !call->owner || !call->provable ||
        !call->region || !call->layout || !call->observation)
        return ppproof_storage_plan_v1_fail(
            error, error_size, "proof storage call record is malformed");
    return true;
}

static bool ppproof_storage_plan_v1_validate(
    const PPProofStoragePlanV1 *plan,
    char *error, size_t error_size) {
    uint32_t index;

    for (index = 1u; index < plan->table_len; index++) {
        if (strcmp(plan->tables[index - 1u].table,
                   plan->tables[index].table) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage table is duplicated");
    }
    for (index = 1u; index < plan->machine_len; index++) {
        if (strcmp(plan->machines[index - 1u].machine,
                   plan->machines[index].machine) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage machine is duplicated");
    }
    for (index = 1u; index < plan->read_len; index++) {
        if (strcmp(plan->reads[index - 1u].machine,
                   plan->reads[index].machine) == 0 &&
            strcmp(plan->reads[index - 1u].role,
                   plan->reads[index].role) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage table role is duplicated");
    }
    for (index = 1u; index < plan->sequence_len; index++) {
        if (strcmp(plan->sequences[index - 1u].owner,
                   plan->sequences[index].owner) == 0 &&
            strcmp(plan->sequences[index - 1u].provable,
                   plan->sequences[index].provable) == 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage sequence is duplicated");
    }
    for (index = 1u; index < plan->call_len; index++) {
        if (strcmp(plan->calls[index - 1u].operation,
                   plan->calls[index].operation) == 0 &&
            plan->calls[index - 1u].action_index ==
                plan->calls[index].action_index)
            return ppproof_storage_plan_v1_fail(
                error, error_size, "proof storage call is duplicated");
    }
    for (index = 0u; index < plan->read_len; index++) {
        const PPProofStorageReadV1 *read = &plan->reads[index];
        const PPProofStorageTableV1 *table =
            ppproof_storage_plan_v1_table(plan, read->table);
        if (!ppproof_storage_plan_v1_machine(plan, read->machine))
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage table read names an unknown machine");
        if (!table || table->lifetime != read->lifetime)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage table read disagrees with its table");
    }
    for (index = 0u; index < plan->machine_len; index++) {
        const PPProofStorageMachineV1 *machine = &plan->machines[index];
        const PPProofStorageSequenceV1 *sequence =
            ppproof_storage_plan_v1_sequence(
                plan, machine->owner, machine->provable);
        if (!sequence || strcmp(sequence->base, machine->base) != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage machine lacks its sequence layout");
    }
    for (index = 0u; index < plan->call_len; index++) {
        const PPProofStorageCallV1 *call = &plan->calls[index];
        const PPProofStorageMachineV1 *machine =
            ppproof_storage_plan_v1_machine(plan, call->machine);
        const PPProofStorageSequenceV1 *sequence =
            ppproof_storage_plan_v1_sequence(
                plan, call->owner, call->provable);
        if (!machine || !sequence ||
            strcmp(machine->owner, call->owner) != 0 ||
            strcmp(machine->provable, call->provable) != 0 ||
            strcmp(sequence->layout, call->layout) != 0 ||
            strcmp(sequence->region, call->region) != 0)
            return ppproof_storage_plan_v1_fail(
                error, error_size,
                "proof storage call disagrees with its machine or sequence");
    }
    return true;
}

bool ppproof_storage_plan_v1_load(
    PPProofStoragePlanV1 *plan,
    const char *answer_path,
    char *error_buf,
    size_t error_buf_size) {
    PPProofStoragePlanV1 result;
    PPProofStoragePlanStorageV1 *storage = NULL;
    PPProofStorageCountsV1 counts = {0};
    PPProofStorageCountsV1 writes = {0};
    size_t index;
    bool ok = false;

    ppproof_storage_plan_v1_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!plan || !answer_path)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size, "invalid proof storage plan request");
    storage = calloc(1u, sizeof(*storage));
    if (!storage)
        return ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "cannot allocate proof storage plan storage");
    fh_answer_stream_v1_init(&storage->answers);
    if (!fh_answer_stream_v1_read(
            &storage->answers, answer_path, error_buf, error_buf_size))
        goto done;
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *record = NULL;
        PPProofStorageRecordV1 kind = ppproof_storage_plan_v1_record(
            storage->answers.terms[index], &record);
        uint32_t *count = NULL;

        (void)record;
        switch (kind) {
        case PPPROOF_STORAGE_RECORD_V1_TABLE:
            count = &counts.tables;
            break;
        case PPPROOF_STORAGE_RECORD_V1_MACHINE:
            count = &counts.machines;
            break;
        case PPPROOF_STORAGE_RECORD_V1_READ:
            count = &counts.reads;
            break;
        case PPPROOF_STORAGE_RECORD_V1_SEQUENCE:
            count = &counts.sequences;
            break;
        case PPPROOF_STORAGE_RECORD_V1_CALL:
            count = &counts.calls;
            break;
        case PPPROOF_STORAGE_RECORD_V1_UNKNOWN:
        default:
            ppproof_storage_plan_v1_fail(
                error_buf, error_buf_size,
                "proof storage answer has an unknown record");
            goto done;
        }
        if (!ppproof_storage_plan_v1_count(
                count, error_buf, error_buf_size))
            goto done;
    }
    if (counts.tables == 0u || counts.machines == 0u ||
        counts.reads == 0u || counts.sequences == 0u || counts.calls == 0u) {
        ppproof_storage_plan_v1_fail(
            error_buf, error_buf_size,
            "proof storage plan omits a required record family");
        goto done;
    }
    if (!ppproof_storage_plan_v1_alloc(
            (void **)&result.tables, counts.tables,
            sizeof(*result.tables), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.machines, counts.machines,
            sizeof(*result.machines), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.reads, counts.reads,
            sizeof(*result.reads), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.sequences, counts.sequences,
            sizeof(*result.sequences), error_buf, error_buf_size) ||
        !ppproof_storage_plan_v1_alloc(
            (void **)&result.calls, counts.calls,
            sizeof(*result.calls), error_buf, error_buf_size))
        goto done;
    result.table_len = counts.tables;
    result.machine_len = counts.machines;
    result.read_len = counts.reads;
    result.sequence_len = counts.sequences;
    result.call_len = counts.calls;
    for (index = 0u; index < storage->answers.len; index++) {
        const Atom *record = NULL;
        PPProofStorageRecordV1 kind = ppproof_storage_plan_v1_record(
            storage->answers.terms[index], &record);
        bool parsed = false;

        switch (kind) {
        case PPPROOF_STORAGE_RECORD_V1_TABLE:
            parsed = ppproof_storage_plan_v1_parse_table(
                record, &result.tables[writes.tables++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_MACHINE:
            parsed = ppproof_storage_plan_v1_parse_machine(
                record, &result.machines[writes.machines++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_READ:
            parsed = ppproof_storage_plan_v1_parse_read(
                record, &result.reads[writes.reads++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_SEQUENCE:
            parsed = ppproof_storage_plan_v1_parse_sequence(
                record, &result.sequences[writes.sequences++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_CALL:
            parsed = ppproof_storage_plan_v1_parse_call(
                record, &result.calls[writes.calls++],
                error_buf, error_buf_size);
            break;
        case PPPROOF_STORAGE_RECORD_V1_UNKNOWN:
        default:
            break;
        }
        if (!parsed)
            goto done;
    }
    qsort(result.tables, result.table_len, sizeof(*result.tables),
          ppproof_storage_plan_v1_table_compare);
    qsort(result.machines, result.machine_len, sizeof(*result.machines),
          ppproof_storage_plan_v1_machine_compare);
    qsort(result.reads, result.read_len, sizeof(*result.reads),
          ppproof_storage_plan_v1_read_compare);
    qsort(result.sequences, result.sequence_len, sizeof(*result.sequences),
          ppproof_storage_plan_v1_sequence_compare);
    qsort(result.calls, result.call_len, sizeof(*result.calls),
          ppproof_storage_plan_v1_call_compare);
    if (!ppproof_storage_plan_v1_validate(
            &result, error_buf, error_buf_size))
        goto done;
    memcpy(result.semantic_digest, storage->answers.digest,
           sizeof(result.semantic_digest));
    result.storage = storage;
    storage = NULL;
    ppproof_storage_plan_v1_free(plan);
    *plan = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (storage) {
        fh_answer_stream_v1_free(&storage->answers);
        free(storage);
    }
    ppproof_storage_plan_v1_free(&result);
    return ok;
}
