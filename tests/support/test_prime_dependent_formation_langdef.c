#include "atom.h"
#include "eval.h"
#include "generated/prime_typing_elaborated_dependent_formation_core_source_binding_v1.generated.h"
#include "gslt_horn_runtime.h"
#include "library.h"
#include "parser.h"
#include "prime_semantics.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERROR_CAP = 1024, QUERY_CAP = 4096 };

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL: %s\n", (label));                        \
            failures++;                                                      \
        }                                                                    \
    } while (0)

typedef struct {
    const char *label;
    const char *signature;
    const char *telescope;
    const char *model_canonical;
    const char *syntax;
    const char *native_canonical;
} PositiveCase;

typedef struct {
    const char *label;
    const char *signature;
    const char *telescope;
    const char *syntax;
    const char *native_status;
    bool native_canonicalizes;
} NegativeCase;

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static CettaGsltHornLimits qualification_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 100000u,
        .max_answers = 16u,
        .max_depth = 256u,
    };
}

static bool prime_model_index(const Atom *model, uint64_t *index) {
    if (!model || !index) return false;
    uint64_t value = 0u;
    const Atom *cursor = model;
    while (cursor && cursor->kind == ATOM_EXPR &&
           cursor->expr.len == 2u &&
           atom_is_symbol(cursor->expr.elems[0], "PrimeSucc")) {
        if (value == (uint64_t)INT64_MAX) return false;
        value++;
        cursor = cursor->expr.elems[1];
    }
    if (!cursor || !atom_is_symbol((Atom *)cursor, "PrimeZero"))
        return false;
    *index = value;
    return true;
}

/* Translate only the authored fragment's model encoding.  This bridge is
 * qualification code: the production Prime judge never executes Horn rules. */
static Atom *model_canonical_to_native(Arena *arena, const Atom *model) {
    if (!arena || !model) return NULL;
    if (model->kind == ATOM_SYMBOL) {
        if (atom_is_symbol((Atom *)model, "PDynamic"))
            return atom_symbol(arena, "%Undefined%");
        return atom_deep_copy(arena, (Atom *)model);
    }
    if (model->kind != ATOM_EXPR || model->expr.len == 0u)
        return NULL;
    if (model->expr.len == 2u &&
        atom_is_symbol(model->expr.elems[0], "PrimeNamed")) {
        return atom_deep_copy(arena, model->expr.elems[1]);
    }
    if (model->expr.len == 2u &&
        atom_is_symbol(model->expr.elems[0], "idx")) {
        uint64_t index = 0u;
        if (!prime_model_index(model->expr.elems[1], &index)) return NULL;
        return atom_expr2(
            arena, atom_symbol(arena, "idx"),
            atom_int(arena, (int64_t)index));
    }
    if (model->expr.len == 3u &&
        atom_is_symbol(model->expr.elems[0], "Pi")) {
        Atom *domain = model_canonical_to_native(
            arena, model->expr.elems[1]);
        Atom *body = model_canonical_to_native(
            arena, model->expr.elems[2]);
        if (!domain || !body) return NULL;
        return atom_expr3(arena, atom_symbol(arena, "Pi"), domain, body);
    }
    return NULL;
}

static bool direct_form_has_status(
    Arena *arena, Space *space, Atom *syntax, const char *status) {
    Atom *judgment = syntax
        ? atom_expr2(arena, atom_symbol(arena, "type:formed"), syntax)
        : NULL;
    Atom *verdict = judgment
        ? cetta_prime_typing_direct_service_v1.judge(
              arena, space, judgment, false, 0u)
        : NULL;
    bool matches = verdict && verdict->kind == ATOM_EXPR &&
                   verdict->expr.len == 4u &&
                   atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
                   atom_is_symbol(verdict->expr.elems[1], status);
    if (!matches) {
        fprintf(stderr, "Prime type:formed status for ");
        atom_print(syntax, stderr);
        fprintf(stderr, " was ");
        atom_print(verdict, stderr);
        fprintf(stderr, "; expected %s\n", status);
    }
    return matches;
}

static void check_positive(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers,
    Arena *native, Space *space, const PositiveCase *test) {
    char query_text[QUERY_CAP];
    int query_size = snprintf(
        query_text, sizeof query_text,
        "(PrimeClosedElaborates %s %s $canonical)",
        test->signature, test->telescope);
    Atom *query = query_size > 0 && (size_t)query_size < sizeof query_text
        ? parse_one(queries, query_text) : NULL;
    Atom *expected_model = parse_one(queries, test->model_canonical);
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && expected_model && cetta_gslt_horn_query(
        program, answers, query, qualification_limits(),
        &result, error, sizeof error);
    CHECK(ran, test->label);
    if (!ran) {
        fprintf(stderr, "%s Horn diagnostic: %s\n", test->label, error);
        cetta_gslt_horn_result_free(&result);
        return;
    }
    bool one_answer = result.outcome == CETTA_GSLT_HORN_COMPLETED &&
                      result.answer_count == 1u && result.answers[0] &&
                      result.answers[0]->kind == ATOM_EXPR &&
                      result.answers[0]->expr.len == 4u;
    CHECK(one_answer, "dependent formation has one complete derivation");
    Atom *model_output = one_answer
        ? result.answers[0]->expr.elems[3] : NULL;
    CHECK(model_output && atom_eq(model_output, expected_model),
          "Horn derivation returns the expected canonical model term");

    Atom *syntax = parse_one(native, test->syntax);
    Atom *expected_native = parse_one(native, test->native_canonical);
    Atom *native_output = syntax
        ? prime_semantics_canonicalize_type(native, syntax) : NULL;
    Atom *translated_model = model_output
        ? model_canonical_to_native(native, model_output) : NULL;
    CHECK(syntax && expected_native && native_output && translated_model,
          "positive parity fixture constructs both realizations");
    CHECK(native_output && expected_native &&
              atom_eq(native_output, expected_native),
          "native Prime elaboration returns the expected canonical ABT");
    CHECK(native_output && translated_model &&
              atom_eq(native_output, translated_model),
          "authored Horn and direct C elaboration agree exactly");
    CHECK(direct_form_has_status(
              native, space, syntax, "Established"),
          "direct Prime authority establishes the syntax type");
    cetta_gslt_horn_result_free(&result);
}

static void check_negative(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers,
    Arena *native, Space *space, const NegativeCase *test) {
    char query_text[QUERY_CAP];
    int query_size = snprintf(
        query_text, sizeof query_text,
        "(PrimeClosedElaborates %s %s $canonical)",
        test->signature, test->telescope);
    Atom *query = query_size > 0 && (size_t)query_size < sizeof query_text
        ? parse_one(queries, query_text) : NULL;
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && cetta_gslt_horn_query(
        program, answers, query, qualification_limits(),
        &result, error, sizeof error);
    CHECK(ran, test->label);
    if (!ran) {
        fprintf(stderr, "%s Horn diagnostic: %s\n", test->label, error);
        cetta_gslt_horn_result_free(&result);
        return;
    }
    CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 0u,
          "negative dependent-formation query has no derivation");

    Atom *syntax = parse_one(native, test->syntax);
    Atom *canonical = syntax
        ? prime_semantics_canonicalize_type(native, syntax) : NULL;
    CHECK((canonical != NULL) == test->native_canonicalizes,
          "native syntactic canonicalization matches the declared boundary");
    CHECK(direct_form_has_status(
              native, space, syntax, test->native_status),
          "direct Prime authority preserves the negative status");
    cetta_gslt_horn_result_free(&result);
}

static void check_invalid_context_is_rejected(
    const CettaGsltHornProgram *program, Arena *queries, Arena *answers) {
    Atom *query = parse_one(
        queries,
        "(PrimeCanonicalForm PrimeSigNil "
        "  (PrimeCtxCons (PrimeNamed Ghost) PrimeCtxNil) "
        "  (idx PrimeZero))");
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && cetta_gslt_horn_query(
        program, answers, query, qualification_limits(),
        &result, error, sizeof error);
    CHECK(ran, "invalid-context formation query executes");
    if (!ran) {
        fprintf(stderr, "invalid-context Horn diagnostic: %s\n", error);
        cetta_gslt_horn_result_free(&result);
        return;
    }
    CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 0u,
          "an in-scope index cannot launder an ill-formed context");
    cetta_gslt_horn_result_free(&result);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s DEPENDENT_FORMATION_LANGDEF\n", argv[0]);
        return 2;
    }

    Arena queries;
    Arena answers;
    Arena native;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variable_names;
    CettaLibraryContext libraries;
    bool libraries_ready = false;
    CettaGsltHornProgram *program = NULL;
    char error[ERROR_CAP] = {0};

    arena_init(&queries);
    arena_init(&answers);
    arena_init(&native);
    arena_set_runtime_kind(&native, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &native);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variable_names);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = &variable_names;
    cetta_library_context_init_for_language_profile(
        &libraries, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    libraries_ready = true;
    eval_set_library_context(&libraries);

    const CettaNikDirectSourceBindingV1 *binding =
        &prime_typing_elaborated_dependent_formation_core_source_binding_v1;
    CHECK(cetta_nik_direct_source_binding_v1_is_valid(binding) &&
              binding->authority ==
                  &cetta_prime_typing_direct_authority_v1 &&
              strcmp(binding->schema_id, "finite-horn-gslt-v1") == 0 &&
              strcmp(binding->presentation_id,
                     "prime-elaborated-dependent-formation-core-v1") == 0 &&
              strcmp(binding->semantic_scope,
                     "prime.typing.elaborated-dependent-formation-core") == 0 &&
              binding->coverage ==
                  CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT,
          "dependent formation source is a valid authored fragment binding");
    CettaNikDirectSourceBindingV1 invalid_binding = *binding;
    invalid_binding.coverage = 0;
    CHECK(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_binding),
          "dependent formation binding rejects an invalid coverage grade");
    invalid_binding = *binding;
    invalid_binding.source_sha256 = "short";
    CHECK(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_binding),
          "dependent formation binding rejects a malformed source digest");
    invalid_binding = *binding;
    invalid_binding.semantic_scope = "";
    CHECK(!cetta_nik_direct_source_binding_v1_is_valid(&invalid_binding),
          "dependent formation binding requires a semantic scope");
    CHECK(cetta_prime_typing_direct_service_v1.judge ==
              prime_semantics_judge_typing_direct,
          "production formation remains the direct certificate-free judge");

    const char *paths[] = {argv[1]};
    bool loaded = cetta_gslt_horn_program_load_paths(
        paths, 1u, &program, error, sizeof error);
    CHECK(loaded, "dependent formation langdef loads in the generic oracle");
    if (!loaded) {
        fprintf(stderr, "dependent formation load diagnostic: %s\n", error);
        goto cleanup;
    }
    CHECK(cetta_gslt_horn_program_rule_count(program) == 23u,
          "all 23 authored dependent-formation rules are executable");
    check_invalid_context_is_rejected(
        program, &queries, &answers);

    Atom *nat_declaration = parse_one(&native, "(: Nat Type)");
    CHECK(nat_declaration != NULL,
          "declared-type parity fixture parses");
    if (nat_declaration) space_add(&space, nat_declaration);

    const PositiveCase positive[] = {
        {
            .label = "identity telescope derives",
            .signature = "PrimeSigNil",
            .telescope =
                "(PrimeTeleBind Type (PrimeTeleDone (idx PrimeZero)))",
            .model_canonical = "(Pi Type (idx PrimeZero))",
            .syntax = "(-> (: $x Type) $x)",
            .native_canonical = "(Pi Type (idx 0))",
        },
        {
            .label = "nested dependent telescope derives",
            .signature = "PrimeSigNil",
            .telescope =
                "(PrimeTeleBind Type "
                "  (PrimeTeleBind (idx PrimeZero) "
                "    (PrimeTeleDone (idx (PrimeSucc PrimeZero)))))",
            .model_canonical =
                "(Pi Type (Pi (idx PrimeZero) "
                "  (idx (PrimeSucc PrimeZero))))",
            .syntax = "(-> (: $x Type) (: $y $x) $x)",
            .native_canonical = "(Pi Type (Pi (idx 0) (idx 1)))",
        },
        {
            .label = "declared telescope derives",
            .signature =
                "(PrimeSigCons (PrimeNamed Nat) PrimeSigNil)",
            .telescope =
                "(PrimeTeleBind (PrimeNamed Nat) "
                "  (PrimeTeleDone (PrimeNamed Nat)))",
            .model_canonical =
                "(Pi (PrimeNamed Nat) (PrimeNamed Nat))",
            .syntax = "(-> Nat Nat)",
            .native_canonical = "(Pi Nat Nat)",
        },
    };
    for (size_t i = 0u; i < sizeof positive / sizeof positive[0]; i++)
        check_positive(
            program, &queries, &answers, &native, &space, &positive[i]);

    Atom *alpha_x = parse_one(&native, "(-> (: $alpha-x Type) $alpha-x)");
    Atom *alpha_y = parse_one(
        &native, "(-> (: $alpha-renamed Type) $alpha-renamed)");
    Atom *alpha_x_canonical = alpha_x
        ? prime_semantics_canonicalize_type(&native, alpha_x) : NULL;
    Atom *alpha_y_canonical = alpha_y
        ? prime_semantics_canonicalize_type(&native, alpha_y) : NULL;
    CHECK(alpha_x_canonical && alpha_y_canonical &&
              atom_eq(alpha_x_canonical, alpha_y_canonical),
          "syntax alpha-renaming erases to one model telescope");

    const NegativeCase negative[] = {
        {
            .label = "loose index has no derivation",
            .signature = "PrimeSigNil",
            .telescope = "(PrimeTeleDone (idx PrimeZero))",
            .syntax = "(idx 0)",
            .native_status = "Undetermined",
            .native_canonicalizes = false,
        },
        {
            .label = "escaping telescope index has no derivation",
            .signature = "PrimeSigNil",
            .telescope =
                "(PrimeTeleBind Type "
                "  (PrimeTeleDone (idx (PrimeSucc PrimeZero))))",
            .syntax = "(-> (: $x Type) $outside)",
            .native_status = "Undetermined",
            .native_canonicalizes = false,
        },
        {
            .label = "undeclared type name has no derivation",
            .signature = "PrimeSigNil",
            .telescope = "(PrimeTeleDone (PrimeNamed Ghost))",
            .syntax = "Ghost",
            .native_status = "Undetermined",
            .native_canonicalizes = true,
        },
        {
            .label = "universe application remains outside the fragment",
            .signature = "PrimeSigNil",
            .telescope = "(PrimeTeleDone (Type Level))",
            .syntax = "(Type Level)",
            .native_status = "Undetermined",
            .native_canonicalizes = true,
        },
    };
    for (size_t i = 0u; i < sizeof negative / sizeof negative[0]; i++)
        check_negative(
            program, &queries, &answers, &native, &space, &negative[i]);

    Atom *malformed_binder = parse_one(
        &native, "(-> (: not-a-variable Type) Type)");
    CHECK(malformed_binder &&
              !prime_semantics_canonicalize_type(&native, malformed_binder),
          "native elaboration rejects a non-variable telescope binder");
    CHECK(direct_form_has_status(
              &native, &space, malformed_binder, "Refuted"),
          "direct Prime authority refutes a non-variable binder");

cleanup:
    cetta_gslt_horn_program_free(program);
    eval_set_library_context(NULL);
    if (libraries_ready) cetta_library_context_free(&libraries);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&native);
    arena_free(&answers);
    arena_free(&queries);

    if (failures != 0u) {
        fprintf(stderr,
                "PrimeDependentFormationLangdefSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(PrimeDependentFormationLangdefSummary checks=%u failures=0)\n",
           checks);
    return 0;
}
