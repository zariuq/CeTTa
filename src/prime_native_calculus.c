#include "prime_native_calculus.h"

#include "prime_regular_pattern.h"
#include "prime_typed_finite_relation.h"
#include "prime_typed_flow_boundary.h"
#include "prime_typed_flow_private.h"
#include "prime_typed_hyp.h"
#include "prime_typed_list.h"
#include "prime_typed_list_relator.h"
#include "prime_typed_relation.h"
#include "grounded.h"
#include "match.h"
#include "parser.h"
#include "stats.h"
#include "symbol.h"

#include <stddef.h>

#define PRIME_NATIVE_HYP_ADMISSION_CACHE_CAPACITY 16u
#define PRIME_NATIVE_HYP_PATH_MAX_LENGTH 256u

typedef enum {
    PRIME_NATIVE_PLAN_DECLINED = 0,
    PRIME_NATIVE_PLAN_BUILT,
    PRIME_NATIVE_PLAN_FAULT,
} PrimeNativePlanBuildV1;

typedef struct {
    const Space *space;
    uint64_t space_instance_id;
    uint64_t space_revision;
    bool admitted;
} PrimeNativeHypPresentationCacheV1;

static _Thread_local PrimeNativeHypPresentationCacheV1
    g_prime_native_hyp_presentation_cache;

static _Thread_local PrimeNativeHypPresentationCacheV1
    g_prime_native_hyp_candidate_presentation_cache;

static _Thread_local PrimeNativeHypPresentationCacheV1
    g_prime_native_hyp_path_candidate_presentation_cache;

static _Thread_local PrimeNativeHypPresentationCacheV1
    g_prime_native_map_rel_presentation_cache;

/* A language-owned realization of the theory's finite evidence provider.
 * The generic provider owns the typed relation fibres; this wrapper retains
 * only the hypothesis program's ordinary proof presentation for raw
 * relational fallback. */
typedef struct {
    CettaPrimeTypedFiniteRelationV1 *relation;
    AtomId *raw_evidence_ids;
    size_t raw_evidence_count;
} PrimeNativeRelationProviderV1;

typedef struct {
    const Space *space;
    uint64_t space_instance_id;
    uint64_t space_revision;
    const TermUniverse *universe;
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    AtomId source_program_id;
    const CettaPrimeTypedValueV1 *program;
    const CettaPrimeTypedValueV1 *denotation;
    bool finite_provider_checked;
    bool finite_provider_available;
    PrimeNativeRelationProviderV1 finite_provider;
} PrimeNativeHypAdmissionCacheEntryV1;

typedef struct {
    CettaPrimeTypedValueV1 *program;
    CettaPrimeTypedValueV1 *denotation;
    bool finite_provider_checked;
    const PrimeNativeRelationProviderV1 *finite_provider;
} PrimeNativeHypAdmittedV1;

typedef struct {
    CettaPrimeTypedValueV1 *source_carrier;
    CettaPrimeTypedValueV1 *target_carrier;
    CettaPrimeTypedValueV1 *relation;
} PrimeNativeHypDenotationV1;

typedef struct {
    Atom *source_sort;
    Atom *target_sort;
    Atom *symbol;
    CettaPrimeTypedValueV1 *typed_source_sort;
    CettaPrimeTypedValueV1 *typed_target_sort;
    CettaPrimeTypedValueV1 *typed_symbol;
} PrimeNativeHypPrimitiveDeclarationV1;

typedef enum {
    PRIME_NATIVE_HYP_CANDIDATE_CHAIN_V1 = 0,
    PRIME_NATIVE_HYP_CANDIDATE_PATH_V1,
} PrimeNativeHypCandidateKindV1;

typedef struct {
    PrimeNativeHypCandidateKindV1 kind;
    Atom *bias;
    Atom *sorts;
    Atom *primitives;
    Atom *source;
    Atom *target;
    size_t path_length;
    const char *rule_name;
} PrimeNativeHypCandidateRequestV1;

typedef struct {
    bool initialized;
    Arena arena;
    size_t count;
    PrimeNativeHypAdmissionCacheEntryV1
        entries[PRIME_NATIVE_HYP_ADMISSION_CACHE_CAPACITY];
} PrimeNativeHypAdmissionCacheV1;

static _Thread_local PrimeNativeHypAdmissionCacheV1
    g_prime_native_hyp_admission_cache;

static bool prime_native_relation_provider_current(
    Space *space, const PrimeNativeRelationProviderV1 *provider) {
    if (!space || !provider ||
        !cetta_prime_typed_finite_relation_is_current_v1(
            provider->relation, space) ||
        provider->raw_evidence_count !=
            cetta_prime_typed_finite_relation_occurrence_count_v1(
                provider->relation) ||
        (provider->raw_evidence_count != 0u &&
         !provider->raw_evidence_ids)) {
        return false;
    }
    for (size_t index = 0u; index < provider->raw_evidence_count; index++)
        if (provider->raw_evidence_ids[index] == CETTA_ATOM_ID_NONE)
            return false;
    return true;
}

static bool prime_native_relation_provider_retain(
    Arena *owner, Space *space,
    const PrimeNativeRelationProviderV1 *source,
    PrimeNativeRelationProviderV1 *target) {
    if (target) *target = (PrimeNativeRelationProviderV1){0};
    if (!owner || !space || !source || !target ||
        !prime_native_relation_provider_current(space, source) ||
        source->raw_evidence_count > SIZE_MAX / sizeof(AtomId)) {
        return false;
    }
    target->relation = cetta_prime_typed_finite_relation_retain_v1(
        owner, space, source->relation);
    target->raw_evidence_count = source->raw_evidence_count;
    target->raw_evidence_ids = source->raw_evidence_count == 0u
        ? NULL
        : arena_alloc(
              owner,
              source->raw_evidence_count *
                  sizeof(*target->raw_evidence_ids));
    if (!target->relation ||
        (source->raw_evidence_count != 0u &&
         !target->raw_evidence_ids)) {
        return false;
    }
    for (size_t index = 0u; index < source->raw_evidence_count; index++)
        target->raw_evidence_ids[index] = source->raw_evidence_ids[index];
    return true;
}

static void prime_native_hyp_admission_cache_reset(void) {
    if (g_prime_native_hyp_admission_cache.initialized)
        arena_free(&g_prime_native_hyp_admission_cache.arena);
    arena_init(&g_prime_native_hyp_admission_cache.arena);
    arena_set_runtime_kind(
        &g_prime_native_hyp_admission_cache.arena,
        CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    g_prime_native_hyp_admission_cache.initialized = true;
    g_prime_native_hyp_admission_cache.count = 0u;
}

static bool prime_native_hyp_admission_cache_find(
    Arena *owner, Space *space, AtomId source_program_id,
    PrimeNativeHypAdmittedV1 *admitted_out) {
    if (admitted_out) *admitted_out = (PrimeNativeHypAdmittedV1){0};
    if (!owner || !space || !space->native.universe ||
        source_program_id == CETTA_ATOM_ID_NONE || !admitted_out) {
        return false;
    }
    if (!g_prime_native_hyp_admission_cache.initialized)
        prime_native_hyp_admission_cache_reset();
    TermUniverse *universe = space->native.universe;
    for (size_t index = 0u;
         index < g_prime_native_hyp_admission_cache.count; index++) {
        const PrimeNativeHypAdmissionCacheEntryV1 *entry =
            &g_prime_native_hyp_admission_cache.entries[index];
        if (entry->space != space ||
            entry->space_instance_id != space_instance_id(space) ||
            entry->space_revision != space_revision(space) ||
            entry->universe != universe ||
            entry->universe_instance_id != universe->instance_id ||
            entry->universe_storage_epoch != universe->storage_epoch ||
            entry->source_program_id != source_program_id) {
            continue;
        }
        if (!cetta_prime_typed_value_v1_is_current(
                entry->program, space) ||
            (entry->denotation &&
             !cetta_prime_typed_value_v1_is_current(
                 entry->denotation, space)) ||
            (entry->finite_provider_available &&
             !prime_native_relation_provider_current(
                 space, &entry->finite_provider))) {
            prime_native_hyp_admission_cache_reset();
            return false;
        }
        CettaPrimeTypedValueV1 *program =
            cetta_prime_typed_value_retain_private_v1(
            owner, space, entry->program);
        CettaPrimeTypedValueV1 *denotation = entry->denotation
            ? cetta_prime_typed_value_retain_private_v1(
                  owner, space, entry->denotation)
            : NULL;
        if (!program || (entry->denotation && !denotation)) {
            prime_native_hyp_admission_cache_reset();
            return false;
        }
        *admitted_out = (PrimeNativeHypAdmittedV1){
            .program = program,
            .denotation = denotation,
            .finite_provider_checked = entry->finite_provider_checked,
            .finite_provider = entry->finite_provider_available
                ? &entry->finite_provider
                : NULL,
        };
        return true;
    }
    return false;
}

static void prime_native_hyp_admission_cache_store(
    Space *space, AtomId source_program_id,
    const CettaPrimeTypedValueV1 *program,
    const CettaPrimeTypedValueV1 *denotation,
    bool finite_provider_checked,
    const PrimeNativeRelationProviderV1 *finite_provider) {
    if (!space || !space->native.universe ||
        source_program_id == CETTA_ATOM_ID_NONE || !program ||
        !cetta_prime_typed_value_v1_is_current(program, space) ||
        (denotation &&
         !cetta_prime_typed_value_v1_is_current(denotation, space)) ||
        (finite_provider &&
         !prime_native_relation_provider_current(
             space, finite_provider))) {
        return;
    }
    if (!g_prime_native_hyp_admission_cache.initialized ||
        g_prime_native_hyp_admission_cache.count ==
            PRIME_NATIVE_HYP_ADMISSION_CACHE_CAPACITY) {
        prime_native_hyp_admission_cache_reset();
    }
    CettaPrimeTypedValueV1 *retained_program =
        cetta_prime_typed_value_retain_private_v1(
            &g_prime_native_hyp_admission_cache.arena, space, program);
    CettaPrimeTypedValueV1 *retained_denotation = denotation
        ? cetta_prime_typed_value_retain_private_v1(
              &g_prime_native_hyp_admission_cache.arena,
              space, denotation)
        : NULL;
    PrimeNativeRelationProviderV1 retained_provider = {0};
    if (!retained_program || (denotation && !retained_denotation) ||
        (finite_provider &&
         !prime_native_relation_provider_retain(
             &g_prime_native_hyp_admission_cache.arena, space,
             finite_provider, &retained_provider))) {
        return;
    }
    TermUniverse *universe = space->native.universe;
    g_prime_native_hyp_admission_cache.entries[
        g_prime_native_hyp_admission_cache.count++] =
        (PrimeNativeHypAdmissionCacheEntryV1){
            .space = space,
            .space_instance_id = space_instance_id(space),
            .space_revision = space_revision(space),
            .universe = universe,
            .universe_instance_id = universe->instance_id,
            .universe_storage_epoch = universe->storage_epoch,
            .source_program_id = source_program_id,
            .program = retained_program,
            .denotation = retained_denotation,
            .finite_provider_checked = finite_provider_checked,
            .finite_provider_available = finite_provider != NULL,
            .finite_provider = retained_provider,
        };
}

static const char *const PRIME_NATIVE_HYP_RUN_EQUATIONS_V1[] = {
    "(= (hyp:run (quote (hyp:primitive $sorts $primitives "
    "$source-sort $target-sort $symbol)) $input) "
    "(hyp:run-primitive $source-sort $target-sort $symbol $input))",
    "(= (hyp:run (quote (App (App (App (App (App hyp:primitive "
    "$sorts) $primitives) $source-sort) $target-sort) $symbol)) "
    "$input) (hyp:run-primitive $source-sort $target-sort $symbol $input))",
    "(= (hyp:run (quote (hyp:chain $sorts $primitives $source-sort "
    "$middle-sort $target-sort $earlier $later)) $input) "
    "(hyp:run-chain (quote $earlier) (quote $later) $input))",
    "(= (hyp:run (quote (App (App (App (App (App (App (App "
    "hyp:chain $sorts) $primitives) $source-sort) $middle-sort) "
    "$target-sort) $earlier) $later)) $input) "
    "(hyp:run-chain (quote $earlier) (quote $later) $input))",
};

static const char *const PRIME_NATIVE_HYP_RUN_CHAIN_EQUATIONS_V1[] = {
    "(= (hyp:run-chain $earlier $later $input) "
    "(let (hyp:edge $middle $earlier-proof) "
    "(hyp:run $earlier $input) "
    "(let (hyp:edge $target $later-proof) "
    "(hyp:run $later $middle) "
    "(hyp:edge $target (hyp:chain-proof $middle $earlier-proof "
    "$later-proof)))))",
};

static const char *const
    PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1[] = {
    "(= (hyp:primitive-declaration $bias $primitives) "
    "(match $bias "
    "(: $symbol ($primitives $source-sort $target-sort)) "
    "(hyp:declaration $source-sort $target-sort $symbol)))",
};

static const char *const
    PRIME_NATIVE_HYP_CHAIN_CANDIDATE_TYPED_EQUATIONS_V1[] = {
    "(= (hyp:chain-candidate-typed "
    "$bias $sorts $primitives $source-sort $target-sort) "
    "(let (hyp:declaration $source-sort $middle-sort $earlier-symbol) "
    "(hyp:primitive-declaration $bias $primitives) "
    "(let (hyp:declaration $middle-sort $target-sort $later-symbol) "
    "(hyp:primitive-declaration $bias $primitives) "
    "(quote (hyp:chain $sorts $primitives "
    "$source-sort $middle-sort $target-sort "
    "(hyp:primitive $sorts $primitives "
    "$source-sort $middle-sort $earlier-symbol) "
    "(hyp:primitive $sorts $primitives "
    "$middle-sort $target-sort $later-symbol))))))",
};

static const char *const PRIME_NATIVE_HYP_PATH_PREPEND_EQUATIONS_V1[] = {
    "(= (hyp:path:prepend $sorts $primitives $source-sort "
    "$middle-sort $target-sort $symbol (quote $later-program)) "
    "(quote (hyp:chain $sorts $primitives "
    "$source-sort $middle-sort $target-sort "
    "(hyp:primitive $sorts $primitives "
    "$source-sort $middle-sort $symbol) $later-program)))",
};

static const char *const
    PRIME_NATIVE_HYP_PATH_CANDIDATE_TYPED_EQUATIONS_V1[] = {
    "(= (hyp:path-candidate-typed $bias $sorts $primitives "
    "$source-sort $target-sort hyp:path:one) "
    "(let (hyp:declaration $source-sort $target-sort $symbol) "
    "(hyp:primitive-declaration $bias $primitives) "
    "(quote (hyp:primitive $sorts $primitives "
    "$source-sort $target-sort $symbol))))",
    "(= (hyp:path-candidate-typed $bias $sorts $primitives "
    "$source-sort $target-sort (hyp:path:more $shape)) "
    "(let (hyp:declaration $source-sort $middle-sort $symbol) "
    "(hyp:primitive-declaration $bias $primitives) "
    "(let $later-program (hyp:path-candidate-typed "
    "$bias $sorts $primitives $middle-sort $target-sort $shape) "
    "(hyp:path:prepend $sorts $primitives "
    "$source-sort $middle-sort $target-sort "
    "$symbol $later-program))))",
};

static const char *const PRIME_NATIVE_MAP_REL_RUN_EQUATIONS_V1[] = {
    "(= (map-rel:run $relation $source-list) "
    "(if (== $source-list ()) "
    "(map-rel:edge () (map-rel:nil-proof)) "
    "(if (== (get-metatype $source-list) Expression) "
    "(let ($source-head $source-tail) (decons-atom $source-list) "
    "(let (rel:edge $target-head $head-evidence) "
    "(rel:apply $relation $source-head) "
    "(let (map-rel:edge $target-tail $tail-evidence) "
    "(map-rel:run $relation $source-tail) "
    "(map-rel:edge (cons-atom $target-head $target-tail) "
    "(map-rel:cons-proof $source-head $target-head "
    "$head-evidence $tail-evidence))))) "
    "(superpose ()))))",
};

static Atom *prime_native_parse_one(
    Arena *owner, const char *source) {
    size_t position = 0u;
    Atom *term = owner && source
        ? parse_sexpr(owner, source, &position)
        : NULL;
    return term && parser_rest_is_delimiters(source, &position)
        ? term
        : NULL;
}

static bool prime_native_patterns_overlap(Atom *left, Atom *right) {
    if (!left || !right) return false;
    Bindings overlap;
    bindings_init(&overlap);
    bool result = match_atoms(left, right, &overlap);
    bindings_free(&overlap);
    return result;
}

static bool prime_native_equation_profile_exact(
    Arena *scratch, Space *space, const char *head_name,
    const char *const *expected_sources, size_t expected_count) {
    if (!scratch || !space || !head_name || !expected_sources ||
        expected_count == 0u || !g_symbols) {
        return false;
    }
    SymbolId head = symbol_intern_cstr(g_symbols, head_name);
    SpaceEquationCursor cursor;
    if (head == SYMBOL_ID_NONE ||
        !space_equation_cursor_init(space, head, &cursor)) {
        return false;
    }
    Atom **expected = arena_alloc(
        scratch, expected_count * sizeof(*expected));
    Atom **expected_lhs = arena_alloc(
        scratch, expected_count * sizeof(*expected_lhs));
    if (!expected || !expected_lhs) return false;
    for (size_t index = 0u; index < expected_count; index++) {
        expected[index] = prime_native_parse_one(
            scratch, expected_sources[index]);
        expected_lhs[index] = expected[index] &&
                expected[index]->kind == ATOM_EXPR &&
                expected[index]->expr.len == 3u &&
                atom_is_symbol(expected[index]->expr.elems[0], "=")
            ? expected[index]->expr.elems[1]
            : NULL;
        if (!expected_lhs[index]) return false;
    }

    size_t matched = 0u;
    for (;;) {
        SpaceEquationOccurrenceId occurrence_id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &occurrence_id);
        if (step == SPACE_EQUATION_CURSOR_END) break;
        if (step != SPACE_EQUATION_CURSOR_ITEM) return false;
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(
                occurrence_id, &occurrence)) {
            return false;
        }
        if (matched < expected_count &&
            atom_alpha_eq(occurrence.equation, expected[matched])) {
            matched++;
            continue;
        }

        /* The Space cursor intentionally includes wildcard-head equations:
         * they might match this operation.  An equation whose structured
         * head is provably disjoint is merely a consumer of the operation,
         * not another definition of it.  Any overlapping wildcard still
         * invalidates the exact admitted presentation. */
        for (size_t index = 0u; index < expected_count; index++) {
            if (prime_native_patterns_overlap(
                    occurrence.lhs, expected_lhs[index])) {
                return false;
            }
        }
    }
    return matched == expected_count;
}

/* `hyp:run` is an authored open relation.  Its native realization is current
 * only while the exact presentation whose recursion it replaces is the live
 * one.  Any extension or drift declines to the complete relational path. */
static bool prime_native_hyp_presentation_admitted(
    Arena *scratch, Space *space) {
    if (!scratch || !space) return false;
    uint64_t instance_id = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (g_prime_native_hyp_presentation_cache.space == space &&
        g_prime_native_hyp_presentation_cache.space_instance_id ==
            instance_id &&
        g_prime_native_hyp_presentation_cache.space_revision == revision) {
        return g_prime_native_hyp_presentation_cache.admitted;
    }
    bool admitted =
        prime_native_equation_profile_exact(
            scratch, space, "hyp:run",
            PRIME_NATIVE_HYP_RUN_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_RUN_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_RUN_EQUATIONS_V1[0])) &&
        prime_native_equation_profile_exact(
            scratch, space, "hyp:run-chain",
            PRIME_NATIVE_HYP_RUN_CHAIN_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_RUN_CHAIN_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_RUN_CHAIN_EQUATIONS_V1[0]));
    g_prime_native_hyp_presentation_cache =
        (PrimeNativeHypPresentationCacheV1){
            .space = space,
            .space_instance_id = instance_id,
            .space_revision = revision,
            .admitted = admitted,
        };
    return admitted;
}

/* Candidate construction is licensed by the exact authored relations it
 * realizes, independently of the `hyp:run` execution presentation.  The
 * parser contributes no authority metadata; current Space identity and the
 * typed result receipt are the admission boundary. */
static bool prime_native_hyp_candidate_presentation_admitted(
    Arena *scratch, Space *space) {
    if (!scratch || !space) return false;
    uint64_t instance_id = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (g_prime_native_hyp_candidate_presentation_cache.space == space &&
        g_prime_native_hyp_candidate_presentation_cache.space_instance_id ==
            instance_id &&
        g_prime_native_hyp_candidate_presentation_cache.space_revision ==
            revision) {
        return g_prime_native_hyp_candidate_presentation_cache.admitted;
    }
    bool admitted =
        prime_native_equation_profile_exact(
            scratch, space, "hyp:primitive-declaration",
            PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1[0])) &&
        prime_native_equation_profile_exact(
            scratch, space, "hyp:chain-candidate-typed",
            PRIME_NATIVE_HYP_CHAIN_CANDIDATE_TYPED_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_CHAIN_CANDIDATE_TYPED_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_CHAIN_CANDIDATE_TYPED_EQUATIONS_V1[0]));
    g_prime_native_hyp_candidate_presentation_cache =
        (PrimeNativeHypPresentationCacheV1){
            .space = space,
            .space_instance_id = instance_id,
            .space_revision = revision,
            .admitted = admitted,
        };
    return admitted;
}

/* Length-indexed candidate search is a second realization of the authored
 * path relation, not an extension of `hyp`.  Its admission therefore names
 * the recursive relation and the one staging helper that constructs ordinary
 * primitive/chain syntax. */
static bool prime_native_hyp_path_candidate_presentation_admitted(
    Arena *scratch, Space *space) {
    if (!scratch || !space) return false;
    uint64_t instance_id = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (g_prime_native_hyp_path_candidate_presentation_cache.space == space &&
        g_prime_native_hyp_path_candidate_presentation_cache
                .space_instance_id == instance_id &&
        g_prime_native_hyp_path_candidate_presentation_cache.space_revision ==
            revision) {
        return g_prime_native_hyp_path_candidate_presentation_cache.admitted;
    }
    bool admitted =
        prime_native_equation_profile_exact(
            scratch, space, "hyp:primitive-declaration",
            PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_PRIMITIVE_DECLARATION_EQUATIONS_V1[0])) &&
        prime_native_equation_profile_exact(
            scratch, space, "hyp:path:prepend",
            PRIME_NATIVE_HYP_PATH_PREPEND_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_PATH_PREPEND_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_PATH_PREPEND_EQUATIONS_V1[0])) &&
        prime_native_equation_profile_exact(
            scratch, space, "hyp:path-candidate-typed",
            PRIME_NATIVE_HYP_PATH_CANDIDATE_TYPED_EQUATIONS_V1,
            sizeof(PRIME_NATIVE_HYP_PATH_CANDIDATE_TYPED_EQUATIONS_V1) /
                sizeof(PRIME_NATIVE_HYP_PATH_CANDIDATE_TYPED_EQUATIONS_V1[0]));
    g_prime_native_hyp_path_candidate_presentation_cache =
        (PrimeNativeHypPresentationCacheV1){
            .space = space,
            .space_instance_id = instance_id,
            .space_revision = revision,
            .admitted = admitted,
        };
    return admitted;
}

/* The List lifting realization belongs to the exact authored relational
 * operation.  The parse layer supplies only this ordinary equation; the
 * current Space identity and the typed result are the admission seam. */
static bool prime_native_map_rel_presentation_admitted(
    Arena *scratch, Space *space) {
    if (!scratch || !space) return false;
    uint64_t instance_id = space_instance_id(space);
    uint64_t revision = space_revision(space);
    if (g_prime_native_map_rel_presentation_cache.space == space &&
        g_prime_native_map_rel_presentation_cache.space_instance_id ==
            instance_id &&
        g_prime_native_map_rel_presentation_cache.space_revision ==
            revision) {
        return g_prime_native_map_rel_presentation_cache.admitted;
    }
    bool admitted = prime_native_equation_profile_exact(
        scratch, space, "map-rel:run",
        PRIME_NATIVE_MAP_REL_RUN_EQUATIONS_V1,
        sizeof(PRIME_NATIVE_MAP_REL_RUN_EQUATIONS_V1) /
            sizeof(PRIME_NATIVE_MAP_REL_RUN_EQUATIONS_V1[0]));
    g_prime_native_map_rel_presentation_cache =
        (PrimeNativeHypPresentationCacheV1){
            .space = space,
            .space_instance_id = instance_id,
            .space_revision = revision,
            .admitted = admitted,
        };
    return admitted;
}

static CettaPrimeNativeExecutionV1 prime_native_declined(void) {
    return (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_DECLINED,
    };
}

static CettaPrimeNativeExecutionV1 prime_native_fault(void) {
    return (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_FAULT,
    };
}

/* Observe the one native-calculus hosting seam without changing its
 * semantics.  A decline still returns to ordinary relational execution, and
 * an implementation fault remains distinct from every typing outcome. */
static CettaPrimeNativeExecutionV1 prime_native_observe(
    CettaPrimeNativeExecutionV1 result,
    CettaRuntimeCounter realized_counter) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_CANDIDATE);
    switch (result.kind) {
    case CETTA_PRIME_NATIVE_EXECUTION_REALIZED:
        cetta_runtime_stats_inc(realized_counter);
        break;
    case CETTA_PRIME_NATIVE_EXECUTION_DECLINED:
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_DECLINED);
        break;
    case CETTA_PRIME_NATIVE_EXECUTION_FAULT:
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_CALCULUS_FAULT);
        break;
    }
    return result;
}

static bool prime_native_authored_lambda(Atom *term, Atom **body_out) {
    if (body_out) *body_out = NULL;
    if (!term || !body_out || term->kind != ATOM_EXPR ||
        term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], "lam")) {
        return false;
    }
    *body_out = term->expr.elems[2];
    return true;
}

/* This is only a cheap admission precheck.  Exact recognition happens on the
 * canonical intrinsic term in `cetta_prime_typed_list_map_v1`; a four-lambda
 * lookalike therefore cannot acquire the realization. */
static bool prime_native_maybe_map_application(Atom *application) {
    if (!application || application->kind != ATOM_EXPR ||
        application->expr.len != 5u) {
        return false;
    }
    Atom *body = application->expr.elems[0];
    for (size_t binder = 0u; binder < 4u; binder++)
        if (!prime_native_authored_lambda(body, &body)) return false;
    return true;
}

static bool prime_native_hyp_run_application(
    Atom *application, Atom **program_out, Atom **input_out) {
    if (program_out) *program_out = NULL;
    if (input_out) *input_out = NULL;
    if (!application || !program_out || !input_out ||
        application->kind != ATOM_EXPR || application->expr.len != 3u ||
        !atom_is_symbol(application->expr.elems[0], "hyp:run")) {
        return false;
    }
    Atom *quoted = application->expr.elems[1];
    if (!quoted || quoted->kind != ATOM_EXPR || quoted->expr.len != 2u ||
        !atom_is_symbol(quoted->expr.elems[0], "quote")) {
        return false;
    }
    *program_out = quoted->expr.elems[1];
    *input_out = application->expr.elems[2];
    return true;
}

static bool prime_native_map_rel_run_application(
    Atom *application, Atom **relation_out, Atom **source_list_out) {
    if (relation_out) *relation_out = NULL;
    if (source_list_out) *source_list_out = NULL;
    if (!application || !relation_out || !source_list_out ||
        application->kind != ATOM_EXPR || application->expr.len != 3u ||
        !atom_is_symbol(application->expr.elems[0], "map-rel:run")) {
        return false;
    }
    *relation_out = application->expr.elems[1];
    *source_list_out = application->expr.elems[2];
    return true;
}

static bool prime_native_hyp_relation_descriptor(
    Atom *relation, Atom **program_out) {
    if (program_out) *program_out = NULL;
    if (!relation || !program_out || relation->kind != ATOM_EXPR ||
        relation->expr.len != 2u ||
        !atom_is_symbol(relation->expr.elems[0], "hyp:relation")) {
        return false;
    }
    Atom *quoted = relation->expr.elems[1];
    if (!quoted || quoted->kind != ATOM_EXPR || quoted->expr.len != 2u ||
        !atom_is_symbol(quoted->expr.elems[0], "quote")) {
        return false;
    }
    *program_out = quoted->expr.elems[1];
    return true;
}

static bool prime_native_hyp_path_shape_length(
    Atom *shape, size_t *length_out) {
    if (length_out) *length_out = 0u;
    if (!shape || !length_out) return false;
    size_t length = 1u;
    Atom *cursor = shape;
    while (cursor && cursor->kind == ATOM_EXPR &&
           cursor->expr.len == 2u &&
           atom_is_symbol(cursor->expr.elems[0], "hyp:path:more")) {
        if (length >= PRIME_NATIVE_HYP_PATH_MAX_LENGTH) return false;
        length++;
        cursor = cursor->expr.elems[1];
    }
    if (!atom_is_symbol(cursor, "hyp:path:one")) return false;
    *length_out = length;
    return true;
}

static bool prime_native_hyp_candidate_application(
    Atom *application, PrimeNativeHypCandidateRequestV1 *request_out) {
    if (request_out) *request_out = (PrimeNativeHypCandidateRequestV1){0};
    if (!application || !request_out || application->kind != ATOM_EXPR)
        return false;
    if (application->expr.len == 6u &&
        atom_is_symbol(
            application->expr.elems[0], "hyp:chain-candidate-typed")) {
        *request_out = (PrimeNativeHypCandidateRequestV1){
            .kind = PRIME_NATIVE_HYP_CANDIDATE_CHAIN_V1,
            .bias = application->expr.elems[1],
            .sorts = application->expr.elems[2],
            .primitives = application->expr.elems[3],
            .source = application->expr.elems[4],
            .target = application->expr.elems[5],
            .path_length = 2u,
            .rule_name = "hyp:chain-candidate-typed",
        };
        return true;
    }
    if (application->expr.len == 7u &&
        atom_is_symbol(
            application->expr.elems[0], "hyp:path-candidate-typed")) {
        size_t path_length = 0u;
        if (!prime_native_hyp_path_shape_length(
                application->expr.elems[6], &path_length)) {
            return false;
        }
        *request_out = (PrimeNativeHypCandidateRequestV1){
            .kind = PRIME_NATIVE_HYP_CANDIDATE_PATH_V1,
            .bias = application->expr.elems[1],
            .sorts = application->expr.elems[2],
            .primitives = application->expr.elems[3],
            .source = application->expr.elems[4],
            .target = application->expr.elems[5],
            .path_length = path_length,
            .rule_name = "hyp:path-candidate-typed",
        };
        return true;
    }
    return false;
}

static bool prime_native_import(
    Arena *owner, Space *space, Atom *term,
    CettaPrimeTypedValueV1 **value_out) {
    CettaPrimeTypingSynthesisObservationV1 observation;
    return cetta_prime_typed_value_import_term_v1(
        owner, space, term, false, 0u, &observation, value_out);
}

static PrimeNativePlanBuildV1 prime_native_import_one(
    Arena *owner, Space *space, Atom *term,
    CettaPrimeTypedValueV1 **value_out) {
    if (value_out) *value_out = NULL;
    if (!value_out || !prime_native_import(owner, space, term, value_out))
        return PRIME_NATIVE_PLAN_FAULT;
    return *value_out
        ? PRIME_NATIVE_PLAN_BUILT
        : PRIME_NATIVE_PLAN_DECLINED;
}

static PrimeNativePlanBuildV1 prime_native_import_checked_one(
    Arena *owner, Space *space, Atom *term,
    const CettaPrimeTypedValueV1 *expected_type,
    CettaPrimeTypedValueV1 **value_out) {
    if (value_out) *value_out = NULL;
    if (!owner || !space || !term || !expected_type || !value_out)
        return PRIME_NATIVE_PLAN_FAULT;
    CettaPrimeTypingCheckingObservationV1 observation;
    if (!cetta_prime_typed_value_import_checked_term_v1(
            owner, space, term, expected_type,
            false, 0u, &observation, value_out)) {
        return PRIME_NATIVE_PLAN_FAULT;
    }
    return *value_out
        ? PRIME_NATIVE_PLAN_BUILT
        : PRIME_NATIVE_PLAN_DECLINED;
}

/* Resolve one closed authored semantic assignment without evaluating it.
 * The stronger native tier accepts exactly one ground equation with the
 * requested left-hand side.  Missing, open, duplicated, or concurrently
 * changing assignments simply leave the ordinary relational tier in charge. */
static PrimeNativePlanBuildV1 prime_native_exact_authored_value(
    Arena *owner, Space *space, Atom *call,
    CettaPrimeTypedValueV1 **value_out) {
    if (value_out) *value_out = NULL;
    if (!owner || !space || !space->native.universe || !call ||
        !value_out || call->kind != ATOM_EXPR || call->expr.len < 2u ||
        call->expr.elems[0]->kind != ATOM_SYMBOL || atom_has_vars(call)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }

    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(
            space, call->expr.elems[0]->sym_id, &cursor)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    Atom *rhs = NULL;
    size_t matches = 0u;
    for (;;) {
        SpaceEquationOccurrenceId occurrence_id;
        SpaceEquationCursorStep step = space_equation_cursor_next(
            &cursor, &occurrence_id);
        if (step == SPACE_EQUATION_CURSOR_END) break;
        if (step == SPACE_EQUATION_CURSOR_INVALIDATED)
            return PRIME_NATIVE_PLAN_DECLINED;
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(
                occurrence_id, &occurrence)) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        if (!atom_alpha_eq(occurrence.lhs, call)) continue;
        matches++;
        rhs = occurrence.rhs;
    }
    if (matches != 1u || !rhs || atom_has_vars(rhs))
        return PRIME_NATIVE_PLAN_DECLINED;

    AtomId rhs_id = term_universe_store_atom_id(
        space->native.universe, owner, rhs);
    Atom *owned_rhs = rhs_id != CETTA_ATOM_ID_NONE
        ? term_universe_copy_atom(
              space->native.universe, owner, rhs_id)
        : NULL;
    if (!owned_rhs) return PRIME_NATIVE_PLAN_FAULT;
    return prime_native_import_one(
        owner, space, owned_rhs, value_out);
}

/* Runtime inputs may be authored n-ary syntax or intrinsic left-associated
 * `App`.  Both are ordinary presentations of the same Prime application. */
static bool prime_native_application_spine(
    Atom *term, const char *head_name,
    Atom **arguments, size_t argument_count) {
    if (!term || !head_name || !arguments || argument_count == 0u)
        return false;
    if (term->kind == ATOM_EXPR &&
        term->expr.len == argument_count + 1u &&
        atom_is_symbol(term->expr.elems[0], head_name)) {
        for (size_t index = 0u; index < argument_count; index++)
            arguments[index] = term->expr.elems[index + 1u];
        return true;
    }
    return cetta_prime_typed_application_spine_private_v1(
        term, head_name, arguments, argument_count);
}

static Atom *prime_native_binder(
    Arena *owner, const char *name, Atom *type) {
    Atom *items[3] = {
        atom_symbol(owner, name), atom_symbol(owner, ":"), type,
    };
    return atom_expr(owner, items, 3u);
}

static Atom *prime_native_map_type(
    Arena *owner, Atom *source_universe, Atom *target_universe) {
    Atom *source = atom_symbol(owner, "source");
    Atom *target = atom_symbol(owner, "target");
    Atom *function_type = atom_expr3(
        owner, atom_symbol(owner, "->"), source, target);
    Atom *source_list = atom_expr2(
        owner, atom_symbol(owner, "list"), source);
    Atom *target_list = atom_expr2(
        owner, atom_symbol(owner, "list"), target);
    Atom *items[6] = {
        atom_symbol(owner, "->"),
        prime_native_binder(owner, "source", source_universe),
        prime_native_binder(owner, "target", target_universe),
        prime_native_binder(owner, "function", function_type),
        prime_native_binder(owner, "xs", source_list),
        target_list,
    };
    for (size_t index = 0u; index < 6u; index++)
        if (!items[index]) return NULL;
    return atom_expr(owner, items, 6u);
}

static Atom *prime_native_type_of(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *value) {
    Atom *term = NULL;
    Atom *type = NULL;
    if (!value || !space || !space->native.universe ||
        !cetta_prime_typed_value_v1_erase(
            value, space->native.universe, owner, &term, &type)) {
        return NULL;
    }
    (void)term;
    return cetta_prime_regular_term_quote_intrinsic_v1(owner, type);
}

/* Runtime erasure differs from a type-query quotation at `App`: application
 * becomes ordinary MeTTa syntax so the normal evaluator may continue with a
 * guest function instead of receiving an internal DTT constructor. */
static Atom *prime_native_runtime_quote(Arena *owner, Atom *intrinsic) {
    if (!owner || !intrinsic) return NULL;
    if (intrinsic->kind == ATOM_EXPR && intrinsic->expr.len == 3u &&
        atom_is_symbol(intrinsic->expr.elems[0], "App")) {
        Atom *function = prime_native_runtime_quote(
            owner, intrinsic->expr.elems[1]);
        Atom *argument = prime_native_runtime_quote(
            owner, intrinsic->expr.elems[2]);
        return function && argument
            ? atom_expr2(owner, function, argument)
            : NULL;
    }
    if (intrinsic->kind != ATOM_EXPR)
        return cetta_prime_regular_term_quote_intrinsic_v1(
            owner, intrinsic);
    if (intrinsic->expr.len > SIZE_MAX / sizeof(Atom *)) return NULL;
    Atom **items = arena_alloc(
        owner, (size_t)intrinsic->expr.len * sizeof(*items));
    for (CettaExprIndex index = 0u;
         index < intrinsic->expr.len; index++) {
        items[index] = prime_native_runtime_quote(
            owner, intrinsic->expr.elems[index]);
        if (!items[index]) return NULL;
    }
    return atom_expr(owner, items, intrinsic->expr.len);
}

static CettaPrimeNativeExecutionV1 prime_native_try_map_application(
    Arena *owner, Space *space, Atom *application) {
    if (!owner || !space || !space->native.universe ||
        !prime_native_maybe_map_application(application)) {
        return prime_native_declined();
    }

    CettaPrimeTypedValueV1 *source_type = NULL;
    CettaPrimeTypedValueV1 *target_type = NULL;
    if (!prime_native_import(
            owner, space, application->expr.elems[1], &source_type) ||
        !prime_native_import(
            owner, space, application->expr.elems[2], &target_type)) {
        return prime_native_fault();
    }
    if (!source_type || !target_type) return prime_native_declined();

    Atom *source_universe = prime_native_type_of(
        owner, space, source_type);
    Atom *target_universe = prime_native_type_of(
        owner, space, target_type);
    Atom *map_type_term = source_universe && target_universe
        ? prime_native_map_type(owner, source_universe, target_universe)
        : NULL;
    CettaPrimeTypedValueV1 *map_type = NULL;
    if (!map_type_term ||
        !prime_native_import(owner, space, map_type_term, &map_type)) {
        return prime_native_fault();
    }
    if (!map_type) return prime_native_declined();

    CettaPrimeTypingCheckingObservationV1 program_observation;
    CettaPrimeTypedValueV1 *program = NULL;
    if (!cetta_prime_typed_value_import_checked_term_v1(
            owner, space, application->expr.elems[0], map_type,
            false, 0u, &program_observation, &program)) {
        return prime_native_fault();
    }
    if (!program) return prime_native_declined();

    CettaPrimeTypedValueV1 *function = NULL;
    CettaPrimeTypedValueV1 *list = NULL;
    if (!prime_native_import(
            owner, space, application->expr.elems[3], &function) ||
        !prime_native_import(
            owner, space, application->expr.elems[4], &list)) {
        return prime_native_fault();
    }
    if (!function || !list) return prime_native_declined();
    list = cetta_prime_typed_value_attach_indexed_application_private_v1(
        owner, space, list, "list", 1u, 0u);
    if (!list) return prime_native_declined();

    CettaPrimeTypedValueV1 *mapped = cetta_prime_typed_list_map_v1(
        owner, space, program, source_type, target_type, function, list);
    if (!mapped) return prime_native_declined();

    Atom *intrinsic_runtime = NULL;
    if (!cetta_prime_typed_list_runtime_representation_v1(
            owner, space, mapped, &intrinsic_runtime)) {
        return prime_native_fault();
    }
    Atom *value = prime_native_runtime_quote(owner, intrinsic_runtime);
    if (!value) return prime_native_fault();
    return (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_REALIZED,
        .value = value,
        .typed_value = mapped,
    };
}

static Atom *prime_native_fresh_variable(
    Arena *owner, const char *spelling) {
    VarId id = VAR_ID_NONE;
    return owner && spelling && fresh_var_id_try(&id)
        ? atom_var_with_id(owner, spelling, id)
        : NULL;
}

static PrimeNativePlanBuildV1 prime_native_hyp_typed_program(
    Arena *owner, Space *space, Atom *program,
    CettaPrimeTypedValueV1 **typed_out) {
    if (typed_out) *typed_out = NULL;
    if (!owner || !space || !program || !typed_out)
        return PRIME_NATIVE_PLAN_FAULT;

    Atom *primitive_arguments[5] = {0};
    if (prime_native_application_spine(
            program, "hyp:primitive", primitive_arguments, 5u)) {
        Atom *rule_term = atom_expr3(
            owner, atom_symbol(owner, "hyp:primitive"),
            primitive_arguments[0], primitive_arguments[1]);
        CettaPrimeTypedValueV1 *rule = NULL;
        CettaPrimeTypedValueV1 *source = NULL;
        CettaPrimeTypedValueV1 *target = NULL;
        CettaPrimeTypedValueV1 *symbol = NULL;
        if (!rule_term) return PRIME_NATIVE_PLAN_FAULT;
        PrimeNativePlanBuildV1 imported = prime_native_import_one(
            owner, space, rule_term, &rule);
        if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
        imported = prime_native_import_one(
            owner, space, primitive_arguments[2], &source);
        if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
        imported = prime_native_import_one(
            owner, space, primitive_arguments[3], &target);
        if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
        imported = prime_native_import_one(
            owner, space, primitive_arguments[4], &symbol);
        if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
        *typed_out = cetta_prime_typed_hyp_primitive_v1(
            owner, space, rule, source, target, symbol);
        return *typed_out
            ? PRIME_NATIVE_PLAN_BUILT
            : PRIME_NATIVE_PLAN_DECLINED;
    }

    Atom *chain_arguments[7] = {0};
    if (!prime_native_application_spine(
            program, "hyp:chain", chain_arguments, 7u)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    Atom *rule_term = atom_expr3(
        owner, atom_symbol(owner, "hyp:chain"),
        chain_arguments[0], chain_arguments[1]);
    CettaPrimeTypedValueV1 *rule = NULL;
    CettaPrimeTypedValueV1 *source = NULL;
    CettaPrimeTypedValueV1 *middle = NULL;
    CettaPrimeTypedValueV1 *target = NULL;
    CettaPrimeTypedValueV1 *earlier = NULL;
    CettaPrimeTypedValueV1 *later = NULL;
    if (!rule_term) return PRIME_NATIVE_PLAN_FAULT;
    PrimeNativePlanBuildV1 imported = prime_native_import_one(
        owner, space, rule_term, &rule);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    imported = prime_native_import_one(
        owner, space, chain_arguments[2], &source);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    imported = prime_native_import_one(
        owner, space, chain_arguments[3], &middle);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    imported = prime_native_import_one(
        owner, space, chain_arguments[4], &target);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    PrimeNativePlanBuildV1 built = prime_native_hyp_typed_program(
        owner, space, chain_arguments[5], &earlier);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    built = prime_native_hyp_typed_program(
        owner, space, chain_arguments[6], &later);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    *typed_out = cetta_prime_typed_hyp_chain_v1(
        owner, space, rule, source, middle, target, earlier, later);
    return *typed_out
        ? PRIME_NATIVE_PLAN_BUILT
        : PRIME_NATIVE_PLAN_DECLINED;
}

typedef enum {
    PRIME_NATIVE_DECLARATION_IRRELEVANT = 0,
    PRIME_NATIVE_DECLARATION_MATCHED,
    PRIME_NATIVE_DECLARATION_OPEN,
} PrimeNativeDeclarationMatchV1;

/* Primitive vocabularies are ordinary curried Prime families, while authored
 * declarations use convenient n-ary source syntax.  Accept both views at
 * this boundary and require exactly two family indices. */
static bool prime_native_binary_application_view(
    Atom *term, Atom **head_out, Atom **left_out, Atom **right_out) {
    if (head_out) *head_out = NULL;
    if (left_out) *left_out = NULL;
    if (right_out) *right_out = NULL;
    if (!term || !head_out || !left_out || !right_out)
        return false;
    if (term->kind == ATOM_EXPR && term->expr.len == 3u &&
        !atom_is_symbol(term->expr.elems[0], "App")) {
        *head_out = term->expr.elems[0];
        *left_out = term->expr.elems[1];
        *right_out = term->expr.elems[2];
        return true;
    }
    if (term->kind != ATOM_EXPR || term->expr.len != 3u ||
        !atom_is_symbol(term->expr.elems[0], "App")) {
        return false;
    }
    Atom *left_application = term->expr.elems[1];
    if (!left_application || left_application->kind != ATOM_EXPR ||
        left_application->expr.len != 3u ||
        !atom_is_symbol(left_application->expr.elems[0], "App")) {
        return false;
    }
    *head_out = left_application->expr.elems[1];
    *left_out = left_application->expr.elems[2];
    *right_out = term->expr.elems[2];
    return true;
}

/* Classify only declarations that the authored
 * `hyp:primitive-declaration` match could observe for this vocabulary.  An
 * open family head could unify with the requested vocabulary, so the native
 * operation declines rather than silently under-approximating it. */
static PrimeNativeDeclarationMatchV1
prime_native_hyp_primitive_declaration_view(
    Atom *form, Atom *primitives, Atom **source_out,
    Atom **target_out, Atom **symbol_out) {
    if (source_out) *source_out = NULL;
    if (target_out) *target_out = NULL;
    if (symbol_out) *symbol_out = NULL;
    if (!form || !primitives || !source_out || !target_out || !symbol_out ||
        form->kind != ATOM_EXPR || form->expr.len != 3u ||
        !atom_is_symbol(form->expr.elems[0], ":")) {
        return PRIME_NATIVE_DECLARATION_IRRELEVANT;
    }
    Atom *family = NULL;
    Atom *source = NULL;
    Atom *target = NULL;
    if (!prime_native_binary_application_view(
            form->expr.elems[2], &family, &source, &target)) {
        return PRIME_NATIVE_DECLARATION_IRRELEVANT;
    }
    if (atom_has_vars(family)) return PRIME_NATIVE_DECLARATION_OPEN;
    if (!atom_alpha_eq(family, primitives))
        return PRIME_NATIVE_DECLARATION_IRRELEVANT;
    Atom *symbol = form->expr.elems[1];
    if (!symbol || symbol->kind != ATOM_SYMBOL || atom_has_vars(form))
        return PRIME_NATIVE_DECLARATION_OPEN;
    *source_out = source;
    *target_out = target;
    *symbol_out = symbol;
    return PRIME_NATIVE_DECLARATION_MATCHED;
}

static PrimeNativePlanBuildV1 prime_native_hyp_collect_declarations(
    Arena *owner, Space *space, Atom *primitives,
    PrimeNativeHypPrimitiveDeclarationV1 **declarations_out,
    size_t *declaration_count_out) {
    if (declarations_out) *declarations_out = NULL;
    if (declaration_count_out) *declaration_count_out = 0u;
    if (!owner || !space || !space->native.universe || !primitives ||
        !declarations_out || !declaration_count_out) {
        return PRIME_NATIVE_PLAN_FAULT;
    }

    SpaceReadToken read = space_read_token(space);
    CettaCount logical_length = space_length64(space);
    if (!space_read_token_matches_live_space(read, space) ||
        logical_length > (CettaCount)SIZE_MAX) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    size_t count = 0u;
    for (CettaIndex index = 0u; index < logical_length; index++) {
        Atom *source = NULL;
        Atom *target = NULL;
        Atom *symbol = NULL;
        Atom *form = space_get_at64(space, index);
        PrimeNativeDeclarationMatchV1 match =
            prime_native_hyp_primitive_declaration_view(
                form, primitives, &source, &target, &symbol);
        if (match == PRIME_NATIVE_DECLARATION_OPEN)
            return PRIME_NATIVE_PLAN_DECLINED;
        if (match != PRIME_NATIVE_DECLARATION_MATCHED) continue;
        if (count == SIZE_MAX) return PRIME_NATIVE_PLAN_DECLINED;
        count++;
    }
    if (!space_read_token_matches_live_space(read, space))
        return PRIME_NATIVE_PLAN_DECLINED;

    if (count > SIZE_MAX / sizeof(PrimeNativeHypPrimitiveDeclarationV1))
        return PRIME_NATIVE_PLAN_DECLINED;
    PrimeNativeHypPrimitiveDeclarationV1 *declarations = count == 0u
        ? NULL
        : arena_alloc(owner, count * sizeof(*declarations));
    size_t written = 0u;
    for (CettaIndex index = 0u; index < logical_length; index++) {
        Atom *source = NULL;
        Atom *target = NULL;
        Atom *symbol = NULL;
        Atom *form = space_get_at64(space, index);
        PrimeNativeDeclarationMatchV1 match =
            prime_native_hyp_primitive_declaration_view(
                form, primitives, &source, &target, &symbol);
        if (match == PRIME_NATIVE_DECLARATION_OPEN)
            return PRIME_NATIVE_PLAN_DECLINED;
        if (match != PRIME_NATIVE_DECLARATION_MATCHED) continue;
        if (written >= count) return PRIME_NATIVE_PLAN_DECLINED;
        declarations[written++] = (PrimeNativeHypPrimitiveDeclarationV1){
            .source_sort = source,
            .target_sort = target,
            .symbol = symbol,
        };
    }
    if (written != count ||
        !space_read_token_matches_live_space(read, space)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    *declarations_out = declarations;
    *declaration_count_out = count;
    return PRIME_NATIVE_PLAN_BUILT;
}

static bool prime_native_current_bias(Space *space, Atom *bias) {
    return space && bias &&
        (atom_is_symbol(bias, "&self") ||
         (bias->kind == ATOM_GROUNDED &&
          bias->ground.gkind == GV_SPACE &&
          bias->ground.ptr == space));
}

static Atom *prime_native_hyp_primitive_term(
    Arena *owner, Atom *sorts, Atom *primitives,
    const PrimeNativeHypPrimitiveDeclarationV1 *declaration) {
    if (!owner || !sorts || !primitives || !declaration) return NULL;
    Atom *items[6] = {
        atom_symbol(owner, "hyp:primitive"), sorts, primitives,
        declaration->source_sort, declaration->target_sort,
        declaration->symbol,
    };
    return atom_expr(owner, items, sizeof(items) / sizeof(items[0]));
}

static bool prime_native_hyp_count_paths(
    const PrimeNativeHypPrimitiveDeclarationV1 *declarations,
    size_t declaration_count, Atom *current_source, Atom *target,
    size_t remaining, size_t *count_inout) {
    if (!current_source || !target || remaining == 0u || !count_inout ||
        (declaration_count != 0u && !declarations)) {
        return false;
    }
    for (size_t index = 0u; index < declaration_count; index++) {
        const PrimeNativeHypPrimitiveDeclarationV1 *declaration =
            &declarations[index];
        if (!atom_alpha_eq(declaration->source_sort, current_source))
            continue;
        if (remaining == 1u) {
            if (!atom_alpha_eq(declaration->target_sort, target)) continue;
            if (*count_inout == SIZE_MAX) return false;
            (*count_inout)++;
            continue;
        }
        if (!prime_native_hyp_count_paths(
                declarations, declaration_count,
                declaration->target_sort, target,
                remaining - 1u, count_inout)) {
            return false;
        }
    }
    return true;
}

typedef struct {
    Arena *owner;
    Space *space;
    Atom *sorts;
    Atom *primitives;
    Atom *target;
    size_t path_length;
    PrimeNativeHypPrimitiveDeclarationV1 *declarations;
    size_t declaration_count;
    size_t *path_indices;
    CettaPrimeTypedValueV1 *primitive_rule;
    CettaPrimeTypedValueV1 *chain_rule;
    CettaPrimeTypedValueV1 **typed_candidates;
    Atom **raw_candidates;
    size_t candidate_capacity;
    size_t candidate_count;
} PrimeNativeHypPathBuilderV1;

static PrimeNativePlanBuildV1 prime_native_hyp_materialize_path(
    PrimeNativeHypPathBuilderV1 *builder) {
    if (!builder || !builder->owner || !builder->space ||
        !builder->sorts || !builder->primitives || !builder->target ||
        builder->path_length == 0u || !builder->path_indices ||
        !builder->primitive_rule ||
        (builder->path_length > 1u && !builder->chain_rule) ||
        builder->candidate_count >= builder->candidate_capacity) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    size_t last_index = builder->path_indices[builder->path_length - 1u];
    if (last_index >= builder->declaration_count)
        return PRIME_NATIVE_PLAN_DECLINED;
    PrimeNativeHypPrimitiveDeclarationV1 *last =
        &builder->declarations[last_index];
    CettaPrimeTypedValueV1 *typed = cetta_prime_typed_hyp_primitive_v1(
        builder->owner, builder->space, builder->primitive_rule,
        last->typed_source_sort, last->typed_target_sort,
        last->typed_symbol);
    Atom *raw = prime_native_hyp_primitive_term(
        builder->owner, builder->sorts, builder->primitives, last);
    if (!typed || !raw) return PRIME_NATIVE_PLAN_DECLINED;

    for (size_t offset = builder->path_length - 1u; offset > 0u; offset--) {
        size_t declaration_index = builder->path_indices[offset - 1u];
        if (declaration_index >= builder->declaration_count)
            return PRIME_NATIVE_PLAN_DECLINED;
        PrimeNativeHypPrimitiveDeclarationV1 *left =
            &builder->declarations[declaration_index];
        CettaPrimeTypedValueV1 *earlier =
            cetta_prime_typed_hyp_primitive_v1(
                builder->owner, builder->space, builder->primitive_rule,
                left->typed_source_sort, left->typed_target_sort,
                left->typed_symbol);
        CettaPrimeTypedValueV1 *chained = earlier
            ? cetta_prime_typed_hyp_chain_v1(
                  builder->owner, builder->space, builder->chain_rule,
                  left->typed_source_sort, left->typed_target_sort,
                  builder->declarations[
                      builder->path_indices[builder->path_length - 1u]]
                      .typed_target_sort,
                  earlier, typed)
            : NULL;
        Atom *earlier_raw = prime_native_hyp_primitive_term(
            builder->owner, builder->sorts, builder->primitives, left);
        Atom *chain_items[8] = {
            atom_symbol(builder->owner, "hyp:chain"),
            builder->sorts, builder->primitives,
            left->source_sort, left->target_sort, builder->target,
            earlier_raw, raw,
        };
        Atom *chained_raw = earlier_raw
            ? atom_expr(
                  builder->owner, chain_items,
                  sizeof(chain_items) / sizeof(chain_items[0]))
            : NULL;
        if (!chained || !chained_raw) return PRIME_NATIVE_PLAN_DECLINED;
        typed = chained;
        raw = chained_raw;
    }
    builder->typed_candidates[builder->candidate_count] = typed;
    builder->raw_candidates[builder->candidate_count] = raw;
    builder->candidate_count++;
    return PRIME_NATIVE_PLAN_BUILT;
}

static PrimeNativePlanBuildV1 prime_native_hyp_enumerate_paths(
    PrimeNativeHypPathBuilderV1 *builder, Atom *current_source,
    size_t remaining, size_t depth) {
    if (!builder || !current_source || remaining == 0u ||
        depth >= builder->path_length) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    for (size_t index = 0u; index < builder->declaration_count; index++) {
        PrimeNativeHypPrimitiveDeclarationV1 *declaration =
            &builder->declarations[index];
        if (!atom_alpha_eq(declaration->source_sort, current_source))
            continue;
        if (remaining == 1u &&
            !atom_alpha_eq(declaration->target_sort, builder->target)) {
            continue;
        }
        builder->path_indices[depth] = index;
        PrimeNativePlanBuildV1 built = remaining == 1u
            ? prime_native_hyp_materialize_path(builder)
            : prime_native_hyp_enumerate_paths(
                  builder, declaration->target_sort,
                  remaining - 1u, depth + 1u);
        if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    }
    return PRIME_NATIVE_PLAN_BUILT;
}

/* The result judgment is exactly the already-constructed typed List.  This
 * receipt adds the admitted operation occurrence and the ordered candidate
 * occurrences, but copies the List term, type, indexed view, and current
 * authority token; it therefore cannot manufacture a typing judgment. */
static CettaPrimeTypedValueV1 *prime_native_hyp_candidate_receipt(
    Arena *owner, Space *space, const CettaPrimeTypedValueV1 *list,
    CettaPrimeTypedValueV1 *const *candidates,
    Atom *const *raw_candidates, size_t candidate_count,
    const char *rule_name) {
    if (!owner || !space || !space->native.universe || !list ||
        !rule_name ||
        (candidate_count != 0u && (!candidates || !raw_candidates)) ||
        candidate_count == SIZE_MAX ||
        candidate_count > SIZE_MAX / sizeof(*candidates) ||
        candidate_count > SIZE_MAX / sizeof(*raw_candidates) ||
        candidate_count > SIZE_MAX / sizeof(AtomId) ||
        candidate_count + 1u >
            SIZE_MAX / sizeof(const CettaPrimeTypedValueV1 *) ||
        list->family_head_id == CETTA_ATOM_ID_NONE) {
        return NULL;
    }
    size_t premise_count = candidate_count + 1u;
    const CettaPrimeTypedValueV1 **premises = arena_alloc(
        owner, premise_count * sizeof(*premises));
    if (!premises) return NULL;
    premises[0] = list;
    for (size_t index = 0u; index < candidate_count; index++)
        premises[index + 1u] = candidates[index];
    if (!cetta_prime_typed_values_cohere_private_v1(
            space, premises, premise_count)) {
        return NULL;
    }

    AtomId *witness_ids = candidate_count == 0u ? NULL :
        arena_alloc(owner, candidate_count * sizeof(*witness_ids));
    if (candidate_count != 0u && !witness_ids) return NULL;
    for (size_t index = 0u; index < candidate_count; index++) {
        witness_ids[index] = term_universe_store_atom_id(
            space->native.universe, owner, raw_candidates[index]);
        if (witness_ids[index] == CETTA_ATOM_ID_NONE) return NULL;
    }
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = rule_name,
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = premise_count,
        .witness_ids = witness_ids,
        .witness_count = candidate_count,
        .family_head_id = list->family_head_id,
        .parameter_ids = list->parameter_ids,
        .parameter_count = list->parameter_count,
        .index_ids = list->index_ids,
        .index_count = list->index_count,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, space->native.universe, list->context_id,
        list->term_id, list->type_id, &list->authority_token, &build);
}

static CettaPrimeNativeExecutionV1 prime_native_try_hyp_candidates(
    Arena *owner, Space *space, Atom *application) {
    PrimeNativeHypCandidateRequestV1 request = {0};
    if (!owner || !space || !space->native.universe ||
        !prime_native_hyp_candidate_application(application, &request)) {
        return prime_native_declined();
    }
    bool presentation_admitted =
        request.kind == PRIME_NATIVE_HYP_CANDIDATE_CHAIN_V1
            ? prime_native_hyp_candidate_presentation_admitted(owner, space)
            : prime_native_hyp_path_candidate_presentation_admitted(
                  owner, space);
    if (!prime_native_current_bias(space, request.bias) ||
        atom_has_vars(request.sorts) || atom_has_vars(request.primitives) ||
        atom_has_vars(request.source) || atom_has_vars(request.target) ||
        request.path_length == 0u || !presentation_admitted) {
        return prime_native_declined();
    }

    SpaceReadToken read = space_read_token(space);
    PrimeNativeHypPrimitiveDeclarationV1 *declarations = NULL;
    size_t declaration_count = 0u;
    PrimeNativePlanBuildV1 collected =
        prime_native_hyp_collect_declarations(
            owner, space, request.primitives,
            &declarations, &declaration_count);
    if (collected == PRIME_NATIVE_PLAN_DECLINED)
        return prime_native_declined();
    if (collected == PRIME_NATIVE_PLAN_FAULT)
        return prime_native_fault();

    size_t candidate_count = 0u;
    if (!prime_native_hyp_count_paths(
            declarations, declaration_count,
            request.source, request.target,
            request.path_length, &candidate_count)) {
        return prime_native_declined();
    }
    if (!space_read_token_matches_live_space(read, space))
        return prime_native_declined();

    if (candidate_count > SIZE_MAX / sizeof(CettaPrimeTypedValueV1 *) ||
        candidate_count > SIZE_MAX / sizeof(Atom *) ||
        request.path_length > SIZE_MAX / sizeof(size_t)) {
        return prime_native_declined();
    }

    Atom *element_items[5] = {
        atom_symbol(owner, "hyp"), request.sorts, request.primitives,
        request.source, request.target,
    };
    Atom *element_term = atom_expr(
        owner, element_items, sizeof(element_items) / sizeof(element_items[0]));
    CettaPrimeTypedValueV1 *element_type = NULL;
    CettaPrimeTypedValueV1 *nil_rule = NULL;
    if (!element_term) return prime_native_fault();
    PrimeNativePlanBuildV1 imported = prime_native_import_one(
        owner, space, element_term, &element_type);
    if (imported == PRIME_NATIVE_PLAN_DECLINED)
        return prime_native_declined();
    if (imported == PRIME_NATIVE_PLAN_FAULT)
        return prime_native_fault();
    imported = prime_native_import_one(
        owner, space, atom_symbol(owner, "list:nil"), &nil_rule);
    if (imported == PRIME_NATIVE_PLAN_DECLINED)
        return prime_native_declined();
    if (imported == PRIME_NATIVE_PLAN_FAULT)
        return prime_native_fault();

    CettaPrimeTypedValueV1 *typed_list =
        cetta_prime_typed_list_nil_v1(
            owner, space, nil_rule, element_type);
    if (!typed_list) return prime_native_declined();

    CettaPrimeTypedValueV1 **typed_candidates = candidate_count == 0u
        ? NULL
        : arena_alloc(
              owner, candidate_count * sizeof(*typed_candidates));
    Atom **raw_candidates = candidate_count == 0u ? NULL :
        arena_alloc(owner, candidate_count * sizeof(*raw_candidates));
    size_t *path_indices = candidate_count == 0u
        ? NULL
        : arena_alloc(owner, request.path_length * sizeof(*path_indices));
    if (candidate_count != 0u &&
        (!typed_candidates || !raw_candidates || !path_indices)) {
        return prime_native_fault();
    }

    CettaPrimeTypedValueV1 *primitive_rule = NULL;
    CettaPrimeTypedValueV1 *chain_rule = NULL;
    CettaPrimeTypedValueV1 *cons_rule = NULL;
    if (candidate_count != 0u) {
        Atom *primitive_rule_term = atom_expr3(
            owner, atom_symbol(owner, "hyp:primitive"),
            request.sorts, request.primitives);
        Atom *chain_rule_term = atom_expr3(
            owner, atom_symbol(owner, "hyp:chain"),
            request.sorts, request.primitives);
        if (!primitive_rule_term || !chain_rule_term)
            return prime_native_fault();
        imported = prime_native_import_one(
            owner, space, primitive_rule_term, &primitive_rule);
        if (imported == PRIME_NATIVE_PLAN_DECLINED)
            return prime_native_declined();
        if (imported == PRIME_NATIVE_PLAN_FAULT)
            return prime_native_fault();
        if (request.path_length > 1u) {
            imported = prime_native_import_one(
                owner, space, chain_rule_term, &chain_rule);
            if (imported == PRIME_NATIVE_PLAN_DECLINED)
                return prime_native_declined();
            if (imported == PRIME_NATIVE_PLAN_FAULT)
                return prime_native_fault();
        }
        imported = prime_native_import_one(
            owner, space, atom_symbol(owner, "list:cons"), &cons_rule);
        if (imported == PRIME_NATIVE_PLAN_DECLINED)
            return prime_native_declined();
        if (imported == PRIME_NATIVE_PLAN_FAULT)
            return prime_native_fault();
        for (size_t index = 0u; index < declaration_count; index++) {
            CettaPrimeTypedValueV1 **parts[] = {
                &declarations[index].typed_source_sort,
                &declarations[index].typed_target_sort,
                &declarations[index].typed_symbol,
            };
            Atom *terms[] = {
                declarations[index].source_sort,
                declarations[index].target_sort,
                declarations[index].symbol,
            };
            for (size_t part = 0u;
                 part < sizeof(parts) / sizeof(parts[0]); part++) {
                imported = prime_native_import_one(
                    owner, space, terms[part], parts[part]);
                if (imported == PRIME_NATIVE_PLAN_DECLINED)
                    return prime_native_declined();
                if (imported == PRIME_NATIVE_PLAN_FAULT)
                    return prime_native_fault();
            }
        }
    }

    PrimeNativeHypPathBuilderV1 builder = {
        .owner = owner,
        .space = space,
        .sorts = request.sorts,
        .primitives = request.primitives,
        .target = request.target,
        .path_length = request.path_length,
        .declarations = declarations,
        .declaration_count = declaration_count,
        .path_indices = path_indices,
        .primitive_rule = primitive_rule,
        .chain_rule = chain_rule,
        .typed_candidates = typed_candidates,
        .raw_candidates = raw_candidates,
        .candidate_capacity = candidate_count,
    };
    if (candidate_count != 0u) {
        PrimeNativePlanBuildV1 built = prime_native_hyp_enumerate_paths(
            &builder, request.source, request.path_length, 0u);
        if (built == PRIME_NATIVE_PLAN_DECLINED)
            return prime_native_declined();
        if (built == PRIME_NATIVE_PLAN_FAULT)
            return prime_native_fault();
    }
    if (builder.candidate_count != candidate_count ||
        !space_read_token_matches_live_space(read, space)) {
        return prime_native_declined();
    }

    for (size_t offset = candidate_count; offset > 0u; offset--) {
        typed_list = cetta_prime_typed_list_cons_v1(
            owner, space, cons_rule, element_type,
            typed_candidates[offset - 1u], typed_list);
        if (!typed_list) return prime_native_declined();
    }
    CettaPrimeTypedValueV1 *receipt =
        prime_native_hyp_candidate_receipt(
            owner, space, typed_list, typed_candidates,
            raw_candidates, candidate_count, request.rule_name);
    if (!receipt) return prime_native_declined();

    Atom **quoted_candidates = candidate_count == 0u ? NULL :
        arena_alloc(owner, candidate_count * sizeof(*quoted_candidates));
    for (size_t index = 0u; index < candidate_count; index++) {
        quoted_candidates[index] = atom_expr2(
            owner, atom_symbol(owner, "quote"), raw_candidates[index]);
        if (!quoted_candidates[index]) return prime_native_fault();
    }
    Atom *bag = atom_expr(
        owner, quoted_candidates, (CettaExprLen)candidate_count);
    Atom *plan = bag
        ? atom_expr2(owner, atom_symbol(owner, "superpose"), bag)
        : NULL;
    if (!plan) return prime_native_fault();
    return (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_REALIZED,
        .value = plan,
        .typed_value = receipt,
    };
}

static PrimeNativePlanBuildV1 prime_native_hyp_carrier(
    Arena *owner, Space *space, Atom *sort_code,
    CettaPrimeTypedValueV1 **carrier_out) {
    Atom *call = owner && sort_code
        ? atom_expr2(
              owner, atom_symbol(owner, "hyp:carrier"), sort_code)
        : NULL;
    return call
        ? prime_native_exact_authored_value(
              owner, space, call, carrier_out)
        : PRIME_NATIVE_PLAN_FAULT;
}

static PrimeNativePlanBuildV1 prime_native_hyp_primitive_meaning(
    Arena *owner, Space *space, Atom *source_sort, Atom *target_sort,
    Atom *symbol, CettaPrimeTypedValueV1 **relation_out) {
    Atom *items[4] = {
        atom_symbol(owner, "hyp:meaning"),
        source_sort, target_sort, symbol,
    };
    Atom *call = owner && source_sort && target_sort && symbol
        ? atom_expr(owner, items, sizeof(items) / sizeof(items[0]))
        : NULL;
    return call
        ? prime_native_exact_authored_value(
              owner, space, call, relation_out)
        : PRIME_NATIVE_PLAN_FAULT;
}

static bool prime_native_hyp_relation_has_carriers(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *relation,
    const CettaPrimeTypedValueV1 *source_carrier,
    const CettaPrimeTypedValueV1 *target_carrier) {
    CettaPrimeTypedRelationViewV1 view;
    CettaPrimeTypedValueMetadataV1 source_metadata;
    CettaPrimeTypedValueMetadataV1 target_metadata;
    return cetta_prime_typed_relation_v1_view(
               owner, space, relation, &view) &&
           cetta_prime_typed_value_v1_metadata(
               source_carrier, &source_metadata) &&
           cetta_prime_typed_value_v1_metadata(
               target_carrier, &target_metadata) &&
           view.source_type_id == source_metadata.term_id &&
           view.target_type_id == target_metadata.term_id;
}

/* Interpret a checked structural hypothesis through the exact authored
 * semantic vocabulary.  Sort codes and their carrier types remain separate:
 * `hyp:carrier` supplies the latter, while `hyp:meaning` supplies an ordinary
 * typed relation for each primitive symbol. */
static PrimeNativePlanBuildV1 prime_native_hyp_denotation(
    Arena *owner, Space *space, Atom *program,
    PrimeNativeHypDenotationV1 *denotation_out) {
    if (denotation_out)
        *denotation_out = (PrimeNativeHypDenotationV1){0};
    if (!owner || !space || !program || !denotation_out)
        return PRIME_NATIVE_PLAN_FAULT;

    Atom *primitive_arguments[5] = {0};
    if (prime_native_application_spine(
            program, "hyp:primitive", primitive_arguments, 5u)) {
        CettaPrimeTypedValueV1 *source_carrier = NULL;
        CettaPrimeTypedValueV1 *target_carrier = NULL;
        CettaPrimeTypedValueV1 *relation = NULL;
        PrimeNativePlanBuildV1 resolved = prime_native_hyp_carrier(
            owner, space, primitive_arguments[2], &source_carrier);
        if (resolved != PRIME_NATIVE_PLAN_BUILT) return resolved;
        resolved = prime_native_hyp_carrier(
            owner, space, primitive_arguments[3], &target_carrier);
        if (resolved != PRIME_NATIVE_PLAN_BUILT) return resolved;
        resolved = prime_native_hyp_primitive_meaning(
            owner, space, primitive_arguments[2], primitive_arguments[3],
            primitive_arguments[4], &relation);
        if (resolved != PRIME_NATIVE_PLAN_BUILT) return resolved;
        if (!prime_native_hyp_relation_has_carriers(
                owner, space, relation,
                source_carrier, target_carrier)) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        *denotation_out = (PrimeNativeHypDenotationV1){
            .source_carrier = source_carrier,
            .target_carrier = target_carrier,
            .relation = relation,
        };
        return PRIME_NATIVE_PLAN_BUILT;
    }

    Atom *chain_arguments[7] = {0};
    if (!prime_native_application_spine(
            program, "hyp:chain", chain_arguments, 7u)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    PrimeNativeHypDenotationV1 earlier = {0};
    PrimeNativeHypDenotationV1 later = {0};
    PrimeNativePlanBuildV1 built = prime_native_hyp_denotation(
        owner, space, chain_arguments[5], &earlier);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    built = prime_native_hyp_denotation(
        owner, space, chain_arguments[6], &later);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;

    CettaPrimeTypedValueMetadataV1 earlier_middle;
    CettaPrimeTypedValueMetadataV1 later_middle;
    if (!cetta_prime_typed_value_v1_metadata(
            earlier.target_carrier, &earlier_middle) ||
        !cetta_prime_typed_value_v1_metadata(
            later.source_carrier, &later_middle) ||
        earlier_middle.term_id != later_middle.term_id) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    CettaPrimeTypedValueV1 *result_type =
        cetta_prime_typed_relation_chain_result_type_v1(
            owner, space,
            earlier.source_carrier, earlier.target_carrier,
            later.target_carrier, earlier.relation, later.relation);
    CettaPrimeTypedValueV1 *relation = result_type
        ? cetta_prime_typed_relation_chain_v1(
              owner, space, result_type, earlier.target_carrier,
              earlier.relation, later.relation)
        : NULL;
    if (!relation) return PRIME_NATIVE_PLAN_DECLINED;
    *denotation_out = (PrimeNativeHypDenotationV1){
        .source_carrier = earlier.source_carrier,
        .target_carrier = later.target_carrier,
        .relation = relation,
    };
    return PRIME_NATIVE_PLAN_BUILT;
}

/* Admit a finite authored fact relation only when its complete live
 * `rel:apply relation source` profile consists of closed facts returning one
 * closed `(rel:edge target evidence)` occurrence apiece.  Logical
 * application `relation source target` remains the evidence fibre; the
 * operational answer stream is a separate authored relation.  An open rule
 * or richer body keeps the ordinary relational evaluator in charge, so a
 * native scan never silently under-approximates it. */
static PrimeNativePlanBuildV1 prime_native_relation_symbol_provider(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *source_type,
    const CettaPrimeTypedValueV1 *target_type,
    const CettaPrimeTypedValueV1 *relation,
    PrimeNativeRelationProviderV1 *provider_out) {
    if (provider_out) *provider_out = (PrimeNativeRelationProviderV1){0};
    const CettaPrimeTypedValueV1 *header[] = {
        source_type, target_type, relation,
    };
    if (!owner || !space || !space->native.universe || !provider_out ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, header, sizeof(header) / sizeof(header[0]))) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }

    CettaPrimeTypedRelationViewV1 relation_view = {0};
    if (!cetta_prime_typed_relation_v1_view(
            owner, space, relation, &relation_view) ||
        relation_view.source_type_id != source_type->term_id ||
        relation_view.target_type_id != target_type->term_id) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    Atom *relation_term = NULL;
    if (!cetta_prime_typed_value_v1_erase(
            relation, space->native.universe,
            owner, &relation_term, NULL)) {
        return PRIME_NATIVE_PLAN_FAULT;
    }
    if (!relation_term || relation_term->kind != ATOM_SYMBOL ||
        symbol_id_is_builtin(relation_term->sym_id) ||
        is_grounded_op(relation_term->sym_id)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    Atom *probe_argument = prime_native_fresh_variable(
        owner, "relation-input");
    Atom *relation_probe = probe_argument
        ? atom_expr3(
              owner, atom_symbol(owner, "rel:apply"),
              relation_term, probe_argument)
        : NULL;
    if (!relation_probe) return PRIME_NATIVE_PLAN_FAULT;

    SpaceReadToken read = space_read_token(space);
    size_t occurrence_count = 0u;
    SpaceEquationCursor cursor;
    SymbolId rel_apply = symbol_intern_cstr(g_symbols, "rel:apply");
    if (rel_apply == SYMBOL_ID_NONE) return PRIME_NATIVE_PLAN_FAULT;
    bool has_equations = space_equation_cursor_init(
        space, rel_apply, &cursor);
    if (has_equations) {
        for (;;) {
            SpaceEquationOccurrenceId occurrence_id;
            SpaceEquationCursorStep step = space_equation_cursor_next(
                &cursor, &occurrence_id);
            if (step == SPACE_EQUATION_CURSOR_END) break;
            if (step == SPACE_EQUATION_CURSOR_INVALIDATED)
                return PRIME_NATIVE_PLAN_DECLINED;
            SpaceEquationOccurrence occurrence;
            if (!space_equation_occurrence_resolve(
                    occurrence_id, &occurrence) ||
                !occurrence.lhs || !occurrence.rhs) {
                return PRIME_NATIVE_PLAN_DECLINED;
            }
            bool exact_fact = occurrence.lhs->kind == ATOM_EXPR &&
                occurrence.lhs->expr.len == 3u &&
                atom_is_symbol(
                    occurrence.lhs->expr.elems[0], "rel:apply") &&
                atom_eq(occurrence.lhs->expr.elems[1], relation_term);
            if (!exact_fact) {
                if (prime_native_patterns_overlap(
                        occurrence.lhs, relation_probe)) {
                    return PRIME_NATIVE_PLAN_DECLINED;
                }
                continue;
            }
            if (occurrence.rhs->kind != ATOM_EXPR ||
                occurrence.rhs->expr.len != 3u ||
                !atom_is_symbol(
                    occurrence.rhs->expr.elems[0], "rel:edge") ||
                atom_has_vars(occurrence.lhs) ||
                atom_has_vars(occurrence.rhs) ||
                occurrence_count == SIZE_MAX) {
                return PRIME_NATIVE_PLAN_DECLINED;
            }
            occurrence_count++;
        }
    }
    /* Absence of authored occurrences is not a closed-world declaration that
     * the relation is empty.  Until an explicit completeness capability says
     * otherwise, the relational evaluator retains authority over that case. */
    if (occurrence_count == 0u ||
        !space_read_token_matches_live_space(read, space) ||
        occurrence_count >
            SIZE_MAX /
                sizeof(CettaPrimeTypedFiniteRelationOccurrenceInputV1) ||
        occurrence_count > SIZE_MAX / sizeof(AtomId)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }

    CettaPrimeTypedFiniteRelationOccurrenceInputV1 *occurrences =
        occurrence_count == 0u
            ? NULL
            : arena_alloc(owner, occurrence_count * sizeof(*occurrences));
    AtomId *raw_evidence_ids = occurrence_count == 0u
        ? NULL
        : arena_alloc(owner, occurrence_count * sizeof(*raw_evidence_ids));
    if (occurrence_count != 0u && (!occurrences || !raw_evidence_ids))
        return PRIME_NATIVE_PLAN_FAULT;
    size_t written = 0u;
    if (has_equations &&
        !space_equation_cursor_init(
            space, rel_apply, &cursor)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    while (has_equations) {
        SpaceEquationOccurrenceId occurrence_id;
        SpaceEquationCursorStep step = space_equation_cursor_next(
            &cursor, &occurrence_id);
        if (step == SPACE_EQUATION_CURSOR_END) break;
        if (step == SPACE_EQUATION_CURSOR_INVALIDATED) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(
                occurrence_id, &occurrence)) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        bool exact_fact = occurrence.lhs &&
            occurrence.lhs->kind == ATOM_EXPR &&
            occurrence.lhs->expr.len == 3u &&
            atom_is_symbol(
                occurrence.lhs->expr.elems[0], "rel:apply") &&
            atom_eq(occurrence.lhs->expr.elems[1], relation_term);
        if (!exact_fact) continue;
        if (written >= occurrence_count)
            return PRIME_NATIVE_PLAN_DECLINED;
        Atom *source_term = occurrence.lhs->expr.elems[2];
        Atom *target_term = occurrence.rhs->expr.elems[1];
        Atom *evidence_term = occurrence.rhs->expr.elems[2];
        CettaPrimeTypedValueV1 *source = NULL;
        CettaPrimeTypedValueV1 *target = NULL;
        CettaPrimeTypedValueV1 *evidence = NULL;
        PrimeNativePlanBuildV1 imported = prime_native_import_one(
            owner, space, source_term, &source);
        if (imported != PRIME_NATIVE_PLAN_BUILT)
            return imported;
        imported = prime_native_import_one(
            owner, space, target_term, &target);
        if (imported != PRIME_NATIVE_PLAN_BUILT)
            return imported;
        if (source->type_id != source_type->term_id ||
            target->type_id != target_type->term_id) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        CettaPrimeTypedValueV1 *relation_at_source =
            cetta_prime_typed_value_apply_v1(
                owner, space, relation, source);
        CettaPrimeTypedValueV1 *evidence_type = relation_at_source
            ? cetta_prime_typed_value_apply_v1(
                  owner, space, relation_at_source, target)
            : NULL;
        if (!evidence_type)
            return PRIME_NATIVE_PLAN_DECLINED;
        imported = prime_native_import_checked_one(
            owner, space, evidence_term, evidence_type, &evidence);
        if (imported != PRIME_NATIVE_PLAN_BUILT)
            return imported;

        AtomId evidence_id = term_universe_store_atom_id(
            space->native.universe, owner, evidence_term);
        if (evidence_id == CETTA_ATOM_ID_NONE)
            return PRIME_NATIVE_PLAN_FAULT;
        occurrences[written] =
            (CettaPrimeTypedFiniteRelationOccurrenceInputV1){
            .source = source,
            .target = target,
            .evidence = evidence,
        };
        raw_evidence_ids[written] = evidence_id;
        written++;
    }
    if (written != occurrence_count ||
        !space_read_token_matches_live_space(read, space)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    CettaPrimeTypedFiniteRelationV1 *relation_provider = NULL;
    CettaPrimeTypedFiniteRelationBuildV1 created =
        cetta_prime_typed_finite_relation_create_v1(
            owner, space, source_type, target_type, relation,
            occurrences, occurrence_count, &relation_provider);
    if (created == CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1)
        return PRIME_NATIVE_PLAN_DECLINED;
    if (created != CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 ||
        !relation_provider) {
        return PRIME_NATIVE_PLAN_FAULT;
    }
    *provider_out = (PrimeNativeRelationProviderV1){
        .relation = relation_provider,
        .raw_evidence_ids = raw_evidence_ids,
        .raw_evidence_count = occurrence_count,
    };
    return PRIME_NATIVE_PLAN_BUILT;
}

static PrimeNativePlanBuildV1 prime_native_hyp_primitive_provider(
    Arena *owner, Space *space, Atom *program,
    PrimeNativeRelationProviderV1 *provider_out) {
    if (provider_out) *provider_out = (PrimeNativeRelationProviderV1){0};
    Atom *arguments[5] = {0};
    if (!owner || !space || !space->native.universe || !program ||
        !provider_out ||
        !prime_native_application_spine(
            program, "hyp:primitive", arguments, 5u)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    PrimeNativeHypDenotationV1 denotation = {0};
    PrimeNativePlanBuildV1 denoted = prime_native_hyp_denotation(
        owner, space, program, &denotation);
    if (denoted != PRIME_NATIVE_PLAN_BUILT) return denoted;

    PrimeNativeRelationProviderV1 relation_provider = {0};
    PrimeNativePlanBuildV1 built = prime_native_relation_symbol_provider(
        owner, space, denotation.source_carrier,
        denotation.target_carrier, denotation.relation,
        &relation_provider);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    if (relation_provider.raw_evidence_count > SIZE_MAX / sizeof(AtomId))
        return PRIME_NATIVE_PLAN_DECLINED;
    AtomId *proof_ids = relation_provider.raw_evidence_count == 0u
        ? NULL
        : arena_alloc(
              owner,
              relation_provider.raw_evidence_count * sizeof(*proof_ids));
    if (relation_provider.raw_evidence_count != 0u && !proof_ids)
        return PRIME_NATIVE_PLAN_FAULT;
    for (size_t index = 0u;
         index < relation_provider.raw_evidence_count; index++) {
        Atom *evidence = term_universe_copy_atom(
            space->native.universe, owner,
            relation_provider.raw_evidence_ids[index]);
        Atom *proof = evidence
            ? atom_expr3(
                  owner, atom_symbol(owner, "hyp:primitive-proof"),
                  arguments[4], evidence)
            : NULL;
        proof_ids[index] = proof
            ? term_universe_store_atom_id(
                  space->native.universe, owner, proof)
            : CETTA_ATOM_ID_NONE;
        if (proof_ids[index] == CETTA_ATOM_ID_NONE)
            return PRIME_NATIVE_PLAN_FAULT;
    }
    *provider_out = (PrimeNativeRelationProviderV1){
        .relation = relation_provider.relation,
        .raw_evidence_ids = proof_ids,
        .raw_evidence_count = relation_provider.raw_evidence_count,
    };
    return PRIME_NATIVE_PLAN_BUILT;
}

static PrimeNativePlanBuildV1 prime_native_hyp_finite_provider(
    Arena *owner, Space *space, Atom *program,
    PrimeNativeRelationProviderV1 *provider_out) {
    if (provider_out) *provider_out = (PrimeNativeRelationProviderV1){0};
    if (!owner || !space || !program || !provider_out)
        return PRIME_NATIVE_PLAN_FAULT;
    Atom *primitive_arguments[5] = {0};
    if (prime_native_application_spine(
            program, "hyp:primitive", primitive_arguments, 5u)) {
        return prime_native_hyp_primitive_provider(
            owner, space, program, provider_out);
    }

    Atom *chain_arguments[7] = {0};
    if (!prime_native_application_spine(
            program, "hyp:chain", chain_arguments, 7u)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    PrimeNativeRelationProviderV1 earlier = {0};
    PrimeNativeRelationProviderV1 later = {0};
    PrimeNativePlanBuildV1 built = prime_native_hyp_finite_provider(
        owner, space, chain_arguments[5], &earlier);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    built = prime_native_hyp_finite_provider(
        owner, space, chain_arguments[6], &later);
    if (built != PRIME_NATIVE_PLAN_BUILT) return built;
    CettaPrimeTypedFiniteRelationV1 *relation_provider = NULL;
    CettaPrimeTypedFiniteRelationChainOriginV1 *origins = NULL;
    CettaPrimeTypedFiniteRelationBuildV1 chained =
        cetta_prime_typed_finite_relation_chain_v1(
            owner, space, earlier.relation, later.relation,
            &relation_provider, &origins);
    if (chained == CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1)
        return PRIME_NATIVE_PLAN_DECLINED;
    if (chained != CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 ||
        !relation_provider) {
        return PRIME_NATIVE_PLAN_FAULT;
    }
    size_t occurrence_count =
        cetta_prime_typed_finite_relation_occurrence_count_v1(
            relation_provider);
    if (occurrence_count > SIZE_MAX / sizeof(AtomId) ||
        (occurrence_count != 0u && !origins)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    AtomId *raw_evidence_ids = occurrence_count == 0u
        ? NULL
        : arena_alloc(owner, occurrence_count * sizeof(*raw_evidence_ids));
    if (occurrence_count != 0u && !raw_evidence_ids)
        return PRIME_NATIVE_PLAN_FAULT;
    for (size_t index = 0u; index < occurrence_count; index++) {
        const CettaPrimeTypedFiniteRelationChainOriginV1 origin =
            origins[index];
        if (origin.earlier_index >= earlier.raw_evidence_count ||
            origin.later_index >= later.raw_evidence_count) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        CettaPrimeTypedFiniteRelationOccurrenceViewV1 left = {0};
        if (!cetta_prime_typed_finite_relation_occurrence_v1(
                earlier.relation, origin.earlier_index, &left)) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        Atom *left_proof = term_universe_copy_atom(
            space->native.universe, owner,
            earlier.raw_evidence_ids[origin.earlier_index]);
        Atom *right_proof = term_universe_copy_atom(
            space->native.universe, owner,
            later.raw_evidence_ids[origin.later_index]);
        Atom *middle = term_universe_copy_atom(
            space->native.universe, owner, left.target->term_id);
        Atom *proof_items[4] = {
            atom_symbol(owner, "hyp:chain-proof"),
            middle, left_proof, right_proof,
        };
        Atom *proof = middle && left_proof && right_proof
            ? atom_expr(owner, proof_items, 4u)
            : NULL;
        raw_evidence_ids[index] = proof
            ? term_universe_store_atom_id(
                  space->native.universe, owner, proof)
            : CETTA_ATOM_ID_NONE;
        if (raw_evidence_ids[index] == CETTA_ATOM_ID_NONE)
            return PRIME_NATIVE_PLAN_FAULT;
    }
    *provider_out = (PrimeNativeRelationProviderV1){
        .relation = relation_provider,
        .raw_evidence_ids = raw_evidence_ids,
        .raw_evidence_count = occurrence_count,
    };
    return PRIME_NATIVE_PLAN_BUILT;
}

/* Resolve one ordinary relation value to an exact finite evidence provider.
 * A symbol is read through its complete closed fact profile.  A structural
 * hypothesis relation is interpreted through the same typed `hyp` family
 * and finite-provider construction used by `hyp:run`; it is not installed as
 * a new named equation and does not acquire a parser-side authority mode. */
static PrimeNativePlanBuildV1 prime_native_relation_finite_provider(
    Arena *owner, Space *space, Atom *relation_term,
    PrimeNativeRelationProviderV1 *provider_out) {
    if (provider_out) *provider_out = (PrimeNativeRelationProviderV1){0};
    if (!owner || !space || !space->native.universe || !relation_term ||
        !provider_out || atom_has_vars(relation_term)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }

    Atom *hyp_program = NULL;
    if (prime_native_hyp_relation_descriptor(
            relation_term, &hyp_program)) {
        if (!prime_native_hyp_presentation_admitted(owner, space))
            return PRIME_NATIVE_PLAN_DECLINED;
        CettaPrimeTypedValueV1 *typed_program = NULL;
        PrimeNativePlanBuildV1 typed = prime_native_hyp_typed_program(
            owner, space, hyp_program, &typed_program);
        if (typed != PRIME_NATIVE_PLAN_BUILT) return typed;
        Atom *canonical_program = NULL;
        if (!cetta_prime_typed_value_v1_erase(
                typed_program, space->native.universe, owner,
                &canonical_program, NULL)) {
            return PRIME_NATIVE_PLAN_FAULT;
        }
        return prime_native_hyp_finite_provider(
            owner, space, canonical_program, provider_out);
    }

    if (relation_term->kind != ATOM_SYMBOL)
        return PRIME_NATIVE_PLAN_DECLINED;
    CettaPrimeTypedValueV1 *relation = NULL;
    PrimeNativePlanBuildV1 imported = prime_native_import_one(
        owner, space, relation_term, &relation);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    CettaPrimeTypedRelationViewV1 relation_view = {0};
    if (!cetta_prime_typed_relation_v1_view(
            owner, space, relation, &relation_view)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    Atom *source_type_term = term_universe_copy_atom(
        space->native.universe, owner, relation_view.source_type_id);
    Atom *target_type_term = term_universe_copy_atom(
        space->native.universe, owner, relation_view.target_type_id);
    CettaPrimeTypedValueV1 *source_type = NULL;
    CettaPrimeTypedValueV1 *target_type = NULL;
    if (!source_type_term || !target_type_term)
        return PRIME_NATIVE_PLAN_FAULT;
    imported = prime_native_import_one(
        owner, space, source_type_term, &source_type);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    imported = prime_native_import_one(
        owner, space, target_type_term, &target_type);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    return prime_native_relation_symbol_provider(
        owner, space, source_type, target_type, relation, provider_out);
}

/* Import one external flat MeTTa expression as a native Prime List by
 * checking each element once against the provider's source carrier and then
 * using only the intrinsic List constructors. */
static PrimeNativePlanBuildV1 prime_native_runtime_list_boundary(
    Arena *owner, Space *space, Atom *runtime_list,
    const CettaPrimeTypedValueV1 *element_type,
    const CettaPrimeTypedValueV1 *nil_rule,
    const CettaPrimeTypedValueV1 *cons_rule,
    CettaPrimeTypedValueV1 **list_out) {
    if (list_out) *list_out = NULL;
    if (!owner || !space || !runtime_list || !element_type || !nil_rule ||
        !cons_rule || !list_out || runtime_list->kind != ATOM_EXPR ||
        atom_has_vars(runtime_list) ||
        !cetta_expr_len_fits_size(runtime_list->expr.len) ||
        !cetta_expr_len_mul_fits_size(
            runtime_list->expr.len, sizeof(CettaPrimeTypedValueV1 *))) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    size_t length = (size_t)runtime_list->expr.len;
    CettaPrimeTypedValueV1 **elements = length == 0u
        ? NULL
        : arena_alloc(owner, length * sizeof(*elements));
    if (length != 0u && !elements)
        return PRIME_NATIVE_PLAN_FAULT;
    for (size_t index = 0u; index < length; index++) {
        PrimeNativePlanBuildV1 imported = prime_native_import_checked_one(
            owner, space, runtime_list->expr.elems[index],
            element_type, &elements[index]);
        if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    }
    CettaPrimeTypedValueV1 *list = cetta_prime_typed_list_nil_v1(
        owner, space, nil_rule, element_type);
    if (!list) return PRIME_NATIVE_PLAN_DECLINED;
    for (size_t offset = length; offset > 0u; offset--) {
        list = cetta_prime_typed_list_cons_v1(
            owner, space, cons_rule, element_type,
            elements[offset - 1u], list);
        if (!list) return PRIME_NATIVE_PLAN_DECLINED;
    }
    *list_out = list;
    return PRIME_NATIVE_PLAN_BUILT;
}

/* Retain the structural program and its independently constructed ordinary
 * relation in one typed receipt.  The carried term/type pair is copied from
 * the relation premise, so this operation cannot mint a new typing judgment. */
static CettaPrimeTypedValueV1 *prime_native_hyp_denotation_receipt(
    Arena *owner, Space *space,
    const CettaPrimeTypedValueV1 *program,
    const PrimeNativeHypDenotationV1 *denotation) {
    const CettaPrimeTypedValueV1 *premises[] = {
        program, denotation ? denotation->source_carrier : NULL,
        denotation ? denotation->target_carrier : NULL,
        denotation ? denotation->relation : NULL,
    };
    if (!owner || !space || !denotation ||
        !cetta_prime_typed_values_cohere_private_v1(
            space, premises, sizeof(premises) / sizeof(premises[0]))) {
        return NULL;
    }
    const CettaPrimeTypedValueV1 *relation = denotation->relation;
    CettaPrimeTypedValueBuildPrivateV1 build = {
        .rule_name = "hyp:denote",
        .construction = CETTA_PRIME_TYPED_VALUE_INTRINSIC_RULE_V1,
        .premises = premises,
        .premise_count = sizeof(premises) / sizeof(premises[0]),
        .family_head_id = CETTA_ATOM_ID_NONE,
    };
    return cetta_prime_typed_value_allocate_private_v1(
        owner, space->native.universe, relation->context_id,
        relation->term_id, relation->type_id,
        &relation->authority_token, &build);
}

/* Materialize the exact source fibre as one ordered occurrence bag.  The
 * provider was checked once at admission; this operation only filters its
 * complete rows and applies intrinsic Sigma/List constructors. */
static PrimeNativePlanBuildV1 prime_native_hyp_finite_search(
    Arena *owner, Space *space,
    const PrimeNativeRelationProviderV1 *provider,
    Atom *input, CettaPrimeNativeExecutionV1 *execution_out) {
    if (execution_out) *execution_out = prime_native_declined();
    if (!owner || !space || !provider || !input || !execution_out ||
        atom_has_vars(input) ||
        !prime_native_relation_provider_current(space, provider)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }
    TermUniverse *universe = space->native.universe;
    CettaPrimeTypedValueV1 *source = NULL;
    size_t occurrence_count =
        cetta_prime_typed_finite_relation_occurrence_count_v1(
            provider->relation);
    for (size_t index = 0u; index < occurrence_count; index++) {
        CettaPrimeTypedFiniteRelationOccurrenceViewV1 occurrence = {0};
        if (!cetta_prime_typed_finite_relation_occurrence_v1(
                provider->relation, index, &occurrence)) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        if (!term_universe_atom_id_eq(
                universe, occurrence.source->term_id, input)) {
            continue;
        }
        if (!source) source = (CettaPrimeTypedValueV1 *)occurrence.source;
    }
    if (!source) {
        PrimeNativePlanBuildV1 imported = prime_native_import_one(
            owner, space, input, &source);
        if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    }
    const CettaPrimeTypedValueV1 *source_type =
        cetta_prime_typed_finite_relation_source_type_v1(
            provider->relation);
    if (!source_type || source->type_id != source_type->term_id)
        return PRIME_NATIVE_PLAN_DECLINED;

    CettaPrimeTypedValueV1 *nil_rule = NULL;
    PrimeNativePlanBuildV1 imported = prime_native_import_one(
        owner, space, atom_symbol(owner, "list:nil"), &nil_rule);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    CettaPrimeTypedValueV1 *cons_rule = NULL;
    imported = prime_native_import_one(
        owner, space, atom_symbol(owner, "list:cons"), &cons_rule);
    if (imported != PRIME_NATIVE_PLAN_BUILT) return imported;
    CettaPrimeTypedFiniteRelationSearchV1 search = {0};
    CettaPrimeTypedFiniteRelationBuildV1 searched =
        cetta_prime_typed_finite_relation_search_v1(
            owner, space, provider->relation, source,
            nil_rule, cons_rule, &search);
    if (searched == CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1)
        return PRIME_NATIVE_PLAN_DECLINED;
    if (searched != CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 ||
        !search.receipt ||
        search.answer_count > SIZE_MAX / sizeof(Atom *)) {
        return PRIME_NATIVE_PLAN_FAULT;
    }
    Atom **raw_edges = search.answer_count == 0u
        ? NULL
        : arena_alloc(owner, search.answer_count * sizeof(*raw_edges));
    if (search.answer_count != 0u && !raw_edges)
        return PRIME_NATIVE_PLAN_FAULT;
    for (size_t index = 0u; index < search.answer_count; index++) {
        size_t provider_index = search.occurrence_indices[index];
        CettaPrimeTypedFiniteRelationOccurrenceViewV1 occurrence = {0};
        if (provider_index >= provider->raw_evidence_count ||
            !cetta_prime_typed_finite_relation_occurrence_v1(
                provider->relation, provider_index, &occurrence)) {
            return PRIME_NATIVE_PLAN_DECLINED;
        }
        Atom *target = term_universe_copy_atom(
            universe, owner, occurrence.target->term_id);
        Atom *proof = term_universe_copy_atom(
            universe, owner, provider->raw_evidence_ids[provider_index]);
        Atom *edge = target && proof
            ? atom_expr3(
                  owner, atom_symbol(owner, "hyp:edge"), target, proof)
            : NULL;
        if (!edge) return PRIME_NATIVE_PLAN_FAULT;
        raw_edges[index] = edge;
    }
    Atom *bag = atom_expr(
        owner, raw_edges, (CettaExprLen)search.answer_count);
    Atom *plan = bag
        ? atom_expr2(owner, atom_symbol(owner, "superpose"), bag)
        : NULL;
    if (!plan) return PRIME_NATIVE_PLAN_FAULT;
    *execution_out = (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_REALIZED,
        .value = plan,
        .typed_value = search.receipt,
    };
    return PRIME_NATIVE_PLAN_BUILT;
}

static Atom *prime_native_map_rel_raw_proof(
    Arena *owner, Space *space,
    const PrimeNativeRelationProviderV1 *provider,
    const CettaPrimeTypedListMapRelFiniteV1 *lift,
    size_t output_index) {
    if (!owner || !space || !space->native.universe || !provider || !lift ||
        output_index >= lift->search.answer_count ||
        (lift->source_length != 0u && !lift->base_occurrence_indices)) {
        return NULL;
    }
    Atom *proof_items[] = {atom_symbol(owner, "map-rel:nil-proof")};
    Atom *proof = atom_expr(
        owner, proof_items, sizeof(proof_items) / sizeof(proof_items[0]));
    for (size_t offset = lift->source_length; proof && offset > 0u;
         offset--) {
        size_t position = offset - 1u;
        size_t provider_index =
            lift->base_occurrence_indices[
                output_index * lift->source_length + position];
        CettaPrimeTypedFiniteRelationOccurrenceViewV1 occurrence = {0};
        if (provider_index >= provider->raw_evidence_count ||
            !cetta_prime_typed_finite_relation_occurrence_v1(
                provider->relation, provider_index, &occurrence)) {
            return NULL;
        }
        Atom *source_intrinsic = term_universe_copy_atom(
            space->native.universe, owner, occurrence.source->term_id);
        Atom *target_intrinsic = term_universe_copy_atom(
            space->native.universe, owner, occurrence.target->term_id);
        Atom *evidence_intrinsic = term_universe_copy_atom(
            space->native.universe, owner,
            provider->raw_evidence_ids[provider_index]);
        Atom *source = prime_native_runtime_quote(owner, source_intrinsic);
        Atom *target = prime_native_runtime_quote(owner, target_intrinsic);
        Atom *evidence = prime_native_runtime_quote(
            owner, evidence_intrinsic);
        Atom *items[5] = {
            atom_symbol(owner, "map-rel:cons-proof"),
            source, target, evidence, proof,
        };
        if (!source || !target || !evidence) return NULL;
        proof = atom_expr(owner, items, sizeof(items) / sizeof(items[0]));
    }
    return proof;
}

static CettaPrimeNativeExecutionV1 prime_native_try_map_rel_run(
    Arena *owner, Space *space, Atom *application) {
    Atom *relation_term = NULL;
    Atom *runtime_source_list = NULL;
    if (!owner || !space || !space->native.universe ||
        !prime_native_map_rel_run_application(
            application, &relation_term, &runtime_source_list)) {
        return prime_native_declined();
    }
    if (!prime_native_map_rel_presentation_admitted(owner, space)) {
        return prime_native_declined();
    }

    PrimeNativeRelationProviderV1 provider = {0};
    PrimeNativePlanBuildV1 resolved = prime_native_relation_finite_provider(
        owner, space, relation_term, &provider);
    if (resolved == PRIME_NATIVE_PLAN_DECLINED)
        return prime_native_declined();
    if (resolved == PRIME_NATIVE_PLAN_FAULT)
        return prime_native_fault();
    const CettaPrimeTypedValueV1 *source_type =
        cetta_prime_typed_finite_relation_source_type_v1(
            provider.relation);
    if (!source_type)
        return prime_native_declined();

    const char *const rule_names[] = {
        "list", "list:nil", "list:cons",
        "map-rel", "map-rel:nil", "map-rel:cons",
    };
    CettaPrimeTypedValueV1 *rules[
        sizeof(rule_names) / sizeof(rule_names[0])] = {0};
    for (size_t index = 0u;
         index < sizeof(rule_names) / sizeof(rule_names[0]); index++) {
        PrimeNativePlanBuildV1 imported = prime_native_import_one(
            owner, space, atom_symbol(owner, rule_names[index]),
            &rules[index]);
        if (imported == PRIME_NATIVE_PLAN_DECLINED)
            return prime_native_declined();
        if (imported == PRIME_NATIVE_PLAN_FAULT)
            return prime_native_fault();
    }
    CettaPrimeTypedValueV1 *source_list = NULL;
    PrimeNativePlanBuildV1 imported = prime_native_runtime_list_boundary(
        owner, space, runtime_source_list, source_type,
        rules[1], rules[2], &source_list);
    if (imported == PRIME_NATIVE_PLAN_DECLINED)
        return prime_native_declined();
    if (imported == PRIME_NATIVE_PLAN_FAULT)
        return prime_native_fault();

    CettaPrimeTypedListMapRelFiniteV1 lift = {0};
    CettaPrimeTypedFiniteRelationBuildV1 lifted =
        cetta_prime_typed_list_map_rel_finite_v1(
            owner, space, provider.relation, source_list,
            rules[0], rules[1], rules[2], rules[3], rules[4], rules[5],
            &lift);
    if (lifted == CETTA_PRIME_TYPED_FINITE_RELATION_DECLINED_V1)
        return prime_native_declined();
    if (lifted != CETTA_PRIME_TYPED_FINITE_RELATION_BUILT_V1 ||
        !lift.search.receipt ||
        lift.search.answer_count > SIZE_MAX / sizeof(Atom *) ||
        (lift.search.answer_count != 0u &&
         (!lift.target_lists || !lift.evidences))) {
        return prime_native_fault();
    }

    Atom **raw_edges = lift.search.answer_count == 0u
        ? NULL
        : arena_alloc(
              owner, lift.search.answer_count * sizeof(*raw_edges));
    if (lift.search.answer_count != 0u && !raw_edges)
        return prime_native_fault();
    for (size_t index = 0u; index < lift.search.answer_count; index++) {
        Atom *target_intrinsic = NULL;
        if (!cetta_prime_typed_list_runtime_representation_v1(
                owner, space, lift.target_lists[index],
                &target_intrinsic)) {
            return prime_native_fault();
        }
        Atom *target = prime_native_runtime_quote(owner, target_intrinsic);
        Atom *proof = prime_native_map_rel_raw_proof(
            owner, space, &provider, &lift, index);
        raw_edges[index] = target && proof
            ? atom_expr3(
                  owner, atom_symbol(owner, "map-rel:edge"),
                  target, proof)
            : NULL;
        if (!raw_edges[index]) return prime_native_fault();
    }
    Atom *bag = atom_expr(
        owner, raw_edges, (CettaExprLen)lift.search.answer_count);
    Atom *plan = bag
        ? atom_expr2(owner, atom_symbol(owner, "superpose"), bag)
        : NULL;
    if (!plan) return prime_native_fault();
    return (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_REALIZED,
        .value = plan,
        .typed_value = lift.search.receipt,
    };
}

/* Compile an exact typed `hyp` proof program to ordinary relational MeTTa.
 * Primitive meaning remains language-authored in `hyp:run-primitive`; only
 * the proof-program composition is specialized.  The generated lets retain
 * every middle value and both proof occurrences. */
static PrimeNativePlanBuildV1 prime_native_hyp_plan(
    Arena *owner, Atom *program, Atom *input, Atom **plan_out) {
    if (plan_out) *plan_out = NULL;
    if (!owner || !program || !input || !plan_out)
        return PRIME_NATIVE_PLAN_FAULT;

    Atom *primitive_arguments[5] = {0};
    if (cetta_prime_typed_application_spine_private_v1(
            program, "hyp:primitive", primitive_arguments, 5u)) {
        Atom *items[5] = {
            atom_symbol(owner, "hyp:run-primitive"),
            primitive_arguments[2], primitive_arguments[3],
            primitive_arguments[4], input,
        };
        Atom *plan = atom_expr(
            owner, items, sizeof(items) / sizeof(items[0]));
        if (!plan) return PRIME_NATIVE_PLAN_FAULT;
        *plan_out = plan;
        return PRIME_NATIVE_PLAN_BUILT;
    }

    Atom *chain_arguments[7] = {0};
    if (!cetta_prime_typed_application_spine_private_v1(
            program, "hyp:chain", chain_arguments, 7u)) {
        return PRIME_NATIVE_PLAN_DECLINED;
    }

    Atom *middle = prime_native_fresh_variable(owner, "middle");
    Atom *earlier_proof = prime_native_fresh_variable(
        owner, "earlier-proof");
    Atom *target = prime_native_fresh_variable(owner, "target");
    Atom *later_proof = prime_native_fresh_variable(
        owner, "later-proof");
    if (!middle || !earlier_proof || !target || !later_proof)
        return PRIME_NATIVE_PLAN_FAULT;

    Atom *earlier_plan = NULL;
    PrimeNativePlanBuildV1 earlier = prime_native_hyp_plan(
        owner, chain_arguments[5], input, &earlier_plan);
    if (earlier != PRIME_NATIVE_PLAN_BUILT) return earlier;

    Atom *later_plan = NULL;
    PrimeNativePlanBuildV1 later = prime_native_hyp_plan(
        owner, chain_arguments[6], middle, &later_plan);
    if (later != PRIME_NATIVE_PLAN_BUILT) return later;

    Atom *earlier_pattern = atom_expr3(
        owner, atom_symbol(owner, "hyp:edge"), middle, earlier_proof);
    Atom *later_pattern = atom_expr3(
        owner, atom_symbol(owner, "hyp:edge"), target, later_proof);
    Atom *proof_items[4] = {
        atom_symbol(owner, "hyp:chain-proof"),
        middle, earlier_proof, later_proof,
    };
    Atom *proof = atom_expr(owner, proof_items, 4u);
    Atom *result = proof
        ? atom_expr3(owner, atom_symbol(owner, "hyp:edge"), target, proof)
        : NULL;
    Atom *later_let_items[4] = {
        atom_symbol(owner, "let"), later_pattern, later_plan, result,
    };
    Atom *later_let = result
        ? atom_expr(owner, later_let_items, 4u)
        : NULL;
    Atom *plan_items[4] = {
        atom_symbol(owner, "let"),
        earlier_pattern, earlier_plan, later_let,
    };
    Atom *plan = later_let
        ? atom_expr(owner, plan_items, 4u)
        : NULL;
    if (!earlier_pattern || !later_pattern || !proof || !result ||
        !later_let || !plan) {
        return PRIME_NATIVE_PLAN_FAULT;
    }
    *plan_out = plan;
    return PRIME_NATIVE_PLAN_BUILT;
}

static CettaPrimeNativeExecutionV1 prime_native_try_hyp_run(
    Arena *owner, Space *space, Atom *application) {
    Atom *source_program = NULL;
    Atom *input = NULL;
    if (!owner || !space || !space->native.universe ||
        !prime_native_hyp_run_application(
            application, &source_program, &input)) {
        return prime_native_declined();
    }
    if (!prime_native_hyp_presentation_admitted(owner, space)) {
        return prime_native_declined();
    }

    AtomId source_program_id = term_universe_store_atom_id(
        space->native.universe, owner, source_program);
    PrimeNativeHypAdmittedV1 admitted = {0};
    PrimeNativeRelationProviderV1 local_provider = {0};
    Atom *canonical_program = NULL;
    if (prime_native_hyp_admission_cache_find(
            owner, space, source_program_id, &admitted)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_ADMISSION_CACHE_HIT);
    } else {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_ADMISSION_CACHE_MISS);
        PrimeNativePlanBuildV1 typed = prime_native_hyp_typed_program(
            owner, space, source_program, &admitted.program);
        if (typed == PRIME_NATIVE_PLAN_DECLINED) {
            return prime_native_declined();
        }
        if (typed == PRIME_NATIVE_PLAN_FAULT || !admitted.program)
            return prime_native_fault();
        if (!cetta_prime_typed_value_v1_erase(
                admitted.program, space->native.universe, owner,
                &canonical_program, NULL)) {
            return prime_native_fault();
        }

        PrimeNativeHypDenotationV1 semantic = {0};
        PrimeNativePlanBuildV1 denoted = prime_native_hyp_denotation(
            owner, space, canonical_program, &semantic);
        if (denoted == PRIME_NATIVE_PLAN_BUILT) {
            admitted.denotation = prime_native_hyp_denotation_receipt(
                owner, space, admitted.program, &semantic);
            if (!admitted.denotation) return prime_native_fault();
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_DENOTATION_ADMITTED);
        } else if (denoted == PRIME_NATIVE_PLAN_DECLINED) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_DENOTATION_FALLBACK);
        } else {
            return prime_native_fault();
        }
        PrimeNativePlanBuildV1 provider_built =
            prime_native_hyp_finite_provider(
                owner, space, canonical_program, &local_provider);
        admitted.finite_provider_checked = true;
        if (provider_built == PRIME_NATIVE_PLAN_BUILT) {
            admitted.finite_provider = &local_provider;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_ADMITTED);
        } else if (provider_built == PRIME_NATIVE_PLAN_FAULT) {
            return prime_native_fault();
        } else {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_PROVIDER_FALLBACK);
        }
        prime_native_hyp_admission_cache_store(
            space, source_program_id,
            admitted.program, admitted.denotation,
            admitted.finite_provider_checked,
            admitted.finite_provider);
    }

    if (!canonical_program &&
        !cetta_prime_typed_value_v1_erase(
            admitted.program, space->native.universe, owner,
            &canonical_program, NULL)) {
        return prime_native_fault();
    }
    if (admitted.finite_provider) {
        CettaPrimeNativeExecutionV1 search = prime_native_declined();
        PrimeNativePlanBuildV1 searched = prime_native_hyp_finite_search(
            owner, space, admitted.finite_provider, input, &search);
        if (searched == PRIME_NATIVE_PLAN_BUILT) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_FINITE_SEARCH_REALIZED);
            return search;
        }
        if (searched == PRIME_NATIVE_PLAN_FAULT)
            return prime_native_fault();
    }
    Atom *plan = NULL;
    PrimeNativePlanBuildV1 built = prime_native_hyp_plan(
        owner, canonical_program, input, &plan);
    if (built == PRIME_NATIVE_PLAN_DECLINED)
        return prime_native_declined();
    if (built == PRIME_NATIVE_PLAN_FAULT || !plan)
        return prime_native_fault();
    return (CettaPrimeNativeExecutionV1){
        .kind = CETTA_PRIME_NATIVE_EXECUTION_REALIZED,
        .value = plan,
        .typed_value = admitted.denotation
            ? admitted.denotation
            : admitted.program,
    };
}

CettaPrimeNativeExecutionV1 cetta_prime_native_calculus_try_v1(
    Arena *owner, Space *space, Atom *application) {
    if (!owner || !space || !space->native.universe || !application)
        return prime_native_declined();
    PrimeNativeHypCandidateRequestV1 candidate_request = {0};
    if (prime_native_hyp_candidate_application(
            application, &candidate_request)) {
        return prime_native_observe(
            prime_native_try_hyp_candidates(owner, space, application),
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_CANDIDATE_BAG_REALIZED);
    }
    Atom *map_rel_relation = NULL;
    Atom *map_rel_source_list = NULL;
    if (prime_native_map_rel_run_application(
            application, &map_rel_relation, &map_rel_source_list)) {
        return prime_native_observe(
            prime_native_try_map_rel_run(owner, space, application),
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_MAP_REL_REALIZED);
    }
    if (prime_native_maybe_map_application(application))
        return prime_native_observe(
            prime_native_try_map_application(owner, space, application),
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_MAP_REALIZED);
    Atom *program = NULL;
    Atom *input = NULL;
    if (prime_native_hyp_run_application(
            application, &program, &input)) {
        return prime_native_observe(
            prime_native_try_hyp_run(owner, space, application),
            CETTA_RUNTIME_COUNTER_PRIME_NATIVE_HYP_REALIZED);
    }
    return prime_native_declined();
}
