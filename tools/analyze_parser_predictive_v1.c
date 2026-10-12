/* Build-time client of the native grammar preparation and specialization.
 * Input serializes CettaLpNativeGrammar productions over local identities:
 * start, production count, then lhs/rhs length and kind/name/scope triples.
 * Output keeps the identity maps and production occurrence decisions. */
#include "lib_parse_native_grammar.h"

#include <stdio.h>
#include <stdlib.h>

static void numbers(const uint32_t *values, uint32_t len) {
    putchar('[');
    for (uint32_t i = 0u; i < len; ++i) printf("%s%u", i ? "," : "", values[i]);
    putchar(']');
}

static void sets(const CettaLpNativeGllProgram *grammar, const uint64_t *words) {
    putchar('[');
    for (uint32_t nt = 0u; nt < grammar->nonterminal_len; ++nt) {
        if (nt) putchar(',');
        putchar('[');
        bool comma = false;
        for (uint32_t t = 0u; t <= grammar->terminal_len; ++t) {
            if (!(words[(size_t)nt * grammar->word_len + t / 64u] & (UINT64_C(1) << (t % 64u)))) continue;
            if (comma) putchar(',');
            if (t == grammar->terminal_len) fputs("null", stdout);
            else printf("%u", grammar->terminals[t]);
            comma = true;
        }
        putchar(']');
    }
    putchar(']');
}

int main(void) {
    CettaLpNativeGrammar grammar = {0};
    CettaLpNativeGllPrepared prepared = {0};
    CettaLpNativeGllProgram stored = {0};
    CettaLpNativePredictiveProgram predictive = {0};
    char error[512] = {0};
    uint32_t start;
    int result = 1;
    if (scanf("%u %u", &start, &grammar.production_len) != 2 || !grammar.production_len ||
        (size_t)grammar.production_len > SIZE_MAX / sizeof(*grammar.productions)) goto done;
    grammar.productions = calloc(grammar.production_len, sizeof(*grammar.productions));
    if (!grammar.productions) { grammar.production_len = 0u; goto done; }
    for (uint32_t p = 0u; p < grammar.production_len; ++p) {
        CettaLpNativeProduction *prod = &grammar.productions[p];
        prod->label = p;
        if (scanf("%u %u", &prod->lhs, &prod->rhs_len) != 2 ||
            (size_t)prod->rhs_len > SIZE_MAX / sizeof(*prod->rhs)) goto done;
        prod->rhs = calloc(prod->rhs_len ? prod->rhs_len : 1u, sizeof(*prod->rhs));
        if (!prod->rhs) goto done;
        for (uint32_t j = 0u; j < prod->rhs_len; ++j) {
            uint32_t kind;
            if (scanf("%u %u %u", &kind, &prod->rhs[j].name, &prod->rhs[j].scope) != 3 ||
                kind > CETTA_LP_NATIVE_SYMBOL_HLB) goto done;
            prod->rhs[j].kind = (CettaLpNativeSymbolKind)kind;
        }
    }
    if (scanf(" %c", &(char){0}) != EOF) goto done;
    if (!cetta_lp_native_gll_prepare(&prepared, &grammar, start, error, sizeof(error)) ||
        !cetta_lp_native_predictive_build(&prepared, NULL, 0u, true, UINT32_MAX,
            &predictive, error, sizeof(error)) ||
        !cetta_lp_native_gll_prepared_export_program(&prepared, &stored, error, sizeof(error))) goto done;
    printf("{\"format\":\"NativePredictiveAnalysisV1\",\"status\":%u,\"nonterminals\":", predictive.status);
    numbers(stored.nonterminals, stored.nonterminal_len);
    fputs(",\"terminals\":", stdout); numbers(stored.terminals, stored.terminal_len);
    fputs(",\"first\":", stdout); sets(&stored, stored.first);
    fputs(",\"follow\":", stdout); sets(&stored, stored.follow);
    fputs(",\"decisions\":[", stdout);
    size_t cells = (size_t)predictive.nonterminal_len * (predictive.terminal_len + 1u);
    for (size_t i = 0u; i < cells; ++i) {
        if (i) putchar(',');
        if (predictive.decisions[i] == UINT32_MAX) fputs("null", stdout);
        else printf("%u", predictive.decisions[i]);
    }
    printf("],\"conflict_nonterminal\":%u,\"conflict_productions\":", predictive.conflict_nonterminal);
    numbers(predictive.conflict_productions, 2u);
    fputs(",\"conflict_terminals\":", stdout); numbers(predictive.conflict_terminals, 2u);
    fputs(",\"cycle_productions\":", stdout); numbers(predictive.cycle_productions, predictive.cycle_len);
    fputs("}\n", stdout);
    result = ferror(stdout) ? 1 : 0;
done:
    if (result) fprintf(stderr, "predictive analysis refused: %s\n", error[0] ? error : "malformed native grammar input");
    cetta_lp_native_grammar_free(&grammar);
    cetta_lp_native_gll_prepared_free(&prepared);
    cetta_lp_native_gll_program_free(&stored);
    cetta_lp_native_predictive_program_free(&predictive);
    return result;
}
