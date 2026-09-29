#include "experiments/gslt2parse_foundation/native/parser_pack_table_snapshot_v1.h"
#include "native/tptp_official_snapshot_v1.h"
#include "lib_parse_native_grammar.h"
#include "regular_span_dfa_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

static void expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
}

static bool copy_pack_replace_first(const char *source_path,
                                    const char *target_path,
                                    const char *old_text,
                                    const char *new_text) {
    FILE *source = NULL;
    FILE *target = NULL;
    char *bytes = NULL;
    char *found;
    long size;
    size_t prefix_size;
    size_t suffix_offset;
    size_t suffix_size;
    size_t old_size;
    size_t new_size;
    bool ok = false;
    source = fopen(source_path, "rb");
    if (!source || fseek(source, 0, SEEK_END) != 0)
        goto done;
    size = ftell(source);
    if (size <= 0 || fseek(source, 0, SEEK_SET) != 0)
        goto done;
    bytes = malloc((size_t)size + 1u);
    if (!bytes || fread(bytes, 1u, (size_t)size, source) != (size_t)size)
        goto done;
    bytes[size] = '\0';
    old_size = strlen(old_text);
    new_size = strlen(new_text);
    found = strstr(bytes, old_text);
    if (!found || old_size == 0u)
        goto done;
    prefix_size = (size_t)(found - bytes);
    suffix_offset = prefix_size + old_size;
    suffix_size = (size_t)size - suffix_offset;
    target = fopen(target_path, "wb");
    if (!target ||
        fwrite(bytes, 1u, prefix_size, target) != prefix_size ||
        fwrite(new_text, 1u, new_size, target) != new_size ||
        fwrite(bytes + suffix_offset, 1u, suffix_size, target) != suffix_size)
        goto done;
    ok = fclose(target) == 0;
    target = NULL;
done:
    if (target)
        fclose(target);
    if (source)
        fclose(source);
    free(bytes);
    return ok;
}

static bool grammar_ab(CettaLpNativeGrammar *grammar,
                       SymbolId label,
                       SymbolId start,
                       SymbolId first,
                       SymbolId second) {
    CettaLpNativeProduction *production;
    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(1u, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = 1u;
    production = &grammar->productions[0];
    production->label = label;
    production->lhs = start;
    production->rhs = calloc(2u, sizeof(*production->rhs));
    if (!production->rhs)
        return false;
    production->rhs_len = 2u;
    production->rhs[0] = (CettaLpNativeSymbol){
        CETTA_LP_NATIVE_SYMBOL_TM, first, 0u};
    production->rhs[1] = (CettaLpNativeSymbol){
        CETTA_LP_NATIVE_SYMBOL_TM, second, 0u};
    return true;
}

static bool result_is_app(const Atom *atom, const char *head, uint32_t arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
           atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool count_tptp_input(Atom *input, void *user) {
    uint32_t *count = user;
    if (!input || !count)
        return false;
    (*count)++;
    return true;
}

static void test_empty_observation_spans(TestCounts *counts, Arena *arena) {
    const CettaTptpLexTokenV1 tokens[] = {
        {0u, 1u, 0u, 0u, 1u}, {4u, 1u, 0u, 4u, 5u}};
    for (uint32_t position = 0u; position <= 2u; ++position) {
        Atom *fields[] = {atom_symbol(arena, "NodeC"),
            atom_symbol(arena, "empty#"), atom_symbol(arena, "empty"),
            atom_int(arena, position), atom_int(arena, position),
            atom_symbol(arena, "Nil")};
        Atom *observed = cetta_tptp_observe_derivation_v1(
            NULL, arena, atom_expr(arena, fields, 6u), tokens, 2u, "a   b", 5u);
        int64_t expected = position == 0u ? 0 : position == 1u ? 4 : 5;
        expect(counts,
            result_is_app(observed, "tptp-cst:spanned-node", 4u) &&
            observed->expr.elems[2]->ground.ival == expected &&
            observed->expr.elems[3]->ground.ival == expected,
            "empty production has a zero-width span, including across layout");
    }
    expect(counts,
        cetta_tptp_observe_derivation_v1(NULL, arena, NULL, tokens, 2u,
                                       "a   b", 5u) == NULL,
        "missing observation derivation is rejected");
}

static bool set_production(CettaLpNativeProduction *production,
                           SymbolId label,
                           SymbolId lhs,
                           const CettaLpNativeSymbol *rhs,
                           uint32_t rhs_len) {
    production->label = label;
    production->lhs = lhs;
    production->rhs_len = rhs_len;
    if (rhs_len == 0u)
        return true;
    production->rhs = calloc(rhs_len, sizeof(*production->rhs));
    if (!production->rhs)
        return false;
    memcpy(production->rhs, rhs, rhs_len * sizeof(*rhs));
    return true;
}

static bool grammar_backtracking(CettaLpNativeGrammar *grammar,
                                 Arena *arena,
                                 SymbolId *start_out,
                                 Atom **tokens_out) {
    SymbolId start = atom_symbol(arena, "bt-start")->sym_id;
    SymbolId a_nt = atom_symbol(arena, "bt-a-nt")->sym_id;
    Atom *a = atom_symbol(arena, "bt-a");
    Atom *b = atom_symbol(arena, "bt-b");
    Atom *c = atom_symbol(arena, "bt-c");
    Atom *d = atom_symbol(arena, "bt-d");
    CettaLpNativeSymbol success_rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, a_nt, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, c->sym_id, 0u},
    };
    CettaLpNativeSymbol failing_rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, a_nt, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, b->sym_id, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, d->sym_id, 0u},
    };
    CettaLpNativeSymbol short_rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, a->sym_id, 0u},
    };
    CettaLpNativeSymbol long_rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, a->sym_id, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, b->sym_id, 0u},
    };
    Atom *nil = atom_symbol(arena, "Nil");

    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(4u, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = 4u;
    if (!set_production(&grammar->productions[0],
                        atom_symbol(arena, "bt-success-prod")->sym_id,
                        start, success_rhs, 2u) ||
        !set_production(&grammar->productions[1],
                        atom_symbol(arena, "bt-failing-prod")->sym_id,
                        start, failing_rhs, 3u) ||
        !set_production(&grammar->productions[2],
                        atom_symbol(arena, "bt-short-prod")->sym_id,
                        a_nt, short_rhs, 1u) ||
        !set_production(&grammar->productions[3],
                        atom_symbol(arena, "bt-long-prod")->sym_id,
                        a_nt, long_rhs, 2u)) {
        return false;
    }
    *start_out = start;
    *tokens_out = atom_expr3(
        arena, atom_symbol(arena, "Cons"), a,
        atom_expr3(arena, atom_symbol(arena, "Cons"), b,
                   atom_expr3(arena, atom_symbol(arena, "Cons"), c, nil)));
    return true;
}

static bool grammar_ambiguous(CettaLpNativeGrammar *grammar,
                              Arena *arena,
                              SymbolId *start_out,
                              Atom **tokens_out) {
    SymbolId start = atom_symbol(arena, "amb-start")->sym_id;
    SymbolId left = atom_symbol(arena, "amb-left")->sym_id;
    SymbolId right = atom_symbol(arena, "amb-right")->sym_id;
    Atom *a = atom_symbol(arena, "amb-a");
    CettaLpNativeSymbol start_left[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, left, 0u},
    };
    CettaLpNativeSymbol start_right[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, right, 0u},
    };
    CettaLpNativeSymbol leaf[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, a->sym_id, 0u},
    };

    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(4u, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = 4u;
    if (!set_production(&grammar->productions[0],
                        atom_symbol(arena, "amb-start-left")->sym_id,
                        start, start_left, 1u) ||
        !set_production(&grammar->productions[1],
                        atom_symbol(arena, "amb-start-right")->sym_id,
                        start, start_right, 1u) ||
        !set_production(&grammar->productions[2],
                        atom_symbol(arena, "amb-left-leaf")->sym_id,
                        left, leaf, 1u) ||
        !set_production(&grammar->productions[3],
                        atom_symbol(arena, "amb-right-leaf")->sym_id,
                        right, leaf, 1u)) {
        return false;
    }
    *start_out = start;
    *tokens_out = atom_expr3(arena, atom_symbol(arena, "Cons"), a,
                             atom_symbol(arena, "Nil"));
    return true;
}

int main(void) {
    static const CettaLpNativeUnicodeRange a_range[] = {{97u, 97u}};
    static const uint32_t starts[] = {0u};
    static const RSDFAV1NfaEdge edges[] = {
        {0u, 1u, RSDFA_V1_NFA_RANGES, a_range, 1u},
        {1u, 1u, RSDFA_V1_NFA_RANGES, a_range, 1u},
    };
    static const RSDFAV1NfaAccept accepts[] = {{1u, 0u}};
    static const char digest[] =
        "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6";
    SymbolTable symbols;
    TestCounts counts = {0};
    Arena arena;
    RSDFAV1Nfa nfa = {
        .state_len = 2u,
        .start_states = starts,
        .start_len = 1u,
        .edges = edges,
        .edge_len = 2u,
        .accepts = accepts,
        .accept_len = 1u,
        .tag_len = 1u,
    };
    RSDFAV1Plan plan;
    RSDFAV1Program program;
    RSDFAV1BuildOutcome build_outcome;
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrPrepared prepared;
    CettaLpNativeSlrProgram slr;
    PPTableSnapshotV1 snap;
    PPTableSnapshotV1 loaded;
    Atom *start;
    Atom *first;
    Atom *second;
    char error[512] = {0};
    char path[160];
    char *tag;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    test_empty_observation_spans(&counts, &arena);
    rsdfa_v1_plan_init(&plan);
    rsdfa_v1_program_init(&program);
    cetta_lp_native_slr_prepared_init(&prepared);
    cetta_lp_native_slr_program_init(&slr);
    pp_table_snapshot_v1_init(&snap);
    pp_table_snapshot_v1_init(&loaded);
    snprintf(path, sizeof(path),
             "runtime/bootstrap/tpp1-test.%ld.bin", (long)getpid());

    expect(&counts,
           rsdfa_v1_plan_build(&nfa, 128u, 1024u, &plan, &build_outcome,
                               error, sizeof(error)) &&
               build_outcome == RSDFA_V1_BUILD_COMPLETED,
           error[0] ? error : "DFA plan build");
    error[0] = '\0';
    expect(&counts,
           rsdfa_v1_plan_export_program(&plan, &program, error, sizeof(error)) &&
               rsdfa_v1_program_validate(&program, error, sizeof(error)),
           error[0] ? error : "DFA program export");
    error[0] = '\0';

    start = atom_symbol(&arena, "snap-start");
    first = atom_symbol(&arena, "snap-first");
    second = atom_symbol(&arena, "snap-second");
    expect(&counts,
           grammar_ab(&grammar,
                      atom_symbol(&arena, "snap-prod")->sym_id,
                      start->sym_id, first->sym_id, second->sym_id),
           "build SLR grammar");
    expect(&counts,
           cetta_lp_native_slr_prepare(&prepared, &grammar, start->sym_id,
                                       error, sizeof(error)),
           error[0] ? error : "prepare SLR");
    error[0] = '\0';
    expect(&counts,
           cetta_lp_native_slr_prepared_export_program(
               &prepared, &slr, error, sizeof(error)) &&
               cetta_lp_native_slr_program_validate(&slr, error, sizeof(error)),
           error[0] ? error : "export SLR program");
    error[0] = '\0';
    expect(&counts, slr.summary.conflict_len == 0u, "tiny grammar is LR");

    {
        CettaLpNativeGrammar bt_grammar;
        CettaLpNativeSlrPrepared bt_prepared;
        CettaLpNativeSlrProgram bt_program;
        SymbolId bt_start = 0u;
        Atom *bt_tokens = NULL;
        Atom *bt_result = NULL;

        cetta_lp_native_slr_prepared_init(&bt_prepared);
        cetta_lp_native_slr_program_init(&bt_program);
        expect(&counts,
               grammar_backtracking(&bt_grammar, &arena, &bt_start,
                                    &bt_tokens),
               "build shift/reduce backtracking grammar");
        error[0] = '\0';
        expect(&counts,
               cetta_lp_native_slr_prepare_glr(
                   &bt_prepared, &bt_grammar, bt_start, error, sizeof(error)) &&
               cetta_lp_native_slr_prepared_export_program(
                   &bt_prepared, &bt_program, error, sizeof(error)) &&
               bt_program.summary.conflict_len > 0u,
               error[0] ? error : "prepare conflicted GLR program");
        error[0] = '\0';
        bt_result = cetta_lp_native_slr_program_parse_shared(
            &bt_program, bt_tokens, &arena, error, sizeof(error));
        expect(&counts, result_is_app(bt_result, "Unique", 1u),
               error[0] ? error
                        : "GLR explores the branch that succeeds after conflict");
        cetta_lp_native_slr_program_free(&bt_program);
        cetta_lp_native_slr_prepared_free(&bt_prepared);
        cetta_lp_native_grammar_free(&bt_grammar);
    }

    {
        CettaLpNativeGrammar amb_grammar;
        CettaLpNativeSlrPrepared amb_prepared;
        CettaLpNativeSlrProgram amb_program;
        SymbolId amb_start = 0u;
        Atom *amb_tokens = NULL;
        Atom *amb_result = NULL;
        uint64_t work_used = 0u;
        uint64_t descriptors_used = 0u;

        cetta_lp_native_slr_prepared_init(&amb_prepared);
        cetta_lp_native_slr_program_init(&amb_program);
        expect(&counts,
               grammar_ambiguous(&amb_grammar, &arena, &amb_start,
                                 &amb_tokens),
               "build ambiguous grammar");
        error[0] = '\0';
        expect(&counts,
               cetta_lp_native_slr_prepare_glr(
                   &amb_prepared, &amb_grammar, amb_start,
                   error, sizeof(error)) &&
               cetta_lp_native_slr_prepared_export_program(
                   &amb_prepared, &amb_program, error, sizeof(error)) &&
               amb_program.summary.conflict_len > 0u,
               error[0] ? error : "prepare ambiguous GLR program");
        error[0] = '\0';
        amb_result = cetta_lp_native_slr_program_parse_shared_counted(
            &amb_program, amb_tokens, 0u, &work_used,
            &arena, error, sizeof(error));
        expect(&counts,
               amb_result && amb_result->kind == ATOM_SYMBOL &&
                   atom_is_symbol(amb_result, "Ambiguous") && work_used > 0u,
               error[0] ? error : "GLR preserves ambiguity");
        error[0] = '\0';
        amb_result = cetta_lp_native_slr_program_parse_shared_counted(
            &amb_program, amb_tokens, 1u, &work_used,
            &arena, error, sizeof(error));
        expect(&counts,
               result_is_app(amb_result, "ResourceLimit", 2u),
               error[0] ? error
                        : "GLR resource exhaustion is not syntax rejection");
        error[0] = '\0';
        amb_result = cetta_lp_native_gll_parse_shared_counted(
            &amb_grammar, amb_start, amb_tokens, 0u, &descriptors_used,
            &arena, error, sizeof(error));
        expect(&counts,
               amb_result && amb_result->kind == ATOM_SYMBOL &&
                   atom_is_symbol(amb_result, "Ambiguous") &&
                   descriptors_used > 0u,
               error[0] ? error : "bounded GLL preserves ambiguity");
        error[0] = '\0';
        amb_result = cetta_lp_native_gll_parse_shared_counted(
            &amb_grammar, amb_start, amb_tokens, 1u, &descriptors_used,
            &arena, error, sizeof(error));
        expect(&counts,
               result_is_app(amb_result, "ResourceLimit", 2u) &&
                   descriptors_used == 1u,
               error[0] ? error
                        : "GLL descriptor exhaustion is not syntax rejection");
        {
            /* Avoided productions: a derivation reducing fewer of them is
             * the reading; derivations tied at the fewest stay ambiguous.
             * Rows mark productions start-left, start-right, left-leaf and
             * right-leaf; the reading is 'L' (left), 'R' or '?' (ambiguous). */
            static const struct {
                uint8_t avoided[4];
                char reading;
            } cases[] = {
                {{0u, 0u, 0u, 1u}, 'L'},
                {{0u, 1u, 0u, 0u}, 'L'},
                {{0u, 0u, 1u, 0u}, 'R'},
                {{0u, 1u, 1u, 1u}, 'L'},
                {{0u, 0u, 1u, 1u}, '?'},
                {{0u, 1u, 1u, 0u}, '?'},
                {{1u, 1u, 1u, 1u}, '?'},
                {{0u, 0u, 0u, 0u}, '?'},
            };
            for (size_t c = 0u; c < sizeof(cases) / sizeof(cases[0]); c++) {
                char label[96];
                for (uint32_t k = 0u; k < 4u; k++)
                    amb_program.productions[k].avoided =
                        cases[c].avoided[k] != 0u;
                for (int engine = 0; engine < 2; engine++) {
                    const char *printed;
                    bool as_expected;
                    error[0] = '\0';
                    amb_result = engine == 0
                        ? cetta_lp_native_slr_program_parse_shared_counted(
                              &amb_program, amb_tokens, 0u, &work_used,
                              &arena, error, sizeof(error))
                        : cetta_lp_native_gll_parse_avoiding_counted(
                              &amb_grammar, amb_start, amb_tokens, 0u,
                              &descriptors_used, cases[c].avoided,
                              &arena, error, sizeof(error));
                    printed = amb_result
                        ? atom_to_parseable_string(&arena, amb_result) : "";
                    if (cases[c].reading == '?')
                        as_expected = amb_result &&
                            amb_result->kind == ATOM_SYMBOL &&
                            atom_is_symbol(amb_result, "Ambiguous");
                    else
                        as_expected =
                            result_is_app(amb_result, "Unique", 1u) &&
                            strstr(printed, cases[c].reading == 'L'
                                                ? "amb-left-leaf"
                                                : "amb-right-leaf") &&
                            !strstr(printed, cases[c].reading == 'L'
                                                 ? "amb-right-leaf"
                                                 : "amb-left-leaf");
                    snprintf(label, sizeof(label),
                             "%s avoid case %zu reads %c",
                             engine == 0 ? "GLR" : "GLL", c,
                             cases[c].reading);
                    expect(&counts, as_expected, error[0] ? error : label);
                }
            }
            for (uint32_t k = 0u; k < 4u; k++)
                amb_program.productions[k].avoided = false;
        }
        cetta_lp_native_slr_program_free(&amb_program);
        cetta_lp_native_slr_prepared_free(&amb_prepared);
        cetta_lp_native_grammar_free(&amb_grammar);
    }

    memcpy(snap.syntax_digest, digest, 65u);
    memcpy(snap.artifact_digest, digest, 65u);
    memcpy(snap.profile, "unit", sizeof("unit"));
    snap.kernel = PP_TABLE_SNAPSHOT_V1_KERNEL_SLR;
    snap.conflict_len = slr.summary.conflict_len;
    snap.dfa = program;
    memset(&program, 0, sizeof(program));
    snap.slr = slr;
    memset(&slr, 0, sizeof(slr));
    tag = malloc(2u);
    expect(&counts, tag != NULL, "tag name");
    if (tag) {
        tag[0] = 'a';
        tag[1] = '\0';
        snap.tag_names = malloc(sizeof(*snap.tag_names));
        snap.tag_names[0] = tag;
        snap.tag_name_len = 1u;
    }

    expect(&counts,
           pp_table_snapshot_v1_write_path(&snap, path, error, sizeof(error)),
           error[0] ? error : "write snapshot path");
    error[0] = '\0';
    expect(&counts,
           pp_table_snapshot_v1_read_path(&loaded, path, error, sizeof(error)),
           error[0] ? error : "read snapshot path");
    error[0] = '\0';
    expect(&counts, strcmp(loaded.syntax_digest, digest) == 0, "digest roundtrip");
    expect(&counts, strcmp(loaded.artifact_digest, digest) == 0,
           "artifact digest roundtrip");
    expect(&counts, strcmp(loaded.profile, "unit") == 0,
           "profile roundtrip");
    expect(&counts, loaded.kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_SLR, "kernel SLR");
    expect(&counts, loaded.conflict_len == 0u, "conflict count");
    expect(&counts, loaded.tag_name_len == 1u && loaded.tag_names &&
                        strcmp(loaded.tag_names[0], "a") == 0,
           "tag name roundtrip");
    expect(&counts,
           rsdfa_v1_program_validate(&loaded.dfa, error, sizeof(error)),
           error[0] ? error : "loaded DFA validates");
    error[0] = '\0';
    expect(&counts,
           cetta_lp_native_slr_program_validate(&loaded.slr, error, sizeof(error)),
           error[0] ? error : "loaded SLR validates");
    expect(&counts, loaded.dfa.state_len == snap.dfa.state_len,
           "DFA state_len");
    expect(&counts, loaded.slr.production_len == snap.slr.production_len,
           "SLR production_len");
    expect(&counts, loaded.slr.start_nonterminal == start->sym_id,
           "start interned");
    {
        PPTableSnapshotV1 mismatch;
        char bad[65];
        memset(&mismatch, 0, sizeof(mismatch));
        pp_table_snapshot_v1_init(&mismatch);
        memset(bad, '0', 64);
        bad[64] = '\0';
        expect(&counts,
               !cetta_tptp_snapshot_load_v1(&mismatch, path, bad, error,
                                            sizeof(error)) &&
                   strstr(error, "DigestMismatch") != NULL,
               "wrong digest is TPTP:DigestMismatch");
        pp_table_snapshot_v1_free(&mismatch);
    }

    unlink(path);

    {
        static const char pack_path[] =
            "runtime/tptp-official-reader/"
            "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6/"
            "pack.sexpr";
        char out_path[160];
        char changed_pack_path[160];
        char changed_out_path[160];
        char changed_profile_pack_path[160];
        char changed_profile_out_path[160];
        char changed_role_pack_path[160];
        char changed_role_out_path[160];
        char changed_source_path_pack_path[160];
        char changed_source_path_out_path[160];
        char changed_digest[65];
        char official_artifact_digest[65];
        static const char sample[] = "fof(a,axiom,p(X)).\n";
        static const char quoted[] = "% hi\ncnf('cat',axiom, q).\n";
        PPTableSnapshotV1 official;
        CettaTptpLexTokenV1 toks[32];
        uint32_t ntok = 0u;
        uint32_t ti;
        bool has_lw = false;
        bool has_fof = false;
        bool has_iff = false;
        bool has_sq = false;
        bool has_uw = false;
        memset(changed_digest, '0', 64u);
        changed_digest[64] = '\0';
        pp_table_snapshot_v1_init(&official);
        snprintf(out_path, sizeof(out_path),
                 "runtime/bootstrap/tables-official.%ld.tpp1",
                 (long)getpid());
        snprintf(changed_pack_path, sizeof(changed_pack_path),
                 "runtime/bootstrap/pack-changed.%ld.sexpr", (long)getpid());
        snprintf(changed_out_path, sizeof(changed_out_path),
                 "runtime/bootstrap/tables-changed.%ld.tpp1", (long)getpid());
        snprintf(changed_profile_pack_path, sizeof(changed_profile_pack_path),
                 "runtime/bootstrap/pack-profile-changed.%ld.sexpr",
                 (long)getpid());
        snprintf(changed_profile_out_path, sizeof(changed_profile_out_path),
                 "runtime/bootstrap/tables-profile-changed.%ld.tpp1",
                 (long)getpid());
        snprintf(changed_role_pack_path, sizeof(changed_role_pack_path),
                 "runtime/bootstrap/pack-role-changed.%ld.sexpr",
                 (long)getpid());
        snprintf(changed_role_out_path, sizeof(changed_role_out_path),
                 "runtime/bootstrap/tables-role-changed.%ld.tpp1",
                 (long)getpid());
        snprintf(changed_source_path_pack_path,
                 sizeof(changed_source_path_pack_path),
                 "runtime/bootstrap/pack-source-path-changed.%ld.sexpr",
                 (long)getpid());
        snprintf(changed_source_path_out_path,
                 sizeof(changed_source_path_out_path),
                 "runtime/bootstrap/tables-source-path-changed.%ld.tpp1",
                 (long)getpid());
        error[0] = '\0';
        expect(&counts,
               cetta_tptp_snapshot_construct_from_pack_v1(
                   pack_path, NULL, out_path, error, sizeof(error)),
               error[0] ? error : "construct official token DFA");
        error[0] = '\0';
        expect(&counts,
               cetta_tptp_snapshot_load_v1(
                   &official, out_path,
                   CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, error,
                   sizeof(error)),
               error[0] ? error : "load official snapshot");
        memcpy(official_artifact_digest, official.artifact_digest, 65u);
        expect(&counts, strcmp(official.profile, "strict") == 0,
               "official snapshot declares strict profile");
        expect(&counts,
               copy_pack_replace_first(
                   pack_path, changed_pack_path,
                   CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, changed_digest),
               "prepare changed-digest pack");
        error[0] = '\0';
        expect(&counts,
               cetta_tptp_snapshot_construct_from_pack_v1(
                   changed_pack_path, NULL, changed_out_path, error, sizeof(error)),
               error[0] ? error : "construct changed-digest snapshot");
        {
            PPTableSnapshotV1 changed;
            PPTableSnapshotV1 mismatch;
            pp_table_snapshot_v1_init(&changed);
            pp_table_snapshot_v1_init(&mismatch);
            error[0] = '\0';
            expect(&counts,
                   cetta_tptp_snapshot_load_v1(
                       &changed, changed_out_path, changed_digest,
                       error, sizeof(error)) &&
                       strcmp(changed.syntax_digest, changed_digest) == 0 &&
                       strcmp(changed.profile, "strict") == 0 &&
                       strcmp(changed.artifact_digest,
                              official_artifact_digest) != 0,
                   error[0] ? error
                            : "snapshot identity comes from manifest and grammar");
            error[0] = '\0';
            expect(&counts,
                   !cetta_tptp_snapshot_load_v1(
                       &mismatch, changed_out_path,
                       CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
                       error, sizeof(error)) &&
                       strstr(error, "DigestMismatch") != NULL,
                   "changed pack cannot masquerade as official snapshot");
            pp_table_snapshot_v1_free(&changed);
            pp_table_snapshot_v1_free(&mismatch);
        }
        expect(&counts,
               copy_pack_replace_first(
                   pack_path, changed_profile_pack_path,
                   " strict ", " corpus-compatible "),
               "prepare changed-profile pack");
        error[0] = '\0';
        expect(&counts,
               !cetta_tptp_snapshot_construct_from_pack_v1(
                   changed_profile_pack_path, NULL, changed_profile_out_path,
                   error, sizeof(error)) && error[0] != '\0',
               "profile substitution without its source is rejected");
        expect(&counts,
               copy_pack_replace_first(
                   pack_path, changed_role_pack_path,
                   "tptp-snapshot-specializer",
                   "tptp-snapshot-specializer-omitted"),
               "prepare changed specializer role pack");
        error[0] = '\0';
        expect(&counts,
               !cetta_tptp_snapshot_construct_from_pack_v1(
                   changed_role_pack_path, NULL, changed_role_out_path,
                   error, sizeof(error)) && error[0] != '\0',
               "artifact requires its snapshot specializer source");
        expect(&counts,
               copy_pack_replace_first(
                   pack_path, changed_source_path_pack_path,
                   "native/tptp_official_snapshot_v1.c",
                   "native/tptp_official_snapshot_omitted_v1.c"),
               "prepare changed specializer source path pack");
        error[0] = '\0';
        expect(&counts,
               !cetta_tptp_snapshot_construct_from_pack_v1(
                   changed_source_path_pack_path, NULL,
                   changed_source_path_out_path,
                   error, sizeof(error)) && error[0] != '\0',
               "artifact requires the exact specializer source path");
        expect(&counts, official.tag_name_len >= 20u,
               "official DFA has many tags, not the identifier seed");
        for (ti = 0u; ti < official.tag_name_len; ti++) {
            const char *nm = official.tag_names[ti];
            if (!nm)
                continue;
            if (strcmp(nm, "lower_word") == 0)
                has_lw = true;
            if (strcmp(nm, "fof") == 0)
                has_fof = true;
            if (strcmp(nm, "<=>") == 0)
                has_iff = true;
            if (strcmp(nm, "single_quoted") == 0)
                has_sq = true;
            if (strcmp(nm, "upper_word") == 0)
                has_uw = true;
        }
        expect(&counts, has_lw && has_fof && has_iff && has_sq && has_uw,
               "official tags include tokens and ::= literals");
        expect(&counts, official.skip_tag_len >= 1u, "skip tags present");
        {
            /* Value-bearing tokens are exactly the tags whose DFA language
             * holds more than one string; fixed tags keep their one text. */
            static const char *const expected_values[] = {
                "lower_word", "upper_word", "dollar_word", "dollar_dollar_word",
                "single_quoted", "back_quoted", "distinct_object", "integer",
                "rational", "real"};
            uint32_t value_count = 0u;
            bool values_expected = true;
            bool vline_fixed = false;
            bool fof_fixed = false;
            for (ti = 0u; ti < official.tag_name_len; ti++) {
                const char *nm = official.tag_names[ti];
                bool skipped = false;
                for (uint32_t si = 0u; si < official.skip_tag_len; si++)
                    skipped = skipped || official.skip_tags[si] == ti;
                if (!nm || skipped)
                    continue;
                if (official.tag_carries_lexeme[ti]) {
                    bool listed = false;
                    for (size_t ei = 0u; ei < sizeof(expected_values) / sizeof(expected_values[0]); ei++)
                        listed = listed || strcmp(nm, expected_values[ei]) == 0;
                    values_expected = values_expected && listed;
                    value_count++;
                }
                if (strcmp(nm, "vline") == 0)
                    vline_fixed = !official.tag_carries_lexeme[ti] &&
                        official.tag_fixed_texts[ti] &&
                        strcmp(official.tag_fixed_texts[ti], "|") == 0;
                if (strcmp(nm, "fof") == 0)
                    fof_fixed = !official.tag_carries_lexeme[ti] &&
                        official.tag_fixed_texts[ti] &&
                        strcmp(official.tag_fixed_texts[ti], "fof") == 0;
            }
            expect(&counts, values_expected &&
                       value_count == sizeof(expected_values) / sizeof(expected_values[0]),
                   "value-bearing tokens are the tags with multi-string languages");
            expect(&counts, vline_fixed, "an operator token row has its one fixed text");
            expect(&counts, fof_fixed, "a syntax literal tag has its one fixed text");
        }
        error[0] = '\0';
        expect(&counts,
               cetta_tptp_snapshot_lex_text_v1(
                   &official, sample, sizeof(sample) - 1u, toks, 32u, &ntok,
                   error, sizeof(error)),
               error[0] ? error : "lex fof sample");
        {
            uint32_t k;
            fprintf(stderr, "fof sample ntok=%u:", ntok);
            for (k = 0u; k < ntok && k < 32u; k++) {
                const char *nm =
                    (official.tag_names && toks[k].tag < official.tag_name_len)
                        ? official.tag_names[toks[k].tag]
                        : "?";
                fprintf(stderr, " %s[%u:%u]", nm, toks[k].start_scalar,
                        cetta_tptp_lex_end(&toks[k]));
            }
            fprintf(stderr, "\n");
        }
        expect(&counts, ntok == 12u, "fof(a,axiom,p(X)). token count");
        if (ntok == 12u) {
            const char *bytes = NULL;
            size_t byte_len = 0u;
            CettaTptpLexTokenV1 invalid = toks[0];
            Atom *token_node = atom_expr3(
                &arena, atom_symbol(&arena, "TokC"),
                atom_symbol(&arena, "lower_word"), atom_int(&arena, 2));
            Atom *observed = cetta_tptp_observe_derivation_v1(
                &official, &arena, token_node, toks, ntok,
                sample, sizeof(sample) - 1u);
            expect(&counts,
                   cetta_tptp_lex_token_bytes_v1(
                       &toks[0], sample, sizeof(sample) - 1u,
                       &bytes, &byte_len) &&
                       byte_len == 3u && memcmp(bytes, "fof", 3u) == 0 &&
                       toks[0].start_byte == 0u &&
                       toks[0].end_byte == 3u,
                   "lexer token keeps its exact source byte slice");
            expect(&counts,
                   result_is_app(observed, "tptp-cst:token", 4u) &&
                       observed->expr.elems[4]->kind == ATOM_GROUNDED &&
                       observed->expr.elems[4]->ground.gkind == GV_STRING &&
                       strcmp(observed->expr.elems[4]->ground.sval, "a") == 0,
                   "observed token payload uses the scanner's byte slice");
            invalid.end_byte = sizeof(sample);
            expect(&counts,
                   !cetta_tptp_lex_token_bytes_v1(
                       &invalid, sample, sizeof(sample) - 1u,
                       &bytes, &byte_len) &&
                       bytes == NULL && byte_len == 0u,
                   "out-of-source token byte slice is rejected");
        }
        if (ntok == 12u && official.tag_names) {
            expect(&counts,
                   strcmp(official.tag_names[toks[0].tag], "fof") == 0 &&
                       strcmp(official.tag_names[toks[1].tag], "(") == 0 &&
                       strcmp(official.tag_names[toks[2].tag], "lower_word") ==
                           0 &&
                       strcmp(official.tag_names[toks[3].tag], ",") == 0 &&
                       strcmp(official.tag_names[toks[4].tag], "lower_word") ==
                           0 &&
                       strcmp(official.tag_names[toks[5].tag], ",") == 0 &&
                       strcmp(official.tag_names[toks[6].tag], "lower_word") ==
                           0 &&
                       strcmp(official.tag_names[toks[7].tag], "(") == 0 &&
                       strcmp(official.tag_names[toks[8].tag], "upper_word") ==
                           0 &&
                       strcmp(official.tag_names[toks[9].tag], ")") == 0 &&
                       strcmp(official.tag_names[toks[10].tag], ")") == 0 &&
                       strcmp(official.tag_names[toks[11].tag], ".") == 0,
                   "fof sample token kinds");
        }
        error[0] = '\0';
        ntok = 0u;
        expect(&counts,
               cetta_tptp_snapshot_lex_text_v1(
                   &official, quoted, sizeof(quoted) - 1u, toks, 32u, &ntok,
                   error, sizeof(error)),
               error[0] ? error : "lex quoted+comment sample");
        if (ntok >= 3u && official.tag_names) {
            const char *bytes = NULL;
            size_t byte_len = 0u;
            expect(&counts,
                   strcmp(official.tag_names[toks[0].tag], "cnf") == 0 &&
                       strcmp(official.tag_names[toks[2].tag],
                              "single_quoted") == 0,
                   "comment skipped; quoted distinct from lower_word");
            expect(&counts,
                   cetta_tptp_lex_token_bytes_v1(
                       &toks[2], quoted, sizeof(quoted) - 1u,
                       &bytes, &byte_len) &&
                       byte_len == 5u &&
                       memcmp(bytes, "'cat'", 5u) == 0,
                   "quoted token keeps delimiters in its source byte slice");
        }
        fprintf(stderr,
                "official snapshot kernel=%s conflicts=%u tags=%u skip=%u "
                "dfa_states=%u slr_prods=%u glr_actions=%u\n",
                official.kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_SLR ? "slr"
                                                                   : "glr",
                official.conflict_len, official.tag_name_len,
                official.skip_tag_len, official.dfa.state_len,
                official.slr.production_len, official.slr.glr_action_len);
        expect(&counts, official.kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_GLR,
               "SyntaxBNF conflicts freeze a GLR kernel");
        expect(&counts, official.conflict_len == 219u,
               "composed grammar reports 219 LR conflicts");
        expect(&counts, official.slr.glr_action_len > official.conflict_len,
               "GLR action multimap includes unique actions plus extras");
        {
            CettaTptpPreparedReaderV1 reader;
            uint32_t input_count = 0u;
            static const char malformed_sample[] = "fof(a,axiom,).\n";
            static const char skipped_prefix_malformed_sample[] =
                "% hi\nfof(a,axiom,).\n";
            static const char contextual_word_sample[] =
                "include('Axioms/example.ax').\n"
                "fof(keyword_words,axiom,"
                "p(include) & q(creator)).\n";
            static const char dollar_word_sample[] =
                "fof(a,axiom,p($let)).\n"
                "fof(b,axiom,q($let(a,b))).\n"
                "tff(c,axiom,$let(c: $i, c := a, p(c))).\n";

            cetta_tptp_prepared_reader_init_v1(&reader);
            error[0] = '\0';
            expect(&counts,
                   cetta_tptp_prepared_reader_load_v1(
                       &reader, out_path,
                       CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
                       error, sizeof(error)),
                   error[0] ? error : "prepare reusable frozen reader");
            error[0] = '\0';
            expect(&counts,
                   cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, sample, sizeof(sample) - 1u,
                       1u, 100000u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) && input_count == 1u,
                   error[0] ? error
                            : "packed GLL completes an exhausted table parse");
            input_count = 0u;
            error[0] = '\0';
            expect(&counts,
                   !cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, malformed_sample, sizeof(malformed_sample) - 1u,
                       1u, 100000u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) &&
                       input_count == 0u &&
                       strstr(error, "TPTP:ResourceLimit") != NULL,
                   error[0] ? error
                            : "GLL NoParse does not refute an exhausted table parse");
            input_count = 0u;
            error[0] = '\0';
            expect(&counts,
                   !cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, malformed_sample, sizeof(malformed_sample) - 1u,
                       0u, 100000u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) &&
                       input_count == 0u && strstr(error, "TPTP:NoParse") != NULL,
                   error[0] ? error
                            : "GLL NoParse preserves an exhaustive table rejection");
            input_count = 0u;
            error[0] = '\0';
            expect(&counts,
                   !cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, skipped_prefix_malformed_sample,
                       sizeof(skipped_prefix_malformed_sample) - 1u,
                       0u, 100000u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) &&
                       input_count == 0u &&
                       strstr(error, "TPTP:NoParse byte=17 ") != NULL,
                   error[0] ? error
                            : "parse rejection reports the token byte after skipped text");
            input_count = 0u;
            error[0] = '\0';
            expect(&counts,
                   !cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, sample, sizeof(sample) - 1u,
                       1u, 1u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) &&
                       input_count == 0u &&
                       strstr(error, "TPTP:ResourceLimit") != NULL,
                   error[0] ? error
                            : "bounded GLL preserves an exhausted table outcome");
            input_count = 0u;
            error[0] = '\0';
            expect(&counts,
                   cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, contextual_word_sample,
                       sizeof(contextual_word_sample) - 1u,
                       0u, 0u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) && input_count == 2u,
                   error[0] ? error
                            : "keywords remain syntax and ordinary lower words");
            input_count = 0u;
            error[0] = '\0';
            expect(&counts,
                   cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
                       &reader, dollar_word_sample,
                       sizeof(dollar_word_sample) - 1u,
                       0u, 0u, &arena, count_tptp_input, &input_count,
                       NULL, error, sizeof(error)) && input_count == 3u,
                   error[0] ? error
                            : "a fixed dollar token reads as a dollar word "
                              "where the grammar allows one");
            cetta_tptp_prepared_reader_free_v1(&reader);
        }
        {
            Atom *records = NULL;
            static const char thf_sample[] = "thf(a,type,p:$o).\n";
            error[0] = '\0';
            expect(&counts,
                   official.slr.production_len > 0u,
                   "parser tables exported");
            expect(&counts,
                   cetta_tptp_snapshot_read_text_v1(
                       &official, sample, sizeof(sample) - 1u, &arena,
                       &records, error, sizeof(error)) &&
                       records != NULL,
                   error[0] ? error : "frozen read fof sample");
            if (records && records->kind == ATOM_EXPR && records->expr.len == 1u &&
                !strstr(atom_to_string(&arena, records->expr.elems[0]), "fof_arguments"))
                fprintf(stderr, "pinned fof read: %s\n",
                        atom_to_string(&arena, records->expr.elems[0]));
            expect(&counts,
                   records && records->kind == ATOM_EXPR && records->expr.len == 1u &&
                       strcmp(atom_to_string(&arena, records->expr.elems[0]),
                              "(bnf fof_annotated (bnf lower_word \"a\") "
                              "(bnf lower_word \"axiom\") (bnf fof_plain_term "
                              "(bnf lower_word \"p\") "
                              "(bnf fof_arguments [(bnf upper_word \"X\")])) "
                              "(bnf nothing))") == 0,
                   "pinned read yields the canonical term");
            records = NULL;
            error[0] = '\0';
            expect(&counts,
                   cetta_tptp_snapshot_read_text_v1(
                       &official, thf_sample, sizeof(thf_sample) - 1u, &arena,
                       &records, error, sizeof(error)) &&
                       records != NULL,
                   error[0] ? error : "frozen read thf p:$o");
            if (records && records->kind == ATOM_EXPR && records->expr.len == 1u &&
                !strstr(atom_to_string(&arena, records->expr.elems[0]), "thf_atom_typing"))
                fprintf(stderr, "pinned thf read: %s\n",
                        atom_to_string(&arena, records->expr.elems[0]));
            expect(&counts,
                   records && records->kind == ATOM_EXPR && records->expr.len == 1u &&
                       strcmp(atom_to_string(&arena, records->expr.elems[0]),
                              "(bnf thf_annotated (bnf lower_word \"a\") "
                              "(bnf lower_word \"type\") (bnf thf_atom_typing/0 "
                              "(bnf lower_word \"p\") (bnf dollar_word \"$o\")) "
                              "(bnf nothing))") == 0,
                   "pinned THF read yields the canonical term");
        }
        pp_table_snapshot_v1_free(&official);
        unlink(out_path);
        unlink(changed_pack_path);
        unlink(changed_out_path);
        unlink(changed_profile_pack_path);
        unlink(changed_profile_out_path);
        unlink(changed_role_pack_path);
        unlink(changed_role_out_path);
        unlink(changed_source_path_pack_path);
        unlink(changed_source_path_out_path);
    }

    printf("(ParserPackTableSnapshotV1Summary %u %u)\n",
           counts.passed, counts.failed);

    pp_table_snapshot_v1_free(&snap);
    pp_table_snapshot_v1_free(&loaded);
    cetta_lp_native_slr_prepared_free(&prepared);
    cetta_lp_native_grammar_free(&grammar);
    rsdfa_v1_plan_free(&plan);
    rsdfa_v1_program_free(&program);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
