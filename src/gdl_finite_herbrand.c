#include "gdl_finite_herbrand.h"

#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    GDL_FINITE_HERBRAND_DEFAULT_MAX_TERMS_V1 = 262144u,
    GDL_FINITE_HERBRAND_DEFAULT_MAX_APPLICATIONS_V1 = 16777216u,
    GDL_FINITE_HERBRAND_DEFAULT_MAX_ROUNDS_V1 = 4096u,
    GDL_FINITE_HERBRAND_DEFAULT_MAX_DEPTH_V1 = 4096u,
};

typedef struct {
    size_t signature_index;
    size_t statement_ordinal;
    size_t name_ordinal;
    size_t *argument_term_indices;
    size_t argument_count;
} GdlFiniteHerbrandConstructionV1;

typedef struct {
    Atom *term;
    size_t exact_type_index;
    size_t depth;
    GdlFiniteHerbrandConstructionV1 *constructions;
    size_t construction_count;
    size_t construction_capacity;
} GdlFiniteHerbrandTermV1;

typedef struct {
    size_t statement_ordinal;
    size_t name_ordinal;
    char *name;
    char **argument_types;
    size_t *argument_type_indices;
    size_t argument_count;
    char *result_type;
    size_t result_type_index;
    bool relation;
} GdlFiniteHerbrandSignatureV1;

typedef struct {
    char *subtype;
    char *supertype;
} GdlFiniteHerbrandSubtypeV1;

struct CettaGdlFiniteHerbrandV1 {
    Arena arena;
    CettaGdlFiniteHerbrandLimitsV1 limits;
    GdlFiniteHerbrandSignatureV1 *signatures;
    size_t signature_count;
    GdlFiniteHerbrandSubtypeV1 *subtypes;
    size_t subtype_count;
    char **types;
    size_t type_count;
    size_t type_capacity;
    uint8_t *accepts;
    size_t *relation_indices;
    size_t relation_count;
    size_t relation_capacity;
    GdlFiniteHerbrandTermV1 *terms;
    size_t term_count;
    size_t term_capacity;
    size_t *term_slots;
    size_t term_slot_capacity;
    size_t term_slot_used;
    CettaGdlFiniteHerbrandStatsV1 stats;
};

typedef enum {
    GDL_FINITE_HERBRAND_ADD_NEW_V1 = 1,
    GDL_FINITE_HERBRAND_ADD_EXISTING_V1,
    GDL_FINITE_HERBRAND_ADD_INCOMPLETE_V1,
    GDL_FINITE_HERBRAND_ADD_FAULT_V1,
} GdlFiniteHerbrandAddV1;

static bool gdl_finite_herbrand_reserve_v1(
    void **items, size_t *capacity, size_t needed, size_t item_size) {
    if (!items || !capacity || item_size == 0u)
        return false;
    if (*capacity >= needed)
        return true;
    size_t next = *capacity ? *capacity : 16u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / item_size)
        return false;
    *items = cetta_realloc(*items, next * item_size);
    *capacity = next;
    return true;
}

static size_t gdl_finite_herbrand_type_index_v1(
    const CettaGdlFiniteHerbrandV1 *domain, const char *type) {
    if (!domain || !type)
        return SIZE_MAX;
    for (size_t index = 0u; index < domain->type_count; index++)
        if (strcmp(domain->types[index], type) == 0)
            return index;
    return SIZE_MAX;
}

static bool gdl_finite_herbrand_add_type_v1(
    CettaGdlFiniteHerbrandV1 *domain, const char *type) {
    if (!domain || !type || !*type)
        return false;
    if (gdl_finite_herbrand_type_index_v1(domain, type) != SIZE_MAX)
        return true;
    if (!gdl_finite_herbrand_reserve_v1(
            (void **)&domain->types, &domain->type_capacity,
            domain->type_count + 1u, sizeof(*domain->types)))
        return false;
    char *copy = arena_strdup(&domain->arena, type);
    if (!copy)
        return false;
    domain->types[domain->type_count++] = copy;
    return true;
}

static Atom *gdl_finite_herbrand_token_v1(
    Arena *arena, const char *token) {
    if (!arena || !token || !*token)
        return NULL;
    char *end = NULL;
    errno = 0;
    intmax_t value = strtoimax(token, &end, 10);
    if (errno == 0 && end && *end == '\0' && value >= INT64_MIN &&
        value <= INT64_MAX)
        return atom_int(arena, (int64_t)value);
    return atom_symbol(arena, token);
}

static uint64_t gdl_finite_herbrand_term_key_v1(
    Atom *term, size_t exact_type_index) {
    uint64_t value = (uint64_t)atom_hash(term);
    value ^= UINT64_C(0x9e3779b97f4a7c15) +
        (uint64_t)exact_type_index + (value << 6u) + (value >> 2u);
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

static bool gdl_finite_herbrand_term_slots_rebuild_v1(
    CettaGdlFiniteHerbrandV1 *domain, size_t capacity) {
    if (!domain || capacity < 16u || (capacity & (capacity - 1u)) != 0u)
        return false;
    size_t *slots = cetta_malloc(capacity * sizeof(*slots));
    memset(slots, 0, capacity * sizeof(*slots));
    for (size_t index = 0u; index < domain->term_count; index++) {
        const GdlFiniteHerbrandTermV1 *term = &domain->terms[index];
        size_t slot = (size_t)gdl_finite_herbrand_term_key_v1(
            term->term, term->exact_type_index) & (capacity - 1u);
        while (slots[slot] != 0u)
            slot = (slot + 1u) & (capacity - 1u);
        slots[slot] = index + 1u;
    }
    free(domain->term_slots);
    domain->term_slots = slots;
    domain->term_slot_capacity = capacity;
    domain->term_slot_used = domain->term_count;
    return true;
}

static bool gdl_finite_herbrand_term_slots_prepare_v1(
    CettaGdlFiniteHerbrandV1 *domain) {
    if (!domain)
        return false;
    if (domain->term_slot_capacity == 0u)
        return gdl_finite_herbrand_term_slots_rebuild_v1(domain, 16u);
    if ((domain->term_slot_used + 1u) * 10u <
        domain->term_slot_capacity * 7u)
        return true;
    if (domain->term_slot_capacity > SIZE_MAX / 2u)
        return false;
    return gdl_finite_herbrand_term_slots_rebuild_v1(
        domain, domain->term_slot_capacity * 2u);
}

static size_t gdl_finite_herbrand_find_term_index_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    Atom *term, size_t exact_type_index) {
    if (!domain || !term || exact_type_index >= domain->type_count ||
        domain->term_slot_capacity == 0u)
        return SIZE_MAX;
    size_t slot = (size_t)gdl_finite_herbrand_term_key_v1(
        term, exact_type_index) & (domain->term_slot_capacity - 1u);
    for (;;) {
        size_t encoded = domain->term_slots[slot];
        if (encoded == 0u)
            return SIZE_MAX;
        size_t index = encoded - 1u;
        const GdlFiniteHerbrandTermV1 *candidate = &domain->terms[index];
        if (candidate->exact_type_index == exact_type_index &&
            atom_eq(candidate->term, term))
            return index;
        slot = (slot + 1u) & (domain->term_slot_capacity - 1u);
    }
}

static bool gdl_finite_herbrand_same_construction_v1(
    const GdlFiniteHerbrandConstructionV1 *construction,
    size_t signature_index,
    const size_t *arguments,
    size_t argument_count) {
    if (!construction || construction->signature_index != signature_index ||
        construction->argument_count != argument_count)
        return false;
    for (size_t index = 0u; index < argument_count; index++)
        if (construction->argument_term_indices[index] != arguments[index])
            return false;
    return true;
}

static bool gdl_finite_herbrand_add_construction_v1(
    CettaGdlFiniteHerbrandV1 *domain,
    GdlFiniteHerbrandTermV1 *term,
    size_t signature_index,
    const size_t *arguments,
    size_t argument_count) {
    if (!domain || !term || signature_index >= domain->signature_count ||
        (argument_count != 0u && !arguments))
        return false;
    for (size_t index = 0u; index < term->construction_count; index++)
        if (gdl_finite_herbrand_same_construction_v1(
                &term->constructions[index], signature_index,
                arguments, argument_count))
            return true;
    if (!gdl_finite_herbrand_reserve_v1(
            (void **)&term->constructions,
            &term->construction_capacity,
            term->construction_count + 1u,
            sizeof(*term->constructions)))
        return false;
    size_t *argument_copy = NULL;
    if (argument_count != 0u) {
        argument_copy = arena_alloc(
            &domain->arena, argument_count * sizeof(*argument_copy));
        memcpy(argument_copy, arguments,
               argument_count * sizeof(*argument_copy));
    }
    GdlFiniteHerbrandConstructionV1 *construction =
        &term->constructions[term->construction_count++];
    *construction = (GdlFiniteHerbrandConstructionV1){
        .signature_index = signature_index,
        .statement_ordinal =
            domain->signatures[signature_index].statement_ordinal,
        .name_ordinal = domain->signatures[signature_index].name_ordinal,
        .argument_term_indices = argument_copy,
        .argument_count = argument_count,
    };
    domain->stats.construction_witnesses++;
    return true;
}

static GdlFiniteHerbrandAddV1 gdl_finite_herbrand_add_term_v1(
    CettaGdlFiniteHerbrandV1 *domain,
    Atom *term,
    size_t exact_type_index,
    size_t depth,
    size_t signature_index,
    const size_t *arguments,
    size_t argument_count) {
    if (!domain || !term || exact_type_index >= domain->type_count ||
        signature_index >= domain->signature_count)
        return GDL_FINITE_HERBRAND_ADD_FAULT_V1;
    size_t found = gdl_finite_herbrand_find_term_index_v1(
        domain, term, exact_type_index);
    if (found != SIZE_MAX)
        return gdl_finite_herbrand_add_construction_v1(
                   domain, &domain->terms[found], signature_index,
                   arguments, argument_count)
            ? GDL_FINITE_HERBRAND_ADD_EXISTING_V1
            : GDL_FINITE_HERBRAND_ADD_FAULT_V1;
    if (domain->term_count >= domain->limits.max_terms)
        return GDL_FINITE_HERBRAND_ADD_INCOMPLETE_V1;
    if (!gdl_finite_herbrand_term_slots_prepare_v1(domain) ||
        !gdl_finite_herbrand_reserve_v1(
            (void **)&domain->terms, &domain->term_capacity,
            domain->term_count + 1u, sizeof(*domain->terms)))
        return GDL_FINITE_HERBRAND_ADD_FAULT_V1;
    size_t index = domain->term_count++;
    GdlFiniteHerbrandTermV1 *entry = &domain->terms[index];
    memset(entry, 0, sizeof(*entry));
    entry->term = term;
    entry->exact_type_index = exact_type_index;
    entry->depth = depth;
    size_t slot = (size_t)gdl_finite_herbrand_term_key_v1(
        term, exact_type_index) & (domain->term_slot_capacity - 1u);
    while (domain->term_slots[slot] != 0u)
        slot = (slot + 1u) & (domain->term_slot_capacity - 1u);
    domain->term_slots[slot] = index + 1u;
    domain->term_slot_used++;
    if (!gdl_finite_herbrand_add_construction_v1(
            domain, entry, signature_index, arguments, argument_count))
        return GDL_FINITE_HERBRAND_ADD_FAULT_V1;
    domain->stats.terms = domain->term_count;
    if (depth > domain->stats.maximum_depth)
        domain->stats.maximum_depth = depth;
    return GDL_FINITE_HERBRAND_ADD_NEW_V1;
}

static bool gdl_finite_herbrand_copy_profile_v1(
    CettaGdlFiniteHerbrandV1 *domain,
    const GdlSourceProfileV1 *profile) {
    if (!domain || !profile || profile->statement_count == 0u ||
        profile->signature_count == 0u || !profile->signatures ||
        (profile->subtype_count != 0u && !profile->subtypes))
        return false;
    for (size_t index = 0u; index < profile->signature_count; index++) {
        const GdlSourceSignatureV1 *signature = &profile->signatures[index];
        if (!signature->name || !*signature->name ||
            !signature->result_type || !*signature->result_type ||
            (signature->argument_count != 0u &&
             !signature->argument_types) ||
            !gdl_finite_herbrand_add_type_v1(
                domain, signature->result_type))
            return false;
        for (size_t argument = 0u;
             argument < signature->argument_count; argument++)
            if (!signature->argument_types[argument] ||
                !*signature->argument_types[argument] ||
                !gdl_finite_herbrand_add_type_v1(
                    domain, signature->argument_types[argument]))
                return false;
    }
    for (size_t index = 0u; index < profile->subtype_count; index++) {
        const GdlSourceSubtypeV1 *subtype = &profile->subtypes[index];
        if (!subtype->subtype || !*subtype->subtype ||
            !subtype->supertype || !*subtype->supertype ||
            !gdl_finite_herbrand_add_type_v1(domain, subtype->subtype) ||
            !gdl_finite_herbrand_add_type_v1(domain, subtype->supertype))
            return false;
    }
    if (domain->type_count > SIZE_MAX / domain->type_count)
        return false;
    size_t closure_size = domain->type_count * domain->type_count;
    domain->accepts = cetta_malloc(closure_size);
    memset(domain->accepts, 0, closure_size);
    for (size_t type = 0u; type < domain->type_count; type++)
        domain->accepts[type * domain->type_count + type] = 1u;

    domain->signature_count = profile->signature_count;
    domain->signatures = cetta_malloc(
        domain->signature_count * sizeof(*domain->signatures));
    memset(domain->signatures, 0,
           domain->signature_count * sizeof(*domain->signatures));
    for (size_t index = 0u; index < profile->signature_count; index++) {
        const GdlSourceSignatureV1 *source = &profile->signatures[index];
        GdlFiniteHerbrandSignatureV1 *target = &domain->signatures[index];
        target->statement_ordinal = source->statement_ordinal;
        target->name_ordinal = source->name_ordinal;
        target->name = arena_strdup(&domain->arena, source->name);
        target->result_type = arena_strdup(
            &domain->arena, source->result_type);
        target->argument_count = source->argument_count;
        target->result_type_index = gdl_finite_herbrand_type_index_v1(
            domain, source->result_type);
        target->relation = strcmp(source->result_type, "bool") == 0;
        if (!target->name || !target->result_type ||
            target->result_type_index == SIZE_MAX)
            return false;
        if (target->argument_count != 0u) {
            target->argument_types = arena_alloc(
                &domain->arena,
                target->argument_count * sizeof(*target->argument_types));
            target->argument_type_indices = arena_alloc(
                &domain->arena,
                target->argument_count *
                    sizeof(*target->argument_type_indices));
            for (size_t argument = 0u;
                 argument < target->argument_count; argument++) {
                target->argument_types[argument] = arena_strdup(
                    &domain->arena, source->argument_types[argument]);
                target->argument_type_indices[argument] =
                    gdl_finite_herbrand_type_index_v1(
                        domain, source->argument_types[argument]);
                if (!target->argument_types[argument] ||
                    target->argument_type_indices[argument] == SIZE_MAX)
                    return false;
            }
        }
        if (target->relation) {
            if (!gdl_finite_herbrand_reserve_v1(
                    (void **)&domain->relation_indices,
                    &domain->relation_capacity,
                    domain->relation_count + 1u,
                    sizeof(*domain->relation_indices)))
                return false;
            domain->relation_indices[domain->relation_count++] = index;
        }
    }

    domain->subtype_count = profile->subtype_count;
    if (domain->subtype_count != 0u) {
        domain->subtypes = cetta_malloc(
            domain->subtype_count * sizeof(*domain->subtypes));
        memset(domain->subtypes, 0,
               domain->subtype_count * sizeof(*domain->subtypes));
    }
    for (size_t index = 0u; index < profile->subtype_count; index++) {
        GdlFiniteHerbrandSubtypeV1 *target = &domain->subtypes[index];
        target->subtype = arena_strdup(
            &domain->arena, profile->subtypes[index].subtype);
        target->supertype = arena_strdup(
            &domain->arena, profile->subtypes[index].supertype);
        size_t subtype_index = gdl_finite_herbrand_type_index_v1(
            domain, profile->subtypes[index].subtype);
        size_t supertype_index = gdl_finite_herbrand_type_index_v1(
            domain, profile->subtypes[index].supertype);
        if (!target->subtype || !target->supertype ||
            subtype_index == SIZE_MAX || supertype_index == SIZE_MAX)
            return false;
        domain->accepts[
            subtype_index * domain->type_count + supertype_index] = 1u;
    }
    for (size_t middle = 0u; middle < domain->type_count; middle++)
        for (size_t actual = 0u; actual < domain->type_count; actual++) {
            if (!domain->accepts[
                    actual * domain->type_count + middle])
                continue;
            for (size_t expected = 0u;
                 expected < domain->type_count; expected++)
                if (domain->accepts[
                        middle * domain->type_count + expected])
                    domain->accepts[
                        actual * domain->type_count + expected] = 1u;
        }
    domain->stats.types = domain->type_count;
    domain->stats.signatures = domain->signature_count;
    domain->stats.relations = domain->relation_count;
    domain->stats.constructors =
        domain->signature_count - domain->relation_count;
    domain->stats.subtype_edges = domain->subtype_count;
    return true;
}

static CettaGdlFiniteHerbrandKindV1
gdl_finite_herbrand_seed_v1(CettaGdlFiniteHerbrandV1 *domain) {
    for (size_t index = 0u; index < domain->signature_count; index++) {
        GdlFiniteHerbrandSignatureV1 *signature =
            &domain->signatures[index];
        if (signature->relation || signature->argument_count != 0u)
            continue;
        Atom *term = gdl_finite_herbrand_token_v1(
            &domain->arena, signature->name);
        GdlFiniteHerbrandAddV1 added =
            gdl_finite_herbrand_add_term_v1(
                domain, term, signature->result_type_index,
                0u, index, NULL, 0u);
        if (added == GDL_FINITE_HERBRAND_ADD_INCOMPLETE_V1)
            return CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1;
        if (added == GDL_FINITE_HERBRAND_ADD_FAULT_V1)
            return CETTA_GDL_FINITE_HERBRAND_ENGINE_FAULT_V1;
    }
    return CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1;
}

static bool gdl_finite_herbrand_signature_candidates_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    const GdlFiniteHerbrandSignatureV1 *signature,
    size_t snapshot,
    size_t ***candidates_out,
    size_t **counts_out) {
    size_t **candidates = cetta_malloc(
        signature->argument_count * sizeof(*candidates));
    size_t *counts = cetta_malloc(
        signature->argument_count * sizeof(*counts));
    memset(candidates, 0,
           signature->argument_count * sizeof(*candidates));
    memset(counts, 0, signature->argument_count * sizeof(*counts));
    for (size_t argument = 0u;
         argument < signature->argument_count; argument++) {
        candidates[argument] = cetta_malloc(snapshot * sizeof(size_t));
        size_t expected = signature->argument_type_indices[argument];
        for (size_t term = 0u; term < snapshot; term++) {
            size_t actual = domain->terms[term].exact_type_index;
            if (domain->accepts[
                    actual * domain->type_count + expected])
                candidates[argument][counts[argument]++] = term;
        }
    }
    *candidates_out = candidates;
    *counts_out = counts;
    return true;
}

static void gdl_finite_herbrand_candidates_free_v1(
    size_t **candidates, size_t *counts, size_t argument_count) {
    if (candidates)
        for (size_t argument = 0u;
             argument < argument_count; argument++)
            free(candidates[argument]);
    free(candidates);
    free(counts);
}

static CettaGdlFiniteHerbrandKindV1
gdl_finite_herbrand_apply_signature_v1(
    CettaGdlFiniteHerbrandV1 *domain,
    size_t signature_index,
    size_t delta_start,
    size_t snapshot) {
    GdlFiniteHerbrandSignatureV1 *signature =
        &domain->signatures[signature_index];
    if (signature->relation || signature->argument_count == 0u)
        return CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1;
    size_t **candidates = NULL;
    size_t *counts = NULL;
    if (!gdl_finite_herbrand_signature_candidates_v1(
            domain, signature, snapshot, &candidates, &counts))
        return CETTA_GDL_FINITE_HERBRAND_ENGINE_FAULT_V1;
    bool empty = false;
    for (size_t argument = 0u;
         argument < signature->argument_count; argument++)
        empty = empty || counts[argument] == 0u;
    if (empty) {
        gdl_finite_herbrand_candidates_free_v1(
            candidates, counts, signature->argument_count);
        return CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1;
    }

    size_t *positions = cetta_malloc(
        signature->argument_count * sizeof(*positions));
    size_t *arguments = cetta_malloc(
        signature->argument_count * sizeof(*arguments));
    memset(positions, 0,
           signature->argument_count * sizeof(*positions));
    CettaGdlFiniteHerbrandKindV1 result =
        CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1;
    bool finished = false;
    while (!finished) {
        bool touches_delta = false;
        size_t depth = 0u;
        for (size_t argument = 0u;
             argument < signature->argument_count; argument++) {
            arguments[argument] =
                candidates[argument][positions[argument]];
            touches_delta = touches_delta ||
                arguments[argument] >= delta_start;
            if (domain->terms[arguments[argument]].depth > depth)
                depth = domain->terms[arguments[argument]].depth;
        }
        if (touches_delta) {
            if (domain->stats.constructor_applications >=
                domain->limits.max_constructor_applications) {
                result = CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1;
                break;
            }
            domain->stats.constructor_applications++;
            if (depth >= domain->limits.max_depth) {
                result = CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1;
                break;
            }
            Atom **items = arena_alloc(
                &domain->arena,
                (signature->argument_count + 1u) * sizeof(*items));
            items[0] = atom_symbol(&domain->arena, signature->name);
            for (size_t argument = 0u;
                 argument < signature->argument_count; argument++)
                items[argument + 1u] =
                    domain->terms[arguments[argument]].term;
            Atom *term = atom_expr(
                &domain->arena, items,
                (CettaExprLen)(signature->argument_count + 1u));
            GdlFiniteHerbrandAddV1 added =
                gdl_finite_herbrand_add_term_v1(
                    domain, term, signature->result_type_index,
                    depth + 1u, signature_index,
                    arguments, signature->argument_count);
            if (added == GDL_FINITE_HERBRAND_ADD_INCOMPLETE_V1) {
                result = CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1;
                break;
            }
            if (added == GDL_FINITE_HERBRAND_ADD_FAULT_V1) {
                result = CETTA_GDL_FINITE_HERBRAND_ENGINE_FAULT_V1;
                break;
            }
        }
        for (size_t reverse = signature->argument_count;
             reverse > 0u; reverse--) {
            size_t argument = reverse - 1u;
            positions[argument]++;
            if (positions[argument] < counts[argument])
                break;
            positions[argument] = 0u;
            if (argument == 0u)
                finished = true;
        }
    }
    free(arguments);
    free(positions);
    gdl_finite_herbrand_candidates_free_v1(
        candidates, counts, signature->argument_count);
    return result;
}

CettaGdlFiniteHerbrandResultV1 cetta_gdl_finite_herbrand_construct_v1(
    const GdlSourceProfileV1 *profile,
    CettaGdlFiniteHerbrandLimitsV1 limits) {
    CettaGdlFiniteHerbrandV1 *domain = cetta_malloc(sizeof(*domain));
    memset(domain, 0, sizeof(*domain));
    arena_init(&domain->arena);
    arena_set_runtime_kind(
        &domain->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    domain->limits = limits;
    if (domain->limits.max_terms == 0u)
        domain->limits.max_terms =
            GDL_FINITE_HERBRAND_DEFAULT_MAX_TERMS_V1;
    if (domain->limits.max_constructor_applications == 0u)
        domain->limits.max_constructor_applications =
            GDL_FINITE_HERBRAND_DEFAULT_MAX_APPLICATIONS_V1;
    if (domain->limits.max_rounds == 0u)
        domain->limits.max_rounds =
            GDL_FINITE_HERBRAND_DEFAULT_MAX_ROUNDS_V1;
    if (domain->limits.max_depth == 0u)
        domain->limits.max_depth =
            GDL_FINITE_HERBRAND_DEFAULT_MAX_DEPTH_V1;
    if (!gdl_finite_herbrand_copy_profile_v1(domain, profile)) {
        cetta_gdl_finite_herbrand_destroy_v1(domain);
        return (CettaGdlFiniteHerbrandResultV1){
            .kind = CETTA_GDL_FINITE_HERBRAND_OUTSIDE_FRAGMENT_V1,
            .domain = NULL,
        };
    }
    CettaGdlFiniteHerbrandKindV1 kind =
        gdl_finite_herbrand_seed_v1(domain);
    if (kind != CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1)
        return (CettaGdlFiniteHerbrandResultV1){kind, domain};

    size_t delta_start = 0u;
    while (delta_start < domain->term_count) {
        if (domain->stats.rounds >= domain->limits.max_rounds)
            return (CettaGdlFiniteHerbrandResultV1){
                CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1, domain};
        size_t snapshot = domain->term_count;
        domain->stats.rounds++;
        for (size_t signature = 0u;
             signature < domain->signature_count; signature++) {
            kind = gdl_finite_herbrand_apply_signature_v1(
                domain, signature, delta_start, snapshot);
            if (kind != CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1)
                return (CettaGdlFiniteHerbrandResultV1){kind, domain};
        }
        if (domain->term_count == snapshot)
            break;
        delta_start = snapshot;
    }
    domain->stats.terms = domain->term_count;
    return (CettaGdlFiniteHerbrandResultV1){
        CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1, domain};
}

void cetta_gdl_finite_herbrand_destroy_v1(
    CettaGdlFiniteHerbrandV1 *domain) {
    if (!domain)
        return;
    for (size_t index = 0u; index < domain->term_count; index++)
        free(domain->terms[index].constructions);
    free(domain->terms);
    free(domain->term_slots);
    free(domain->relation_indices);
    free(domain->accepts);
    free(domain->types);
    free(domain->subtypes);
    free(domain->signatures);
    arena_free(&domain->arena);
    free(domain);
}

size_t cetta_gdl_finite_herbrand_term_count_v1(
    const CettaGdlFiniteHerbrandV1 *domain) {
    return domain ? domain->term_count : 0u;
}

size_t cetta_gdl_finite_herbrand_relation_count_v1(
    const CettaGdlFiniteHerbrandV1 *domain) {
    return domain ? domain->relation_count : 0u;
}

bool cetta_gdl_finite_herbrand_term_view_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    size_t index,
    CettaGdlFiniteHerbrandTermViewV1 *view_out) {
    if (!domain || !view_out || index >= domain->term_count)
        return false;
    const GdlFiniteHerbrandTermV1 *term = &domain->terms[index];
    *view_out = (CettaGdlFiniteHerbrandTermViewV1){
        .term = term->term,
        .exact_type = domain->types[term->exact_type_index],
        .depth = term->depth,
        .construction_count = term->construction_count,
    };
    return true;
}

bool cetta_gdl_finite_herbrand_construction_view_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    size_t term_index,
    size_t construction_index,
    CettaGdlFiniteHerbrandConstructionViewV1 *view_out) {
    if (!domain || !view_out || term_index >= domain->term_count ||
        construction_index >=
            domain->terms[term_index].construction_count)
        return false;
    const GdlFiniteHerbrandConstructionV1 *construction =
        &domain->terms[term_index].constructions[construction_index];
    *view_out = (CettaGdlFiniteHerbrandConstructionViewV1){
        .signature_index = construction->signature_index,
        .statement_ordinal = construction->statement_ordinal,
        .name_ordinal = construction->name_ordinal,
        .argument_term_indices = construction->argument_term_indices,
        .argument_count = construction->argument_count,
    };
    return true;
}

bool cetta_gdl_finite_herbrand_relation_view_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    size_t relation_index,
    CettaGdlFiniteHerbrandRelationViewV1 *view_out) {
    if (!domain || !view_out || relation_index >= domain->relation_count)
        return false;
    size_t signature_index = domain->relation_indices[relation_index];
    const GdlFiniteHerbrandSignatureV1 *signature =
        &domain->signatures[signature_index];
    *view_out = (CettaGdlFiniteHerbrandRelationViewV1){
        .signature_index = signature_index,
        .statement_ordinal = signature->statement_ordinal,
        .name_ordinal = signature->name_ordinal,
        .name = signature->name,
        .argument_types =
            (const char *const *)signature->argument_types,
        .argument_count = signature->argument_count,
    };
    return true;
}

bool cetta_gdl_finite_herbrand_find_term_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    Atom *term,
    const char *exact_type,
    size_t *index_out) {
    if (!domain || !term || !exact_type || !index_out)
        return false;
    size_t type_index = gdl_finite_herbrand_type_index_v1(
        domain, exact_type);
    if (type_index == SIZE_MAX)
        return false;
    size_t term_index = gdl_finite_herbrand_find_term_index_v1(
        domain, term, type_index);
    if (term_index == SIZE_MAX)
        return false;
    *index_out = term_index;
    return true;
}

size_t cetta_gdl_finite_herbrand_matching_terms_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    Atom *term,
    const char *expected_type,
    size_t *indices_out,
    size_t capacity) {
    if (!domain || !term || !expected_type ||
        (capacity != 0u && !indices_out))
        return 0u;
    size_t expected = gdl_finite_herbrand_type_index_v1(
        domain, expected_type);
    if (expected == SIZE_MAX)
        return 0u;
    size_t count = 0u;
    for (size_t actual = 0u; actual < domain->type_count; actual++) {
        if (domain->accepts[
                actual * domain->type_count + expected] == 0u)
            continue;
        size_t term_index = gdl_finite_herbrand_find_term_index_v1(
            domain, term, actual);
        if (term_index == SIZE_MAX)
            continue;
        if (count < capacity)
            indices_out[count] = term_index;
        count++;
    }
    return count;
}

bool cetta_gdl_finite_herbrand_type_accepts_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    const char *actual_type,
    const char *expected_type) {
    if (!domain || !actual_type || !expected_type)
        return false;
    size_t actual = gdl_finite_herbrand_type_index_v1(
        domain, actual_type);
    size_t expected = gdl_finite_herbrand_type_index_v1(
        domain, expected_type);
    return actual != SIZE_MAX && expected != SIZE_MAX &&
        domain->accepts[actual * domain->type_count + expected] != 0u;
}

bool cetta_gdl_finite_herbrand_stats_v1(
    const CettaGdlFiniteHerbrandV1 *domain,
    CettaGdlFiniteHerbrandStatsV1 *stats_out) {
    if (!domain || !stats_out)
        return false;
    *stats_out = domain->stats;
    return true;
}
