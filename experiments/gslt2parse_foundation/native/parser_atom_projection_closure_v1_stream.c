#define _POSIX_C_SOURCE 200809L

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_atom_projection_closure_v1.h"
#include "parser_pack_abi_stream_v1.h"

#include "symbol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t state_limit;
    uint32_t transition_limit;
    uint32_t pair_limit;
} PPAtomClosureStreamV1Options;

static bool digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool read_file(const char *path,
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
        (void)snprintf(error, error_size,
                       "cannot open Atom-projection artifact");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size,
                               "Atom-projection artifact is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "failed to allocate projection artifact");
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
                           "failed while reading projection artifact");
            goto done;
        }
        break;
    }
    if (len > 0u && bytes[len - 1u] == (uint8_t)'\n')
        len--;
    if (len == 0u || memchr(bytes, '\0', len) != NULL ||
        memchr(bytes, '\n', len) != NULL ||
        memchr(bytes, '\r', len) != NULL) {
        (void)snprintf(error, error_size,
                       "projection artifact is not one LF-framed term");
        goto done;
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

static bool read_projection(const char *path,
                            Arena *arena,
                            Atom **out,
                            char *error,
                            size_t error_size) {
    uint8_t *bytes = NULL;
    uint8_t *canonical = NULL;
    size_t len = 0u;
    size_t canonical_len = 0u;
    bool ok = false;

    if (!read_file(path, &bytes, &len, error, error_size) ||
        !fh_ground_term_v1_parse(
            arena, bytes, len, out, error, error_size) ||
        !fh_ground_term_v1_render(
            *out, &canonical, &canonical_len, error, error_size)) {
        goto done;
    }
    if (canonical_len != len || memcmp(canonical, bytes, len) != 0) {
        (void)snprintf(error, error_size,
                       "Atom-projection artifact is not canonical");
        goto done;
    }
    ok = true;

done:
    free(canonical);
    free(bytes);
    return ok;
}

static const char *outcome_name(PPAtomProjectionClosureV1Outcome outcome) {
    switch (outcome) {
    case PPATOM_CLOSURE_V1_COMPLETED:
        return "completed";
    case PPATOM_CLOSURE_V1_SOURCE_STATE_LIMIT:
        return "source-state-limit";
    case PPATOM_CLOSURE_V1_SOURCE_TRANSITION_LIMIT:
        return "source-transition-limit";
    case PPATOM_CLOSURE_V1_DOMAIN_STATE_LIMIT:
        return "domain-state-limit";
    case PPATOM_CLOSURE_V1_DOMAIN_TRANSITION_LIMIT:
        return "domain-transition-limit";
    case PPATOM_CLOSURE_V1_INCLUSION_PAIR_LIMIT:
        return "inclusion-pair-limit";
    }
    return "unknown";
}

static void sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void sha_text(CettaNativeSha256 *sha, const char *text) {
    uint32_t len = (uint32_t)strlen(text);
    sha_u32(sha, len);
    cetta_native_sha256_update(sha, (const uint8_t *)text, len);
}

static void certificate_digest(
    const PPABIV1Pack *pack,
    const FHAnswerStreamV1 *answers,
    const char *semantic_compiler_digest,
    const PPAtomProjectionV1ProvenanceView *projection,
    const PPAtomProjectionClosureV1Result *result,
    char out[65]) {
    static const uint8_t domain[] = "ParserAtomProjectionClosureV1\n";
    CettaNativeSha256 sha;
    uint32_t row_index;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    sha_text(&sha, pack->pack_digest);
    sha_text(&sha, semantic_compiler_digest);
    sha_text(&sha, answers->digest);
    sha_text(&sha, projection->profile);
    sha_text(&sha, projection->source_digest);
    sha_text(&sha, projection->reader_digest);
    sha_text(&sha, projection->compiler_digest);
    sha_text(&sha, projection->plan_digest);
    sha_u32(&sha, (uint32_t)result->outcome);
    sha_u32(&sha, result->all_included ? 1u : 0u);
    sha_u32(&sha, result->wrapper_len);
    sha_u32(&sha, result->source_root_tag_len);
    sha_u32(&sha, result->source_state_len);
    sha_u32(&sha, result->source_transition_len);
    sha_u32(&sha, result->stopped_wrapper);
    sha_u32(&sha, result->row_len);
    for (row_index = 0u; row_index < result->row_len; row_index++) {
        const PPAtomProjectionClosureV1Row *row = &result->rows[row_index];
        uint32_t scalar_index;
        sha_text(&sha, row->label);
        sha_u32(&sha, row->source_tag);
        sha_u32(&sha, row->included ? 1u : 0u);
        sha_u32(&sha, row->explored_pair_len);
        sha_u32(&sha, row->counterexample_len);
        for (scalar_index = 0u;
             scalar_index < row->counterexample_len;
             scalar_index++) {
            sha_u32(&sha, row->counterexample[scalar_index]);
        }
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

static bool write_witness(const PPAtomProjectionClosureV1Row *row) {
    uint32_t index;

    if (row->counterexample_len == 0u)
        return fputc('-', stdout) != EOF;
    for (index = 0u; index < row->counterexample_len; index++) {
        if (index > 0u && fputc(',', stdout) == EOF)
            return false;
        if (printf("%x", row->counterexample[index]) < 0)
            return false;
    }
    return true;
}

static bool run(const char *abi_path,
                const char *nfa_path,
                const char *projection_path,
                const char *semantic_compiler_digest,
                const PPAtomClosureStreamV1Options *options) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 answers;
    Arena projection_arena;
    Atom *projection_term = NULL;
    PPAtomProjectionV1Plan projection;
    PPAtomProjectionV1ProvenanceView provenance;
    PPAtomProjectionClosureV1Result result;
    char certificate[65] = {0};
    char error[512] = {0};
    uint32_t row_index;
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&answers);
    arena_init(&projection_arena);
    ppatom_projection_v1_plan_init(&projection);
    ppatom_projection_closure_v1_result_init(&result);
    if (!digest_valid(semantic_compiler_digest) ||
        !ppabi_v1_wire_read(&wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &answers, nfa_path, error, sizeof(error)) ||
        answers.len == 0u ||
        !read_projection(
            projection_path, &projection_arena, &projection_term,
            error, sizeof(error)) ||
        !ppatom_projection_v1_plan_load(
            &projection, projection_term, error, sizeof(error)) ||
        !ppatom_projection_v1_plan_provenance(
            &projection, &provenance, error, sizeof(error)) ||
        !ppatom_projection_closure_v1_check(
            &pack, answers.terms, answers.len, &projection,
            options->state_limit, options->transition_limit,
            options->pair_limit, &result, error, sizeof(error))) {
        (void)fprintf(stderr, "Atom-projection closure rejected: %s\n",
                      error[0] ? error : "unknown rejection");
        goto done;
    }
    certificate_digest(
        &pack, &answers, semantic_compiler_digest,
        &provenance, &result, certificate);
    (void)printf("parser-atom-projection-closure-v1\n");
    (void)printf("source-digest\t%s\n", pack.source_digest);
    (void)printf("pack-digest\t%s\n", pack.pack_digest);
    (void)printf("semantic-compiler-digest\t%s\n",
                 semantic_compiler_digest);
    (void)printf("semantic-nfa-answer-digest\t%s\n", answers.digest);
    (void)printf("semantic-nfa-answers\t%zu\n", answers.len);
    (void)printf("projection-profile\t%s\n", provenance.profile);
    (void)printf("projection-source-digest\t%s\n",
                 provenance.source_digest);
    (void)printf("projection-reader-digest\t%s\n",
                 provenance.reader_digest);
    (void)printf("projection-compiler-digest\t%s\n",
                 provenance.compiler_digest);
    (void)printf("projection-plan-digest\t%s\n",
                 provenance.plan_digest);
    (void)printf("outcome\t%s\n", outcome_name(result.outcome));
    (void)printf("bijection\t1\n");
    (void)printf("wrapper-count\t%u\n", result.wrapper_len);
    (void)printf("root-tag-count\t%u\n", result.source_root_tag_len);
    (void)printf("source-states\t%u\n", result.source_state_len);
    (void)printf("source-transitions\t%u\n",
                 result.source_transition_len);
    (void)printf("completed-wrappers\t%u\n", result.row_len);
    (void)printf("stopped-wrapper\t");
    if (result.stopped_wrapper == UINT32_MAX)
        (void)printf("-\n");
    else
        (void)printf("%u\n", result.stopped_wrapper);
    for (row_index = 0u; row_index < result.row_len; row_index++) {
        const PPAtomProjectionClosureV1Row *row = &result.rows[row_index];
        if (printf("wrapper\t%s\t%u\t%u\t%u\t",
                   row->label, row->source_tag,
                   row->included ? 1u : 0u,
                   row->explored_pair_len) < 0 ||
            !write_witness(row) || fputc('\n', stdout) == EOF) {
            (void)fprintf(stderr,
                          "failed to write projection-closure witness\n");
            goto done;
        }
    }
    (void)printf("all-included\t%u\n", result.all_included ? 1u : 0u);
    (void)printf("certified\t%u\n",
                 result.outcome == PPATOM_CLOSURE_V1_COMPLETED &&
                         result.all_included
                     ? 1u
                     : 0u);
    (void)printf("certificate-digest\t%s\n", certificate);
    (void)printf("end\n");
    ok = true;

done:
    ppatom_projection_closure_v1_result_free(&result);
    ppatom_projection_v1_plan_free(&projection);
    arena_free(&projection_arena);
    fh_answer_stream_v1_free(&answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

static bool parse_u32(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;
    const char *cursor;

    if (!text || text[0] == '\0')
        return false;
    for (cursor = text; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9')
            return false;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0u ||
        value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_options(int argc,
                          char **argv,
                          PPAtomClosureStreamV1Options *options) {
    int index;

    *options = (PPAtomClosureStreamV1Options){
        .state_limit = UINT32_C(65536),
        .transition_limit = UINT32_C(2000000),
        .pair_limit = UINT32_C(2000000),
    };
    for (index = 5; index < argc; index++) {
        if (strcmp(argv[index], "--state-limit") == 0 &&
            index + 1 < argc) {
            if (!parse_u32(argv[++index], &options->state_limit))
                return false;
        } else if (strcmp(argv[index], "--transition-limit") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], &options->transition_limit))
                return false;
        } else if (strcmp(argv[index], "--pair-limit") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], &options->pair_limit))
                return false;
        } else {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    PPAtomClosureStreamV1Options options;
    bool ok;

    if (argc < 5 || !parse_options(argc, argv, &options)) {
        (void)fprintf(
            stderr,
            "usage: %s ABI SEMANTIC_NFA PROJECTION SEMANTIC_COMPILER_SHA256 "
            "[--state-limit N] [--transition-limit N] [--pair-limit N]\n",
            argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], argv[4], &options);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return ok ? 0 : 1;
}
