/* Build-time emission through the same interval DFA compiler as ParserPack.
 * Input is a numeric NFA (states, start, edges, accepts, tags), followed by
 * edges (from,to,kind,range count,low/high pairs) and accepts (state,tag).
 * Runtime consumers receive only immutable transition and accept tables. */
#include "regular_span_dfa_v1.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *input = fopen(argv[1], "r"), *out = NULL;
    RSDFAV1Nfa nfa = {0};
    RSDFAV1Plan plan; rsdfa_v1_plan_init(&plan);
    RSDFAV1Program program; rsdfa_v1_program_init(&program);
    RSDFAV1BuildOutcome outcome;
    uint32_t start;
    RSDFAV1NfaEdge *edges = NULL;
    RSDFAV1NfaAccept *accepts = NULL;
    int result = 1;
    char error[512] = {0};
    if (!input || fscanf(input, "%u %u %u %u %u", &nfa.state_len, &start,
                         &nfa.edge_len, &nfa.accept_len, &nfa.tag_len) != 5) goto done;
    if (!nfa.state_len || nfa.tag_len > 8 || nfa.edge_len > 1000000) goto done;
    edges = calloc(nfa.edge_len, sizeof(*edges));
    accepts = calloc(nfa.accept_len, sizeof(*accepts));
    if (!edges || !accepts) goto done;
    nfa.start_states = &start; nfa.start_len = 1; nfa.edges = edges; nfa.accepts = accepts;
    for (uint32_t i = 0; i < nfa.edge_len; i++) {
        unsigned kind;
        if (fscanf(input, "%u %u %u %u", &edges[i].from, &edges[i].to,
                   &kind, &edges[i].range_len) != 4 || kind > 2 || edges[i].range_len > 1000000)
            goto done;
        edges[i].kind = (RSDFAV1NfaEdgeKind)kind;
        CettaLpNativeUnicodeRange *ranges = edges[i].range_len ? calloc(edges[i].range_len, sizeof(*ranges)) : NULL;
        edges[i].ranges = ranges;
        if (edges[i].range_len && !ranges) goto done;
        for (uint32_t j = 0; j < edges[i].range_len; j++)
            if (fscanf(input, "%u %u", &ranges[j].low, &ranges[j].high) != 2) goto done;
    }
    for (uint32_t i = 0; i < nfa.accept_len; i++)
        if (fscanf(input, "%u %u", &accepts[i].state, &accepts[i].tag) != 2) goto done;
    if (!rsdfa_v1_plan_build(&nfa, 65536, 1048576, &plan, &outcome, error, sizeof(error)) ||
        outcome != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_plan_export_program(&plan, &program, error, sizeof(error)) ||
        !rsdfa_v1_program_validate(&program, error, sizeof(error))) goto done;
    out = fopen(argv[2], "w"); if (!out) goto done;
    fprintf(out, "/* Generated interval DFA; do not edit. */\n");
    fprintf(out, "typedef struct { unsigned low, high, target; } JsonLexEdgeV1;\n");
    fprintf(out, "static const JsonLexEdgeV1 jd_lex_edges[] = {\n");
    for (uint32_t i = 0; i < program.transition_len; i++) {
        RSDFAV1ProgramTransition t = program.transitions[i];
        fprintf(out, "{%u,%u,%u},\n", t.low, t.high, t.target);
    }
    fprintf(out, "};\nstatic const unsigned jd_lex_offsets[][2] = {\n");
    for (uint32_t i = 0; i < program.state_len; i++)
        fprintf(out, "{%u,%u},\n", program.states[i].transition_begin, program.states[i].transition_len);
    fprintf(out, "};\nstatic const unsigned jd_lex_accept[] = {\n");
    for (uint32_t i = 0; i < program.state_len; i++) {
        uint32_t mask = 0;
        for (uint32_t j = 0; j < program.states[i].accept_len; j++)
            mask |= 1u << program.accept_tags[program.states[i].accept_begin+j];
        fprintf(out, "%u,", mask);
    }
    fprintf(out, "\n};\nstatic const unsigned jd_lex_ascii[][128] = {\n");
    for (uint32_t i = 0; i < program.state_len; i++) {
        fprintf(out, "{");
        for (uint32_t cp = 0; cp < 128; cp++) {
            uint32_t target = UINT32_MAX;
            for (uint32_t j = 0; j < program.states[i].transition_len; j++) {
                RSDFAV1ProgramTransition t = program.transitions[program.states[i].transition_begin+j];
                if (t.low <= cp && cp <= t.high) { target = t.target; break; }
            }
            fprintf(out, "%u%s", target, cp==127 ? "},\n" : ",");
        }
    }
    fprintf(out, "};\n#define JD_LEX_START %uu\n", program.start_state);
    fprintf(out, "static unsigned jd_lex_step(unsigned state, unsigned cp) {\n"
        "if (cp < 128) return jd_lex_ascii[state][cp];\n"
        "unsigned begin=jd_lex_offsets[state][0], end=begin+jd_lex_offsets[state][1];\n"
        "while(begin<end) { unsigned mid=begin+(end-begin)/2; JsonLexEdgeV1 t=jd_lex_edges[mid];\n"
        "if(cp<t.low) end=mid; else if(cp>t.high) begin=mid+1; else return t.target; }\n"
        "return UINT32_MAX; }\n");
    if (fclose(out)) { out = NULL; goto done; } out = NULL;
    result = 0;
done:
    if (result) fprintf(stderr,"DFA emission failed: %s\n",error);
    if (input) fclose(input);
    if (out) fclose(out);
    for (uint32_t i = 0; edges && i < nfa.edge_len; i++) free((void *)edges[i].ranges);
    free(edges); free(accepts); rsdfa_v1_program_free(&program); rsdfa_v1_plan_free(&plan);
    return result;
}
