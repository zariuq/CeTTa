#include "native/language_def_ground_term_v1.h"

#include "src/atom.h"
#include "src/symbol.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

typedef struct {
    Arena arena;
} TestAtomPool;

static bool expect(TestCounts *counts, bool condition, const char *name) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

static void pool_init(TestAtomPool *pool) {
    arena_init(&pool->arena);
}

static void pool_free(TestAtomPool *pool) {
    arena_free(&pool->arena);
}

static Atom *pool_symbol(TestAtomPool *pool, const char *name) {
    return pool && name ? atom_symbol(&pool->arena, name) : NULL;
}

static Atom *pool_string(TestAtomPool *pool, const char *value) {
    return pool && value ? atom_string(&pool->arena, value) : NULL;
}

static Atom *pool_int(TestAtomPool *pool, int64_t value) {
    return pool ? atom_int(&pool->arena, value) : NULL;
}

static Atom *pool_var(TestAtomPool *pool, const char *name) {
    return pool && name ? atom_var_with_id(&pool->arena, name, 1u) : NULL;
}

static Atom *pool_expr(TestAtomPool *pool, const char *head,
                       Atom **arguments, uint32_t argument_len) {
    Atom *elements[64];
    uint32_t index;

    if (!pool || !head || argument_len >= 64u) {
        return NULL;
    }
    elements[0] = pool_symbol(pool, head);
    if (!elements[0])
        return NULL;
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    return atom_expr(
        &pool->arena, elements, (CettaExprLen)argument_len + 1u);
}

static Atom *pool_nullary(TestAtomPool *pool, const char *head) {
    return pool_expr(pool, head, NULL, 0u);
}

static Atom *make_empty_problem(TestAtomPool *pool, Atom *digest_value) {
    Atom *source_arguments[1] = {digest_value};
    Atom *source = pool_expr(
        pool, "fo-cnf:source-digest", source_arguments, 1u);
    Atom *clauses = pool_nullary(pool, "fo-cnf:clauses-nil");
    Atom *problem_arguments[2] = {source, clauses};

    return pool_expr(pool, "fo-cnf:problem", problem_arguments, 2u);
}

static Atom *make_scoped_variable_term(TestAtomPool *pool) {
    Atom *source_arguments[1] = {pool_string(pool, "sha256:source")};
    Atom *source = pool_expr(
        pool, "fo-cnf:source-digest", source_arguments, 1u);
    Atom *occurrence_arguments[2] = {source, pool_int(pool, 7)};
    Atom *occurrence = pool_expr(
        pool, "fo-cnf:occurrence", occurrence_arguments, 2u);
    Atom *name_arguments[1] = {pool_string(pool, "X")};
    Atom *name = pool_expr(
        pool, "fo-cnf:variable-name", name_arguments, 1u);
    Atom *identifier_arguments[2] = {occurrence, name};
    Atom *identifier = pool_expr(
        pool, "fo-cnf:variable-id", identifier_arguments, 2u);
    Atom *term_arguments[1] = {identifier};

    return pool_expr(pool, "fo-cnf:term-variable", term_arguments, 1u);
}

static Atom *make_empty_resolution_problem(TestAtomPool *pool) {
    Atom *source_arguments[1] = {pool_string(pool, "sha256:source")};
    Atom *source = pool_expr(
        pool, "fo-resolution:source-digest", source_arguments, 1u);
    Atom *clauses = pool_nullary(pool, "fo-resolution:clauses-nil");
    Atom *problem_arguments[2] = {source, clauses};

    return pool_expr(
        pool, "fo-resolution:problem", problem_arguments, 2u);
}

static Atom *make_resolution_variable_id(TestAtomPool *pool,
                                         int64_t occurrence_index) {
    Atom *source_arguments[1] = {pool_string(pool, "sha256:source")};
    Atom *source = pool_expr(
        pool, "fo-resolution:source-digest", source_arguments, 1u);
    Atom *occurrence_arguments[2] = {
        source, pool_int(pool, occurrence_index)};
    Atom *occurrence = pool_expr(
        pool, "fo-resolution:occurrence", occurrence_arguments, 2u);
    Atom *name_arguments[1] = {pool_string(pool, "X")};
    Atom *name = pool_expr(
        pool, "fo-resolution:variable-name", name_arguments, 1u);
    Atom *identifier_arguments[2] = {occurrence, name};

    return pool_expr(
        pool, "fo-resolution:variable-id", identifier_arguments, 2u);
}

static bool pattern_apply_has_head(const CettaLdPatternV1 *pattern,
                                   const char *head,
                                   uint32_t arity) {
    size_t head_len = head ? strlen(head) : 0u;

    return pattern && head && head_len <= UINT32_MAX &&
        pattern->kind == CETTA_LD_PATTERN_APPLY_V1 &&
        pattern->as.apply.head.len == (uint32_t)head_len &&
        (head_len == 0u ||
         (pattern->as.apply.head.bytes &&
          memcmp(pattern->as.apply.head.bytes, head, head_len) == 0)) &&
        pattern->as.apply.arguments.len == arity &&
        (arity == 0u || pattern->as.apply.arguments.items);
}

static const CettaLdPatternV1 *pattern_argument(
    const CettaLdPatternV1 *pattern, uint32_t index) {
    if (!pattern || pattern->kind != CETTA_LD_PATTERN_APPLY_V1 ||
        index >= pattern->as.apply.arguments.len ||
        !pattern->as.apply.arguments.items)
        return NULL;
    return &pattern->as.apply.arguments.items[index];
}

static void typed_pattern_codec_gates(
    TestCounts *counts, CettaLanguageDefCoreV1 *language) {
    TestAtomPool source_pool;
    TestAtomPool result_pool;
    CettaLdGroundTermV1Status status = CETTA_LD_GROUND_TERM_V1_OK;
    CettaLdPatternV1 encoded;
    CettaLdPatternV1 integer_pattern;
    CettaLdPatternV1 noncanonical = {
        .kind = CETTA_LD_PATTERN_APPLY_V1,
        .as.apply = {
            .head = {(uint8_t *)(uintptr_t)"04", 2u},
            .arguments = {NULL, 0u},
        },
    };
    CettaLdPatternV1 free_variable = {
        .kind = CETTA_LD_PATTERN_FVAR_V1,
        .as.fvar = {(uint8_t *)(uintptr_t)"X", 1u},
    };
    char error[512] = {0};
    Atom *source;
    Atom *roundtrip;
    Atom *minimum;
    Atom *minimum_roundtrip;
    const CettaLdPatternV1 *variable_id;
    const CettaLdPatternV1 *occurrence;
    const CettaLdPatternV1 *variable_name;
    CettaLdTypeDeclV1 *string_type = NULL;
    CettaLdCarrierKindV1 saved_string_carrier = CETTA_LD_CARRIER_AST_V1;

    pool_init(&source_pool);
    pool_init(&result_pool);
    cetta_ld_pattern_v1_init(&encoded);
    cetta_ld_pattern_v1_init(&integer_pattern);
    source = make_scoped_variable_term(&source_pool);

    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_supports_pattern_codec(
            language, "Integer", 20000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error :
            "declared integer type is in the typed Pattern codec profile");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_supports_pattern_codec(
            language, "InventedType", 20000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE,
        "unknown result category is rejected before execution");
    for (uint32_t index = 0u; index < language->type_len; index++) {
        CettaLdTextV1 *name = &language->types[index].name;
        if (name->len == 6u && name->bytes &&
            memcmp(name->bytes, "String", 6u) == 0) {
            string_type = &language->types[index];
            saved_string_carrier = string_type->carrier;
            string_type->carrier = CETTA_LD_CARRIER_BUILTIN_BOOL_V1;
            break;
        }
    }
    error[0] = '\0';
    (void)expect(
        counts,
        string_type &&
            !cetta_language_def_ground_term_v1_supports_pattern_codec(
                language, "Problem", 20000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "typed Pattern codec preflight checks the reachable type graph");
    if (string_type)
        string_type->carrier = saved_string_carrier;
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_to_pattern(
            language, "Term", source, &encoded, 128u, 20000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK &&
            pattern_apply_has_head(
                &encoded, "fo-cnf:term-variable", 1u),
        error[0] ? error :
            "typed codec maps an admitted constructor term to PApp");
    variable_id = pattern_argument(&encoded, 0u);
    occurrence = pattern_argument(variable_id, 0u);
    variable_name = pattern_argument(variable_id, 1u);
    (void)expect(
        counts,
        pattern_apply_has_head(
            variable_id, "fo-cnf:variable-id", 2u) &&
            pattern_apply_has_head(
                occurrence, "fo-cnf:occurrence", 2u) &&
            pattern_apply_has_head(
                pattern_argument(occurrence, 1u), "7", 0u) &&
            pattern_apply_has_head(
                variable_name, "fo-cnf:variable-name", 1u) &&
            pattern_apply_has_head(
                pattern_argument(variable_name, 0u), "X", 0u),
        "typed codec preserves constructor structure and builtin leaves");
    error[0] = '\0';
    roundtrip = cetta_language_def_ground_term_v1_from_pattern(
        &result_pool.arena, language, "Term", &encoded,
        128u, 20000u, &status, error, sizeof(error));
    (void)expect(
        counts,
        roundtrip && atom_eq(source, roundtrip) &&
            cetta_language_def_ground_term_v1_admit(
                language, "Term", roundtrip, 128u, 20000u,
                &status, error, sizeof(error)),
        error[0] ? error :
            "typed constructor/string/integer Pattern roundtrip is exact");

    minimum = pool_int(&source_pool, INT64_MIN);
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_to_pattern(
            language, "Integer", minimum, &integer_pattern,
            16u, 20000u, &status, error, sizeof(error)) &&
            pattern_apply_has_head(
                &integer_pattern, "-9223372036854775808", 0u),
        error[0] ? error :
            "minimum int64 has canonical decimal Pattern spelling");
    error[0] = '\0';
    minimum_roundtrip = cetta_language_def_ground_term_v1_from_pattern(
        &result_pool.arena, language, "Integer", &integer_pattern,
        16u, 20000u, &status, error, sizeof(error));
    (void)expect(
        counts,
        minimum_roundtrip && atom_eq(minimum, minimum_roundtrip),
        error[0] ? error :
            "canonical minimum int64 Pattern roundtrips exactly");

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_from_pattern(
            &result_pool.arena, language, "Integer", &noncanonical,
            16u, 20000u, &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
        "noncanonical integer spelling fails closed");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_from_pattern(
            &result_pool.arena, language, "String", &free_variable,
            16u, 20000u, &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
        "free Pattern variable cannot become a grounded string");

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_to_pattern(
            language, "Problem", source, &encoded,
            128u, 20000u, &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH &&
            pattern_apply_has_head(
                &encoded, "fo-cnf:term-variable", 1u),
        "failed typed conversion leaves the previous Pattern unchanged");

    cetta_ld_pattern_v1_free(&integer_pattern);
    cetta_ld_pattern_v1_free(&encoded);
    pool_free(&result_pool);
    pool_free(&source_pool);
}

static bool load_language_file(const char *path,
                               CettaOperationalLanguageDefV1 *wire,
                               CettaLanguageDefCoreV1 *language,
                               char *error,
                               size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;

    return cetta_op_lang_v1_parse_file(
               wire,
               path,
               4000000u, 8000000u, &wire_status,
               error, error_size) &&
        cetta_language_def_core_v1_decode(
            language, wire, 200000u, &core_status,
            error, error_size);
}

static Atom *make_syntax_tree(TestAtomPool *pool,
                              const char *label_name,
                              bool label_has_argument) {
    Atom *label;
    Atom *payload = pool_nullary(pool, "tptp-cst:nil");
    Atom *node_arguments[2];

    if (label_has_argument) {
        Atom *arguments[1] = {pool_string(pool, "invented-payload")};
        label = pool_expr(pool, label_name, arguments, 1u);
    } else {
        label = pool_nullary(pool, label_name);
    }
    node_arguments[0] = label;
    node_arguments[1] = payload;
    return pool_expr(pool, "tptp-cst:node", node_arguments, 2u);
}

static Atom *make_deep_syntax_tree(TestAtomPool *pool, uint32_t item_count) {
    Atom *cursor = pool_nullary(pool, "tptp-cst:nil");
    Atom *item = pool_nullary(pool, "tptp-cst:nil");
    Atom *label = pool_nullary(pool, "tptp-cst:label-tptp-file");

    if (!cursor || !item || !label)
        return NULL;
    for (uint32_t index = 0u; index < item_count; index++) {
        Atom *arguments[2] = {item, cursor};
        cursor = pool_expr(pool, "tptp-cst:cons", arguments, 2u);
        if (!cursor)
            return NULL;
    }
    {
        Atom *arguments[2] = {label, cursor};
        return pool_expr(pool, "tptp-cst:node", arguments, 2u);
    }
}

static void syntax_tree_language_gates(
    TestCounts *counts,
    CettaLanguageDefCoreV1 *language) {
    TestAtomPool pool;
    CettaLdGroundTermV1Status status = CETTA_LD_GROUND_TERM_V1_OK;
    char error[512] = {0};
    pool_init(&pool);
    Atom *tree = make_syntax_tree(
        &pool, "tptp-cst:label-tptp-file", false);
    Atom *invented = make_syntax_tree(
        &pool, "tptp-cst:label-invented", false);
    Atom *wrong_arity = make_syntax_tree(
        &pool, "tptp-cst:label-tptp-file", true);
    Atom *deep_tree = make_deep_syntax_tree(&pool, 5000u);
    Atom *label = pool_nullary(&pool, "tptp-cst:label-tptp-file");
    uint32_t original_term_len = language->term_len;

    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_admit(
            language, "SyntaxTree", tree, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error : "closed TPTP syntax tree inhabits SyntaxTree");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "SyntaxTree", invented, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
        "invented TPTP node label is rejected");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "SyntaxTree", wrong_arity, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH,
        "nullary TPTP node labels have exact arity");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "SyntaxTree", label, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
        "a NodeLabel cannot be published as a SyntaxTree");
    error[0] = '\0';
    (void)expect(
        counts,
        deep_tree &&
            !cetta_language_def_ground_term_v1_admit(
                language, "SyntaxTree", deep_tree, 128u, 1000000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "an explicit caller depth bound still rejects a deeper tree");
    error[0] = '\0';
    (void)expect(
        counts,
        deep_tree &&
            cetta_language_def_ground_term_v1_admit(
                language, "SyntaxTree", deep_tree, UINT32_MAX, 1000000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error :
            "iterative admission accepts a long finite constructor list");

    /* The syntax-tree nil constructor is the final authored declaration. */
    if (language->term_len > 0u)
        language->term_len--;
    error[0] = '\0';
    (void)expect(
        counts,
        original_term_len > 0u &&
            !cetta_language_def_ground_term_v1_admit(
                language, "SyntaxTree", tree, 128u, 10000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
        "deleting a source constructor makes the same tree inadmissible");
    language->term_len = original_term_len;
    pool_free(&pool);
}

static void positive_and_type_gates(TestCounts *counts,
                                    CettaLanguageDefCoreV1 *language) {
    TestAtomPool pool;
    CettaLdGroundTermV1Status status =
        CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT;
    char error[512] = {0};
    pool_init(&pool);
    Atom *problem = make_empty_problem(
        &pool, pool_string(&pool, "sha256:source"));
    Atom *variable_term = make_scoped_variable_term(&pool);
    Atom *wrong_problem = make_empty_problem(&pool, pool_int(&pool, 4));
    Atom *term_as_problem = variable_term;

    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_admit(
            language, "Problem", problem, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error : "empty problem inhabits Problem");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_admit(
            language, "Term", variable_term, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error : "explicitly scoped variable is ground clause data");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", wrong_problem, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
        "grounded integer cannot inhabit String");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", term_as_problem, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
        "a Term constructor cannot be published as Problem");
    pool_free(&pool);
}

static void malformed_and_mutation_gates(TestCounts *counts,
                                         CettaLanguageDefCoreV1 *language) {
    TestAtomPool pool;
    CettaLdGroundTermV1Status status = CETTA_LD_GROUND_TERM_V1_OK;
    char error[512] = {0};
    pool_init(&pool);
    Atom *problem = make_empty_problem(
        &pool, pool_string(&pool, "sha256:source"));
    Atom *source_arguments[1] = {pool_string(&pool, "sha256:source")};
    Atom *source = pool_expr(
        &pool, "fo-cnf:source-digest", source_arguments, 1u);
    Atom *short_arguments[1] = {source};
    Atom *short_problem = pool_expr(
        &pool, "fo-cnf:problem", short_arguments, 1u);
    Atom *unknown_arguments[2] = {
        source, pool_nullary(&pool, "fo-cnf:clauses-nil")};
    Atom *unknown = pool_expr(
        &pool, "fo-cnf:problem-invented", unknown_arguments, 2u);
    Atom *variable = pool_var(&pool, "P");
    uint32_t original_term_len = language->term_len;
    CettaLdTextV1 saved_second_type = {0};
    CettaLdTermParamV1 *saved_params = NULL;
    uint32_t parameterized_rule = UINT32_MAX;

    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", short_problem, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH,
        "constructor arity is exact");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", unknown, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
        "invented constructor is rejected");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", variable, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
        "a host-language variable cannot replace explicit clause data");

    if (language->type_len > 1u) {
        saved_second_type = language->types[1].name;
        language->types[1].name = language->types[0].name;
    }
    error[0] = '\0';
    (void)expect(
        counts,
        language->type_len > 1u &&
            !cetta_language_def_ground_term_v1_admit(
                language, "Problem", problem, 128u, 10000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE,
        "duplicate LanguageDef type declarations fail closed");
    if (language->type_len > 1u)
        language->types[1].name = saved_second_type;

    for (uint32_t index = 0u; index < language->term_len; index++) {
        if (language->terms[index].param_len > 0u) {
            parameterized_rule = index;
            saved_params = language->terms[index].params;
            language->terms[index].params = NULL;
            break;
        }
    }
    error[0] = '\0';
    (void)expect(
        counts,
        parameterized_rule != UINT32_MAX &&
            !cetta_language_def_ground_term_v1_admit(
                language, "Problem", problem, 128u, 10000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
        "missing constructor parameter storage fails closed");
    if (parameterized_rule != UINT32_MAX)
        language->terms[parameterized_rule].params = saved_params;

    /* The problem constructor is the final authored term declaration. */
    if (language->term_len > 0u)
        language->term_len--;
    error[0] = '\0';
    (void)expect(
        counts,
        original_term_len > 0u &&
            !cetta_language_def_ground_term_v1_admit(
                language, "Problem", problem, 128u, 10000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
        "deleting the target constructor makes the same output inadmissible");
    language->term_len = original_term_len;
    pool_free(&pool);
}

static void resource_gates(TestCounts *counts,
                           CettaLanguageDefCoreV1 *language) {
    TestAtomPool pool;
    CettaLdGroundTermV1Status status = CETTA_LD_GROUND_TERM_V1_OK;
    char error[512] = {0};
    pool_init(&pool);
    Atom *problem = make_empty_problem(
        &pool, pool_string(&pool, "sha256:source"));

    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", problem, 128u, 1u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "work exhaustion is explicit");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", problem, 0u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "depth exhaustion is explicit");
    pool_free(&pool);
}

static void resolution_input_language_gates(
    TestCounts *counts, CettaLanguageDefCoreV1 *language) {
    TestAtomPool pool;
    CettaLdGroundTermV1Status status = CETTA_LD_GROUND_TERM_V1_OK;
    char error[512] = {0};
    pool_init(&pool);
    Atom *problem = make_empty_resolution_problem(&pool);
    Atom *variable_id = make_resolution_variable_id(&pool, 7);
    Atom *term_arguments[1] = {variable_id};
    Atom *term = pool_expr(
        &pool, "fo-resolution:term-variable", term_arguments, 1u);
    Atom *wrong_source_arguments[1] = {pool_int(&pool, 9)};
    Atom *wrong_source = pool_expr(
        &pool, "fo-resolution:source-digest",
        wrong_source_arguments, 1u);
    Atom *wrong_problem_arguments[2] = {
        wrong_source, pool_nullary(&pool, "fo-resolution:clauses-nil")};
    Atom *wrong_problem = pool_expr(
        &pool, "fo-resolution:problem", wrong_problem_arguments, 2u);
    Atom *host_variable = pool_var(&pool, "X");
    uint32_t original_term_len = language->term_len;

    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_admit(
            language, "Problem", problem, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error : "empty resolution problem inhabits Problem");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_admit(
            language, "Term", term, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK,
        error[0] ? error :
            "scoped object variable inhabits resolution Term");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Problem", wrong_problem, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
        "resolution source digest rejects an integer payload");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_ground_term_v1_admit(
            language, "Term", host_variable, 128u, 10000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
        "evaluator variable cannot replace scoped resolution data");

    /* The problem constructor is the final authored declaration. */
    if (language->term_len > 0u)
        language->term_len--;
    error[0] = '\0';
    (void)expect(
        counts,
        original_term_len > 0u &&
            !cetta_language_def_ground_term_v1_admit(
                language, "Problem", problem, 128u, 10000u,
                &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
        "deleting resolution:problem makes the same output inadmissible");
    language->term_len = original_term_len;
    pool_free(&pool);
}

int main(void) {
    TestCounts counts = {0};
    SymbolTable symbols;
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOperationalLanguageDefV1 syntax_wire;
    CettaLanguageDefCoreV1 syntax_language;
    CettaOperationalLanguageDefV1 resolution_wire;
    CettaLanguageDefCoreV1 resolution_language;
    char error[512] = {0};

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_op_lang_v1_init(&syntax_wire);
    cetta_language_def_core_v1_init(&syntax_language);
    cetta_op_lang_v1_init(&resolution_wire);
    cetta_language_def_core_v1_init(&resolution_language);
    if (!load_language_file(
            "langdef/logic/first_order_clause_data_v1.metta",
            &wire, &language, error, sizeof(error))) {
        fprintf(stderr, "FAIL: %s\n",
                error[0] ? error : "load FirstOrderClauseData");
        cetta_language_def_core_v1_free(&language);
        cetta_op_lang_v1_free(&wire);
        g_symbols = NULL;
        symbol_table_free(&symbols);
        return 1;
    }
    if (!load_language_file(
            "langdef/tptp/fof_cnf_syntax_tree_v1.metta",
            &syntax_wire, &syntax_language, error, sizeof(error))) {
        fprintf(stderr, "FAIL: %s\n",
                error[0] ? error : "load TptpFofCnfSyntaxTree");
        cetta_language_def_core_v1_free(&syntax_language);
        cetta_op_lang_v1_free(&syntax_wire);
        cetta_language_def_core_v1_free(&language);
        cetta_op_lang_v1_free(&wire);
        g_symbols = NULL;
        symbol_table_free(&symbols);
        return 1;
    }
    if (!load_language_file(
            "langdef/logic/first_order_resolution_input_v1.metta",
            &resolution_wire, &resolution_language,
            error, sizeof(error))) {
        fprintf(stderr, "FAIL: %s\n",
                error[0] ? error : "load FirstOrderResolutionInput");
        cetta_language_def_core_v1_free(&resolution_language);
        cetta_op_lang_v1_free(&resolution_wire);
        cetta_language_def_core_v1_free(&syntax_language);
        cetta_op_lang_v1_free(&syntax_wire);
        cetta_language_def_core_v1_free(&language);
        cetta_op_lang_v1_free(&wire);
        g_symbols = NULL;
        symbol_table_free(&symbols);
        return 1;
    }

    positive_and_type_gates(&counts, &language);
    typed_pattern_codec_gates(&counts, &language);
    malformed_and_mutation_gates(&counts, &language);
    resource_gates(&counts, &language);
    syntax_tree_language_gates(&counts, &syntax_language);
    resolution_input_language_gates(&counts, &resolution_language);
    (void)expect(
        &counts,
        strcmp(cetta_ld_ground_term_v1_status_name(
                   CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM),
               "non_ground_term") == 0,
        "statuses have stable diagnostic names");
    (void)expect(
        &counts,
        strcmp(cetta_ld_ground_term_v1_status_name(
                   CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN),
               "noncanonical_builtin") == 0,
        "noncanonical builtin status has a stable diagnostic name");

    cetta_language_def_core_v1_free(&resolution_language);
    cetta_op_lang_v1_free(&resolution_wire);
    cetta_language_def_core_v1_free(&syntax_language);
    cetta_op_lang_v1_free(&syntax_wire);
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("LanguageDef ground-term admission v1: %u passed, %u failed\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
