/* Scoped steps of the generic inference trace: one retained conclusion per
 * step, identity-memoized validation inside a scope, arena reclamation at
 * commit and abort, and memo invalidation when constructors are added. */
#include "atom.h"
#include "inference_checker.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition, message) \
    do { \
        if (condition) { \
            printf("PASS: %s\n", message); \
        } else { \
            printf("FAIL: %s\n", message); \
            failures++; \
        } \
    } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, text, &position);
    if (!atom || !parser_rest_is_delimiters(text, &position)) {
        fprintf(stderr, "cannot parse: %s\n", text);
        exit(2);
    }
    return atom;
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variable_names;
    Arena persistent, scratch;
    char error[256];

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&persistent);
    arena_init(&scratch);

    Atom *presentation = parse_one(&persistent,
        "(GInferenceLanguageV1 1 "
        " (LCons (CDecl \"Z\" 0) (LCons (CDecl \"S\" 1) LNil)) "
        " (LCons (JDecl \"Nat\" 1) LNil) "
        " (LCons (GRuleV1 \"zero\" LNil LNil (PApp \"Nat\" (LCons (PApp \"Z\" LNil) LNil)) LNil) "
        " (LCons (GRuleV1 \"succ\" (LCons (Formal \"n\" 0) LNil) "
        "   (LCons (PApp \"Nat\" (LCons (FVar \"n\") LNil)) LNil) "
        "   (PApp \"Nat\" (LCons (PApp \"S\" (LCons (FVar \"n\") LNil)) LNil)) LNil) "
        " (LCons (GRuleV1 \"any\" (LCons (Formal \"x\" 0) LNil) LNil "
        "   (PApp \"Nat\" (LCons (FVar \"x\") LNil)) LNil) LNil))) "
        " GNoConversion)");
    CettaInferenceChecker *checker = NULL;
    CHECK(cetta_inference_checker_create(presentation, &checker, error, sizeof error) ==
              CETTA_INFERENCE_OK, "presentation admitted");
    CettaInferenceRuleHandle zero = cetta_inference_checker_find_rule(checker, "zero");
    CettaInferenceRuleHandle succ = cetta_inference_checker_find_rule(checker, "succ");
    CettaInferenceRuleHandle any = cetta_inference_checker_find_rule(checker, "any");

    Atom *z = parse_one(&persistent, "(PApp \"Z\" LNil)");
    Atom *sz = parse_one(&persistent, "(PApp \"S\" (LCons (PApp \"Z\" LNil) LNil))");
    Atom *nat_z = parse_one(&persistent, "(PApp \"Nat\" (LCons (PApp \"Z\" LNil) LNil))");
    Atom *nat_sz = parse_one(&persistent,
        "(PApp \"Nat\" (LCons (PApp \"S\" (LCons (PApp \"Z\" LNil) LNil)) LNil))");

    CettaInferenceTrace trace;
    CettaInferenceTraceScope scope;
    size_t index0 = 99, index1 = 99;
    cetta_inference_trace_init(&trace, checker, &scratch);

    CHECK(cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, zero, NULL, 0u, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_scope_commit(&trace, &scope, nat_z, &index0, error, sizeof error) ==
                  CETTA_INFERENCE_OK,
          "a scope retains its single conclusion");
    CHECK(index0 == 0u && trace.stack_len == 0u && trace.saved_len == 1u,
          "commit clears the stack and saves the goal");

    CHECK(cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_reference(&trace, index0, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, succ, &z, 1u, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_scope_commit(&trace, &scope, nat_sz, &index1, error, sizeof error) ==
                  CETTA_INFERENCE_OK,
          "a later scope builds on a retained conclusion");

    CHECK(cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_reference(&trace, index1, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, succ, &sz, 1u, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_scope_commit(&trace, &scope, nat_z, NULL, error, sizeof error) ==
                  CETTA_INFERENCE_FINAL_MISMATCH,
          "commit refuses a goal the scope did not conclude");
    CHECK(trace.stack_len == 0u && trace.saved_len == 2u && !trace.scope_active,
          "a refused commit restores the state at scope begin");

    CHECK(cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, zero, NULL, 0u, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_save(&trace, error, sizeof error) == CETTA_INFERENCE_OK,
          "work inside a scope");
    CettaInferenceTraceScope inner;
    CHECK(cetta_inference_trace_scope_begin(&trace, &inner, error, sizeof error) ==
              CETTA_INFERENCE_MALFORMED_PROOF,
          "scopes do not nest");
    cetta_inference_trace_scope_abort(&trace, &scope);
    CHECK(trace.stack_len == 0u && trace.saved_len == 2u && !trace.scope_active,
          "abort discards the scope's stack entries and saved values");

    size_t before = arena_accounted_live_bytes(&scratch);
    bool all_ok = true;
    for (int round = 0; round < 1000; round++) {
        size_t unused;
        all_ok &= cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK;
        all_ok &= cetta_inference_trace_reference(&trace, index0, error, sizeof error) == CETTA_INFERENCE_OK;
        all_ok &= cetta_inference_trace_apply(&trace, succ, &z, 1u, error, sizeof error) == CETTA_INFERENCE_OK;
        all_ok &= cetta_inference_trace_scope_commit(&trace, &scope, nat_sz, &unused, error, sizeof error) ==
                  CETTA_INFERENCE_OK;
    }
    CHECK(all_ok && arena_accounted_live_bytes(&scratch) == before,
          "a thousand scopes leave the scratch arena at its prior size");

    Atom *opaque = parse_one(&persistent, "(PApp \"Opaque\" LNil)");
    CHECK(cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, any, &opaque, 1u, error, sizeof error) == CETTA_INFERENCE_OK,
          "undeclared nullary data is opaque inside a scope");
    CHECK(cetta_inference_checker_add_constructor(checker, "Opaque", 1u, error, sizeof error) ==
              CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, any, &opaque, 1u, error, sizeof error) ==
                  CETTA_INFERENCE_INVALID_ARGUMENTS,
          "a grown constructor signature invalidates the scope memo");
    cetta_inference_trace_scope_abort(&trace, &scope);

    /* Foreign-atom validation remembered across scopes, dropped on growth. */
    cetta_inference_trace_retain_foreign_validation(&trace, true);
    Atom *opaque2 = parse_one(&persistent, "(PApp \"Opaque2\" LNil)");
    Atom *nat_opaque2 = parse_one(&persistent, "(PApp \"Nat\" (LCons (PApp \"Opaque2\" LNil) LNil))");
    CHECK(cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, any, &opaque2, 1u, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_scope_commit(&trace, &scope, nat_opaque2, NULL, error, sizeof error) ==
                  CETTA_INFERENCE_OK,
          "retained foreign validation accepts a persistent argument");
    CHECK(cetta_inference_checker_add_constructor(checker, "Opaque2", 1u, error, sizeof error) ==
              CETTA_INFERENCE_OK &&
              cetta_inference_trace_scope_begin(&trace, &scope, error, sizeof error) == CETTA_INFERENCE_OK &&
              cetta_inference_trace_apply(&trace, any, &opaque2, 1u, error, sizeof error) ==
                  CETTA_INFERENCE_INVALID_ARGUMENTS,
          "a grown constructor signature invalidates retained foreign validation");
    cetta_inference_trace_scope_abort(&trace, &scope);

    cetta_inference_trace_free(&trace);
    cetta_inference_checker_destroy(checker);
    arena_free(&scratch);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    if (failures) {
        printf("FAIL: inference trace scopes (%d failures)\n", failures);
        return 1;
    }
    printf("PASS: inference trace scopes\n");
    return 0;
}
