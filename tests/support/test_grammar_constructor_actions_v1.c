#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_action_bytecode_v1.h"
#include "parser_action_primitive_v1.h"
#include "parser_pack_native_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Differential controls for typed exports, not a grammar-coverage test. */
static Atom *app(Arena *arena, const char *head, Atom **args, size_t len) {
    Atom **items = arena_alloc(arena, (len + 1u) * sizeof(*items));
    if (!items)
        return NULL;
    items[0] = atom_symbol(arena, head);
    for (size_t i = 0u; i < len; ++i)
        items[i + 1u] = args[i];
    return atom_expr(arena, items, len + 1u);
}

static bool head_is(const Atom *term, const char *head, uint32_t arity) {
    return term && term->kind == ATOM_EXPR && term->expr.len == arity + 1u &&
           atom_is_symbol(term->expr.elems[0], head);
}

static Atom *index_term(Arena *arena, uint32_t index) {
    Atom *value = atom_symbol(arena, "q-zero");
    for (uint32_t i = 0u; i < index; ++i)
        value = app(arena, "q-succ", &value, 1u);
    return value;
}

static bool answer_digest(Atom *answer, char digest[65]) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    CettaNativeSha256 sha;
    const char domain[] = "FiniteHornAnswerSetV1\n";
    if (!fh_ground_term_v1_render(answer, &bytes, &len, NULL, 0u))
        return false;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    cetta_native_sha256_update(&sha, bytes, len);
    cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    cetta_native_sha256_finish_hex(&sha, digest);
    free(bytes);
    return true;
}

/* Recompute the receipt after each mutation, so only source/code validation
 * (not a stale digest) can reject the changed instruction. */
static bool reject_primitive_mutations(Arena *arena, const PPABIV1Pack *pack,
                                       PPActionBytecodeV1Program *program,
                                       Atom *answer, const char *fixture_digest) {
    char error[512] = {0};
    char digest[65];
    Atom *code = answer->expr.elems[5];
    while (head_is(code, "pbc-cons", 2u)) {
        Atom *instruction = code->expr.elems[1];
        if (head_is(instruction, "pbc-primitive", 2u)) {
            Atom *operation = instruction->expr.elems[1];
            Atom *arity = instruction->expr.elems[2];
            bool rejected;
            instruction->expr.elems[1] = atom_symbol(arena, "unknown-primitive");
            rejected = answer_digest(answer, digest) &&
                !pp_action_bytecode_v1_program_build(pack, &answer, 1u,
                    fixture_digest, digest, program, error, sizeof(error));
            instruction->expr.elems[1] = operation;
            if (!rejected)
                return false;
            instruction->expr.elems[2] = atom_symbol(arena, "q-zero");
            rejected = answer_digest(answer, digest) &&
                !pp_action_bytecode_v1_program_build(pack, &answer, 1u,
                    fixture_digest, digest, program, error, sizeof(error));
            instruction->expr.elems[2] = arity;
            if (!rejected)
                return false;
            if (!atom_is_symbol(operation, "atom-symbol")) {
                instruction->expr.elems[1] = atom_symbol(arena,
                    atom_is_symbol(operation, "expression-cons")
                        ? "expression-append" : "expression-cons");
                rejected = answer_digest(answer, digest) &&
                    !pp_action_bytecode_v1_program_build(pack, &answer, 1u,
                        fixture_digest, digest, program, error, sizeof(error));
                instruction->expr.elems[1] = operation;
                if (!rejected)
                    return false;
            }
            return true;
        }
        code = code->expr.elems[2];
    }
    return true;
}

/* Oversized forged vectors must fail before their absent elements are read. */
static bool primitive_size_controls(void) {
    Arena arena;
    arena_init(&arena);
    /* A real expression header with a forged length. */
    Atom huge = *atom_expr(&arena, NULL, 0u);
    huge.expr.len = UINT64_MAX;
    huge.expr.elems = NULL;
    Atom *head = atom_symbol(&arena, "item");
    Atom *arguments[] = {head, &huge};
    Atom *result = NULL;
    bool ok = pp_action_primitive_v1_execute(
        atom_symbol(&arena, "expression-cons"), arguments, 2u, &arena, &result)
            == PP_ACTION_PRIMITIVE_V1_OVERFLOW && !result;
    huge.expr.len = SIZE_MAX / sizeof(Atom *);
    ok = ok && pp_action_primitive_v1_execute(
        atom_symbol(&arena, "expression-cons"), arguments, 2u, &arena, &result)
            == PP_ACTION_PRIMITIVE_V1_OVERFLOW && !result;
    arena_free(&arena);
    return ok;
}

static bool run_case(Atom *test, const char *fixture_digest) {
    Arena arena;
    PPABIV1Pack pack;
    PPActionBytecodeV1Program program;
    char error[512] = {0};
    char digest[65];
    bool ok = false;
    arena_init(&arena);
    ppabi_v1_pack_init(&pack);
    pp_action_bytecode_v1_program_init(&program);
    bool expect_rejection = head_is(test, "GrammarConstructorActionRejectV1", 6u);
    if (!head_is(test, "GrammarConstructorActionCaseV1", 6u) && !expect_rejection)
        goto done;
    Atom **fields = test->expr.elems;
    Atom *arity_atom = fields[2];
    if (arity_atom->kind != ATOM_GROUNDED || arity_atom->ground.gkind != GV_INT ||
        arity_atom->ground.ival < 0 || arity_atom->ground.ival >= UINT32_MAX)
        goto done;
    uint32_t arity = (uint32_t)arity_atom->ground.ival;
    Atom *slots = fields[5];
    if (!head_is(slots, "Slots", arity))
        goto done;
    Atom *items = atom_symbol(&arena, "pp-items-nil");
    Atom *any = atom_symbol(&arena, "pp-terminal-any");
    Atom *terminal = app(&arena, "pp-terminal", &any, 1u);
    for (uint32_t i = 0u; i < arity; ++i) {
        Atom *pair[] = {terminal, items};
        items = app(&arena, "pp-items-cons", pair, 2u);
    }
    Atom *production_fields[] = {fields[1], atom_symbol(&arena, "case-state"),
                                 items, fields[3]};
    Atom *production = app(&arena, "pp-production", production_fields, 4u);
    Atom *owner = atom_symbol(&arena, "typed-action-fixture");
    Atom *source_fields[] = {owner, production};
    Atom *source_answer = app(&arena, "compile-pack-production", source_fields, 2u);
    /* Structural fixture provenance only; no proof replay is claimed here. */
    PPABIV1DerivationInput derivation = {PPABI_V1_EVIDENCE_PRODUCTION,
        production, source_answer, atom_symbol(&arena, "fixture-certificate")};
    PPABIV1ProvenanceInput provenance = {fixture_digest, fixture_digest,
                                        fixture_digest, &derivation, 1u};
    if (!ppabi_v1_pack_load(&pack, &production, 1u, NULL, 0u, &provenance,
                           error, sizeof(error)))
        goto done;
    Atom *answer_fields[] = {owner, fields[1], index_term(&arena, arity),
                            fields[3], fields[4]};
    Atom *answer = app(&arena, "compile-pack-action-program", answer_fields, 5u);
    if (!answer_digest(answer, digest) ||
        !pp_action_bytecode_v1_program_build(&pack, &answer, 1u, fixture_digest,
                                             digest, &program, error, sizeof(error)))
        goto done;
    if (!reject_primitive_mutations(&arena, &pack, &program, answer, fixture_digest))
        goto done;
    if (!answer_digest(answer, digest))
        goto done;
    Atom *tree_result = NULL;
    Atom *compiled_result = NULL;
    if (expect_rejection) {
        bool tree_accepted = ppnative_v1_apply_action_term(
            &arena, fields[3], slots->expr.elems + 1u, arity,
            &tree_result, error, sizeof(error));
        bool code_accepted = pp_action_bytecode_v1_execute(
            &program, &pack, 0u, slots->expr.elems + 1u, arity,
            &arena, &compiled_result, error, sizeof(error));
        ok = !tree_accepted && !code_accepted;
        goto done;
    }
    if (!ppnative_v1_apply_action_term(&arena, fields[3], slots->expr.elems + 1u,
                                      arity, &tree_result, error, sizeof(error)) ||
        !pp_action_bytecode_v1_execute(&program, &pack, 0u, slots->expr.elems + 1u,
                                      arity, &arena, &compiled_result, error, sizeof(error)) ||
        !atom_eq(tree_result, fields[6]) || !atom_eq(compiled_result, fields[6]))
        goto done;
    /* Reject a wrong runtime arity before inspecting the borrowed slot vector. */
    if (pp_action_bytecode_v1_execute(&program, &pack, 0u, slots->expr.elems + 1u,
                                     arity + 1u, &arena, &compiled_result,
                                     error, sizeof(error)))
        goto done;
    /* A missing action cannot replace the complete admitted program. */
    if (pp_action_bytecode_v1_program_build(&pack, &answer, 0u, fixture_digest,
                                           digest, &program, error, sizeof(error)))
        goto done;
    /* A source action plus incomplete emitted code fails native admission. */
    answer->expr.elems[5] = atom_symbol(&arena, "pbc-nil");
    if (!answer_digest(answer, digest) ||
        pp_action_bytecode_v1_program_build(&pack, &answer, 1u, fixture_digest,
                                            digest, &program, error, sizeof(error)))
        goto done;
    /* Failed replacement left the old program operational. */
    ok = pp_action_bytecode_v1_execute(&program, &pack, 0u, slots->expr.elems + 1u,
                                      arity, &arena, &compiled_result, error, sizeof(error)) &&
         atom_eq(compiled_result, fields[6]);
done:
    if (!ok)
        fprintf(stderr, "grammar constructor action failed: %s\n", error);
    pp_action_bytecode_v1_program_free(&program);
    ppabi_v1_pack_free(&pack);
    arena_free(&arena);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: test_grammar_constructor_actions_v1 FIXTURES\n");
        return 2;
    }
    SymbolTable symbols;
    FHAnswerStreamV1 fixtures;
    char error[512] = {0};
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    fh_answer_stream_v1_init(&fixtures);
    bool ok = primitive_size_controls() &&
        fh_answer_stream_v1_read(&fixtures, argv[1], error, sizeof(error));
    size_t passed = 0u;
    if (!ok)
        fprintf(stderr, "%s\n", error);
    for (size_t i = 0u; ok && i < fixtures.len; ++i) {
        ok = run_case(fixtures.terms[i], fixtures.digest);
        passed += ok;
    }
    ok = ok && passed > 0u;
    printf("(GrammarConstructorActionsV1 cases=%zu accepted=%s)\n", passed, ok ? "true" : "false");
    fh_answer_stream_v1_free(&fixtures);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
