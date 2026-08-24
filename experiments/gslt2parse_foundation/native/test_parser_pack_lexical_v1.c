#include "finite_horn_ground_term_v1.h"
#include "parser_pack_gll_v1.h"
#include "parser_pack_glr_v1.h"
#include "parser_pack_guard_relation_v1.h"
#include "parser_pack_lexical_v1.h"
#include "regular_span_dfa_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char DIGEST_A[] =
    "0000000000000000000000000000000000000000000000000000000000000001";
static const char DIGEST_B[] =
    "0000000000000000000000000000000000000000000000000000000000000002";
static const char DIGEST_C[] =
    "0000000000000000000000000000000000000000000000000000000000000003";
static const char EXPECTED_GUARD_RELATION_DIGEST[] =
    "65067222f11653223ad753376201af5640fd2746c1ebbaab2c79e4c347994828";

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

typedef struct {
    Atom *term;
    char *canonical;
} CanonicalTerm;

static bool expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
    return false;
}

#define REQUIRE(counts, condition, label) \
    do { \
        if (!expect((counts), (condition), (label))) \
            goto done; \
    } while (0)

static Atom *app(Arena *arena,
                 const char *head,
                 size_t argument_len,
                 Atom **arguments) {
    Atom **elements = malloc(sizeof(*elements) * (argument_len + 1u));
    Atom *result;
    size_t index;

    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr(arena, elements, argument_len + 1u);
    free(elements);
    return result;
}

static Atom *unary(Arena *arena, const char *head, Atom *argument) {
    Atom *arguments[1] = {argument};
    return app(arena, head, 1u, arguments);
}

static Atom *binary(Arena *arena,
                    const char *head,
                    Atom *left,
                    Atom *right) {
    Atom *arguments[2] = {left, right};
    return app(arena, head, 2u, arguments);
}

static Atom *cp(Arena *arena, uint32_t scalar) {
    return unary(arena, "cp", atom_int(arena, scalar));
}

static Atom *items(Arena *arena, Atom **values, uint32_t len) {
    Atom *list = atom_symbol(arena, "pp-items-nil");
    while (len > 0u) {
        len--;
        list = binary(arena, "pp-items-cons", values[len], list);
    }
    return list;
}

static Atom *actions(Arena *arena, Atom **values, uint32_t len) {
    Atom *list = atom_symbol(arena, "pa-nil");
    while (len > 0u) {
        len--;
        list = binary(arena, "pa-cons", values[len], list);
    }
    return list;
}

static Atom *terminal_char(Arena *arena, uint32_t scalar) {
    return unary(arena, "pp-terminal",
                 unary(arena, "pp-terminal-char", cp(arena, scalar)));
}

static Atom *nonterminal(Arena *arena, Atom *state) {
    return unary(arena, "pp-nonterminal", state);
}

static Atom *slot(Arena *arena, uint32_t index) {
    Atom *peano = atom_symbol(arena, "q-zero");
    while (index > 0u) {
        peano = unary(arena, "q-succ", peano);
        index--;
    }
    return unary(arena, "pa-slot", peano);
}

static Atom *apply(Arena *arena,
                   const char *head,
                   Atom **arguments,
                   uint32_t argument_len) {
    return binary(arena, "pa-apply", atom_symbol(arena, head),
                  actions(arena, arguments, argument_len));
}

static Atom *production(Arena *arena,
                        const char *label,
                        Atom *state,
                        Atom **rhs,
                        uint32_t rhs_len,
                        Atom *action) {
    Atom *arguments[4] = {
        atom_symbol(arena, label), state, items(arena, rhs, rhs_len), action,
    };
    return app(arena, "pp-production", 4u, arguments);
}

static Atom *evidence_answer(Arena *arena, Atom *artifact) {
    Atom *arguments[2] = {atom_symbol(arena, "owner"), artifact};
    return app(arena, "compile-pack-production", 2u, arguments);
}

static Atom *evidence_certificate(Arena *arena, uint32_t index) {
    Atom *arguments[2] = {
        atom_int(arena, index), atom_symbol(arena, "evidence"),
    };
    return app(arena, "cert", 2u, arguments);
}

static char *render(Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static int canonical_compare(const void *left, const void *right) {
    const CanonicalTerm *lhs = left;
    const CanonicalTerm *rhs = right;
    return strcmp(lhs->canonical, rhs->canonical);
}

static bool canonicalize_terms(Atom **terms, size_t len) {
    CanonicalTerm *entries = calloc(len ? len : 1u, sizeof(*entries));
    size_t index;

    if (!entries)
        return false;
    for (index = 0u; index < len; index++) {
        entries[index].term = terms[index];
        entries[index].canonical = render(terms[index]);
        if (!entries[index].canonical)
            goto fail;
    }
    if (len > 1u)
        qsort(entries, len, sizeof(*entries), canonical_compare);
    for (index = 0u; index < len; index++)
        terms[index] = entries[index].term;
    for (index = 0u; index < len; index++)
        free(entries[index].canonical);
    free(entries);
    return true;

fail:
    for (index = 0u; index < len; index++)
        free(entries[index].canonical);
    free(entries);
    return false;
}

static bool load_pack(Arena *arena,
                      Atom **productions,
                      size_t production_len,
                      PPABIV1Pack *pack,
                      char *error,
                      size_t error_size) {
    PPABIV1DerivationInput *roots;
    PPABIV1ProvenanceInput provenance;
    size_t index;
    bool ok;

    if (!canonicalize_terms(productions, production_len))
        return false;
    roots = calloc(production_len ? production_len : 1u, sizeof(*roots));
    if (!roots)
        return false;
    for (index = 0u; index < production_len; index++) {
        roots[index].kind = PPABI_V1_EVIDENCE_PRODUCTION;
        roots[index].artifact = productions[index];
        roots[index].answer = evidence_answer(arena, productions[index]);
        roots[index].certificate = evidence_certificate(arena, (uint32_t)index);
    }
    provenance = (PPABIV1ProvenanceInput){
        .source_digest = DIGEST_A,
        .compiler_digest = DIGEST_B,
        .environment_digest = DIGEST_C,
        .derivations = roots,
        .derivation_len = production_len,
    };
    ok = ppabi_v1_pack_load(
        pack, productions, production_len, NULL, 0u,
        &provenance, error, error_size);
    free(roots);
    return ok;
}

static bool result_is(const PPNativeV1Result *result,
                      const char *expected) {
    char *actual;
    bool equal;

    if (result->semantic_result_len != 1u)
        return false;
    actual = render(result->semantic_results[0]);
    equal = actual && strcmp(actual, expected) == 0;
    free(actual);
    return equal;
}

static bool forest_contains(const PPNativeV1Result *result,
                            const char *needle) {
    char *forest;
    bool found;

    if (!result->canonical_forest_materialized ||
        !result->canonical_forest) {
        return false;
    }
    forest = render(result->canonical_forest);
    found = forest && strstr(forest, needle) != NULL;
    free(forest);
    return found;
}

static bool result_has(const PPNativeV1Result *result,
                       const Atom *expected) {
    uint32_t index;

    for (index = 0u; index < result->semantic_result_len; index++) {
        if (atom_eq(result->semantic_results[index], (Atom *)expected))
            return true;
    }
    return false;
}

static void report_result_failure(const char *label,
                                  const PPNativeV1Result *result) {
    uint32_t index;

    fprintf(stderr,
            "%s: outcome=%u accepted=%u roots=%u results=%u detail=%s\n",
            label, (unsigned)result->outcome, result->accepted ? 1u : 0u,
            (unsigned)result->forest.root_len,
            (unsigned)result->semantic_result_len, result->detail);
    for (index = 0u; index < result->semantic_result_len; index++) {
        char *value = render(result->semantic_results[index]);
        fprintf(stderr, "%s-result-%u: %s\n", label, (unsigned)index,
                value ? value : "<unrenderable>");
        free(value);
    }
}

static void test_guard_relation_contract(TestCounts *counts,
                                         Arena *arena,
                                         char out_digest[65]) {
    uint32_t codepoints[2] = {97u, 955u};
    uint32_t byte_offsets[3] = {0u, 1u, 3u};
    static const uint32_t base_terminal_ids[] = {3u};
    static const uint32_t start_offsets[] = {0u, 1u, 1u, 1u};
    static const uint32_t guard_terminal_ids[] = {8u, 7u};
    static const uint32_t collision_terminal_ids[] = {3u};
    CettaLpNativeUtf8LatticeEdge base_edges[1] = {
        {
            .terminal_id = 3u,
            .scalar_left = 0u,
            .scalar_right = 1u,
            .byte_left = 0u,
            .byte_right = 1u,
            .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS,
            .value = 0u,
        },
    };
    CettaLpNativeUtf8LatticeEdge invalid_base_edge;
    CettaLpNativeUtf8Lattice base = {
        .terminal_ids = base_terminal_ids,
        .terminal_len = 1u,
        .edges = base_edges,
        .edge_len = 1u,
        .start_offsets = start_offsets,
        .start_offset_len = 4u,
        .codepoints = codepoints,
        .byte_offsets = byte_offsets,
        .scalar_len = 2u,
        .input_byte_len = 3u,
        .decoded_byte_len = 3u,
        .source_pass_count = 1u,
    };
    CettaLpNativeUtf8Lattice altered_base;
    PPGuardRelationV1Match matches[6];
    PPGuardRelationV1Match reordered[6];
    PPGuardRelationV1Match mutated[6];
    PPGuardRelationV1Match invalid;
    PPGuardRelationV1 relation;
    PPGuardRelationV1 reordered_relation;
    PPGuardRelationV1 mutated_relation;
    PPGuardRelationV1 absent_relation;
    PPGuardRelationV1 borrowed_relation;
    PPGuardRelationV1 wrong_owner_relation;
    PPGuardRelationV1 expiring_relation;
    HashConsTable active_hashcons;
    HashConsTable *saved_hashcons = g_hashcons;
    Arena wrong_owner;
    Arena expiring_owner;
    const Arena *borrowed_owners[1] = {arena};
    const Arena *wrong_owners[1] = {&wrong_owner};
    const Arena *expiring_owners[1] = {&expiring_owner};
    Atom *shared = atom_symbol(arena, "shared-value");
    Atom *base_value = atom_symbol(arena, "base-token-value");
    Atom *base_witness_values[1] = {base_value};
    Atom *long_value = atom_symbol(arena, "long-value");
    Atom *nullable = atom_symbol(arena, "nullable-value");
    Atom *eof_value = atom_symbol(arena, "eof-value");
    Atom *proof_short_a = atom_symbol(arena, "proof-short-a");
    Atom *proof_short_b = atom_symbol(arena, "proof-short-b");
    Atom *proof_long = atom_symbol(arena, "proof-long");
    Atom *proof_nullable = atom_symbol(arena, "proof-nullable");
    Atom *proof_eof = atom_symbol(arena, "proof-eof");
    Atom *proof_mutated = atom_symbol(arena, "proof-mutated");
    Atom *variable_proof = atom_var_with_id(arena, "x", 1u);
    Atom *expiring_base_value;
    Atom *expiring_value;
    Atom *expiring_proof;
    Atom *expiring_base_values[1];
    PPGuardRelationV1Match expiring_match;
    uint32_t shared_proofs = 0u;
    uint32_t distinct_extents = 0u;
    uint32_t guard_edges = 0u;
    uint32_t index;
    char error[512] = {0};

    hashcons_init(&active_hashcons);
    g_hashcons = &active_hashcons;
    ppguard_relation_v1_init(&relation);
    ppguard_relation_v1_init(&reordered_relation);
    ppguard_relation_v1_init(&mutated_relation);
    ppguard_relation_v1_init(&absent_relation);
    ppguard_relation_v1_init(&borrowed_relation);
    ppguard_relation_v1_init(&wrong_owner_relation);
    ppguard_relation_v1_init(&expiring_relation);
    g_hashcons = saved_hashcons;
    arena_init(&wrong_owner);
    arena_init(&expiring_owner);
    if (out_digest)
        out_digest[0] = '\0';

    matches[0] = (PPGuardRelationV1Match){
        8u, 2u, 2u, 3u, 3u, eof_value, proof_eof,
    };
    matches[1] = (PPGuardRelationV1Match){
        7u, 0u, 2u, 0u, 3u, long_value, proof_long,
    };
    matches[2] = (PPGuardRelationV1Match){
        7u, 0u, 1u, 0u, 1u, shared, proof_short_a,
    };
    matches[3] = (PPGuardRelationV1Match){
        8u, 1u, 1u, 1u, 1u, nullable, proof_nullable,
    };
    matches[4] = matches[2];
    matches[5] = (PPGuardRelationV1Match){
        7u, 0u, 1u, 0u, 1u, shared, proof_short_b,
    };
    for (index = 0u; index < 6u; index++) {
        reordered[index] = matches[5u - index];
        mutated[index] = matches[index];
    }
    mutated[2].proof = proof_mutated;
    mutated[4].proof = proof_mutated;

    REQUIRE(counts,
            cetta_lp_native_utf8_lattice_validate(
                &base, error, sizeof(error)),
            error[0] ? error : "validate guard source lattice");
    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                matches, 6u, DIGEST_A,
                &relation, error, sizeof(error)),
            error[0] ? error : "build canonical positive guard relation");
    if (out_digest)
        memcpy(out_digest, relation.relation_digest, 65u);
    REQUIRE(counts,
            relation.evidence_len == 5u &&
                relation.witness_len == 6u &&
                relation.base_witness_len == 1u &&
                relation.lattice.edge_len == 6u &&
                relation.lattice.terminal_len == 3u &&
                relation.terminal_ids[0] == 3u &&
                relation.terminal_ids[1] == 7u &&
                relation.terminal_ids[2] == 8u &&
                relation.witness_values[0] != base_value &&
                atom_eq(relation.witness_values[0], base_value) &&
                arena_owns_ptr(
                    &relation.arena, relation.witness_values[0]) &&
                arena_owns_ptr(
                    &relation.arena, relation.evidence[0].value) &&
                arena_owns_ptr(
                    &relation.arena, relation.evidence[0].proof) &&
                relation.lattice.source_pass_count == 1u,
            "guard relation owns roots despite ambient hash-consing and "
            "canonicalizes duplicates");
    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build_borrowed_atoms(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                matches, 6u, borrowed_owners, 1u, DIGEST_A,
                &borrowed_relation, error, sizeof(error)) &&
                borrowed_relation.atom_storage ==
                    PPGUARD_RELATION_V1_ATOMS_BORROWED &&
                borrowed_relation.borrowed_atom_owner_len == 1u &&
                borrowed_relation.borrowed_atom_owners !=
                    borrowed_owners &&
                borrowed_relation.borrowed_atom_owners[0] == arena &&
                borrowed_relation.arena.live_bytes == 0u &&
                borrowed_relation.witness_values[0] == base_value &&
                strcmp(borrowed_relation.relation_digest,
                       relation.relation_digest) == 0,
            error[0] ? error :
                "borrowed guard relation preserves identity and digest");
    for (index = 0u; index < borrowed_relation.evidence_len; index++) {
        REQUIRE(counts,
                arena_owns_ptr(
                    arena, borrowed_relation.evidence[index].value) &&
                    arena_owns_ptr(
                        arena, borrowed_relation.evidence[index].proof),
                "borrowed guard evidence stays in its declared owner");
    }
    REQUIRE(counts,
            relation.atom_storage ==
                    PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED &&
                relation.borrowed_atom_owner_len == 0u &&
                !relation.borrowed_atom_owners,
            "ordinary guard relation remains self-contained");

    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build_borrowed_atoms(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                matches, 6u, wrong_owners, 1u, DIGEST_A,
                &wrong_owner_relation, error, sizeof(error)) &&
                strstr(error, "owner") != NULL,
            "borrowed guard relation rejects the wrong Atom owner");

    expiring_base_value =
        atom_symbol(&expiring_owner, "expiring-base-value");
    expiring_value = atom_symbol(&expiring_owner, "expiring-value");
    expiring_proof = atom_symbol(&expiring_owner, "expiring-proof");
    expiring_base_values[0] = expiring_base_value;
    expiring_match = matches[3];
    expiring_match.value = expiring_value;
    expiring_match.proof = expiring_proof;
    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build_borrowed_atoms(
                &base, expiring_base_values, 1u,
                guard_terminal_ids, 2u,
                &expiring_match, 1u,
                expiring_owners, 1u, DIGEST_A,
                &expiring_relation, error, sizeof(error)) &&
                ppguard_relation_v1_validate(
                    &expiring_relation, error, sizeof(error)),
            error[0] ? error :
                "borrowed guard relation accepts its live owner");
    arena_free(&expiring_owner);
    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_validate(
                &expiring_relation, error, sizeof(error)) &&
                strstr(error, "owner") != NULL,
            "borrowed guard relation rejects an expired Atom owner");
    for (index = 0u; index < relation.evidence_len; index++) {
        const PPGuardRelationV1Evidence *evidence =
            &relation.evidence[index];
        if (atom_eq(evidence->value, shared))
            shared_proofs++;
        if (evidence->scalar_left == 0u &&
            (evidence->body_scalar_right == 1u ||
             evidence->body_scalar_right == 2u)) {
            distinct_extents++;
        }
        REQUIRE(counts,
                evidence->witness_id == index + 1u &&
                    evidence->scalar_left <=
                        evidence->body_scalar_right &&
                    evidence->byte_left ==
                        relation.byte_offsets[evidence->scalar_left] &&
                    evidence->body_byte_right ==
                        relation.byte_offsets[
                            evidence->body_scalar_right],
                "guard evidence retains its complete body span");
    }
    for (index = 0u; index < relation.lattice.edge_len; index++) {
        const CettaLpNativeUtf8LatticeEdge *edge =
            &relation.edges[index];
        if (edge->terminal_id == 7u || edge->terminal_id == 8u) {
            guard_edges++;
            REQUIRE(counts,
                    edge->scalar_left == edge->scalar_right &&
                        edge->byte_left == edge->byte_right &&
                        edge->value_kind ==
                            CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
                        edge->value >= relation.base_witness_len &&
                        edge->value < relation.witness_len,
                    "parser guard edge is zero-width and witness-bound");
        }
    }
    REQUIRE(counts,
            shared_proofs == 2u && distinct_extents == 3u &&
                guard_edges == 5u,
            "equal values retain proofs and distinct body extents");
    codepoints[0] = 98u;
    byte_offsets[1] = 2u;
    REQUIRE(counts,
            relation.codepoints[0] == 97u &&
                relation.byte_offsets[1] == 1u,
            "guard relation owns the retained Unicode carrier");
    codepoints[0] = 97u;
    byte_offsets[1] = 1u;

    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                reordered, 6u, DIGEST_A,
                &reordered_relation, error, sizeof(error)) &&
                strcmp(relation.relation_digest,
                       reordered_relation.relation_digest) == 0,
            error[0] ? error :
                "guard relation seal is independent of producer ordering");
    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                mutated, 6u, DIGEST_A,
                &mutated_relation, error, sizeof(error)) &&
                strcmp(relation.relation_digest,
                       mutated_relation.relation_digest) != 0,
            error[0] ? error : "guard relation seal binds proof identity");
    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                NULL, 0u, DIGEST_A,
                &absent_relation, error, sizeof(error)) &&
                absent_relation.evidence_len == 0u &&
                absent_relation.witness_len == 1u &&
                absent_relation.base_witness_len == 1u &&
                atom_eq(absent_relation.witness_values[0], base_value) &&
                absent_relation.lattice.edge_len == 1u &&
                absent_relation.lattice.terminal_len == 3u,
            error[0] ? error :
                "absent positive guard remains declared without an edge");

    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                collision_terminal_ids, 1u,
                NULL, 0u, DIGEST_A,
                &absent_relation, error, sizeof(error)) &&
                strstr(error, "disjoint") != NULL,
            "guard terminal collision fails closed");
    invalid = matches[1];
    invalid.body_byte_right = 2u;
    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                &invalid, 1u, DIGEST_A,
                &absent_relation, error, sizeof(error)) &&
                strstr(error, "inconsistent") != NULL,
            "inconsistent guard body extent fails closed");
    invalid = matches[1];
    invalid.proof = variable_proof;
    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                &invalid, 1u, DIGEST_A,
                &absent_relation, error, sizeof(error)) &&
                strstr(error, "not ground") != NULL,
            "nonground guard proof fails closed");
    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build(
                &base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                matches, 6u, "bad-digest",
                &absent_relation, error, sizeof(error)),
            "malformed guard provenance digest fails closed");
    altered_base = base;
    altered_base.source_pass_count = 2u;
    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build(
                &altered_base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                matches, 6u, DIGEST_A,
                &absent_relation, error, sizeof(error)),
            "multi-pass guard carrier fails closed");
    invalid_base_edge = base_edges[0];
    invalid_base_edge.value = 1u;
    altered_base = base;
    altered_base.edges = &invalid_base_edge;
    error[0] = '\0';
    REQUIRE(counts,
            !ppguard_relation_v1_build(
                &altered_base, base_witness_values, 1u,
                guard_terminal_ids, 2u,
                matches, 6u, DIGEST_A,
                &absent_relation, error, sizeof(error)) &&
                strstr(error, "outside") != NULL,
            "out-of-range base witness fails closed");
    if (strcmp(relation.relation_digest,
               EXPECTED_GUARD_RELATION_DIGEST) != 0) {
        fprintf(stderr, "guard relation digest changed: %s\n",
                relation.relation_digest);
    }
    REQUIRE(counts,
            strcmp(relation.relation_digest,
                   EXPECTED_GUARD_RELATION_DIGEST) == 0,
            "guard relation digest is frozen");

done:
    ppguard_relation_v1_free(&expiring_relation);
    ppguard_relation_v1_free(&wrong_owner_relation);
    ppguard_relation_v1_free(&borrowed_relation);
    ppguard_relation_v1_free(&absent_relation);
    ppguard_relation_v1_free(&mutated_relation);
    ppguard_relation_v1_free(&reordered_relation);
    ppguard_relation_v1_free(&relation);
    arena_free(&expiring_owner);
    arena_free(&wrong_owner);
    hashcons_free(&active_hashcons);
}

static void test_zero_width_semantic_witnesses(
    TestCounts *counts,
    Arena *arena,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *projection,
    const Atom *start_state) {
    static const uint32_t start_offsets[] = {0u, 0u};
    static const uint32_t byte_offsets[] = {0u};
    CettaLpNativeUtf8Lattice base_lattice;
    PPGuardRelationV1Match distinct_matches[2];
    PPGuardRelationV1Match equal_matches[2];
    PPGuardRelationV1 relation;
    PPGuardRelationV1 collapsed_relation;
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    PPNativeV1Result collapsed;
    Atom *left_value = atom_symbol(arena, "look-left");
    Atom *right_value = atom_symbol(arena, "look-right");
    Atom *left_proof = atom_symbol(arena, "look-left-proof");
    Atom *right_proof = atom_symbol(arena, "look-right-proof");
    Atom *left_result = binary(
        arena, "result", left_value, atom_symbol(arena, "nil"));
    Atom *right_result = binary(
        arena, "result", right_value, atom_symbol(arena, "nil"));
    uint32_t *base_terminal_ids = NULL;
    uint32_t guard_terminal_ids[1];
    uint32_t terminal_id;
    uint32_t index;
    char error[512] = {0};

    ppguard_relation_v1_init(&relation);
    ppguard_relation_v1_init(&collapsed_relation);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    ppnative_v1_result_init(&collapsed);
    REQUIRE(counts,
            projection && projection->entry_len == 1u &&
                pack->terminal_len > 0u &&
                pack->terminal_len < UINT32_MAX,
            "zero-width semantic witness fixture is representable");
    base_terminal_ids = malloc(
        sizeof(*base_terminal_ids) * pack->terminal_len);
    REQUIRE(counts, base_terminal_ids != NULL,
            "allocate base terminal identities for zero-width guard");
    for (index = 0u; index < pack->terminal_len; index++)
        base_terminal_ids[index] = index;
    terminal_id = projection->terminals[0].terminal_id;
    guard_terminal_ids[0] = terminal_id;
    base_lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = base_terminal_ids,
        .terminal_len = pack->terminal_len,
        .edges = NULL,
        .edge_len = 0u,
        .start_offsets = start_offsets,
        .start_offset_len = 2u,
        .codepoints = NULL,
        .byte_offsets = byte_offsets,
        .scalar_len = 0u,
        .input_byte_len = 0u,
        .decoded_byte_len = 0u,
        .source_pass_count = 1u,
    };
    distinct_matches[0] = (PPGuardRelationV1Match){
        terminal_id, 0u, 0u, 0u, 0u, left_value, left_proof,
    };
    distinct_matches[1] = (PPGuardRelationV1Match){
        terminal_id, 0u, 0u, 0u, 0u, right_value, right_proof,
    };
    equal_matches[0] = distinct_matches[0];
    equal_matches[1] = distinct_matches[1];
    equal_matches[1].value = left_value;

    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base_lattice, NULL, 0u,
                guard_terminal_ids, 1u,
                distinct_matches, 2u, projection->plan_digest,
                &relation, error, sizeof(error)) &&
                relation.evidence_len == 2u &&
                relation.witness_len == 2u &&
                relation.lattice.edge_len == 2u,
            error[0] ? error :
                "derive multi-valued zero-width guard relation");
    error[0] = '\0';
    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base_lattice, NULL, 0u,
                guard_terminal_ids, 1u,
                equal_matches, 2u, projection->plan_digest,
                &collapsed_relation, error, sizeof(error)) &&
                collapsed_relation.evidence_len == 2u &&
                collapsed_relation.witness_len == 2u &&
                strcmp(relation.relation_digest,
                       collapsed_relation.relation_digest) != 0,
            error[0] ? error :
                "derive proof-distinct equal-value guard relation");

    REQUIRE(counts,
            pplex_v1_gll_parse(
                pack, start_state, projection, &relation.lattice,
                relation.witness_values, relation.witness_len,
                10000u, 128u, 128u,
                &gll, error, sizeof(error)),
            error[0] ? error : "parse multi-valued zero-width GLL witness");
    error[0] = '\0';
    REQUIRE(counts,
            pplex_v1_glr_parse(
                pack, start_state, projection, &relation.lattice,
                relation.witness_values, relation.witness_len,
                10000u, 128u, 128u,
                &glr, error, sizeof(error)),
            error[0] ? error : "parse multi-valued zero-width GLR witness");
    if (!(gll.outcome == PPNATIVE_V1_COMPLETED &&
          glr.outcome == PPNATIVE_V1_COMPLETED &&
          gll.accepted && glr.accepted &&
          gll.semantic_result_len == 2u &&
          glr.semantic_result_len == 2u &&
          result_has(&gll, left_result) &&
          result_has(&gll, right_result) &&
          result_has(&glr, left_result) &&
          result_has(&glr, right_result))) {
        report_result_failure("zero-width-gll", &gll);
        report_result_failure("zero-width-glr", &glr);
    }
    REQUIRE(counts,
            gll.outcome == PPNATIVE_V1_COMPLETED &&
                glr.outcome == PPNATIVE_V1_COMPLETED &&
                gll.accepted && glr.accepted &&
                gll.semantic_result_len == 2u &&
                glr.semantic_result_len == 2u &&
                result_has(&gll, left_result) &&
                result_has(&gll, right_result) &&
                result_has(&glr, left_result) &&
                result_has(&glr, right_result),
            "zero-width witnesses preserve every distinct semantic value");
    REQUIRE(counts,
            strcmp(gll.forest_digest, glr.forest_digest) == 0 &&
                forest_contains(&gll, "(pf-witness 0)") &&
                forest_contains(&gll, "(pf-witness 1)") &&
                gll.forest.source_pass_count == 1u &&
                gll.forest.decoded_byte_len == 0u,
            "zero-width witness forests agree with one-pass provenance");

    error[0] = '\0';
    REQUIRE(counts,
            pplex_v1_gll_parse(
                pack, start_state, projection,
                &collapsed_relation.lattice,
                collapsed_relation.witness_values,
                collapsed_relation.witness_len,
                10000u, 128u, 128u,
                &collapsed, error, sizeof(error)),
            error[0] ? error : "parse equivalent zero-width GLL witnesses");
    REQUIRE(counts,
            collapsed.accepted && collapsed.semantic_result_len == 1u &&
                result_has(&collapsed, left_result) &&
                forest_contains(&collapsed, "(pf-witness 0)") &&
                forest_contains(&collapsed, "(pf-witness 1)"),
            "equivalent zero-width values collapse without erasing proofs");

done:
    free(base_terminal_ids);
    ppnative_v1_result_free(&collapsed);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    ppguard_relation_v1_free(&collapsed_relation);
    ppguard_relation_v1_free(&relation);
}

static void test_guard_body_extent_is_nonconsuming(
    TestCounts *counts,
    Arena *arena,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *projection,
    const Atom *start_state) {
    static const uint32_t codepoints[] = {97u, 97u};
    static const uint32_t byte_offsets[] = {0u, 1u, 2u};
    static const uint32_t start_offsets[] = {0u, 1u, 2u, 2u};
    CettaLpNativeUtf8LatticeEdge base_edges[2];
    CettaLpNativeUtf8Lattice base_lattice;
    PPGuardRelationV1Match match;
    PPGuardRelationV1 relation;
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    Atom *value = atom_symbol(arena, "look-through");
    Atom *proof = atom_symbol(arena, "look-through-proof");
    Atom *expected = binary(
        arena, "result", value, atom_symbol(arena, "nil"));
    uint32_t *base_terminal_ids = NULL;
    uint32_t guard_terminal_ids[1];
    uint32_t char_terminal_id = UINT32_MAX;
    uint32_t char_terminal_count = 0u;
    uint32_t guard_edge_count = 0u;
    uint32_t index;
    char error[512] = {0};

    ppguard_relation_v1_init(&relation);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    REQUIRE(counts,
            projection && projection->entry_len == 1u &&
                pack->terminal_len > 0u,
            "nonconsuming guard fixture is representable");
    for (index = 0u; index < pack->terminal_len; index++) {
        if (pack->terminals[index].kind == PPABI_V1_TERMINAL_CHAR &&
            pack->terminals[index].codepoint == 97u) {
            char_terminal_id = index;
            char_terminal_count++;
        }
    }
    REQUIRE(counts,
            char_terminal_count == 1u &&
                char_terminal_id < pack->terminal_len,
            "nonconsuming guard fixture has one consuming terminal");
    base_terminal_ids = malloc(
        sizeof(*base_terminal_ids) * pack->terminal_len);
    REQUIRE(counts, base_terminal_ids != NULL,
            "allocate nonconsuming guard terminal identities");
    for (index = 0u; index < pack->terminal_len; index++)
        base_terminal_ids[index] = index;
    base_edges[0] = (CettaLpNativeUtf8LatticeEdge){
        char_terminal_id, 0u, 1u, 0u, 1u,
        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 97u,
    };
    base_edges[1] = (CettaLpNativeUtf8LatticeEdge){
        char_terminal_id, 1u, 2u, 1u, 2u,
        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 97u,
    };
    base_lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = base_terminal_ids,
        .terminal_len = pack->terminal_len,
        .edges = base_edges,
        .edge_len = 2u,
        .start_offsets = start_offsets,
        .start_offset_len = 4u,
        .codepoints = codepoints,
        .byte_offsets = byte_offsets,
        .scalar_len = 2u,
        .input_byte_len = 2u,
        .decoded_byte_len = 2u,
        .source_pass_count = 1u,
    };
    guard_terminal_ids[0] = projection->terminals[0].terminal_id;
    match = (PPGuardRelationV1Match){
        guard_terminal_ids[0], 0u, 2u, 0u, 2u, value, proof,
    };
    REQUIRE(counts,
            ppguard_relation_v1_build(
                &base_lattice, NULL, 0u,
                guard_terminal_ids, 1u,
                &match, 1u, projection->plan_digest,
                &relation, error, sizeof(error)) &&
                relation.evidence_len == 1u &&
                relation.evidence[0].body_scalar_right == 2u,
            error[0] ? error :
                "derive zero-width guard from a consuming body proof");
    for (index = 0u; index < relation.lattice.edge_len; index++) {
        const CettaLpNativeUtf8LatticeEdge *edge =
            &relation.edges[index];
        if (edge->terminal_id == guard_terminal_ids[0] &&
            edge->scalar_left == 0u && edge->scalar_right == 0u &&
            edge->byte_left == 0u && edge->byte_right == 0u) {
            guard_edge_count++;
        }
    }
    REQUIRE(counts, guard_edge_count == 1u,
            "consuming body creates exactly one zero-width guard edge");
    error[0] = '\0';
    REQUIRE(counts,
            pplex_v1_gll_parse(
                pack, start_state, projection, &relation.lattice,
                relation.witness_values, relation.witness_len,
                10000u, 128u, 128u, &gll,
                error, sizeof(error)),
            error[0] ? error :
                "parse nonconsuming positive guard with GLL");
    error[0] = '\0';
    REQUIRE(counts,
            pplex_v1_glr_parse(
                pack, start_state, projection, &relation.lattice,
                relation.witness_values, relation.witness_len,
                10000u, 128u, 128u, &glr,
                error, sizeof(error)),
            error[0] ? error :
                "parse nonconsuming positive guard with GLR");
    if (!(gll.accepted && glr.accepted &&
          result_has(&gll, expected) && result_has(&glr, expected))) {
        report_result_failure("guard-extent-gll", &gll);
        report_result_failure("guard-extent-glr", &glr);
    }
    REQUIRE(counts,
            gll.outcome == PPNATIVE_V1_COMPLETED &&
                glr.outcome == PPNATIVE_V1_COMPLETED &&
                gll.accepted && glr.accepted &&
                result_has(&gll, expected) && result_has(&glr, expected) &&
                strcmp(gll.forest_digest, glr.forest_digest) == 0 &&
                forest_contains(
                    &gll,
                    "(pf-terminal (pp-span-terminal lexeme) "
                    "(pf-witness 0) 0 0)"),
            "body extent is evidence while the guard consumes nothing");

done:
    free(base_terminal_ids);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    ppguard_relation_v1_free(&relation);
}

int main(void) {
    static const uint8_t input[] = {
        0xceu, 0xbbu, 'a', 'a', 0xceu, 0xbbu,
    };
    static const CettaLpNativeUnicodeRange a_range[] = {{97u, 97u}};
    static const uint32_t nfa_starts[] = {0u};
    static const RSDFAV1NfaEdge nfa_edges[] = {
        {0u, 1u, RSDFA_V1_NFA_RANGES, a_range, 1u},
        {1u, 2u, RSDFA_V1_NFA_RANGES, a_range, 1u},
    };
    static const RSDFAV1NfaAccept nfa_accepts[] = {{2u, 0u}};
    static const char expected_result[] =
        "(result (document (cp 955) (token (cp 97) (cp 97)) (cp 955)) nil)";
    SymbolTable symbols;
    Arena arena;
    PPABIV1Pack pack;
    PPNativeV1Result scannerless_gll;
    PPNativeV1Result scannerless_glr;
    PPNativeV1Result projected_gll;
    PPNativeV1Result projected_glr;
    PPNativeV1Result rejected;
    PPNativeV1TerminalPlan terminal_plan = {0};
    PPNativeV1PreparedStartFamily witness_family;
    CettaLpNativeGrammar witness_grammar;
    PPLexV1Plan projection;
    PPLexV1Plan missing_projection;
    PPLexV1Plan forward_projection;
    PPLexV1Plan reverse_projection;
    PPLexV1Plan ambiguous_projection;
    PPLexV1Plan mismatch_projection;
    PPLexV1Plan open_projection;
    PPLexV1WitnessTable gll_witnesses;
    PPLexV1WitnessTable glr_witnesses;
    PPLexV1WitnessTable prepared_gll_witnesses;
    PPLexV1WitnessTable prepared_glr_witnesses;
    PPLexV1WitnessTable boundary_witnesses;
    RSDFAV1Plan dfa_plan;
    RSDFAV1ScanResult scan;
    RSDFAV1ParserLattice lattice;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_COMPLETED;
    RSDFAV1Nfa nfa = {
        .state_len = 3u,
        .start_states = nfa_starts,
        .start_len = 1u,
        .edges = nfa_edges,
        .edge_len = 2u,
        .accepts = nfa_accepts,
        .accept_len = 1u,
        .tag_len = 1u,
    };
    Atom *lex_state;
    Atom *spare_state;
    Atom *ambiguous_state;
    Atom *open_state;
    Atom *undefined_state;
    Atom *start_state;
    Atom *guard_state;
    Atom *guard_tail_state;
    Atom *lex_rhs[2];
    Atom *lex_action_arguments[2];
    Atom *spare_rhs[1];
    Atom *ambiguous_rhs[2];
    Atom *ambiguous_left_arguments[2];
    Atom *ambiguous_right_arguments[2];
    Atom *open_rhs[1];
    Atom *document_rhs[3];
    Atom *document_action_arguments[3];
    Atom *guard_rhs[1];
    Atom *guard_tail_rhs[3];
    Atom *productions[8];
    Atom *tags[1];
    Atom *missing_tags[1];
    Atom *ambiguous_tags[1];
    Atom *mismatch_tags[1];
    Atom *open_tags[1];
    Atom *forward_tags[2];
    Atom *reverse_tags[2];
    Atom *saved_projection_tag;
    Atom *saved_terminal_identity;
    char error[512] = {0};
    char saved_plan_digit;
    const CettaLpNativeGllPrepared *family_gll = NULL;
    const CettaLpNativeGlrPrepared *family_glr = NULL;
    TestCounts counts = {0};
    TestCounts guard_counts = {0};
    char guard_digest[65] = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    ppabi_v1_pack_init(&pack);
    ppnative_v1_result_init(&scannerless_gll);
    ppnative_v1_result_init(&scannerless_glr);
    ppnative_v1_result_init(&projected_gll);
    ppnative_v1_result_init(&projected_glr);
    ppnative_v1_result_init(&rejected);
    ppnative_v1_prepared_start_family_init(&witness_family);
    cetta_lp_native_grammar_init(&witness_grammar);
    pplex_v1_plan_init(&projection);
    pplex_v1_plan_init(&missing_projection);
    pplex_v1_plan_init(&forward_projection);
    pplex_v1_plan_init(&reverse_projection);
    pplex_v1_plan_init(&ambiguous_projection);
    pplex_v1_plan_init(&mismatch_projection);
    pplex_v1_plan_init(&open_projection);
    pplex_v1_witness_table_init(&gll_witnesses);
    pplex_v1_witness_table_init(&glr_witnesses);
    pplex_v1_witness_table_init(&prepared_gll_witnesses);
    pplex_v1_witness_table_init(&prepared_glr_witnesses);
    pplex_v1_witness_table_init(&boundary_witnesses);
    rsdfa_v1_plan_init(&dfa_plan);
    rsdfa_v1_scan_result_init(&scan);
    rsdfa_v1_parser_lattice_init(&lattice);

    test_guard_relation_contract(
        &guard_counts, &arena, guard_digest);
    REQUIRE(&counts, guard_counts.failed == 0u,
            "positive guard relation contract completed");

    lex_state = unary(&arena, "pp-def", atom_symbol(&arena, "lexeme"));
    spare_state = unary(&arena, "pp-def", atom_symbol(&arena, "spare"));
    ambiguous_state = unary(
        &arena, "pp-def", atom_symbol(&arena, "ambiguous-lexeme"));
    open_state = unary(
        &arena, "pp-def", atom_symbol(&arena, "open-lexeme"));
    undefined_state = unary(
        &arena, "pp-def", atom_symbol(&arena, "undefined-lexeme"));
    start_state = unary(&arena, "pp-def", atom_symbol(&arena, "document"));
    guard_state = unary(
        &arena, "pp-def", atom_symbol(&arena, "guard-document"));
    guard_tail_state = unary(
        &arena, "pp-def", atom_symbol(&arena, "guard-tail-document"));
    lex_rhs[0] = terminal_char(&arena, 97u);
    lex_rhs[1] = terminal_char(&arena, 97u);
    lex_action_arguments[0] = slot(&arena, 0u);
    lex_action_arguments[1] = slot(&arena, 1u);
    document_rhs[0] = terminal_char(&arena, 955u);
    document_rhs[1] = nonterminal(&arena, lex_state);
    document_rhs[2] = terminal_char(&arena, 955u);
    document_action_arguments[0] = slot(&arena, 0u);
    document_action_arguments[1] = slot(&arena, 1u);
    document_action_arguments[2] = slot(&arena, 2u);
    spare_rhs[0] = terminal_char(&arena, 98u);
    ambiguous_rhs[0] = terminal_char(&arena, 97u);
    ambiguous_rhs[1] = terminal_char(&arena, 97u);
    ambiguous_left_arguments[0] = slot(&arena, 0u);
    ambiguous_left_arguments[1] = slot(&arena, 1u);
    ambiguous_right_arguments[0] = slot(&arena, 0u);
    ambiguous_right_arguments[1] = slot(&arena, 1u);
    open_rhs[0] = nonterminal(&arena, undefined_state);
    guard_rhs[0] = nonterminal(&arena, lex_state);
    guard_tail_rhs[0] = nonterminal(&arena, lex_state);
    guard_tail_rhs[1] = terminal_char(&arena, 97u);
    guard_tail_rhs[2] = terminal_char(&arena, 97u);
    productions[0] = production(
        &arena, "lexeme-production", lex_state, lex_rhs, 2u,
        apply(&arena, "token", lex_action_arguments, 2u));
    productions[1] = production(
        &arena, "document-production", start_state, document_rhs, 3u,
        apply(&arena, "document", document_action_arguments, 3u));
    productions[2] = production(
        &arena, "spare-production", spare_state, spare_rhs, 1u,
        slot(&arena, 0u));
    productions[3] = production(
        &arena, "ambiguous-left-production", ambiguous_state,
        ambiguous_rhs, 2u,
        apply(&arena, "left-token", ambiguous_left_arguments, 2u));
    productions[4] = production(
        &arena, "ambiguous-right-production", ambiguous_state,
        ambiguous_rhs, 2u,
        apply(&arena, "right-token", ambiguous_right_arguments, 2u));
    productions[5] = production(
        &arena, "open-production", open_state, open_rhs, 1u,
        slot(&arena, 0u));
    productions[6] = production(
        &arena, "guard-document-production", guard_state, guard_rhs, 1u,
        slot(&arena, 0u));
    productions[7] = production(
        &arena, "guard-tail-document-production", guard_tail_state,
        guard_tail_rhs, 3u, slot(&arena, 0u));

    REQUIRE(&counts,
            load_pack(&arena, productions, 8u, &pack,
                      error, sizeof(error)),
            error[0] ? error : "load lexical ParserPack");
    REQUIRE(&counts,
            ppgll_v1_parse(
                &pack, start_state, input, sizeof(input), 10000u,
                128u, 128u, &scannerless_gll, error, sizeof(error)),
            error[0] ? error : "scannerless GLL parse");
    REQUIRE(&counts,
            ppglr_v1_parse(
                &pack, start_state, input, sizeof(input), 10000u,
                128u, 128u, &scannerless_glr, error, sizeof(error)),
            error[0] ? error : "scannerless GLR parse");
    REQUIRE(&counts,
            scannerless_gll.outcome == PPNATIVE_V1_COMPLETED &&
                scannerless_glr.outcome == PPNATIVE_V1_COMPLETED &&
                scannerless_gll.accepted && scannerless_glr.accepted,
            "scannerless parsers accept the complete input");
    REQUIRE(&counts,
            result_is(&scannerless_gll, expected_result) &&
                result_is(&scannerless_glr, expected_result),
            "scannerless semantic actions replay exactly");
    REQUIRE(&counts,
            strcmp(scannerless_gll.forest_digest,
                   scannerless_glr.forest_digest) == 0,
            "scannerless GLL and GLR forests agree");

    tags[0] = atom_symbol(&arena, "lexeme");
    REQUIRE(&counts,
            pplex_v1_plan_build(
                &pack, tags, 1u, DIGEST_B, DIGEST_C, &projection,
                error, sizeof(error)),
            error[0] ? error : "build lexical projection");
    REQUIRE(&counts,
            projection.entry_len == 1u &&
                projection.terminals[0].terminal_id == pack.terminal_len &&
                projection.tag_terminal_ids[0] == pack.terminal_len &&
                strcmp(projection.base_pack_digest, pack.pack_digest) == 0 &&
                strlen(projection.plan_digest) == 64u,
            "lexical projection identity and provenance are sealed");

    test_zero_width_semantic_witnesses(
        &counts, &arena, &pack, &projection, guard_state);
    REQUIRE(&counts, counts.failed == 0u,
            "zero-width semantic witness canary completed");
    test_guard_body_extent_is_nonconsuming(
        &counts, &arena, &pack, &projection, guard_tail_state);
    REQUIRE(&counts, counts.failed == 0u,
            "nonconsuming guard-body canary completed");

    forward_tags[0] = atom_symbol(&arena, "lexeme");
    forward_tags[1] = atom_symbol(&arena, "spare");
    reverse_tags[0] = atom_symbol(&arena, "spare");
    reverse_tags[1] = atom_symbol(&arena, "lexeme");
    REQUIRE(&counts,
            pplex_v1_plan_build(
                &pack, forward_tags, 2u, DIGEST_B, DIGEST_C,
                &forward_projection, error, sizeof(error)) &&
                pplex_v1_plan_build(
                    &pack, reverse_tags, 2u, DIGEST_B, DIGEST_C,
                    &reverse_projection, error, sizeof(error)),
            error[0] ? error : "build ordinal-sensitive projections");
    REQUIRE(&counts,
            strcmp(forward_projection.plan_digest,
                   reverse_projection.plan_digest) != 0 &&
                forward_projection.tag_terminal_ids[0] !=
                    reverse_projection.tag_terminal_ids[0],
            "projection digest binds the NFA tag ordinal mapping");

    missing_tags[0] = atom_symbol(&arena, "missing-lexeme");
    error[0] = '\0';
    REQUIRE(&counts,
            !pplex_v1_plan_build(
                &pack, missing_tags, 1u, DIGEST_B, DIGEST_C,
                &missing_projection, error, sizeof(error)) &&
                strstr(error, "no closed definition state") != NULL,
            "unknown lexical projection tag fails closed");

    open_tags[0] = atom_symbol(&arena, "open-lexeme");
    error[0] = '\0';
    REQUIRE(&counts,
            !pplex_v1_plan_build(
                &pack, open_tags, 1u, DIGEST_B, DIGEST_C,
                &open_projection, error, sizeof(error)) &&
                strstr(error, "undefined") != NULL,
            "open lexical projection state fails closed");

    error[0] = '\0';
    REQUIRE(&counts,
            ppnative_v1_terminal_plan_build(
                &pack, &terminal_plan, error, sizeof(error)),
            error[0] ? error : "build residual terminal plan");
    REQUIRE(&counts,
            rsdfa_v1_plan_build(
                &nfa, 32u, 128u, &dfa_plan, &build_outcome,
                error, sizeof(error)) &&
                build_outcome == RSDFA_V1_BUILD_COMPLETED,
            error[0] ? error : "build exact-aa DFA");
    REQUIRE(&counts,
            rsdfa_v1_scan(
                &dfa_plan, input, sizeof(input), 1000u, 32u,
                &scan, error, sizeof(error)) &&
                scan.outcome == RSDFA_V1_SCAN_COMPLETED,
            error[0] ? error : "scan exact-aa span relation");
    REQUIRE(&counts,
            scan.source_pass_count == 1u &&
                scan.input_scalar_len == 4u &&
                scan.decoded_byte_len == sizeof(input) &&
                scan.token_len == 1u &&
                scan.tokens[0].tag == 0u &&
                scan.tokens[0].start_scalar == 1u &&
                scan.tokens[0].end_scalar == 3u,
            "regular compiler witness is exact and single-pass");

    REQUIRE(&counts,
            rsdfa_v1_composite_parser_lattice_build(
                &scan, terminal_plan.terminals, terminal_plan.len,
                projection.tag_terminal_ids, projection.entry_len,
                &lattice, error, sizeof(error)),
            error[0] ? error : "build composite scalar/span lattice");
    REQUIRE(&counts,
                lattice.lattice.source_pass_count == 1u &&
                lattice.lattice.codepoints == scan.codepoints &&
                lattice.lattice.byte_offsets == scan.byte_offsets &&
                lattice.lattice.scalar_len == 4u,
            "parser lattice borrows the one decoded source carrier");

    REQUIRE(&counts,
            pplex_v1_witness_table_build_gll(
                &pack, &projection, &scan, 10000u, 128u, 128u,
                &gll_witnesses, error, sizeof(error)),
            error[0] ? error : "realize lexical witnesses with GLL");
    REQUIRE(&counts,
            pplex_v1_witness_table_build_glr(
                &pack, &projection, &scan, 10000u, 128u, 128u,
                &glr_witnesses, error, sizeof(error)),
            error[0] ? error : "realize lexical witnesses with GLR");
    REQUIRE(&counts,
            gll_witnesses.outcome == PPLEX_V1_WITNESS_COMPLETED &&
                glr_witnesses.outcome == PPLEX_V1_WITNESS_COMPLETED &&
                gll_witnesses.len == scan.token_len &&
                glr_witnesses.len == scan.token_len &&
                gll_witnesses.parser_runs == 1u &&
                glr_witnesses.parser_runs == 1u &&
                gll_witnesses.source_pass_count == 1u &&
                glr_witnesses.source_pass_count == 1u &&
                strcmp(gll_witnesses.plan_digest,
                       projection.plan_digest) == 0 &&
                strcmp(glr_witnesses.plan_digest,
                       projection.plan_digest) == 0,
            "lexical witness realization retains receipts");
    REQUIRE(&counts,
            gll_witnesses.values[0] && glr_witnesses.values[0] &&
                atom_eq(gll_witnesses.values[0], glr_witnesses.values[0]) &&
                atom_eq(
                    gll_witnesses.values[0],
                    binary(&arena, "token", cp(&arena, 97u), cp(&arena, 97u))),
            "independent GLL and GLR witness values agree exactly");

    REQUIRE(&counts,
            pplex_v1_witness_family_prepare(
                &pack, &projection, &witness_family,
                error, sizeof(error)) &&
                pplex_v1_witness_family_prepare(
                    &pack, &projection, &witness_family,
                    error, sizeof(error)) &&
                ppnative_v1_prepared_start_family_build_count(
                    &witness_family) == 1u &&
                ppnative_v1_prepared_start_family_len(
                    &witness_family) == 1u,
            error[0] ? error :
                "prepare and cache the lexical start-state family");
    REQUIRE(&counts,
            pplex_v1_witness_table_build_gll_prepared(
                &pack, &projection, &scan, &witness_family,
                &terminal_plan, 10000u, 128u, 128u,
                &prepared_gll_witnesses, error, sizeof(error)) &&
                pplex_v1_witness_table_build_glr_prepared(
                    &pack, &projection, &scan, &witness_family,
                    &terminal_plan, 10000u, 128u, 128u,
                    &prepared_glr_witnesses, error, sizeof(error)),
            error[0] ? error :
                "realize lexical witnesses with prepared families");
    REQUIRE(&counts,
            prepared_gll_witnesses.outcome ==
                PPLEX_V1_WITNESS_COMPLETED &&
                prepared_glr_witnesses.outcome ==
                    PPLEX_V1_WITNESS_COMPLETED &&
                prepared_gll_witnesses.parser_runs ==
                    gll_witnesses.parser_runs &&
                prepared_glr_witnesses.parser_runs ==
                    glr_witnesses.parser_runs &&
                atom_eq(prepared_gll_witnesses.values[0],
                        gll_witnesses.values[0]) &&
                atom_eq(prepared_glr_witnesses.values[0],
                        glr_witnesses.values[0]),
            "cold and prepared lexical witnesses agree exactly");
    error[0] = '\0';
    REQUIRE(&counts,
            !ppnative_v1_prepared_start_family_lookup(
                &witness_family, DIGEST_A,
                projection.entries[0].state_id,
                &family_gll, &family_glr,
                error, sizeof(error)) &&
                strstr(error, "binding") != NULL,
            "stale lexical witness family binding fails closed");
    {
        uint32_t duplicate_starts[2] = {
            projection.entries[0].state_id,
            projection.entries[0].state_id,
        };
        uint32_t invalid_start = UINT32_MAX;

        error[0] = '\0';
        REQUIRE(&counts,
                ppnative_v1_grammar_build(&pack, &witness_grammar) &&
                    ppnative_v1_prepared_start_family_prepare(
                        &witness_family, &witness_grammar,
                        projection.plan_digest, duplicate_starts, 2u,
                        error, sizeof(error)) &&
                    ppnative_v1_prepared_start_family_build_count(
                        &witness_family) == 1u &&
                    ppnative_v1_prepared_start_family_len(
                        &witness_family) == 1u,
                error[0] ? error :
                    "duplicate starts share one prepared parser pair");
        error[0] = '\0';
        REQUIRE(&counts,
                !ppnative_v1_prepared_start_family_prepare(
                    &witness_family, &witness_grammar, DIGEST_A,
                    &invalid_start, 1u, error, sizeof(error)) &&
                    ppnative_v1_prepared_start_family_build_count(
                        &witness_family) == 1u &&
                    ppnative_v1_prepared_start_family_lookup(
                        &witness_family, projection.plan_digest,
                        projection.entries[0].state_id,
                        &family_gll, &family_glr,
                        NULL, 0u),
                "failed family replacement preserves the prior owner");
    }

    ambiguous_tags[0] = atom_symbol(&arena, "ambiguous-lexeme");
    REQUIRE(&counts,
            pplex_v1_plan_build(
                &pack, ambiguous_tags, 1u, DIGEST_B, DIGEST_C,
                &ambiguous_projection, error, sizeof(error)) &&
                pplex_v1_witness_table_build_gll(
                    &pack, &ambiguous_projection, &scan,
                    10000u, 128u, 128u, &boundary_witnesses,
                    error, sizeof(error)) &&
                boundary_witnesses.outcome ==
                    PPLEX_V1_WITNESS_NONFUNCTIONAL &&
                strstr(boundary_witnesses.detail, "2 semantic values") != NULL,
            "nonfunctional lexical semantics remain a typed boundary");

    mismatch_tags[0] = atom_symbol(&arena, "spare");
    REQUIRE(&counts,
            pplex_v1_plan_build(
                &pack, mismatch_tags, 1u, DIGEST_B, DIGEST_C,
                &mismatch_projection, error, sizeof(error)) &&
                pplex_v1_witness_table_build_gll(
                    &pack, &mismatch_projection, &scan,
                    10000u, 128u, 128u, &boundary_witnesses,
                    error, sizeof(error)) &&
                boundary_witnesses.outcome ==
                    PPLEX_V1_WITNESS_RECOGNITION_MISMATCH,
            "DFA and ParserPack recognition disagreement remains typed");

    error[0] = '\0';
    REQUIRE(&counts,
            pplex_v1_gll_parse(
                &pack, start_state, &projection, &lattice.lattice,
                gll_witnesses.values, gll_witnesses.len,
                10000u, 128u, 128u,
                &projected_gll, error, sizeof(error)),
            error[0] ? error : "projected GLL parse");
    error[0] = '\0';
    REQUIRE(&counts,
            pplex_v1_glr_parse(
                &pack, start_state, &projection, &lattice.lattice,
                glr_witnesses.values, glr_witnesses.len,
                10000u, 128u, 128u,
                &projected_glr, error, sizeof(error)),
            error[0] ? error : "projected GLR parse");
    REQUIRE(&counts,
            projected_gll.outcome == PPNATIVE_V1_COMPLETED &&
                projected_glr.outcome == PPNATIVE_V1_COMPLETED &&
                projected_gll.accepted && projected_glr.accepted,
            "projected parsers accept the complete mixed input");
    REQUIRE(&counts,
            result_is(&projected_gll, expected_result) &&
                result_is(&projected_glr, expected_result) &&
                atom_eq(scannerless_gll.semantic_results[0],
                        projected_gll.semantic_results[0]),
            "projected and scannerless semantic results agree exactly");
    REQUIRE(&counts,
            strcmp(projected_gll.forest_digest,
                   projected_glr.forest_digest) == 0,
            "projected GLL and GLR forests agree");
    REQUIRE(&counts,
            strcmp(projected_gll.forest_digest,
                   scannerless_gll.forest_digest) != 0,
            "projected forest honestly records lexical compression");
    REQUIRE(&counts,
            forest_contains(&projected_gll,
                            "(pp-span-terminal lexeme)") &&
                forest_contains(&projected_gll, "(pf-witness 0)"),
            "projected forest retains terminal identity and witness identity");

    error[0] = '\0';
    REQUIRE(&counts,
            !pplex_v1_gll_parse(
                &pack, start_state, &projection, &lattice.lattice,
                NULL, 0u, 10000u, 128u, 128u,
                &rejected, error, sizeof(error)) &&
                error[0] != '\0',
            "missing lexical witness table fails closed");

    saved_plan_digit = projection.plan_digest[0];
    projection.plan_digest[0] = saved_plan_digit == '0' ? '1' : '0';
    error[0] = '\0';
    REQUIRE(&counts,
            !pplex_v1_gll_parse(
                &pack, start_state, &projection, &lattice.lattice,
                gll_witnesses.values, gll_witnesses.len,
                10000u, 128u, 128u,
                &rejected, error, sizeof(error)) &&
                strstr(error, "bad projected ParserPack") != NULL,
            "altered lexical projection provenance fails closed");
    projection.plan_digest[0] = saved_plan_digit;

    saved_projection_tag = projection.entries[0].tag;
    saved_terminal_identity = projection.terminals[0].identity;
    projection.entries[0].tag = missing_tags[0];
    projection.terminals[0].identity = unary(
        &projection.arena, "pp-span-terminal", missing_tags[0]);
    error[0] = '\0';
    REQUIRE(&counts,
            !pplex_v1_gll_parse(
                &pack, start_state, &projection, &lattice.lattice,
                gll_witnesses.values, gll_witnesses.len,
                10000u, 128u, 128u,
                &rejected, error, sizeof(error)) &&
                strstr(error, "bad projected ParserPack") != NULL,
            "altered span-terminal identity fails closed");
    projection.entries[0].tag = saved_projection_tag;
    projection.terminals[0].identity = saved_terminal_identity;

    error[0] = '\0';
    REQUIRE(&counts,
            !pplex_v1_gll_parse(
                &pack, lex_state, &projection, &lattice.lattice,
                gll_witnesses.values, gll_witnesses.len,
                10000u, 128u, 128u,
                &rejected, error, sizeof(error)) &&
                strstr(error, "cannot replace its start state") != NULL,
            "projection cannot collapse the requested start state");

done:
    printf("(ParserPackGuardRelationV1NativeSummary %u %s %u)\n",
           guard_counts.passed,
           guard_digest[0] ? guard_digest : "absent",
           guard_counts.failed);
    printf("(ParserPackLexicalV1NativeSummary %u %u)\n",
           counts.passed, counts.failed);
    pplex_v1_witness_table_free(&boundary_witnesses);
    pplex_v1_witness_table_free(&prepared_glr_witnesses);
    pplex_v1_witness_table_free(&prepared_gll_witnesses);
    pplex_v1_witness_table_free(&glr_witnesses);
    pplex_v1_witness_table_free(&gll_witnesses);
    rsdfa_v1_parser_lattice_free(&lattice);
    rsdfa_v1_scan_result_free(&scan);
    rsdfa_v1_plan_free(&dfa_plan);
    pplex_v1_plan_free(&reverse_projection);
    pplex_v1_plan_free(&forward_projection);
    pplex_v1_plan_free(&open_projection);
    pplex_v1_plan_free(&mismatch_projection);
    pplex_v1_plan_free(&ambiguous_projection);
    pplex_v1_plan_free(&missing_projection);
    pplex_v1_plan_free(&projection);
    ppnative_v1_terminal_plan_free(&terminal_plan);
    cetta_lp_native_grammar_free(&witness_grammar);
    ppnative_v1_prepared_start_family_free(&witness_family);
    ppnative_v1_result_free(&rejected);
    ppnative_v1_result_free(&projected_glr);
    ppnative_v1_result_free(&projected_gll);
    ppnative_v1_result_free(&scannerless_glr);
    ppnative_v1_result_free(&scannerless_gll);
    ppabi_v1_pack_free(&pack);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
