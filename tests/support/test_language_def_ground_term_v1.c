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
        pool, "fo-resolution:source-digest", source_arguments, 1u);
    Atom *clauses = pool_nullary(pool, "fo-resolution:clauses-nil");
    Atom *problem_arguments[2] = {source, clauses};

    return pool_expr(pool, "fo-resolution:problem", problem_arguments, 2u);
}

static Atom *make_scoped_variable_term(TestAtomPool *pool) {
    Atom *source_arguments[1] = {pool_string(pool, "sha256:source")};
    Atom *source = pool_expr(
        pool, "fo-resolution:source-digest", source_arguments, 1u);
    Atom *occurrence_arguments[2] = {source, pool_int(pool, 7)};
    Atom *occurrence = pool_expr(
        pool, "fo-resolution:occurrence", occurrence_arguments, 2u);
    Atom *name_arguments[1] = {pool_string(pool, "X")};
    Atom *name = pool_expr(
        pool, "fo-resolution:variable-name", name_arguments, 1u);
    Atom *identifier_arguments[2] = {occurrence, name};
    Atom *identifier = pool_expr(
        pool, "fo-resolution:variable-id", identifier_arguments, 2u);
    Atom *term_arguments[1] = {identifier};

    return pool_expr(pool, "fo-resolution:term-variable", term_arguments, 1u);
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
    {
        const char bytes[] = {'a', '\0', 'b'};
        CettaLdPatternV1 string_pattern;
        cetta_ld_pattern_v1_init(&string_pattern);
        Atom *value = atom_string_n(&source_pool.arena, bytes, sizeof(bytes));
        bool encoded_exact = cetta_language_def_ground_term_v1_to_pattern(
            language, "String", value, &string_pattern, 16u, 20000u,
            &status, error, sizeof(error)) && string_pattern.as.apply.head.len == sizeof(bytes) &&
            !memcmp(string_pattern.as.apply.head.bytes, bytes, sizeof(bytes));
        (void)expect(counts, encoded_exact, "counted string Pattern encoding retains embedded NUL");
        Atom *decoded = encoded_exact ? cetta_language_def_ground_term_v1_from_pattern(
            &result_pool.arena, language, "String", &string_pattern, 16u, 20000u,
            &status, error, sizeof(error)) : NULL;
        (void)expect(counts, decoded && atom_string_len(decoded) == sizeof(bytes) && atom_eq(value, decoded),
            "counted string Pattern decoding retains the complete original value");
        cetta_ld_pattern_v1_free(&string_pattern);
    }
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_language_def_ground_term_v1_to_pattern(
            language, "Term", source, &encoded, 128u, 20000u,
            &status, error, sizeof(error)) &&
            status == CETTA_LD_GROUND_TERM_V1_OK &&
            pattern_apply_has_head(
                &encoded, "fo-resolution:term-variable", 1u),
        error[0] ? error :
            "typed codec maps an admitted constructor term to PApp");
    variable_id = pattern_argument(&encoded, 0u);
    occurrence = pattern_argument(variable_id, 0u);
    variable_name = pattern_argument(variable_id, 1u);
    (void)expect(
        counts,
        pattern_apply_has_head(
            variable_id, "fo-resolution:variable-id", 2u) &&
            pattern_apply_has_head(
                occurrence, "fo-resolution:occurrence", 2u) &&
            pattern_apply_has_head(
                pattern_argument(occurrence, 1u), "7", 0u) &&
            pattern_apply_has_head(
                variable_name, "fo-resolution:variable-name", 1u) &&
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
                &encoded, "fo-resolution:term-variable", 1u),
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
        error[0] ? error : "explicitly scoped variable is ground problem data");
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
        &pool, "fo-resolution:source-digest", source_arguments, 1u);
    Atom *short_arguments[1] = {source};
    Atom *short_problem = pool_expr(
        &pool, "fo-resolution:problem", short_arguments, 1u);
    Atom *unknown_arguments[2] = {
        source, pool_nullary(&pool, "fo-resolution:clauses-nil")};
    Atom *unknown = pool_expr(
        &pool, "fo-resolution:problem-invented", unknown_arguments, 2u);
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
        "a host-language variable cannot replace explicit problem data");

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

static Atom *projection_span(TestAtomPool *pool, int64_t left, int64_t right) {
    Atom *args[] = {pool_int(pool, left), pool_int(pool, right),
                   pool_int(pool, left), pool_int(pool, right)};
    return pool_expr(pool, "LbnfSourceSpanV1", args, 4u);
}

static Atom *projection_node(TestAtomPool *pool, const char *category,
                             const char *label, Atom *span, Atom **children, uint32_t count) {
    Atom *list = pool_symbol(pool, "LNil");
    for (uint32_t i = count; i-- > 0u;) {
        Atom *args[] = {children[i], list};
        list = pool_expr(pool, "LCons", args, 2u);
    }
    Atom *args[] = {pool_string(pool, category), pool_string(pool, label), span, list};
    return pool_expr(pool, "LbnfAstNodeV1", args, 4u);
}

static Atom *projection_token(TestAtomPool *pool, const char *category,
                              Atom *span, const uint32_t *points, uint32_t count) {
    Atom *text = pool_nullary(pool, "bnf-v1:text-nil");
    for (uint32_t i = count; i-- > 0u;) {
        Atom *args[] = {pool_int(pool, points[i]), text};
        text = pool_expr(pool, "bnf-v1:text-cons", args, 2u);
    }
    Atom *where[] = {pool_int(pool, 3), pool_int(pool, 7)};
    Atom *args[] = {pool_string(pool, category), pool_symbol(pool, "TokenTextV1"), text,
        pool_expr(pool, "LbnfTokenPositionV1", where, 2u), span, text};
    return pool_expr(pool, "LbnfAstTokenV1", args, 6u);
}

#define PROJECT_TEXT(s) { .bytes = (uint8_t *)(s), .len = sizeof(s) - 1u }
static void parser_projection_gates(TestCounts *counts) {
    CettaLdTypeDeclV1 types[] = {
        {.name = PROJECT_TEXT("Nat"), .carrier = CETTA_LD_CARRIER_AST_V1},
        {.name = PROJECT_TEXT("Word"), .carrier = CETTA_LD_CARRIER_BUILTIN_STRING_V1},
        {.name = PROJECT_TEXT("Count"), .carrier = CETTA_LD_CARRIER_BUILTIN_INT_V1},
        {.name = PROJECT_TEXT("Raw"), .carrier = CETTA_LD_CARRIER_TOKEN_RAW_V1},
        {.name = PROJECT_TEXT("Flag"), .carrier = CETTA_LD_CARRIER_BUILTIN_BOOL_V1}};
    CettaLdTermParamV1 parameter = {.kind = CETTA_LD_PARAM_SIMPLE_V1,
        .type = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = PROJECT_TEXT("Nat")}};
    CettaLdGrammarRuleV1 rules[] = {
        {.label = PROJECT_TEXT("Z"), .category = PROJECT_TEXT("Nat")},
        {.label = PROJECT_TEXT("S"), .category = PROJECT_TEXT("Nat"), .params = &parameter, .param_len = 1u},
        {.label = PROJECT_TEXT("Z"), .category = PROJECT_TEXT("Nat")}};
    CettaLanguageDefCoreV1 language = {.types = types, .type_len = 5u, .terms = rules, .term_len = 2u};
    TestAtomPool input, output;
    pool_init(&input); pool_init(&output);
    Atom *span = projection_span(&input, 0, 10);
    Atom *z = projection_node(&input, "Nat", "Z", span, NULL, 0u);
    Atom *children[] = {z};
    Atom *s = projection_node(&input, "Nat", "S", span, children, 1u);
    Atom *sentinel = pool_symbol(&output, "unchanged"), *term = sentinel, *sources = sentinel;
    const Atom *blame = NULL;
    CettaLdGroundTermV1Status status;
    char error[256];
#define PROJECT(category, value, depth, work) \
    cetta_language_def_ground_term_v1_project_ast(&output.arena, &language, category, value, \
        depth, work, &term, &sources, &blame, &status, error, sizeof(error))
    (void)expect(counts, PROJECT("Nat", s, 100u, 10000u) &&
        cetta_language_def_ground_term_v1_admit(&language, "Nat", term, 100u, 10000u,
            &status, error, sizeof(error)) && sources->expr.len == 2u &&
        sources->expr.elems[1]->expr.elems[2]->ground.ival == 0 &&
        sources->expr.elems[1]->expr.elems[3]->ground.ival == 0,
        "labelled AST projects to an admitted constructor term with recoverable child positions");
    Atom *retained = term, *retained_sources = sources;
    term = sources = sentinel;
    (void)expect(counts, !PROJECT("Word", s, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH && blame == span &&
        term == sentinel && sources == sentinel, "wrong category refuses atomically at its source span");
    Atom *unknown = projection_node(&input, "Nat", "Invented", span, NULL, 0u);
    (void)expect(counts, !PROJECT("Nat", unknown, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR, "projection cannot invent a constructor");
    Atom *arity = projection_node(&input, "Nat", "Z", span, children, 1u);
    (void)expect(counts, !PROJECT("Nat", arity, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH, "projection checks the core constructor arity");
    language.term_len = 3u;
    (void)expect(counts, !PROJECT("Nat", z, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_CONSTRUCTOR,
        "ambiguous authority declarations are not silently identified by label");
    language.term_len = 2u;
    Atom *outside = projection_node(&input, "Nat", "Z", projection_span(&input, 9, 11), NULL, 0u);
    children[0] = outside;
    Atom *bad_span = projection_node(&input, "Nat", "S", span, children, 1u);
    (void)expect(counts, !PROJECT("Nat", bad_span, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT, "child source spans must lie inside their parent");
    (void)expect(counts, !PROJECT("Nat", s, 0u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT && blame == span &&
        term == sentinel && sources == sentinel,
        "projection depth exhaustion is incomplete with no published prefix");
    (void)expect(counts, !PROJECT("Nat", s, 100u, 1u) &&
        status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT,
        "projection construction work is bounded independently of semantic refusal");
    uint32_t word[] = {0x3bb, 0x1f600};
    Atom *token = projection_token(&input, "Word", span, word, 2u);
    (void)expect(counts, PROJECT("Word", token, 100u, 10000u) && term->ground.gkind == GV_STRING &&
        !strcmp(term->ground.sval, "λ😀") &&
        sources->expr.elems[0]->expr.elems[5]->expr.elems[1]->ground.ival == 3,
        "builtin string projection decodes Unicode and retains token position");
    uint32_t digits[] = {'0', '0', '4', '2'};
    token->expr.elems[4] = pool_symbol(&input, "LbnfNoTokenPositionV1");
    (void)expect(counts, PROJECT("Word", token, 100u, 10000u) &&
        term->ground.gkind == GV_STRING && !strcmp(term->ground.sval, "λ😀"),
        "the parser's symbolic absent token position projects without inventing a position");
    token = projection_token(&input, "Count", span, digits, 4u);
    (void)expect(counts, PROJECT("Count", token, 100u, 10000u) && term->ground.gkind == GV_INT &&
        term->ground.ival == 42, "decimal token spelling projects to the declared integer value");
    uint32_t overflow[] = {'9','2','2','3','3','7','2','0','3','6','8','5','4','7','7','5','8','0','8'};
    token = projection_token(&input, "Count", span, overflow, 19u);
    (void)expect(counts, !PROJECT("Count", token, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN, "integer overflow cannot silently change a value");
    token = projection_token(&input, "Raw", span, word, 2u);
    (void)expect(counts, !PROJECT("Raw", token, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE, "raw token declaration is not silently a builtin string");
    uint32_t nul[] = {0};
    token = projection_token(&input, "Word", span, nul, 1u);
    (void)expect(counts, !PROJECT("Word", token, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE, "counted NUL text cannot be truncated into a builtin string");
    uint32_t truth[] = {'T','r','u','e'};
    token = projection_token(&input, "Flag", span, truth, 4u);
    (void)expect(counts, PROJECT("Flag", token, 100u, 10000u) &&
        term->ground.gkind == GV_BOOL && term->ground.bval &&
        cetta_language_def_ground_term_v1_admit(&language, "Flag", term, 100u, 10000u,
            &status, error, sizeof(error)), "declared builtin Boolean projects to an admitted Boolean atom");
    uint32_t invented_truth[] = {'y','e','s'};
    token = projection_token(&input, "Flag", span, invented_truth, 3u);
    (void)expect(counts, !PROJECT("Flag", token, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
        "a token spelling cannot invent a Boolean value");
    Atom *list_fields[] = {pool_string(&input, "Nat"), span, pool_symbol(&input, "LNil")};
    Atom *ast_list = pool_expr(&input, "LbnfAstListV1", list_fields, 3u);
    (void)expect(counts, !PROJECT("Nat", ast_list, 100u, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "a parser list cannot silently acquire a constructor-carrier meaning");
    Atom *deep = z;
    for (uint32_t i = 0u; i < 100000u; ++i) {
        children[0] = deep;
        deep = projection_node(&input, "Nat", "S", span, children, 1u);
    }
    (void)expect(counts, PROJECT("Nat", deep, UINT32_MAX, 4000000u) &&
        sources->expr.len == 100001u &&
        cetta_language_def_ground_term_v1_admit(&language, "Nat", term, UINT32_MAX, 4000000u,
            &status, error, sizeof(error)), "100000-level projection and admission use no recursive C traversal");
    pool_free(&input);
    (void)expect(counts, cetta_language_def_ground_term_v1_admit(&language, "Nat", retained, 100u, 10000u,
        &status, error, sizeof(error)) && retained_sources->expr.elems[0]->expr.elems[4]->expr.elems[2]->ground.ival == 10,
        "projected terms and associated spans outlive the AST input arena");
    pool_free(&output);
#undef PROJECT
}
static void mixed_profile_gates(TestCounts *counts) {
    CettaLdTypeDeclV1 types[] = {
        {.name = PROJECT_TEXT("Nat"), .carrier = CETTA_LD_CARRIER_AST_V1},
        {.name = PROJECT_TEXT("Scoped"), .carrier = CETTA_LD_CARRIER_AST_V1}};
    CettaLdTermParamV1 plain = {.kind = CETTA_LD_PARAM_SIMPLE_V1,
        .type = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = PROJECT_TEXT("Nat")}};
    CettaLdTermParamV1 binding = {.kind = CETTA_LD_PARAM_ABSTRACTION_NAMED_V1,
        .type = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = PROJECT_TEXT("Nat")}};
    CettaLdTermParamV1 scoped = {.kind = CETTA_LD_PARAM_SIMPLE_V1,
        .type = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = PROJECT_TEXT("Scoped")}};
    CettaLdGrammarRuleV1 rules[] = {
        {.label = PROJECT_TEXT("Z"), .category = PROJECT_TEXT("Nat")},
        {.label = PROJECT_TEXT("S"), .category = PROJECT_TEXT("Nat"), .params = &plain, .param_len = 1u},
        {.label = PROJECT_TEXT("Binder"), .category = PROJECT_TEXT("Scoped"), .params = &binding, .param_len = 1u},
        {.label = PROJECT_TEXT("Inject"), .category = PROJECT_TEXT("Nat"), .params = &scoped, .param_len = 1u},
        {.label = PROJECT_TEXT("Z"), .category = PROJECT_TEXT("Nat"), .params = &binding, .param_len = 1u}};
    CettaLanguageDefCoreV1 language = {.types = types, .type_len = 2u, .terms = rules, .term_len = 3u};
    TestAtomPool input, output;
    pool_init(&input); pool_init(&output);
    Atom *z = pool_nullary(&input, "Z");
    Atom *args[] = {z};
    Atom *s = pool_expr(&input, "S", args, 1u);
    Atom *binder = pool_expr(&input, "Binder", args, 1u);
    CettaLdGroundTermV1Status status;
    char error[256];
    CettaLdPatternV1 pattern;
    cetta_ld_pattern_v1_init(&pattern);
#define MIX_ADMIT(category, term, work) \
    cetta_language_def_ground_term_v1_admit(&language, category, term, 128u, work, \
        &status, error, sizeof(error))
#define MIX_CODEC(category) \
    cetta_language_def_ground_term_v1_supports_pattern_codec(&language, category, 10000u, \
        &status, error, sizeof(error))
    (void)expect(counts, MIX_ADMIT("Nat", s, 10000u),
        "an independent binder declaration does not disable constructor admission");
    (void)expect(counts, MIX_CODEC("Nat"),
        "codec closure considers only constructors reachable from its category");
    (void)expect(counts, !MIX_CODEC("Scoped") && status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "a reachable binding declaration still needs its own codec");
    (void)expect(counts, !MIX_ADMIT("Scoped", binder, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "selecting a binding constructor never erases its scope");
    (void)expect(counts, cetta_language_def_ground_term_v1_to_pattern(&language, "Nat", s, &pattern,
        128u, 10000u, &status, error, sizeof(error)) && pattern_apply_has_head(&pattern, "S", 1u),
        "supported terms retain the Pattern codec in a mixed language");
    Atom *roundtrip = cetta_language_def_ground_term_v1_from_pattern(&output.arena, &language,
        "Nat", &pattern, 128u, 10000u, &status, error, sizeof(error));
    (void)expect(counts, roundtrip && atom_eq(s, roundtrip),
        "mixed-language constructor conversion roundtrips exactly");
    (void)expect(counts, !cetta_language_def_ground_term_v1_to_pattern(&language, "Scoped", binder,
        &pattern, 128u, 10000u, &status, error, sizeof(error)) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE && pattern_apply_has_head(&pattern, "S", 1u),
        "unsupported conversion preserves the previous complete Pattern");
    CettaLdPatternV1 binding_pattern = {.kind = CETTA_LD_PATTERN_APPLY_V1,
        .as.apply = {.head = PROJECT_TEXT("Binder"), .arguments = pattern.as.apply.arguments}};
    (void)expect(counts, !cetta_language_def_ground_term_v1_from_pattern(&output.arena, &language,
        "Scoped", &binding_pattern, 128u, 10000u, &status, error, sizeof(error)) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "Pattern readback also refuses the selected binding constructor");

    Atom *span = projection_span(&input, 0, 4);
    Atom *node = projection_node(&input, "Nat", "Z", span, NULL, 0u);
    Atom *children[] = {node};
    Atom *ast = projection_node(&input, "Nat", "S", span, children, 1u);
    Atom *sentinel = pool_symbol(&output, "unchanged"), *term = sentinel, *sources = sentinel;
    const Atom *blame = NULL;
#define MIX_PROJECT(category, ast) \
    cetta_language_def_ground_term_v1_project_ast(&output.arena, &language, category, ast, \
        128u, 10000u, &term, &sources, &blame, &status, error, sizeof(error))
    (void)expect(counts, MIX_PROJECT("Nat", ast) && atom_eq(term, s),
        "AST projection reaches the independent constructor profile");
    ast = projection_node(&input, "Scoped", "Binder", span, children, 1u);
    term = sources = sentinel;
    (void)expect(counts, !MIX_PROJECT("Scoped", ast) &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE && blame == span &&
        term == sentinel && sources == sentinel,
        "binding projection refuses atomically at the selected occurrence");
    language.term_len = 4u;
    (void)expect(counts, !MIX_CODEC("Nat") && status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "a transitive binding dependency is not skipped by a category-wide codec claim");
    (void)expect(counts, MIX_ADMIT("Nat", s, 10000u),
        "concrete admission needs only the constructors in the actual term");
    language.term_len = 5u;
    (void)expect(counts, !MIX_ADMIT("Nat", z, 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_CONSTRUCTOR,
        "an unsupported declaration is not filtered out to resolve ambiguity");
    language.term_len = 3u;
    CettaLdTypeExprV1 base = plain.type;
    binding.kind = CETTA_LD_PARAM_SIMPLE_V1;
    binding.type = (CettaLdTypeExprV1){.kind = CETTA_LD_TYPE_ARROW_V1,
        .as.arrow = {.domain = &base, .codomain = &base}};
    (void)expect(counts, MIX_ADMIT("Nat", s, 10000u) && !MIX_CODEC("Scoped") &&
        status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "non-base parameter profiles are likewise local to the partial consumer");
    bool budget_ok = true, saw_success = false, saw_incomplete = false;
    for (uint64_t budget = 0; budget < 128u; ++budget) {
        bool ok = MIX_ADMIT("Nat", s, budget);
        saw_success |= ok;
        saw_incomplete |= status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT;
        budget_ok &= ok || status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT ||
            (budget == 0u && status == CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT);
    }
    (void)expect(counts, budget_ok && saw_success && saw_incomplete,
        "mixed-profile admission budgets never turn an unrelated capability into rejection");
    types[0].carrier = CETTA_LD_CARRIER_BUILTIN_STRING_V1;
    (void)expect(counts, MIX_ADMIT("Nat", s, 10000u),
        "builtin carrier retains recursive declared constructor terms");
    (void)expect(counts, MIX_ADMIT("Nat", pool_string(&input, "Z"), 10000u),
        "builtin carrier also admits its grounded leaves");
    (void)expect(counts, !MIX_ADMIT("Nat", pool_int(&input, 0), 10000u) &&
        status == CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
        "builtin carrier does not silently coerce integer payloads");
    (void)expect(counts, !MIX_CODEC("Nat") && status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
        "admission does not imply a reversible untagged builtin-plus-constructor codec");
    cetta_ld_pattern_v1_free(&pattern);
    pool_free(&input); pool_free(&output);
#undef MIX_PROJECT
#undef MIX_CODEC
#undef MIX_ADMIT
}
#undef PROJECT_TEXT

int main(void) {
    TestCounts counts = {0};
    SymbolTable symbols;
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOperationalLanguageDefV1 syntax_wire;
    CettaLanguageDefCoreV1 syntax_language;
    char error[512] = {0};

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_op_lang_v1_init(&syntax_wire);
    cetta_language_def_core_v1_init(&syntax_language);
    if (!load_language_file(
            "langdef/logic/first_order_resolution_input_v1.metta",
            &wire, &language, error, sizeof(error))) {
        fprintf(stderr, "FAIL: %s\n",
                error[0] ? error : "load FirstOrderResolutionInput");
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
    positive_and_type_gates(&counts, &language);
    typed_pattern_codec_gates(&counts, &language);
    malformed_and_mutation_gates(&counts, &language);
    resource_gates(&counts, &language);
    syntax_tree_language_gates(&counts, &syntax_language);
    resolution_input_language_gates(&counts, &language);
    parser_projection_gates(&counts);
    mixed_profile_gates(&counts);
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
