#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_pack_guard_plan_v1.h"

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
static const char EXPECTED_PLAN_DIGEST[] =
    "a3bfa6cd2cf1c7ce95364951a2c57fb06725380e7dbf5c3f126af530ad45cc7d";
static const char EXPECTED_EVIDENCE_DIGEST[] =
    "d07f35a171e059f04287f2eb3dfbabe6008f6ec073b7ec3d1019daf57075549f";
static const char EXPECTED_RELATION_DIGEST[] =
    "793ed3fe07bfc9e5070b2bb33c7967bc61001c0cb78c46068a6563c61dc586d7";

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

static Atom *slot(Arena *arena, uint32_t index) {
    Atom *peano = atom_symbol(arena, "q-zero");
    while (index > 0u) {
        peano = unary(arena, "q-succ", peano);
        index--;
    }
    return unary(arena, "pa-slot", peano);
}

static Atom *terminal_char(Arena *arena, uint32_t scalar) {
    return unary(arena, "pp-terminal",
                 unary(arena, "pp-terminal-char", cp(arena, scalar)));
}

static Atom *nonterminal(Arena *arena, Atom *state) {
    return unary(arena, "pp-nonterminal", state);
}

static Atom *production(Arena *arena,
                        Atom *label,
                        Atom *state,
                        Atom **rhs,
                        uint32_t rhs_len,
                        Atom *action) {
    Atom *arguments[4] = {
        label, state, items(arena, rhs, rhs_len), action,
    };
    return app(arena, "pp-production", 4u, arguments);
}

static Atom *pack_answer(Arena *arena, Atom *artifact) {
    Atom *arguments[2] = {atom_symbol(arena, "owner"), artifact};
    return app(arena, "compile-pack-production", 2u, arguments);
}

static Atom *certificate(Arena *arena, uint32_t index) {
    Atom *arguments[2] = {
        atom_int(arena, index), atom_symbol(arena, "evidence"),
    };
    return app(arena, "cert", 2u, arguments);
}

static Atom *guard_answer(Arena *arena,
                          Atom *owner,
                          Atom *state,
                          Atom *body,
                          Atom *action) {
    Atom *tag = unary(arena, "pp-positive-guard-tag", state);
    Atom *terminal = unary(arena, "pp-span-terminal", tag);
    Atom *rhs[1] = {unary(arena, "pp-terminal", terminal)};
    Atom *label_arguments[2] = {state, atom_symbol(arena, "peek")};
    Atom *label = app(arena, "pp-label", 2u, label_arguments);
    Atom *guard_production = production(
        arena, label, state, rhs, 1u,
        action ? action : slot(arena, 0u));
    Atom *artifact_arguments[4] = {
        state, tag, body, guard_production,
    };
    Atom *artifact = app(
        arena, "pp-positive-guard-v1", 4u, artifact_arguments);
    Atom *answer_arguments[2] = {owner, artifact};
    return app(arena, "compile-pack-positive-guard", 2u,
               answer_arguments);
}

static char *render(Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static char *evidence_key(Atom *answer, Atom *certificate) {
    char *answer_text = render(answer);
    char *certificate_text = render(certificate);
    size_t answer_len;
    size_t certificate_len;
    char *key = NULL;

    if (!answer_text || !certificate_text)
        goto done;
    answer_len = strlen(answer_text);
    certificate_len = strlen(certificate_text);
    if (answer_len > SIZE_MAX - certificate_len - 2u)
        goto done;
    key = malloc(answer_len + certificate_len + 2u);
    if (!key)
        goto done;
    memcpy(key, answer_text, answer_len);
    key[answer_len] = '\n';
    memcpy(key + answer_len + 1u,
           certificate_text, certificate_len + 1u);

done:
    free(certificate_text);
    free(answer_text);
    return key;
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
        roots[index].answer = pack_answer(arena, productions[index]);
        roots[index].certificate = certificate(arena, (uint32_t)index);
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

static int text_pointer_compare(const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static bool answer_set_digest(Atom **answers,
                              uint32_t answer_len,
                              char out[65]) {
    static const char domain[] = "FiniteHornAnswerSetV1\n";
    CettaNativeSha256 sha;
    char **terms = calloc(answer_len ? answer_len : 1u, sizeof(*terms));
    uint32_t index;
    bool ok = false;

    if (!terms)
        return false;
    for (index = 0u; index < answer_len; index++) {
        terms[index] = render(answers[index]);
        if (!terms[index])
            goto done;
    }
    if (answer_len > 1u)
        qsort(terms, answer_len, sizeof(*terms), text_pointer_compare);
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    for (index = 0u; index < answer_len; index++) {
        cetta_native_sha256_update(
            &sha, (const uint8_t *)terms[index], strlen(terms[index]));
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    ok = true;

done:
    for (index = 0u; index < answer_len; index++)
        free(terms[index]);
    free(terms);
    return ok;
}

static const PPGuardPlanV1Entry *find_entry(
    const PPGuardPlanV1 *plan,
    const Atom *state) {
    uint32_t index;
    for (index = 0u; index < plan->entry_len; index++) {
        if (atom_eq(plan->entries[index].state, (Atom *)state))
            return &plan->entries[index];
    }
    return NULL;
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

int main(void) {
    SymbolTable symbols;
    Arena arena;
    PPABIV1Pack pack;
    PPLexV1Plan lexical;
    PPGuardPlanV1 plan;
    PPGuardPlanV1 reordered_plan;
    PPGuardPlanV1 multiproof_plan;
    PPGuardPlanV1 rejected_plan;
    PPGuardRelationV1 relation;
    PPGuardRelationV1 derived_relation;
    PPGuardRelationV1 absent_relation;
    PPGuardRelationV1 wrong_source_relation;
    PPNativeV1Result main_gll;
    PPNativeV1Result main_glr;
    PPNativeV1Result derived_gll;
    PPNativeV1Result derived_glr;
    PPNativeV1Result root_gll;
    PPNativeV1Result root_glr;
    PPNativeV1Result absent_gll;
    PPNativeV1Result absent_glr;
    PPNativeV1Result rejected_result;
    PPGuardPlanV1Receipt main_gll_receipt = {0};
    PPGuardPlanV1Receipt main_glr_receipt = {0};
    PPGuardPlanV1Receipt derived_gll_receipt = {0};
    PPGuardPlanV1Receipt derived_glr_receipt = {0};
    PPGuardPlanV1Receipt root_gll_receipt = {0};
    PPGuardPlanV1Receipt root_glr_receipt = {0};
    PPGuardPlanV1Receipt absent_gll_receipt = {0};
    PPGuardPlanV1Receipt absent_glr_receipt = {0};
    PPGuardPlanV1Receipt rejected_receipt = {0};
    CettaLpNativeGrammar grammar;
    Atom *main_owner;
    Atom *root_owner;
    Atom *lex_owner;
    Atom *main_state;
    Atom *open_state;
    Atom *root_state;
    Atom *lex_state;
    Atom *main_rhs[1];
    Atom *lex_rhs[1];
    Atom *productions[2];
    Atom *tags[1];
    Atom *guard_nfa_tags[2];
    Atom *missing_guard_nfa_tags[1];
    Atom *foreign_guard_nfa_tags[2];
    Atom *open_body;
    Atom *root_body;
    Atom *open_answer;
    Atom *root_answer;
    Atom *bad_action_answer;
    Atom *defined_answer;
    Atom *wrong_owner_answer;
    Atom *conflicting_answer;
    Atom *answer_terms[2];
    PPGuardPlanV1DerivationInput derivations[2];
    PPGuardPlanV1DerivationInput reordered_derivations[2];
    PPGuardPlanV1DerivationInput multiproof_derivations[3];
    PPGuardPlanV1DerivationInput duplicate_derivations[3];
    PPGuardPlanV1DerivationInput conflict_derivations[3];
    PPGuardPlanV1ProvenanceInput provenance;
    const PPGuardPlanV1Entry *open_entry;
    const PPGuardPlanV1Entry *root_entry;
    uint32_t *base_terminal_ids = NULL;
    uint32_t base_terminal_len;
    static const uint32_t byte_offsets[] = {0u};
    static const uint32_t start_offsets[] = {0u, 0u};
    CettaLpNativeUtf8Lattice base_lattice;
    PPGuardRelationV1Match matches[2];
    PPNativeV1ForestExtension extension;
    PPNativeV1StateExtension bad_state;
    PPNativeV1ProductionExtension *bad_productions = NULL;
    Atom *saved_proof;
    Atom *saved_certificate;
    char *saved_derivation_canonical;
    char *mutated_derivation_canonical = NULL;
    bool mutated_certificate_rejected;
    uint32_t saved_terminal_id;
    char answer_digest[65] = {0};
    char error[512] = {0};
    TestCounts counts = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    ppabi_v1_pack_init(&pack);
    pplex_v1_plan_init(&lexical);
    ppguard_plan_v1_init(&plan);
    ppguard_plan_v1_init(&reordered_plan);
    ppguard_plan_v1_init(&multiproof_plan);
    ppguard_plan_v1_init(&rejected_plan);
    ppguard_relation_v1_init(&relation);
    ppguard_relation_v1_init(&derived_relation);
    ppguard_relation_v1_init(&absent_relation);
    ppguard_relation_v1_init(&wrong_source_relation);
    ppnative_v1_result_init(&main_gll);
    ppnative_v1_result_init(&main_glr);
    ppnative_v1_result_init(&derived_gll);
    ppnative_v1_result_init(&derived_glr);
    ppnative_v1_result_init(&root_gll);
    ppnative_v1_result_init(&root_glr);
    ppnative_v1_result_init(&absent_gll);
    ppnative_v1_result_init(&absent_glr);
    ppnative_v1_result_init(&rejected_result);
    cetta_lp_native_grammar_init(&grammar);

    main_owner = atom_symbol(&arena, "main");
    root_owner = atom_symbol(&arena, "root-guard");
    lex_owner = atom_symbol(&arena, "lexeme");
    main_state = unary(&arena, "pp-def", main_owner);
    open_state = binary(
        &arena, "pp-sub", main_state, atom_symbol(&arena, "guard"));
    root_state = unary(&arena, "pp-def", root_owner);
    lex_state = unary(&arena, "pp-def", lex_owner);
    main_rhs[0] = nonterminal(&arena, open_state);
    lex_rhs[0] = terminal_char(&arena, 97u);
    productions[0] = production(
        &arena, atom_symbol(&arena, "main-production"),
        main_state, main_rhs, 1u, slot(&arena, 0u));
    productions[1] = production(
        &arena, atom_symbol(&arena, "lex-production"),
        lex_state, lex_rhs, 1u, slot(&arena, 0u));
    REQUIRE(&counts,
            load_pack(&arena, productions, 2u, &pack,
                      error, sizeof(error)),
            error[0] ? error : "load open-state ParserPack canary");
    REQUIRE(&counts,
            ppnative_v1_state_find(&pack, open_state) >= 0 &&
                !pack.states[ppnative_v1_state_find(
                    &pack, open_state)].defined &&
                ppnative_v1_state_find(&pack, root_state) < 0,
            "canary has one open and one absent guard state");

    tags[0] = lex_owner;
    REQUIRE(&counts,
            pplex_v1_plan_build(
                &pack, tags, 1u, DIGEST_B, DIGEST_C,
                &lexical, error, sizeof(error)),
            error[0] ? error : "build lexical side of composite plan");

    open_body = unary(
        &arena, "sir-eps", atom_symbol(&arena, "nested-body"));
    root_body = unary(
        &arena, "sir-eps", atom_symbol(&arena, "root-body"));
    open_answer = guard_answer(
        &arena, main_owner, open_state, open_body, NULL);
    root_answer = guard_answer(
        &arena, root_owner, root_state, root_body, NULL);
    guard_nfa_tags[0] = unary(
        &arena, "pp-positive-guard-tag", open_state);
    guard_nfa_tags[1] = unary(
        &arena, "pp-positive-guard-tag", root_state);
    answer_terms[0] = open_answer;
    answer_terms[1] = root_answer;
    REQUIRE(&counts,
            answer_set_digest(answer_terms, 2u, answer_digest),
            "compute guard answer-set provenance digest");
    derivations[0] = (PPGuardPlanV1DerivationInput){
        .answer = root_answer,
        .certificate = certificate(&arena, 2u),
    };
    derivations[1] = (PPGuardPlanV1DerivationInput){
        .answer = open_answer,
        .certificate = certificate(&arena, 1u),
    };
    provenance = (PPGuardPlanV1ProvenanceInput){
        .source_digest = DIGEST_A,
        .pre_reflection_digest = DIGEST_B,
        .environment_digest = DIGEST_C,
        .answer_set_digest = answer_digest,
        .regular_compiler_digest = DIGEST_B,
        .guard_nfa_answer_digest = DIGEST_C,
        .guard_nfa_tags = guard_nfa_tags,
        .guard_nfa_tag_len = 2u,
        .derivations = derivations,
        .derivation_len = 2u,
    };
    REQUIRE(&counts,
            ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &plan, error, sizeof(error)),
            error[0] ? error : "build sealed composite guard plan");
    missing_guard_nfa_tags[0] = guard_nfa_tags[0];
    provenance.guard_nfa_tags = missing_guard_nfa_tags;
    provenance.guard_nfa_tag_len = 1u;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "bijective") != NULL,
            "guard body without one compiled NFA tag fails closed");
    foreign_guard_nfa_tags[0] = guard_nfa_tags[0];
    foreign_guard_nfa_tags[1] = unary(
        &arena, "pp-positive-guard-tag",
        unary(&arena, "pp-def", atom_symbol(&arena, "foreign-guard")));
    provenance.guard_nfa_tags = foreign_guard_nfa_tags;
    provenance.guard_nfa_tag_len = 2u;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "bijective") != NULL,
            "compiled NFA tag without one guard body fails closed");
    provenance.guard_nfa_tags = guard_nfa_tags;
    provenance.guard_nfa_tag_len = 2u;
    open_entry = find_entry(&plan, open_state);
    root_entry = find_entry(&plan, root_state);
    REQUIRE(&counts,
            open_entry && root_entry && plan.entry_len == 2u &&
                plan.derivation_len == 2u && plan.state_len == 1u &&
                plan.production_len == 2u &&
                plan.lexical_terminal_len == 1u &&
                plan.terminal_len == 3u &&
                !open_entry->state_is_extension &&
                root_entry->state_is_extension &&
                strlen(plan.plan_digest) == 64u,
            "plan assigns dense generic state, terminal, and production IDs");
    REQUIRE(&counts,
            ppguard_plan_v1_grammar_build(
                &pack, &lexical, &plan, &grammar,
                error, sizeof(error)) &&
                grammar.production_len == pack.production_len + 2u &&
                grammar.productions[pack.production_len].label ==
                    pack.production_len &&
                grammar.productions[pack.production_len + 1u].label ==
                    pack.production_len + 1u,
            error[0] ? error : "extend native grammar atomically");
    cetta_lp_native_grammar_free(&grammar);

    extension = (PPNativeV1ForestExtension){
        .states = plan.states,
        .state_len = plan.state_len,
        .terminals = plan.terminals,
        .terminal_len = plan.terminal_len,
        .productions = plan.productions,
        .production_len = plan.production_len,
    };
    REQUIRE(&counts,
            ppnative_v1_start_is_closed_extended(
                &pack, main_state, &extension,
                error, sizeof(error)) &&
                ppnative_v1_start_is_closed_extended(
                    &pack, root_state, &extension,
                    error, sizeof(error)),
            error[0] ? error :
                "extended closure covers nested and root guards");
    bad_state = plan.states[0];
    bad_state.state_id++;
    extension.states = &bad_state;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppnative_v1_start_is_closed_extended(
                &pack, root_state, &extension,
                error, sizeof(error)) &&
                strstr(error, "contiguous") != NULL,
            "noncontiguous supplemental state fails closed");
    extension.states = plan.states;
    bad_productions = malloc(
        sizeof(*bad_productions) * plan.production_len);
    REQUIRE(&counts, bad_productions != NULL,
            "allocate corrupted supplemental production fixture");
    memcpy(bad_productions, plan.productions,
           sizeof(*bad_productions) * plan.production_len);
    bad_productions[0].action = unary(
        &arena, "pa-const", atom_symbol(&arena, "corrupt"));
    extension.productions = bad_productions;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppnative_v1_start_is_closed_extended(
                &pack, main_state, &extension,
                error, sizeof(error)) &&
                strstr(error, "inconsistent") != NULL,
            "supplemental action/identity mismatch fails closed");
    extension.productions = plan.productions;

    base_terminal_len = pack.terminal_len + lexical.entry_len;
    base_terminal_ids = malloc(
        sizeof(*base_terminal_ids) * base_terminal_len);
    REQUIRE(&counts, base_terminal_ids != NULL,
            "allocate composite base terminal inventory");
    for (uint32_t index = 0u; index < base_terminal_len; index++)
        base_terminal_ids[index] = index;
    base_lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = base_terminal_ids,
        .terminal_len = base_terminal_len,
        .start_offsets = start_offsets,
        .start_offset_len = 2u,
        .byte_offsets = byte_offsets,
        .scalar_len = 0u,
        .input_byte_len = 0u,
        .decoded_byte_len = 0u,
        .source_pass_count = 1u,
    };
    matches[0] = (PPGuardRelationV1Match){
        .guard_terminal_id = open_entry->terminal_id,
        .scalar_left = 0u,
        .body_scalar_right = 0u,
        .byte_left = 0u,
        .body_byte_right = 0u,
        .value = atom_symbol(&arena, "nested-value"),
        .proof = atom_symbol(&arena, "nested-proof"),
    };
    matches[1] = (PPGuardRelationV1Match){
        .guard_terminal_id = root_entry->terminal_id,
        .scalar_left = 0u,
        .body_scalar_right = 0u,
        .byte_left = 0u,
        .body_byte_right = 0u,
        .value = atom_symbol(&arena, "root-value"),
        .proof = atom_symbol(&arena, "root-proof"),
    };
    REQUIRE(&counts,
            ppguard_relation_v1_build(
                &base_lattice, NULL, 0u,
                plan.guard_terminal_ids, plan.entry_len,
                matches, 2u, plan.plan_digest,
                &relation, error, sizeof(error)) &&
                ppguard_relation_v1_validate(
                    &relation, error, sizeof(error)),
            error[0] ? error : "build plan-bound guard relation");

    error[0] = '\0';
    REQUIRE(&counts,
            ppguard_plan_v1_gll_parse(
                &pack, main_state, &lexical, &plan, &relation,
                10000u, 128u, 128u,
                &main_gll, &main_gll_receipt,
                error, sizeof(error)),
            error[0] ? error : "parse nested guard with GLL");
    error[0] = '\0';
    REQUIRE(&counts,
            ppguard_plan_v1_glr_parse(
                &pack, main_state, &lexical, &plan, &relation,
                10000u, 128u, 128u,
                &main_glr, &main_glr_receipt,
                error, sizeof(error)),
            error[0] ? error : "parse nested guard with GLR");
    REQUIRE(&counts,
            main_gll.outcome == PPNATIVE_V1_COMPLETED &&
                main_glr.outcome == PPNATIVE_V1_COMPLETED &&
                main_gll.accepted && main_glr.accepted &&
                result_is(&main_gll, "(result nested-value nil)") &&
                result_is(&main_glr, "(result nested-value nil)") &&
                strcmp(main_gll.forest_digest,
                       main_glr.forest_digest) == 0,
            "nested guard has exact GLL/GLR forest and semantics");
    REQUIRE(&counts,
            forest_contains(&main_gll, "pp-positive-guard-tag") &&
                forest_contains(&main_gll, "peek") &&
                forest_contains(&main_gll, "pf-witness"),
            "neutral forest retains guard identity and witness provenance");
    REQUIRE(&counts,
            strcmp(main_gll_receipt.plan_digest, plan.plan_digest) == 0 &&
                strcmp(main_gll_receipt.evidence_digest,
                       plan.evidence_digest) == 0 &&
                strcmp(main_gll_receipt.relation_digest,
                       relation.relation_digest) == 0 &&
                strcmp(main_gll_receipt.forest_digest,
                       main_gll.forest_digest) == 0 &&
                main_gll_receipt.source_pass_count == 1u &&
                strlen(main_gll_receipt.execution_digest) == 64u &&
                strcmp(main_gll_receipt.execution_digest,
                       main_glr_receipt.execution_digest) != 0,
            "execution receipt binds backend, plan, relation, and forest");

    {
        CettaLpNativeUtf8Lattice derived_lattice = base_lattice;

        derived_lattice.source_pass_count = 0u;
        error[0] = '\0';
        REQUIRE(&counts,
                ppguard_relation_v1_build(
                    &derived_lattice, NULL, 0u,
                    plan.guard_terminal_ids, plan.entry_len,
                    matches, 2u, plan.plan_digest,
                    &derived_relation, error, sizeof(error)) &&
                    ppguard_relation_v1_validate(
                        &derived_relation, error, sizeof(error)),
                error[0] ? error :
                    "build guard relation over a derived scalar view");
        REQUIRE(&counts,
                ppguard_plan_v1_gll_parse(
                    &pack, main_state, &lexical, &plan,
                    &derived_relation, 10000u, 128u, 128u,
                    &derived_gll, &derived_gll_receipt,
                    error, sizeof(error)) &&
                    ppguard_plan_v1_glr_parse(
                        &pack, main_state, &lexical, &plan,
                        &derived_relation, 10000u, 128u, 128u,
                        &derived_glr, &derived_glr_receipt,
                        error, sizeof(error)),
                error[0] ? error :
                    "parse a zero-pass guard relation with both kernels");
        REQUIRE(&counts,
                derived_gll.accepted && derived_glr.accepted &&
                    result_is(&derived_gll, "(result nested-value nil)") &&
                    result_is(&derived_glr, "(result nested-value nil)") &&
                    derived_gll_receipt.source_pass_count == 0u &&
                    derived_glr_receipt.source_pass_count == 0u &&
                    strcmp(derived_gll.forest_digest,
                           derived_glr.forest_digest) == 0,
                "derived guard execution performs no source pass");
    }

    error[0] = '\0';
    REQUIRE(&counts,
            ppguard_plan_v1_gll_parse(
                &pack, root_state, &lexical, &plan, &relation,
                10000u, 128u, 128u,
                &root_gll, &root_gll_receipt,
                error, sizeof(error)) &&
                ppguard_plan_v1_glr_parse(
                    &pack, root_state, &lexical, &plan, &relation,
                    10000u, 128u, 128u,
                    &root_glr, &root_glr_receipt,
                    error, sizeof(error)),
            error[0] ? error : "parse extended root guard with both kernels");
    REQUIRE(&counts,
            root_gll.accepted && root_glr.accepted &&
                result_is(&root_gll, "(result root-value nil)") &&
                result_is(&root_glr, "(result root-value nil)") &&
                strcmp(root_gll.forest_digest,
                       root_glr.forest_digest) == 0,
            "absent base state becomes one exact generic root guard");

    REQUIRE(&counts,
            ppguard_relation_v1_build(
                &base_lattice, NULL, 0u,
                plan.guard_terminal_ids, plan.entry_len,
                NULL, 0u, plan.plan_digest,
                &absent_relation, error, sizeof(error)) &&
                ppguard_plan_v1_gll_parse(
                    &pack, main_state, &lexical, &plan, &absent_relation,
                    10000u, 128u, 128u,
                    &absent_gll, &absent_gll_receipt,
                    error, sizeof(error)) &&
                ppguard_plan_v1_glr_parse(
                    &pack, main_state, &lexical, &plan, &absent_relation,
                    10000u, 128u, 128u,
                    &absent_glr, &absent_glr_receipt,
                    error, sizeof(error)),
            error[0] ? error : "parse absent guard relation");
    REQUIRE(&counts,
            !absent_gll.accepted && !absent_glr.accepted &&
                absent_gll.semantic_result_len == 0u &&
                absent_glr.semantic_result_len == 0u &&
                strcmp(absent_gll.forest_digest,
                       absent_glr.forest_digest) == 0,
            "absent positive guard rejects without consuming input");

    reordered_derivations[0] = derivations[1];
    reordered_derivations[1] = derivations[0];
    provenance.derivations = reordered_derivations;
    REQUIRE(&counts,
            ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &reordered_plan, error, sizeof(error)) &&
                strcmp(reordered_plan.plan_digest,
                       plan.plan_digest) == 0,
            error[0] ? error :
                "plan digest is independent of producer ordering");
    multiproof_derivations[0] = derivations[0];
    multiproof_derivations[1] = derivations[1];
    multiproof_derivations[2] = (PPGuardPlanV1DerivationInput){
        .answer = open_answer,
        .certificate = certificate(&arena, 3u),
    };
    provenance.derivations = multiproof_derivations;
    provenance.derivation_len = 3u;
    REQUIRE(&counts,
            ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &multiproof_plan, error, sizeof(error)) &&
                multiproof_plan.entry_len == 2u &&
                multiproof_plan.derivation_len == 3u &&
                strcmp(multiproof_plan.answer_set_digest,
                       plan.answer_set_digest) == 0 &&
                strcmp(multiproof_plan.plan_digest,
                       plan.plan_digest) == 0 &&
                strcmp(multiproof_plan.evidence_digest,
                       plan.evidence_digest) != 0,
            error[0] ? error :
                "proof multiplicity changes evidence but not semantic plan");

    duplicate_derivations[0] = derivations[0];
    duplicate_derivations[1] = derivations[1];
    duplicate_derivations[2] = derivations[1];
    provenance.derivations = duplicate_derivations;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "duplicate") != NULL,
            "duplicate guard derivation root fails closed");
    provenance.derivations = derivations;
    provenance.derivation_len = 2u;
    provenance.answer_set_digest = DIGEST_A;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "answer-set") != NULL,
            "stale guard answer-set digest fails closed");
    provenance.answer_set_digest = answer_digest;

    bad_action_answer = guard_answer(
        &arena, main_owner, open_state, open_body,
        unary(&arena, "pa-const", atom_symbol(&arena, "corrupt")));
    derivations[1].answer = bad_action_answer;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "malformed") != NULL,
            "corrupt guard action fails closed");
    derivations[1].answer = open_answer;

    defined_answer = guard_answer(
        &arena, lex_owner, lex_state, root_body, NULL);
    derivations[1].answer = defined_answer;
    answer_terms[0] = root_answer;
    answer_terms[1] = defined_answer;
    REQUIRE(&counts,
            answer_set_digest(answer_terms, 2u, answer_digest),
            "compute defined-state mutation digest");
    provenance.answer_set_digest = answer_digest;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "defined") != NULL,
            "guard sidecar cannot replace a defined state");

    wrong_owner_answer = guard_answer(
        &arena, atom_symbol(&arena, "other-owner"),
        open_state, open_body, NULL);
    derivations[1].answer = wrong_owner_answer;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "malformed") != NULL,
            "guard owner/state provenance mismatch fails closed");

    conflicting_answer = guard_answer(
        &arena, main_owner, open_state,
        unary(&arena, "sir-eps", atom_symbol(&arena, "other-body")),
        NULL);
    conflict_derivations[0] = derivations[0];
    conflict_derivations[1] = (PPGuardPlanV1DerivationInput){
        .answer = open_answer,
        .certificate = certificate(&arena, 1u),
    };
    conflict_derivations[2] = (PPGuardPlanV1DerivationInput){
        .answer = conflicting_answer,
        .certificate = certificate(&arena, 4u),
    };
    provenance.derivations = conflict_derivations;
    provenance.derivation_len = 3u;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_build(
                &pack, &lexical, &provenance,
                &rejected_plan, error, sizeof(error)) &&
                strstr(error, "conflicting") != NULL,
            "one guard state cannot carry conflicting artifacts");
    derivations[1].answer = open_answer;
    provenance.derivations = derivations;
    provenance.derivation_len = 2u;
    provenance.answer_set_digest = plan.answer_set_digest;

    saved_terminal_id = plan.terminals[plan.lexical_terminal_len].terminal_id;
    plan.terminals[plan.lexical_terminal_len].terminal_id++;
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_grammar_build(
                &pack, &lexical, &plan, &grammar,
                error, sizeof(error)) && error[0] != '\0',
            "mutated dense terminal assignment fails closed");
    plan.terminals[plan.lexical_terminal_len].terminal_id =
        saved_terminal_id;

    saved_certificate = plan.derivations[0].certificate;
    saved_derivation_canonical = plan.derivations[0].canonical;
    plan.derivations[0].certificate =
        atom_symbol(&arena, "mutated-certificate");
    mutated_derivation_canonical = evidence_key(
        plan.derivations[0].answer,
        plan.derivations[0].certificate);
    plan.derivations[0].canonical = mutated_derivation_canonical;
    error[0] = '\0';
    mutated_certificate_rejected = mutated_derivation_canonical &&
        !ppguard_plan_v1_grammar_build(
            &pack, &lexical, &plan, &grammar,
            error, sizeof(error)) &&
        strstr(error, "digest") != NULL;
    free(mutated_derivation_canonical);
    mutated_derivation_canonical = NULL;
    plan.derivations[0].certificate = saved_certificate;
    plan.derivations[0].canonical = saved_derivation_canonical;
    REQUIRE(&counts, mutated_certificate_rejected,
            "mutated executor-local certificate invalidates evidence seal");

    saved_proof = relation.evidence[0].proof;
    relation.evidence[0].proof = atom_symbol(&arena, "mutated-proof");
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_relation_v1_validate(
                &relation, error, sizeof(error)) &&
                strstr(error, "digest") != NULL,
            "mutated guard proof invalidates the relation seal");
    relation.evidence[0].proof = saved_proof;
    REQUIRE(&counts,
            ppguard_relation_v1_validate(
                &relation, error, sizeof(error)),
            error[0] ? error : "restored guard relation validates");

    REQUIRE(&counts,
            ppguard_relation_v1_build(
                &base_lattice, NULL, 0u,
                plan.guard_terminal_ids, plan.entry_len,
                matches, 2u, DIGEST_A,
                &wrong_source_relation, error, sizeof(error)),
            error[0] ? error : "build valid relation with wrong plan source");
    error[0] = '\0';
    REQUIRE(&counts,
            !ppguard_plan_v1_gll_parse(
                &pack, main_state, &lexical, &plan,
                &wrong_source_relation,
                10000u, 128u, 128u,
                &rejected_result, &rejected_receipt,
                error, sizeof(error)) &&
                strstr(error, "provenance") != NULL,
            "relation from another plan fails closed");
    REQUIRE(&counts,
            strcmp(plan.plan_digest, EXPECTED_PLAN_DIGEST) == 0 &&
                strcmp(plan.evidence_digest,
                       EXPECTED_EVIDENCE_DIGEST) == 0 &&
                strcmp(relation.relation_digest,
                       EXPECTED_RELATION_DIGEST) == 0,
            "guard plan, evidence, and relation digests are frozen");

done:
    printf("(ParserPackPositiveGuardPlanV1NativeSummary %u %s %s %s %u)\n",
           counts.passed,
           plan.plan_digest[0] ? plan.plan_digest : "absent",
           plan.evidence_digest[0] ? plan.evidence_digest : "absent",
           relation.relation_digest[0]
               ? relation.relation_digest : "absent",
           counts.failed);
    free(bad_productions);
    free(mutated_derivation_canonical);
    free(base_terminal_ids);
    cetta_lp_native_grammar_free(&grammar);
    ppnative_v1_result_free(&rejected_result);
    ppnative_v1_result_free(&absent_glr);
    ppnative_v1_result_free(&absent_gll);
    ppnative_v1_result_free(&root_glr);
    ppnative_v1_result_free(&root_gll);
    ppnative_v1_result_free(&main_glr);
    ppnative_v1_result_free(&main_gll);
    ppguard_relation_v1_free(&wrong_source_relation);
    ppguard_relation_v1_free(&absent_relation);
    ppguard_relation_v1_free(&relation);
    ppnative_v1_result_free(&derived_glr);
    ppnative_v1_result_free(&derived_gll);
    ppguard_relation_v1_free(&derived_relation);
    ppguard_plan_v1_free(&rejected_plan);
    ppguard_plan_v1_free(&multiproof_plan);
    ppguard_plan_v1_free(&reordered_plan);
    ppguard_plan_v1_free(&plan);
    pplex_v1_plan_free(&lexical);
    ppabi_v1_pack_free(&pack);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
