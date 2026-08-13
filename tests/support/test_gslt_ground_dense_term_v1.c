#include "../../src/gslt_ground_dense_term_v1.h"

#include "../../src/symbol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t checks;
static uint32_t failures;

static void expect_true(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

typedef struct {
    VarId variables[4];
    Atom *values[4];
    uint32_t len;
} ViewEnvironment;

static CettaGsltGroundDenseStatusV1 resolve_view_variable(
        void *context, Atom *source_variable, Atom **target_out) {
    ViewEnvironment *environment = context;

    if (target_out)
        *target_out = NULL;
    if (!environment || !source_variable || !target_out ||
        source_variable->kind != ATOM_VAR)
        return CETTA_GSLT_GROUND_DENSE_INVALID_V1;
    for (uint32_t index = 0u; index < environment->len; index++) {
        if (environment->variables[index] == source_variable->var_id) {
            *target_out = environment->values[index];
            return *target_out
                ? CETTA_GSLT_GROUND_DENSE_OK_V1
                : CETTA_GSLT_GROUND_DENSE_INVALID_V1;
        }
    }
    return CETTA_GSLT_GROUND_DENSE_DEFER_V1;
}

static Atom *expression(
    Arena *arena, const char *head, Atom **arguments, uint32_t arity) {
    Atom *items[8];

    if (!arena || !head || arity > 7u)
        return NULL;
    items[0] = atom_symbol(arena, head);
    for (uint32_t index = 0u; index < arity; index++)
        items[index + 1u] = arguments[index];
    return atom_expr(arena, items, (CettaExprLen)arity + 1u);
}

int main(void) {
    SymbolTable symbols;
    Arena source;
    Arena target;
    Arena output;
    CettaGsltGroundDenseTermProgramV1 parser_head;
    CettaGsltGroundDenseTermProgramV1 parser_body;
    CettaGsltGroundDenseTermProgramV1 rewrite_head;
    CettaGsltGroundDenseTermProgramV1 rewrite_body;
    CettaGsltGroundDenseTermProgramV1 missing_body;
    CettaGsltGroundDenseTermProgramV1 rigid;
    CettaGsltGroundDenseTermProgramV1 capture;
    CettaGsltGroundDenseWorkspaceV1 workspace;
    CettaGsltGroundDenseStatsV1 stats = {0};
    CettaGsltGroundDenseStatsV1 reuse_stats = {0};
    char error[256] = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&source);
    arena_set_runtime_kind(&source, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&target);
    arena_set_runtime_kind(&target, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_init(&output);
    arena_set_runtime_kind(&output, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    cetta_gslt_ground_dense_term_program_init_v1(&parser_head);
    cetta_gslt_ground_dense_term_program_init_v1(&parser_body);
    cetta_gslt_ground_dense_term_program_init_v1(&rewrite_head);
    cetta_gslt_ground_dense_term_program_init_v1(&rewrite_body);
    cetta_gslt_ground_dense_term_program_init_v1(&missing_body);
    cetta_gslt_ground_dense_term_program_init_v1(&rigid);
    cetta_gslt_ground_dense_term_program_init_v1(&capture);
    cetta_gslt_ground_dense_workspace_init_v1(&workspace);

    {
        Atom *cursor = atom_var_with_id(&source, "cursor", 101u);
        Atom *forest = atom_var_with_id(&source, "forest", 102u);
        Atom *literal_arguments[] = {atom_string(&source, "name")};
        Atom *literal = expression(
            &source, "token-kind", literal_arguments, 1u);
        Atom *pair_arguments[] = {cursor, forest, cursor};
        Atom *pair = expression(
            &source, "parser-cell", pair_arguments, 3u);
        Atom *head_arguments[] = {literal, pair};
        Atom *head = expression(
            &source, "parser-step", head_arguments, 2u);
        Atom *body_pair_arguments[] = {forest, cursor};
        Atom *body_pair = expression(
            &source, "parser-output", body_pair_arguments, 2u);
        Atom *body_arguments[] = {literal, body_pair};
        Atom *body = expression(
            &source, "parser-commit", body_arguments, 2u);
        Atom *cursor_value = atom_int(&target, 7);
        Atom *forest_value = atom_string(&target, "forest");
        Atom *target_pair_arguments[] = {
            cursor_value, forest_value, cursor_value,
        };
        Atom *target_pair = expression(
            &target, "parser-cell", target_pair_arguments, 3u);
        Atom *target_arguments[] = {literal, target_pair};
        Atom *closed_target = expression(
            &target, "parser-step", target_arguments, 2u);
        Atom *expected_pair_arguments[] = {forest_value, cursor_value};
        Atom *expected_pair = expression(
            &target, "parser-output", expected_pair_arguments, 2u);
        Atom *expected_arguments[] = {literal, expected_pair};
        Atom *expected = expression(
            &target, "parser-commit", expected_arguments, 2u);
        Atom *instantiated = NULL;
        uint32_t parser_nodes;

        expect_true(
            cetta_gslt_ground_dense_term_compile_v1(
                &parser_head, head, 101u, 2u, error, sizeof(error)),
            error[0] ? error : "parser-shaped head compiles");
        error[0] = '\0';
        expect_true(
            cetta_gslt_ground_dense_term_compile_v1(
                &parser_body, body, 101u, 2u, error, sizeof(error)),
            error[0] ? error : "parser-shaped body compiles");
        expect_true(
            !cetta_gslt_ground_dense_term_is_linear_v1(&parser_head),
            "repeated parser slot is recognized as nonlinear");
        parser_nodes =
            cetta_gslt_ground_dense_term_node_count_v1(&parser_head);
        expect_true(
            parser_nodes > 0u &&
                cetta_gslt_ground_dense_term_width_v1(&parser_head) == 2u,
            "compiled parser program records nodes and dense width");
        expect_true(
            cetta_gslt_ground_dense_term_match_v1(
                &workspace, &parser_head, closed_target, &stats) ==
                CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                stats.slot_writes == 2u && stats.slot_compares == 1u,
            "parser-shaped matching writes dense slots and checks repetition");
        expect_true(
            cetta_gslt_ground_dense_term_instantiate_v1(
                &workspace, &parser_body, &output, &instantiated, &stats) ==
                CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                instantiated && !atom_has_vars(instantiated) &&
                atom_eq(instantiated, expected),
            "parser-shaped body instantiation is exact and closed");
        expect_true(
            stats.expression_materializations == 2u &&
                stats.rigid_subtrees_reused > 0u,
            "instantiation rebuilds only slot-dependent expressions");

        {
            Atom *view_cursor = atom_var_with_id(&source, "view-cursor", 501u);
            Atom *view_forest = atom_var_with_id(&source, "view-forest", 502u);
            Atom *view_pair_arguments[] = {
                view_cursor, view_forest, view_cursor,
            };
            Atom *view_pair = expression(
                &source, "parser-cell", view_pair_arguments, 3u);
            Atom *view_arguments[] = {literal, view_pair};
            Atom *view = expression(
                &source, "parser-step", view_arguments, 2u);
            ViewEnvironment environment = {
                .variables = {501u, 502u},
                .values = {cursor_value, forest_value},
                .len = 2u,
            };
            CettaGsltGroundDenseStatsV1 view_stats = {0};

            expect_true(
                cetta_gslt_ground_dense_term_match_view_v1(
                    &workspace, &parser_head, view,
                    resolve_view_variable, &environment, &view_stats) ==
                    CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                    view_stats.view_nodes > 0u &&
                    view_stats.view_variable_resolutions == 3u &&
                    view_stats.view_deferrals == 0u,
                "dense matching traverses a substituted parser view directly");
            instantiated = NULL;
            expect_true(
                cetta_gslt_ground_dense_term_instantiate_v1(
                    &workspace, &parser_body, &output, &instantiated, NULL) ==
                    CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                    instantiated && atom_eq(instantiated, expected),
                "a direct view match supplies the same bounded body slots");

            environment.len = 1u;
            expect_true(
                cetta_gslt_ground_dense_term_match_view_v1(
                    &workspace, &parser_head, view,
                    resolve_view_variable, &environment, &view_stats) ==
                    CETTA_GSLT_GROUND_DENSE_DEFER_V1 &&
                    view_stats.view_deferrals > 0u,
                "an unresolved view variable defers instead of mismatching");
            expect_true(
                cetta_gslt_ground_dense_term_instantiate_v1(
                    &workspace, &parser_body, &output, &instantiated, NULL) ==
                    CETTA_GSLT_GROUND_DENSE_INVALID_V1,
                "a deferred view cannot leak a partial dense substitution");
        }

        memset(&reuse_stats, 0, sizeof(reuse_stats));
        expect_true(
            cetta_gslt_ground_dense_term_match_v1(
                &workspace, &parser_head, closed_target, &reuse_stats) ==
                CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                reuse_stats.workspace_growths == 0u,
            "a repeated match reuses dense workspace storage");

        {
            Atom *wrong_pair_arguments[] = {
                cursor_value, forest_value, atom_int(&target, 8),
            };
            Atom *wrong_pair = expression(
                &target, "parser-cell", wrong_pair_arguments, 3u);
            Atom *wrong_arguments[] = {literal, wrong_pair};
            Atom *wrong = expression(
                &target, "parser-step", wrong_arguments, 2u);
            expect_true(
                cetta_gslt_ground_dense_term_match_v1(
                    &workspace, &parser_head, wrong, NULL) ==
                    CETTA_GSLT_GROUND_DENSE_MISMATCH_V1,
                "a repeated-slot disagreement is rejected");
        }
        {
            Atom *open_pair_arguments[] = {
                cursor_value, forest_value,
                atom_var_with_id(&target, "open", 9001u),
            };
            Atom *open_pair = expression(
                &target, "parser-cell", open_pair_arguments, 3u);
            Atom *open_arguments[] = {literal, open_pair};
            Atom *open_target = expression(
                &target, "parser-step", open_arguments, 2u);
            expect_true(
                cetta_gslt_ground_dense_term_match_v1(
                    &workspace, &parser_head, open_target, NULL) ==
                    CETTA_GSLT_GROUND_DENSE_INVALID_V1,
                "an open target is outside the admitted ground path");
        }
        {
            Atom *outside = atom_var_with_id(&source, "outside", 103u);
            error[0] = '\0';
            expect_true(
                !cetta_gslt_ground_dense_term_compile_v1(
                    &parser_head, outside, 101u, 2u,
                    error, sizeof(error)) &&
                    error[0] != '\0' &&
                    cetta_gslt_ground_dense_term_node_count_v1(
                        &parser_head) == parser_nodes,
                "out-of-range compilation rejects transactionally");
            expect_true(
                cetta_gslt_ground_dense_term_match_v1(
                    &workspace, &parser_head, closed_target, NULL) ==
                    CETTA_GSLT_GROUND_DENSE_OK_V1,
                "rejected compilation preserves the admitted program");
        }
    }

    {
        Atom *state = atom_var_with_id(&source, "state", 201u);
        Atom *head_arguments[] = {state, atom_symbol(&source, "ready")};
        Atom *head = expression(
            &source, "rewrite-step", head_arguments, 2u);
        Atom *body_arguments[] = {state};
        Atom *body = expression(
            &source, "rewrite-result", body_arguments, 1u);
        Atom *missing = atom_var_with_id(&source, "missing", 202u);
        Atom *missing_arguments[] = {missing};
        Atom *missing_term = expression(
            &source, "rewrite-result", missing_arguments, 1u);
        Atom *state_value = atom_string(&target, "closed-state");
        Atom *target_arguments[] = {
            state_value, atom_symbol(&target, "ready"),
        };
        Atom *closed_target = expression(
            &target, "rewrite-step", target_arguments, 2u);
        Atom *expected_arguments[] = {state_value};
        Atom *expected = expression(
            &target, "rewrite-result", expected_arguments, 1u);
        Atom *instantiated = NULL;

        error[0] = '\0';
        expect_true(
            cetta_gslt_ground_dense_term_compile_v1(
                &rewrite_head, head, 201u, 2u, error, sizeof(error)) &&
                cetta_gslt_ground_dense_term_is_linear_v1(&rewrite_head),
            error[0] ? error : "linear rewrite-shaped head compiles");
        error[0] = '\0';
        expect_true(
            cetta_gslt_ground_dense_term_compile_v1(
                &rewrite_body, body, 201u, 2u, error, sizeof(error)) &&
                cetta_gslt_ground_dense_term_compile_v1(
                    &missing_body, missing_term, 201u, 2u,
                    error, sizeof(error)),
            error[0] ? error : "rewrite-shaped bodies compile");
        expect_true(
            cetta_gslt_ground_dense_term_match_v1(
                &workspace, &rewrite_head, closed_target, NULL) ==
                CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                cetta_gslt_ground_dense_term_instantiate_v1(
                    &workspace, &rewrite_body, &output,
                    &instantiated, NULL) ==
                CETTA_GSLT_GROUND_DENSE_OK_V1 &&
                atom_eq(instantiated, expected),
            "linear rewrite-shaped match and instantiation agree");
        expect_true(
            cetta_gslt_ground_dense_term_instantiate_v1(
                &workspace, &missing_body, &output,
                &instantiated, NULL) ==
                CETTA_GSLT_GROUND_DENSE_INVALID_V1,
            "an unbound body slot fails closed");
        cetta_gslt_ground_dense_workspace_discard_match_v1(&workspace);
        expect_true(
            cetta_gslt_ground_dense_term_instantiate_v1(
                &workspace, &rewrite_body, &output,
                &instantiated, NULL) ==
                CETTA_GSLT_GROUND_DENSE_INVALID_V1,
            "discarded substitutions cannot escape their transaction");
    }

    {
        Atom *rigid_source = atom_symbol(&source, "rigid-fact");
        Atom *rigid_target = atom_symbol(&target, "rigid-fact");

        error[0] = '\0';
        expect_true(
            cetta_gslt_ground_dense_term_compile_v1(
                &rigid, rigid_source, 1u, 0u,
                error, sizeof(error)) &&
                cetta_gslt_ground_dense_term_match_v1(
                    &workspace, &rigid, rigid_target, NULL) ==
                CETTA_GSLT_GROUND_DENSE_OK_V1,
            error[0] ? error : "zero-width rigid program is admitted");
        expect_true(
            !cetta_gslt_ground_dense_term_compile_v1(
                &rigid, rigid_source, UINT64_MAX, 2u,
                error, sizeof(error)),
            "a wrapping variable interval is rejected");
    }

    {
        Atom *consumer_slot = atom_var_with_id(&source, "capture", 301u);
        Atom *open = atom_var_with_id(&source, "open-source", 601u);
        Atom *open_arguments[] = {open};
        Atom *open_view = expression(
            &source, "source-wrapper", open_arguments, 1u);

        error[0] = '\0';
        expect_true(
            cetta_gslt_ground_dense_term_compile_v1(
                &capture, consumer_slot, 301u, 1u,
                error, sizeof(error)),
            error[0] ? error : "capture-shaped consumer compiles");
        expect_true(
            cetta_gslt_ground_dense_term_match_view_v1(
                &workspace, &capture, open_view,
                resolve_view_variable, NULL, NULL) ==
                CETTA_GSLT_GROUND_DENSE_DEFER_V1,
            "a consumer capture of an open source expression defers");
    }

    cetta_gslt_ground_dense_workspace_free_v1(&workspace);
    cetta_gslt_ground_dense_term_program_free_v1(&capture);
    cetta_gslt_ground_dense_term_program_free_v1(&rigid);
    cetta_gslt_ground_dense_term_program_free_v1(&missing_body);
    cetta_gslt_ground_dense_term_program_free_v1(&rewrite_body);
    cetta_gslt_ground_dense_term_program_free_v1(&rewrite_head);
    cetta_gslt_ground_dense_term_program_free_v1(&parser_body);
    cetta_gslt_ground_dense_term_program_free_v1(&parser_head);
    arena_free(&output);
    arena_free(&target);
    arena_free(&source);
    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("GsltGroundDenseTermV1Summary checks=%u failures=%u\n",
           checks, failures);
    return failures == 0u ? 0 : 1;
}
