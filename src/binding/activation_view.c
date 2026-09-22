#include "slot_store_internal.h"

#include <string.h>

void bindings_activation_view_init(BindingsActivationView *frame) {
    if (frame) {
        memset(frame, 0, sizeof(*frame));
        frame->source_kind = BINDING_VALUE_CONTEXTUAL;
    }
}

void bindings_activation_view_free(BindingsActivationView *frame) {
    if (frame)
        memset(frame, 0, sizeof(*frame));
}

bool bindings_activation_view_borrow(
        const Bindings *bindings, const BindingsActivationView *frame,
        BindingsActivationRead *read_out) {
    if (!read_out)
        return false;
    *read_out = (BindingsActivationRead){0};
    if (!frame || !bindings || frame->epoch == 0u ||
        !binding_value_kind_is_contextual(frame->source_kind) ||
        frame->first_entry > bindings->len ||
        (frame->len > 0u && (!frame->source_ids || !frame->source_variables)))
        return false;
    const BindingsFrameIndexEntry *authority =
        bindings_frame_index_find_ref_const(bindings->frame_index, frame->authority_ref);
    if (frame->len > 0u && !authority)
        return false;
    *read_out = (BindingsActivationRead){frame, authority};
    return true;
}

bool bindings_activation_view_available(
        const BindingsActivationView *frame, const Bindings *bindings) {
    BindingsActivationRead read;
    return bindings_activation_view_borrow(bindings, frame, &read);
}

bool bindings_activation_read_slot(
        const BindingsActivationRead *read, uint32_t source_slot,
        BindingValue *value_out, bool *present_out) {
    if (value_out)
        *value_out = binding_value_from_atom(NULL);
    if (present_out)
        *present_out = false;
    if (!read || !read->view || !read->authority || !value_out || !present_out ||
        source_slot >= read->view->len)
        return false;
    const BindingsActivationView *frame = read->view;
    const BindingsFrameIndexEntry *authority = read->authority;
    uint32_t slot = source_slot;
    if (authority->schema->source_ids != frame->source_ids ||
        source_slot >= authority->slot_len) {
        if (!bindings_frame_index_find_slot(
                authority, frame->source_ids[source_slot], &slot))
            return false;
    }
    *present_out = authority->write_version[slot] > frame->first_write_version &&
        authority->values[slot].skeleton != NULL;
    if (*present_out)
        *value_out = authority->values[slot];
    return true;
}

bool bindings_activation_view_read_slot(
        const Bindings *bindings, const BindingsActivationView *frame,
        uint32_t source_slot, BindingValue *value_out, bool *present_out) {
    BindingsActivationRead read;
    if (!bindings_activation_view_borrow(bindings, frame, &read)) {
        if (value_out)
            *value_out = binding_value_from_atom(NULL);
        if (present_out)
            *present_out = false;
        return false;
    }
    return bindings_activation_read_slot(&read, source_slot, value_out, present_out);
}

static uint64_t bindings_frame_activation_write_boundary(
        const BindingsFrameIndex *index,
        const BindingsFrameIndexEntry *frame,
        uint32_t first_entry) {
    (void)frame;
    return first_entry == 0u
        ? 0u : (index ? index->write_clock : 0u);
}

bool bindings_activation_view_prepare(
        BindingsActivationView *frame,
        BindingsBuilder *builder,
        const VarId *source_ids, Atom *const *source_variables,
        uint32_t variable_count, uint32_t epoch,
        uint32_t first_entry) {
    Bindings *bindings = builder ? &builder->current : NULL;
    if (!frame || !builder || !bindings || builder->instance_id == 0u ||
        epoch == 0u ||
        first_entry > bindings->len ||
        (variable_count > 0u && (!source_ids || !source_variables))) {
        return false;
    }
    bool source_ids_contiguous = variable_count > 0u;
    for (uint32_t index = 0u; index < variable_count; index++) {
        Atom *variable = source_variables[index];
        if (!variable || variable->kind != ATOM_VAR ||
            variable->var_id != source_ids[index] ||
            var_epoch_suffix(source_ids[index]) != 0u ||
            (index > 0u && source_ids[index - 1u] >= source_ids[index])) {
            return false;
        }
        if (index > 0u &&
            source_ids[index] != source_ids[index - 1u] + UINT64_C(1)) {
            source_ids_contiguous = false;
        }
    }
    BindingsFrameRef authority_ref = {0};
    BindingsFrameIndexEntry *authority = NULL;
    if (variable_count == 0u) {
        authority = bindings_frame_index_find_frame(
            bindings->frame_index, epoch);
        authority_ref = bindings_frame_ref_from_entry(
            bindings->frame_index, authority);
    } else if (bindings_builder_register_frame_kind(
                   builder, source_ids, variable_count, epoch, false, NULL,
                   &authority_ref)) {
        authority = bindings_frame_index_find_ref(
            bindings->frame_index, authority_ref);
    }
    if (!authority)
        return false;
    if (!authority->has_activation_write_boundary) {
        uint64_t boundary = bindings_frame_activation_write_boundary(
            bindings->frame_index, authority, first_entry);
        if (!bindings_frame_index_mark_activation_boundary_ref(
                bindings, authority_ref, boundary)) {
            return false;
        }
        authority = bindings_frame_index_find_ref(
            bindings->frame_index, authority_ref);
        if (!authority)
            return false;
    }
    if (!bindings_frame_ref_is_valid(authority_ref))
        return false;
    frame->source_ids = source_ids;
    frame->source_variables = source_variables;
    frame->len = variable_count;
    frame->source_first_id = variable_count > 0u
        ? source_ids[0] : VAR_ID_NONE;
    frame->source_ids_contiguous = source_ids_contiguous;
    frame->epoch = epoch;
    frame->source_kind = BINDING_VALUE_CONTEXTUAL;
    frame->authority_ref = authority_ref;
    frame->first_entry = first_entry;
    frame->first_write_version = authority->activation_write_boundary;
    return bindings_activation_view_available(frame, &builder->current);
}

bool bindings_activation_view_prepare_schema(
        BindingsActivationView *frame,
        BindingsBuilder *builder,
        BindingsFrameSchema *schema,
        Atom *const *source_variables,
        uint32_t epoch, uint32_t first_entry) {
    Bindings *bindings = builder ? &builder->current : NULL;
    uint32_t variable_count = schema ? schema->len : 0u;
    if (!frame || !builder || !bindings || !schema ||
        builder->instance_id == 0u || epoch == 0u ||
        first_entry > bindings->len ||
        (variable_count > 0u && !source_variables)) {
        return false;
    }

    BindingsFrameIndex *index = bindings->frame_index;
    BindingsFrameIndexEntry *authority =
        bindings_frame_index_find_frame(index, epoch);
    BindingsFrameRef authority_ref = bindings_frame_ref_from_entry(
        index, authority);
    bool exact_schema = authority && authority->schema_complete &&
        (authority->schema == schema ||
         (authority->schema->len == schema->len &&
          memcmp(authority->schema->source_ids, schema->source_ids,
                 (size_t)schema->len * sizeof(*schema->source_ids)) == 0));
    if (variable_count > 0u && !exact_schema) {
        if (!bindings_builder_register_complete_frame_schema_ref(
                builder, schema, epoch, &authority_ref)) {
            return false;
        }
        index = bindings->frame_index;
        authority = bindings_frame_index_find_ref(index, authority_ref);
        exact_schema = authority && authority->schema_complete &&
            (authority->schema == schema ||
             (authority->schema->len == schema->len &&
              memcmp(authority->schema->source_ids, schema->source_ids,
                     (size_t)schema->len *
                         sizeof(*schema->source_ids)) == 0));
        if (!exact_schema)
            return false;
    }
    if (variable_count > 0u &&
        !authority->has_activation_write_boundary) {
        uint64_t boundary = bindings_frame_activation_write_boundary(
            index, authority, first_entry);
        if (!bindings_frame_index_mark_activation_boundary_ref(
                bindings, authority_ref, boundary)) {
            return false;
        }
        index = bindings->frame_index;
        authority = bindings_frame_index_find_ref(index, authority_ref);
        if (!authority)
            return false;
    }
    if (variable_count > 0u &&
        !bindings_frame_ref_is_valid(authority_ref)) {
        return false;
    }

    frame->source_ids = schema->source_ids;
    frame->source_variables = source_variables;
    frame->len = variable_count;
    frame->source_first_id = schema->source_first_id;
    frame->source_ids_contiguous = schema->source_ids_contiguous;
    frame->epoch = epoch;
    frame->source_kind = BINDING_VALUE_CONTEXTUAL;
    frame->authority_ref = authority_ref;
    frame->first_entry = first_entry;
    frame->first_write_version = authority &&
            authority->has_activation_write_boundary
        ? authority->activation_write_boundary
        : (index ? index->write_clock : 0u);
    return bindings_activation_view_available(frame, &builder->current);
}
