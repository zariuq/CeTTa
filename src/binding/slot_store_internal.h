#ifndef CETTA_BINDING_SLOT_STORE_INTERNAL_H
#define CETTA_BINDING_SLOT_STORE_INTERNAL_H

/* Private storage interface shared with closure interpretation and unification.
 * Each frame owns its slot values; the handle table only addresses those frames.
 * Syntax inventories are immutable and are shared independently of versions. */
#include "../match.h"
#include "../stats.h"
#include "frame_schema_internal.h"
#include <assert.h>

#define BINDINGS_DIRECT_FRAME_SLOT_CAP 64u

typedef struct BindingsFrameIndexEntry {
    CettaFrameIdentity epoch;
    bool owns_identity;
    BindingsFrameSchema *schema;
    _Atomic uint32_t *storage_references;
    BindingValue *values;
    uint64_t *write_version;
    uint32_t bound_value_count;
    uint32_t private_value_count;
    uint32_t slot_len; /* logical coordinates, including runtime additions */
    uint32_t cap;      /* physical backing; it may cover only a prefix */
    uint64_t activation_write_boundary;
    bool has_activation_write_boundary;
    /* True only when the schema was admitted from the complete variable
     * inventory of the activation that owns this epoch.  Subterm discovery
     * may extend an incomplete schema, but a complete schema is closed. */
    bool schema_complete;
} BindingsFrameIndexEntry;

/* The caller has admitted this coordinate through the logical schema. A
 * slot outside the backing prefix is unbound with version zero; writing it
 * grows the prefix. Checked builds validate that entrance invariant. */
static inline BindingValue bindings_frame_slot_value(
        const BindingsFrameIndexEntry *entry, uint32_t slot) {
#if CETTA_PROVENANCE_ASSERT
    assert(entry && slot < entry->slot_len);
#endif
    return __builtin_expect(slot < entry->cap, 1)
        ? entry->values[slot] : binding_value_from_atom(NULL);
}

static inline uint64_t bindings_frame_slot_version(
        const BindingsFrameIndexEntry *entry, uint32_t slot) {
#if CETTA_PROVENANCE_ASSERT
    assert(entry && slot < entry->slot_len);
#endif
    return __builtin_expect(slot < entry->cap, 1)
        ? entry->write_version[slot] : 0u;
}

struct BindingsFrameUndoEntry {
    BindingValue previous_value;
    uint64_t previous_write_version;
    uint64_t written_write_version;
    BindingsFrameRef frame_ref;
    uint64_t source_id;
    uint32_t slot;
    uint32_t trail_mark;
};

struct BindingsFrameRegistrationUndoEntry {
    CettaFrameIdentity epoch;
    uint32_t trail_mark;
    BindingsFrameSchema *previous_schema;
    bool frame_existed;
    bool previous_schema_complete;
    uint32_t previous_slot_len;
};

/*
 * A contextual frame partitions the binding domain before lookup.  Each
 * slot stores the authoritative current value and its monotone write version;
 * an empty value means that the registered metavariable is unbound.  The
 * generic VarId hash never decides a coordinate owned by one of these frames.
 *
 * Captured binding images share the directory and each frame's slot storage.
 * A write detaches the directory and only the changed frame, matching the
 * copy-on-write image in BindingVersions.lean. The builder's LIFO undo log
 * records framed update history without manufacturing Binding rows.
 */
struct BindingsFrameIndex {
    _Atomic uint32_t references;
    uint64_t schema_revision;
    /* Monotone logical time for authoritative slot writes.  Unlike row
     * coordinates, this remains meaningful when a framed value has no
     * generic Binding row.  Rollback restores each slot's prior version but
     * never rewinds this clock, so a later write cannot be confused with a
     * discarded branch. */
    uint64_t write_clock;
    /* Exact summary of nonempty authoritative slots across the directory.
     * It changes with the slot value and its undo, so the dereference fast
     * path never has to rediscover whether any frame contains a binding. */
    size_t bound_value_count;
    size_t private_value_count;
    /* Conservative O(1) bound for variable-only dereference walks.  The
     * product of this value and the live frame count bounds the number of
     * frame slots without rescanning every authoritative frame at each
     * matcher coordinate.  It may remain high after a large frame leaves,
     * but it must never be lower than any live schema length. */
    uint32_t max_schema_len;
    BindingsFrameIndexEntry *frames;
    uint32_t len;
    uint32_t cap;
    /* The identity selects this table: an open-addressing map from frame
     * handle to entry, sized by the number of frames the directory holds
     * and never by the handle values themselves.  A directory is cloned for
     * every branch that writes it, so its map must cost O(frames), not
     * O(largest handle ever minted).  `frames` stays the unordered owning
     * vector for enumeration; the map points into it and holds no values.
     * Capacity is zero or a power of two, at least twice the frame count. */
    BindingsFrameIndexEntry **handle_table;
    uint32_t handle_table_mask;
    uint32_t handle_table_shift;
    BindingsFrameIndexEntry *spares;
    uint32_t spare_len;
    uint32_t spare_cap;
};


bool bindings_frame_index_entry_detach(
        BindingsFrameIndexEntry *entry);
/* Retained immutable relocation images. Replacing only the backing payload
 * leaves each view's epoch, counts, activation boundary and schema flags intact.
 * The caller establishes equal slot inventories and unchanged logical values. */
void bindings_frame_index_entry_hold(BindingsFrameIndexEntry *image,
                                     const BindingsFrameIndexEntry *source);
void bindings_frame_index_entry_drop(BindingsFrameIndexEntry *image);
void bindings_frame_index_entry_share_payload(BindingsFrameIndexEntry *view,
                                              const BindingsFrameIndexEntry *image);

void bindings_frame_index_retain(BindingsFrameIndex *index);

void bindings_frame_index_release(BindingsFrameIndex *index);

bool bindings_frame_index_grow_slots(Bindings *bindings,
        CettaFrameIdentity identity, uint32_t needed);

bool bindings_frame_index_detach(Bindings *bindings);

bool bindings_frame_index_ensure_presentation_slot(
        Bindings *bindings, BindingsFrameRef ref, uint32_t slot,
        VarId id, SymbolId spelling, Atom *name_key);

bool bindings_frame_index_ensure_presentation(
        Bindings *bindings, VarId id, SymbolId spelling,
        Atom *name_key);

bool bindings_frame_index_replace_slot_value(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *frame,
        uint32_t slot, BindingValue value);

/* Copy a slot's chronology without requiring its implicit unbound tail to
 * have physical backing. A nonzero version acquires storage before writing. */
bool bindings_frame_index_replace_slot_version(
        BindingsFrameIndexEntry *frame, uint32_t slot, uint64_t version);

bool bindings_frame_index_write_slot_ref(
        Bindings *bindings, BindingsFrameRef ref, uint32_t slot,
        BindingValue value, uint64_t *version_out,
        bool *authority_moved_out);

/* A short-lived focus already resolved in bindings->frame_index. No operation
 * that can replace the directory, move entries, or grow a schema may occur
 * between resolving frame and this call. Copy-on-write inside the operation
 * reacquires the entry by its generational identity. The focus never escapes
 * to a continuation or a captured image. */
bool bindings_frame_index_write_resolved_slot(
        Bindings *bindings, BindingsFrameIndexEntry *frame, uint32_t slot,
        BindingValue value, uint64_t *version_out,
        bool *authority_moved_out);

/* The entry named by `ref`, owned by this image and ready for writes. */
BindingsFrameIndexEntry *bindings_frame_index_detach_entry_ref(
        Bindings *bindings, BindingsFrameRef ref);

/* Write an unbound slot of a detached entry with a fresh version.  Only for
 * an entry registered after the caller's rollback mark: undoing that
 * registration recycles the entry with its values, so the write keeps no
 * inverse record of its own. */
bool bindings_frame_index_fill_slot(
        BindingsFrameIndex *index, BindingsFrameIndexEntry *frame,
        uint32_t slot, BindingValue value);

bool bindings_frame_index_record(
        Bindings *bindings, VarId id, BindingValue value);

bool bindings_frame_index_mark_activation_boundary_ref(
        Bindings *bindings, BindingsFrameRef ref, uint64_t boundary);

bool bindings_frame_index_register_kind(
        Bindings *bindings, const VarId *source_ids,
        uint32_t source_len, CettaFrameIdentity epoch, bool schema_complete,
        BindingsFrameSchema *prepared_schema,
        BindingsFrameRef *ref_out,
        bool registration_change_proved);

bool bindings_frame_index_register(
        Bindings *bindings, const VarId *source_ids,
        uint32_t source_len, CettaFrameIdentity epoch);

bool bindings_frame_index_merge_schemas(
    Bindings *destination, const Bindings *source);

bool bindings_builder_register_frame_kind(
        BindingsBuilder *builder, const VarId *source_ids,
        uint32_t source_len, CettaFrameIdentity epoch, bool schema_complete,
        BindingsFrameSchema *prepared_schema,
        BindingsFrameRef *ref_out);

bool bindings_builder_register_frame(
        BindingsBuilder *builder, const VarId *source_ids,
        uint32_t source_len, CettaFrameIdentity epoch);

bool bindings_builder_register_complete_frame_schema_ref(
        BindingsBuilder *builder, BindingsFrameSchema *schema,
        CettaFrameIdentity epoch, BindingsFrameRef *ref_out);

bool bindings_builder_merge_frame_schemas(
        BindingsBuilder *builder, const Bindings *source);

bool bindings_frame_index_ensure_id(
        Bindings *bindings, VarId id);

bool bindings_builder_frame_index_ensure_id(
        BindingsBuilder *builder, VarId id);

bool bindings_builder_restore_frame_values(
        BindingsBuilder *bb, uint32_t trail_mark);

bool bindings_builder_restore_frame_registrations(
        BindingsBuilder *bb, uint32_t trail_mark);

void bindings_builder_discard_frame_registration_history(
        BindingsBuilder *bb, bool release_storage);

static inline uint32_t bindings_frame_table_home(
        const BindingsFrameIndex *index, uint32_t handle) {
    return (handle * UINT32_C(0x9E3779B1)) >> index->handle_table_shift;
}

/* The entry holding `handle`, or NULL.  Linear probing over a table kept at
 * most half full, so the expected probe length is below two. */
static inline BindingsFrameIndexEntry *bindings_frame_table_lookup(
        const BindingsFrameIndex *index, uint32_t handle) {
    if (!index->handle_table)
        return NULL;
    uint32_t mask = index->handle_table_mask;
    uint32_t slot = bindings_frame_table_home(index, handle);
    for (;;) {
        BindingsFrameIndexEntry *entry = index->handle_table[slot];
        if (!entry)
            return NULL;
        if (cetta_frame_handle(entry->epoch) == handle)
            return entry;
        slot = (slot + 1u) & mask;
    }
}

static inline BindingsFrameIndexEntry *bindings_frame_index_find_ref(
        BindingsFrameIndex *index, BindingsFrameRef ref) {
    if (!index || !bindings_frame_ref_is_valid(ref))
        return NULL;
    BindingsFrameIndexEntry *entry = bindings_frame_table_lookup(
        index, cetta_frame_handle(ref.identity));
    return entry && cetta_frame_identity_eq(entry->epoch, ref.identity)
        ? entry : NULL;
}

static inline const BindingsFrameIndexEntry *bindings_frame_index_find_ref_const(
        const BindingsFrameIndex *index, BindingsFrameRef ref) {
    return bindings_frame_index_find_ref((BindingsFrameIndex *)index, ref);
}

static inline BindingsFrameRef bindings_frame_ref_from_entry(
        const BindingsFrameIndex *index,
        const BindingsFrameIndexEntry *entry) {
    BindingsFrameRef ref = {
        .identity = entry ? entry->epoch : CETTA_FRAME_IDENTITY_NONE};
    return bindings_frame_index_find_ref_const(index, ref) == entry
        ? ref : (BindingsFrameRef){0};
}

static inline BindingsFrameIndexEntry *bindings_frame_index_find_frame(
        BindingsFrameIndex *index, CettaFrameIdentity identity) {
    return bindings_frame_index_find_ref(index,
        (BindingsFrameRef){.identity = identity});
}

static inline const BindingsFrameIndexEntry *bindings_frame_index_find_frame_const(
        const BindingsFrameIndex *index, CettaFrameIdentity epoch) {
    return bindings_frame_index_find_frame(
        (BindingsFrameIndex *)index, epoch);
}

static inline VarId bindings_frame_slot_source_id(const BindingsFrameIndexEntry *frame,
                                          uint32_t slot) {
    return slot < frame->schema->len ? frame->schema->source_ids[slot] : var_id_from_base((uint64_t)slot + 1u);
}

static inline SymbolId bindings_frame_slot_spelling(const BindingsFrameIndexEntry *frame,
                                           uint32_t slot) {
    return slot < frame->schema->len ||
        (slot < frame->schema->cap && frame->schema->spellings[slot] != SYMBOL_ID_NONE)
        ? frame->schema->spellings[slot]
        : symbol_intern_cstr(g_symbols, "__petta_machine");
}

static inline Atom *bindings_frame_slot_name_key(const BindingsFrameIndexEntry *frame,
                                        uint32_t slot) {
    return slot < frame->schema->cap ? frame->schema->name_keys[slot] : NULL;
}

static inline bool bindings_frame_index_find_slot(
        const BindingsFrameIndexEntry *frame, VarId source_id,
        uint32_t *slot_out) {
    if (!frame || !slot_out || var_id_is_none(source_id))
        return false;
    /* Source ids carry no activation: slots are keyed by their bases. */
    if (frame->schema->source_ids_contiguous) {
        if (source_id.base < frame->schema->source_first_id.base)
            return false;
        uint64_t offset = source_id.base - frame->schema->source_first_id.base;
        if (offset >= frame->slot_len)
            return false;
        *slot_out = (uint32_t)offset;
        return true;
    }
    uint32_t low = 0u;
    uint32_t high = frame->schema->len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (frame->schema->source_ids[middle].base < source_id.base)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= frame->schema->len || !var_id_eq(frame->schema->source_ids[low], source_id))
        return false;
    *slot_out = low;
    return true;
}

static inline bool bindings_frame_index_find_coordinate(
        const Bindings *bindings, VarId id,
        const BindingsFrameIndexEntry **frame_out,
        uint32_t *slot_out) {
    if (frame_out)
        *frame_out = NULL;
    if (slot_out)
        *slot_out = 0u;
    if (!bindings || !bindings->frame_index || !frame_out || !slot_out)
        return false;
    CettaFrameIdentity epoch = var_epoch_suffix(id);
    if (cetta_frame_identity_is_none(epoch))
        return false;
    const BindingsFrameIndexEntry *frame =
        bindings_frame_index_find_frame_const(
            bindings->frame_index, epoch);
    if (!frame)
        return false;
    uint32_t slot = 0u;
    if (!bindings_frame_index_find_slot(
            frame, var_source_id(id), &slot)) {
        /* One activation may contain authored source slots and separately
         * minted destination holes.  Until a hole is explicitly admitted to
         * this frame, the epoch alone cannot assign its coordinate to the
         * source-slot table. */
        return false;
    }
    *frame_out = frame;
    *slot_out = slot;
    return true;
}

static inline bool bindings_frame_index_lookup(
        const Bindings *bindings, VarId id,
        bool *known_out) {
    if (known_out)
        *known_out = false;
    if (!bindings || !known_out || !bindings->frame_index)
        return false;
    const BindingsFrameIndexEntry *frame = NULL;
    uint32_t slot = 0u;
    *known_out = bindings_frame_index_find_coordinate(
        bindings, id, &frame, &slot);
    return true;
}

static inline bool bindings_frame_index_owns_id(
        const Bindings *bindings, VarId id) {
    const BindingsFrameIndexEntry *frame = NULL;
    uint32_t slot = 0u;
    return bindings_frame_index_find_coordinate(
        bindings, id, &frame, &slot);
}

static inline bool bindings_frame_index_epoch_complete(
        const Bindings *bindings, CettaFrameIdentity epoch) {
    const BindingsFrameIndexEntry *frame = bindings
        ? bindings_frame_index_find_frame_const(
              bindings->frame_index, epoch)
        : NULL;
    return frame && frame->schema_complete;
}

/* Common branch checkpoint allocation; Prime payload interpretation stays with
 * the binding builder. A frame registration reserves its rollback checkpoint. */
bool bindings_builder_trail_reserve(BindingsBuilder *builder, uint32_t needed);
bool bindings_builder_prime_trail_reserve(BindingsBuilder *builder, uint32_t needed);

#endif
