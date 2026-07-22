#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_lexical_v1.h"
#include "regular_span_nfa_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static char *render_term(const Atom *term) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;

    if (!fh_ground_term_v1_render(
            term, &canonical, &canonical_len, NULL, 0u)) {
        free(canonical);
        return NULL;
    }
    return (char *)canonical;
}

static bool write_tags(const RSNFAV1Plan *plan) {
    uint32_t index;

    for (index = 0u; index < plan->nfa.tag_len; index++) {
        if (printf("lexical-tag\t%s\n", plan->tag_canonical[index]) < 0)
            return false;
    }
    return true;
}

static bool write_entries(const PPLexV1Plan *plan) {
    uint32_t index;

    for (index = 0u; index < plan->entry_len; index++) {
        const PPLexV1Entry *entry = &plan->entries[index];
        char *tag = render_term(entry->tag);
        int written;

        if (!tag)
            return false;
        written = printf(
            "lexical-entry\t%u\t%u\t%u\t%s\t%s\n",
            entry->tag_index,
            entry->state_id,
            entry->terminal_index,
            tag,
            entry->state_canonical);
        free(tag);
        if (written < 0)
            return false;
    }
    return true;
}

static bool run(const char *abi_path,
                const char *nfa_path,
                const char *regular_compiler_digest) {
    PPABIV1Wire pack_wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 answers;
    RSNFAV1Plan nfa;
    PPLexV1Plan plan;
    CettaLpNativeGrammar grammar;
    CettaLpNativeGrammarSummary grammar_summary;
    CettaLpNativeSlrSummary slr_summary;
    int32_t start_state_id;
    char closure_detail[512] = {0};
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&pack_wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&answers);
    rsnfa_v1_plan_init(&nfa);
    pplex_v1_plan_init(&plan);
    cetta_lp_native_grammar_init(&grammar);
    if (!ppabi_v1_wire_read(
            &pack_wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &pack_wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &answers, nfa_path, error, sizeof(error)) ||
        answers.len == 0u || answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &pack, answers.terms, answers.len,
            &nfa, error, sizeof(error)) ||
        !pplex_v1_plan_build(
            &pack, nfa.tags, nfa.nfa.tag_len,
            regular_compiler_digest, answers.digest,
            &plan, error, sizeof(error)) ||
        !pplex_v1_grammar_build(
            &pack, &plan, &grammar, error, sizeof(error))) {
        goto rejected;
    }
    if (!ppabi_v1_pack_start_is_closed(
            &pack, pack_wire.start,
            closure_detail, sizeof(closure_detail))) {
        (void)snprintf(
            error, sizeof(error),
            "lexically projected ParserPack start remains open: %.432s",
            closure_detail[0] ? closure_detail : "unknown state");
        goto rejected;
    }
    start_state_id = ppnative_v1_state_find(&pack, pack_wire.start);
    if (start_state_id < 0 || !cetta_lp_native_slr_summary(
            &grammar, (SymbolId)start_state_id, &slr_summary,
            error, sizeof(error))) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "failed to summarize lexical ParserPack SLR table");
        }
        goto rejected;
    }
    cetta_lp_native_grammar_summary(&grammar, &grammar_summary);
    printf("parser-pack-lexical-plan-v1\n");
    printf("base-pack-digest\t%s\n", pack.pack_digest);
    printf("base-states\t%u\n", pack.state_len);
    printf("base-terminals\t%u\n", pack.terminal_len);
    printf("base-productions\t%u\n", pack.production_len);
    printf("regular-compiler-digest\t%s\n", regular_compiler_digest);
    printf("lexical-nfa-answer-digest\t%s\n", answers.digest);
    printf("lexical-nfa-answers\t%zu\n", answers.len);
    printf("lexical-tag-count\t%u\n", nfa.nfa.tag_len);
    printf("lexical-plan-digest\t%s\n", plan.plan_digest);
    printf("lexical-terminal-extension-count\t%u\n", plan.entry_len);
    printf("projected-grammar-productions\t%u\n",
           grammar_summary.production_len);
    printf("slr-states\t%u\n", slr_summary.state_len);
    printf("slr-shifts\t%u\n", slr_summary.shift_len);
    printf("slr-gotos\t%u\n", slr_summary.goto_len);
    printf("slr-reductions\t%u\n", slr_summary.reduce_len);
    printf("slr-accepts\t%u\n", slr_summary.accept_len);
    printf("slr-conflicts\t%u\n", slr_summary.conflict_len);
    printf("start-closed\t1\n");
    if (!write_tags(&nfa) || !write_entries(&plan) ||
        printf("end\n") < 0) {
        (void)snprintf(error, sizeof(error),
                       "failed to write lexical plan stream");
        goto rejected;
    }
    ok = true;
    goto done;

rejected:
    fprintf(stderr, "lexical plan stream rejected: %s\n",
            error[0] ? error : "unknown rejection");

done:
    cetta_lp_native_grammar_free(&grammar);
    pplex_v1_plan_free(&plan);
    rsnfa_v1_plan_free(&nfa);
    fh_answer_stream_v1_free(&answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&pack_wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 4) {
        fprintf(stderr,
                "usage: parser_pack_lexical_plan_v1_stream "
                "ABI NFA REGULAR_COMPILER_SHA256\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3]);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
