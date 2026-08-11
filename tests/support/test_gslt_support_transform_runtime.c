#include "atom.h"
#include "generated/mm2_gslt_profile_v1.generated.h"
#include "gslt_support_transform_runtime.h"
#include "mm2_lower.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                             \
    do {                                                                    \
        checks++;                                                           \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static bool result_contains(
    const CettaGsltSupportTransformResultV1 *result, Atom *expected) {
    for (size_t index = 0u; index < result->atom_count; index++) {
        if (atom_eq(result->atoms[index], expected))
            return true;
    }
    return false;
}

static bool result_has_head(
    const CettaGsltSupportTransformResultV1 *result, const char *head) {
    for (size_t index = 0u; index < result->atom_count; index++) {
        Atom *atom = result->atoms[index];
        if (atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
            atom_is_symbol(atom->expr.elems[0], head))
            return true;
    }
    return false;
}

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 ? forms[0] : NULL;
    free(forms);
    return result;
}

static bool run_text(
    const CettaGsltSupportTransformProfileV1 *profile,
    Arena *arena, const char *text, uint64_t fuel,
    CettaGsltSupportTransformResultV1 *result) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    char error[512] = {0};
    bool ok = count >= 0 && cetta_gslt_support_transform_run_v1(
        profile, arena, forms, (size_t)count, fuel,
        result, error, sizeof(error));
    if (!ok && error[0])
        fprintf(stderr, "support-transform diagnostic: %s\n", error);
    free(forms);
    return ok;
}

int main(void) {
    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    char error[512] = {0};
    CHECK(cetta_gslt_support_transform_profile_validate_v1(
              &cetta_mm2_gslt_profile_v1, error, sizeof(error)),
          "generated MM2 GSLT profile validates");
    CettaGsltSupportTransformProfileV1 malformed =
        cetta_mm2_gslt_profile_v1;
    malformed.manifest_sha256 = "short";
    CHECK(!cetta_gslt_support_transform_profile_validate_v1(
              &malformed, error, sizeof(error)),
          "malformed generated identity fails closed");
    malformed = cetta_mm2_gslt_profile_v1;
    malformed.compiler_sha256 =
        "gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg";
    CHECK(!cetta_gslt_support_transform_profile_validate_v1(
              &malformed, error, sizeof(error)),
          "non-hex generated identity fails closed");

    Atom *roundtrip_source = parse_one(
        &arena, "(pair $left (nested $left $right) \"wide value\")");
    uint8_t *roundtrip_packet = NULL;
    size_t roundtrip_packet_len = 0u;
    const char *roundtrip_error = NULL;
    Atom *roundtrip_atom = NULL;
    CHECK(cetta_mm2_atom_to_bridge_expr_packet(
              &arena, roundtrip_source,
              &roundtrip_packet, &roundtrip_packet_len, &roundtrip_error) &&
              cetta_mm2_bridge_expr_packet_to_atom(
                  &arena, roundtrip_packet, roundtrip_packet_len,
                  &roundtrip_atom, &roundtrip_error),
          "stable bridge packet round-trips into one canonical atom");
    CHECK(roundtrip_atom &&
              strcmp(atom_to_string(&arena, roundtrip_atom),
                     "(pair $v0 (nested $v0 $v1) \"wide value\")") == 0,
          "bridge decoding preserves co-reference with canonical names");
    free(roundtrip_packet);

    Atom *grounded_source = parse_one(
        &arena,
        "(types -12 999999999999999999999999 0.34714285714285714 "
        "2/3 True \"line\\ntext\")");
    Atom *grounded_roundtrip = cetta_mm2_alpha_canonicalize_atom(
        &arena, grounded_source, &roundtrip_error);
    CHECK(grounded_roundtrip && atom_eq(grounded_source, grounded_roundtrip),
          "bridge decoding preserves numeric, Boolean, rational, and string kinds");

    Atom *float_surface_source = parse_one(
        &arena,
        "(values 0.34714285714285714 0.000001 10000000000000000.0 -0.0)");
    const char *float_surface = cetta_mm2_atom_to_surface_string(
        &arena, float_surface_source);
    CHECK(float_surface &&
              strcmp(float_surface,
                     "(values 0.34714285714285714 1e-6 1e16 -0.0)") == 0,
          "MM2 f64 surface follows Rust shortest-roundtrip Debug spelling");

    const uint8_t dangling_varref[] = {3u, 0u};
    Atom *malformed_atom = NULL;
    CHECK(!cetta_mm2_bridge_expr_packet_to_atom(
              &arena, dangling_varref, sizeof(dangling_varref),
              &malformed_atom, &roundtrip_error),
          "bridge decoding rejects references before their binder");

    CettaGsltSupportTransformResultV1 result = {0};
    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(seed a) (seed a) "
              "(exec (0 fire) (, (seed $x)) (, (answer $x)))",
              8u, &result),
          "compatible MM2 GSLT directive executes");
    CHECK(result.outcome == CETTA_GSLT_SUPPORT_COMPLETED &&
              result.steps == 1u && result.atom_count == 2u,
          "support carrier coalesces duplicates and completes exactly");
    CHECK(result_contains(&result, parse_one(&arena, "(answer a)")),
          "compatible matching instantiates output");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(pair a a) (pair a b) "
              "(exec (0 repeated) (, (pair $x $x)) (, (same $x)))",
              8u, &result),
          "repeated-variable discriminator executes");
    CHECK(result_contains(&result, parse_one(&arena, "(same a)")) &&
              !result_contains(&result, parse_one(&arena, "(same b)")),
          "repeated variables require one consistent binding");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(p a) "
              "(exec (0 reusable) (, (p $x) (p $y)) (, (pair $x $y)))",
              8u, &result),
          "relational-product reuse discriminator executes");
    CHECK(result.atom_count == 2u &&
              result_contains(&result, parse_one(&arena, "(pair a a)")),
          "one support atom may satisfy two relational conjuncts");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(goal (Mammal Lassie)) "
              "(kb rule (-> (Dog $x) (Mammal $x))) "
              "(exec (0 open-row) "
              "  (, (goal $c) (kb rule (-> $p $c))) "
              "  (O (+ (goal $p)) (+ (pending $p $c)) (- (goal $c))))",
              8u, &result),
          "variable-bearing support row executes");
    CHECK(result_contains(&result, parse_one(&arena, "(goal (Dog Lassie))")) &&
              result_contains(
                  &result,
                  parse_one(&arena,
                            "(pending (Dog Lassie) (Mammal Lassie))")) &&
              !result_contains(
                  &result, parse_one(&arena, "(goal (Mammal Lassie))")),
          "stored variables unify bidirectionally with the query row");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(p $z) "
              "(exec (0 fresh-row) (, (p a) (p b)) (, (independent)))",
              8u, &result),
          "reused open support row executes");
    CHECK(result_contains(&result, parse_one(&arena, "(independent)")),
          "each relational use freshens stored variables independently");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(seed a) "
              "(exec (0 first) (, (seed $x)) "
              "  (, (mid $x) "
              "     (exec (1 second) (, (mid $x)) (, (done $x)))))",
              8u, &result),
          "generated work discriminator executes");
    CHECK(result.outcome == CETTA_GSLT_SUPPORT_COMPLETED &&
              result.steps == 2u &&
              result_contains(&result, parse_one(&arena, "(done a)")),
          "generated exec atoms re-enter the authored work queue");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(seed a) (old a) "
              "(exec (0 sinks) (, (seed $x)) "
              "  (O (- (old $x)) (+ (new $x)) (head 1 (front $x))))",
              8u, &result),
          "explicit sink discriminator executes");
    CHECK(!result_contains(&result, parse_one(&arena, "(old a)")) &&
              result_contains(&result, parse_one(&arena, "(new a)")) &&
              result_contains(&result, parse_one(&arena, "(front a)")),
          "remove, add, and head apply in authored sink order");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(item a) (item b) (item c) "
              "(exec (0 extrema) (, (item $x)) "
              "  (O (head 1 (least $x)) (tail 1 (greatest $x))))",
              8u, &result),
          "head/tail staging discriminator executes");
    CHECK(result_contains(&result, parse_one(&arena, "(least a)")) &&
              result_contains(&result, parse_one(&arena, "(greatest c)")) &&
              !result_contains(&result, parse_one(&arena, "(least b)")) &&
              !result_contains(&result, parse_one(&arena, "(greatest b)")),
          "head and tail select structural extrema after staging all rows");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(color apple red) (color strawberry red) (color cherry red) "
              "(color banana yellow) (color lemon yellow) (color grape purple) "
              "(exec (0 count) (, (color $fruit $color)) "
              "  (O (count (count-by-color $color $n) $n $fruit)))",
              8u, &result),
          "group-cardinality sink executes");
    CHECK(result_contains(
              &result, parse_one(&arena, "(count-by-color red 3)")) &&
              result_contains(
                  &result, parse_one(&arena, "(count-by-color yellow 2)")) &&
              result_contains(
                  &result, parse_one(&arena, "(count-by-color purple 1)")),
          "group-cardinality groups and counts distinct witnesses");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(shade apple red) (shade berry red) "
              "(exec (0 guarded-count) (, (shade $fruit $color)) "
              "  (O (count (exact $color 2) 2 $fruit) "
              "     (count (wrong $color 3) 3 $fruit)))",
              8u, &result),
          "fixed group-cardinality guard executes");
    CHECK(result_contains(&result, parse_one(&arena, "(exact red 2)")) &&
              !result_has_head(&result, "wrong"),
          "fixed cardinality emits exactly when its guard agrees");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(weight 0.95) (scale 0.99) "
              "(exec (0 pure-f64) (, (weight $x) (scale $y)) "
              "  (O (pure (product $result) $result "
              "       (f64_to_string "
              "         (product_f64 (f64_from_string $x) "
              "                      (f64_from_string $y))))))",
              8u, &result),
          "declared pure-f64 provider executes");
    CHECK(result_contains(
              &result, parse_one(&arena, "(product 0.9405)")),
          "pure-f64 evaluation projects its result through the pattern");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(weight 0.95) "
              "(exec (0 unsupported-pure) (, (weight $x)) "
              "  (O (pure (bad $result) $result (unknown_numeric $x))))",
              8u, &result),
          "undeclared pure operation boundary executes");
    CHECK(result.steps == 0u && result_has_head(&result, "exec") &&
              !result_has_head(&result, "bad"),
          "undeclared pure operation leaves its directive inert");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(weight not-a-number) "
              "(exec (0 invalid-pure-input) (, (weight $x)) "
              "  (O (pure (bad $result) $result "
              "       (f64_to_string (f64_from_string $x)))))",
              8u, &result),
          "declared pure operation with invalid data executes safely");
    CHECK(result.steps == 1u && !result_has_head(&result, "exec") &&
              !result_has_head(&result, "bad"),
          "invalid pure data yields no row without becoming a runtime fault");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(chosen one) (a one) (a two) "
              "(exec (0 sources) "
              "  (I (BTM (chosen $x)) (== (a $x) (a $x)) "
              "     (!= (a $x) (a $y))) "
              "  (, (different $x $y)))",
              8u, &result),
          "explicit source discriminator executes");
    CHECK(result_contains(&result, parse_one(&arena, "(different one two)")),
          "BTM, equality, and inequality factors thread one environment");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(ready) "
              "(exec é (, (ready)) (O (+ (winner unicode)) (- (ready)))) "
              "(exec aa (, (ready)) (O (+ (winner ascii)) (- (ready))))",
              8u, &result),
          "compact-byte scheduler discriminator executes");
    CHECK(result_contains(&result, parse_one(&arena, "(winner ascii)")) &&
              !result_contains(&result, parse_one(&arena, "(winner unicode)")),
          "scheduler compares MORK compact UTF-8 bytes, not Unicode structure");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(seed a) "
              "(exec (0 unknown) (, (seed $x)) (O (mystery (bad $x))))",
              8u, &result),
          "unsupported directive boundary executes");
    CHECK(result.outcome == CETTA_GSLT_SUPPORT_COMPLETED &&
              result.steps == 0u && result.atom_count == 2u &&
              result_has_head(&result, "exec"),
          "unsupported exec remains inert rather than disappearing");
    cetta_gslt_support_transform_result_free_v1(&result);

    CHECK(run_text(
              &cetta_mm2_gslt_profile_v1, &arena,
              "(seed a) "
              "(exec (0 bounded) (, (seed $x)) (, (answer $x)))",
              0u, &result),
          "zero-fuel boundary executes");
    CHECK(result.outcome == CETTA_GSLT_SUPPORT_EXPIRED &&
              result.steps == 0u && result.atom_count == 2u,
          "zero fuel is identity with explicit expiration");
    cetta_gslt_support_transform_result_free_v1(&result);

    CettaGsltSupportTransformProfileV1 renamed =
        cetta_mm2_gslt_profile_v1;
    const CettaGsltSupportOperatorDeclV1 renamed_sources[] = {
        {.surface_symbol = "store", .argument_count = 1u,
         .operator_id = "support.snapshot-match.v1"},
        {.surface_symbol = "same", .argument_count = 2u,
         .operator_id = "support.equal.v1"},
        {.surface_symbol = "other", .argument_count = 2u,
         .operator_id = "support.not-equal.v1"},
    };
    const CettaGsltSupportOperatorDeclV1 renamed_sinks[] = {
        {.surface_symbol = "put", .argument_count = 1u,
         .operator_id = "support.add.v1"},
        {.surface_symbol = "drop", .argument_count = 1u,
         .operator_id = "support.remove.v1"},
        {.surface_symbol = "front", .argument_count = 2u,
         .operator_id = "support.head.v1"},
        {.surface_symbol = "back", .argument_count = 2u,
         .operator_id = "support.tail.v1"},
        {.surface_symbol = "card", .argument_count = 3u,
         .operator_id = "support.group-cardinality.v1"},
        {.surface_symbol = "calculate", .argument_count = 3u,
         .operator_id = "support.evaluate-project.mm2-pure-f64.v1"},
    };
    renamed.language_name = "renamed";
    renamed.profile_name = "canary";
    renamed.work_symbol = "work";
    renamed.compat_input_symbol = "all";
    renamed.explicit_input_symbol = "from";
    renamed.source_declarations = renamed_sources;
    renamed.source_declaration_count =
        sizeof(renamed_sources) / sizeof(renamed_sources[0]);
    renamed.compat_output_symbol = "emit";
    renamed.explicit_output_symbol = "do";
    renamed.sink_declarations = renamed_sinks;
    renamed.sink_declaration_count =
        sizeof(renamed_sinks) / sizeof(renamed_sinks[0]);
    CHECK(run_text(
              &renamed, &arena,
              "(datum a) "
              "(work (0 renamed) (all (datum $x)) (emit (seen $x)))",
              8u, &result),
          "renamed support-transform profile executes");
    CHECK(result.outcome == CETTA_GSLT_SUPPORT_COMPLETED &&
              result_contains(&result, parse_one(&arena, "(seen a)")),
          "generic C runner contains no fixed MM2 vocabulary");
    cetta_gslt_support_transform_result_free_v1(&result);

    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    arena_free(&arena);

    printf("(GsltSupportTransformRuntimeSummary checks=%u failures=%u)\n",
           checks, failures);
    return failures ? 1 : 0;
}
