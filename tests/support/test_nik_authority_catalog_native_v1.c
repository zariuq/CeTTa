/* Compare complete registry values, then use the ordinary production checker.
 * The differential replay service is deliberately absent from this link. */
#include "nik_runtime_internal.h"
#include "parser.h"
#include "symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NIK_CATALOG_REFERENCE_C
#error NIK_CATALOG_REFERENCE_C must name the retained registry source
#endif
#ifndef NIK_CATALOG_GENERATED_C
#error NIK_CATALOG_GENERATED_C must name the newly projected registry source
#endif
#define cetta_prime_nik_authorities_v1 retained_authorities
#define cetta_prime_nik_authorities_v1_count retained_count
#define cetta_prime_nik_authorities_v1_catalog_sha256 retained_digest
#include NIK_CATALOG_REFERENCE_C
#undef cetta_prime_nik_authorities_v1
#undef cetta_prime_nik_authorities_v1_count
#undef cetta_prime_nik_authorities_v1_catalog_sha256
#define CETTA_GENERATED_cetta_prime_nik_authorities_v1_H
#include NIK_CATALOG_GENERATED_C

static unsigned checks, failures;
#define CHECK(value, label) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL: %s\n", label); ++failures; } } while (0)

static Atom *parse(Arena *arena, const char *source) {
    size_t position = 0;
    Atom *term = parse_sexpr(arena, source, &position);
    return term && parser_rest_is_delimiters(source, &position) ? term : NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    bool original = !strcmp(argv[1], "original"), identity = !strcmp(argv[1], "identity");
    bool empty = !strcmp(argv[1], "empty"), changed = !strcmp(argv[1], "changed-proof");
    bool toy = !strcmp(argv[1], "toy");
    bool scoped = !strcmp(argv[1], "scoped");
    if (!original && !identity && !empty && !changed && !toy && !scoped) return 2;
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    g_hashcons = NULL;
    Arena arena; arena_init(&arena);
    CHECK(cetta_nik_authority_catalog_valid_v1(cetta_prime_nik_authorities_v1,
        cetta_prime_nik_authorities_v1_count, cetta_prime_nik_authorities_v1_catalog_sha256),
        "native catalog validator accepts exact descriptor identity");
    if (original || identity) {
        CHECK(cetta_prime_nik_authorities_v1_count == retained_count, "ordered registry count preserved");
        CHECK((strcmp(cetta_prime_nik_authorities_v1_catalog_sha256, retained_digest) == 0) == original,
            "identity mutation changes catalog digest, original preserves it");
        for (size_t i = 0; i < retained_count && i < cetta_prime_nik_authorities_v1_count; ++i) {
            const CettaNikAuthorityV1 *a = &cetta_prime_nik_authorities_v1[i], *b = &retained_authorities[i];
            const char *av[] = {a->alias,a->system_id,a->revision,a->digest,a->presentation_metta,a->positive_goal_metta,a->positive_proof_metta};
            const char *bv[] = {b->alias,b->system_id,b->revision,b->digest,b->presentation_metta,b->positive_goal_metta,b->positive_proof_metta};
            for (size_t j = 0; j < 7; ++j)
                CHECK((strcmp(av[j], bv[j]) == 0) == !(identity && i == 0 && j == 1),
                    "all seven ordered ABI fields preserved except selected source identity mutation");
        }
    } else {
        CHECK(cetta_prime_nik_authorities_v1_count == 1, "native registry permits one admitted authority");
        CHECK(!strcmp(cetta_prime_nik_authorities_v1[0].system_id, "test.λ\\\"\t\r"),
            "UTF-8, quote, backslash, tab and control byte survive metadata projection");
        if (empty) CHECK(!strcmp(cetta_prime_nik_authorities_v1[0].positive_proof_metta, "\"\""),
            "zero-length quoted specimen remains quoted empty data");
    }
    for (size_t i = 0; i < cetta_prime_nik_authorities_v1_count; ++i) {
        const CettaNikAuthorityV1 *a = &cetta_prime_nik_authorities_v1[i];
        CHECK(cetta_nik_authority_descriptor_valid_v1(a), "presentation digest rechecked by production validator");
        Atom *goal = parse(&arena, a->positive_goal_metta), *proof = parse(&arena, a->positive_proof_metta);
        CHECK(goal && proof, "projected specimens decode in production reader");
        CettaNikReceiptV1 receipt; char error[512];
        CettaNikOutcome result = cetta_nik_check_v1(a->alias, goal, proof, (CettaNikLimits){0},
            &arena, &receipt, error, sizeof(error));
        CHECK(result == (empty ? CETTA_NIK_MALFORMED : changed ? CETTA_NIK_REJECTED : CETTA_NIK_ACCEPTED),
            "actual projected proof controls ordinary native checker result");
        CHECK(receipt.native_ran && !receipt.reference_ran && !receipt.compiled_ran,
            "production check never invokes replay qualification");
        CHECK(receipt.total_work == receipt.native_nodes, "ordinary work accounting unchanged");
    }
    arena_free(&arena); var_intern_free(&variables); symbol_table_free(&symbols);
    g_symbols = NULL; g_var_intern = NULL;
    printf("Native NIK catalog: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
