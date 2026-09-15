#define _XOPEN_SOURCE 700
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the actual private transport without adding a public runtime
 * interface. Allocation injection affects this translation unit only. */
static size_t allocation_count;
static size_t allocation_failure = SIZE_MAX;
static void *transport_malloc(size_t n) {
    return allocation_count++ == allocation_failure ? NULL : malloc(n);
}
static void *transport_calloc(size_t n, size_t size) {
    return allocation_count++ == allocation_failure ? NULL : calloc(n, size);
}
static void *transport_realloc(void *p, size_t n) {
    return allocation_count++ == allocation_failure ? NULL : realloc(p, n);
}
#define malloc transport_malloc
#define calloc transport_calloc
#define realloc transport_realloc
#include "../../native/langdef_module.c"
#undef malloc
#undef calloc
#undef realloc

static unsigned passed, failed;
static void check(bool condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}

static CettaOpLangV1SExpr *decode(Atom *value, uint32_t work,
                                 char digest[65], char error[256]) {
    AuthoredParserStructuredValueContext context = {
        .remaining_work = work, .error = error, .error_size = 256u,
    };
    error[0] = '\0';
    return authored_parser_structured_value_decode(value, &context, digest);
}

static Atom *spine(Arena *arena, unsigned n, Atom *tail) {
    Atom *head = atom_symbol(arena, "LCons");
    for (unsigned i = 0; i < n; ++i)
        tail = atom_expr3(arena, head, atom_int(arena, i % 7), tail);
    return tail;
}

int __wrap_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena;
    arena_init(&arena);
    char digest[65], expected[65], error[256];
    Atom *leaf = atom_expr3(&arena, atom_symbol(&arena, "Leaf"),
                            atom_string(&arena, "λ"), atom_int(&arena, 17));
    Atom *sample = atom_expr3(&arena, atom_symbol(&arena, "Pair"), leaf,
                              atom_symbol(&arena, "tag"));
    /* Independent literal encoding: Pair(Leaf(string lambda, natural 17),
     * symbol tag). Deliberately do not call the production header encoder. */
    static const uint8_t bytes[] =
        "cetta-structured-language-value-v1"
        "\x03\x00\x00\x00\x04" "Pair" "\x00\x00\x00\x02"
        "\x03\x00\x00\x00\x04" "Leaf" "\x00\x00\x00\x02"
        "\x01\x00\x00\x00\x02\xce\xbb"
        "\x02\x00\x00\x00\x02" "17"
        "\x00\x00\x00\x00\x03" "tag";
    cetta_native_sha256_hex(bytes, sizeof(bytes) - 1u, expected);
    CettaOpLangV1SExpr *root = decode(sample, 5u, digest, error);
    check(root && !strcmp(digest, expected), "independent digest byte encoding");
    check(root && cetta_op_lang_v1_application_is(root, "Pair", 2u) &&
          cetta_op_lang_v1_application_is(root->as.application.arguments[0], "Leaf", 2u) &&
          cetta_op_lang_v1_symbol_is(root->as.application.arguments[1], "tag"),
          "ordered mixed-kind tree decoding");
    authored_parser_structured_root_free(root);
    Atom *swapped = atom_expr3(&arena, atom_symbol(&arena, "Pair"),
                               atom_symbol(&arena, "tag"), leaf);
    root = decode(swapped, 5u, digest, error);
    check(root && strcmp(digest, expected), "child order changes digest");
    authored_parser_structured_root_free(root);
    Atom *shared = atom_expr3(&arena, atom_symbol(&arena, "Pair"), leaf, leaf);
    root = decode(shared, 7u, digest, error);
    check(root && root->as.application.arguments[0] != root->as.application.arguments[1] &&
          cetta_op_lang_v1_sexpr_equal(root->as.application.arguments[0],
                                      root->as.application.arguments[1]),
          "shared atom input creates two independently owned occurrences");
    authored_parser_structured_root_free(root);

    /* Same characters in different constructors must not alias at the wire
     * boundary. Empty applications are also distinct from bare symbols. */
    Atom *scalar_forms[] = {atom_symbol(&arena, "17"), atom_string(&arena, "17"),
                           atom_int(&arena, 17)};
    char scalar_digests[3][65];
    for (size_t i = 0u; i < 3u; ++i) {
        root = decode(scalar_forms[i], 1u, scalar_digests[i], error);
        check(root && root->kind == (CettaOpLangV1SExprKind)i,
              "symbol, string and natural constructors stay distinct");
        authored_parser_structured_root_free(root);
    }
    check(strcmp(scalar_digests[0], scalar_digests[1]) &&
          strcmp(scalar_digests[0], scalar_digests[2]) &&
          strcmp(scalar_digests[1], scalar_digests[2]), "scalar kind tags enter the digest");
    Atom *tag = atom_symbol(&arena, "Tag");
    root = decode(tag, 1u, expected, error);
    authored_parser_structured_root_free(root);
    root = decode(atom_expr(&arena, &tag, 1u), 1u, digest, error);
    check(root && cetta_op_lang_v1_application_is(root, "Tag", 0u) &&
          strcmp(digest, expected), "nullary application is not a bare symbol");
    authored_parser_structured_root_free(root);
    cetta_native_sha256_hex(bytes, sizeof(bytes) - 1u, expected);

    const unsigned depth = 20000u;
    Atom *deep = spine(&arena, depth, atom_symbol(&arena, "LNil"));
    root = decode(deep, 2u * depth + 1u, digest, error);
    bool exact = root != NULL;
    const CettaOpLangV1SExpr *cursor = root;
    for (unsigned i = depth; exact && i > 0u; --i) {
        char number[16];
        snprintf(number, sizeof(number), "%u", (i - 1u) % 7u);
        exact = cetta_op_lang_v1_application_is(cursor, "LCons", 2u) &&
            cursor->as.application.arguments[0]->kind == CETTA_OP_LANG_V1_SEXPR_NATURAL &&
            !strcmp(cursor->as.application.arguments[0]->as.natural, number);
        if (exact) cursor = cursor->as.application.arguments[1];
    }
    check(exact && cetta_op_lang_v1_symbol_is(cursor, "LNil"),
          "deep decoding preserves every ordered payload");
    authored_parser_structured_root_free(root);
    strcpy(digest, "unpublished");
    root = decode(deep, 2u * depth, digest, error);
    check(!root && strstr(error, "bound") && !strcmp(digest, "unpublished"),
          "exact work bound refuses without publishing a digest");
    authored_parser_structured_root_free(root);
    Atom *bad = spine(&arena, depth, atom_var(&arena, "open"));
    root = decode(bad, 2u * depth + 1u, digest, error);
    check(!root && strstr(error, "unsupported"), "deep malformed tail is cleaned up");
    authored_parser_structured_root_free(root);

    Atom *invalid[] = {atom_var(&arena, "x"), atom_int(&arena, -1),
                       atom_float(&arena, 0.5), atom_bool(&arena, true),
                       atom_expr(&arena, NULL, 0u)};
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        root = decode(invalid[i], 20u, digest, error);
        check(!root && error[0], "unsupported scalar/empty expression is refused");
        authored_parser_structured_root_free(root);
    }
    CettaOpLangV1Document document;
    cetta_op_lang_v1_document_init(&document);
    bool adopted = authored_parser_structured_value_decode_document(
        &document, sample, error, sizeof(error));
    CettaOpLangV1SExpr *old = document.root;
    check(adopted && !authored_parser_structured_value_decode_document(
        &document, invalid[0], error, sizeof(error)) && document.root == old &&
        !strcmp(document.authority_sha256, expected),
        "failed replacement preserves the existing document and authority");
    cetta_op_lang_v1_document_free(&document);

    /* Cross the first frame-stack growth, then fail each transport
     * allocation in turn. Cleanup must not allocate or retain partial trees. */
    Atom *allocation_sample = spine(&arena, 40u, atom_symbol(&arena, "LNil"));
    allocation_count = 0u;
    root = decode(allocation_sample, 81u, digest, error);
    size_t allocations = allocation_count;
    check(root != NULL, "allocation sweep reference succeeds");
    authored_parser_structured_root_free(root);
    for (size_t i = 0u; i < allocations; ++i) {
        allocation_count = 0u;
        allocation_failure = i;
        root = decode(allocation_sample, 81u, digest, error);
        allocation_failure = SIZE_MAX;
        check(!root && error[0], "injected allocation failure is reported and cleaned up");
        authored_parser_structured_root_free(root);
    }
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(StructuredParserValueV1Summary %u %u %u)\n",
           passed + failed, passed, failed);
    return failed ? 1 : 0;
}
