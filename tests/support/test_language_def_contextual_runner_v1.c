#include "native/language_def_contextual_runner_v1.h"
#include "native/operational_language_def_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

typedef enum {
    PROVIDER_EQUALITY_NORMAL = 0,
    PROVIDER_EQUALITY_DUPLICATE,
    PROVIDER_EQUALITY_INVENTED,
    PROVIDER_EQUALITY_FAILURE
} EqualityProviderMode;

typedef struct {
    EqualityProviderMode mode;
    CettaLdPatternV1 equal;
    CettaLdPatternV1 different;
    CettaLdPatternV1 invented;
    CettaLdPatternV1 row[3];
} EqualityProvider;

static bool expect(TestCounts *counts, bool condition, const char *name) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

static CettaLdTextV1 text_owned(const char *value) {
    CettaLdTextV1 result = {0};
    size_t len = value ? strlen(value) : 0u;
    if (len > UINT32_MAX)
        abort();
    if (len > 0u) {
        result.bytes = malloc(len);
        if (!result.bytes)
            abort();
        memcpy(result.bytes, value, len);
    }
    result.len = (uint32_t)len;
    return result;
}

static bool text_is(const CettaLdTextV1 *text, const char *value) {
    size_t len = value ? strlen(value) : 0u;
    return text && value && (size_t)text->len == len &&
        (len == 0u ||
         (text->bytes && memcmp(text->bytes, value, len) == 0));
}

static CettaLdPatternV1 app_n(const char *head,
                              const CettaLdPatternV1 *arguments,
                              uint32_t argument_len) {
    CettaLdPatternV1 result;
    cetta_ld_pattern_v1_init(&result);
    result.kind = CETTA_LD_PATTERN_APPLY_V1;
    result.as.apply.head = text_owned(head);
    result.as.apply.arguments.len = argument_len;
    if (argument_len > 0u) {
        result.as.apply.arguments.items = malloc(
            (size_t)argument_len * sizeof(*result.as.apply.arguments.items));
        if (!result.as.apply.arguments.items)
            abort();
        memcpy(result.as.apply.arguments.items, arguments,
               (size_t)argument_len * sizeof(*arguments));
    }
    return result;
}

static CettaLdPatternV1 app0(const char *head) {
    return app_n(head, NULL, 0u);
}

static CettaLdPatternV1 app1(const char *head, CettaLdPatternV1 first) {
    CettaLdPatternV1 arguments[1] = {first};
    return app_n(head, arguments, 1u);
}

static CettaLdPatternV1 app2(const char *head, CettaLdPatternV1 first,
                             CettaLdPatternV1 second) {
    CettaLdPatternV1 arguments[2] = {first, second};
    return app_n(head, arguments, 2u);
}

static CettaLdPatternV1 app3(const char *head, CettaLdPatternV1 first,
                             CettaLdPatternV1 second,
                             CettaLdPatternV1 third) {
    CettaLdPatternV1 arguments[3] = {first, second, third};
    return app_n(head, arguments, 3u);
}

static CettaLdPatternV1 app4(const char *head, CettaLdPatternV1 first,
                             CettaLdPatternV1 second,
                             CettaLdPatternV1 third,
                             CettaLdPatternV1 fourth) {
    CettaLdPatternV1 arguments[4] = {first, second, third, fourth};
    return app_n(head, arguments, 4u);
}

static bool load_language(const char *path,
                          CettaOperationalLanguageDefV1 *wire,
                          CettaLanguageDefCoreV1 *language,
                          char *error,
                          size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    return cetta_op_lang_v1_parse_file(
               wire, path, 8000000u, 16000000u, &wire_status,
               error, error_size) &&
        cetta_language_def_core_v1_decode(
            language, wire, 1000000u, &core_status, error, error_size);
}

static bool load_language_bytes(const char *source,
                                CettaOperationalLanguageDefV1 *wire,
                                CettaLanguageDefCoreV1 *language,
                                char *error,
                                size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    size_t source_len = source ? strlen(source) : 0u;
    return source &&
        cetta_op_lang_v1_parse_bytes(
            wire, (const uint8_t *)source, source_len,
            8000000u, 16000000u, &wire_status, error, error_size) &&
        cetta_language_def_core_v1_decode(
            language, wire, 1000000u, &core_status, error, error_size);
}

static const CettaLdTextV1 *trace_rule_name(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdCrV1Trace *trace) {
    if (!language || !trace || trace->rule_index >= language->rewrite_len)
        return NULL;
    return &language->rewrites[trace->rule_index].name;
}

static CettaLdPatternV1 official_variable_term(void) {
    return app1(
        "tptp92-ast:fof-term:alt-2",
        app1("tptp92-ast:variable:alt-1",
             app1("tptp92-ast:token:upper-word", app0("X"))));
}

static CettaLdPatternV1 official_lower_name(const char *lexeme) {
    return app1(
        "tptp92-ast:name:alt-1",
        app1("tptp92-ast:atomic-word:alt-1",
             app1("tptp92-ast:token:lower-word", app0(lexeme))));
}

static CettaLdPatternV1 official_arguments_request(void) {
    return app1(
        "tptp-fof-elab:arguments",
        app1("tptp92-ast:fof-arguments:alt-1",
             official_variable_term()));
}

static CettaLdPatternV1 named_arguments_result(void) {
    return app2(
        "tptp-fof-named:terms-cons",
        app1("tptp-fof-named:term-variable",
             app1("tptp-fof-named:name", app0("X"))),
        app0("tptp-fof-named:terms-nil"));
}

static CettaLdPatternV1 binder_lookup_request(void) {
    return app2(
        "tptp-fof-resolve:lookup", app0("X"),
        app2("tptp-fof-resolve:environment-cons", app0("X"),
             app0("tptp-fof-resolve:environment-nil")));
}

static bool equality_provider_query(
    void *opaque,
    const CettaLdTextV1 *relation,
    const CettaLdPatternV1 *arguments,
    uint32_t argument_len,
    uint32_t row_index,
    const CettaLdPatternV1 **row,
    uint32_t *row_len,
    uint64_t *receipt_id,
    bool *present,
    char *error,
    size_t error_size) {
    EqualityProvider *provider = opaque;
    uint32_t count;
    const CettaLdPatternV1 *decision;
    if (!provider || !row || !row_len || !receipt_id || !present)
        return false;
    *row = NULL;
    *row_len = 0u;
    *receipt_id = 0u;
    *present = false;
    if (provider->mode == PROVIDER_EQUALITY_FAILURE) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size, "%s",
                           "injected relation-provider failure");
        return false;
    }
    if (!text_is(relation, "PatternEqualityDecision") ||
        argument_len != 3u)
        return true;
    count = provider->mode == PROVIDER_EQUALITY_DUPLICATE ? 2u : 1u;
    if (row_index >= count)
        return true;
    if (provider->mode == PROVIDER_EQUALITY_INVENTED) {
        decision = &provider->invented;
    } else if (cetta_ld_cr_v1_pattern_equal(&arguments[0], &arguments[1])) {
        decision = &provider->equal;
    } else {
        decision = &provider->different;
    }
    provider->row[0] = arguments[0];
    provider->row[1] = arguments[1];
    provider->row[2] = *decision;
    *row = provider->row;
    *row_len = 3u;
    *receipt_id = UINT64_C(1000) + row_index;
    *present = true;
    return true;
}

static void equality_provider_init(EqualityProvider *provider) {
    memset(provider, 0, sizeof(*provider));
    provider->equal = app0("pattern-equality-decision:equal");
    provider->different = app0("pattern-equality-decision:different");
    provider->invented = app0("pattern-equality-decision:invented");
}

static void equality_provider_free(EqualityProvider *provider) {
    if (!provider)
        return;
    cetta_ld_pattern_v1_free(&provider->equal);
    cetta_ld_pattern_v1_free(&provider->different);
    cetta_ld_pattern_v1_free(&provider->invented);
    memset(provider, 0, sizeof(*provider));
}

static void all_artifacts_compile_gate(TestCounts *counts) {
    static const char *const paths[] = {
        "langdef/tptp/official_fof_to_named_v1.metta",
        "langdef/tptp/named_fof_to_resolved_v1.metta",
        "langdef/tptp/fof_normalization_v1.metta",
        "langdef/tptp/fof_prenex_normalization_v1.metta",
        "langdef/tptp/fof_skolemization_v1.metta",
        "langdef/tptp/fof_definitional_naming_v1.metta",
        "langdef/tptp/fof_definitional_cnf_generation_v1.metta",
        "langdef/tptp/official_fof_batch_projection_v1.metta",
        "langdef/tptp/fof_clausification_batch_generation_v1.metta",
        "langdef/tptp/fof_cnf_name_allocation_v1.metta",
        "langdef/tptp/fof_cnf_official_ast_v1.metta",
        "langdef/tptp/official_include_directive_v1.metta",
        "langdef/tptp/official_include_resolution_carrier_v1.metta"
    };
    uint32_t index;
    for (index = 0u; index < sizeof(paths) / sizeof(paths[0]); index++) {
        CettaOperationalLanguageDefV1 wire;
        CettaLanguageDefCoreV1 language;
        CettaLdCrV1Program program;
        CettaLdCrV1Status status = CETTA_LD_CR_V1_BAD_ARGUMENT;
        char error[512] = {0};
        cetta_op_lang_v1_init(&wire);
        cetta_language_def_core_v1_init(&language);
        cetta_ld_cr_v1_program_init(&program);
        (void)expect(
            counts,
            load_language(paths[index], &wire, &language,
                          error, sizeof(error)) &&
                cetta_ld_cr_v1_compile(
                    &program, &language, &status,
                    error, sizeof(error)) &&
                status == CETTA_LD_CR_V1_OK &&
                program.language == &language,
            error[0] ? error : "TPTP transformation enters contextual profile");
        cetta_language_def_core_v1_free(&language);
        cetta_op_lang_v1_free(&wire);
    }
}

static void exact_stage_gate(TestCounts *counts,
                             const char *path,
                             CettaLdPatternV1 source,
                             CettaLdPatternV1 expected,
                             const char *expected_rule,
                             const char *load_name,
                             const char *execution_name) {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaLdCrV1Program program;
    CettaLdCrV1Results results;
    CettaLdCrV1Status status = CETTA_LD_CR_V1_BAD_ARGUMENT;
    char error[512] = {0};
    bool loaded;
    bool exact = false;

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_ld_cr_v1_program_init(&program);
    cetta_ld_cr_v1_results_init(&results);
    loaded = load_language(path, &wire, &language, error, sizeof(error)) &&
        cetta_ld_cr_v1_compile(
            &program, &language, &status, error, sizeof(error));
    (void)expect(counts, loaded, error[0] ? error : load_name);
    if (loaded) {
        error[0] = '\0';
        exact = cetta_ld_cr_v1_reducts(
                    &program, NULL, 4u, 200000u, &source,
                    &results, &status, error, sizeof(error)) &&
            results.len == 1u && !results.context_fuel_exhausted &&
            cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                          &expected) &&
            results.items[0].trace &&
            text_is(trace_rule_name(&language, results.items[0].trace),
                    expected_rule);
    }
    (void)expect(counts, exact, error[0] ? error : execution_name);
    cetta_ld_cr_v1_results_free(&results);
    cetta_ld_pattern_v1_free(&expected);
    cetta_ld_pattern_v1_free(&source);
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void remaining_exact_stage_gates(TestCounts *counts) {
    exact_stage_gate(
        counts, "langdef/tptp/official_include_directive_v1.metta",
        app1(
            "tptp-include:decode-name-list",
            app2(
                "tptp92-ast:name-list:alt-2",
                official_lower_name("second"),
                app2(
                    "tptp92-ast:name-list:alt-2",
                    official_lower_name("first"),
                    app1("tptp92-ast:name-list:alt-1",
                         official_lower_name("second"))))),
        app1(
            "tptp-include:decoded-name-list",
            app2(
                "tptp-include:names-cons", app0("second"),
                app2(
                    "tptp-include:names-cons", app0("first"),
                    app1("tptp-include:names-one", app0("second"))))),
        "tptp-include:name-list-cons",
        "load official include-directive contextual program",
        "official include decoding preserves source order and duplicates");

    exact_stage_gate(
        counts, "langdef/tptp/fof_normalization_v1.metta",
        app1("tptp-fof-normalize:positive",
             app0("tptp-fof-resolved:verum")),
        app0("tptp-fof-nnf:verum"),
        "tptp-fof-normalize:positive-verum",
        "load FOF normalization contextual program",
        "FOF normalization executes its Lean-exact verum row");

    exact_stage_gate(
        counts, "langdef/tptp/fof_prenex_normalization_v1.metta",
        app1("tptp-fof-prenex-normalize:request",
             app0("tptp-fof-nnf:verum")),
        app2("tptp-fof-prenex-normalize:result",
             app0("tptp-fof-nnf:verum"),
             app1("tptp-fof-prenex:matrix",
                  app0("tptp-fof-prenex:matrix-verum"))),
        "tptp-fof-prenex-normalize:verum",
        "load FOF prenex normalization contextual program",
        "FOF prenex normalization executes its Lean-exact verum row");

    exact_stage_gate(
        counts, "langdef/tptp/fof_skolemization_v1.metta",
        app2("tptp-fof-skolemize:matrix-request",
             app0("tptp-fof-skolem-term:env-nil"),
             app0("tptp-fof-prenex:matrix-verum")),
        app3("tptp-fof-skolemize:matrix-result",
             app0("tptp-fof-skolem-term:env-nil"),
             app0("tptp-fof-prenex:matrix-verum"),
             app0("tptp-fof-skolem:verum")),
        "tptp-fof-skolemize:matrix-verum",
        "load FOF Skolemization contextual program",
        "FOF Skolemization executes its Lean-exact matrix-verum row");

    exact_stage_gate(
        counts, "langdef/tptp/fof_definitional_naming_v1.metta",
        app2("tptp-fof-name:variables-request",
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-resolved:index-zero")),
        app3("tptp-fof-name:variables-result",
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-skolem:terms-nil")),
        "tptp-fof-name:variables-zero",
        "load FOF definitional naming contextual program",
        "FOF definitional naming executes its Lean-exact zero-variable row");

    exact_stage_gate(
        counts, "langdef/tptp/fof_definitional_cnf_generation_v1.metta",
        app2("tptp-fof-cnf-gen:variables-request",
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-resolved:index-zero")),
        app3("tptp-fof-cnf-gen:variables-result",
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-skolem:terms-nil")),
        "tptp-fof-cnf-gen:variables-zero",
        "load FOF definitional CNF contextual program",
        "FOF definitional CNF executes its Lean-exact zero-variable row");

    exact_stage_gate(
        counts, "langdef/tptp/official_fof_batch_projection_v1.metta",
        app3(
            "tptp-fof-batch-project:request",
            app3(
                "tptp-semantic:fof-input",
                app2(
                    "tptp-semantic:occurrence-id",
                    app1("tptp-semantic:source-digest", app0("digest-canary")),
                    app0("index-canary")),
                app4(
                    "tptp92-ast:fof-annotated:alt-1",
                    app0("name-canary"),
                    app1(
                        "tptp92-ast:formula-role:alt-1",
                        app1("tptp92-ast:token:lower-word", app0("axiom"))),
                    app0("formula-canary"), app0("annotations-canary")),
                app0("span-canary")),
            app0("skolem-canary"), app0("cnf-canary")),
        app4(
            "tptp-fof-batch-gen:request",
            app2("tptp-fof-batch:occurrence", app0("digest-canary"),
                 app0("index-canary")),
            app0("tptp-fof-batch:positive"), app0("skolem-canary"),
            app0("cnf-canary")),
        "tptp-fof-batch-project:axiom:plain",
        "load official FOF batch projection contextual program",
        "official FOF batch projection preserves source occurrence and axiom polarity");

    exact_stage_gate(
        counts, "langdef/tptp/fof_clausification_batch_generation_v1.metta",
        app3("tptp-fof-batch-gen:index-entries",
             app0("tptp-fof-batch:occurrence-canary"),
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-cnf:clauses-nil")),
        app0("tptp-fof-batch:entries-nil"),
        "tptp-fof-batch-gen:entries-nil",
        "load FOF clausification batch contextual program",
        "FOF clausification batch executes its Lean-exact empty row");

    exact_stage_gate(
        counts, "langdef/tptp/fof_cnf_name_allocation_v1.metta",
        app2("tptp-fof-cnf-name-allocation:entries",
             app0("tptp-fof-batch:entries-nil"),
             app0("tptp-fof-resolved:index-zero")),
        app2("tptp-fof-cnf-allocated:allocation-result",
             app0("tptp-fof-resolved:index-zero"),
             app0("tptp-fof-cnf-allocated:entries-nil")),
        "tptp-fof-cnf-name-allocation:entries-nil",
        "load FOF CNF name allocation contextual program",
        "FOF CNF name allocation executes its Lean-exact empty row");

    exact_stage_gate(
        counts, "langdef/tptp/fof_cnf_official_ast_v1.metta",
        app1("tptp-cnf-official-serialization:terms",
             app0("tptp-fof-skolem:terms-nil")),
        app0("tptp-cnf-official-serialization:rendered-terms-nil"),
        "tptp-cnf-official-serialization:terms-nil",
        "load FOF CNF official serialization contextual program",
        "FOF CNF official serialization executes its Lean-exact empty terms row");
}

static void congruence_stage_gate(TestCounts *counts) {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaLdCrV1Program program;
    CettaLdCrV1Results results;
    CettaLdCrV1Status status = CETTA_LD_CR_V1_BAD_ARGUMENT;
    CettaLdPatternV1 source = official_arguments_request();
    CettaLdPatternV1 expected = named_arguments_result();
    CettaLdPatternV1 mutant = app1("tptp-fof-elab:invented", app0("X"));
    char error[512] = {0};
    bool loaded;

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_ld_cr_v1_program_init(&program);
    cetta_ld_cr_v1_results_init(&results);
    loaded = load_language(
        "langdef/tptp/official_fof_to_named_v1.metta",
        &wire, &language, error, sizeof(error)) &&
        cetta_ld_cr_v1_compile(
            &program, &language, &status, error, sizeof(error));
    (void)expect(counts, loaded, error[0] ? error :
                 "load official-to-named contextual program");
    if (loaded) {
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, NULL, 2u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                results.len == 1u &&
                !results.context_fuel_exhausted &&
                cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                              &expected),
            error[0] ? error : "congruence stage has one exact reduct");
        (void)expect(
            counts,
            results.len == 1u && results.items[0].trace &&
                text_is(trace_rule_name(&language, results.items[0].trace),
                        "tptp-fof-elab:arguments-one") &&
                results.items[0].trace->premise_len == 1u &&
                results.items[0].trace->premises[0].kind ==
                    CETTA_LD_PREMISE_CONGRUENCE_V1 &&
                results.items[0].trace->premises[0].as.congruence.step &&
                text_is(trace_rule_name(
                            &language,
                            results.items[0].trace->premises[0]
                                .as.congruence.step),
                        "tptp-fof-elab:term-variable"),
            "congruence receipt retains both source rule occurrences");

        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, NULL, 1u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                results.len == 0u && results.context_fuel_exhausted,
            "insufficient contextual fuel is observable and yields no reduct");
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, NULL, 2u, 200000u, &mutant,
                &results, &status, error, sizeof(error)) &&
                results.len == 0u && !results.context_fuel_exhausted,
            "invented request root has no declared reduct");

        error[0] = '\0';
        (void)cetta_ld_cr_v1_reducts(
            &program, NULL, 2u, 200000u, &source,
            &results, &status, error, sizeof(error));
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_ld_cr_v1_reducts(
                &program, NULL, 2u, 1u, &source,
                &results, &status, error, sizeof(error)) &&
                status == CETTA_LD_CR_V1_WORK_LIMIT &&
                results.len == 1u &&
                cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                              &expected),
            "work exhaustion is distinct and leaves prior results atomic");
    }
    cetta_ld_cr_v1_results_free(&results);
    cetta_ld_pattern_v1_free(&mutant);
    cetta_ld_pattern_v1_free(&expected);
    cetta_ld_pattern_v1_free(&source);
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void relation_stage_gate(TestCounts *counts) {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaLdCrV1Program program;
    CettaLdCrV1Results results;
    CettaLdCrV1Status status = CETTA_LD_CR_V1_BAD_ARGUMENT;
    CettaLdPatternV1 source = binder_lookup_request();
    CettaLdPatternV1 expected = app0("tptp-fof-resolved:index-zero");
    EqualityProvider equality;
    CettaLdCrV1RelationProvider provider;
    char error[512] = {0};
    bool loaded;

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_ld_cr_v1_program_init(&program);
    cetta_ld_cr_v1_results_init(&results);
    equality_provider_init(&equality);
    provider.context = &equality;
    provider.query = equality_provider_query;
    loaded = load_language(
        "langdef/tptp/named_fof_to_resolved_v1.metta",
        &wire, &language, error, sizeof(error)) &&
        cetta_ld_cr_v1_compile(
            &program, &language, &status, error, sizeof(error));
    (void)expect(counts, loaded, error[0] ? error :
                 "load named-to-resolved contextual program");
    if (loaded) {
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, &provider, 2u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                results.len == 1u &&
                cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                              &expected),
            error[0] ? error : "relation-query stage has one exact reduct");
        (void)expect(
            counts,
            results.len == 1u && results.items[0].trace &&
                text_is(trace_rule_name(&language, results.items[0].trace),
                        "tptp-fof-resolve:lookup-cons") &&
                results.items[0].trace->premise_len == 2u &&
                results.items[0].trace->premises[0].kind ==
                    CETTA_LD_PREMISE_RELATION_QUERY_V1 &&
                results.items[0].trace->premises[0]
                        .as.relation_query.source ==
                    CETTA_LD_CR_V1_RELATION_EXTERNAL &&
                results.items[0].trace->premises[0]
                        .as.relation_query.row_index == 0u &&
                results.items[0].trace->premises[0]
                        .as.relation_query.receipt_id == UINT64_C(1000) &&
                results.items[0].trace->premises[1].kind ==
                    CETTA_LD_PREMISE_CONGRUENCE_V1 &&
                text_is(trace_rule_name(
                            &language,
                            results.items[0].trace->premises[1]
                                .as.congruence.step),
                        "tptp-fof-resolve:lookup-equal"),
            "relation receipt records provider row and continuation rule");

        equality.mode = PROVIDER_EQUALITY_DUPLICATE;
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, &provider, 2u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                results.len == 2u &&
                cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                              &expected) &&
                cetta_ld_cr_v1_pattern_equal(&results.items[1].term,
                                              &expected) &&
                results.items[0].trace->premises[0]
                        .as.relation_query.receipt_id == UINT64_C(1000) &&
                results.items[1].trace->premises[0]
                        .as.relation_query.receipt_id == UINT64_C(1001),
            "duplicate provider rows preserve result multiplicity and identity");

        equality.mode = PROVIDER_EQUALITY_NORMAL;
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, NULL, 2u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                results.len == 0u && !results.context_fuel_exhausted,
            "absent semantic provider fails closed without a runtime fault");

        equality.mode = PROVIDER_EQUALITY_INVENTED;
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, &provider, 2u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                results.len == 0u,
            "invented provider decision cannot fabricate a declared reduct");

        equality.mode = PROVIDER_EQUALITY_NORMAL;
        error[0] = '\0';
        (void)cetta_ld_cr_v1_reducts(
            &program, &provider, 2u, 200000u, &source,
            &results, &status, error, sizeof(error));
        equality.mode = PROVIDER_EQUALITY_FAILURE;
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_ld_cr_v1_reducts(
                &program, &provider, 2u, 200000u, &source,
                &results, &status, error, sizeof(error)) &&
                status == CETTA_LD_CR_V1_PROVIDER_FAILURE &&
                results.len == 1u &&
                cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                              &expected),
            "provider fault is distinct and leaves the prior result atomic");
    }
    cetta_ld_cr_v1_results_free(&results);
    equality_provider_free(&equality);
    cetta_ld_pattern_v1_free(&expected);
    cetta_ld_pattern_v1_free(&source);
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void builtin_equality_gate(TestCounts *counts) {
    static const char source_language[] =
        "(GSLTLanguageDefWireV1 \"BuiltinEqualityCanary\" "
        "(LCons (TypeDecl \"Value\" CarrierAst) LNil) LNil LNil "
        "(LCons (RewriteRule \"builtin-equality\" "
        "(LCons (TypeBinding \"left\" (TBase \"Value\")) "
        "(LCons (TypeBinding \"right\" (TBase \"Value\")) LNil)) "
        "(LCons (RelationQuery \"eq\" "
        "(LCons (FVar \"left\") (LCons (FVar \"right\") LNil))) LNil) "
        "(PApp \"eq-request\" "
        "(LCons (FVar \"left\") (LCons (FVar \"right\") LNil))) "
        "(PApp \"eq-result\" LNil)) LNil))";
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaLdCrV1Program program;
    CettaLdCrV1Results results;
    CettaLdCrV1Status status = CETTA_LD_CR_V1_BAD_ARGUMENT;
    CettaLdPatternV1 equal_source =
        app2("eq-request", app0("same"), app0("same"));
    CettaLdPatternV1 unequal_source =
        app2("eq-request", app0("left"), app0("right"));
    CettaLdPatternV1 expected = app0("eq-result");
    char error[512] = {0};
    bool loaded;

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_ld_cr_v1_program_init(&program);
    cetta_ld_cr_v1_results_init(&results);
    loaded = load_language_bytes(
        source_language, &wire, &language, error, sizeof(error)) &&
        cetta_ld_cr_v1_compile(
            &program, &language, &status, error, sizeof(error));
    (void)expect(counts, loaded, error[0] ? error :
                 "load built-in equality contextual program");
    if (loaded) {
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, NULL, 1u, 200000u, &equal_source,
                &results, &status, error, sizeof(error)) &&
                results.len == 2u &&
                cetta_ld_cr_v1_pattern_equal(&results.items[0].term,
                                              &expected) &&
                cetta_ld_cr_v1_pattern_equal(&results.items[1].term,
                                              &expected) &&
                results.items[0].trace->premises[0]
                        .as.relation_query.source ==
                    CETTA_LD_CR_V1_RELATION_BUILTIN &&
                results.items[0].trace->premises[0]
                        .as.relation_query.row_index == 0u &&
                results.items[1].trace->premises[0]
                        .as.relation_query.row_index == 1u,
            error[0] ? error :
                "built-in equality preserves duplicate tuple multiplicity");

        error[0] = '\0';
        (void)expect(
            counts,
            cetta_ld_cr_v1_reducts(
                &program, NULL, 1u, 200000u, &unequal_source,
                &results, &status, error, sizeof(error)) &&
                results.len == 0u && !results.context_fuel_exhausted,
            error[0] ? error :
                "built-in equality rejects unequal ground arguments");
    }
    cetta_ld_cr_v1_results_free(&results);
    cetta_ld_pattern_v1_free(&expected);
    cetta_ld_pattern_v1_free(&unequal_source);
    cetta_ld_pattern_v1_free(&equal_source);
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

static void profile_mutation_gate(TestCounts *counts) {
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaLdCrV1Program program;
    CettaLdCrV1Status status = CETTA_LD_CR_V1_OK;
    CettaLdPatternKindV1 saved_kind;
    char error[512] = {0};
    bool loaded;

    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_ld_cr_v1_program_init(&program);
    loaded = load_language(
        "langdef/tptp/fof_normalization_v1.metta",
        &wire, &language, error, sizeof(error));
    (void)expect(counts, loaded && language.rewrite_len > 0u,
                 error[0] ? error : "load mutation fixture");
    if (loaded && language.rewrite_len > 0u) {
        saved_kind = language.rewrites[0].left.kind;
        language.rewrites[0].left.kind = CETTA_LD_PATTERN_COLLECTION_V1;
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_ld_cr_v1_compile(
                &program, &language, &status, error, sizeof(error)) &&
                status == CETTA_LD_CR_V1_UNSUPPORTED_PROFILE &&
                program.language == NULL,
            "unsupported Pattern mutation is rejected rather than interpreted");
        language.rewrites[0].left.kind = saved_kind;
    }
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
}

int main(void) {
    TestCounts counts = {0};
    all_artifacts_compile_gate(&counts);
    remaining_exact_stage_gates(&counts);
    congruence_stage_gate(&counts);
    relation_stage_gate(&counts);
    builtin_equality_gate(&counts);
    profile_mutation_gate(&counts);
    printf("(LanguageDefContextualRunnerV1Summary %u %u %u)\n",
           counts.passed + counts.failed, counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
