#include "parser_pack_native_v1.h"
#include "parser_pack_table_snapshot_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native/language_def_parser_pack_v1.h"
#include "native/parser_baked_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed, failed;

static void expect(bool condition, const char *label) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", label); }
}

static Atom *constant(Arena *arena, const char *name) {
    return atom_expr2(arena, atom_symbol(arena, "pa-const"), atom_symbol(arena, name));
}

static bool form(const Atom *atom, const char *name, uint32_t arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
        atom_is_symbol(atom->expr.elems[0], name);
}

static bool action_slot(const Atom *action, uint32_t *index) {
    if (!form(action, "pa-slot", 1u)) return false;
    Atom *number = action->expr.elems[1];
    *index = 0u;
    while (!atom_is_symbol(number, "q-zero")) {
        if (!form(number, "q-succ", 1u) || *index == UINT32_MAX) return false;
        ++*index;
        number = number->expr.elems[1];
    }
    return true;
}

static bool retains_children(const PPABIV1Production *production) {
    const Atom *action = production->action;
    uint32_t transparent;
    if (action_slot(action, &transparent)) {
        uint32_t nonterminals = 0u;
        for (uint32_t i = 0u; i < production->item_len; ++i)
            if (production->items[i].kind == PPABI_V1_ITEM_NONTERMINAL) {
                if (i != transparent) return false;
                ++nonterminals;
            }
        return nonterminals == 1u;
    }
    if (!form(action, "pa-apply", 2u) ||
        !atom_is_symbol(action->expr.elems[1], "CstRuleV1")) return false;
    Atom *arguments = action->expr.elems[2];
    if (!form(arguments, "pa-cons", 2u) ||
        !form(arguments->expr.elems[1], "pa-const", 1u)) return false;
    bool *retained = calloc(production->item_len ? production->item_len : 1u, sizeof(*retained));
    if (!retained) return false;
    bool ok = false;
    arguments = arguments->expr.elems[2];
    while (!atom_is_symbol(arguments, "pa-nil")) {
        uint32_t index;
        if (!form(arguments, "pa-cons", 2u) ||
            !action_slot(arguments->expr.elems[1], &index) ||
            index >= production->item_len || retained[index]) goto done;
        retained[index] = true;
        arguments = arguments->expr.elems[2];
    }
    for (uint32_t i = 0u; i < production->item_len; ++i)
        if (production->items[i].kind == PPABI_V1_ITEM_NONTERMINAL && !retained[i]) goto done;
    ok = true;
done:
    free(retained);
    return ok;
}

static void regex_projection_inventory(void) {
    const CettaBakedParserV1 *baked = cetta_parser_baked_v1_find("regex-pattern-reader");
    PPTableSnapshotV1 snapshot;
    CettaLdParserPackV1 compiled;
    Arena arena;
    pp_table_snapshot_v1_init(&snapshot);
    cetta_ld_parser_pack_v1_init(&compiled);
    arena_init(&arena);
    Atom *metadata = NULL;
    char error[512] = {0};
    bool loaded = baked && pp_table_snapshot_v1_read(&snapshot, baked->bytes, baked->length,
        error, sizeof(error)) && fh_ground_term_v1_read_source(&arena,
        snapshot.general_metadata, snapshot.general_metadata_len, &metadata, error, sizeof(error)) &&
        metadata && metadata->kind == ATOM_EXPR && metadata->expr.len > 4u &&
        cetta_ld_parser_pack_v1_import_storage(&compiled, metadata->expr.elems[4], error, sizeof(error));
    expect(loaded, "restore actual baked regex action inventory without recompiling grammar");
    if (loaded) {
        uint32_t retaining = 0u;
        for (uint32_t i = 0u; i < compiled.pack.production_len; ++i)
            retaining += retains_children(&compiled.pack.productions[i]);
        expect(retaining == compiled.pack.production_len,
               "every regex production retains its semantic nonterminal children");
        printf("(RegexSelectionActionCoverageV1 %u %u)\n", retaining, compiled.pack.production_len);
        PPABIV1Item child = {.kind = PPABI_V1_ITEM_NONTERMINAL};
        PPABIV1Production drops_child = {.item_len = 1u, .items = &child,
                                       .action = constant(&arena, "discarded")};
        expect(!retains_children(&drops_child),
               "arbitrary child-erasing action cannot earn output-CST correspondence");
    }
    cetta_ld_parser_pack_v1_free(&compiled);
    pp_table_snapshot_v1_free(&snapshot);
    arena_free(&arena);
}

static Atom *slots(Arena *arena, uint32_t count) {
    Atom *list = atom_symbol(arena, "pa-nil");
    for (uint32_t i = count; i > 0u; --i) {
        Atom *index = atom_symbol(arena, "q-zero");
        for (uint32_t j = 1u; j < i; ++j)
            index = atom_expr2(arena, atom_symbol(arena, "q-succ"), index);
        Atom *slot = atom_expr2(arena, atom_symbol(arena, "pa-slot"), index);
        list = atom_expr3(arena, atom_symbol(arena, "pa-cons"), slot, list);
    }
    return list;
}

typedef struct { bool none, alternating; } Policy;

static bool select_family(void *context, const PPABIV1Pack *pack,
                          const CettaLpNativeUtf8Forest *forest,
                          uint32_t node, uint32_t production) {
    (void)pack;
    const Policy *policy = context;
    if (policy->none) return false;
    return production != 1u ||
        (policy->alternating && (forest->nodes[node].scalar_right % 2u) == 0u);
}

static bool selected_value(Atom *result, const Policy *policy) {
    if (policy->none) return false;
    Atom *vector = result->expr.elems[1];
    for (CettaExprIndex i = 1u; i < vector->expr.len; ++i) {
        if (atom_is_symbol(vector->expr.elems[i], "discarded") &&
            !(policy->alternating && (i % 2u) == 0u)) return false;
    }
    return true;
}

/* Each position has two independent derivations. The forest is linear in
 * positions; unrestricted semantic expansion has 2^positions occurrences. */
static void ambiguity_control(uint32_t positions, Policy policy, bool expand_all) {
    Arena arena;
    arena_init(&arena);
    PPABIV1Pack pack = {0};
    PPABIV1Production productions[3] = {0};
    pack.productions = productions;
    pack.production_len = 3u;
    productions[0].action = constant(&arena, "retained");
    productions[1].action = constant(&arena, "discarded");
    productions[0].item_len = productions[1].item_len = 1u;
    productions[2].action = atom_expr3(&arena, atom_symbol(&arena, "pa-apply"),
        atom_symbol(&arena, "Vector"), slots(&arena, positions));
    productions[2].item_len = positions;

    PPNativeV1Result parsed;
    ppnative_v1_result_init(&parsed);
    CettaLpNativeUtf8Forest *forest = &parsed.forest;
    forest->node_len = 3u * positions;
    forest->choice_len = 3u * positions;
    forest->nodes = calloc(forest->node_len, sizeof(*forest->nodes));
    forest->choices = calloc(forest->choice_len, sizeof(*forest->choices));
    forest->roots = malloc(sizeof(*forest->roots));
    forest->root_len = 1u;
    forest->scalar_len = positions;
    forest->codepoints = calloc(positions, sizeof(*forest->codepoints));
    forest->byte_offsets = calloc((size_t)positions + 1u, sizeof(*forest->byte_offsets));
    expect(forest->nodes && forest->choices && forest->roots &&
           forest->codepoints && forest->byte_offsets, "allocate linear packed forest");
    if (!forest->nodes || !forest->choices || !forest->roots ||
        !forest->codepoints || !forest->byte_offsets) goto done;
    for (uint32_t i = 0u; i < positions; ++i) {
        uint32_t terminal = 2u * i, symbol = terminal + 1u;
        forest->nodes[terminal] = (CettaLpNativeUtf8ForestNode){
            .kind = CETTA_LP_NATIVE_UTF8_FOREST_TERM,
            .scalar_left = i, .scalar_right = i + 1u,
            .terminal_value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR,
            .terminal_scalar = 'a'};
        forest->nodes[symbol] = (CettaLpNativeUtf8ForestNode){
            .kind = CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL,
            .scalar_left = i, .scalar_right = i + 1u,
            .choice_begin = 2u * i, .choice_len = 2u};
        for (uint32_t p = 0u; p < 2u; ++p)
            forest->choices[2u * i + p] = (CettaLpNativeUtf8ForestChoice){
                .parent_node = symbol,
                .prefix_node = CETTA_LP_NATIVE_UTF8_FOREST_NONE,
                .child_node = terminal, .production_index = p};
        forest->codepoints[i] = 'a';
        forest->byte_offsets[i + 1u] = i + 1u;
    }
    for (uint32_t i = 0u; i < positions; ++i) {
        uint32_t node = 2u * positions + i;
        bool last = i + 1u == positions;
        forest->nodes[node] = (CettaLpNativeUtf8ForestNode){
            .kind = last ? CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL :
                           CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE,
            .scalar_left = 0u, .scalar_right = i + 1u,
            .choice_begin = 2u * positions + i, .choice_len = 1u};
        forest->choices[2u * positions + i] = (CettaLpNativeUtf8ForestChoice){
            .parent_node = node,
            .prefix_node = i ? node - 1u : CETTA_LP_NATIVE_UTF8_FOREST_NONE,
            .child_node = 2u * i + 1u, .production_index = 2u};
    }
    forest->roots[0] = forest->node_len - 1u;
    parsed.accepted = true;
    CettaLpNativeUtf8ForestNode *snapshot_nodes = malloc(sizeof(*snapshot_nodes) * forest->node_len);
    CettaLpNativeUtf8ForestChoice *snapshot_choices = malloc(sizeof(*snapshot_choices) * forest->choice_len);
    if (!snapshot_nodes || !snapshot_choices) {
        free(snapshot_nodes); free(snapshot_choices);
        expect(false, "allocate immutable forest snapshot");
        goto done;
    }
    memcpy(snapshot_nodes, forest->nodes, sizeof(*snapshot_nodes) * forest->node_len);
    memcpy(snapshot_choices, forest->choices, sizeof(*snapshot_choices) * forest->choice_len);
    Atom **expected = NULL;
    uint32_t expected_len = 0u;
    char error[512] = {0};
    if (expand_all) {
        expect(ppnative_v1_finish_values(&parsed, &pack, NULL, UINT32_MAX,
            UINT32_MAX, error, sizeof(error)), "unrestricted forest replay succeeds");
        expect(parsed.semantic_result_len == (1u << positions),
               "independent binary occurrences all materialize without selection");
        expected = malloc(sizeof(*expected) * parsed.semantic_result_len);
        if (!expected) { free(snapshot_nodes); free(snapshot_choices); goto done; }
        for (uint32_t i = 0u; i < parsed.semantic_result_len; ++i)
            if (selected_value(parsed.semantic_results[i], &policy))
                expected[expected_len++] = parsed.semantic_results[i];
    }
    PPNativeV1FamilySelection selection = {.accepts = select_family, .context = &policy};
    bool executed = ppnative_v1_finish_values_selected(&parsed, &pack, NULL,
        UINT32_MAX, expand_all ? UINT32_MAX : 1u, &selection, error, sizeof(error));
    expect(executed && parsed.outcome == PPNATIVE_V1_COMPLETED,
           "early selection completes before rejected products expand");
    expect(parsed.accepted, "selection does not turn raw recognition into rejection");
    if (expand_all) {
        expect(parsed.semantic_result_len == expected_len, "no selected derivation missing or invented");
        for (uint32_t i = 0u; i < expected_len && i < parsed.semantic_result_len; ++i)
            expect(atom_eq(expected[i], parsed.semantic_results[i]),
                   "early and late selection retain exact ordered observations");
    } else expect(parsed.semantic_result_len == 1u,
                  "large selected product returns one result with allowance one");
    expect(memcmp(snapshot_nodes, forest->nodes, sizeof(*snapshot_nodes) * forest->node_len) == 0 &&
           memcmp(snapshot_choices, forest->choices, sizeof(*snapshot_choices) * forest->choice_len) == 0,
           "all original packed nodes and families retained verbatim");
    PPNativeV1FamilySelection malformed = {0};
    expect(!ppnative_v1_finish_values_selected(&parsed, &pack, NULL,
        UINT32_MAX, UINT32_MAX, &malformed, error, sizeof(error)) && error[0],
        "missing family predicate is not silently accepted");
    free(expected); free(snapshot_nodes); free(snapshot_choices);
done:
    ppnative_v1_result_free(&parsed);
    arena_free(&arena);
}

static void shared_constant_lifetime(void) {
    Arena source;
    arena_init(&source);
    Atom *text = atom_string(&source, "owned parser constant retained across repeated derivations");
    Atom *value = atom_expr2(&source, atom_symbol(&source, "Payload"), text);
    Atom *action = atom_expr2(&source, atom_symbol(&source, "pa-const"), value);
    PPABIV1Production productions[2] = {{.action = action}, {.action = action}};
    PPABIV1Pack pack = {.productions = productions, .production_len = 2u};
    PPNativeV1Result parsed;
    ppnative_v1_result_init(&parsed);
    parsed.accepted = true;
    parsed.forest.node_len = parsed.forest.root_len = 1u;
    parsed.forest.choice_len = 2u;
    parsed.forest.nodes = calloc(1u, sizeof(*parsed.forest.nodes));
    parsed.forest.choices = calloc(2u, sizeof(*parsed.forest.choices));
    parsed.forest.roots = calloc(1u, sizeof(*parsed.forest.roots));
    bool allocated = parsed.forest.nodes && parsed.forest.choices && parsed.forest.roots;
    expect(allocated, "allocate shared-constant lifetime forest");
    if (allocated) {
        parsed.forest.nodes[0].kind = CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL;
        parsed.forest.nodes[0].choice_len = 2u;
        for (uint32_t p = 0u; p < 2u; ++p)
            parsed.forest.choices[p] = (CettaLpNativeUtf8ForestChoice){
                .prefix_node = CETTA_LP_NATIVE_UTF8_FOREST_NONE,
                .child_node = CETTA_LP_NATIVE_UTF8_FOREST_NONE,
                .production_index = p};
        char error[512] = {0};
        bool executed = ppnative_v1_finish_values(&parsed, &pack, NULL,
            UINT32_MAX, UINT32_MAX, error, sizeof(error));
        expect(executed && parsed.outcome == PPNATIVE_V1_COMPLETED &&
               parsed.semantic_result_len == 2u,
               "equal-valued families retain both occurrence results");
        if (executed && parsed.semantic_result_len == 2u) {
            Atom *first = parsed.semantic_results[0]->expr.elems[1];
            Atom *second = parsed.semantic_results[1]->expr.elems[1];
            expect(first == second && first != value,
                   "one replay shares its owned copy rather than borrowed source storage");
            arena_free(&source);
            arena_init(&source);
            /* Reuse the source arena before inspecting the retained answer. */
            for (unsigned i = 0u; i < 100u; ++i)
                (void)atom_string(&source, "source arena replaced");
            expect(atom_eq(first, second) && first->expr.elems[1]->kind == ATOM_GROUNDED &&
                   strcmp(first->expr.elems[1]->ground.sval,
                          "owned parser constant retained across repeated derivations") == 0,
                   "result constants outlive source arena reset and memo teardown");
        }
    }
    ppnative_v1_result_free(&parsed);
    arena_free(&source);
}

static void trace_replay_contract(void) {
    Arena arena;
    arena_init(&arena);
    PPABIV1Production production = {.action = constant(&arena, "trace-empty")};
    PPABIV1Pack pack = {.productions = &production, .production_len = 1u};
    CettaLpNativeLrTraceStep step = {.production = 0u, .rhs_len = 0u, .left = 0u, .right = 0u};
    CettaLpNativeLrTrace trace = {.steps = &step, .len = 1u};
    PPNativeV1Result result;
    ppnative_v1_result_init(&result);
    result.accepted = true;
    char error[256] = {0};
    expect(ppnative_v1_finish_values_trace(&result, &pack, &trace, UINT32_MAX, UINT32_MAX,
        NULL, error, sizeof(error)) && result.outcome == PPNATIVE_V1_COMPLETED &&
        result.semantic_result_len == 1u, "unbounded trace replay retains an empty-span value");
    Policy none = {.none = true};
    PPNativeV1FamilySelection selection = {.context = &none, .accepts = select_family};
    expect(ppnative_v1_finish_values_trace(&result, &pack, &trace, UINT32_MAX, UINT32_MAX,
        &selection, error, sizeof(error)) && result.outcome == PPNATIVE_V1_COMPLETED &&
        result.semantic_result_len == 0u, "boundary-local trace selection can retain no values");
    expect(!ppnative_v1_finish_values_trace(&result, &pack, &trace, 1u, UINT32_MAX,
        NULL, error, sizeof(error)) && result.semantic_result_len == 0u,
        "trace replay refuses a finite forest-replay allowance without publishing a value");
    ppnative_v1_result_free(&result);
    arena_free(&arena);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    ambiguity_control(4u, (Policy){0}, true);
    ambiguity_control(8u, (Policy){.alternating = true}, true);
    ambiguity_control(5u, (Policy){.none = true}, true);
    ambiguity_control(24u, (Policy){0}, false);
    shared_constant_lifetime();
    regex_projection_inventory();
    trace_replay_contract();
    printf("(ParserFamilySelectionV1Summary %u %u)\n", passed, failed);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failed ? 1 : 0;
}
