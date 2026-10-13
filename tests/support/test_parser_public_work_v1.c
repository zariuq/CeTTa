#include "native/langdef_module.h"
#include "native/language_def_parser_pack_v1.h"
#include "native/parser_baked_v1.h"
#include "native/parser_token_ast_v1.h"
#include "library.h"
#include "native_handle.h"
#include "native_sha256.h"
#include "parser_pack_abi_v1.h"
#include "regular_span_dfa_v1.h"
#include "symbol.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic unsigned sha_calls, constructions, compilations, audits;

void __real_cetta_native_sha256_init(CettaNativeSha256 *);
void __wrap_cetta_native_sha256_init(CettaNativeSha256 *sha) {
    ++sha_calls; __real_cetta_native_sha256_init(sha);
}
void __real_cetta_native_sha256_hex(const uint8_t *, size_t, char[65]);
void __wrap_cetta_native_sha256_hex(const uint8_t *bytes, size_t length, char out[65]) {
    ++sha_calls; __real_cetta_native_sha256_hex(bytes, length, out);
}
#define WRAP_TABLE(name, type) \
    bool __real_##name(type *, const CettaLpNativeGrammar *, uint32_t, char *, size_t); \
    bool __wrap_##name(type *out, const CettaLpNativeGrammar *grammar, uint32_t start, char *error, size_t size) { \
        ++constructions; return __real_##name(out, grammar, start, error, size); \
    }
WRAP_TABLE(cetta_lp_native_gll_prepare, CettaLpNativeGllPrepared)
WRAP_TABLE(cetta_lp_native_glr_prepare, CettaLpNativeGlrPrepared)
bool __real_cetta_lp_native_predictive_build(const CettaLpNativeGllPrepared *,
    const CettaLpNativeUtf8Terminal *, uint32_t, bool, uint32_t, CettaLpNativePredictiveProgram *, char *, size_t);
bool __wrap_cetta_lp_native_predictive_build(const CettaLpNativeGllPrepared *prepared,
    const CettaLpNativeUtf8Terminal *terminals, uint32_t count, bool chain, uint32_t eof,
    CettaLpNativePredictiveProgram *out, char *error, size_t size) {
    ++constructions;
    return __real_cetta_lp_native_predictive_build(prepared, terminals, count, chain, eof, out, error, size);
}
bool __real_rsdfa_v1_plan_build(const RSDFAV1Nfa *, uint32_t, uint32_t, RSDFAV1Plan *, RSDFAV1BuildOutcome *, char *, size_t);
bool __wrap_rsdfa_v1_plan_build(const RSDFAV1Nfa *nfa, uint32_t states, uint32_t edges,
    RSDFAV1Plan *out, RSDFAV1BuildOutcome *outcome, char *error, size_t size) {
    ++constructions; return __real_rsdfa_v1_plan_build(nfa, states, edges, out, outcome, error, size);
}
bool __real_cetta_language_def_parser_pack_v1_compile_source(CettaLdParserPackV1 *,
    const CettaLanguageDefCoreV1 *, const char[65], const CettaLdParserProfileV1 *,
    const uint8_t *, size_t, uint32_t, CettaLdParserPackV1Status *, char *, size_t);
bool __wrap_cetta_language_def_parser_pack_v1_compile_source(CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language, const char digest[65], const CettaLdParserProfileV1 *profile,
    const uint8_t *source, size_t length, uint32_t work, CettaLdParserPackV1Status *status, char *error, size_t size) {
    ++compilations;
    return __real_cetta_language_def_parser_pack_v1_compile_source(out, language, digest, profile,
        source, length, work, status, error, size);
}
bool __real_cetta_language_def_parser_pack_v1_compile(CettaLdParserPackV1 *,
    const CettaLanguageDefCoreV1 *, const char[65], const CettaLdParserProfileV1 *,
    uint32_t, CettaLdParserPackV1Status *, char *, size_t);
bool __wrap_cetta_language_def_parser_pack_v1_compile(CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language, const char digest[65], const CettaLdParserProfileV1 *profile,
    uint32_t work, CettaLdParserPackV1Status *status, char *error, size_t size) {
    ++compilations;
    return __real_cetta_language_def_parser_pack_v1_compile(out, language, digest, profile,
        work, status, error, size);
}
bool __real_ppabi_v1_pack_start_is_closed(const PPABIV1Pack *, const Atom *, char *, size_t);
bool __wrap_ppabi_v1_pack_start_is_closed(const PPABIV1Pack *pack, const Atom *start, char *error, size_t size) {
    ++audits; return __real_ppabi_v1_pack_start_is_closed(pack, start, error, size);
}
bool __real_cetta_lp_native_utf8_scalar_view_validate(const CettaLpNativeUtf8ScalarView *, char *, size_t);
bool __wrap_cetta_lp_native_utf8_scalar_view_validate(const CettaLpNativeUtf8ScalarView *view, char *error, size_t size) {
    ++audits; return __real_cetta_lp_native_utf8_scalar_view_validate(view, error, size);
}
bool __real_cetta_lp_native_utf8_lattice_validate(const CettaLpNativeUtf8Lattice *, char *, size_t);
bool __wrap_cetta_lp_native_utf8_lattice_validate(const CettaLpNativeUtf8Lattice *view, char *error, size_t size) {
    ++audits; return __real_cetta_lp_native_utf8_lattice_validate(view, error, size);
}

static void reset_work(void) { sha_calls = constructions = compilations = audits = 0u; }
static void no_work(void) {
    if (sha_calls || constructions || compilations || audits)
        fprintf(stderr, "unexpected work: sha=%u tables/dfa=%u compile=%u audits=%u\n",
            (unsigned)sha_calls, (unsigned)constructions, (unsigned)compilations, (unsigned)audits);
    assert(!sha_calls && !constructions && !compilations && !audits);
}
static Atom *call(CettaLibraryContext *ctx, Arena *arena, const char *head, Atom **args, uint32_t len) {
    Atom *result = cetta_langdef_module_dispatch(ctx, NULL, arena, atom_symbol(arena, head), args, len);
    assert(result);
    return result;
}
static uint64_t handle_id(Atom *handle) {
    uint64_t id = 0u;
    assert(cetta_native_handle_arg(handle, "cetta.authored-parser.v1", &id));
    return id;
}
static Atom *load(CettaLibraryContext *ctx, Arena *arena, const char *name) {
    Atom *argument = atom_string(arena, name);
    Atom *value = call(ctx, arena, "__cetta_lib_parser_load_baked", &argument, 1u);
    (void)handle_id(value);
    return value;
}
static void parse(CettaLibraryContext *ctx, Arena *arena, Atom *parser, const char *source, uint32_t values) {
    Atom *args[] = {parser, atom_string(arena, source)};
    Atom *value = call(ctx, arena, "__cetta_lib_parser_parse_text", args, 2u);
    assert(cetta_langdef_expr_head(value, "ParserAcceptedV1", 2u));
    assert(value->expr.elems[1]->kind == ATOM_EXPR && value->expr.elems[1]->expr.len == values);
}
static void close_parser(CettaLibraryContext *ctx, Arena *arena, Atom *parser) {
    Atom *value = call(ctx, arena, "__cetta_lib_authored_parser_close", &parser, 1u);
    assert(cetta_langdef_expr_head(value, "return", 1u));
    assert(value->expr.elems[1]->kind == ATOM_GROUNDED && value->expr.elems[1]->ground.gkind == GV_BOOL &&
        value->expr.elems[1]->ground.bval);
}
typedef struct { CettaLibraryContext *ctx; Atom *handle; } Worker;
static void *parse_worker(void *input) {
    Worker *worker = input;
    for (unsigned i = 0u; i < 40u; ++i) {
        Arena arena;
        arena_init_detached(&arena);
        parse(worker->ctx, &arena, worker->handle, "a", 2u);
        arena_free(&arena);
    }
    return NULL;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    CettaLibraryContext *ctx = calloc(1u, sizeof(*ctx));
    assert(ctx);
    cetta_library_context_init(ctx);
    Arena source, survivor;
    arena_init_detached(&source); arena_init_detached(&survivor);
    unsigned checks = 0u;
    reset_work();
    Atom *first = load(ctx, &source, "joined-parser");
    Atom *second = load(ctx, &source, "joined-parser");
    assert(handle_id(first) != handle_id(second));
    assert(cetta_native_handle_get(ctx, "cetta.authored-parser.v1", handle_id(first)) !=
           cetta_native_handle_get(ctx, "cetta.authored-parser.v1", handle_id(second)));
    Atom *info = call(ctx, &source, "__cetta_lib_parser_info", &first, 1u);
    assert(cetta_langdef_expr_head(info, "ParserInfoV1", 6u) && info->expr.elems[4]->ground.ival == 2);
    no_work(); checks += 3u;
    for (unsigned i = 0u; i < 20u; ++i) parse(ctx, &source, first, "a", 2u);
    no_work(); checks += 2u;
    Atom *promoted = atom_deep_copy(&survivor, second);
    assert(promoted);
    close_parser(ctx, &source, first);
    arena_free(&source);
    parse(ctx, &survivor, promoted, "a", 2u);
    no_work(); checks += 2u;

    Atom *independent = load(ctx, &survivor, "joined-parser");
    Worker workers[2] = {{ctx, promoted}, {ctx, independent}};
    pthread_t threads[2];
    for (unsigned i = 0u; i < 2u; ++i) assert(!pthread_create(&threads[i], NULL, parse_worker, &workers[i]));
    for (unsigned i = 0u; i < 2u; ++i) assert(!pthread_join(threads[i], NULL));
    no_work(); ++checks;
    close_parser(ctx, &survivor, independent);

    Atom *path = atom_string(&survivor, "runtime/parser-public-work/artifact.tpp");
    Atom *freeze_args[] = {promoted, path};
    Atom *frozen = call(ctx, &survivor, "__cetta_lib_parser_freeze", freeze_args, 2u);
    assert(cetta_langdef_expr_head(frozen, "ParserArtifactV1", 2u) && sha_calls);
    ++checks;
    reset_work();
    Atom *restored = call(ctx, &survivor, "__cetta_lib_parser_load_artifact", &path, 1u);
    (void)handle_id(restored);
    no_work(); ++checks;
    parse(ctx, &survivor, restored, "a", 2u);
    no_work(); ++checks;
    close_parser(ctx, &survivor, restored);
    reset_work();
    restored = call(ctx, &survivor, "__cetta_lib_parser_load_artifact_checked", &path, 1u);
    (void)handle_id(restored);
    assert(sha_calls && !compilations && !constructions);
    ++checks;
    close_parser(ctx, &survivor, restored);
    close_parser(ctx, &survivor, promoted);

    reset_work();
    Atom *authored_args[] = {atom_string(&survivor, "langdef/bnf/plain_bnf_source_v1.metta"),
                            atom_string(&survivor, "langdef/bnf/plain_bnf_parser_profile_v1.metta")};
    Atom *authored = call(ctx, &survivor, "__cetta_lib_authored_parser_load", authored_args, 2u);
    assert(cetta_langdef_expr_head(authored, "return", 1u));
    assert(sha_calls && compilations && constructions);
    ++checks;
    close_parser(ctx, &survivor, authored->expr.elems[1]);
    reset_work();
    Atom *predictive = load(ctx, &survivor, "token-word");
    for (unsigned i = 0u; i < 20u; ++i) parse(ctx, &survivor, predictive, " /* note */ ÿz ", 1u);
    no_work(); ++checks;
    Atom *predictive_freeze_args[] = {predictive, path};
    frozen = call(ctx, &survivor, "__cetta_lib_parser_freeze", predictive_freeze_args, 2u);
    assert(cetta_langdef_expr_head(frozen, "ParserArtifactV1", 2u));
    close_parser(ctx, &survivor, predictive);
    reset_work();
    restored = call(ctx, &survivor, "__cetta_lib_parser_load_artifact", &path, 1u);
    for (unsigned i = 0u; i < 20u; ++i) parse(ctx, &survivor, restored, "ÿz", 1u);
    no_work(); ++checks;
    close_parser(ctx, &survivor, restored);
    /* The externally supplied source-view boundary still validates offsets. */
    CettaParserTokenProfileV1 profile;
    cetta_parser_token_profile_v1_init(&profile);
    uint8_t start_name[] = "S";
    CettaLdTextV1 start = {start_name, 1u};
    uint32_t scalar = 'a', bad_offsets[] = {0u, 0u};
    CettaLpNativeUtf8ScalarView bad = {.codepoints = &scalar, .byte_offsets = bad_offsets,
        .scalar_len = 1u, .input_byte_len = 1u};
    Atom *projected = NULL;
    char error[512];
    reset_work();
    assert(!cetta_parser_token_ast_v1_project(&survivor, &profile, &start, &bad,
        atom_symbol(&survivor, "bad-cst"), 100u, 1000u, &projected, error, sizeof(error)));
    assert(audits);
    cetta_parser_token_profile_v1_free(&profile);
    ++checks;
    /* Teardown must retire an unclosed owner as well as an explicitly closed one. */
    (void)load(ctx, &survivor, "bnf-reader");
    cetta_library_context_free(ctx);
    arena_free(&survivor);
    free(ctx);
    ++checks;
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(ParserPublicWorkV1Summary %u 0)\n", checks);
    return 0;
}
