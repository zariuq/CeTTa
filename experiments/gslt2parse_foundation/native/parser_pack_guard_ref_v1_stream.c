#define _POSIX_C_SOURCE 200809L

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_guard_evidence_stream_v1.h"
#include "parser_pack_guard_plan_v1.h"
#include "parser_pack_guard_ref_v1.h"
#include "regular_span_dfa_v1.h"
#include "regular_span_nfa_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_input(const char *path,
                       uint8_t **out,
                       size_t *out_len,
                       char *error,
                       size_t error_size) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input) {
        (void)snprintf(error, error_size, "cannot open guard input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size,
                               "guard input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "failed to allocate guard input");
                goto done;
            }
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(input)) {
            (void)snprintf(error, error_size,
                           "failed while reading guard input");
            goto done;
        }
        break;
    }
    *out = bytes;
    *out_len = len;
    bytes = NULL;
    ok = true;

done:
    if (input)
        (void)fclose(input);
    free(bytes);
    return ok;
}

static const char *parser_outcome_name(PPNativeV1Outcome outcome) {
    switch (outcome) {
    case PPNATIVE_V1_COMPLETED:
        return "completed";
    case PPNATIVE_V1_UNSUPPORTED_OPEN_PACK:
        return "unsupported-open-pack";
    case PPNATIVE_V1_RECOGNIZER_LIMIT:
        return "recognizer-limit";
    case PPNATIVE_V1_REPLAY_DEPTH:
        return "replay-depth";
    case PPNATIVE_V1_RESULT_LIMIT:
        return "result-limit";
    }
    return "unknown";
}

static bool write_results(const char *field,
                          const PPNativeV1Result *result) {
    uint32_t index;

    for (index = 0u; index < result->semantic_result_len; index++) {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        int written;
        if (!fh_ground_term_v1_render(
                result->semantic_results[index],
                &canonical, &canonical_len, NULL, 0u)) {
            free(canonical);
            return false;
        }
        written = printf(
            "%s\t%.*s\n", field, (int)canonical_len,
            (const char *)canonical);
        free(canonical);
        if (written < 0)
            return false;
    }
    return true;
}

static bool results_equal(const PPNativeV1Result *left,
                          const PPNativeV1Result *right) {
    uint32_t index;

    if (left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

static bool run(const char *abi_path,
                const char *guard_nfa_path,
                const char *guard_evidence_path,
                const char *input_path,
                const char *regular_compiler_digest) {
    PPABIV1Wire pack_wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 guard_answers;
    RSNFAV1Plan guard_nfa;
    RSDFAV1Plan dfa;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_COMPLETED;
    RSDFAV1ScanResult scan;
    PPGuardEvidenceWireV1 evidence;
    PPGuardPlanV1 plan;
    PPGuardPlanV1ProvenanceInput provenance;
    PPNativeV1TerminalPlan terminals = {0};
    RSDFAV1ParserLattice scalar_lattice;
    uint32_t *excluded_tags = NULL;
    PPGuardRelationV1 gll_relation;
    PPGuardRelationV1 glr_relation;
    PPGuardRefV1Receipt gll_witness = {0};
    PPGuardRefV1Receipt glr_witness = {0};
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    PPGuardPlanV1Receipt gll_receipt = {0};
    PPGuardPlanV1Receipt glr_receipt = {0};
    uint8_t *input = NULL;
    size_t input_len = 0u;
    uint32_t index;
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&pack_wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&guard_answers);
    rsnfa_v1_plan_init(&guard_nfa);
    rsdfa_v1_plan_init(&dfa);
    rsdfa_v1_scan_result_init(&scan);
    ppguard_evidence_wire_v1_init(&evidence);
    ppguard_plan_v1_init(&plan);
    rsdfa_v1_parser_lattice_init(&scalar_lattice);
    ppguard_relation_v1_init(&gll_relation);
    ppguard_relation_v1_init(&glr_relation);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    if (!ppabi_v1_wire_read(
            &pack_wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &pack_wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &guard_answers, guard_nfa_path, error, sizeof(error)) ||
        guard_answers.len == 0u ||
        !rsnfa_v1_plan_load(
            &pack, guard_answers.terms, guard_answers.len,
            &guard_nfa, error, sizeof(error)) ||
        !ppguard_evidence_wire_v1_read(
            &evidence, guard_evidence_path, error, sizeof(error)) ||
        !read_input(
            input_path, &input, &input_len, error, sizeof(error))) {
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
            &pack, NULL, &provenance, &plan,
            error, sizeof(error)) ||
        !rsdfa_v1_plan_build(
            &guard_nfa.nfa, UINT32_C(65536), UINT32_C(2000000),
            &dfa, &build_outcome, error, sizeof(error)) ||
        build_outcome != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_scan(
            &dfa, input, input_len, UINT64_C(20000000),
            UINT32_C(2000000), &scan, error, sizeof(error)) ||
        scan.outcome != RSDFA_V1_SCAN_COMPLETED) {
        if (!error[0]) {
            (void)snprintf(error, sizeof(error),
                           "guard DFA did not complete");
        }
        goto rejected;
    }
    excluded_tags = malloc(
        sizeof(*excluded_tags) * (size_t)scan.tag_len);
    if (!excluded_tags)
        goto rejected;
    for (index = 0u; index < scan.tag_len; index++)
        excluded_tags[index] = UINT32_MAX;
    if (!ppnative_v1_terminal_plan_build(
            &pack, &terminals, error, sizeof(error)) ||
        !rsdfa_v1_composite_parser_lattice_build(
            &scan, terminals.terminals, terminals.len,
            excluded_tags, scan.tag_len,
            &scalar_lattice, error, sizeof(error)) ||
        !ppguard_ref_v1_relation_build_gll(
            &pack, &plan, guard_nfa.tags, guard_nfa.nfa.tag_len,
            &scan, &scalar_lattice.lattice, NULL, 0u,
            UINT32_C(10000000), 4096u, UINT32_C(1000000),
            &gll_relation, &gll_witness, error, sizeof(error)) ||
        !ppguard_ref_v1_relation_build_glr(
            &pack, &plan, guard_nfa.tags, guard_nfa.nfa.tag_len,
            &scan, &scalar_lattice.lattice, NULL, 0u,
            UINT32_C(10000000), 4096u, UINT32_C(1000000),
            &glr_relation, &glr_witness, error, sizeof(error)) ||
        strcmp(gll_relation.relation_digest,
               glr_relation.relation_digest) != 0 ||
        gll_witness.parser_run_len != glr_witness.parser_run_len ||
        gll_witness.match_len != glr_witness.match_len) {
        if (!error[0]) {
            (void)snprintf(error, sizeof(error),
                           "GLL and GLR guard witnesses differ");
        }
        goto rejected;
    }
    if (!ppguard_plan_v1_gll_parse(
            &pack, pack_wire.start, NULL, &plan, &gll_relation,
            UINT32_C(50000000), 4096u, UINT32_C(1000000),
            &gll, &gll_receipt, error, sizeof(error)) ||
        !ppguard_plan_v1_glr_parse(
            &pack, pack_wire.start, NULL, &plan, &glr_relation,
            UINT32_C(50000000), 4096u, UINT32_C(1000000),
            &glr, &glr_receipt, error, sizeof(error)) ||
        !results_equal(&gll, &glr)) {
        if (!error[0]) {
            (void)snprintf(error, sizeof(error),
                           "GLL and GLR guarded parses differ");
        }
        goto rejected;
    }

    printf("parser-pack-positive-guard-ref-exec-v1\n");
    printf("base-pack-digest\t%s\n", pack.pack_digest);
    printf("guard-plan-digest\t%s\n", plan.plan_digest);
    printf("guard-evidence-digest\t%s\n", plan.evidence_digest);
    printf("guard-nfa-answer-digest\t%s\n", guard_answers.digest);
    printf("guard-nfa-answers\t%zu\n", guard_answers.len);
    printf("guard-tags\t%u\n", guard_nfa.nfa.tag_len);
    printf("dfa-states\t%u\n", dfa.state_len);
    printf("dfa-transitions\t%u\n", dfa.transition_len);
    printf("source-passes\t%u\n", scan.source_pass_count);
    printf("input-bytes\t%u\n", scan.input_byte_len);
    printf("input-scalars\t%u\n", scan.input_scalar_len);
    printf("guard-tokens\t%u\n", scan.token_len);
    printf("guard-witness-runs\t%u\n", gll_witness.parser_run_len);
    printf("guard-witness-matches\t%u\n", gll_witness.match_len);
    printf("guard-witness-work-items-gll\t%llu\n",
           (unsigned long long)gll_witness.work_item_len);
    printf("guard-witness-work-items-glr\t%llu\n",
           (unsigned long long)glr_witness.work_item_len);
    printf("guard-relation-digest\t%s\n", gll_relation.relation_digest);
    printf("gll-outcome\t%s\n", parser_outcome_name(gll.outcome));
    printf("glr-outcome\t%s\n", parser_outcome_name(glr.outcome));
    printf("gll-decision\t%s\n", gll.accepted ? "accepted" : "rejected");
    printf("glr-decision\t%s\n", glr.accepted ? "accepted" : "rejected");
    printf("gll-forest-digest\t%s\n", gll.forest_digest);
    printf("glr-forest-digest\t%s\n", glr.forest_digest);
    printf("gll-forest-nodes\t%u\n", gll.forest.node_len);
    printf("glr-forest-nodes\t%u\n", glr.forest.node_len);
    printf("gll-execution-digest\t%s\n", gll_receipt.execution_digest);
    printf("glr-execution-digest\t%s\n", glr_receipt.execution_digest);
    if (!write_results("gll-result", &gll) ||
        !write_results("glr-result", &glr)) {
        (void)snprintf(error, sizeof(error),
                       "failed to write guarded parse results");
        goto rejected;
    }
    printf("end\n");
    ok = true;
    goto done;

rejected:
    fprintf(stderr, "guarded ParserPack execution rejected: %s\n",
            error[0] ? error : "unknown rejection");

done:
    free(input);
    free(excluded_tags);
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    ppguard_relation_v1_free(&glr_relation);
    ppguard_relation_v1_free(&gll_relation);
    rsdfa_v1_parser_lattice_free(&scalar_lattice);
    ppnative_v1_terminal_plan_free(&terminals);
    ppguard_plan_v1_free(&plan);
    ppguard_evidence_wire_v1_free(&evidence);
    rsdfa_v1_scan_result_free(&scan);
    rsdfa_v1_plan_free(&dfa);
    rsnfa_v1_plan_free(&guard_nfa);
    fh_answer_stream_v1_free(&guard_answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&pack_wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 6) {
        fprintf(stderr,
                "usage: parser_pack_guard_ref_v1_stream ABI GUARD_NFA "
                "GUARD_EVIDENCE INPUT REGULAR_COMPILER_SHA256\n");
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
