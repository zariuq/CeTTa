#include "lib_parse_native_grammar.h"

#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    (void)fprintf(stderr, "FAIL: %s\n", label);
}

static bool grammar_simple(CettaLpNativeGrammar *grammar,
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

static bool grammar_ambiguous(CettaLpNativeGrammar *grammar,
                              SymbolId start,
                              SymbolId terminal) {
    CettaLpNativeProduction *recursive;
    CettaLpNativeProduction *leaf;

    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(2u, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = 2u;
    recursive = &grammar->productions[0];
    recursive->label = start;
    recursive->lhs = start;
    recursive->rhs = calloc(2u, sizeof(*recursive->rhs));
    if (!recursive->rhs)
        return false;
    recursive->rhs_len = 2u;
    recursive->rhs[0] = (CettaLpNativeSymbol){
        CETTA_LP_NATIVE_SYMBOL_HL, start, 0u};
    recursive->rhs[1] = (CettaLpNativeSymbol){
        CETTA_LP_NATIVE_SYMBOL_HL, start, 0u};
    leaf = &grammar->productions[1];
    leaf->label = terminal;
    leaf->lhs = start;
    leaf->rhs = calloc(1u, sizeof(*leaf->rhs));
    if (!leaf->rhs)
        return false;
    leaf->rhs_len = 1u;
    leaf->rhs[0] = (CettaLpNativeSymbol){
        CETTA_LP_NATIVE_SYMBOL_TM, terminal, 0u};
    return true;
}

static bool grammar_alternatives(CettaLpNativeGrammar *grammar,
                                 SymbolId start,
                                 SymbolId first,
                                 SymbolId second) {
    uint32_t index;

    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(2u, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = 2u;
    for (index = 0u; index < 2u; index++) {
        CettaLpNativeProduction *production = &grammar->productions[index];
        production->label = index == 0u ? first : second;
        production->lhs = start;
        production->rhs = calloc(1u, sizeof(*production->rhs));
        if (!production->rhs)
            return false;
        production->rhs_len = 1u;
        production->rhs[0] = (CettaLpNativeSymbol){
            CETTA_LP_NATIVE_SYMBOL_TM,
            index == 0u ? first : second,
            0u,
        };
    }
    return true;
}

static bool forest_node_equal(
    const CettaLpNativeUtf8ForestNode *left,
    const CettaLpNativeUtf8ForestNode *right) {
    return left->kind == right->kind &&
        left->symbol_id == right->symbol_id &&
        left->production_index == right->production_index &&
        left->dot == right->dot &&
        left->scalar_left == right->scalar_left &&
        left->scalar_right == right->scalar_right &&
        left->byte_left == right->byte_left &&
        left->byte_right == right->byte_right &&
        left->terminal_is_eof == right->terminal_is_eof &&
        left->terminal_scalar == right->terminal_scalar &&
        left->terminal_value_kind == right->terminal_value_kind &&
        left->terminal_witness_id == right->terminal_witness_id &&
        left->choice_len == right->choice_len;
}

static bool forest_choice_equal_mapped(
    const CettaLpNativeUtf8ForestChoice *left,
    const CettaLpNativeUtf8ForestChoice *right,
    const uint32_t *node_map) {
    return node_map[left->parent_node] == right->parent_node &&
        (left->prefix_node == CETTA_LP_NATIVE_UTF8_FOREST_NONE
             ? right->prefix_node == CETTA_LP_NATIVE_UTF8_FOREST_NONE
             : node_map[left->prefix_node] == right->prefix_node) &&
        node_map[left->child_node] == right->child_node &&
        left->production_index == right->production_index &&
        left->scalar_pivot == right->scalar_pivot &&
        left->byte_pivot == right->byte_pivot;
}

static bool forest_content_equal(const CettaLpNativeUtf8Forest *left,
                                 const CettaLpNativeUtf8Forest *right) {
    uint32_t *node_map = NULL;
    bool *node_used = NULL;
    bool *choice_used = NULL;
    uint32_t index;
    bool equal = false;

    if (left->outcome != right->outcome ||
        left->node_len != right->node_len ||
        left->choice_len != right->choice_len ||
        left->root_len != right->root_len ||
        left->expected_terminal_len != right->expected_terminal_len ||
        left->scalar_len != right->scalar_len ||
        left->input_byte_len != right->input_byte_len ||
        left->farthest_scalar != right->farthest_scalar ||
        left->farthest_byte != right->farthest_byte ||
        left->decoded_byte_len != right->decoded_byte_len ||
        left->source_pass_count != right->source_pass_count) {
        return false;
    }
    node_map = malloc(sizeof(*node_map) *
                      (left->node_len ? left->node_len : 1u));
    node_used = calloc(right->node_len ? right->node_len : 1u,
                       sizeof(*node_used));
    choice_used = calloc(right->choice_len ? right->choice_len : 1u,
                         sizeof(*choice_used));
    if (!node_map || !node_used || !choice_used)
        goto done;
    for (index = 0u; index < left->node_len; index++) {
        uint32_t other;
        bool found = false;
        for (other = 0u; other < right->node_len; other++) {
            if (node_used[other] ||
                !forest_node_equal(
                    &left->nodes[index], &right->nodes[other])) {
                continue;
            }
            node_map[index] = other;
            node_used[other] = true;
            found = true;
            break;
        }
        if (!found)
            goto done;
    }
    for (index = 0u; index < left->root_len; index++) {
        uint32_t other;
        bool found = false;
        if (left->roots[index] >= left->node_len)
            goto done;
        for (other = 0u; other < right->root_len; other++) {
            if (right->roots[other] == node_map[left->roots[index]]) {
                found = true;
                break;
            }
        }
        if (!found)
            goto done;
    }
    for (index = 0u; index < left->choice_len; index++) {
        const CettaLpNativeUtf8ForestChoice *choice =
            &left->choices[index];
        uint32_t other;
        bool found = false;
        if (choice->parent_node >= left->node_len ||
            choice->child_node >= left->node_len ||
            (choice->prefix_node != CETTA_LP_NATIVE_UTF8_FOREST_NONE &&
             choice->prefix_node >= left->node_len)) {
            goto done;
        }
        for (other = 0u; other < right->choice_len; other++) {
            if (choice_used[other] ||
                !forest_choice_equal_mapped(
                    choice, &right->choices[other], node_map)) {
                continue;
            }
            choice_used[other] = true;
            found = true;
            break;
        }
        if (!found)
            goto done;
    }
    equal = (left->expected_terminal_len == 0u ||
             memcmp(left->expected_terminal_ids,
                    right->expected_terminal_ids,
                    sizeof(*left->expected_terminal_ids) *
                        left->expected_terminal_len) == 0) &&
        (left->scalar_len == 0u ||
         memcmp(left->codepoints, right->codepoints,
                sizeof(*left->codepoints) * left->scalar_len) == 0) &&
        memcmp(left->byte_offsets, right->byte_offsets,
               sizeof(*left->byte_offsets) *
                   ((size_t)left->scalar_len + 1u)) == 0;

done:
    free(choice_used);
    free(node_used);
    free(node_map);
    return equal;
}

static Atom *token_list_two(Arena *arena, Atom *first, Atom *second) {
    Atom *nil = atom_symbol(arena, "Nil");
    Atom *tail = atom_expr3(
        arena, atom_symbol(arena, "Cons"), second, nil);
    return atom_expr3(
        arena, atom_symbol(arena, "Cons"), first, tail);
}

static void test_prepared_lifecycle(TestCounts *counts) {
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrPrepared prepared;
    CettaLpNativeSlrPrepared empty;
    CettaLpNativeSlrProgram program;
    Arena arena;
    Atom *start_atom;
    Atom *first_atom;
    Atom *second_atom;
    Atom *tokens;
    Atom *wrong_tokens;
    Atom *cold;
    Atom *warm_first;
    Atom *warm_second;
    Atom *no_parse;
    CettaLpNativeSlrSummary summary = {0};
    char error[256] = {0};

    arena_init(&arena);
    cetta_lp_native_slr_prepared_init(&prepared);
    cetta_lp_native_slr_prepared_init(&empty);
    cetta_lp_native_slr_program_init(&program);
    start_atom = atom_symbol(&arena, "slr-prepared-start");
    /* Terminal/nonterminal kind, rather than SymbolId alone, is the namespace. */
    first_atom = start_atom;
    second_atom = atom_symbol(&arena, "slr-prepared-second");
    tokens = token_list_two(&arena, first_atom, second_atom);
    wrong_tokens = token_list_two(&arena, first_atom, first_atom);
    if (!grammar_simple(
            &grammar,
            atom_symbol(&arena, "slr-prepared-production")->sym_id,
            start_atom->sym_id, first_atom->sym_id, second_atom->sym_id)) {
        expect(counts, false, "build deterministic SLR grammar");
        goto done;
    }
    cold = cetta_lp_native_slr_parse_shared(
        &grammar, start_atom->sym_id, tokens, &arena,
        error, sizeof(error));
    expect(counts, cold != NULL && atom_is_symbol(cold, "NoParse") == false,
           error[0] ? error : "cold SLR parse succeeds");
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepare(
            &prepared, &grammar, start_atom->sym_id,
            error, sizeof(error)),
        error[0] ? error : "prepare SLR table");
    error[0] = '\0';
    expect(
        counts,
        !cetta_lp_native_slr_prepare(
            &prepared, &grammar, start_atom->sym_id + 1000u,
            error, sizeof(error)) &&
            strstr(error, "start nonterminal") != NULL,
        "failed SLR re-prepare preserves prior owner");
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepared_summary(
            &prepared, &summary, error, sizeof(error)) &&
            summary.state_len > 0u && summary.shift_len > 0u &&
            summary.accept_len == 1u && summary.conflict_len == 0u,
        error[0] ? error : "prepared SLR exposes its retained summary");
    error[0] = '\0';
    expect(
        counts,
        !cetta_lp_native_slr_prepared_summary(
            &empty, &summary, error, sizeof(error)) &&
            strstr(error, "prepared") != NULL,
        "unprepared SLR summary fails closed");
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepared_export_program(
            &prepared, &program, error, sizeof(error)) &&
            cetta_lp_native_slr_program_validate(
                &program, error, sizeof(error)) &&
            program.start_nonterminal == start_atom->sym_id &&
            program.terminal_len == 2u &&
            program.nonterminal_len == 1u &&
            program.production_len == 1u &&
            program.authored_production_len == 1u &&
            program.rhs_len == 2u &&
            program.summary.accept_len == 1u,
        error[0] ? error : "export prepared SLR program");
    error[0] = '\0';
    expect(
        counts,
        !cetta_lp_native_slr_prepared_export_program(
            &empty, &program, error, sizeof(error)) &&
            strstr(error, "prepared") != NULL,
        "unprepared SLR program export fails closed");
    {
        uint32_t action_index;
        for (action_index = 0u; action_index < program.action_len;
             action_index++) {
            if (program.actions[action_index].kind ==
                CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT) {
                int32_t saved = program.actions[action_index].value;
                program.actions[action_index].value =
                    (int32_t)program.summary.state_len;
                error[0] = '\0';
                expect(
                    counts,
                    !cetta_lp_native_slr_program_validate(
                        &program, error, sizeof(error)),
                    "SLR program rejects an invalid shift target");
                program.actions[action_index].value = saved;
                break;
            }
        }
    }

    cetta_lp_native_grammar_free(&grammar);
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepared_summary(
            &prepared, &summary, error, sizeof(error)) &&
            summary.accept_len == 1u && summary.conflict_len == 0u,
        error[0] ? error :
            "prepared SLR summary survives source grammar release");
    error[0] = '\0';
    warm_first = cetta_lp_native_slr_prepared_parse_shared(
        &prepared, tokens, &arena, error, sizeof(error));
    warm_second = cetta_lp_native_slr_prepared_parse_shared(
        &prepared, tokens, &arena, error, sizeof(error));
    expect(counts, warm_first != NULL && warm_second != NULL,
           error[0] ? error : "reuse SLR owner after grammar free");
    expect(counts,
           cold && warm_first && warm_second &&
               atom_eq(cold, warm_first) && atom_eq(warm_first, warm_second),
           "cold and repeated prepared SLR certificates agree");
    error[0] = '\0';
    no_parse = cetta_lp_native_slr_prepared_parse_shared(
        &prepared, wrong_tokens, &arena, error, sizeof(error));
    expect(counts, no_parse && atom_is_symbol(no_parse, "NoParse"),
           "prepared SLR rejects an invalid token stream normally");
    error[0] = '\0';
    expect(
        counts,
        !cetta_lp_native_slr_prepared_parse_shared(
            &empty, tokens, &arena, error, sizeof(error)) &&
            strstr(error, "prepared") != NULL,
        "unprepared SLR owner fails closed");
    error[0] = '\0';
    expect(
        counts,
        !cetta_lp_native_slr_prepared_parse_shared(
            &prepared, atom_symbol(&arena, "not-a-token-list"),
            &arena, error, sizeof(error)) &&
            strstr(error, "Cons/Nil") != NULL,
        "prepared SLR rejects a malformed token carrier");
    cetta_lp_native_slr_prepared_free(&prepared);
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_program_validate(
            &program, error, sizeof(error)),
        error[0] ? error :
            "exported SLR program survives prepared-owner release");
    error[0] = '\0';
    expect(
        counts,
        !cetta_lp_native_slr_prepared_parse_shared(
            &prepared, tokens, &arena, error, sizeof(error)) &&
            strstr(error, "prepared") != NULL,
        "freed SLR owner fails closed");

done:
    cetta_lp_native_slr_program_free(&program);
    cetta_lp_native_slr_prepared_free(&prepared);
    cetta_lp_native_slr_prepared_free(&empty);
    cetta_lp_native_grammar_free(&grammar);
    arena_free(&arena);
}

static void test_conflict_rejection(TestCounts *counts) {
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrPrepared prepared;
    Arena arena;
    Atom *start;
    Atom *terminal;
    char error[256] = {0};

    arena_init(&arena);
    cetta_lp_native_slr_prepared_init(&prepared);
    start = atom_symbol(&arena, "slr-conflict-start");
    terminal = atom_symbol(&arena, "slr-conflict-terminal");
    if (!grammar_ambiguous(&grammar, start->sym_id, terminal->sym_id)) {
        expect(counts, false, "build ambiguous SLR grammar");
        goto done;
    }
    expect(
        counts,
        !cetta_lp_native_slr_prepare(
            &prepared, &grammar, start->sym_id,
            error, sizeof(error)) &&
            strstr(error, "conflicts") != NULL,
        "prepared SLR rejects a conflicted table");

done:
    cetta_lp_native_slr_prepared_free(&prepared);
    cetta_lp_native_grammar_free(&grammar);
    arena_free(&arena);
}

static void test_lattice_fast_path(TestCounts *counts) {
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrPrepared slr;
    CettaLpNativeGllPrepared gll;
    CettaLpNativeGlrPrepared glr;
    CettaLpNativeUtf8Forest slr_forest;
    CettaLpNativeUtf8Forest gll_forest;
    CettaLpNativeUtf8Forest glr_forest;
    CettaLpNativeUtf8Forest limited_forest;
    CettaLpNativeUtf8Forest rejected_forest;
    CettaLpNativeSlrLatticeOutcome outcome;
    uint32_t terminals[2];
    uint32_t codepoints[2] = {'a', 'b'};
    uint32_t byte_offsets[3] = {0u, 1u, 2u};
    uint32_t start_offsets[4] = {0u, 1u, 2u, 2u};
    CettaLpNativeUtf8LatticeEdge edges[2];
    CettaLpNativeUtf8Lattice lattice;
    CettaLpNativeUtf8LatticeEdge rejected_edges[1];
    uint32_t rejected_offsets[4] = {0u, 0u, 1u, 1u};
    CettaLpNativeUtf8Lattice rejected;
    Arena arena;
    Atom *start;
    Atom *first;
    Atom *second;
    char error[256] = {0};
    bool prepared = false;

    arena_init(&arena);
    cetta_lp_native_slr_prepared_init(&slr);
    cetta_lp_native_gll_prepared_init(&gll);
    cetta_lp_native_glr_prepared_init(&glr);
    cetta_lp_native_utf8_forest_init(&slr_forest);
    cetta_lp_native_utf8_forest_init(&gll_forest);
    cetta_lp_native_utf8_forest_init(&glr_forest);
    cetta_lp_native_utf8_forest_init(&limited_forest);
    cetta_lp_native_utf8_forest_init(&rejected_forest);
    start = atom_symbol(&arena, "slr-lattice-start");
    first = atom_symbol(&arena, "slr-lattice-first");
    second = atom_symbol(&arena, "slr-lattice-second");
    if (!grammar_simple(
            &grammar,
            atom_symbol(&arena, "slr-lattice-production")->sym_id,
            start->sym_id, first->sym_id, second->sym_id)) {
        expect(counts, false, "build SLR lattice grammar");
        goto done;
    }
    prepared = cetta_lp_native_slr_prepare(
            &slr, &grammar, start->sym_id, error, sizeof(error)) &&
        cetta_lp_native_gll_prepare(
            &gll, &grammar, start->sym_id, error, sizeof(error)) &&
        cetta_lp_native_glr_prepare(
            &glr, &grammar, start->sym_id, error, sizeof(error));
    expect(counts, prepared,
           error[0] ? error : "prepare all deterministic lattice backends");
    if (!prepared)
        goto done;
    cetta_lp_native_grammar_free(&grammar);

    terminals[0] = first->sym_id < second->sym_id
        ? first->sym_id : second->sym_id;
    terminals[1] = first->sym_id < second->sym_id
        ? second->sym_id : first->sym_id;
    edges[0] = (CettaLpNativeUtf8LatticeEdge){
        .terminal_id = first->sym_id,
        .scalar_left = 0u,
        .scalar_right = 1u,
        .byte_left = 0u,
        .byte_right = 1u,
        .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
        .value = 'a',
    };
    edges[1] = (CettaLpNativeUtf8LatticeEdge){
        .terminal_id = second->sym_id,
        .scalar_left = 1u,
        .scalar_right = 2u,
        .byte_left = 1u,
        .byte_right = 2u,
        .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
        .value = 'b',
    };
    lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = terminals,
        .terminal_len = 2u,
        .edges = edges,
        .edge_len = 2u,
        .start_offsets = start_offsets,
        .start_offset_len = 4u,
        .codepoints = codepoints,
        .byte_offsets = byte_offsets,
        .scalar_len = 2u,
        .input_byte_len = 2u,
        .decoded_byte_len = 2u,
        .source_pass_count = 1u,
    };
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
            &slr, &lattice, 0u, 100u, &outcome, &slr_forest,
            error, sizeof(error)) &&
            outcome == CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED &&
            slr_forest.root_len == 1u,
        error[0] ? error : "prepared SLR accepts a unique lattice path");
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
            &gll, &lattice, 0u, 100u, &gll_forest,
            error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
            &glr, &lattice, 0u, 100u, &glr_forest,
            error, sizeof(error)),
        error[0] ? error : "general backends accept the unique lattice path");
    expect(counts,
           forest_content_equal(&slr_forest, &gll_forest),
           "SLR and GLL export the same deterministic forest");
    expect(counts,
           forest_content_equal(&gll_forest, &glr_forest),
           "GLL and GLR export the same deterministic forest");
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
            &slr, &lattice, 0u, 1u, &outcome, &limited_forest,
            error, sizeof(error)) &&
            outcome == CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT &&
            limited_forest.outcome ==
                CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT,
        error[0] ? error : "prepared SLR reports its lattice work limit");

    rejected_edges[0] = edges[1];
    rejected = lattice;
    rejected.edges = rejected_edges;
    rejected.edge_len = 1u;
    rejected.start_offsets = rejected_offsets;
    error[0] = '\0';
    expect(
        counts,
        cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
            &slr, &rejected, 0u, 100u, &outcome, &rejected_forest,
            error, sizeof(error)) &&
            outcome == CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL,
        error[0] ? error : "prepared SLR delegates ordinary rejection");

done:
    cetta_lp_native_utf8_forest_free(&rejected_forest);
    cetta_lp_native_utf8_forest_free(&limited_forest);
    cetta_lp_native_utf8_forest_free(&glr_forest);
    cetta_lp_native_utf8_forest_free(&gll_forest);
    cetta_lp_native_utf8_forest_free(&slr_forest);
    cetta_lp_native_glr_prepared_free(&glr);
    cetta_lp_native_gll_prepared_free(&gll);
    cetta_lp_native_slr_prepared_free(&slr);
    cetta_lp_native_grammar_free(&grammar);
    arena_free(&arena);
}

static void test_lattice_branch_fallback(TestCounts *counts) {
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrPrepared prepared;
    CettaLpNativeUtf8Forest forest;
    CettaLpNativeSlrLatticeOutcome outcome;
    uint32_t terminals[2];
    uint32_t codepoints[1] = {'x'};
    uint32_t byte_offsets[2] = {0u, 1u};
    uint32_t start_offsets[3] = {0u, 2u, 2u};
    CettaLpNativeUtf8LatticeEdge edges[2];
    CettaLpNativeUtf8Lattice lattice;
    Arena arena;
    Atom *start;
    Atom *first;
    Atom *second;
    char error[256] = {0};

    arena_init(&arena);
    cetta_lp_native_slr_prepared_init(&prepared);
    cetta_lp_native_utf8_forest_init(&forest);
    start = atom_symbol(&arena, "slr-lattice-choice-start");
    first = atom_symbol(&arena, "slr-lattice-choice-first");
    second = atom_symbol(&arena, "slr-lattice-choice-second");
    if (!grammar_alternatives(
            &grammar, start->sym_id, first->sym_id, second->sym_id) ||
        !cetta_lp_native_slr_prepare(
            &prepared, &grammar, start->sym_id,
            error, sizeof(error))) {
        expect(counts, false,
               error[0] ? error : "prepare SLR lexical-choice grammar");
        goto done;
    }
    cetta_lp_native_grammar_free(&grammar);
    terminals[0] = first->sym_id < second->sym_id
        ? first->sym_id : second->sym_id;
    terminals[1] = first->sym_id < second->sym_id
        ? second->sym_id : first->sym_id;
    edges[0] = (CettaLpNativeUtf8LatticeEdge){
        .terminal_id = terminals[0],
        .scalar_left = 0u,
        .scalar_right = 1u,
        .byte_left = 0u,
        .byte_right = 1u,
        .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
        .value = 'x',
    };
    edges[1] = edges[0];
    edges[1].terminal_id = terminals[1];
    lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = terminals,
        .terminal_len = 2u,
        .edges = edges,
        .edge_len = 2u,
        .start_offsets = start_offsets,
        .start_offset_len = 3u,
        .codepoints = codepoints,
        .byte_offsets = byte_offsets,
        .scalar_len = 1u,
        .input_byte_len = 1u,
        .decoded_byte_len = 1u,
        .source_pass_count = 1u,
    };
    expect(
        counts,
        cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
            &prepared, &lattice, 0u, 100u, &outcome, &forest,
            error, sizeof(error)) &&
            outcome == CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL,
        error[0] ? error : "prepared SLR delegates lexical branching");

done:
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_slr_prepared_free(&prepared);
    cetta_lp_native_grammar_free(&grammar);
    arena_free(&arena);
}

int main(void) {
    SymbolTable symbols;
    TestCounts counts = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    test_prepared_lifecycle(&counts);
    test_conflict_rejection(&counts);
    test_lattice_fast_path(&counts);
    test_lattice_branch_fallback(&counts);

    (void)printf("(NativeSLRPreparedSummary %u %u)\n",
                 counts.passed, counts.failed);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
