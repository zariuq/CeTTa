#include "frame_schema_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void bindings_frame_schema_release(BindingsFrameSchema *schema) {
    if (!schema)
        return;
    uint32_t previous = atomic_fetch_sub_explicit(
        &schema->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous == 1u) {
        free(schema->spellings);
        free(schema->name_keys);
        free(schema);
    }
}

BindingsFrameSchema *bindings_frame_schema_alloc(uint32_t capacity) {
    BindingsFrameSchema *schema = cetta_malloc(
        sizeof(*schema) + (size_t)capacity * sizeof(*schema->source_ids));
    atomic_init(&schema->references, 1u);
    schema->len = 0u;
    schema->cap = capacity;
    schema->source_first_id = VAR_ID_NONE;
    schema->source_ids_contiguous = false;
    schema->spellings = capacity > 0u
        ? cetta_malloc((size_t)capacity * sizeof(*schema->spellings))
        : NULL;
    schema->name_keys = capacity > 0u
        ? cetta_malloc((size_t)capacity * sizeof(*schema->name_keys))
        : NULL;
    if (capacity > 0u) {
        for (uint32_t slot = 0u; slot < capacity; slot++)
            schema->spellings[slot] = SYMBOL_ID_NONE;
        memset(schema->name_keys, 0,
               (size_t)capacity * sizeof(*schema->name_keys));
    }
    return schema;
}

void bindings_frame_schema_publish(
        BindingsFrameSchema *schema, uint32_t len) {
    assert(schema && len <= schema->cap &&
           atomic_load_explicit(&schema->references, memory_order_acquire) == 1u);
    schema->len = len;
    schema->source_first_id = len ? schema->source_ids[0] : VAR_ID_NONE;
    schema->source_ids_contiguous = true;
    for (uint32_t i = 1u; i < len; i++) {
        if (schema->source_ids[i] != schema->source_ids[i - 1u] + UINT64_C(1))
            schema->source_ids_contiguous = false;
    }
}

BindingsFrameSchema *bindings_frame_schema_new(
        const VarId *source_ids, uint32_t source_len) {
    if ((source_len > 0u && !source_ids) ||
        (size_t)source_len > (SIZE_MAX - sizeof(BindingsFrameSchema)) / sizeof(VarId))
        return NULL;
    for (uint32_t i = 0u; i < source_len; i++) {
        if (source_ids[i] == VAR_ID_NONE || var_epoch_suffix(source_ids[i]) != 0u ||
            (i > 0u && source_ids[i - 1u] >= source_ids[i]))
            return NULL;
    }
    BindingsFrameSchema *schema = bindings_frame_schema_alloc(source_len);
    if (source_len > 0u)
        memcpy(schema->source_ids, source_ids, (size_t)source_len * sizeof(VarId));
    bindings_frame_schema_publish(schema, source_len);
    return schema;
}

BindingsFrameSchema *bindings_frame_schema_dense(uint32_t count) {
    BindingsFrameSchema *schema = bindings_frame_schema_alloc(count);
    if (!schema)
        return NULL;
    for (uint32_t slot = 0u; slot < count; slot++)
        schema->source_ids[slot] = (VarId)slot + 1u;
    bindings_frame_schema_publish(schema, count);
    return schema;
}

BindingsFrameSchema *bindings_frame_schema_new_presented(
        const VarId *source_ids, Atom *const *source_variables,
        uint32_t source_len) {
    if (source_len > 0u && !source_variables)
        return NULL;
    BindingsFrameSchema *schema = bindings_frame_schema_new(
        source_ids, source_len);
    if (!schema)
        return NULL;
    for (uint32_t slot = 0u; slot < source_len; slot++) {
        Atom *variable = source_variables[slot];
        if (!variable || variable->kind != ATOM_VAR ||
            (VarId)var_base_id(variable->var_id) != source_ids[slot]) {
            bindings_frame_schema_release(schema);
            return NULL;
        }
        schema->spellings[slot] = variable->sym_id;
        schema->name_keys[slot] = variable->name_key;
    }
    return schema;
}

void bindings_frame_schema_retain(BindingsFrameSchema *schema) {
    if (!schema)
        return;
    uint32_t previous = atomic_fetch_add_explicit(
        &schema->references, 1u, memory_order_relaxed);
    assert(previous > 0u && previous < UINT32_MAX);
}

const VarId *bindings_frame_schema_source_ids(const BindingsFrameSchema *schema) {
    return schema ? schema->source_ids : NULL;
}

uint32_t bindings_frame_schema_len(const BindingsFrameSchema *schema) {
    return schema ? schema->len : 0u;
}

bool bindings_frame_schema_find_slot(
        const BindingsFrameSchema *schema, VarId source_id,
        uint32_t *slot_out) {
    if (!schema || !slot_out || source_id == VAR_ID_NONE)
        return false;
    if (schema->source_ids_contiguous) {
        if (source_id < schema->source_first_id)
            return false;
        uint64_t offset = source_id - schema->source_first_id;
        if (offset >= schema->len)
            return false;
        *slot_out = (uint32_t)offset;
        return true;
    }
    uint32_t low = 0u;
    uint32_t high = schema->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (schema->source_ids[middle] < source_id)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= schema->len || schema->source_ids[low] != source_id)
        return false;
    *slot_out = low;
    return true;
}

BindingsFrameSchema *bindings_frame_schema_clone(
        const BindingsFrameSchema *source) {
    if (!source)
        return NULL;
    BindingsFrameSchema *copy = bindings_frame_schema_alloc(source->cap);
    if (source->len > 0u) {
        memcpy(copy->source_ids, source->source_ids,
               (size_t)source->len * sizeof(*copy->source_ids));
        memcpy(copy->spellings, source->spellings,
               (size_t)source->len * sizeof(*copy->spellings));
        memcpy(copy->name_keys, source->name_keys,
               (size_t)source->len * sizeof(*copy->name_keys));
    }
    bindings_frame_schema_publish(copy, source->len);
    return copy;
}
