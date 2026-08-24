#include "prime_level.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct CettaPrimeLevelV1 {
    uint64_t constant;
    size_t parameter_count;
    CettaPrimeLevelParameterOffsetV1 parameters[];
};

static bool prime_level_size(
    size_t parameter_count, size_t *size_out) {
    if (!size_out ||
        parameter_count >
            (SIZE_MAX - sizeof(CettaPrimeLevelV1)) /
                sizeof(CettaPrimeLevelParameterOffsetV1)) {
        return false;
    }
    size_t size = sizeof(CettaPrimeLevelV1) +
        parameter_count * sizeof(CettaPrimeLevelParameterOffsetV1);
    if (size > SIZE_MAX - 7u) return false;
    *size_out = size;
    return true;
}

static CettaPrimeLevelStatusV1 prime_level_allocate(
    Arena *owner, uint64_t constant,
    const CettaPrimeLevelParameterOffsetV1 *parameters,
    size_t parameter_count, const CettaPrimeLevelV1 **level_out) {
    if (level_out) *level_out = NULL;
    if (!owner || !level_out || (parameter_count != 0u && !parameters))
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    size_t size = 0u;
    if (!prime_level_size(parameter_count, &size))
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    CettaPrimeLevelV1 *level = arena_alloc(owner, size);
    level->constant = constant;
    level->parameter_count = parameter_count;
    if (parameter_count != 0u)
        memcpy(
            level->parameters, parameters,
            parameter_count * sizeof(*parameters));
    *level_out = level;
    return CETTA_PRIME_LEVEL_OK_V1;
}

static uint64_t prime_level_sup_offsets(
    const CettaPrimeLevelParameterOffsetV1 *parameters,
    size_t parameter_count) {
    uint64_t supremum = 0u;
    for (size_t index = 0u; index < parameter_count; index++)
        if (parameters[index].offset > supremum)
            supremum = parameters[index].offset;
    return supremum;
}

static uint64_t prime_level_absorb_constant(
    uint64_t constant,
    const CettaPrimeLevelParameterOffsetV1 *parameters,
    size_t parameter_count) {
    return constant <= prime_level_sup_offsets(parameters, parameter_count)
        ? 0u
        : constant;
}

CettaPrimeLevelStatusV1 cetta_prime_level_constant_v1(
    Arena *owner, uint64_t constant, const CettaPrimeLevelV1 **level_out) {
    return prime_level_allocate(owner, constant, NULL, 0u, level_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_parameter_v1(
    Arena *owner, uint64_t parameter, const CettaPrimeLevelV1 **level_out) {
    CettaPrimeLevelParameterOffsetV1 atom = {
        .parameter = parameter,
        .offset = 0u,
    };
    return prime_level_allocate(owner, 0u, &atom, 1u, level_out);
}

static CettaPrimeLevelStatusV1 prime_level_shift(
    Arena *owner, const CettaPrimeLevelV1 *level, uint64_t offset,
    const CettaPrimeLevelV1 **shifted_out) {
    if (shifted_out) *shifted_out = NULL;
    if (!owner || !level || !shifted_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (level->constant > UINT64_MAX - offset)
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    for (size_t index = 0u; index < level->parameter_count; index++)
        if (level->parameters[index].offset > UINT64_MAX - offset)
            return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    size_t size = 0u;
    if (!prime_level_size(level->parameter_count, &size))
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    CettaPrimeLevelV1 *shifted = arena_alloc(owner, size);
    shifted->parameter_count = level->parameter_count;
    for (size_t index = 0u; index < level->parameter_count; index++) {
        shifted->parameters[index] = (CettaPrimeLevelParameterOffsetV1){
            .parameter = level->parameters[index].parameter,
            .offset = level->parameters[index].offset + offset,
        };
    }
    uint64_t constant = level->constant + offset;
    shifted->constant = prime_level_absorb_constant(
        constant, shifted->parameters, shifted->parameter_count);
    *shifted_out = shifted;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_successor_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelV1 **successor_out) {
    return prime_level_shift(owner, level, 1u, successor_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_offset_v1(
    Arena *owner, const CettaPrimeLevelV1 *level, uint64_t offset,
    const CettaPrimeLevelV1 **shifted_out) {
    return prime_level_shift(owner, level, offset, shifted_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_maximum_v1(
    Arena *owner, const CettaPrimeLevelV1 *left,
    const CettaPrimeLevelV1 *right,
    const CettaPrimeLevelV1 **maximum_out) {
    if (maximum_out) *maximum_out = NULL;
    if (!owner || !left || !right || !maximum_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (left->parameter_count > SIZE_MAX - right->parameter_count)
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    size_t capacity = left->parameter_count + right->parameter_count;
    size_t size = 0u;
    if (!prime_level_size(capacity, &size))
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    CettaPrimeLevelV1 *maximum = arena_alloc(owner, size);
    size_t left_index = 0u;
    size_t right_index = 0u;
    size_t output_index = 0u;
    while (left_index < left->parameter_count ||
           right_index < right->parameter_count) {
        if (right_index == right->parameter_count ||
            (left_index < left->parameter_count &&
             left->parameters[left_index].parameter <
                 right->parameters[right_index].parameter)) {
            maximum->parameters[output_index++] =
                left->parameters[left_index++];
        } else if (left_index == left->parameter_count ||
                   right->parameters[right_index].parameter <
                       left->parameters[left_index].parameter) {
            maximum->parameters[output_index++] =
                right->parameters[right_index++];
        } else {
            uint64_t left_offset = left->parameters[left_index].offset;
            uint64_t right_offset = right->parameters[right_index].offset;
            maximum->parameters[output_index++] =
                (CettaPrimeLevelParameterOffsetV1){
                    .parameter = left->parameters[left_index].parameter,
                    .offset = left_offset > right_offset
                        ? left_offset
                        : right_offset,
                };
            left_index++;
            right_index++;
        }
    }
    maximum->parameter_count = output_index;
    uint64_t constant = left->constant > right->constant
        ? left->constant
        : right->constant;
    maximum->constant = prime_level_absorb_constant(
        constant, maximum->parameters, maximum->parameter_count);
    *maximum_out = maximum;
    return CETTA_PRIME_LEVEL_OK_V1;
}

static int prime_level_parameter_compare(
    const void *left_pointer, const void *right_pointer) {
    const CettaPrimeLevelParameterOffsetV1 *left = left_pointer;
    const CettaPrimeLevelParameterOffsetV1 *right = right_pointer;
    if (left->parameter < right->parameter) return -1;
    if (left->parameter > right->parameter) return 1;
    return 0;
}

CettaPrimeLevelStatusV1 cetta_prime_level_substitute_v1(
    Arena *owner, const CettaPrimeLevelV1 *source,
    CettaPrimeLevelSubstitutionV1 substitution, void *context,
    const CettaPrimeLevelV1 **result_out) {
    if (result_out) *result_out = NULL;
    if (!owner || !source || !substitution || !result_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (source->parameter_count == 0u)
        return prime_level_allocate(
            owner, source->constant, NULL, 0u, result_out);
    if (source->parameter_count >
        SIZE_MAX / sizeof(const CettaPrimeLevelV1 *)) {
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    }
    const CettaPrimeLevelV1 **replacements = malloc(
        source->parameter_count * sizeof(*replacements));
    if (!replacements)
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    size_t capacity = 0u;
    CettaPrimeLevelStatusV1 status = CETTA_PRIME_LEVEL_OK_V1;
    for (size_t index = 0u; index < source->parameter_count; index++) {
        replacements[index] = NULL;
        status = substitution(
            context, source->parameters[index].parameter,
            &replacements[index]);
        if (status != CETTA_PRIME_LEVEL_OK_V1 || !replacements[index]) {
            if (status == CETTA_PRIME_LEVEL_OK_V1)
                status = CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
            free(replacements);
            return status;
        }
        if (capacity > SIZE_MAX - replacements[index]->parameter_count) {
            free(replacements);
            return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
        }
        capacity += replacements[index]->parameter_count;
    }
    if (capacity > SIZE_MAX / sizeof(CettaPrimeLevelParameterOffsetV1)) {
        free(replacements);
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    }
    CettaPrimeLevelParameterOffsetV1 *parameters = capacity == 0u
        ? NULL
        : malloc(capacity * sizeof(*parameters));
    if (capacity != 0u && !parameters) {
        free(replacements);
        return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
    }
    size_t parameter_count = 0u;
    uint64_t constant = source->constant;
    for (size_t source_index = 0u;
         source_index < source->parameter_count; source_index++) {
        const CettaPrimeLevelV1 *replacement = replacements[source_index];
        uint64_t offset = source->parameters[source_index].offset;
        if (replacement->constant > UINT64_MAX - offset) {
            status = CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
            goto cleanup;
        }
        uint64_t shifted_constant = replacement->constant + offset;
        if (shifted_constant > constant) constant = shifted_constant;
        for (size_t replacement_index = 0u;
             replacement_index < replacement->parameter_count;
             replacement_index++) {
            CettaPrimeLevelParameterOffsetV1 atom =
                replacement->parameters[replacement_index];
            if (atom.offset > UINT64_MAX - offset) {
                status = CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
                goto cleanup;
            }
            atom.offset += offset;
            parameters[parameter_count++] = atom;
        }
    }
    if (parameter_count > 1u)
        qsort(
            parameters, parameter_count, sizeof(*parameters),
            prime_level_parameter_compare);
    size_t distinct_count = 0u;
    for (size_t index = 0u; index < parameter_count; index++) {
        if (distinct_count != 0u &&
            parameters[distinct_count - 1u].parameter ==
                parameters[index].parameter) {
            if (parameters[index].offset >
                parameters[distinct_count - 1u].offset) {
                parameters[distinct_count - 1u].offset =
                    parameters[index].offset;
            }
        } else {
            parameters[distinct_count++] = parameters[index];
        }
    }
    constant = prime_level_absorb_constant(
        constant, parameters, distinct_count);
    status = prime_level_allocate(
        owner, constant, parameters, distinct_count, result_out);

cleanup:
    free(parameters);
    free(replacements);
    return status;
}

bool cetta_prime_level_equal_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right) {
    if (!left || !right || left->constant != right->constant ||
        left->parameter_count != right->parameter_count) {
        return false;
    }
    for (size_t index = 0u; index < left->parameter_count; index++)
        if (left->parameters[index].parameter !=
                right->parameters[index].parameter ||
            left->parameters[index].offset !=
                right->parameters[index].offset) {
            return false;
        }
    return true;
}

bool cetta_prime_level_le_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right) {
    if (!left || !right) return false;
    uint64_t right_zero = prime_level_sup_offsets(
        right->parameters, right->parameter_count);
    if (right->constant > right_zero) right_zero = right->constant;
    if (left->constant > right_zero) return false;
    size_t right_index = 0u;
    for (size_t left_index = 0u;
         left_index < left->parameter_count; left_index++) {
        while (right_index < right->parameter_count &&
               right->parameters[right_index].parameter <
                   left->parameters[left_index].parameter) {
            right_index++;
        }
        if (right_index == right->parameter_count ||
            right->parameters[right_index].parameter !=
                left->parameters[left_index].parameter ||
            left->parameters[left_index].offset >
                right->parameters[right_index].offset) {
            return false;
        }
    }
    return true;
}

CettaPrimeLevelStatusV1 cetta_prime_level_evaluate_v1(
    const CettaPrimeLevelV1 *level, CettaPrimeLevelValuationV1 valuation,
    void *context, uint64_t *value_out) {
    if (!level || !valuation || !value_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    uint64_t value = level->constant;
    for (size_t index = 0u; index < level->parameter_count; index++) {
        uint64_t parameter_value = 0u;
        CettaPrimeLevelStatusV1 status = valuation(
            context, level->parameters[index].parameter,
            &parameter_value);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        if (parameter_value >
            UINT64_MAX - level->parameters[index].offset) {
            return CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1;
        }
        uint64_t atom_value =
            parameter_value + level->parameters[index].offset;
        if (atom_value > value) value = atom_value;
    }
    *value_out = value;
    return CETTA_PRIME_LEVEL_OK_V1;
}

bool cetta_prime_level_view_v1(
    const CettaPrimeLevelV1 *level, CettaPrimeLevelViewV1 *view_out) {
    if (!level || !view_out) return false;
    *view_out = (CettaPrimeLevelViewV1){
        .constant = level->constant,
        .parameters = level->parameters,
        .parameter_count = level->parameter_count,
    };
    return true;
}
