#include "lib_parse_native_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed, failed;

static void expect(bool value, const char *label) {
    if (value) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", label); }
}

static bool grammar(CettaLpNativeGrammar *g, uint32_t count, const uint32_t *lhs,
                    const uint32_t *offsets, const CettaLpNativeSymbol *rhs) {
    g->productions = calloc(count, sizeof(*g->productions));
    if (!g->productions) return false;
    g->production_len = count;
    for (uint32_t i = 0u; i < count; ++i) {
        g->productions[i].label = i + 1u;
        g->productions[i].lhs = lhs[i];
        uint32_t len = offsets[i + 1u] - offsets[i];
        g->productions[i].rhs_len = len;
        g->productions[i].rhs = malloc((size_t)(len ? len : 1u) * sizeof(*rhs));
        if (!g->productions[i].rhs) return false;
        if (len) memcpy(g->productions[i].rhs, rhs + offsets[i], (size_t)len * sizeof(*rhs));
    }
    return true;
}

static void run_case(const char *name, CettaLpNativeGrammar *g,
                     const CettaLpNativeUtf8Terminal *terminals, uint32_t terminal_len,
                     const uint8_t *source, size_t source_len,
                     bool admitted) {
    CettaLpNativeGlrPrepared owner = {0};
    CettaLpNativeGllPrepared prediction = {0};
    CettaLpNativeSlrProgram stored = {0};
    CettaLpNativeGllProgram stored_prediction = {0};
    CettaLpNativeGlrPrepared restored = {0};
    CettaLpNativeUtf8Forest forest = {0}, reference = {0};
    CettaLpNativeSlrLatticeOutcome outcome;
    char error[512] = {0};
    bool ready = cetta_lp_native_glr_prepare(&owner, g, 10u, error, sizeof(error)) &&
        cetta_lp_native_gll_prepare(&prediction, g, 10u, error, sizeof(error));
    expect(ready, name);
    if (!ready) { fprintf(stderr, "%s\n", error); goto done; }
    bool executed = cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(
        &owner, terminals, terminal_len, source, source_len, UINT32_MAX,
        &outcome, &forest, error, sizeof(error));
    expect(executed, "corridor executes against immutable owner");
    expect(outcome == (admitted ? CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED :
                                 CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL), name);
    if (!executed) { fprintf(stderr, "%s\n", error); goto done; }
    if (admitted) {
        expect(forest.root_len == 1u && forest.nodes[forest.roots[0]].scalar_left == 0u &&
            forest.nodes[forest.roots[0]].scalar_right == forest.scalar_len,
            "admitted corridor has one complete source-root occurrence");
        bool recognized = cetta_lp_native_gll_prepared_parse_owned_utf8_forest_complete(&prediction,
            terminals, terminal_len, source, source_len, UINT32_MAX, &reference, error, sizeof(error));
        bool full_root = false;
        for (uint32_t i = 0u; recognized && i < reference.root_len; ++i)
            full_root |= reference.nodes[reference.roots[i]].scalar_left == 0u &&
                reference.nodes[reference.roots[i]].scalar_right == reference.scalar_len;
        expect(recognized && full_root, "general grammar accepts admitted source");
        uint32_t *retained_offsets = forest.byte_offsets;
        expect(cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(&owner,
            terminals, terminal_len, (const uint8_t *)"?", 1u, UINT32_MAX,
            &outcome, &forest, error, sizeof(error)) &&
            outcome == CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL &&
            forest.byte_offsets == retained_offsets, "corridor refusal preserves previous owned output");
        const uint8_t invalid_utf8[] = {0xc0u, 0xafu};
        expect(!cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(&owner,
            terminals, terminal_len, invalid_utf8, sizeof(invalid_utf8), UINT32_MAX,
            &outcome, &forest, error, sizeof(error)) && forest.byte_offsets == retained_offsets,
            "invalid UTF-8 faults without replacing previous owner");
        expect(cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(&owner,
            terminals, terminal_len, source, source_len, 1u, &outcome, &forest, error, sizeof(error)) &&
            outcome == CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT &&
            forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT,
            "explicit corridor cutoff is unfinished, not source rejection");
    }
    bool frozen = cetta_lp_native_glr_prepared_export_program(&owner, &stored, error, sizeof(error)) &&
        cetta_lp_native_gll_prepared_export_program(&prediction, &stored_prediction, error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_import_program(&restored, &stored, &stored_prediction, error, sizeof(error));
    expect(frozen, "corridor restores existing actions and state-index without table construction");
    if (frozen) {
        cetta_lp_native_glr_prepared_free(&owner);
        expect(cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(&restored,
            terminals, terminal_len, source, source_len, UINT32_MAX, &outcome, &forest, error, sizeof(error)) &&
            outcome == (admitted ? CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED :
                                   CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL),
            "restored corridor admission and ownership survive original owner release");
    }
done:
    cetta_lp_native_utf8_forest_free(&forest); cetta_lp_native_utf8_forest_free(&reference);
    cetta_lp_native_glr_prepared_free(&owner); cetta_lp_native_glr_prepared_free(&restored);
    cetta_lp_native_gll_prepared_free(&prediction);
    cetta_lp_native_slr_program_free(&stored); cetta_lp_native_gll_program_free(&stored_prediction);
    cetta_lp_native_grammar_free(g);
}

int main(void) {
    enum { T = CETTA_LP_NATIVE_SYMBOL_TM, N = CETTA_LP_NATIVE_SYMBOL_HL };
    const CettaLpNativeUnicodeRange a_range[] = {{'a', 'a'}};
    const CettaLpNativeUtf8Terminal terminals[] = {
        {.terminal_id=20u,.kind=CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR,.scalar='a'},
        {.terminal_id=21u,.kind=CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR,.scalar='b'},
        {.terminal_id=22u,.kind=CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES,.ranges=a_range,.range_len=1u}};
    CettaLpNativeGrammar g = {0};
    const uint32_t left_lhs[] = {10u, 10u}, left_offsets[] = {0u, 2u, 3u};
    const CettaLpNativeSymbol left_rhs[] = {{.kind=N,.name=10u},{.kind=T,.name=20u},{.kind=T,.name=20u}};
    if (grammar(&g,2u,left_lhs,left_offsets,left_rhs)) run_case("left-recursive written order",&g,terminals,3u,(const uint8_t *)"aaaa",4u,true);
    const uint32_t nullable_lhs[] = {10u, 11u}, nullable_offsets[] = {0u,2u,2u};
    const CettaLpNativeSymbol nullable_rhs[] = {{.kind=N,.name=11u},{.kind=T,.name=21u}};
    if (grammar(&g,2u,nullable_lhs,nullable_offsets,nullable_rhs)) run_case("nullable reduction before consumption",&g,terminals,3u,(const uint8_t *)"b",1u,true);
    const uint32_t empty_lhs[] = {10u}, empty_offsets[] = {0u,0u};
    if (grammar(&g,1u,empty_lhs,empty_offsets,NULL)) run_case("empty accepting derivation",&g,terminals,3u,(const uint8_t *)"",0u,true);
    const uint32_t coalesced_lhs[] = {10u,10u,11u}, coalesced_offsets[] = {0u,2u,5u,5u};
    const CettaLpNativeSymbol coalesced_rhs[] = {
        {.kind=N,.name=11u},{.kind=T,.name=20u},
        {.kind=T,.name=21u},{.kind=N,.name=11u},{.kind=T,.name=22u}};
    if (grammar(&g,3u,coalesced_lhs,coalesced_offsets,coalesced_rhs))
        run_case("duplicate lookahead witnesses coalesce one nullable operation",&g,terminals,3u,(const uint8_t *)"a",1u,true);
    const uint32_t ambiguous_lhs[] = {10u,10u}, ambiguous_offsets[] = {0u,1u,2u};
    const CettaLpNativeSymbol ambiguous_rhs[] = {{.kind=T,.name=20u},{.kind=T,.name=20u}};
    if (grammar(&g,2u,ambiguous_lhs,ambiguous_offsets,ambiguous_rhs)) run_case("different production occurrences refuse",&g,terminals,3u,(const uint8_t *)"a",1u,false);
    const CettaLpNativeSymbol overlap_rhs[] = {{.kind=T,.name=20u},{.kind=T,.name=22u}};
    if (grammar(&g,2u,ambiguous_lhs,ambiguous_offsets,overlap_rhs)) run_case("overlapping lexical classes refuse",&g,terminals,3u,(const uint8_t *)"a",1u,false);
    const uint32_t delayed_offsets[] = {0u,2u,4u,4u};
    const CettaLpNativeSymbol delayed_rhs[] = {
        {.kind=N,.name=11u},{.kind=T,.name=20u},{.kind=N,.name=11u},{.kind=T,.name=22u}};
    if (grammar(&g,3u,coalesced_lhs,delayed_offsets,delayed_rhs))
        run_case("nullable lookahead coalescing does not erase later lexical conflict",&g,terminals,3u,(const uint8_t *)"a",1u,false);
    const uint32_t cycle_lhs[] = {10u,10u}, cycle_offsets[] = {0u,1u,1u};
    const CettaLpNativeSymbol cycle_rhs[] = {{.kind=N,.name=10u}};
    if (grammar(&g,2u,cycle_lhs,cycle_offsets,cycle_rhs)) run_case("zero-consumption ambiguity refuses",&g,terminals,3u,(const uint8_t *)"",0u,false);
    printf("(ParserDeterministicCorridorV1Summary %u %u)\n",passed,failed);
    return failed ? 1 : 0;
}
