/* Exercise the private selector and both existing executors, including code
 * mutations that a future source regeneration can produce. */
#ifndef RULE_PROGRAM_TEST_SOURCE
#define RULE_PROGRAM_TEST_SOURCE "../../src/rule_machine.c"
#endif
#include RULE_PROGRAM_TEST_SOURCE

#include <stdio.h>

static unsigned checks, failures;
#define CHECK(test, label) do { ++checks; if (!(test)) { \
    fprintf(stderr, "FAIL: %s\n", label); ++failures; } } while (0)

static Atom *code_node(Atom *code, uint32_t index) {
    Atom *node = code->expr.elems[1];
    while (index-- && rm_is_expr_head(node, "rule-program-cons", 3))
        node = node->expr.elems[2];
    return node;
}

static RMRuleProgramRule generated_rule(Arena *arena, RMRuleProgramRuleKind kind) {
    Atom *x = atom_var_with_id(arena, "x", fresh_var_id());
    Atom *schema_items[] = {atom_symbol(arena, "imp"), x, x};
    RMRuleProgramRule source = {
        .kind = kind,
        .id = atom_symbol(arena, kind == RM_RULE_PROGRAM_AXIOM ? "a" : "m"),
        .source = atom_symbol(arena, "native-guard-test"),
        .schema = rm_expr(arena, 3, schema_items),
        .proof_symbol = atom_symbol(arena, kind == RM_RULE_PROGRAM_AXIOM ? "ax" : "mp"),
    };
    RMRuleProgramRule result = {0};
    CHECK(rm_parse_rule_program_block(rm_rule_program_block(arena, &source), &result),
          "actual generated instructions compile and admit");
    return result;
}

static RMRuleProgramRule changed_rule(Arena *arena, const RMRuleProgramRule *rule) {
    RMRuleProgramRule copy = *rule;
    copy.code = atom_deep_copy(arena, rule->code);
    copy.native_eligible = false;
    return copy;
}

static void same_step(RMRuleProgramRule *rule, Atom *type, Atom *proof,
                      int32_t expected_add, bool succeeds) {
    RMRuleProgramRun native = {.native_backend = true}, bytecode = {0};
    arena_init(&native.scratch);
    arena_init(&bytecode.scratch);
    Atom *nt = NULL, *np = NULL, *bt = NULL, *bp = NULL;
    int32_t nh = -1, bh = -1;
    bool n = rm_rule_program_apply_rule(&native, rule, type, proof, &nt, &np, &nh);
    bool b = rm_rule_program_apply_rule(&bytecode, rule, type, proof, &bt, &bp, &bh);
    CHECK(n == succeeds && b == succeeds, "selected and instruction success agree");
    if (n && b) {
        CHECK(nh == expected_add && bh == expected_add,
              "both routes honor the actual hypothesis operand");
        CHECK(atom_alpha_eq(atom_expr2(&native.scratch, nt, np),
                            atom_expr2(&bytecode.scratch, bt, bp)),
              "type, proof and shared-variable structure agree modulo fresh names");
    }
    arena_free(&native.scratch);
    arena_free(&bytecode.scratch);
}

static void guard_mutations(Arena *arena, const RMRuleProgramRule *rule) {
    uint32_t count = rule->kind == RM_RULE_PROGRAM_AXIOM
        ? rm_generated_rule_program_axiom_op_count
        : rm_generated_rule_program_inverse_mp_op_count;
    for (uint32_t i = 0; i < count; ++i) {
        RMRuleProgramRule changed = changed_rule(arena, rule);
        Atom *node = code_node(changed.code, i);
        Atom *original = node->expr.elems[1];
        node->expr.elems[1] = atom_symbol(arena, "unknown-instruction");
        CHECK(!rm_rule_program_native_eligible(&changed), "changed opcode disables shortcut");
        if (original->kind == ATOM_EXPR) {
            for (CettaExprIndex j = 1; j < original->expr.len; ++j) {
                changed = changed_rule(arena, rule);
                node = code_node(changed.code, i);
                node->expr.elems[1]->expr.elems[j] = atom_symbol(arena, "changed-operand");
                CHECK(!rm_rule_program_native_eligible(&changed),
                      "each changed register, template, proof or integer disables shortcut");
            }
        }
        changed = changed_rule(arena, rule);
        node = code_node(changed.code, i);
        if (i == 0) changed.code->expr.elems[1] = node->expr.elems[2];
        else code_node(changed.code, i - 1)->expr.elems[2] = node->expr.elems[2];
        CHECK(!rm_rule_program_native_eligible(&changed), "missing instruction disables shortcut");
        changed = changed_rule(arena, rule);
        node = code_node(changed.code, i);
        Atom *extra_items[] = {atom_symbol(arena, "rule-program-cons"),
                              node->expr.elems[1], node->expr.elems[2]};
        node->expr.elems[2] = rm_expr(arena, 3, extra_items);
        CHECK(!rm_rule_program_native_eligible(&changed), "extra instruction disables shortcut");
        if (i + 1 < count) {
            changed = changed_rule(arena, rule);
            Atom *left = code_node(changed.code, i);
            Atom *right = code_node(changed.code, i + 1);
            Atom *swap = left->expr.elems[1];
            left->expr.elems[1] = right->expr.elems[1];
            right->expr.elems[1] = swap;
            CHECK(!rm_rule_program_native_eligible(&changed), "reordered instructions disable shortcut");
        }
    }
    RMRuleProgramRule changed = changed_rule(arena, rule);
    code_node(changed.code, count - 1)->expr.elems[2] = atom_symbol(arena, "bad-tail");
    CHECK(!rm_rule_program_native_eligible(&changed), "nonempty/malformed terminator disables shortcut");
    changed = *rule;
    changed.proof_symbol = atom_symbol(arena, "different-proof");
    CHECK(!rm_rule_program_native_eligible(&changed), "proof binding is checked");
}

static void same_search(Arena *arena, RMRuleProgramRule *rules, uint32_t count,
                        Atom *target, uint64_t states, uint64_t occurrences) {
    RMRuleProgramRun native = {
        .output = arena, .rules = rules, .rule_count = count, .target = target,
        .max_size = 7, .max_states = states, .max_occurrences = occurrences,
        .native_backend = true,
    };
    RMRuleProgramRun bytecode = native;
    bytecode.native_backend = false;
    arena_init(&native.scratch);
    arena_init(&bytecode.scratch);
    rm_rule_program_search(&native);
    rm_rule_program_search(&bytecode);
    CHECK(native.states == bytecode.states &&
          native.rule_attempts == bytecode.rule_attempts &&
          native.rule_successes == bytecode.rule_successes &&
          native.accepted == bytecode.accepted &&
          native.max_search_stack == bytecode.max_search_stack,
          "whole-search counters, occurrence counts and stack depth agree");
    CHECK((!native.limit_reason && !bytecode.limit_reason) ||
          (native.limit_reason && bytecode.limit_reason &&
           !strcmp(native.limit_reason, bytecode.limit_reason)),
          "completion/resource outcomes agree");
    CHECK(native.proofs.len == bytecode.proofs.len, "answer occurrence counts agree");
    for (uint32_t i = 0; i < native.proofs.len && i < bytecode.proofs.len; ++i)
        CHECK(atom_alpha_eq(native.proofs.items[i], bytecode.proofs.items[i]),
              "answer order and proof structure agree");
    rm_vec_free(&native.proofs); rm_vec_free(&bytecode.proofs);
    free(native.frames.items); free(bytecode.frames.items);
    arena_free(&native.scratch); arena_free(&bytecode.scratch);
}

static Atom *public_program(Arena *arena, RMRuleProgramRule *rules) {
    Atom *empty[] = {atom_symbol(arena, "rule-program-empty"),
                    atom_symbol(arena, "HilbertBFCProgramV1"),
                    atom_symbol(arena, RM_RULE_PROGRAM_GSLT_IDENTITY)};
    Atom *chain = rm_expr(arena, 3, empty);
    for (uint32_t i = 0; i < 2; ++i) {
        Atom *link[] = {atom_symbol(arena, "rule-program-link"), chain,
                       rm_rule_program_block(arena, &rules[i])};
        chain = rm_expr(arena, 3, link);
    }
    return rm_rule_program(arena, atom_symbol(arena, "r0"),
                           rm_delta(arena, 2, 2, 0), chain);
}

static Atom *public_run(Arena *arena, Atom *program, Atom *target,
                        uint32_t size, uint32_t states, bool incomplete) {
    Atom *args[] = {program, atom_int(arena, size), atom_int(arena, states),
                    atom_int(arena, 100), target};
    Atom *native = cetta_rule_machine_dispatch(arena,
        atom_symbol(arena, "compile:rule-program-run-native"), args, 5);
    Atom *bytecode = cetta_rule_machine_dispatch(arena,
        atom_symbol(arena, "compile:rule-program-run"), args, 5);
    CHECK(rm_is_expr_head(native, incomplete ? "compile-incomplete" : "rule-program-result",
                          incomplete ? 6 : 5), "public run has the intended result class");
    CHECK(atom_alpha_eq(native, bytecode), "public results, revisions and metrics agree");
    return native;
}

static void public_checks(Arena *arena, RMRuleProgramRule *rules,
                          Atom *target, Atom *new_target) {
    Atom *program = public_program(arena, rules);
    (void)public_run(arena, program, target, 7, 10000, false);
    (void)public_run(arena, program, target, 7, 1, true);
    Atom *before = public_run(arena, program, new_target, 1, 100, false);
    Atom *proof[] = {atom_symbol(arena, "rm-proof-atom"), atom_symbol(arena, "new-proof")};
    Atom *block[] = {atom_symbol(arena, "rm-block"), atom_symbol(arena, "new-id"),
                    atom_symbol(arena, "new-source"), rm_expr(arena, 2, proof),
                    atom_symbol(arena, "rm-nil"), new_target};
    Atom *args[] = {program, atom_symbol(arena, "r1"), rm_expr(arena, 6, block)};
    Atom *linked = cetta_rule_machine_dispatch(arena,
        atom_symbol(arena, "compile:rule-program-link"), args, 3);
    CHECK(rm_is_expr_head(linked, "rule-program-v1", 7), "public link admits the new axiom");
    Atom *after = public_run(arena, linked, new_target, 1, 100, false);
    Atom *old_again = public_run(arena, program, new_target, 1, 100, false);
    CHECK(atom_eq(before, old_again), "linking leaves the old revision unchanged");
    CHECK(rm_is_symbol(before->expr.elems[4], "r0") &&
          rm_is_symbol(after->expr.elems[4], "r1"), "revision identities remain distinct");
    CHECK(!atom_eq(before->expr.elems[2], after->expr.elems[2]),
          "new revision has a new answer, not just a changed revision label");
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols; g_var_intern = &variables; g_hashcons = NULL;
    Arena arena, source_arena;
    arena_init(&arena);
    arena_init(&source_arena);
    /* The runtime intentionally shares destination-owned immutable children
     * during deep copy.  Mutations must be in a different arena from sources. */
    RMRuleProgramRule rules[] = {generated_rule(&source_arena, RM_RULE_PROGRAM_AXIOM),
                                generated_rule(&source_arena, RM_RULE_PROGRAM_INVERSE_MP)};
    Atom *p = atom_symbol(&arena, "p"), *q = atom_symbol(&arena, "q");
    Atom *ii[] = {atom_symbol(&arena, "imp"), p, p};
    Atom *domain = rm_expr(&arena, 3, ii);
    Atom *type = rm_rule_program_fun(&arena, domain, p);
    Atom *proof = atom_symbol(&arena, "I");
    for (uint32_t i = 0; i < 2; ++i) {
        CHECK(rules[i].native_eligible, "unchanged generated sequence enables shortcut");
        guard_mutations(&arena, &rules[i]);
        same_step(&rules[i], type, proof, i ? 2 : 0, true);
        same_step(&rules[i], p, proof, 0, false);
    }
    Atom *bad_items[] = {atom_symbol(&arena, "imp"), p, q};
    same_step(&rules[0], rm_rule_program_fun(&arena, rm_expr(&arena, 3, bad_items), p),
              proof, 0, false);
    same_search(&arena, rules, 2, domain, 10000, 100);
    same_search(&arena, rules, 2, domain, 3, 100);
    same_search(&arena, rules, 2, domain, 10000, 0);
    public_checks(&arena, rules, domain, q);

    RMRuleProgramRule changed = changed_rule(&arena, &rules[1]);
    code_node(changed.code, 7)->expr.elems[1]->expr.elems[1] = atom_int(&arena, 1);
    changed.native_eligible = rm_rule_program_native_eligible(&changed);
    CHECK(!changed.native_eligible, "hyp-add=1 is interpreted, not forced to native hyp-add=2");
    same_step(&changed, type, proof, 1, true);
    same_search(&arena, &changed, 1, domain, 10000, 100);

    changed = changed_rule(&arena, &rules[0]);
    code_node(changed.code, 5)->expr.elems[1]->expr.elems[1] = atom_int(&arena, INT32_MAX);
    changed.native_eligible = rm_rule_program_native_eligible(&changed);
    CHECK(!changed.native_eligible, "largest allowed hyp-add is interpreted");
    same_step(&changed, type, proof, INT32_MAX, true);
    RMRuleProgramRule wide_rules[] = {rules[1], changed};
    same_search(&arena, wide_rules, 2, domain, 10000, 100);
    same_search(&arena, wide_rules, 2, domain, 3, 100);

    arena_free(&arena);
    arena_free(&source_arena);
    g_var_intern = NULL; var_intern_free(&variables);
    g_symbols = NULL; symbol_table_free(&symbols);
    printf("rule-program native guard: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
