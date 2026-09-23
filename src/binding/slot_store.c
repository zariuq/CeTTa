#include "slot_store_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void bindings_frame_index_entry_free(
        BindingsFrameIndexEntry *entry);

static void bindings_frame_index_entry_retain(
        BindingsFrameIndexEntry *entry);

static bool bindings_frame_table_reserve(
        BindingsFrameIndex *index, uint32_t needed);

static void bindings_frame_table_relocate(BindingsFrameIndex *index);

static bool bindings_frame_index_spare_reserve(
        BindingsFrameIndex *index, uint32_t needed);

static bool bindings_frame_index_recycle_entry(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *entry);

static bool bindings_frame_index_initialize_entry(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *entry,
        const VarId *source_ids, uint32_t source_len,
        uint32_t epoch, BindingsFrameSchema *prepared_schema);

static BindingsFrameIndex *bindings_frame_index_alloc(void);

static bool bindings_frame_index_contains_sources(
        const BindingsFrameIndexEntry *frame,
        const VarId *source_ids, uint32_t source_len);

static BindingsFrameIndex *bindings_frame_index_clone(
        const BindingsFrameIndex *source);

static bool bindings_frame_index_write_slot(
        Bindings *bindings, uint32_t epoch, uint32_t slot,
        BindingValue value, uint64_t *version_out,
        bool *authority_moved_out);

static bool bindings_builder_restore_frame_registration_count(
        BindingsBuilder *bb, uint32_t target_len);

static void bindings_frame_index_entry_free(
        BindingsFrameIndexEntry *entry) {
    if (!entry)
        return;
    if (entry->owns_identity)
        cetta_frame_identity_release(entry->epoch);
    bindings_frame_schema_release(entry->schema);
    if (entry->storage_references) {
        uint32_t previous = atomic_fetch_sub_explicit(
            entry->storage_references, 1u, memory_order_acq_rel);
        assert(previous > 0u);
        if (previous == 1u) {
            free(entry->values);
            free(entry->write_version);
            free(entry->storage_references);
        }
    }
    memset(entry, 0, sizeof(*entry));
}

static void bindings_frame_index_entry_retain(
        BindingsFrameIndexEntry *entry) {
    if (!entry || !entry->storage_references)
        return;
    assert(entry->schema);
    if (entry->owns_identity && !cetta_frame_identity_retain(entry->epoch))
        abort();
    bindings_frame_schema_retain(entry->schema);
    uint32_t previous = atomic_fetch_add_explicit(
        entry->storage_references, 1u, memory_order_relaxed);
    assert(previous > 0u && previous < UINT32_MAX);
}

bool bindings_frame_index_entry_detach(
        BindingsFrameIndexEntry *entry) {
    if (!entry || !entry->storage_references)
        return false;
    uint32_t references = atomic_load_explicit(
        entry->storage_references, memory_order_acquire);
    if (references == 1u)
        return true;
    _Atomic uint32_t *storage_references =
        cetta_malloc(sizeof(*storage_references));
    BindingValue *values = entry->cap > 0u
        ? cetta_malloc((size_t)entry->cap * sizeof(*values))
        : NULL;
    uint64_t *write_version = entry->cap > 0u
        ? cetta_malloc((size_t)entry->cap * sizeof(*write_version))
        : NULL;
    if (entry->slot_len > 0u) {
        memcpy(values, entry->values,
               (size_t)entry->slot_len * sizeof(*values));
        memcpy(write_version, entry->write_version,
               (size_t)entry->slot_len * sizeof(*write_version));
    }
    atomic_init(storage_references, 1u);
    uint32_t previous = atomic_fetch_sub_explicit(
        entry->storage_references, 1u, memory_order_acq_rel);
    assert(previous > 1u);
    entry->storage_references = storage_references;
    entry->values = values;
    entry->write_version = write_version;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_DETACH);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_COPIED_SLOT,
        entry->slot_len);
    return true;
}

/* Place an entry in the handle table; the caller has reserved room. */
static void bindings_frame_table_insert(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *entry) {
    uint32_t mask = index->handle_table_mask;
    uint32_t slot = bindings_frame_table_home(
        index, cetta_frame_handle(entry->epoch));
    while (index->handle_table[slot])
        slot = (slot + 1u) & mask;
    index->handle_table[slot] = entry;
}

/* Rebuild the table from the owning vector: entry addresses change when
 * `frames` grows, and a removal is cheapest as a rebuild at these sizes. */
static void bindings_frame_table_relocate(BindingsFrameIndex *index) {
    if (!index->handle_table)
        return;
    memset(index->handle_table, 0,
        ((size_t)index->handle_table_mask + 1u) * sizeof(*index->handle_table));
    for (uint32_t i = 0u; i < index->len; i++) {
        if (index->frames[i].epoch != 0u)
            bindings_frame_table_insert(index, &index->frames[i]);
    }
}

/* Ensure the table can hold `frames` entries at no more than half load. */
static bool bindings_frame_table_reserve(
        BindingsFrameIndex *index, uint32_t frames) {
    if (!index || frames > CETTA_FRAME_HANDLE_MASK + 1u)
        return false;
    uint32_t capacity = index->handle_table ? index->handle_table_mask + 1u : 0u;
    if (frames * 2u <= capacity)
        return true;
    uint32_t next = capacity ? capacity : 8u;
    while (next < frames * 2u)
        next *= 2u;
    uint32_t shift = 32u;
    for (uint32_t bits = next; bits > 1u; bits >>= 1u)
        shift--;
    free(index->handle_table);
    index->handle_table = cetta_malloc(
        (size_t)next * sizeof(*index->handle_table));
    index->handle_table_mask = next - 1u;
    index->handle_table_shift = shift;
    bindings_frame_table_relocate(index);
    return true;
}

/* Remove `handle` from the table by backward shift (Knuth's deletion for
 * linear probing): later entries of the same run move down when their home
 * lies at or before the hole, so no tombstones and no rebuild. */
static void bindings_frame_table_remove(
        BindingsFrameIndex *index, uint32_t handle) {
    if (!index->handle_table)
        return;
    uint32_t mask = index->handle_table_mask;
    uint32_t slot = bindings_frame_table_home(index, handle);
    while (index->handle_table[slot] &&
           cetta_frame_handle(index->handle_table[slot]->epoch) != handle)
        slot = (slot + 1u) & mask;
    if (!index->handle_table[slot])
        return;
    uint32_t hole = slot;
    for (uint32_t next = (hole + 1u) & mask; index->handle_table[next];
         next = (next + 1u) & mask) {
        uint32_t home = bindings_frame_table_home(
            index, cetta_frame_handle(index->handle_table[next]->epoch));
        bool stays = hole <= next
            ? (home > hole && home <= next)
            : (home > hole || home <= next);
        if (!stays) {
            index->handle_table[hole] = index->handle_table[next];
            hole = next;
        }
    }
    index->handle_table[hole] = NULL;
}

/* Point the table at `entry` for its handle after the entry moved. */
static void bindings_frame_table_relink(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *entry) {
    uint32_t handle = cetta_frame_handle(entry->epoch);
    uint32_t mask = index->handle_table_mask;
    uint32_t slot = bindings_frame_table_home(index, handle);
    while (index->handle_table[slot] &&
           cetta_frame_handle(index->handle_table[slot]->epoch) != handle)
        slot = (slot + 1u) & mask;
    if (index->handle_table[slot])
        index->handle_table[slot] = entry;
}

static bool bindings_frame_index_spare_reserve(
        BindingsFrameIndex *index, uint32_t needed) {
    if (!index)
        return false;
    if (needed <= index->spare_cap)
        return true;
    uint32_t cap = index->spare_cap ? index->spare_cap * 2u : 8u;
    while (cap < needed) {
        if (cap > UINT32_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if ((size_t)cap > SIZE_MAX / sizeof(*index->spares))
        return false;
    index->spares = index->spares
        ? cetta_realloc(index->spares,
              (size_t)cap * sizeof(*index->spares))
        : cetta_malloc((size_t)cap * sizeof(*index->spares));
    index->spare_cap = cap;
    return true;
}

static bool bindings_frame_index_recycle_entry(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *entry) {
    if (!index || !entry || !entry->storage_references ||
        index->bound_value_count < entry->bound_value_count)
        return false;
    uint32_t handle = cetta_frame_handle(entry->epoch);
    if (bindings_frame_table_lookup(index, handle) != entry)
        return false;
    bindings_frame_table_remove(index, handle);
    index->bound_value_count -= entry->bound_value_count;
    index->private_value_count -= entry->private_value_count;
    entry->bound_value_count = 0u;
    entry->private_value_count = 0u;
    uint32_t references = atomic_load_explicit(
        entry->storage_references, memory_order_acquire);
    if (references != 1u || !bindings_frame_index_spare_reserve(
            index, index->spare_len + 1u)) {
        bindings_frame_index_entry_free(entry);
        return true;
    }
    if (entry->owns_identity)
        cetta_frame_identity_release(entry->epoch);
    entry->owns_identity = false;
    entry->epoch = 0u;
    entry->activation_write_boundary = 0u;
    entry->has_activation_write_boundary = false;
    entry->schema_complete = false;
    index->spares[index->spare_len++] = *entry;
    memset(entry, 0, sizeof(*entry));
    return true;
}

static bool bindings_frame_index_initialize_entry(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *entry,
        const VarId *source_ids, uint32_t source_len,
        uint32_t epoch, BindingsFrameSchema *prepared_schema) {
    if (!index || !entry || !source_ids || source_len == 0u || epoch == 0u)
        return false;
    BindingsFrameIndexEntry storage = {0};
    uint32_t spare = index->spare_len;
    while (spare > 0u) {
        spare--;
        if (index->spares[spare].cap < source_len)
            continue;
        storage = index->spares[spare];
        index->spare_len--;
        if (spare < index->spare_len) {
            index->spares[spare] =
                index->spares[index->spare_len];
        }
        memset(&index->spares[index->spare_len], 0,
               sizeof(*index->spares));
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_REUSE);
        break;
    }
    if (!storage.storage_references) {
        storage.storage_references =
            cetta_malloc(sizeof(*storage.storage_references));
        atomic_init(storage.storage_references, 1u);
        storage.values = cetta_malloc(
            (size_t)source_len * sizeof(*storage.values));
        storage.write_version = cetta_malloc(
            (size_t)source_len * sizeof(*storage.write_version));
        storage.cap = source_len;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_ALLOC);
    }
    if (prepared_schema) {
        bindings_frame_schema_retain(prepared_schema);
        bindings_frame_schema_release(storage.schema);
        storage.schema = prepared_schema;
    } else {
        if (!storage.schema || storage.schema->cap < source_len ||
            atomic_load_explicit(&storage.schema->references, memory_order_acquire) != 1u) {
            bindings_frame_schema_release(storage.schema);
            storage.schema = bindings_frame_schema_alloc(source_len);
        }
        memcpy(storage.schema->source_ids, source_ids,
               (size_t)source_len * sizeof(*storage.schema->source_ids));
        bindings_frame_schema_publish(storage.schema, source_len);
    }
    storage.epoch = epoch;
    storage.owns_identity = cetta_frame_identity_retain(epoch);
    storage.bound_value_count = 0u;
    storage.private_value_count = 0u;
    storage.slot_len = source_len;
    storage.activation_write_boundary = 0u;
    storage.has_activation_write_boundary = false;
    storage.schema_complete = false;
    memset(storage.values, 0,
           (size_t)source_len * sizeof(*storage.values));
    memset(storage.write_version, 0,
           (size_t)source_len * sizeof(*storage.write_version));
    *entry = storage;
    return true;
}

static BindingsFrameIndex *bindings_frame_index_alloc(void) {
    BindingsFrameIndex *index = cetta_malloc(sizeof(*index));
    atomic_init(&index->references, 1u);
    index->schema_revision = 1u;
    index->write_clock = 0u;
    index->bound_value_count = 0u;
    index->private_value_count = 0u;
    index->max_schema_len = 0u;
    index->frames = NULL;
    index->len = 0u;
    index->cap = 0u;
    index->handle_table = NULL;
    index->handle_table_mask = 0u;
    index->handle_table_shift = 32u;
    index->spares = NULL;
    index->spare_len = 0u;
    index->spare_cap = 0u;
    return index;
}

void bindings_frame_index_retain(BindingsFrameIndex *index) {
    if (!index)
        return;
    uint32_t previous = atomic_fetch_add_explicit(
        &index->references, 1u, memory_order_relaxed);
    assert(previous > 0u && previous < UINT32_MAX);
}

void bindings_frame_index_release(BindingsFrameIndex *index) {
    if (!index)
        return;
    uint32_t previous = atomic_fetch_sub_explicit(
        &index->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous != 1u)
        return;
    for (uint32_t i = 0u; i < index->len; i++)
        bindings_frame_index_entry_free(&index->frames[i]);
    for (uint32_t i = 0u; i < index->spare_len; i++)
        bindings_frame_index_entry_free(&index->spares[i]);
    free(index->frames);
    free(index->handle_table);
    free(index->spares);
    free(index);
}

bool bindings_frame_index_grow_slots(Bindings *bindings,
        CettaFrameIdentity identity, uint32_t needed) {
    const BindingsFrameIndexEntry *known = bindings_frame_index_find_frame_const(
        bindings->frame_index, identity);
    if (!known || !known->schema->source_ids_contiguous ||
        known->schema->source_first_id != 1u)
        return false;
    if (needed <= known->slot_len)
        return true;
    if (bindings->frame_index->schema_revision == UINT64_MAX ||
        !bindings_frame_index_detach(bindings))
        return false;
    BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(
        bindings->frame_index, identity);
    if (!bindings_frame_index_entry_detach(frame))
        return false;
    if (needed > frame->cap) {
        uint32_t cap = frame->cap > 0u ? frame->cap : 8u;
        while (cap < needed) {
            if (cap > UINT32_MAX / 2u) {
                cap = needed;
                break;
            }
            cap *= 2u;
        }
        if ((size_t)cap > SIZE_MAX / sizeof(*frame->values) ||
            (size_t)cap > SIZE_MAX / sizeof(*frame->write_version))
            return false;
        frame->values = cetta_realloc(frame->values, (size_t)cap * sizeof(*frame->values));
        frame->write_version = cetta_realloc(
            frame->write_version, (size_t)cap * sizeof(*frame->write_version));
        frame->cap = cap;
    }
    memset(frame->values + frame->slot_len, 0,
        (size_t)(needed - frame->slot_len) * sizeof(*frame->values));
    memset(frame->write_version + frame->slot_len, 0,
        (size_t)(needed - frame->slot_len) * sizeof(*frame->write_version));
    frame->slot_len = needed;
    bindings->frame_index->schema_revision++;
    if (bindings->frame_index->max_schema_len < needed)
        bindings->frame_index->max_schema_len = needed;
    return true;
}

static bool bindings_frame_index_contains_sources(
        const BindingsFrameIndexEntry *frame,
        const VarId *source_ids, uint32_t source_len) {
    if (!frame || (source_len > 0u && !source_ids))
        return false;
    if (frame->schema->source_ids_contiguous && frame->schema->source_first_id == 1u)
        return source_len == 0u || (source_ids[0] >= 1u &&
            source_ids[source_len - 1u] <= frame->slot_len);
    uint32_t frame_slot = 0u;
    uint32_t source_slot = 0u;
    while (frame_slot < frame->schema->len && source_slot < source_len) {
        if (frame->schema->source_ids[frame_slot] < source_ids[source_slot]) {
            frame_slot++;
        } else if (frame->schema->source_ids[frame_slot] == source_ids[source_slot]) {
            frame_slot++;
            source_slot++;
        } else {
            return false;
        }
    }
    return source_slot == source_len;
}

static BindingsFrameIndex *bindings_frame_index_clone(
        const BindingsFrameIndex *source) {
    BindingsFrameIndex *copy = bindings_frame_index_alloc();
    if (!source)
        return copy;
    copy->schema_revision = source->schema_revision;
    copy->write_clock = source->write_clock;
    copy->bound_value_count = source->bound_value_count;
    copy->private_value_count = source->private_value_count;
    copy->max_schema_len = source->max_schema_len;
    if (source->len > 0u) {
        copy->frames = cetta_malloc(
            (size_t)source->len * sizeof(*copy->frames));
        memset(copy->frames, 0,
               (size_t)source->len * sizeof(*copy->frames));
        copy->cap = source->len;
    }
    for (uint32_t i = 0u; i < source->len; i++) {
        BindingsFrameIndexEntry *to = &copy->frames[i];
        *to = source->frames[i];
        bindings_frame_index_entry_retain(to);
        copy->len++;
    }
    if (!bindings_frame_table_reserve(copy, copy->len))
        abort();
    return copy;
}

bool bindings_frame_index_detach(Bindings *bindings) {
    if (!bindings)
        return false;
    if (!bindings->frame_index) {
        bindings->frame_index = bindings_frame_index_alloc();
        return true;
    }
    uint32_t references = atomic_load_explicit(
        &bindings->frame_index->references, memory_order_acquire);
    if (references == 1u)
        return true;
    BindingsFrameIndex *copy = bindings_frame_index_clone(
        bindings->frame_index);
    bindings_frame_index_release(bindings->frame_index);
    bindings->frame_index = copy;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_DIRECTORY_DETACH);
    return true;
}

bool bindings_frame_index_ensure_presentation_slot(
        Bindings *bindings, BindingsFrameRef ref, uint32_t slot,
        VarId id, SymbolId spelling, Atom *name_key) {
    if (!bindings || !bindings->frame_index ||
        !bindings_frame_ref_is_valid(ref) ||
        ref.identity != var_epoch_suffix(id)) {
        return false;
    }
    BindingsFrameIndexEntry *frame = bindings_frame_index_find_ref(
        bindings->frame_index, ref);
    if (!frame || slot >= frame->slot_len ||
        bindings_frame_slot_source_id(frame, slot) != (VarId)var_base_id(id)) {
        return false;
    }
    if (slot >= frame->schema->len)
        return name_key == NULL;
    SymbolId existing_spelling = frame->schema->spellings[slot];
    Atom *existing_key = frame->schema->name_keys[slot];
    if (existing_key && name_key &&
        !atom_eq(existing_key, name_key)) {
        return false;
    }
    bool needs_spelling = existing_spelling == SYMBOL_ID_NONE &&
        spelling != SYMBOL_ID_NONE;
    bool needs_key = name_key && existing_key != name_key;
    if (!needs_spelling && !needs_key)
        return true;
    if (!bindings_frame_index_detach(bindings))
        return false;
    frame = bindings_frame_index_find_ref(bindings->frame_index, ref);
    if (!frame || slot >= frame->slot_len ||
        bindings_frame_slot_source_id(frame, slot) != (VarId)var_base_id(id))
        return false;
    BindingsFrameSchema *schema = bindings_frame_schema_clone(
        frame->schema);
    if (!schema)
        return false;
    if (needs_spelling)
        schema->spellings[slot] = spelling;
    if (needs_key)
        schema->name_keys[slot] = name_key;
    bindings_frame_schema_release(frame->schema);
    frame->schema = schema;
    if (bindings->frame_index->schema_revision != UINT64_MAX)
        bindings->frame_index->schema_revision++;
    return true;
}

bool bindings_frame_index_ensure_presentation(
        Bindings *bindings, VarId id, SymbolId spelling,
        Atom *name_key) {
    const BindingsFrameIndexEntry *frame = NULL;
    uint32_t slot = 0u;
    if (!bindings_frame_index_find_coordinate(
            bindings, id, &frame, &slot)) {
        return false;
    }
    BindingsFrameRef ref = bindings_frame_ref_from_entry(
        bindings->frame_index, frame);
    return bindings_frame_index_ensure_presentation_slot(
        bindings, ref, slot, id, spelling, name_key);
}

bool bindings_frame_index_replace_slot_value(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *frame,
        uint32_t slot, BindingValue value) {
    if (!index || !frame || slot >= frame->slot_len)
        return false;
    bool current_bound = frame->values[slot].skeleton != NULL;
    bool replacement_bound = value.skeleton != NULL;
    if (current_bound != replacement_bound) {
        if (replacement_bound) {
            if (frame->bound_value_count == UINT32_MAX ||
                index->bound_value_count == SIZE_MAX) {
                return false;
            }
            frame->bound_value_count++;
            index->bound_value_count++;
        } else {
            if (frame->bound_value_count == 0u ||
                index->bound_value_count == 0u) {
                return false;
            }
            frame->bound_value_count--;
            index->bound_value_count--;
        }
    }
    bool was_private = atom_has_private_variant_vars(frame->values[slot].skeleton);
    bool is_private = atom_has_private_variant_vars(value.skeleton);
    if (was_private != is_private) {
        if (is_private) {
            frame->private_value_count++;
            index->private_value_count++;
        } else {
            assert(frame->private_value_count && index->private_value_count);
            frame->private_value_count--;
            index->private_value_count--;
        }
    }
    frame->values[slot] = value;
    return true;
}

bool bindings_frame_index_write_slot_ref(
        Bindings *bindings, BindingsFrameRef ref, uint32_t slot,
        BindingValue value, uint64_t *version_out,
        bool *authority_moved_out) {
    if (version_out)
        *version_out = 0u;
    if (authority_moved_out)
        *authority_moved_out = false;
    if (!bindings || !bindings->frame_index || !value.skeleton ||
        !bindings_frame_ref_is_valid(ref)) {
        return false;
    }
    BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref(bindings->frame_index, ref);
    if (!frame || slot >= frame->slot_len)
        return false;
    bool authority_moved =
        atomic_load_explicit(
            &bindings->frame_index->references,
            memory_order_acquire) != 1u ||
        atomic_load_explicit(
            frame->storage_references,
            memory_order_acquire) != 1u;
    if (!bindings_frame_index_detach(bindings))
        return false;
    /* A detached directory preserves its handle table.  Resolve the stable
     * handle again after copy-on-write instead of recovering the activation
     * from its epoch or relying on a movable array position. */
    frame = bindings_frame_index_find_ref(bindings->frame_index, ref);
    if (!frame || slot >= frame->slot_len)
        return false;
    if (!bindings_frame_index_entry_detach(frame))
        return false;
    if (bindings->frame_index->write_clock == UINT64_MAX ||
        !bindings_frame_index_replace_slot_value(
            bindings->frame_index, frame, slot, value)) {
        return false;
    }
    uint64_t version = ++bindings->frame_index->write_clock;
    frame->write_version[slot] = version;
    if (version_out)
        *version_out = version;
    if (authority_moved_out)
        *authority_moved_out = authority_moved;
    return true;
}

BindingsFrameIndexEntry *bindings_frame_index_detach_entry_ref(
        Bindings *bindings, BindingsFrameRef ref) {
    if (!bindings || !bindings->frame_index ||
        !bindings_frame_ref_is_valid(ref) ||
        !bindings_frame_index_detach(bindings))
        return NULL;
    BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_ref(bindings->frame_index, ref);
    return frame && bindings_frame_index_entry_detach(frame) ? frame : NULL;
}

bool bindings_frame_index_fill_slot(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *frame,
        uint32_t slot, BindingValue value) {
    if (!index || !frame || slot >= frame->slot_len || !value.skeleton ||
        frame->values[slot].skeleton ||
        index->write_clock == UINT64_MAX ||
        !bindings_frame_index_replace_slot_value(index, frame, slot, value))
        return false;
    frame->write_version[slot] = ++index->write_clock;
    return true;
}

static bool bindings_frame_index_write_slot(
        Bindings *bindings, uint32_t epoch, uint32_t slot,
        BindingValue value, uint64_t *version_out,
        bool *authority_moved_out) {
    BindingsFrameIndexEntry *frame = bindings && bindings->frame_index
        ? bindings_frame_index_find_frame(bindings->frame_index, epoch)
        : NULL;
    BindingsFrameRef ref = bindings_frame_ref_from_entry(
        bindings ? bindings->frame_index : NULL, frame);
    return bindings_frame_index_write_slot_ref(
        bindings, ref, slot, value, version_out, authority_moved_out);
}

bool bindings_frame_index_record(
        Bindings *bindings, VarId id, BindingValue value) {
    const BindingsFrameIndexEntry *frame = NULL;
    uint32_t slot = 0u;
    return bindings_frame_index_find_coordinate(
               bindings, id, &frame, &slot) &&
        bindings_frame_index_write_slot(
            bindings, var_epoch_suffix(id), slot,
            value, NULL, NULL);
}

bool bindings_frame_index_mark_activation_boundary_ref(
        Bindings *bindings, BindingsFrameRef ref, uint64_t boundary) {
    if (!bindings || !bindings->frame_index ||
        !bindings_frame_ref_is_valid(ref) ||
        !bindings_frame_index_detach(bindings)) {
        return false;
    }
    BindingsFrameIndexEntry *frame = bindings_frame_index_find_ref(
        bindings->frame_index, ref);
    if (!frame)
        return false;
    frame->activation_write_boundary = boundary;
    frame->has_activation_write_boundary = true;
    return true;
}

bool bindings_frame_index_register_kind(
        Bindings *bindings, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch, bool schema_complete,
        BindingsFrameSchema *prepared_schema,
        BindingsFrameRef *ref_out,
        bool registration_change_proved) {
    if (ref_out)
        *ref_out = (BindingsFrameRef){0};
    if (!bindings || epoch == 0u ||
        (source_len > 0u && !source_ids)) {
        return false;
    }
    if (!prepared_schema) {
        for (uint32_t i = 0u; i < source_len; i++) {
            if (source_ids[i] == VAR_ID_NONE ||
                var_epoch_suffix(source_ids[i]) != 0u ||
                (i > 0u && source_ids[i - 1u] >= source_ids[i])) {
                return false;
            }
        }
    }
    if (source_len == 0u)
        return true;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION);
    const BindingsFrameIndexEntry *known_frame = registration_change_proved
        ? NULL
        : bindings_frame_index_find_frame_const(
              bindings->frame_index, epoch);
    if (((prepared_schema && known_frame && known_frame->schema == prepared_schema) ||
         bindings_frame_index_contains_sources(known_frame, source_ids, source_len)) &&
        (!schema_complete || known_frame->schema_complete)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_KNOWN);
        if (ref_out) {
            *ref_out = bindings_frame_ref_from_entry(
                bindings->frame_index, known_frame);
        }
        return true;
    }
    if (!bindings_frame_index_detach(bindings))
        return false;
    BindingsFrameIndex *index = bindings->frame_index;
    BindingsFrameIndexEntry *existing = bindings_frame_index_find_frame(index, epoch);
    if (existing) {
        if ((existing->schema_complete || existing->slot_len > existing->schema->len) &&
            !bindings_frame_index_contains_sources(
                existing, source_ids, source_len)) {
            /* An activation with manufactured slots has a fixed inventory.
             * Adding a source-name schema here would renumber live slots.
             * Runtime extension and transport use grow_slots instead. */
            return false;
        }
        if (bindings_frame_index_contains_sources(
                existing, source_ids, source_len)) {
            existing->schema_complete =
                existing->schema_complete || schema_complete;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_KNOWN);
            if (ref_out)
                *ref_out = bindings_frame_ref_from_entry(index, existing);
            return true;
        }
        uint32_t old_slot = 0u;
        uint32_t new_slot = 0u;
        uint32_t merged_len = 0u;
        while (old_slot < existing->schema->len || new_slot < source_len) {
            if (new_slot >= source_len ||
                (old_slot < existing->schema->len &&
                 existing->schema->source_ids[old_slot] < source_ids[new_slot])) {
                old_slot++;
            } else if (old_slot >= existing->schema->len ||
                       source_ids[new_slot] < existing->schema->source_ids[old_slot]) {
                new_slot++;
            } else {
                old_slot++;
                new_slot++;
            }
            if (merged_len == UINT32_MAX)
                return false;
            merged_len++;
        }
        if (merged_len == existing->schema->len) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_KNOWN);
            if (ref_out)
                *ref_out = bindings_frame_ref_from_entry(index, existing);
            return true;
        }

        if (!bindings_frame_index_entry_detach(existing))
            return false;
        if (index->schema_revision == UINT64_MAX)
            return false;

        BindingsFrameSchema *merged_schema = bindings_frame_schema_alloc(merged_len);
        VarId *merged_ids = merged_schema->source_ids;
        BindingValue *merged_values = cetta_malloc(
            (size_t)merged_len * sizeof(*merged_values));
        uint64_t *merged_versions = cetta_malloc(
            (size_t)merged_len * sizeof(*merged_versions));
        old_slot = 0u;
        new_slot = 0u;
        uint32_t merged_slot = 0u;
        while (old_slot < existing->schema->len || new_slot < source_len) {
            if (new_slot >= source_len ||
                (old_slot < existing->schema->len &&
                 existing->schema->source_ids[old_slot] < source_ids[new_slot])) {
                merged_ids[merged_slot++] =
                    existing->schema->source_ids[old_slot++];
            } else if (old_slot >= existing->schema->len ||
                       source_ids[new_slot] < existing->schema->source_ids[old_slot]) {
                merged_ids[merged_slot++] = source_ids[new_slot++];
            } else {
                merged_ids[merged_slot++] = source_ids[new_slot];
                old_slot++;
                new_slot++;
            }
        }
        assert(merged_slot == merged_len);
        bindings_frame_schema_publish(merged_schema, merged_len);
        memset(merged_values, 0,
               (size_t)merged_len * sizeof(*merged_values));
        memset(merged_versions, 0,
               (size_t)merged_len * sizeof(*merged_versions));
        for (uint32_t slot = 0u; slot < existing->schema->len; slot++) {
            uint32_t low = 0u;
            uint32_t high = merged_len;
            VarId source_id = existing->schema->source_ids[slot];
            while (low < high) {
                uint32_t middle = low + (high - low) / 2u;
                if (merged_ids[middle] < source_id)
                    low = middle + 1u;
                else
                    high = middle;
            }
            assert(low < merged_len && merged_ids[low] == source_id);
            merged_values[low] = existing->values[slot];
            merged_versions[low] = existing->write_version[slot];
            merged_schema->spellings[low] =
                existing->schema->spellings[slot];
            merged_schema->name_keys[low] =
                existing->schema->name_keys[slot];
        }
        if (prepared_schema) {
            for (uint32_t slot = 0u; slot < prepared_schema->len; slot++) {
                uint32_t merged_position = 0u;
                if (!bindings_frame_schema_find_slot(
                        merged_schema,
                        prepared_schema->source_ids[slot],
                        &merged_position)) {
                    continue;
                }
                if (merged_schema->spellings[merged_position] ==
                        SYMBOL_ID_NONE) {
                    merged_schema->spellings[merged_position] =
                        prepared_schema->spellings[slot];
                }
                if (!merged_schema->name_keys[merged_position]) {
                    merged_schema->name_keys[merged_position] =
                        prepared_schema->name_keys[slot];
                }
            }
        }
        bindings_frame_schema_release(existing->schema);
        free(existing->values);
        free(existing->write_version);
        existing->schema = merged_schema;
        existing->values = merged_values;
        existing->write_version = merged_versions;
        existing->cap = merged_len;
        existing->slot_len = merged_len;
        existing->schema_complete = schema_complete;
        if (index->max_schema_len < merged_len)
            index->max_schema_len = merged_len;
        index->schema_revision++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_EXTENDED);
        if (ref_out)
            *ref_out = bindings_frame_ref_from_entry(index, existing);
        return true;
    }
    uint32_t handle = cetta_frame_handle(epoch);
    if (bindings_frame_table_lookup(index, handle) ||
        index->schema_revision == UINT64_MAX ||
        !bindings_frame_table_reserve(index, index->len + 1u))
        return false;
    if (index->len == index->cap) {
        uint32_t capacity = index->cap ? index->cap * 2u : 8u;
        if (capacity < index->cap ||
            (size_t)capacity > SIZE_MAX / sizeof(*index->frames))
            return false;
        index->frames = cetta_realloc(index->frames,
            (size_t)capacity * sizeof(*index->frames));
        index->cap = capacity;
        bindings_frame_table_relocate(index);
    }
    BindingsFrameIndexEntry *frame = &index->frames[index->len];
    memset(frame, 0, sizeof(*frame));
    if (!bindings_frame_index_initialize_entry(
            index, frame, source_ids, source_len, epoch, prepared_schema))
        return false;
    frame->schema_complete = schema_complete;
    if (index->max_schema_len < frame->schema->len)
        index->max_schema_len = frame->schema->len;
    index->len++;
    bindings_frame_table_insert(index, frame);
    index->schema_revision++;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_COUNT_PEAK,
        index->len);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_NEW);
    if (ref_out)
        *ref_out = bindings_frame_ref_from_entry(index, frame);
    return true;
}

bool bindings_frame_index_register(
        Bindings *bindings, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch) {
    return bindings_frame_index_register_kind(
        bindings, source_ids, source_len, epoch, false, NULL, NULL,
        false);
}

bool bindings_frame_index_merge_schemas(
    Bindings *destination, const Bindings *source) {
    if (!destination || !source)
        return false;
    if (!source->frame_index || source->frame_index->len == 0u)
        return true;
    for (uint32_t i = 0u; i < source->frame_index->len; i++) {
        const BindingsFrameIndexEntry *frame =
            &source->frame_index->frames[i];
        if (!bindings_frame_index_register_kind(
                destination, frame->schema->source_ids, frame->schema->len,
                frame->epoch, frame->schema_complete, frame->schema, NULL,
                false) ||
            (frame->slot_len > frame->schema->len &&
             !bindings_frame_index_grow_slots(destination, frame->epoch, frame->slot_len))) {
            return false;
        }
    }
    return true;
}

bool bindings_register_contextual_frame(
        Bindings *bindings, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch) {
    return bindings_frame_index_register(
        bindings, source_ids, source_len, epoch);
}

bool bindings_register_complete_contextual_frame(
        Bindings *bindings, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch) {
    return bindings_frame_index_register_kind(
        bindings, source_ids, source_len, epoch, true, NULL, NULL,
        false);
}

bool bindings_register_complete_frame_schema(
        Bindings *bindings, BindingsFrameSchema *schema, uint32_t epoch) {
    return schema && bindings_frame_index_register_kind(
        bindings, schema->source_ids, schema->len, epoch, true, schema, NULL,
        false);
}

bool bindings_builder_register_frame_kind(
        BindingsBuilder *builder, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch, bool schema_complete,
        BindingsFrameSchema *prepared_schema,
        BindingsFrameRef *ref_out) {
    if (ref_out)
        *ref_out = (BindingsFrameRef){0};
    if (!builder || epoch == 0u ||
        (source_len > 0u && !source_ids)) {
        return false;
    }
    if (!prepared_schema) {
        for (uint32_t i = 0u; i < source_len; i++) {
            if (source_ids[i] == VAR_ID_NONE ||
                var_epoch_suffix(source_ids[i]) != 0u ||
                (i > 0u && source_ids[i - 1u] >= source_ids[i])) {
                return false;
            }
        }
    }
    const BindingsFrameIndexEntry *known =
        bindings_frame_index_find_frame_const(
            builder->current.frame_index, epoch);
    bool changes = source_len > 0u &&
        !((((prepared_schema && known &&
             known->schema == prepared_schema) ||
            bindings_frame_index_contains_sources(
                known, source_ids, source_len)) &&
           (!schema_complete || (known && known->schema_complete))));
    if (!changes) {
        if (source_len == 0u)
            return true;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_KNOWN);
        if (ref_out) {
            *ref_out = bindings_frame_ref_from_entry(
                builder->current.frame_index, known);
        }
        return !ref_out || bindings_frame_ref_is_valid(*ref_out);
    }
    if (builder->frame_registration_undo_len == UINT32_MAX ||
        builder->trail_len == UINT32_MAX ||
        !bindings_builder_trail_reserve(
            builder, builder->trail_len + 1u) ||
        (bindings_prime_present(&builder->current) &&
         (builder->prime_trail_len == UINT32_MAX ||
          !bindings_builder_prime_trail_reserve(
              builder, builder->prime_trail_len + 1u)))) {
        return false;
    }
    if (builder->frame_registration_undo_len ==
            builder->frame_registration_undo_cap) {
        uint32_t cap = builder->frame_registration_undo_cap
            ? builder->frame_registration_undo_cap * 2u : 8u;
        if (cap < builder->frame_registration_undo_cap ||
            (size_t)cap >
                SIZE_MAX / sizeof(*builder->frame_registration_undo)) {
            return false;
        }
        builder->frame_registration_undo =
            builder->frame_registration_undo
                ? cetta_realloc(
                      builder->frame_registration_undo,
                      (size_t)cap *
                          sizeof(*builder->frame_registration_undo))
                : cetta_malloc(
                      (size_t)cap *
                          sizeof(*builder->frame_registration_undo));
        builder->frame_registration_undo_cap = cap;
    }
    uint32_t undo_start = builder->frame_registration_undo_len;
    BindingsFrameRegistrationUndoEntry undo = {
        .epoch = epoch,
        .trail_mark = builder->trail_len,
        .previous_schema = known ? known->schema : NULL,
        .frame_existed = known != NULL,
        .previous_schema_complete = known && known->schema_complete,
        .previous_slot_len = known ? known->slot_len : 0u,
    };
    bindings_frame_schema_retain(undo.previous_schema);
    builder->frame_registration_undo[
        builder->frame_registration_undo_len++] = undo;
    if (!bindings_frame_index_register_kind(
            &builder->current, source_ids, source_len, epoch,
            schema_complete, prepared_schema, ref_out, true)) {
        /* Registration may fail after publishing an extended schema (for
         * example at the monotone-version boundary). Restore through the
         * same inverse record used by branch rollback, rather than leaving
         * a partially changed frame directory behind. */
        (void)bindings_builder_restore_frame_registration_count(
            builder, undo_start);
        return false;
    }
    builder->frame_registration_save_barrier = true;
    return true;
}

bool bindings_builder_register_frame(
        BindingsBuilder *builder, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch) {
    return bindings_builder_register_frame_kind(
        builder, source_ids, source_len, epoch, false, NULL, NULL);
}

bool bindings_builder_register_frame_schema(
        BindingsBuilder *builder, BindingsFrameSchema *schema,
        uint32_t epoch) {
    return schema && bindings_builder_register_frame_kind(
        builder, schema->source_ids, schema->len, epoch, false, schema, NULL);
}

bool bindings_builder_register_complete_frame_schema(
        BindingsBuilder *builder, BindingsFrameSchema *schema,
        uint32_t epoch) {
    return schema && bindings_builder_register_frame_kind(
        builder, schema->source_ids, schema->len, epoch, true, schema, NULL);
}

bool bindings_builder_register_complete_frame_schema_ref(
        BindingsBuilder *builder, BindingsFrameSchema *schema,
        uint32_t epoch, BindingsFrameRef *ref_out) {
    return schema && ref_out && bindings_builder_register_frame_kind(
        builder, schema->source_ids, schema->len, epoch, true, schema,
        ref_out);
}

bool bindings_builder_register_contextual_frame(
        BindingsBuilder *builder, const VarId *source_ids,
        uint32_t source_len, uint32_t epoch) {
    return bindings_builder_register_frame(
        builder, source_ids, source_len, epoch);
}

bool bindings_builder_merge_frame_schemas(
        BindingsBuilder *builder, const Bindings *source) {
    if (!builder || !source)
        return false;
    if (source->frame_index) {
        for (uint32_t index = 0u;
             index < source->frame_index->len; index++) {
            const BindingsFrameIndexEntry *frame =
                &source->frame_index->frames[index];
            if (!bindings_builder_register_frame_kind(
                    builder, frame->schema->source_ids,
                    frame->schema->len, frame->epoch,
                    frame->schema_complete, frame->schema, NULL) ||
                (frame->slot_len > frame->schema->len &&
                 !bindings_frame_index_grow_slots(
                     &builder->current, frame->epoch, frame->slot_len))) {
                return false;
            }
        }
    }
    return true;
}

Atom *bindings_builder_new_variable(BindingsBuilder *builder, Arena *arena,
                                    CettaFrameIdentity identity) {
    if (!builder || !arena || identity == 0u)
        return NULL;
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
        builder->current.frame_index, identity);
    if (frame && (!frame->schema->source_ids_contiguous ||
                  frame->schema->source_first_id != 1u))
        return NULL;
    uint32_t slot = 0u;
    if (!cetta_frame_identity_new_slot(identity, frame ? frame->slot_len : 0u, &slot))
        return NULL;
    Atom *variable = atom_var_with_id(arena, "__petta_machine", var_epoch_id(slot, identity));
    if (!variable)
        return NULL;
    if (!frame) {
        VarId first = 1u;
        BindingsFrameSchema *schema = bindings_frame_schema_new(&first, 1u);
        if (!schema)
            return NULL;
        schema->spellings[0] = variable->sym_id;
        bool installed = bindings_builder_register_frame_kind(
            builder, &first, 1u, identity, false, schema, NULL);
        bindings_frame_schema_release(schema);
        if (!installed)
            return NULL;
    }
    return bindings_frame_index_grow_slots(&builder->current, identity, slot)
        ? variable : NULL;
}

/*
 * An image that does not run an activation registers only the identifiers it
 * holds.  The dense inventory of a frame belongs to the builder that mints its
 * slots (`bindings_builder_frame_index_ensure_id`); copying that whole extent
 * into every published or projected image is what made each answer own the
 * root activation's entire hole inventory.  A sparse schema here is the
 * captured projection of the frame: correct for every lookup the image can
 * make, and proportional to what the image references.
 */
bool bindings_frame_index_ensure_id(
        Bindings *bindings, VarId id) {
    if (!bindings || id == VAR_ID_NONE)
        return false;
    uint32_t epoch = var_epoch_suffix(id);
    if (epoch == 0u || bindings_frame_index_owns_id(bindings, id))
        return true;
    VarId source_id = (VarId)var_base_id(id);
    if (bindings_frame_index_grow_slots(bindings, epoch, (uint32_t)source_id))
        return true;
    return bindings_frame_index_register(
        bindings, &source_id, 1u, epoch);
}

bool bindings_builder_frame_index_ensure_id(
        BindingsBuilder *builder, VarId id) {
    if (!builder || id == VAR_ID_NONE)
        return false;
    uint32_t epoch = var_epoch_suffix(id);
    if (epoch == 0u ||
        bindings_frame_index_owns_id(&builder->current, id)) {
        return true;
    }
    VarId source_id = (VarId)var_base_id(id);
    if (bindings_frame_index_grow_slots(&builder->current, epoch, (uint32_t)source_id))
        return true;
    uint32_t extent = 0u;
    if (cetta_frame_identity_slot_count(epoch, &extent) &&
        source_id <= extent) {
        BindingsFrameSchema *schema = bindings_frame_schema_dense(extent);
        if (!schema)
            return false;
        bool registered = bindings_builder_register_frame_kind(
            builder, schema->source_ids, extent, epoch, false, schema, NULL);
        bindings_frame_schema_release(schema);
        return registered;
    }
    return bindings_builder_register_frame(
        builder, &source_id, 1u, epoch);
}

bool bindings_builder_restore_frame_values(
        BindingsBuilder *bb, uint32_t trail_mark) {
    if (!bb)
        return false;
    while (bb->frame_undo_len > 0u &&
           bb->frame_undo[bb->frame_undo_len - 1u].trail_mark >= trail_mark) {
        const BindingsFrameUndoEntry *undo =
            &bb->frame_undo[bb->frame_undo_len - 1u];
        BindingsFrameRef ref = undo->frame_ref;
        uint32_t slot = undo->slot;
        const BindingsFrameIndexEntry *current_frame =
            bindings_frame_index_find_ref_const(
                bb->current.frame_index, ref);
        if (!current_frame || slot >= current_frame->slot_len ||
            bindings_frame_slot_source_id(current_frame, slot) != undo->source_id) {
            VarId id = var_epoch_id(
                (VarId)undo->source_id, ref.identity);
            if (!bindings_frame_index_find_coordinate(
                    &bb->current, id, &current_frame, &slot)) {
                return false;
            }
            ref = bindings_frame_ref_from_entry(
                bb->current.frame_index, current_frame);
        }
        if (!bindings_frame_ref_is_valid(ref) ||
            current_frame->write_version[slot] !=
                undo->written_write_version) {
            return false;
        }
        if (!bindings_frame_index_detach(&bb->current))
            return false;
        BindingsFrameIndexEntry *frame = bindings_frame_index_find_ref(
            bb->current.frame_index, ref);
        if (!frame || slot >= frame->slot_len ||
            bindings_frame_slot_source_id(frame, slot) != undo->source_id ||
            !bindings_frame_index_entry_detach(frame)) {
            return false;
        }
        if (!bindings_frame_index_replace_slot_value(
                bb->current.frame_index, frame, slot,
                undo->previous_value)) {
            return false;
        }
        frame->write_version[slot] = undo->previous_write_version;
        bb->frame_undo_len--;
    }
    return true;
}

static bool bindings_builder_restore_frame_registration_count(
        BindingsBuilder *bb, uint32_t target_len) {
    if (!bb || target_len > bb->frame_registration_undo_len)
        return false;
    while (bb->frame_registration_undo_len > target_len) {
        BindingsFrameRegistrationUndoEntry *undo =
            &bb->frame_registration_undo[
                bb->frame_registration_undo_len - 1u];
        if (!bindings_frame_index_detach(&bb->current))
            return false;
        BindingsFrameIndex *index = bb->current.frame_index;
        BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame(index, undo->epoch);
        if (!frame)
            return false;
        if (!undo->frame_existed) {
            uint32_t position = (uint32_t)(frame - index->frames);
            if (!bindings_frame_index_recycle_entry(index, frame))
                return false;
            index->len--;
            if (position < index->len) {
                *frame = index->frames[index->len];
                bindings_frame_table_relink(index, frame);
            }
            memset(&index->frames[index->len], 0, sizeof(*index->frames));
        } else {
            if (!undo->previous_schema ||
                !bindings_frame_index_entry_detach(frame)) {
                return false;
            }
            BindingsFrameSchema *current_schema = frame->schema;
            uint32_t retained_bound_value_count = 0u;
            uint32_t retained_private_value_count = 0u;
            for (uint32_t previous_slot = 0u;
                 previous_slot < undo->previous_slot_len;
                 previous_slot++) {
                uint32_t current_slot = 0u;
                VarId source_id = previous_slot < undo->previous_schema->len
                    ? undo->previous_schema->source_ids[previous_slot]
                    : (VarId)previous_slot + 1u;
                if (!bindings_frame_index_find_slot(
                        frame, source_id,
                        &current_slot)) {
                    return false;
                }
                frame->values[previous_slot] =
                    frame->values[current_slot];
                frame->write_version[previous_slot] =
                    frame->write_version[current_slot];
                if (frame->values[previous_slot].skeleton)
                    retained_bound_value_count++;
                if (atom_has_private_variant_vars(frame->values[previous_slot].skeleton))
                    retained_private_value_count++;
            }
            if (index->bound_value_count < frame->bound_value_count)
                return false;
            index->bound_value_count -= frame->bound_value_count;
            index->bound_value_count += retained_bound_value_count;
            frame->bound_value_count = retained_bound_value_count;
            index->private_value_count -= frame->private_value_count;
            index->private_value_count += retained_private_value_count;
            frame->private_value_count = retained_private_value_count;
            frame->schema = undo->previous_schema;
            undo->previous_schema = NULL;
            frame->schema_complete = undo->previous_schema_complete;
            frame->slot_len = undo->previous_slot_len;
            bindings_frame_schema_release(current_schema);
        }
        if (index->schema_revision == UINT64_MAX)
            return false;
        index->schema_revision++;
        bindings_frame_schema_release(undo->previous_schema);
        bb->frame_registration_undo_len--;
    }
    bb->frame_registration_save_barrier =
        bb->frame_registration_undo_len > 0u &&
        bb->frame_registration_undo[
            bb->frame_registration_undo_len - 1u].trail_mark ==
            bb->trail_len;
    return true;
}

bool bindings_builder_restore_frame_registrations(
        BindingsBuilder *bb, uint32_t trail_mark) {
    if (!bb)
        return false;
    uint32_t target_len = bb->frame_registration_undo_len;
    while (target_len > 0u &&
           bb->frame_registration_undo[target_len - 1u].trail_mark >=
               trail_mark) {
        target_len--;
    }
    return bindings_builder_restore_frame_registration_count(
        bb, target_len);
}

void bindings_builder_discard_frame_registration_history(
        BindingsBuilder *bb, bool release_storage) {
    if (!bb)
        return;
    for (uint32_t i = 0u;
         i < bb->frame_registration_undo_len; i++) {
        bindings_frame_schema_release(
            bb->frame_registration_undo[i].previous_schema);
    }
    bb->frame_registration_undo_len = 0u;
    bb->frame_registration_save_barrier = false;
    if (release_storage) {
        free(bb->frame_registration_undo);
        bb->frame_registration_undo = NULL;
        bb->frame_registration_undo_cap = 0u;
    }
}
