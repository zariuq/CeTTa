/* Build-time emission through the same interval DFA compiler as ParserPack.
 *
 *   emit-regular-span-dfa-v1 NFA OUT [--prefix NAME] [--alphabet scalars|bytes]
 *                            [--tag-names NAME0,NAME1,...]
 *
 * Input is a numeric NFA (states, start, edges, accepts, tags), followed by
 * edges (from,to,kind,range count,low/high pairs) and accepts (state,tag).
 * The output is a C header of immutable tables and step functions, every
 * name beginning with NAME (default `rsdfa`), so several scanners can share
 * a translation unit:
 *
 *   NAME_step(state, unit)       the next state, or UINT32_MAX
 *   NAME_START                   the start state
 *   NAME_accepts(state, tag)     whether the state accepts the tag
 *   NAME_eof_accepts(state, tag) whether it accepts the tag at the end of input
 *   NAME_winner[state]           the least tag the state accepts, UINT32_MAX
 *                                for none: the rule a longest match selects
 *                                when tags are numbered in rule order
 *                                (ocamllex, Flex)
 *   NAME_eof_winner[state]       the same at the end of input, where the
 *                                state's end-of-input accepts count too
 *   NAME_TAG_COUNT               the number of tags
 *   NAME_tag_T                   tag T's number, for each name given with
 *                                --tag-names (one per tag, in tag order;
 *                                characters outside [A-Za-z0-9_] become _)
 *
 * A scanner over scalars has a direct table for ASCII and binary search
 * above it; one over bytes (--alphabet bytes, every edge within 0..255) has
 * a direct table for every byte.  Any number of tags. */
#include "regular_span_dfa_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_prefix(const char *p) {
    if (!p || !*p || !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_'))
        return false;
    for (; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_'))
            return false;
    return true;
}

static void upper(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    for (; src[i] && i + 1u < cap; ++i)
        dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 'a' + 'A') : src[i];
    dst[i] = '\0';
}

/* The --tag-names list, split; each name made an identifier's tail. */
static char **split_names(const char *list, uint32_t *count) {
    uint32_t n = 1;
    for (const char *c = list; *c; ++c)
        n += *c == ',';
    char **names = calloc(n, sizeof(*names));
    if (!names)
        return NULL;
    const char *begin = list;
    for (uint32_t i = 0; i < n; i++) {
        const char *end = strchr(begin, ',');
        size_t len = end ? (size_t)(end - begin) : strlen(begin);
        names[i] = malloc(len + 1u);
        if (!names[i] || !len) {
            for (uint32_t j = 0; j <= i; j++)
                free(names[j]);
            free(names);
            return NULL;
        }
        for (size_t k = 0; k < len; k++) {
            char ch = begin[k];
            bool ident = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
            names[i][k] = ident ? ch : '_';
        }
        names[i][len] = '\0';
        begin = end ? end + 1 : begin + len;
    }
    *count = n;
    return names;
}

static uint32_t target_of(const RSDFAV1Program *program, uint32_t state, uint32_t unit) {
    const RSDFAV1ProgramState *s = &program->states[state];
    for (uint32_t j = 0; j < s->transition_len; j++) {
        RSDFAV1ProgramTransition t = program->transitions[s->transition_begin + j];
        if (t.low <= unit && unit <= t.high)
            return t.target;
    }
    return UINT32_MAX;
}

static void emit_lists(FILE *out, const char *p, const char *what, const RSDFAV1Program *program,
                       const uint32_t *tags, bool eof) {
    fprintf(out, "static const unsigned %s_%s_offsets[][2] = {\n", p, what);
    for (uint32_t i = 0; i < program->state_len; i++)
        fprintf(out, "{%u,%u},\n", eof ? program->states[i].eof_accept_begin : program->states[i].accept_begin,
                eof ? program->states[i].eof_accept_len : program->states[i].accept_len);
    uint32_t len = eof ? program->eof_accept_tag_len : program->accept_tag_len;
    fprintf(out, "};\nstatic const unsigned %s_%s_tags[] = {", p, what);
    for (uint32_t i = 0; i < len; i++)
        fprintf(out, "%s%u", i ? (i % 16u ? "," : ",\n") : "\n", tags[i]);
    fprintf(out, "%s0};\n", len ? ",\n" : "");
}

int main(int argc, char **argv) {
    const char *prefix = "rsdfa", *tag_list = NULL;
    bool bytes = false;
    if (argc < 3)
        return 2;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--prefix") == 0 && i + 1 < argc)
            prefix = argv[++i];
        else if (strcmp(argv[i], "--tag-names") == 0 && i + 1 < argc)
            tag_list = argv[++i];
        else if (strcmp(argv[i], "--alphabet") == 0 && i + 1 < argc) {
            const char *a = argv[++i];
            if (strcmp(a, "bytes") == 0)
                bytes = true;
            else if (strcmp(a, "scalars") != 0)
                return 2;
        } else {
            return 2;
        }
    }
    if (!valid_prefix(prefix) || strlen(prefix) > 64u)
        return 2;
    char up[80];
    upper(up, prefix, sizeof up);
    FILE *input = fopen(argv[1], "r"), *out = NULL;
    RSDFAV1Nfa nfa = {0};
    RSDFAV1Plan plan; rsdfa_v1_plan_init(&plan);
    RSDFAV1Program program; rsdfa_v1_program_init(&program);
    RSDFAV1BuildOutcome outcome;
    uint32_t start;
    RSDFAV1NfaEdge *edges = NULL;
    RSDFAV1NfaAccept *accepts = NULL;
    char **names = NULL;
    uint32_t name_len = 0;
    int result = 1;
    char error[512] = {0};
    if (!input || fscanf(input, "%u %u %u %u %u", &nfa.state_len, &start,
                         &nfa.edge_len, &nfa.accept_len, &nfa.tag_len) != 5) goto done;
    if (!nfa.state_len || !nfa.tag_len || nfa.edge_len > 1000000) goto done;
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
        for (uint32_t j = 0; j < edges[i].range_len; j++) {
            if (fscanf(input, "%u %u", &ranges[j].low, &ranges[j].high) != 2) goto done;
            if (bytes && ranges[j].high > 255u) {
                snprintf(error, sizeof error, "a byte scanner's edge reaches %u", ranges[j].high);
                goto done;
            }
        }
    }
    for (uint32_t i = 0; i < nfa.accept_len; i++)
        if (fscanf(input, "%u %u", &accepts[i].state, &accepts[i].tag) != 2 || accepts[i].tag >= nfa.tag_len)
            goto done;
    if (tag_list) {
        names = split_names(tag_list, &name_len);
        if (!names || name_len != nfa.tag_len) {
            snprintf(error, sizeof error, "--tag-names gives %u nonempty names for %u tags",
                     names ? name_len : 0u, nfa.tag_len);
            goto done;
        }
        for (uint32_t i = 0; i < name_len; i++)
            for (uint32_t j = 0; j < i; j++)
                if (strcmp(names[i], names[j]) == 0) {
                    snprintf(error, sizeof error, "tags %u and %u are both named %s", j, i, names[i]);
                    goto done;
                }
    }
    if (!rsdfa_v1_plan_build(&nfa, 65536, 1048576, &plan, &outcome, error, sizeof(error)) ||
        outcome != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_plan_export_program(&plan, &program, error, sizeof(error)) ||
        !rsdfa_v1_program_validate(&program, error, sizeof(error))) goto done;
    out = fopen(argv[2], "w"); if (!out) goto done;
    const char *p = prefix;
    fprintf(out, "/* Generated interval DFA (emit-regular-span-dfa-v1); do not edit. */\n");
    fprintf(out, "#include <stdbool.h>\n#include <stdint.h>\n");
    fprintf(out, "#define %s_START %uu\n#define %s_TAG_COUNT %uu\n", up, program.start_state, up, nfa.tag_len);
    if (names) {
        fprintf(out, "enum {");
        for (uint32_t i = 0; i < name_len; i++)
            fprintf(out, "%s\n    %s_tag_%s = %u", i ? "," : "", p, names[i], i);
        fprintf(out, "\n};\n");
    }
    fprintf(out, "typedef struct { unsigned low, high, target; } %s_edge;\n", p);
    fprintf(out, "static const %s_edge %s_edges[] = {\n", p, p);
    for (uint32_t i = 0; i < program.transition_len; i++) {
        RSDFAV1ProgramTransition t = program.transitions[i];
        fprintf(out, "{%u,%u,%u},\n", t.low, t.high, t.target);
    }
    fprintf(out, "%s};\nstatic const unsigned %s_offsets[][2] = {\n", program.transition_len ? "" : "{0,0,0}", p);
    for (uint32_t i = 0; i < program.state_len; i++)
        fprintf(out, "{%u,%u},\n", program.states[i].transition_begin, program.states[i].transition_len);
    fprintf(out, "};\n");
    unsigned direct = bytes ? 256u : 128u;
    fprintf(out, "static const unsigned %s_direct[][%u] = {\n", p, direct);
    for (uint32_t i = 0; i < program.state_len; i++) {
        fprintf(out, "{");
        for (uint32_t unit = 0; unit < direct; unit++)
            fprintf(out, "%u%s", target_of(&program, i, unit), unit + 1u == direct ? "},\n" : ",");
    }
    fprintf(out, "};\n");
    if (bytes)
        fprintf(out, "static inline unsigned %s_step(unsigned state, unsigned unit) {\n"
                     "    return unit < 256u ? %s_direct[state][unit] : UINT32_MAX;\n}\n", p, p);
    else
        fprintf(out, "static inline unsigned %s_step(unsigned state, unsigned unit) {\n"
                     "    if (unit < 128u) return %s_direct[state][unit];\n"
                     "    unsigned begin = %s_offsets[state][0], end = begin + %s_offsets[state][1];\n"
                     "    while (begin < end) {\n"
                     "        unsigned mid = begin + (end - begin) / 2u;\n"
                     "        %s_edge t = %s_edges[mid];\n"
                     "        if (unit < t.low) end = mid; else if (unit > t.high) begin = mid + 1u; else return t.target;\n"
                     "    }\n    return UINT32_MAX;\n}\n", p, p, p, p, p, p);
    emit_lists(out, p, "accept", &program, program.accept_tags, false);
    emit_lists(out, p, "eof_accept", &program, program.eof_accept_tags, true);
    for (int at_eof = 0; at_eof < 2; at_eof++) {
        fprintf(out, "static const unsigned %s_%swinner[] = {", p, at_eof ? "eof_" : "");
        for (uint32_t i = 0; i < program.state_len; i++) {
            const RSDFAV1ProgramState *st = &program.states[i];
            uint32_t best = UINT32_MAX;
            for (uint32_t j = 0; j < st->accept_len; j++)
                if (program.accept_tags[st->accept_begin + j] < best)
                    best = program.accept_tags[st->accept_begin + j];
            for (uint32_t j = 0; at_eof && j < st->eof_accept_len; j++)
                if (program.eof_accept_tags[st->eof_accept_begin + j] < best)
                    best = program.eof_accept_tags[st->eof_accept_begin + j];
            fprintf(out, "%s%u", i ? (i % 16u ? "," : ",\n") : "\n", best);
        }
        fprintf(out, "};\n");
    }
    fprintf(out, "static inline bool %s_accepts(unsigned state, unsigned tag) {\n"
                 "    for (unsigned k = 0; k < %s_accept_offsets[state][1]; k++)\n"
                 "        if (%s_accept_tags[%s_accept_offsets[state][0] + k] == tag) return true;\n"
                 "    return false;\n}\n", p, p, p, p);
    fprintf(out, "static inline bool %s_eof_accepts(unsigned state, unsigned tag) {\n"
                 "    for (unsigned k = 0; k < %s_eof_accept_offsets[state][1]; k++)\n"
                 "        if (%s_eof_accept_tags[%s_eof_accept_offsets[state][0] + k] == tag) return true;\n"
                 "    return false;\n}\n", p, p, p, p);
    if (fclose(out)) { out = NULL; goto done; } out = NULL;
    result = 0;
done:
    if (result) fprintf(stderr, "DFA emission failed: %s\n", error);
    if (input) fclose(input);
    if (out) fclose(out);
    for (uint32_t i = 0; edges && i < nfa.edge_len; i++) free((void *)edges[i].ranges);
    for (uint32_t i = 0; names && i < name_len; i++) free(names[i]);
    free(names);
    free(edges); free(accepts); rsdfa_v1_program_free(&program); rsdfa_v1_plan_free(&plan);
    return result;
}
