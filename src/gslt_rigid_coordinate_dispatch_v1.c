#include "gslt_rigid_coordinate_dispatch_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaGsltRigidKeyV1 key;
    uint32_t count;
    bool occupied;
} CettaGsltRigidCoordinateCountSlotV1;

static uint64_t rigid_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint64_t rigid_mul_sat(uint64_t left, uint64_t right) {
    return left != 0u && right > UINT64_MAX / left
        ? UINT64_MAX : left * right;
}

static bool rigid_key_equal(
    const CettaGsltRigidKeyV1 *left,
    const CettaGsltRigidKeyV1 *right) {
    if (!left || !right || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case CETTA_GSLT_RIGID_KEY_SYMBOL_V1:
        return left->symbol == right->symbol;
    case CETTA_GSLT_RIGID_KEY_STRING_V1:
        return left->text && right->text &&
            strcmp(left->text, right->text) == 0;
    case CETTA_GSLT_RIGID_KEY_INTEGER_V1:
        return left->integer == right->integer;
    case CETTA_GSLT_RIGID_KEY_APPLICATION_V1:
        return left->symbol == right->symbol &&
            left->arity == right->arity;
    default:
        return false;
    }
}

static uint64_t rigid_key_hash(const CettaGsltRigidKeyV1 *key) {
    uint64_t value = key ? (uint64_t)key->kind : 0u;
    if (!key)
        return value;
    switch (key->kind) {
    case CETTA_GSLT_RIGID_KEY_SYMBOL_V1:
        value ^= (uint64_t)key->symbol << 17u;
        break;
    case CETTA_GSLT_RIGID_KEY_STRING_V1:
        if (key->text) {
            const unsigned char *cursor =
                (const unsigned char *)key->text;
            while (*cursor) {
                value ^= *cursor++;
                value *= UINT64_C(1099511628211);
            }
        }
        break;
    case CETTA_GSLT_RIGID_KEY_INTEGER_V1:
        value ^= (uint64_t)key->integer;
        break;
    case CETTA_GSLT_RIGID_KEY_APPLICATION_V1:
        value ^= ((uint64_t)key->symbol << 32u) | key->arity;
        break;
    default:
        break;
    }
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

void cetta_gslt_rigid_coordinate_scratch_init_v1(
    CettaGsltRigidCoordinateScratchV1 *scratch) {
    if (scratch)
        memset(scratch, 0, sizeof(*scratch));
}

void cetta_gslt_rigid_coordinate_scratch_free_v1(
    CettaGsltRigidCoordinateScratchV1 *scratch) {
    if (!scratch)
        return;
    free(scratch->slots);
    cetta_gslt_rigid_coordinate_scratch_init_v1(scratch);
}

void cetta_gslt_rigid_coordinate_index_init_v1(
    CettaGsltRigidCoordinateIndexV1 *index) {
    if (index)
        memset(index, 0, sizeof(*index));
}

void cetta_gslt_rigid_coordinate_index_free_v1(
    CettaGsltRigidCoordinateIndexV1 *index) {
    if (!index)
        return;
    for (uint32_t group = 0u; group < index->group_count; group++)
        free(index->groups[group].positions);
    free(index->groups);
    free(index->wildcard_positions);
    cetta_gslt_rigid_coordinate_index_init_v1(index);
}

static bool rigid_scratch_reserve(
    CettaGsltRigidCoordinateScratchV1 *scratch,
    uint32_t occurrence_count) {
    size_t required;
    size_t capacity = 1u;
    void *replacement;

    if (!scratch || (size_t)occurrence_count > SIZE_MAX / 2u)
        return false;
    required = (size_t)occurrence_count * 2u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u)
            return false;
        capacity *= 2u;
    }
    if (capacity <= scratch->slot_capacity)
        return true;
    if (capacity > SIZE_MAX / sizeof(CettaGsltRigidCoordinateCountSlotV1))
        return false;
    replacement = realloc(
        scratch->slots,
        capacity * sizeof(CettaGsltRigidCoordinateCountSlotV1));
    if (!replacement)
        return false;
    scratch->slots = replacement;
    scratch->slot_capacity = capacity;
    scratch->allocation_count++;
    return true;
}

static bool rigid_u32_append(
    uint32_t **values, uint32_t *length, uint32_t *capacity,
    uint32_t value) {
    uint32_t next_capacity;
    uint32_t *replacement;

    if (!values || !length || !capacity)
        return false;
    if (*length < *capacity) {
        (*values)[(*length)++] = value;
        return true;
    }
    next_capacity = *capacity ? *capacity * 2u : 4u;
    if (next_capacity < *capacity ||
        (size_t)next_capacity > SIZE_MAX / sizeof(*replacement))
        return false;
    replacement = realloc(
        *values, (size_t)next_capacity * sizeof(*replacement));
    if (!replacement)
        return false;
    *values = replacement;
    *capacity = next_capacity;
    (*values)[(*length)++] = value;
    return true;
}

static CettaGsltRigidCoordinateGroupV1 *rigid_group(
    CettaGsltRigidCoordinateIndexV1 *index,
    const CettaGsltRigidKeyV1 *key, bool create) {
    CettaGsltRigidCoordinateGroupV1 *replacement;
    uint32_t next_capacity;

    for (uint32_t group = 0u; group < index->group_count; group++)
        if (rigid_key_equal(&index->groups[group].key, key))
            return &index->groups[group];
    if (!create)
        return NULL;
    if (index->group_count == index->group_capacity) {
        next_capacity = index->group_capacity
            ? index->group_capacity * 2u : 4u;
        if (next_capacity < index->group_capacity ||
            (size_t)next_capacity > SIZE_MAX / sizeof(*replacement))
            return NULL;
        replacement = realloc(
            index->groups,
            (size_t)next_capacity * sizeof(*replacement));
        if (!replacement)
            return NULL;
        memset(
            replacement + index->group_capacity, 0,
            (size_t)(next_capacity - index->group_capacity) *
                sizeof(*replacement));
        index->groups = replacement;
        index->group_capacity = next_capacity;
    }
    CettaGsltRigidCoordinateGroupV1 *group =
        &index->groups[index->group_count++];
    group->key = *key;
    return group;
}

bool cetta_gslt_rigid_coordinate_index_build_v1(
    CettaGsltRigidCoordinateIndexV1 *index,
    CettaGsltRigidCoordinateScratchV1 *scratch,
    uint32_t arity, uint32_t occurrence_count,
    CettaGsltRigidCoordinateKeyAtV1 key_at, void *context) {
    CettaGsltRigidCoordinateIndexV1 replacement;
    CettaGsltRigidCoordinateCountSlotV1 *slots;
    uint32_t best_coordinate = 0u;
    uint64_t best_score = 0u;

    if (!index || !scratch || !key_at ||
        !rigid_scratch_reserve(scratch, occurrence_count))
        return false;
    cetta_gslt_rigid_coordinate_index_init_v1(&replacement);
    slots = scratch->slots;
    for (uint32_t coordinate = 0u; coordinate < arity; coordinate++) {
        uint64_t rigid = 0u;
        uint64_t separated_pairs = 0u;
        memset(slots, 0, scratch->slot_capacity * sizeof(*slots));
        for (uint32_t occurrence = 0u;
             occurrence < occurrence_count; occurrence++) {
            CettaGsltRigidKeyV1 key;
            if (!key_at(context, occurrence, coordinate, &key))
                continue;
            size_t slot = (size_t)rigid_key_hash(&key) &
                (scratch->slot_capacity - 1u);
            while (slots[slot].occupied &&
                   !rigid_key_equal(&slots[slot].key, &key))
                slot = (slot + 1u) & (scratch->slot_capacity - 1u);
            uint64_t equal = slots[slot].occupied
                ? slots[slot].count : 0u;
            separated_pairs = rigid_add_sat(
                separated_pairs, rigid - equal);
            if (!slots[slot].occupied) {
                slots[slot].key = key;
                slots[slot].occupied = true;
            }
            if (slots[slot].count != UINT32_MAX)
                slots[slot].count++;
            rigid++;
        }
        uint64_t score = rigid_add_sat(
            rigid_mul_sat(
                separated_pairs, (uint64_t)occurrence_count + 1u),
            rigid);
        if (score > best_score) {
            best_score = score;
            best_coordinate = coordinate;
        }
    }
    if (best_score == 0u) {
        cetta_gslt_rigid_coordinate_index_free_v1(index);
        *index = replacement;
        return true;
    }
    replacement.coordinate = best_coordinate;
    replacement.occurrence_count = occurrence_count;
    replacement.admitted = true;
    for (uint32_t occurrence = 0u;
         occurrence < occurrence_count; occurrence++) {
        CettaGsltRigidKeyV1 key;
        if (!key_at(context, occurrence, best_coordinate, &key)) {
            if (!rigid_u32_append(
                    &replacement.wildcard_positions,
                    &replacement.wildcard_count,
                    &replacement.wildcard_capacity, occurrence))
                goto fail;
            for (uint32_t group = 0u;
                 group < replacement.group_count; group++)
                if (!rigid_u32_append(
                        &replacement.groups[group].positions,
                        &replacement.groups[group].position_count,
                        &replacement.groups[group].position_capacity,
                        occurrence))
                    goto fail;
            continue;
        }
        CettaGsltRigidCoordinateGroupV1 *group =
            rigid_group(&replacement, &key, false);
        if (!group) {
            group = rigid_group(&replacement, &key, true);
            if (!group)
                goto fail;
            for (uint32_t wildcard = 0u;
                 wildcard < replacement.wildcard_count; wildcard++)
                if (!rigid_u32_append(
                        &group->positions, &group->position_count,
                        &group->position_capacity,
                        replacement.wildcard_positions[wildcard]))
                    goto fail;
        }
        if (!rigid_u32_append(
                &group->positions, &group->position_count,
                &group->position_capacity, occurrence))
            goto fail;
    }
    cetta_gslt_rigid_coordinate_index_free_v1(index);
    *index = replacement;
    return true;

fail:
    cetta_gslt_rigid_coordinate_index_free_v1(&replacement);
    return false;
}

bool cetta_gslt_rigid_coordinate_index_positions_v1(
    const CettaGsltRigidCoordinateIndexV1 *index,
    const CettaGsltRigidKeyV1 *query_key,
    const uint32_t **positions_out, uint32_t *position_count_out) {
    if (!index || !query_key || !positions_out || !position_count_out ||
        !index->admitted)
        return false;
    for (uint32_t group = 0u; group < index->group_count; group++) {
        if (!rigid_key_equal(&index->groups[group].key, query_key))
            continue;
        *positions_out = index->groups[group].positions;
        *position_count_out = index->groups[group].position_count;
        return true;
    }
    *positions_out = index->wildcard_positions;
    *position_count_out = index->wildcard_count;
    return true;
}

bool cetta_gslt_rigid_key_from_atom_v1(
    const Atom *atom, CettaGsltRigidKeyV1 *key_out) {
    if (!atom || !key_out || atom->kind == ATOM_VAR)
        return false;
    memset(key_out, 0, sizeof(*key_out));
    switch (atom->kind) {
    case ATOM_SYMBOL:
        key_out->kind = CETTA_GSLT_RIGID_KEY_SYMBOL_V1;
        key_out->symbol = atom->sym_id;
        return true;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_STRING && atom->ground.sval) {
            key_out->kind = CETTA_GSLT_RIGID_KEY_STRING_V1;
            key_out->text = atom->ground.sval;
            return true;
        }
        if (atom->ground.gkind == GV_INT) {
            key_out->kind = CETTA_GSLT_RIGID_KEY_INTEGER_V1;
            key_out->integer = atom->ground.ival;
            return true;
        }
        return false;
    case ATOM_EXPR:
        if (!atom->expr.elems || atom->expr.len == 0u ||
            !atom->expr.elems[0] ||
            atom->expr.elems[0]->kind != ATOM_SYMBOL ||
            atom->expr.len - 1u > UINT32_MAX)
            return false;
        key_out->kind = CETTA_GSLT_RIGID_KEY_APPLICATION_V1;
        key_out->symbol = atom->expr.elems[0]->sym_id;
        key_out->arity = (uint32_t)(atom->expr.len - 1u);
        return true;
    default:
        return false;
    }
}
