#define _POSIX_C_SOURCE 200809L
#include "native/langdef_module.h"
#include "src/library.h"
#include "src/native_handle.h"
#include "src/symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + value.tv_nsec;
}

static Atom *call(CettaLibraryContext *ctx, Arena *arena, const char *head,
                  Atom **arguments, uint32_t count) {
    Atom *value = cetta_langdef_module_dispatch(ctx, NULL, arena,
        atom_symbol(arena, head), arguments, count);
    if (!value || atom_is_error(value)) {
        fprintf(stderr, "benchmark operation failed: %s\n", head);
        exit(1);
    }
    return value;
}

static Atom *unhold(Atom *value) {
    return cetta_langdef_expr_head(value, "return", 1u)
        ? value->expr.elems[1] : value;
}

static void parser_handle(Atom *value) {
    uint64_t id;
    if (!cetta_native_handle_arg(value, "cetta.authored-parser.v1", &id)) {
        fputs("benchmark expected a parser handle\n", stderr);
        exit(1);
    }
}

static uint64_t parse(CettaLibraryContext *ctx, Atom *handle,
                      const char *source, uint32_t count, bool evidence) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *args[] = {handle, atom_string(&arena, source), atom_int(&arena, 200000)};
    ArenaMark mark = arena_mark(&arena);
    uint64_t start = now();
    for (uint32_t i = 0u; i < count; ++i) {
        Atom *value = unhold(call(ctx, &arena, evidence
            ? "__cetta_lib_authored_parser_parse_text_evidence"
            : "__cetta_lib_parser_parse_text", args, evidence ? 3u : 2u));
        if (!cetta_langdef_expr_head(value, evidence
                ? "LangDef:AuthoredParseEvidenceAccepted" : "ParserAcceptedV1",
                evidence ? 6u : 2u)) {
            fputs("benchmark input was not accepted\n", stderr);
            exit(1);
        }
        arena_reset(&arena, mark);
    }
    uint64_t elapsed = now() - start;
    arena_free(&arena);
    return elapsed;
}

static void close_parser(CettaLibraryContext *ctx, Arena *arena, Atom *handle) {
    Atom *value = unhold(call(ctx, arena, "__cetta_lib_authored_parser_close", &handle, 1u));
    if (value->kind != ATOM_GROUNDED || value->ground.gkind != GV_BOOL || !value->ground.bval)
        abort();
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s artifact-output\n", argv[0]);
        return 2;
    }
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    CettaLibraryContext *ctx = calloc(1u, sizeof(*ctx));
    if (!ctx) return 1;
    cetta_library_context_init(ctx);
    Arena arena;
    arena_init_detached(&arena);
    Atom *args[] = {atom_string(&arena, "langdef/bnf/plain_bnf_source_v1.metta"),
                   atom_string(&arena, "langdef/bnf/plain_bnf_parser_profile_v1.metta")};
    uint64_t start = now();
    Atom *handle = unhold(call(ctx, &arena, "__cetta_lib_authored_parser_load", args, 2u));
    uint64_t construction = now() - start;
    parser_handle(handle);
    const char sample[] = "<S> ::= \"a\"\n";
    uint64_t first = parse(ctx, handle, sample, 1u, false);
    uint64_t repeated = parse(ctx, handle, sample, 200u, false);
    uint64_t evidence = parse(ctx, handle, sample, 20u, true);
    Atom *path = atom_string(&arena, argv[1]);
    Atom *freeze[] = {handle, path};
    start = now();
    Atom *frozen = call(ctx, &arena, "__cetta_lib_parser_freeze", freeze, 2u);
    uint64_t freezing = now() - start;
    if (!cetta_langdef_expr_head(frozen, "ParserArtifactV1", 2u)) return 1;
    close_parser(ctx, &arena, handle);
    start = now();
    handle = call(ctx, &arena, "__cetta_lib_parser_load_artifact", &path, 1u);
    uint64_t restoring = now() - start;
    parser_handle(handle);
    uint64_t restored_first = parse(ctx, handle, sample, 1u, false);
    close_parser(ctx, &arena, handle);
    start = now();
    handle = call(ctx, &arena, "__cetta_lib_parser_load_artifact_checked", &path, 1u);
    uint64_t checked = now() - start;
    parser_handle(handle);
    close_parser(ctx, &arena, handle);
    Atom *name = atom_string(&arena, "bnf-reader");
    start = now();
    handle = call(ctx, &arena, "__cetta_lib_parser_load_baked", &name, 1u);
    uint64_t baked = now() - start;
    parser_handle(handle);
    uint64_t baked_first = parse(ctx, handle, sample, 1u, false);
    close_parser(ctx, &arena, handle);
    printf("{\"construction_ns\":%" PRIu64 ",\"first_parse_ns\":%" PRIu64
           ",\"repeated_200_ns\":%" PRIu64 ",\"evidence_20_ns\":%" PRIu64
           ",\"freeze_ns\":%" PRIu64 ",\"trusted_cold_restore_ns\":%" PRIu64
           ",\"restored_first_parse_ns\":%" PRIu64 ",\"checked_cold_restore_ns\":%" PRIu64
           ",\"baked_cold_restore_ns\":%" PRIu64 ",\"baked_first_parse_ns\":%" PRIu64 "}\n",
           construction, first, repeated, evidence, freezing, restoring,
           restored_first, checked, baked, baked_first);
    arena_free(&arena);
    cetta_library_context_free(ctx);
    free(ctx);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
