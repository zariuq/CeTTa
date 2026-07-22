#define _POSIX_C_SOURCE 200809L

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_lexical_v1.h"
#include "regular_span_dfa_v1.h"
#include "regular_span_nfa_v1.h"

#include "symbol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct {
    uint32_t state_limit;
    uint32_t transition_limit;
    uint64_t work_limit;
    uint32_t token_limit;
    bool summary_only;
    const char *project_start;
    const char *regular_compiler_digest;
} RSDFAV1Options;

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
        (void)snprintf(error, error_size, "cannot open DFA input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size, "DFA input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "failed to allocate DFA input");
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
                           "failed while reading DFA input");
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

static const char *build_outcome_name(RSDFAV1BuildOutcome outcome) {
    switch (outcome) {
    case RSDFA_V1_BUILD_COMPLETED:
        return "completed";
    case RSDFA_V1_BUILD_STATE_LIMIT:
        return "dfa-state-limit";
    case RSDFA_V1_BUILD_TRANSITION_LIMIT:
        return "dfa-transition-limit";
    case RSDFA_V1_BUILD_ANNOTATION_CONFLICT:
        return "dfa-annotation-conflict";
    }
    return "unknown";
}

static const char *scan_outcome_name(RSDFAV1ScanOutcome outcome) {
    switch (outcome) {
    case RSDFA_V1_SCAN_COMPLETED:
        return "completed";
    case RSDFA_V1_SCAN_WORK_LIMIT:
        return "scan-work-limit";
    case RSDFA_V1_SCAN_TOKEN_LIMIT:
        return "token-limit";
    }
    return "unknown";
}

static bool write_tokens(const RSNFAV1Plan *nfa_plan,
                         const RSDFAV1ScanResult *result) {
    uint32_t index;

    for (index = 0u; index < result->token_len; index++) {
        const RSDFAV1Token *token = &result->tokens[index];
        const char *tag;
        if (token->tag >= nfa_plan->nfa.tag_len)
            return false;
        tag = nfa_plan->tag_canonical[token->tag];
        if (printf("token\t%s\t%u\t%u\t%u\t%u\n",
                   tag,
                   token->start_scalar, token->end_scalar,
                   token->start_byte, token->end_byte) < 0) {
            return false;
        }
    }
    return true;
}

static const char *witness_outcome_name(PPLexV1WitnessOutcome outcome) {
    switch (outcome) {
    case PPLEX_V1_WITNESS_COMPLETED:
        return "completed";
    case PPLEX_V1_WITNESS_RECOGNIZER_LIMIT:
        return "recognizer-limit";
    case PPLEX_V1_WITNESS_RECOGNITION_MISMATCH:
        return "recognition-mismatch";
    case PPLEX_V1_WITNESS_NONFUNCTIONAL:
        return "nonfunctional";
    case PPLEX_V1_WITNESS_REPLAY_DEPTH:
        return "replay-depth";
    case PPLEX_V1_WITNESS_RESULT_LIMIT:
        return "result-limit";
    }
    return "unknown";
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

static bool write_semantic_results(const char *field,
                                   const PPNativeV1Result *result) {
    uint32_t index;

    for (index = 0u; index < result->semantic_result_len; index++) {
        uint8_t *rendered = NULL;
        size_t rendered_len = 0u;
        if (!fh_ground_term_v1_render(
                result->semantic_results[index], &rendered, &rendered_len,
                NULL, 0u)) {
            free(rendered);
            return false;
        }
        if (printf("%s\t%.*s\n", field, (int)rendered_len,
                   (const char *)rendered) < 0) {
            free(rendered);
            return false;
        }
        free(rendered);
    }
    return true;
}

static bool witness_tables_equal(const PPLexV1WitnessTable *left,
                                 const PPLexV1WitnessTable *right) {
    uint32_t index;

    if (left->outcome != PPLEX_V1_WITNESS_COMPLETED ||
        right->outcome != PPLEX_V1_WITNESS_COMPLETED ||
        left->len != right->len) {
        return false;
    }
    for (index = 0u; index < left->len; index++) {
        if (!left->values[index] || !right->values[index] ||
            !atom_eq(left->values[index], right->values[index])) {
            return false;
        }
    }
    return true;
}

static bool run_projection(const PPABIV1Pack *pack,
                           const RSNFAV1Plan *nfa_plan,
                           const char *nfa_answer_digest,
                           const RSDFAV1ScanResult *scan,
                           const RSDFAV1Options *options,
                           char *error,
                           size_t error_size) {
    Arena arena;
    Atom *start_state = NULL;
    uint8_t *canonical_start = NULL;
    size_t canonical_start_len = 0u;
    size_t start_len = strlen(options->project_start);
    PPLexV1Plan projection;
    PPLexV1WitnessTable gll_witnesses;
    PPLexV1WitnessTable glr_witnesses;
    PPNativeV1TerminalPlan terminals = {0};
    RSDFAV1ParserLattice lattice;
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    bool ok = false;

    arena_init(&arena);
    pplex_v1_plan_init(&projection);
    pplex_v1_witness_table_init(&gll_witnesses);
    pplex_v1_witness_table_init(&glr_witnesses);
    rsdfa_v1_parser_lattice_init(&lattice);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    if (!fh_ground_term_v1_parse(
            &arena, (const uint8_t *)options->project_start, start_len,
            &start_state, error, error_size) ||
        !fh_ground_term_v1_render(
            start_state, &canonical_start, &canonical_start_len,
            error, error_size) ||
        canonical_start_len != start_len ||
        memcmp(canonical_start, options->project_start, start_len) != 0) {
        if (!error[0])
            (void)snprintf(error, error_size,
                           "projection start state is not canonical");
        goto done;
    }
    if (!pplex_v1_plan_build(
            pack, nfa_plan->tags, nfa_plan->nfa.tag_len,
            options->regular_compiler_digest, nfa_answer_digest,
            &projection, error, error_size) ||
        !pplex_v1_witness_table_build_gll(
            pack, &projection, scan, UINT32_C(10000000), 4096u,
            UINT32_C(1000000), &gll_witnesses, error, error_size) ||
        !pplex_v1_witness_table_build_glr(
            pack, &projection, scan, UINT32_C(10000000), 4096u,
            UINT32_C(1000000), &glr_witnesses, error, error_size)) {
        goto done;
    }
    printf("projection-start\t%.*s\n",
           (int)canonical_start_len, (const char *)canonical_start);
    printf("regular-compiler-digest\t%s\n",
           options->regular_compiler_digest);
    printf("projection-plan-digest\t%s\n", projection.plan_digest);
    printf("projection-gll-witness-outcome\t%s\n",
           witness_outcome_name(gll_witnesses.outcome));
    printf("projection-glr-witness-outcome\t%s\n",
           witness_outcome_name(glr_witnesses.outcome));
    printf("projection-gll-witness-runs\t%u\n",
           gll_witnesses.parser_runs);
    printf("projection-glr-witness-runs\t%u\n",
           glr_witnesses.parser_runs);
    printf("projection-gll-witness-work-items\t%llu\n",
           (unsigned long long)gll_witnesses.work_item_len);
    printf("projection-glr-witness-work-items\t%llu\n",
           (unsigned long long)glr_witnesses.work_item_len);
    if (gll_witnesses.outcome != PPLEX_V1_WITNESS_COMPLETED ||
        glr_witnesses.outcome != PPLEX_V1_WITNESS_COMPLETED) {
        ok = true;
        goto done;
    }
    if (!witness_tables_equal(&gll_witnesses, &glr_witnesses)) {
        (void)snprintf(error, error_size,
                       "GLL and GLR lexical witnesses differ");
        goto done;
    }
    if (!ppnative_v1_terminal_plan_build(
            pack, &terminals, error, error_size) ||
        !rsdfa_v1_composite_parser_lattice_build(
            scan, terminals.terminals, terminals.len,
            projection.tag_terminal_ids, projection.entry_len,
            &lattice, error, error_size) ||
        !pplex_v1_gll_parse(
            pack, start_state, &projection, &lattice.lattice,
            gll_witnesses.values, gll_witnesses.len,
            UINT32_C(10000000), 4096u, UINT32_C(1000000),
            &gll, error, error_size) ||
        !pplex_v1_glr_parse(
            pack, start_state, &projection, &lattice.lattice,
            glr_witnesses.values, glr_witnesses.len,
            UINT32_C(10000000), 4096u, UINT32_C(1000000),
            &glr, error, error_size)) {
        goto done;
    }
    printf("projection-source-passes\t%u\n",
           lattice.lattice.source_pass_count);
    printf("projection-decoded-bytes\t%u\n",
           lattice.lattice.decoded_byte_len);
    printf("projection-witness-values\t%u\n", gll_witnesses.len);
    printf("projection-gll-outcome\t%s\n",
           parser_outcome_name(gll.outcome));
    printf("projection-glr-outcome\t%s\n",
           parser_outcome_name(glr.outcome));
    printf("projection-gll-decision\t%s\n",
           gll.accepted ? "accepted" : "rejected");
    printf("projection-glr-decision\t%s\n",
           glr.accepted ? "accepted" : "rejected");
    printf("projection-gll-forest-digest\t%s\n", gll.forest_digest);
    printf("projection-glr-forest-digest\t%s\n", glr.forest_digest);
    printf("projection-gll-forest-nodes\t%u\n", gll.forest.node_len);
    printf("projection-glr-forest-nodes\t%u\n", glr.forest.node_len);
    if (!write_semantic_results("projection-gll-result", &gll) ||
        !write_semantic_results("projection-glr-result", &glr)) {
        (void)snprintf(error, error_size,
                       "failed to write projected semantic results");
        goto done;
    }
    ok = true;

done:
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    rsdfa_v1_parser_lattice_free(&lattice);
    ppnative_v1_terminal_plan_free(&terminals);
    pplex_v1_witness_table_free(&glr_witnesses);
    pplex_v1_witness_table_free(&gll_witnesses);
    pplex_v1_plan_free(&projection);
    free(canonical_start);
    arena_free(&arena);
    return ok;
}

static bool run(const char *abi_path,
                const char *nfa_path,
                const char *input_path,
                const RSDFAV1Options *options) {
    PPABIV1Wire pack_wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 nfa_wire;
    RSNFAV1Plan nfa_plan;
    RSDFAV1Plan dfa_plan;
    RSDFAV1ScanResult result;
    RSDFAV1BuildOutcome build_outcome;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&pack_wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&nfa_wire);
    rsnfa_v1_plan_init(&nfa_plan);
    rsdfa_v1_plan_init(&dfa_plan);
    rsdfa_v1_scan_result_init(&result);
    if (!ppabi_v1_wire_read(
            &pack_wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &pack_wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &nfa_wire, nfa_path, error, sizeof(error)) ||
        nfa_wire.len == 0u ||
        !rsnfa_v1_plan_load(
            &pack, nfa_wire.terms, nfa_wire.len,
            &nfa_plan, error, sizeof(error)) ||
        !rsdfa_v1_plan_build(
            &nfa_plan.nfa,
            options->state_limit, options->transition_limit,
            &dfa_plan, &build_outcome,
            error, sizeof(error))) {
        fprintf(stderr, "regular-span DFA stream rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    printf("regular-span-dfa-v1\n");
    printf("build-outcome\t%s\n", build_outcome_name(build_outcome));
    printf("source-digest\t%s\n", pack.source_digest);
    printf("pack-digest\t%s\n", pack.pack_digest);
    printf("nfa-answer-digest\t%s\n", nfa_wire.digest);
    printf("nfa-answers\t%zu\n", nfa_wire.len);
    printf("tags\t%u\n", nfa_plan.nfa.tag_len);
    printf("nfa-states\t%u\n", nfa_plan.nfa.state_len);
    printf("nfa-edges\t%u\n", nfa_plan.nfa.edge_len);
    if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
        printf("end\n");
        ok = true;
        goto done;
    }
    if (!read_input(input_path, &input, &input_len,
                    error, sizeof(error)) ||
        !rsdfa_v1_scan(
            &dfa_plan, input, input_len,
            options->work_limit, options->token_limit,
            &result, error, sizeof(error))) {
        fprintf(stderr, "regular-span DFA scan rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    printf("scan-outcome\t%s\n", scan_outcome_name(result.outcome));
    printf("dfa-states\t%u\n", dfa_plan.state_len);
    printf("dfa-transitions\t%u\n", dfa_plan.transition_len);
    printf("input-bytes\t%u\n", result.input_byte_len);
    printf("input-scalars\t%u\n", result.input_scalar_len);
    printf("decoded-bytes\t%u\n", result.decoded_byte_len);
    printf("source-passes\t%u\n", result.source_pass_count);
    printf("work-items\t%llu\n",
           (unsigned long long)result.work_item_len);
    printf("token-count\t%u\n", result.token_len);
    if (!options->summary_only && !write_tokens(&nfa_plan, &result)) {
        fprintf(stderr, "failed to write regular-span tokens\n");
        goto done;
    }
    if (options->project_start &&
        !run_projection(
            &pack, &nfa_plan, nfa_wire.digest, &result, options,
            error, sizeof(error))) {
        fprintf(stderr, "lexical projection rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    printf("end\n");
    ok = true;

done:
    free(input);
    rsdfa_v1_scan_result_free(&result);
    rsdfa_v1_plan_free(&dfa_plan);
    rsnfa_v1_plan_free(&nfa_plan);
    fh_answer_stream_v1_free(&nfa_wire);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&pack_wire);
    return ok;
}

static bool parse_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    const char *cursor;
    unsigned long long parsed;

    if (!text || *text == '\0')
        return false;
    for (cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9')
            return false;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || !end || *end != '\0' || parsed == 0u)
        return false;
    *value = (uint64_t)parsed;
    return (unsigned long long)*value == parsed;
}

static bool parse_u32(const char *text, uint32_t *value) {
    uint64_t parsed;

    if (!parse_u64(text, &parsed) || parsed > UINT32_MAX)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_options(int argc,
                          char **argv,
                          RSDFAV1Options *options) {
    int index;

    *options = (RSDFAV1Options){
        .state_limit = 65536u,
        .transition_limit = 2000000u,
        .work_limit = UINT64_C(20000000),
        .token_limit = UINT32_C(2000000),
        .summary_only = false,
        .project_start = NULL,
        .regular_compiler_digest = NULL,
    };
    for (index = 4; index < argc; index++) {
        if (strcmp(argv[index], "--summary") == 0) {
            options->summary_only = true;
        } else if (strcmp(argv[index], "--state-limit") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], &options->state_limit))
                return false;
        } else if (strcmp(argv[index], "--transition-limit") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], &options->transition_limit))
                return false;
        } else if (strcmp(argv[index], "--work-limit") == 0 &&
                   index + 1 < argc) {
            if (!parse_u64(argv[++index], &options->work_limit))
                return false;
        } else if (strcmp(argv[index], "--token-limit") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], &options->token_limit))
                return false;
        } else if (strcmp(argv[index], "--project-start") == 0 &&
                   index + 1 < argc) {
            options->project_start = argv[++index];
        } else if (strcmp(argv[index], "--regular-compiler-digest") == 0 &&
                   index + 1 < argc) {
            options->regular_compiler_digest = argv[++index];
        } else {
            return false;
        }
    }
    return (options->project_start == NULL) ==
        (options->regular_compiler_digest == NULL);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    RSDFAV1Options options;
    bool ok;

    if (argc < 4 || !parse_options(argc, argv, &options)) {
        fprintf(stderr,
                "usage: regular_span_dfa_v1_stream ABI NFA INPUT "
                "[--summary] [--state-limit N] [--transition-limit N] "
                "[--work-limit N] [--token-limit N] "
                "[--project-start TERM --regular-compiler-digest SHA256]\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], &options);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
