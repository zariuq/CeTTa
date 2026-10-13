#include "native/language_def_parser_pack_v1.h"
#include "native/lbnf_document_v1.h"
#include "native/parser_token_profile_v1.h"
#include "native/parser_token_runtime_v1.h"
#include "native/parser_token_ast_v1.h"
#include "native_sha256.h"
#include "parser.h"
#include "parser_pack_gll_v1.h"
#include "parser_pack_table_snapshot_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed, failed, sha_calls, table_builds, grammar_compilations, grammar_audits;

void cetta_parser_qualification_input_audit(void) { ++grammar_audits; }

#define WRAP_PREPARATION(name, type) \
    bool __real_##name(type *, const CettaLpNativeGrammar *, uint32_t, char *, size_t); \
    bool __wrap_##name(type *out, const CettaLpNativeGrammar *grammar, uint32_t start, \
        char *error, size_t size) { \
        ++table_builds; return __real_##name(out, grammar, start, error, size); \
    }
WRAP_PREPARATION(cetta_lp_native_gll_prepare, CettaLpNativeGllPrepared)
WRAP_PREPARATION(cetta_lp_native_glr_prepare, CettaLpNativeGlrPrepared)

bool __real_rsdfa_v1_plan_build(const RSDFAV1Nfa *, uint32_t, uint32_t,
    RSDFAV1Plan *, RSDFAV1BuildOutcome *, char *, size_t);
bool __wrap_rsdfa_v1_plan_build(const RSDFAV1Nfa *nfa, uint32_t states, uint32_t edges,
    RSDFAV1Plan *out, RSDFAV1BuildOutcome *outcome, char *error, size_t size) {
    ++table_builds;
    return __real_rsdfa_v1_plan_build(nfa, states, edges, out, outcome, error, size);
}
bool __real_cetta_language_def_parser_pack_v1_compile_source(CettaLdParserPackV1 *,
    const CettaLanguageDefCoreV1 *, const char[65], const CettaLdParserProfileV1 *,
    const uint8_t *, size_t, uint32_t, CettaLdParserPackV1Status *, char *, size_t);
bool __wrap_cetta_language_def_parser_pack_v1_compile_source(CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language, const char digest[65], const CettaLdParserProfileV1 *profile,
    const uint8_t *source, size_t length, uint32_t work, CettaLdParserPackV1Status *status,
    char *error, size_t size) {
    ++grammar_compilations;
    return __real_cetta_language_def_parser_pack_v1_compile_source(out, language, digest,
        profile, source, length, work, status, error, size);
}
bool __real_ppabi_v1_pack_start_is_closed(const PPABIV1Pack *, const Atom *, char *, size_t);
bool __wrap_ppabi_v1_pack_start_is_closed(const PPABIV1Pack *pack, const Atom *start,
    char *error, size_t size) {
    ++grammar_audits;
    return __real_ppabi_v1_pack_start_is_closed(pack, start, error, size);
}

static void reset_work(void) {
    sha_calls = table_builds = grammar_compilations = grammar_audits = 0u;
}

static bool no_construction_or_audit(void) {
    return !sha_calls && !table_builds && !grammar_compilations && !grammar_audits;
}

void __real_cetta_native_sha256_init(CettaNativeSha256 *sha);
void __real_cetta_native_sha256_hex(const uint8_t *bytes, size_t len, char out[65]);
void __wrap_cetta_native_sha256_init(CettaNativeSha256 *sha) {
    ++sha_calls;
    __real_cetta_native_sha256_init(sha);
}
void __wrap_cetta_native_sha256_hex(const uint8_t *bytes, size_t len, char out[65]) {
    ++sha_calls;
    __real_cetta_native_sha256_hex(bytes, len, out);
}

static bool check(bool condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
    return condition;
}

static Atom *data(Arena *arena, const char *source) {
    Atom **values = NULL;
    Atom *value = parse_metta_text(source, arena, &values) == 1 ? values[0] : NULL;
    free(values);
    return value;
}

static void general_snapshot(const PPNativeV1Prepared *prepared, const PPABIV1Pack *pack) {
    PPTableSnapshotV1 snapshot, restored;
    CettaLpNativeGllPrepared imported;
    CettaLpNativeGlrPrepared imported_glr;
    PPNativeV1Prepared restored_prepared;
    PPNativeV1PreparedView view;
    PPNativeV1Result result;
    PPABIV1Pack restored_pack;
    Arena metadata;
    uint8_t *bytes = NULL;
    size_t size = 0u, written = 0u;
    char error[512] = {0};
    pp_table_snapshot_v1_init(&snapshot);
    pp_table_snapshot_v1_init(&restored);
    cetta_lp_native_gll_prepared_init(&imported);
    cetta_lp_native_glr_prepared_init(&imported_glr);
    ppnative_v1_prepared_init(&restored_prepared);
    ppnative_v1_result_init(&result);
    ppabi_v1_pack_init(&restored_pack);
    arena_init(&metadata);
    snapshot.kernel = PP_TABLE_SNAPSHOT_V1_KERNEL_GENERAL;
    strcpy(snapshot.syntax_digest, pack->source_digest);
    strcpy(snapshot.artifact_digest, pack->pack_digest);
    strcpy(snapshot.profile, "parserpack-dense");
    Atom *stored_pack = ppabi_v1_pack_export_storage(&metadata, pack, error, sizeof(error));
    size_t metadata_size = 0u;
    if (!check(stored_pack && fh_ground_term_v1_render(stored_pack,
        &snapshot.general_metadata, &metadata_size, error, sizeof(error)) &&
        metadata_size <= UINT32_MAX, "retain symbolic pack identities and projection in storage")) goto done;
    snapshot.general_metadata_len = (uint32_t)metadata_size;
    if (!check(ppnative_v1_prepared_immutable_view(prepared, &view) &&
        cetta_lp_native_gll_prepared_export_program(view.gll, &snapshot.gll,
            error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_export_program(view.glr, &snapshot.slr,
            error, sizeof(error)) &&
        pp_table_snapshot_v1_size(&snapshot, &size, error, sizeof(error)),
        error[0] ? error : "export complete GLL preparation")) goto done;
    snapshot.scalar_terminals.terminals = calloc(view.terminal_plan->len,
        sizeof(*snapshot.scalar_terminals.terminals));
    snapshot.scalar_terminals.owned_ranges = calloc(view.terminal_plan->len,
        sizeof(*snapshot.scalar_terminals.owned_ranges));
    if (!check(snapshot.scalar_terminals.terminals && snapshot.scalar_terminals.owned_ranges,
        "allocate scalar-terminal storage")) goto done;
    snapshot.scalar_terminals.len = view.terminal_plan->len;
    for (uint32_t i = 0u; i < view.terminal_plan->len; ++i) {
        const CettaLpNativeUtf8Terminal *terminal = &view.terminal_plan->terminals[i];
        snapshot.scalar_terminals.terminals[i] = *terminal;
        if (!terminal->range_len) continue;
        CettaLpNativeUnicodeRange *ranges = malloc((size_t)terminal->range_len * sizeof(*ranges));
        if (!check(ranges != NULL, "allocate scalar-terminal ranges")) goto done;
        memcpy(ranges, terminal->ranges, (size_t)terminal->range_len * sizeof(*ranges));
        snapshot.scalar_terminals.owned_ranges[i] = ranges;
        snapshot.scalar_terminals.terminals[i].ranges = ranges;
    }
    if (!check(pp_table_snapshot_v1_size(&snapshot, &size, error, sizeof(error)),
        "size complete scalar preparation")) goto done;
    bytes = malloc(size);
    if (!check(bytes && pp_table_snapshot_v1_write(&snapshot, bytes, size,
        &written, error, sizeof(error)) && written == size,
        error[0] ? error : "write version 5 GLL snapshot")) goto done;
    reset_work();
    Atom *decoded_pack = NULL;
    SymbolTable *symbols = g_symbols;
    g_symbols = NULL;
    bool read = pp_table_snapshot_v1_read(&restored, bytes, size, error, sizeof(error));
    g_symbols = symbols;
    if (!check(read &&
        fh_ground_term_v1_read_source(&metadata, restored.general_metadata,
            restored.general_metadata_len, &decoded_pack, error, sizeof(error)) &&
        ppabi_v1_pack_import_storage(&restored_pack, decoded_pack, error, sizeof(error)) &&
        cetta_lp_native_gll_prepared_import_program(&imported, &restored.gll,
            error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_import_program(&imported_glr, &restored.slr,
            &restored.gll, error, sizeof(error)) &&
        ppnative_v1_prepared_import_programs(&restored_prepared, &restored_pack,
            restored.gll.start_nonterminal, &restored.scalar_terminals,
            &restored.gll, &restored.slr, error, sizeof(error)) && no_construction_or_audit(),
        error[0] ? error : "restore GLL/GLR decisions without hashes or compilation")) goto done;
    pp_table_snapshot_v1_free(&restored);
    pp_table_snapshot_v1_free(&snapshot);
    arena_free(&metadata);
    arena_init(&metadata);
    PPNativeV1PreparedView restored_view;
    check(ppgll_v1_prepared_parse_values(&restored_prepared, (const uint8_t *)"a", 1u,
        10000u, 128u, 32u, &result, error, sizeof(error)) &&
        result.semantic_result_len == 2u && no_construction_or_audit(),
        error[0] ? error : "restored program retains both derivations after exporter teardown");
    check(ppnative_v1_prepared_immutable_view(&restored_prepared, &restored_view) &&
        cetta_lp_native_glr_prepared_parse_owned_utf8_forest(restored_view.glr,
        restored_view.terminal_plan->terminals, restored_view.terminal_plan->len,
        (const uint8_t *)"a", 1u, 10000u, &result.forest, error, sizeof(error)) &&
        ppnative_v1_finish_values(&result, &restored_pack, NULL, 128u, 32u, error, sizeof(error)) &&
        result.semantic_result_len == 2u && no_construction_or_audit(),
        error[0] ? error : "restored GLR retains conflicting reductions and duplicate values");
    CettaLpNativeUtf8Terminal bad = restored_view.terminal_plan->terminals[0];
    bad.kind = (CettaLpNativeUtf8TerminalKind)UINT32_MAX;
    reset_work();
    check(!cetta_lp_native_gll_prepared_parse_utf8_forest_complete(restored_view.gll,
        &bad, 1u, (const uint8_t *)"a", 1u, 10000u, &result.forest, error, sizeof(error)) &&
        grammar_audits && error[0], "external terminal tables retain their validating boundary");
    check(ppabi_v1_pack_check_storage(&restored_pack, error, sizeof(error)) && sha_calls,
        "explicit stored-pack audit retains the existing ABI admission checks");
    bool saved_defined = restored_pack.states[0].defined;
    restored_pack.states[0].defined = !saved_defined;
    error[0] = '\0';
    check(!ppabi_v1_pack_check_storage(&restored_pack, error, sizeof(error)) && error[0],
        "checked inspection refuses a forged stored closure fact");
    restored_pack.states[0].defined = saved_defined;
    bool atomic = true;
    for (size_t truncated = 0u; truncated < size; ++truncated) {
        error[0] = '\0';
        if (pp_table_snapshot_v1_read(&restored, bytes, truncated, error, sizeof(error)) ||
            !error[0] || restored.gll.productions || restored.general_metadata) {
            atomic = false;
            break;
        }
    }
    check(atomic, "every snapshot truncation is refused atomically");
    uint32_t offset = 136u + 4u + (uint32_t)strlen("parserpack-dense") + 8u;
    uint8_t saved[4];
    memcpy(saved, bytes + offset, 4u);
    memset(bytes + offset, 255, 4u);
    check(!pp_table_snapshot_v1_read(&restored, bytes, size, error, sizeof(error)) &&
        !restored.gll.productions, "impossible declaration counts refuse before allocation");
    memcpy(bytes + offset, saved, 4u);
    bytes[4] = 255u;
    check(!pp_table_snapshot_v1_read(&restored, bytes, size, error, sizeof(error)) &&
        strstr(error, "version"), "unsupported snapshot version refuses without replacement");
done:
    free(bytes);
    arena_free(&metadata);
    ppnative_v1_prepared_free(&restored_prepared);
    ppabi_v1_pack_free(&restored_pack);
    ppnative_v1_result_free(&result);
    cetta_lp_native_gll_prepared_free(&imported);
    cetta_lp_native_glr_prepared_free(&imported_glr);
    pp_table_snapshot_v1_free(&restored);
    pp_table_snapshot_v1_free(&snapshot);
}

static void scannerless(Arena *arena) {
    static const char digest[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    Atom *productions[] = {
        data(arena, "(pp-production left S (pp-items-cons (pp-terminal (pp-terminal-char (cp 97))) pp-items-nil) (pa-const Same))"),
        data(arena, "(pp-production right S (pp-items-cons (pp-terminal (pp-terminal-char (cp 97))) pp-items-nil) (pa-const Same))")
    };
    PPABIV1Pack pack;
    PPNativeV1Prepared prepared;
    PPNativeV1Result values, evidence;
    char error[512] = {0};
    ppabi_v1_pack_init(&pack);
    ppnative_v1_prepared_init(&prepared);
    ppnative_v1_result_init(&values);
    ppnative_v1_result_init(&evidence);
    if (!check(productions[0] && productions[1] &&
        ppabi_v1_pack_load_structural(&pack, productions, 2u, NULL, 0u,
            digest, digest, digest, error, sizeof(error)) &&
        ppnative_v1_prepare(&prepared, &pack, atom_symbol(arena, "S"),
            error, sizeof(error)), error[0] ? error : "prepare ambiguous scannerless grammar")) goto done;
    general_snapshot(&prepared, &pack);
    for (unsigned i = 0u; i < 3u; ++i) {
        reset_work();
        bool ok = ppgll_v1_prepared_parse_values(&prepared,
            (const uint8_t *)"a", 1u, 10000u, 128u, 32u,
            &values, error, sizeof(error));
        check(ok && values.accepted && values.semantic_result_len == 2u &&
            atom_eq(values.semantic_results[0], values.semantic_results[1]),
            "equal values retain both production occurrences");
        check(no_construction_or_audit() && !values.forest_digest[0] &&
            !values.canonical_forest && !values.canonical_forest_materialized &&
            ppnative_v1_prepared_table_build_count(&prepared) == 1u,
            "repeated values parsing performs no digest or canonical evidence work");
    }
    check(ppgll_v1_prepared_parse(&prepared, (const uint8_t *)"a", 1u,
        10000u, 128u, 32u, &evidence, error, sizeof(error)) &&
        evidence.accepted && evidence.semantic_result_len == 1u &&
        evidence.forest_digest[0] && sha_calls > 0u,
        "legacy receipt path keeps its evidence and set-valued projection");
    check(ppgll_v1_prepared_parse_values(&prepared, (const uint8_t *)"b", 1u,
        10000u, 128u, 32u, &values, error, sizeof(error)) &&
        !values.accepted && values.outcome == PPNATIVE_V1_COMPLETED &&
        values.semantic_result_len == 0u && !values.forest_digest[0],
        "positioned rejection does not manufacture evidence");
    check(ppgll_v1_prepared_parse_values(&prepared, (const uint8_t *)"a", 1u,
        10000u, 128u, 1u, &values, error, sizeof(error)) &&
        values.outcome == PPNATIVE_V1_RESULT_LIMIT,
        "occurrence limit is incomplete, not accepted or rejected");
    check(ppgll_v1_prepared_parse_values(&prepared, (const uint8_t *)"a", 1u,
        1u, 128u, 32u, &values, error, sizeof(error)) &&
        values.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT,
        "recognizer exhaustion stays incomplete");
    char saved = pack.pack_digest[0];
    pack.pack_digest[0] = saved == 'a' ? 'b' : 'a';
    check(!ppgll_v1_prepared_parse(&prepared, (const uint8_t *)"a", 1u,
        10000u, 128u, 32u, &evidence, error, sizeof(error)),
        "borrowed receipt API still refuses stale preparation");
    pack.pack_digest[0] = saved;
done:
    ppnative_v1_result_free(&evidence);
    ppnative_v1_result_free(&values);
    ppnative_v1_prepared_free(&prepared);
    ppabi_v1_pack_free(&pack);
}

static bool compile_lowered(Arena *arena, const CettaLbnfLoweredV1 *lowered,
    CettaLdParserProfileV1 *profile, CettaLdParserPackV1 *compiled,
    char *error, size_t size) {
    CettaOperationalLanguageDefV1 wire;
    CettaOpLangV1Document document;
    CettaLanguageDefCoreV1 core;
    CettaOpLangV1Status ingress;
    CettaLdCoreV1Status core_status;
    CettaLdParserPackV1Status status;
    cetta_op_lang_v1_init(&wire);
    cetta_op_lang_v1_document_init(&document);
    cetta_language_def_core_v1_init(&core);
    char *language = atom_to_parseable_string(arena, lowered->language);
    char *parser = atom_to_parseable_string(arena, lowered->profile);
    bool ok = language && parser && cetta_op_lang_v1_parse_bytes(&wire,
            (const uint8_t *)language, strlen(language), 4000000u,
            4000000u, &ingress, error, size);
    if (ok) ok = cetta_language_def_core_v1_decode(&core, &wire, 1000000u,
            &core_status, error, size) &&
        cetta_op_lang_v1_parse_document_bytes(&document,
            (const uint8_t *)parser, strlen(parser), 4000000u,
            4000000u, &ingress, error, size) &&
        cetta_ld_parser_profile_v1_decode(profile, &document, 1000000u,
            &status, error, size) &&
        cetta_language_def_parser_pack_v1_validate_structure(&core, profile,
            4000000u, &status, error, size);
    if (ok) {
        reset_work();
        check(cetta_language_def_parser_pack_v1_validate_structure(&core, profile,
            4000000u, &status, error, size) && no_construction_or_audit(),
            "structural admission builds no compiler program, tables or provenance hashes");
        ok = cetta_language_def_parser_pack_v1_compile(compiled, &core,
            wire.authority_sha256, profile, 4000000u, &status, error, size);
    }
    cetta_language_def_core_v1_free(&core);
    cetta_op_lang_v1_document_free(&document);
    cetta_op_lang_v1_free(&wire);
    return ok;
}

static void token_program_restore(CettaParserTokenRuntimeV1 *original,
    const CettaLdParserPackV1 *compiled, const CettaParserTokenProfileV1 *profile) {
    PPTableSnapshotV1 snapshot, decoded;
    CettaParserTokenRuntimeV1 restored;
    CettaLdParserPackV1 restored_compiled;
    CettaParserTokenScanV1 scan;
    PPNativeV1Result result;
    Arena metadata;
    Atom *stored = NULL, *read = NULL;
    uint8_t *bytes = NULL;
    size_t metadata_len = 0u, size = 0u, written = 0u;
    char error[512] = {0};
    pp_table_snapshot_v1_init(&snapshot);
    pp_table_snapshot_v1_init(&decoded);
    cetta_parser_token_runtime_v1_init(&restored);
    cetta_ld_parser_pack_v1_init(&restored_compiled);
    cetta_parser_token_scan_v1_init(&scan);
    ppnative_v1_result_init(&result);
    arena_init(&metadata);
    if (!check(cetta_parser_token_runtime_v1_export_program(original, &snapshot,
        &metadata, &stored, error, sizeof(error)), "export token execution program")) goto done;
    Atom *stored_compiled = cetta_ld_parser_pack_v1_export_storage(&metadata,
        compiled, error, sizeof(error));
    if (!check(stored_compiled != NULL, "export complete tokenized grammar and bindings")) goto done;
    stored = atom_expr3(&metadata, atom_symbol(&metadata, "ParserTestStoredV1"), stored_compiled, stored);
    if (!check(stored_compiled && stored &&
        fh_ground_term_v1_render(stored, &snapshot.general_metadata, &metadata_len, error, sizeof(error)) &&
        metadata_len <= UINT32_MAX, "freeze token priorities, comments, value adapters and DFA decisions")) goto done;
    snapshot.general_metadata_len = (uint32_t)metadata_len;
    if (!check(pp_table_snapshot_v1_size(&snapshot, &size, error, sizeof(error)),
        "size complete token preparation")) goto done;
    bytes = malloc(size);
    if (!check(bytes && pp_table_snapshot_v1_write(&snapshot, bytes, size, &written, error, sizeof(error)),
        "write complete token preparation")) goto done;
    pp_table_snapshot_v1_free(&snapshot);
    arena_free(&metadata);
    arena_init(&metadata);
    reset_work();
    if (!check(pp_table_snapshot_v1_read(&decoded, bytes, written, error, sizeof(error)) &&
        fh_ground_term_v1_read_source(&metadata, decoded.general_metadata, decoded.general_metadata_len,
            &read, error, sizeof(error)) &&
        read && read->kind == ATOM_EXPR && read->expr.len == 3u &&
        cetta_ld_parser_pack_v1_import_storage(&restored_compiled, read->expr.elems[1], error, sizeof(error)) &&
        cetta_parser_token_runtime_v1_import_program(&restored, &restored_compiled, profile, &decoded,
            read->expr.elems[2], error, sizeof(error)) && no_construction_or_audit(),
        error[0] ? error : "restore tokenized GLL/GLR without lexer or table construction")) goto done;
    pp_table_snapshot_v1_free(&decoded);
    arena_free(&metadata);
    arena_init(&metadata);
    for (unsigned glr = 0u; glr < 2u; ++glr) {
        check(cetta_parser_token_runtime_v1_scan_values(&restored, (const uint8_t *)" a ", 3u,
            100000u, &scan, error, sizeof(error)) &&
            cetta_parser_token_runtime_v1_parse_values(&restored, &scan, glr != 0u,
                100000u, 128u, 32u, &result, error, sizeof(error)) &&
            result.semantic_result_len == 2u && no_construction_or_audit(),
            error[0] ? error : "restored tokenized owner retains both production occurrences");
    }
    check(cetta_parser_token_runtime_v1_scan(&restored, (const uint8_t *)" a ", 3u,
        100000u, &scan, error, sizeof(error)) &&
        cetta_parser_token_runtime_v1_parse(&restored, &scan, false,
            100000u, 128u, 32u, &result, error, sizeof(error)) && result.forest_digest[0],
        "restored program remains compatible with explicit receipt operations");
done:
    if (error[0]) fprintf(stderr, "token restore: %s\n", error);
    free(bytes);
    arena_free(&metadata);
    ppnative_v1_result_free(&result);
    cetta_parser_token_scan_v1_free(&scan);
    cetta_parser_token_runtime_v1_free(&restored);
    cetta_ld_parser_pack_v1_free(&restored_compiled);
    pp_table_snapshot_v1_free(&decoded);
    pp_table_snapshot_v1_free(&snapshot);
}

static void tokenized(Arena *arena) {
    Atom *document = data(arena,
        "(LbnfDocumentV1 \"occurrences\""
        " (LCons (LbnfDefinitionV1 0 (LbnfRuleV1 \"Same\" \"S\" (LCons (LbnfTerminalV1 \"a\") LNil) False))"
        " (LCons (LbnfDefinitionV1 1 (LbnfRuleV1 \"Same\" \"S\" (LCons (LbnfTerminalV1 \"a\") LNil) False)) LNil)))");
    Atom *refusal = NULL;
    CettaLbnfLoweredV1 lowered = {0};
    CettaLdParserProfileV1 profile;
    CettaLdParserPackV1 compiled;
    CettaParserTokenRuntimeV1 runtime;
    CettaParserTokenScanV1 scan;
    PPNativeV1Result gll, glr;
    char error[512] = {0};
    cetta_ld_parser_profile_v1_init(&profile);
    cetta_ld_parser_pack_v1_init(&compiled);
    cetta_parser_token_runtime_v1_init(&runtime);
    cetta_parser_token_scan_v1_init(&scan);
    ppnative_v1_result_init(&gll);
    ppnative_v1_result_init(&glr);
    if (!check(document && cetta_lbnf_document_v1_lower(arena, document,
        atom_string(arena, "occurrences"), atom_string(arena, "S"),
        data(arena, "(SyntaxLexicalPolicyV1 bnfc-documented)"),
        4000000u, &lowered, &refusal) && !refusal &&
        compile_lowered(arena, &lowered, &profile, &compiled, error, sizeof(error)) &&
        cetta_parser_token_runtime_v1_prepare(&runtime, &compiled, profile.tokens,
            4000000u, error, sizeof(error)),
        error[0] ? error : "prepare tokenized duplicate occurrences")) goto done;
    token_program_restore(&runtime, &compiled, profile.tokens);
    reset_work();
    check(cetta_parser_token_runtime_v1_scan_values(&runtime,
        (const uint8_t *)" a ", 3u, 100000u, &scan, error, sizeof(error)) &&
        scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED &&
        cetta_parser_token_runtime_v1_parse_values(&runtime, &scan, false,
            100000u, 128u, 32u, &gll, error, sizeof(error)) &&
        cetta_parser_token_runtime_v1_parse_values(&runtime, &scan, true,
            100000u, 128u, 32u, &glr, error, sizeof(error)) &&
        gll.accepted && glr.accepted && gll.semantic_result_len == 2u &&
        glr.semantic_result_len == 2u &&
        atom_eq(gll.semantic_results[0], glr.semantic_results[0]) &&
        atom_eq(gll.semantic_results[1], glr.semantic_results[1]),
        error[0] ? error : "GLL and GLR keep tokenized production multiplicity");
    check(no_construction_or_audit() && !gll.forest_digest[0] && !glr.forest_digest[0] &&
        !scan.binding_sha256[0], "tokenized values path performs no SHA work");
done:
    ppnative_v1_result_free(&glr);
    ppnative_v1_result_free(&gll);
    cetta_parser_token_scan_v1_free(&scan);
    cetta_parser_token_runtime_v1_free(&runtime);
    cetta_ld_parser_pack_v1_free(&compiled);
    cetta_ld_parser_profile_v1_free(&profile);
}

static bool predictive_same_full_derivation(
    const CettaLpNativeUtf8Forest *a, const CettaLpNativeUtf8Forest *b) {
    if (a->root_len != 1u || !b->root_len) return false;
    /* Compare occurrence/span relations rather than incidental node order.
     * General parsing can retain additional incomplete prefix roots. */
    for (uint32_t i = 0u; i < a->choice_len; ++i) {
        const CettaLpNativeUtf8ForestChoice *choice = &a->choices[i];
        const CettaLpNativeUtf8ForestNode *parent = &a->nodes[choice->parent_node];
        bool found = false;
        for (uint32_t j = 0u; j < b->choice_len; ++j) {
            const CettaLpNativeUtf8ForestChoice *other = &b->choices[j];
            const CettaLpNativeUtf8ForestNode *node = &b->nodes[other->parent_node];
            if (choice->production_index == other->production_index &&
                choice->scalar_pivot == other->scalar_pivot &&
                parent->kind == node->kind && parent->symbol_id == node->symbol_id &&
                parent->production_index == node->production_index && parent->dot == node->dot &&
                parent->scalar_left == node->scalar_left && parent->scalar_right == node->scalar_right)
                found = true;
        }
        if (!found) return false;
    }
    return true;
}

static bool forest_accepts(const CettaLpNativeUtf8Forest *forest) {
    for (uint32_t i = 0u; i < forest->root_len; ++i)
        if (forest->nodes[forest->roots[i]].scalar_right == forest->scalar_len) return true;
    return false;
}

static void predictive(void) {
    CettaLpNativeGllPrepared prepared = {0};
    CettaLpNativeGllPrepared restored = {0};
    CettaLpNativeGllProgram stored_grammar = {0};
    CettaLpNativeGlrPrepared glr = {0};
    CettaLpNativePredictiveProgram program = {0};
    CettaLpNativePredictiveProgram imported = {0};
    Arena storage;
    arena_init(&storage);
    CettaLpNativeUtf8Forest result = {0}, general = {0}, bottom_up = {0};
    CettaLpNativeSymbol recursive[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 10u, 0u}, {CETTA_LP_NATIVE_SYMBOL_HL, 1u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 11u, 0u}};
    CettaLpNativeProduction productions[] = {
        {.label = 100u, .lhs = 1u, .rhs = recursive, .rhs_len = 3u},
        {.label = 100u, .lhs = 1u}};
    CettaLpNativeGrammar grammar = {.productions = productions, .production_len = 2u};
    CettaLpNativeUtf8Terminal terminals[] = {
        {.terminal_id = 10u, .kind = CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, .scalar = 'a'},
        {.terminal_id = 11u, .kind = CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, .scalar = 'b'},
        {.terminal_id = 12u, .kind = CETTA_LP_NATIVE_UTF8_TERMINAL_EOF}};
    char error[512] = {0};
    if (!check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_glr_prepare(&glr, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_ELIGIBLE,
        error[0] ? error : "generic predictive preparation reuses nullable/FIRST/FOLLOW")) goto done;
    const char *inputs[] = {"", "ab", "aabb", "aaabbb", "a", "b", "abb", "aab", "aacbb"};
    reset_work();
    bool agree = true, identity = true;
    for (uint32_t i = 0u; i < sizeof(inputs) / sizeof(*inputs); ++i) {
        size_t len = strlen(inputs[i]);
        error[0] = '\0';
        bool parsed = cetta_lp_native_predictive_parse_owned_utf8_forest(&prepared, &program,
            terminals, 3u, (const uint8_t *)inputs[i], len, 100000u, &result, error, sizeof(error)) &&
            cetta_lp_native_gll_prepared_parse_owned_utf8_forest_complete(&prepared, terminals, 3u,
                (const uint8_t *)inputs[i], len, 100000u, &general, error, sizeof(error)) &&
            cetta_lp_native_glr_prepared_parse_owned_utf8_forest(&glr, terminals, 3u,
                (const uint8_t *)inputs[i], len, 100000u, &bottom_up, error, sizeof(error));
        agree = agree && parsed && forest_accepts(&result) == forest_accepts(&general) &&
            forest_accepts(&result) == forest_accepts(&bottom_up);
        if (parsed && forest_accepts(&result)) identity = identity && predictive_same_full_derivation(&result, &general);
    }
    check(agree, error[0] ? error : "predictive, GLL and GLR agree on positive and negative inputs");
    check(identity, "predictive replay keeps production occurrences and spans independently of labels");
    check(no_construction_or_audit(), "generic predictive execution has zero construction, audits and SHA");
    check(result.root_len == 0u && result.farthest_scalar == 2u && result.expected_terminal_len,
        "predictive rejection retains its position and expectations");
    Atom *stored = cetta_lp_native_predictive_export_storage(&storage, &program);
    check(stored && cetta_lp_native_gll_prepared_export_program(&prepared, &stored_grammar,
        error, sizeof(error)), "freeze generic predictive decisions with their grammar binding");
    reset_work();
    check(stored && cetta_lp_native_predictive_import_storage(&imported, stored, &stored_grammar,
        error, sizeof(error)) && cetta_lp_native_gll_prepared_import_program(&restored, &stored_grammar,
        error, sizeof(error)) && no_construction_or_audit(),
        error[0] ? error : "restore predictive decisions without repeating eligibility analysis");
    check(cetta_lp_native_predictive_parse_owned_utf8_forest(&restored, &imported, terminals, 3u,
        (const uint8_t *)"aabb", 4u, 100000u, &result, error, sizeof(error)) && forest_accepts(&result) &&
        no_construction_or_audit(), "stored predictive program executes directly");
    reset_work();
    check(cetta_lp_native_predictive_check(&restored, &imported, terminals, 3u, error, sizeof(error)) &&
        grammar_audits && !table_builds && !grammar_compilations && !sha_calls,
        "explicit predictive audit checks decisions without constructing tables");
    uint32_t saved_selection = imported.decisions[0];
    imported.decisions[0] = saved_selection == 0u ? 1u : 0u;
    check(!cetta_lp_native_predictive_check(&restored, &imported, terminals, 3u, error, sizeof(error)),
        "checked prediction refuses a shape-compatible but wrong occurrence decision");
    imported.decisions[0] = saved_selection;
    uint64_t saved_first = stored_grammar.first[0];
    stored_grammar.first[0] ^= UINT64_C(1);
    CettaLpNativeGllPrepared altered_prediction = {0};
    check(cetta_lp_native_gll_prepared_import_program(&altered_prediction, &stored_grammar, error, sizeof(error)) &&
        !cetta_lp_native_gll_prepared_check_prediction(&altered_prediction, error, sizeof(error)),
        "checked restoration refuses an altered FIRST set rather than trusting a digest");
    cetta_lp_native_gll_prepared_free(&altered_prediction);
    stored_grammar.first[0] = saved_first;
    Atom *decision_vector = stored ? stored->expr.elems[9] : NULL;
    Atom *old_decision = decision_vector ? decision_vector->expr.elems[0] : NULL;
    if (decision_vector) decision_vector->expr.elems[0] = atom_int(&storage, program.production_len);
    check(decision_vector && !cetta_lp_native_predictive_import_storage(&imported, stored, &stored_grammar,
        error, sizeof(error)) && imported.status == CETTA_LP_NATIVE_PREDICTIVE_ELIGIBLE &&
        cetta_lp_native_predictive_parse_owned_utf8_forest(&restored, &imported, terminals, 3u,
            (const uint8_t *)"ab", 2u, 1000u, &result, error, sizeof(error)) && forest_accepts(&result),
        "malformed stored occurrence is refused atomically, leaving the previous program usable");
    if (decision_vector) decision_vector->expr.elems[0] = old_decision;
    check(cetta_lp_native_predictive_parse_owned_utf8_forest(&prepared, &program, terminals, 3u,
        (const uint8_t *)"aabb", 4u, 1u, &result, error, sizeof(error)) &&
        result.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT && !result.root_len,
        "predictive exhaustion is incomplete, not a rejection or fallback");
    uint32_t depth = 50000u;
    uint8_t *deep = malloc((size_t)depth * 2u);
    if (deep) { memset(deep, 'a', depth); memset(deep + depth, 'b', depth); }
    check(deep && cetta_lp_native_predictive_parse_owned_utf8_forest(&prepared, &program,
        terminals, 3u, deep, (size_t)depth * 2u, 500000u, &result, error, sizeof(error)) &&
        forest_accepts(&result) && result.stack_node_len == depth + 1u,
        error[0] ? error : "predictive execution is iterative at 50000 recursive levels");
    free(deep);

    /* Equal-valued declarations remain separate occurrence alternatives. */
    productions[1].rhs = recursive; productions[1].rhs_len = 3u;
    check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) &&
        program.status == CETTA_LP_NATIVE_PREDICTIVE_SELECT_CONFLICT &&
        program.conflict_productions[0] != program.conflict_productions[1],
        "text-identical productions with the same label are a predictive conflict");
    check(!cetta_lp_native_predictive_parse_owned_utf8_forest(&prepared, &program, terminals, 3u,
        (const uint8_t *)"ab", 2u, 100u, &result, error, sizeof(error)),
        "an ineligible predictive program never runs a general fallback");

    CettaLpNativeSymbol a[] = {{CETTA_LP_NATIVE_SYMBOL_TM, 10u, 0u}};
    CettaLpNativeSymbol b[] = {{CETTA_LP_NATIVE_SYMBOL_TM, 11u, 0u}};
    productions[0].rhs = a; productions[0].rhs_len = 1u;
    productions[1].rhs = b; productions[1].rhs_len = 1u;
    CettaLpNativeUnicodeRange overlap = {'a', 'z'};
    terminals[1].kind = CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES;
    terminals[1].ranges = &overlap; terminals[1].range_len = 1u;
    check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_SELECT_CONFLICT &&
        program.conflict_scalar == 'a' && program.conflict_terminals[0] != program.conflict_terminals[1],
        "scannerless overlap is detected across differently named terminal classes");
    check(cetta_lp_native_predictive_build(&prepared, NULL, 0u, true, 12u,
        &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_ELIGIBLE,
        "token-chain selection does not impose scannerless overlap semantics");
    uint32_t codepoint = 'x', byte_offsets[] = {0u, 1u}, offsets[] = {0u, 1u, 1u};
    uint32_t ids[] = {10u, 11u};
    CettaLpNativeUtf8LatticeEdge edge = {.terminal_id = 11u, .scalar_left = 0u,
        .scalar_right = 1u, .byte_left = 0u, .byte_right = 1u,
        .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, .value = 7u};
    CettaLpNativeUtf8Lattice lattice = {.terminal_ids = ids, .terminal_len = 2u,
        .edges = &edge, .edge_len = 1u, .start_offsets = offsets, .start_offset_len = 3u,
        .codepoints = &codepoint, .byte_offsets = byte_offsets, .scalar_len = 1u, .input_byte_len = 1u};
    reset_work();
    check(cetta_lp_native_predictive_parse_owned_lattice_forest(&prepared, &program, &lattice,
        100u, &result, error, sizeof(error)) && forest_accepts(&result) &&
        result.choice_len == 1u && result.choices[0].production_index == 1u && no_construction_or_audit(),
        "predictive token execution retains the selected lexer token and production occurrence");

    CettaLpNativeSymbol left[] = {{CETTA_LP_NATIVE_SYMBOL_HL, 1u, 0u}};
    productions[0].rhs = left; productions[0].rhs_len = 1u;
    productions[1].rhs = NULL; productions[1].rhs_len = 0u;
    check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_ZERO_PROGRESS_CYCLE &&
        program.cycle_len == 1u && program.cycle_productions[0] == 0u,
        "nullable left recursion has an occurrence-bearing cycle witness");
    CettaLpNativeSymbol eof_cycle[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 12u, 0u}, {CETTA_LP_NATIVE_SYMBOL_HL, 1u, 0u}};
    productions[0].rhs = eof_cycle; productions[0].rhs_len = 2u;
    productions[1].rhs = a; productions[1].rhs_len = 1u;
    check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_ZERO_PROGRESS_CYCLE,
        "EOF is zero progress, not a licence for infinite predictive recursion");
    stored = cetta_lp_native_predictive_export_storage(&storage, &program);
    check(stored && cetta_lp_native_gll_prepared_export_program(&prepared, &stored_grammar,
        error, sizeof(error)) && cetta_lp_native_predictive_import_storage(&imported, stored, &stored_grammar,
        error, sizeof(error)) && imported.status == CETTA_LP_NATIVE_PREDICTIVE_ZERO_PROGRESS_CYCLE &&
        imported.cycle_len == 1u && imported.cycle_productions[0] == 0u,
        "artifacts retain the refusal's cycle witness rather than recomputing it on load");
    CettaLpNativeSymbol eof[] = {{CETTA_LP_NATIVE_SYMBOL_TM, 12u, 0u}};
    productions[0].rhs = eof; productions[0].rhs_len = 1u;
    productions[1].rhs = NULL; productions[1].rhs_len = 0u;
    check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_SELECT_CONFLICT &&
        program.conflict_scalar == UINT32_MAX,
        "explicit EOF and synthetic end collide rather than silently select an occurrence");
    CettaLpNativeSymbol followed[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, 2u, 0u}, {CETTA_LP_NATIVE_SYMBOL_TM, 10u, 0u}};
    CettaLpNativeProduction follow_conflict[] = {
        {.label = 101u, .lhs = 1u, .rhs = followed, .rhs_len = 2u},
        {.label = 102u, .lhs = 2u, .rhs = a, .rhs_len = 1u},
        {.label = 103u, .lhs = 2u}};
    grammar.productions = follow_conflict; grammar.production_len = 3u;
    check(cetta_lp_native_gll_prepare(&prepared, &grammar, 1u, error, sizeof(error)) &&
        cetta_lp_native_predictive_build(&prepared, terminals, 3u, false, UINT32_MAX,
            &program, error, sizeof(error)) && program.status == CETTA_LP_NATIVE_PREDICTIVE_SELECT_CONFLICT &&
        program.conflict_nonterminal == 2u && program.conflict_productions[0] == 1u &&
        program.conflict_productions[1] == 2u,
        "FIRST/FOLLOW conflict names the empty and consuming production occurrences");
done:
    arena_free(&storage);
    cetta_lp_native_predictive_program_free(&program);
    cetta_lp_native_predictive_program_free(&imported);
    cetta_lp_native_gll_prepared_free(&restored);
    cetta_lp_native_gll_program_free(&stored_grammar);
    cetta_lp_native_utf8_forest_free(&result);
    cetta_lp_native_utf8_forest_free(&general);
    cetta_lp_native_utf8_forest_free(&bottom_up);
    cetta_lp_native_gll_prepared_free(&prepared);
    cetta_lp_native_glr_prepared_free(&glr);
}

static void deep_ast(Arena *arena) {
    CettaParserTokenProfileV1 profile = {0};
    profile.productions = data(arena,
        "(LCons (ParserProductionMetadataV1 0 ParserVisibleV1 (BnfcConstructorV1 \"Wrap\" \"S\" (LCons \"S\" LNil)))"
        " (LCons (ParserProductionMetadataV1 1 ParserVisibleV1 (BnfcConstructorV1 \"End\" \"S\" LNil))"
        " (LCons (ParserProductionMetadataV1 2 ParserVisibleV1 (BnfcListEmptyV1 \"[S]\"))"
        " (LCons (ParserProductionMetadataV1 3 ParserVisibleV1 (BnfcListConsV1 \"[S]\"))"
        " (LCons (ParserProductionMetadataV1 4 ParserVisibleV1 (BnfcCoercionV1 \"[S]\")) LNil)))))");
    CettaLpNativeUtf8ScalarBuffer source = {0};
    char error[512] = {0};
    uint32_t depth = 50000u;
    CettaLdTextV1 start = {.bytes = (uint8_t *)"S", .len = 1u};
    CettaParserTokenAstOutcomeV1 outcome;
    Atom *end = data(arena, "(CstRuleV1 \"End\" 0 1)"), *cst = end, *ast = NULL;
    Atom *wrap[] = {atom_symbol(arena, "CstRuleV1"), atom_string(arena, "Wrap"),
        atom_int(arena, 0), atom_int(arena, 1), NULL};
    for (uint32_t i = 0u; i < depth; ++i) {
        wrap[4] = cst; cst = atom_expr(arena, wrap, 5u);
    }
    bool projected = cetta_lp_native_utf8_scalar_buffer_decode(&source, (const uint8_t *)"x", 1u,
        error, sizeof(error)) && cetta_parser_token_ast_v1_project_owned_outcome(arena, &profile,
        &start, &source.view, cst, depth + 1u, depth + 1u, &ast, &outcome, error, sizeof(error));
    check(projected && outcome == CETTA_PARSER_TOKEN_AST_COMPLETED_V1,
        error[0] ? error : "labelled AST projection is iterative at 50000 nested constructors");
    Atom *node = ast;
    uint32_t seen = 0u;
    while (node && node->kind == ATOM_EXPR && node->expr.len == 5u &&
        atom_is_symbol(node->expr.elems[0], "LbnfAstNodeV1") &&
        !strcmp(node->expr.elems[2]->ground.sval, "Wrap")) {
        Atom *children = node->expr.elems[4];
        if (!children || children->kind != ATOM_EXPR || children->expr.len != 3u ||
            !atom_is_symbol(children->expr.elems[0], "LCons") ||
            !atom_is_symbol(children->expr.elems[2], "LNil")) break;
        node = children->expr.elems[1]; ++seen;
    }
    check(seen == depth && node && !strcmp(node->expr.elems[2]->ground.sval, "End"),
        "iterative projection preserves every nested constructor and child role");
    Atom *previous = ast;
    check(!cetta_parser_token_ast_v1_project_owned_outcome(arena, &profile, &start, &source.view,
        cst, depth + 1u, 10u, &ast, &outcome, error, sizeof(error)) && ast == previous &&
        outcome == CETTA_PARSER_TOKEN_AST_INCOMPLETE_V1,
        "projection exhaustion is typed incomplete and leaves the previous output intact");
    CettaLdTextV1 list_start = {.bytes = (uint8_t *)"[S]", .len = 3u};
    Atom *list = data(arena, "(CstRuleV1 \"_\" 0 1 (CstRuleV1 \"[]\" 0 1))");
    Atom *cons[] = {atom_symbol(arena, "CstRuleV1"), atom_string(arena, "(:)"),
        atom_int(arena, 0), atom_int(arena, 1), end, NULL};
    for (uint32_t i = 0u; i < depth; ++i) {
        cons[5] = list; list = atom_expr(arena, cons, 6u);
    }
    projected = cetta_parser_token_ast_v1_project_owned_outcome(arena, &profile, &list_start,
        &source.view, list, 10u, depth * 2u + 10u, &ast, &outcome, error, sizeof(error));
    Atom *items = projected ? ast->expr.elems[3] : NULL;
    seen = 0u;
    while (items && items->kind == ATOM_EXPR && items->expr.len == 3u &&
        atom_is_symbol(items->expr.elems[0], "LCons")) { ++seen; items = items->expr.elems[2]; }
    check(projected && seen == depth && atom_is_symbol(items, "LNil"),
        error[0] ? error : "long lists retain their spine and coercion tail without consuming nesting depth");
    Atom *malformed = data(arena, "(CstRuleV1 \"Wrap\" 0 2 (CstRuleV1 \"End\" 0 1))");
    check(!cetta_parser_token_ast_v1_project_owned_outcome(arena, &profile, &start, &source.view,
        malformed, 10u, 100u, &ast, &outcome, error, sizeof(error)) &&
        outcome == CETTA_PARSER_TOKEN_AST_INVALID_V1,
        "an invalid source span remains an invalid projection, not resource exhaustion");
    cetta_lp_native_utf8_scalar_buffer_free(&source);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    scannerless(&arena);
    tokenized(&arena);
    predictive();
    deep_ast(&arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(ParserValuesV1Summary %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
