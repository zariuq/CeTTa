#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_guard_evidence_stream_v1.h"
#include "parser_pack_guard_plan_v1.h"
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

static bool write_lexical_entries(const PPLexV1Plan *plan) {
    uint32_t index;

    for (index = 0u; index < plan->entry_len; index++) {
        char *tag = render_term(plan->entries[index].tag);
        int written;

        if (!tag)
            return false;
        written = printf(
            "lexical-entry\t%u\t%u\t%u\t%s\t%s\n",
            plan->entries[index].tag_index,
            plan->entries[index].state_id,
            plan->entries[index].terminal_index,
            tag,
            plan->entries[index].state_canonical);
        free(tag);
        if (written < 0)
            return false;
    }
    return true;
}

static bool write_guard_entries(const PPGuardPlanV1 *plan) {
    uint32_t index;

    for (index = 0u; index < plan->entry_len; index++) {
        const PPGuardPlanV1Entry *entry = &plan->entries[index];
        char *owner = render_term(entry->owner);
        char *state = render_term(entry->state);
        char *tag = render_term(entry->tag);
        char *body = render_term(entry->body);
        char *production = render_term(entry->production);
        int written = -1;

        if (owner && state && tag && body && production) {
            written = printf(
                "guard-entry\t%u\t%u\t%u\t%u\t%s\t%s\t%s\t%s\t%s\n",
                entry->state_id,
                entry->terminal_id,
                entry->production_id,
                entry->state_is_extension ? 1u : 0u,
                owner,
                state,
                tag,
                body,
                production);
        }
        free(owner);
        free(state);
        free(tag);
        free(body);
        free(production);
        if (written < 0 ||
            printf("guard-entry-evidence\t%u\t%u\t%u\n",
                   index, entry->evidence_begin,
                   entry->evidence_len) < 0) {
            return false;
        }
    }
    return true;
}

static bool write_tag_records(const char *field,
                              const RSNFAV1Plan *plan) {
    uint32_t index;

    for (index = 0u; index < plan->nfa.tag_len; index++) {
        if (printf("%s\t%s\n", field, plan->tag_canonical[index]) < 0)
            return false;
    }
    return true;
}

static bool run(const char *abi_path,
                const char *lexical_nfa_path,
                const char *guard_nfa_path,
                const char *guard_evidence_path,
                const char *regular_compiler_digest) {
    PPABIV1Wire pack_wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 lexical_answers;
    FHAnswerStreamV1 guard_answers;
    RSNFAV1Plan lexical_nfa;
    RSNFAV1Plan guard_nfa;
    PPLexV1Plan lexical_plan;
    PPGuardEvidenceWireV1 evidence;
    PPGuardPlanV1 plan;
    PPGuardPlanV1ProvenanceInput provenance;
    PPNativeV1ForestExtension extension;
    CettaLpNativeGrammar grammar;
    CettaLpNativeGrammarSummary grammar_summary;
    CettaLpNativeSlrSummary slr_summary;
    int32_t start_state_id;
    char closure_detail[512] = {0};
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&pack_wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&lexical_answers);
    fh_answer_stream_v1_init(&guard_answers);
    rsnfa_v1_plan_init(&lexical_nfa);
    rsnfa_v1_plan_init(&guard_nfa);
    pplex_v1_plan_init(&lexical_plan);
    ppguard_evidence_wire_v1_init(&evidence);
    ppguard_plan_v1_init(&plan);
    cetta_lp_native_grammar_init(&grammar);
    if (!ppabi_v1_wire_read(
            &pack_wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &pack_wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &lexical_answers, lexical_nfa_path,
            error, sizeof(error)) ||
        lexical_answers.len == 0u ||
        lexical_answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &pack, lexical_answers.terms, lexical_answers.len,
            &lexical_nfa, error, sizeof(error)) ||
        !pplex_v1_plan_build(
            &pack, lexical_nfa.tags, lexical_nfa.nfa.tag_len,
            regular_compiler_digest, lexical_answers.digest,
            &lexical_plan, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &guard_answers, guard_nfa_path,
            error, sizeof(error)) ||
        guard_answers.len == 0u ||
        !rsnfa_v1_plan_load(
            &pack, guard_answers.terms, guard_answers.len,
            &guard_nfa, error, sizeof(error)) ||
        !ppguard_evidence_wire_v1_read(
            &evidence, guard_evidence_path,
            error, sizeof(error))) {
        goto rejected;
    }
    provenance = (PPGuardPlanV1ProvenanceInput){
        .source_digest = evidence.source_digest,
        .pre_reflection_digest = evidence.pre_reflection_digest,
        .environment_digest = evidence.environment_digest,
        .answer_set_digest = evidence.answer_set_digest,
        .regular_compiler_digest = regular_compiler_digest,
        .guard_nfa_answer_digest = guard_answers.digest,
        .guard_nfa_tags = guard_nfa.tags,
        .guard_nfa_tag_len = guard_nfa.nfa.tag_len,
        .derivations = evidence.derivations,
        .derivation_len = evidence.derivation_len,
    };
    if (!ppguard_plan_v1_build(
            &pack, &lexical_plan, &provenance,
            &plan, error, sizeof(error)) ||
        !ppguard_plan_v1_grammar_build(
            &pack, &lexical_plan, &plan,
            &grammar, error, sizeof(error))) {
        goto rejected;
    }
    extension = (PPNativeV1ForestExtension){
        .states = plan.states,
        .state_len = plan.state_len,
        .terminals = plan.terminals,
        .terminal_len = plan.terminal_len,
        .productions = plan.productions,
        .production_len = plan.production_len,
    };
    if (!ppnative_v1_start_is_closed_extended(
            &pack, pack_wire.start, &extension,
            closure_detail, sizeof(closure_detail))) {
        (void)snprintf(
            error, sizeof(error), "composite ParserPack start remains open: %.448s",
            closure_detail[0] ? closure_detail : "unknown state");
        goto rejected;
    }
    start_state_id = ppnative_v1_state_find_extended(
        &pack, &extension, pack_wire.start);
    if (start_state_id < 0 || !cetta_lp_native_slr_summary(
            &grammar, (SymbolId)start_state_id, &slr_summary,
            error, sizeof(error))) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "failed to summarize composite ParserPack SLR table");
        }
        goto rejected;
    }
    cetta_lp_native_grammar_summary(&grammar, &grammar_summary);
    printf("parser-pack-positive-guard-plan-v1\n");
    printf("base-pack-digest\t%s\n", pack.pack_digest);
    printf("base-states\t%u\n", pack.state_len);
    printf("base-terminals\t%u\n", pack.terminal_len);
    printf("base-productions\t%u\n", pack.production_len);
    printf("regular-compiler-digest\t%s\n",
           regular_compiler_digest);
    printf("lexical-nfa-answer-digest\t%s\n", lexical_answers.digest);
    printf("lexical-nfa-answers\t%zu\n", lexical_answers.len);
    printf("lexical-tag-count\t%u\n", lexical_nfa.nfa.tag_len);
    printf("lexical-plan-digest\t%s\n", lexical_plan.plan_digest);
    if (!write_tag_records("lexical-tag", &lexical_nfa) ||
        !write_lexical_entries(&lexical_plan)) {
        goto write_failed;
    }
    printf("guard-source-digest\t%s\n", evidence.source_digest);
    printf("guard-pre-reflection-digest\t%s\n",
           evidence.pre_reflection_digest);
    printf("guard-environment-digest\t%s\n",
           evidence.environment_digest);
    printf("guard-answer-set-digest\t%s\n",
           evidence.answer_set_digest);
    printf("guard-evidence-count\t%zu\n", evidence.derivation_len);
    printf("guard-nfa-answer-digest\t%s\n", guard_answers.digest);
    printf("guard-nfa-answers\t%zu\n", guard_answers.len);
    printf("guard-tag-count\t%u\n", guard_nfa.nfa.tag_len);
    if (!write_tag_records("guard-tag", &guard_nfa))
        goto write_failed;
    printf("guard-plan-digest\t%s\n", plan.plan_digest);
    printf("guard-evidence-digest\t%s\n", plan.evidence_digest);
    printf("guard-entry-count\t%u\n", plan.entry_len);
    printf("guard-state-extension-count\t%u\n", plan.state_len);
    printf("composite-terminal-extension-count\t%u\n",
           plan.terminal_len);
    printf("guard-production-extension-count\t%u\n",
           plan.production_len);
    printf("composite-grammar-productions\t%u\n",
           grammar_summary.production_len);
    printf("slr-states\t%u\n", slr_summary.state_len);
    printf("slr-shifts\t%u\n", slr_summary.shift_len);
    printf("slr-gotos\t%u\n", slr_summary.goto_len);
    printf("slr-reductions\t%u\n", slr_summary.reduce_len);
    printf("slr-accepts\t%u\n", slr_summary.accept_len);
    printf("slr-conflicts\t%u\n", slr_summary.conflict_len);
    printf("start-closed\t1\n");
    if (!write_guard_entries(&plan))
        goto write_failed;
    printf("end\n");
    ok = true;
    goto done;

write_failed:
    (void)snprintf(error, sizeof(error),
                   "failed to write positive guard plan stream");
rejected:
    fprintf(stderr, "positive guard plan stream rejected: %s\n",
            error[0] ? error : "unknown rejection");

done:
    cetta_lp_native_grammar_free(&grammar);
    ppguard_plan_v1_free(&plan);
    ppguard_evidence_wire_v1_free(&evidence);
    pplex_v1_plan_free(&lexical_plan);
    rsnfa_v1_plan_free(&guard_nfa);
    rsnfa_v1_plan_free(&lexical_nfa);
    fh_answer_stream_v1_free(&guard_answers);
    fh_answer_stream_v1_free(&lexical_answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&pack_wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 6) {
        fprintf(stderr,
                "usage: parser_pack_guard_plan_v1_stream ABI "
                "LEXICAL_NFA GUARD_NFA GUARD_EVIDENCE "
                "REGULAR_COMPILER_SHA256\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], argv[4], argv[5]);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
