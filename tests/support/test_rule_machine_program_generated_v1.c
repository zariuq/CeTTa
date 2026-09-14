/* This unit is compiled against an actual newly generated header via a copy
 * of the production consumer. The default artifact is never changed. */
#ifndef RULE_PROGRAM_TEST_SOURCE
#error RULE_PROGRAM_TEST_SOURCE must identify the copied production consumer
#endif
#ifndef RULE_PROGRAM_REFERENCE_HEADER
#error RULE_PROGRAM_REFERENCE_HEADER must identify the retained comparison header
#endif
#include RULE_PROGRAM_TEST_SOURCE

static const char actual_digest[] = RM_RULE_PROGRAM_GSLT_DIGEST;
static const char actual_identity[] = RM_RULE_PROGRAM_GSLT_IDENTITY;
static const char actual_core_sha[] = RM_RULE_PROGRAM_CORE_SOURCE_SHA256;
static const char actual_program_sha[] = RM_RULE_PROGRAM_PROGRAM_SOURCE_SHA256;

#undef CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H
#undef RM_RULE_PROGRAM_GSLT_DIGEST
#undef RM_RULE_PROGRAM_GSLT_IDENTITY
#undef RM_RULE_PROGRAM_CORE_SOURCE_SHA256
#undef RM_RULE_PROGRAM_PROGRAM_SOURCE_SHA256
#define rm_generated_rule_program_axiom_ops retained_axiom_ops
#define rm_generated_rule_program_axiom_op_count retained_axiom_count
#define rm_generated_rule_program_inverse_mp_ops retained_inverse_ops
#define rm_generated_rule_program_inverse_mp_op_count retained_inverse_count
#include RULE_PROGRAM_REFERENCE_HEADER
#undef rm_generated_rule_program_axiom_ops
#undef rm_generated_rule_program_axiom_op_count
#undef rm_generated_rule_program_inverse_mp_ops
#undef rm_generated_rule_program_inverse_mp_op_count

static unsigned checks, failures;
#define CHECK(value, label) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL: %s\n", label); ++failures; } } while (0)

static void compare(const RMRuleProgramGeneratedOp *actual, uint32_t count,
                     const RMRuleProgramGeneratedOp *reference, uint32_t reference_count,
                     int inverse_hypotheses) {
    CHECK(count == reference_count, "retained instruction count preserved");
    for (uint32_t i = 0; i < count && i < reference_count; ++i) {
        CHECK(!strcmp(actual[i].opcode, reference[i].opcode), "retained opcode preserved");
        CHECK(actual[i].nargs == reference[i].nargs, "retained operand count preserved");
        for (size_t j = 0; j < 3; ++j) {
            const RMRuleProgramGeneratedOperand *a = &actual[i].operands[j];
            const RMRuleProgramGeneratedOperand *b = &reference[i].operands[j];
            CHECK(a->kind == b->kind, "retained operand kind preserved");
            CHECK((!a->symbol && !b->symbol) || (a->symbol && b->symbol &&
                !strcmp(a->symbol, b->symbol)), "retained operand symbol preserved");
            int64_t expected = inverse_hypotheses >= 0 && j == 0 &&
                !strcmp(actual[i].opcode, "rmbc-hyp-add") ? inverse_hypotheses : b->integer;
            CHECK(a->integer == expected, "actual authored integer, not a fixed template value");
        }
    }
}

static RMRuleProgramRule generated_rule(Arena *arena, RMRuleProgramRuleKind kind) {
    Atom *x = atom_var_with_id(arena, "x", fresh_var_id());
    Atom *imp[] = {atom_symbol(arena, "imp"), x, x};
    RMRuleProgramRule source = {
        .kind = kind,
        .id = atom_symbol(arena, kind == RM_RULE_PROGRAM_AXIOM ? "a" : "m"),
        .source = atom_symbol(arena, "generated-source-test"),
        .schema = rm_expr(arena, 3, imp),
        .proof_symbol = atom_symbol(arena, kind == RM_RULE_PROGRAM_AXIOM ? "ax" : "mp"),
    };
    RMRuleProgramRule result = {0};
    CHECK(rm_parse_rule_program_block(rm_rule_program_block(arena, &source), &result),
          "production loader admits newly generated instructions");
    return result;
}

static void execute(RMRuleProgramRule *rule, Atom *type, Atom *proof, int expected_hypotheses) {
    RMRuleProgramRun native = {.native_backend = true}, bytecode = {0};
    arena_init(&native.scratch); arena_init(&bytecode.scratch);
    Atom *nt = NULL, *np = NULL, *bt = NULL, *bp = NULL;
    int32_t nh = -1, bh = -1;
    bool n = rm_rule_program_apply_rule(&native, rule, type, proof, &nt, &np, &nh);
    bool b = rm_rule_program_apply_rule(&bytecode, rule, type, proof, &bt, &bp, &bh);
    CHECK(n && b, "both dispatched execution routes succeed");
    if (n && b) {
        CHECK(nh == expected_hypotheses && bh == expected_hypotheses,
              "both dispatched routes execute the authored hypothesis operand");
        CHECK(atom_alpha_eq(atom_expr2(&native.scratch, nt, np),
                            atom_expr2(&bytecode.scratch, bt, bp)),
              "type, proof, and shared variables agree modulo fresh names");
    }
    arena_free(&native.scratch); arena_free(&bytecode.scratch);
}

static Atom *program(Arena *arena, RMRuleProgramRule *rules) {
    Atom *empty[] = {atom_symbol(arena, "rule-program-empty"),
                    atom_symbol(arena, "HilbertBFCProgramV1"), atom_symbol(arena, actual_identity)};
    Atom *chain = rm_expr(arena, 3, empty);
    for (uint32_t i = 0; i < 2; ++i) {
        Atom *link[] = {atom_symbol(arena, "rule-program-link"), chain,
                       rm_rule_program_block(arena, &rules[i])};
        chain = rm_expr(arena, 3, link);
    }
    return rm_rule_program(arena, atom_symbol(arena, "r0"), rm_delta(arena, 2, 2, 0), chain);
}

static void public_run(Arena *arena, Atom *compiled, Atom *target, unsigned states) {
    Atom *args[] = {compiled, atom_int(arena, 5), atom_int(arena, states),
                    atom_int(arena, 100), target};
    Atom *native = cetta_rule_machine_dispatch(arena,
        atom_symbol(arena, "compile:rule-program-run-native"), args, 5);
    Atom *bytecode = cetta_rule_machine_dispatch(arena,
        atom_symbol(arena, "compile:rule-program-run"), args, 5);
    CHECK(rm_is_expr_head(native, "rule-program-result", 5) ||
          rm_is_expr_head(native, "compile-incomplete", 6), "public loader and search accept generated identity");
    CHECK(atom_alpha_eq(native, bytecode), "public ordered results, provenance and metrics agree");
}

int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "1") && strcmp(argv[1], "2"))) return 2;
    int hypotheses = argv[1][0] - '0';
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    g_hashcons = NULL;
    Arena arena; arena_init(&arena);
    CHECK(!strcmp(actual_core_sha, argv[2]), "exact core source byte identity");
    CHECK(!strcmp(actual_program_sha, argv[3]), "exact program source byte identity");
    const char prefix[] = "HilbertBFCProgramSpecializationV1-";
    CHECK(strlen(actual_digest) == 64 && !strncmp(actual_identity, prefix, sizeof(prefix) - 1u) &&
          !strcmp(actual_identity + sizeof(prefix) - 1u, actual_digest), "honest selected composition identity");
    compare(rm_generated_rule_program_axiom_ops, rm_generated_rule_program_axiom_op_count,
            retained_axiom_ops, retained_axiom_count, -1);
    compare(rm_generated_rule_program_inverse_mp_ops, rm_generated_rule_program_inverse_mp_op_count,
            retained_inverse_ops, retained_inverse_count, hypotheses);
    RMRuleProgramRule rules[] = {generated_rule(&arena, RM_RULE_PROGRAM_AXIOM),
                                generated_rule(&arena, RM_RULE_PROGRAM_INVERSE_MP)};
    CHECK(rules[0].native_eligible, "canonical axiom selects existing shortcut");
    CHECK(rules[1].native_eligible == (hypotheses == 2),
          "changed authored instructions disable shortcut at the actual loader");
    Atom *p = atom_symbol(&arena, "p");
    Atom *domain = atom_expr3(&arena, atom_symbol(&arena, "imp"), p, p);
    Atom *type = rm_rule_program_fun(&arena, domain, p);
    Atom *proof = atom_symbol(&arena, "seed");
    execute(&rules[0], type, proof, 0);
    execute(&rules[1], type, proof, hypotheses);
    Atom *compiled = program(&arena, rules);
    public_run(&arena, compiled, domain, 1);
    public_run(&arena, compiled, domain, 1000);
    printf("generated rule program: %u checks, %u failures; inverse hypotheses=%d\n",
           checks, failures, hypotheses);
    arena_free(&arena); var_intern_free(&variables); symbol_table_free(&symbols);
    g_symbols = NULL; g_var_intern = NULL;
    return failures ? 1 : 0;
}
