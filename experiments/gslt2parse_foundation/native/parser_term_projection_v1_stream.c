#include "parser_term_projection_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "symbol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    VarId id;
    uint32_t ordinal;
} PPTPStreamVar;

typedef struct {
    PPTPStreamVar *data;
    uint32_t len;
    uint32_t cap;
    CettaNativeSha256 alpha;
    CettaNativeSha256 spelling;
    uint32_t depth_limit;
} PPTPStreamDigest;

static bool parse_limit(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' ||
        value == 0u || value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static bool read_input(
    const char *path,
    uint8_t **out,
    size_t *out_len,
    char *error,
    size_t error_cap) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input) {
        (void)snprintf(error, error_cap, "cannot open projection input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_cap,
                               "projection input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_cap,
                               "cannot allocate projection input");
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
            (void)snprintf(error, error_cap,
                           "failed while reading projection input");
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

static void digest_u64(CettaNativeSha256 *sha, uint64_t value) {
    uint8_t bytes[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        bytes[7u - index] = (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void digest_bytes(
    CettaNativeSha256 *sha,
    const uint8_t *bytes,
    size_t len) {
    digest_u64(sha, (uint64_t)len);
    if (len)
        cetta_native_sha256_update(sha, bytes, len);
}

static bool variable_ordinal(
    PPTPStreamDigest *digest,
    VarId id,
    uint32_t *out) {
    PPTPStreamVar *next;
    uint32_t index;
    uint32_t cap;

    for (index = 0u; index < digest->len; index++) {
        if (digest->data[index].id == id) {
            *out = digest->data[index].ordinal;
            return true;
        }
    }
    if (digest->len == digest->cap) {
        cap = digest->cap ? digest->cap * 2u : 16u;
        if (cap < digest->cap)
            return false;
        next = realloc(digest->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        digest->data = next;
        digest->cap = cap;
    }
    digest->data[digest->len].id = id;
    digest->data[digest->len].ordinal = digest->len;
    *out = digest->len;
    digest->len++;
    return true;
}

static bool digest_atom(
    PPTPStreamDigest *digest,
    Atom *atom,
    uint32_t depth) {
    static const uint8_t symbol_tag = 'S';
    static const uint8_t variable_tag = 'V';
    static const uint8_t expression_tag = 'E';
    const char *name;
    uint32_t ordinal;
    CettaExprIndex index;

    if (!atom || depth > digest->depth_limit)
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        name = atom_name_cstr(atom);
        cetta_native_sha256_update(&digest->alpha, &symbol_tag, 1u);
        cetta_native_sha256_update(&digest->spelling, &symbol_tag, 1u);
        digest_bytes(&digest->alpha, (const uint8_t *)name, strlen(name));
        digest_bytes(&digest->spelling, (const uint8_t *)name, strlen(name));
        return true;
    case ATOM_VAR:
        if (!variable_ordinal(digest, atom->var_id, &ordinal))
            return false;
        name = atom_name_cstr(atom);
        cetta_native_sha256_update(&digest->alpha, &variable_tag, 1u);
        cetta_native_sha256_update(&digest->spelling, &variable_tag, 1u);
        digest_u64(&digest->alpha, ordinal);
        digest_u64(&digest->spelling, ordinal);
        digest_bytes(
            &digest->spelling, (const uint8_t *)name, strlen(name));
        return true;
    case ATOM_EXPR:
        cetta_native_sha256_update(&digest->alpha, &expression_tag, 1u);
        cetta_native_sha256_update(&digest->spelling, &expression_tag, 1u);
        digest_u64(&digest->alpha, atom->expr.len);
        digest_u64(&digest->spelling, atom->expr.len);
        for (index = 0u; index < atom->expr.len; index++) {
            if (!digest_atom(
                    digest, atom->expr.elems[index], depth + 1u))
                return false;
        }
        return true;
    case ATOM_GROUNDED:
        return false;
    }
    return false;
}

static bool projection_digests(
    Atom *atom,
    uint32_t depth_limit,
    char alpha_hex[65],
    char spelling_hex[65]) {
    PPTPStreamDigest digest;

    memset(&digest, 0, sizeof(digest));
    digest.depth_limit = depth_limit;
    cetta_native_sha256_init(&digest.alpha);
    cetta_native_sha256_init(&digest.spelling);
    if (!digest_atom(&digest, atom, 1u)) {
        free(digest.data);
        return false;
    }
    cetta_native_sha256_finish_hex(&digest.alpha, alpha_hex);
    cetta_native_sha256_finish_hex(&digest.spelling, spelling_hex);
    free(digest.data);
    return true;
}

static bool run(
    const char *plan_path,
    const char *result_path,
    uint32_t depth_limit) {
    uint8_t *plan_bytes = NULL;
    uint8_t *result_bytes = NULL;
    size_t plan_len = 0u;
    size_t result_len = 0u;
    Arena input_arena;
    Arena output_arena;
    Arena print_arena;
    PPTermProjectionV1Plan plan;
    Atom *compiled_plan = NULL;
    Atom *semantic_result = NULL;
    Atom *projected = NULL;
    char *rendered = NULL;
    char alpha_digest[65] = {0};
    char spelling_digest[65] = {0};
    char error[512] = {0};
    bool ok = false;

    arena_init(&input_arena);
    arena_init(&output_arena);
    arena_init(&print_arena);
    ppterm_projection_v1_plan_init(&plan);
    if (!read_input(plan_path, &plan_bytes, &plan_len,
                    error, sizeof(error)) ||
        !read_input(result_path, &result_bytes, &result_len,
                    error, sizeof(error)) ||
        !fh_ground_term_v1_parse(
            &input_arena, plan_bytes, plan_len, &compiled_plan,
            error, sizeof(error)) ||
        !fh_ground_term_v1_parse(
            &input_arena, result_bytes, result_len, &semantic_result,
            error, sizeof(error)) ||
        !ppterm_projection_v1_plan_load(
            &plan, compiled_plan, error, sizeof(error)) ||
        !ppterm_projection_v1_project_result(
            &plan, semantic_result, &output_arena, depth_limit,
            &projected, error, sizeof(error)) ||
        !(rendered = atom_to_string(&print_arena, projected)) ||
        !projection_digests(
            projected, depth_limit, alpha_digest, spelling_digest)) {
        fprintf(stderr, "term projection rejected: %s\n",
                error[0] ? error : "unrenderable projected Atom");
        goto done;
    }
    printf("parser-term-projection-v1\n");
    printf("projected\t%s\n", rendered);
    printf("alpha-digest\t%s\n", alpha_digest);
    printf("spelling-digest\t%s\n", spelling_digest);
    printf("end\n");
    ok = true;

done:
    ppterm_projection_v1_plan_free(&plan);
    arena_free(&print_arena);
    arena_free(&output_arena);
    arena_free(&input_arena);
    free(result_bytes);
    free(plan_bytes);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    uint32_t depth_limit = 4096u;
    bool ok;

    if ((argc != 3 && argc != 4) ||
        (argc == 4 && !parse_limit(argv[3], &depth_limit))) {
        fprintf(stderr,
                "usage: %s PLAN RESULT [DEPTH_LIMIT]\n", argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], depth_limit);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return ok ? 0 : 1;
}
