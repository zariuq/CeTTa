#include "native/deterministic_equation_plan_v1.h"
#include "native/language_def_parser_pack_v1.h"
#include "native/parser_token_profile_v1.h"
#include "native/parser_token_runtime_v1.h"
#include "native/regular_token_compiler_v1.h"
#include "native/parser_token_ast_v1.h"
#include "native/lbnf_document_v1.h"
#include "parser.h"
#include "finite_horn_ground_term_v1.h"
#include "regular_span_nfa_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Atom *term; char *text; } Answer;

static bool form(const Atom *value, const char *name, uint32_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
        atom_is_symbol(value->expr.elems[0], name);
}

static bool word(const Atom *value, const char *name) {
    return value && atom_is_symbol((Atom *)value, name);
}

static bool reference(const Atom *regex, const uint8_t *text, size_t left,
                      size_t right, size_t input_len) {
    if (word(regex, "rx-eps")) return left == right;
    if (word(regex, "rx-empty")) return false;
    if (word(regex, "rx-eof")) return left == right && right == input_len;
    if (word(regex, "rx-any")) return right == left + 1u;
    if (form(regex, "rx-char", 1u)) {
        Atom *cp = regex->expr.elems[1];
        return form(cp, "cp", 1u) && cp->expr.elems[1]->kind == ATOM_GROUNDED &&
            cp->expr.elems[1]->ground.gkind == GV_INT && right == left + 1u &&
            text[left] == cp->expr.elems[1]->ground.ival;
    }
    if (form(regex, "rx-range", 2u)) {
        return right == left + 1u && text[left] >= regex->expr.elems[1]->expr.elems[1]->ground.ival &&
            text[left] <= regex->expr.elems[2]->expr.elems[1]->ground.ival;
    }
    if (form(regex, "rx-minus", 2u))
        return reference(regex->expr.elems[1], text, left, right, input_len) &&
            !reference(regex->expr.elems[2], text, left, right, input_len);
    if (form(regex, "rx-class", 1u)) {
        return right == left + 1u &&
            (word(regex->expr.elems[1], "letter") ? text[left] == 'a' : text[left] != '\n');
    }
    if (form(regex, "rx-alt", 2u))
        return reference(regex->expr.elems[1], text, left, right, input_len) ||
            reference(regex->expr.elems[2], text, left, right, input_len);
    if (form(regex, "rx-seq", 2u)) {
        for (size_t split = left; split <= right; ++split)
            if (reference(regex->expr.elems[1], text, left, split, input_len) &&
                reference(regex->expr.elems[2], text, split, right, input_len)) return true;
        return false;
    }
    if (form(regex, "rx-star", 1u)) {
        if (left == right) return true;
        for (size_t split = left + 1u; split <= right; ++split)
            if (reference(regex->expr.elems[1], text, left, split, input_len) &&
                reference(regex, text, split, right, input_len)) return true;
    }
    return false;
}

static int compare_answer(const void *left, const void *right) {
    return strcmp(((const Answer *)left)->text, ((const Answer *)right)->text);
}

static Atom *data(Arena *arena, const char *source) {
    Atom **values = NULL;
    int count = parse_metta_text(source, arena, &values);
    return count == 1 ? values[0] : NULL;
}

static bool compile_lowered(Arena *arena, const CettaLbnfLoweredV1 *lowered,
    CettaLdParserProfileV1 *profile, CettaLdParserPackV1 *compiled, char *error, size_t size) {
    CettaOperationalLanguageDefV1 wire;
    CettaOpLangV1Document document;
    CettaLanguageDefCoreV1 core;
    CettaOpLangV1Status ingress;
    CettaLdCoreV1Status core_status;
    CettaLdParserPackV1Status status;
    bool ok = false;
    cetta_op_lang_v1_init(&wire);
    cetta_op_lang_v1_document_init(&document);
    cetta_language_def_core_v1_init(&core);
    char *language = atom_to_parseable_string(arena, lowered->language);
    char *parser = atom_to_parseable_string(arena, lowered->profile);
    if (language && parser &&
        cetta_op_lang_v1_parse_bytes(&wire, (const uint8_t *)language, strlen(language),
            4000000u, 4000000u, &ingress, error, size) &&
        cetta_language_def_core_v1_decode(&core, &wire, 1000000u, &core_status, error, size) &&
        cetta_op_lang_v1_parse_document_bytes(&document, (const uint8_t *)parser, strlen(parser),
            4000000u, 4000000u, &ingress, error, size) &&
        cetta_ld_parser_profile_v1_decode(profile, &document, 1000000u, &status, error, size) &&
        cetta_language_def_parser_pack_v1_compile(compiled, &core, wire.authority_sha256,
            profile, 4000000u, &status, error, size)) ok = true;
    cetta_language_def_core_v1_free(&core);
    cetta_op_lang_v1_document_free(&document);
    cetta_op_lang_v1_free(&wire);
    return ok;
}

static bool check_counted_scalar_text(char *error, size_t size) {
    static const struct { const char *text; bool accepted; } cases[] = {
        {"(bnf-v1:text-cons 0 (bnf-v1:text-cons 128512 (bnf-v1:text-nil)))", true},
        {"(bnf-v1:text-nil)", true},
        {"(bnf-v1:text-cons 55296 (bnf-v1:text-nil))", false},
        {"(bnf-v1:text-cons 1114112 (bnf-v1:text-nil))", false},
        {"(bnf-v1:text-cons 999999999999999999999999 (bnf-v1:text-nil))", false},
        {"(bnf-v1:text-cons -1 (bnf-v1:text-nil))", false},
        {"(bnf-v1:text-cons 120 WrongTail)", false},
        {"(bnf-v1:text-cons 120 (bnf-v1:text-nil 1))", false},
    };
    CettaLanguageDefCoreV1 core;
    cetta_language_def_core_v1_init(&core);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(*cases); ++i) {
        char source[2048];
        snprintf(source, sizeof(source),
            "(GSLTLanguageDefWireV1 \"ScalarText\" (LCons (TypeDecl \"E\" CarrierAst) LNil)"
            " (LCons (GrammarRule \"X\" \"E\" LNil (LCons (SyntaxTerminal %s) LNil) EvalNone) LNil) LNil LNil)",
            cases[i].text);
        CettaOperationalLanguageDefV1 wire;
        CettaOpLangV1Status ingress;
        CettaLdCoreV1Status status;
        cetta_op_lang_v1_init(&wire);
        uint8_t *prior = core.name.bytes;
        bool pass = cetta_op_lang_v1_parse_bytes(&wire, (const uint8_t *)source, strlen(source),
            1000000u, 1000000u, &ingress, error, size);
        bool decoded = pass && cetta_language_def_core_v1_decode(&core, &wire, 100000u,
            &status, error, size);
        pass = pass && decoded == cases[i].accepted;
        if (pass && !decoded) pass = core.name.bytes == prior;
        if (pass && decoded) {
            const CettaLdTextV1 *text = &core.terms[0].syntax_pattern.items[0].as.text;
            static const uint8_t expected[] = {0u, 0xf0u, 0x9fu, 0x98u, 0x80u};
            pass = i ? text->len == 0u : text->len == sizeof(expected) &&
                !memcmp(text->bytes, expected, sizeof(expected));
        }
        cetta_op_lang_v1_free(&wire);
        if (!pass) { cetta_language_def_core_v1_free(&core); return false; }
    }
    cetta_language_def_core_v1_free(&core);
    printf("(LanguageDefCountedScalarTextV1Summary %zu 0)\n", sizeof(cases) / sizeof(*cases));
    return true;
}

static bool check_lbnf_document(Arena *arena, char *error, size_t size) {
    static const struct { const char *source; const char *reason; } cases[] = {
        {"", "empty-grammar"},
        {"-- Only a comment.\n", "empty-grammar"},
        {"X. E ::= \"x\";", NULL},
        {"X. E ::= \"x\"; separator E \",\"; Root. Root ::= [E]; entrypoints Root;", NULL},
        {"X. E ::= \"x\"; X. E ::= \"y\";", NULL},
        {"Root. Root ::= Word; position token Word letter+;", NULL},
        {"A. S ::= T; B. T ::= S; token Id letter;", "no-terminals"},
        {"_. Foo ::= \"later\" Foo;", "empty-abstract-syntax"},
        {"Root. Root ::= Missing;", "missing-production"},
        {"(:). [Seq] ::= Integer [Seq]; Seq. Seq ::= \"x\";", "bad-list-construction"},
        {"Root. Root ::= \"x\"; token X 'x'?;", "nullable-token"},
        {"Root. Root ::= \"x\"; token X 'x' - 'x';", "empty-token"},
        {"Root. Root ::= \"x\"; comment \"\";", "empty-comment-delimiter"},
        {"undefined. E ::= \"x\";", "undefined-rule-label"},
        {"Foo. Foo ::= \"x\"; Foo. Bar ::= \"y\";", "constructor-signature-conflict"},
        {"Root. Root ::= \"x\"; layout toplevel;", "layout"},
        {"Root. Root ::= \"x\"; define f x = x;", "define"},
    };
    Atom *bootstrap = NULL, *refusal = NULL;
    Atom *policy = data(arena, "(SyntaxLexicalPolicyV1 bnfc-documented)");
    CettaLbnfLoweredV1 reader = {0};
    CettaLdParserProfileV1 profile;
    CettaLdParserPackV1 compiled;
    CettaParserTokenRuntimeV1 runtime;
    cetta_ld_parser_profile_v1_init(&profile);
    cetta_ld_parser_pack_v1_init(&compiled);
    cetta_parser_token_runtime_v1_init(&runtime);
    bool ok = false;
    size_t finished_checks = 0u;
    if (!cetta_lbnf_document_v1_bootstrap(arena, &bootstrap, error, size) ||
        !cetta_lbnf_document_v1_lower(arena, bootstrap, atom_string(arena, "LbnfReaderV1"),
            atom_string(arena, "Grammar"), policy, 4000000u, &reader, &refusal) ||
        !compile_lowered(arena, &reader, &profile, &compiled, error, size) ||
        !cetta_parser_token_runtime_v1_prepare(&runtime, &compiled, profile.tokens,
            4000000u, error, size)) goto done;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(*cases); ++i) {
        CettaParserTokenScanV1 scan;
        PPNativeV1Result gll, glr;
        cetta_parser_token_scan_v1_init(&scan);
        ppnative_v1_result_init(&gll); ppnative_v1_result_init(&glr);
        Atom *ast = NULL, *document = NULL;
        CettaLbnfLoweredV1 result = {atom_symbol(arena, "unchanged"), NULL, NULL, NULL};
        bool pass = cetta_parser_token_runtime_v1_scan(&runtime,
                (const uint8_t *)cases[i].source, strlen(cases[i].source), 4000000u,
                &scan, error, size) && scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED &&
            cetta_parser_token_runtime_v1_parse(&runtime, &scan, false, 4000000u,
                4096u, 128u, &gll, error, size) &&
            cetta_parser_token_runtime_v1_parse(&runtime, &scan, true, 4000000u,
                4096u, 128u, &glr, error, size) && gll.accepted && glr.accepted &&
            gll.semantic_result_len == 1u && glr.semantic_result_len == 1u &&
            atom_eq(gll.semantic_results[0], glr.semantic_results[0]) &&
            !strcmp(gll.forest_digest, glr.forest_digest) &&
            cetta_parser_token_runtime_v1_materialize(&runtime, &scan, &gll, 100000u, error, size) &&
            cetta_parser_token_runtime_v1_materialize(&runtime, &scan, &glr, 100000u, error, size) &&
            atom_eq(gll.canonical_forest, glr.canonical_forest) &&
            form(gll.semantic_results[0], "result", 2u) &&
            cetta_parser_token_ast_v1_project(arena, profile.tokens, &profile.start_sort,
                &scan.source.view, gll.semantic_results[0]->expr.elems[1], 4096u, 4000000u, &ast, error, size) &&
            cetta_lbnf_document_v1_project(arena, atom_string(arena, "test"), ast,
                4000000u, &document, &refusal);
        if (pass) {
            Atom *asts = atom_expr(arena, &ast, 1u);
            for (uint32_t evidence = 0u; evidence < 2u && pass; ++evidence) {
                Atom *elements[] = {atom_symbol(arena, evidence
                    ? "LangDef:AuthoredParseEvidenceAccepted" : "LangDef:AuthoredParseAccepted"),
                    atom_string(arena, "reader"), atom_string(arena, "binding"),
                    atom_string(arena, "test"), evidence ? atom_string(arena, "digest") : asts,
                    gll.canonical_forest, asts};
                Atom *accepted = atom_expr(arena, elements, evidence ? 7u : 5u);
                Atom *finished = NULL, *finished_refusal = NULL;
                pass = cetta_lbnf_document_v1_finish_read(arena, accepted, 4000000u,
                    &finished, &finished_refusal) && !finished_refusal &&
                    form(finished, evidence ? "LbnfReadEvidenceV1" : "LbnfReadValueV1",
                        evidence ? 5u : 3u) &&
                    atom_eq(finished->expr.elems[evidence ? 5u : 3u], document) &&
                    atom_eq(finished->expr.elems[1], elements[2]) &&
                    atom_eq(finished->expr.elems[2], elements[3]) &&
                    (!evidence || (atom_eq(finished->expr.elems[3], elements[4]) &&
                        atom_eq(finished->expr.elems[4], gll.canonical_forest)));
                if (pass) ++finished_checks;
                if (i == 0u && evidence == 1u && pass) {
                    Atom *twice[] = {ast, ast};
                    elements[6] = atom_expr(arena, twice, 2u);
                    accepted = atom_expr(arena, elements, 7u);
                    pass = cetta_lbnf_document_v1_finish_read(arena, accepted, 4000000u,
                        &finished, &finished_refusal) && atom_eq(finished, accepted);
                    if (pass) ++finished_checks;
                    Atom *bad = atom_symbol(arena, "not-a-labelled-grammar");
                    elements[6] = atom_expr(arena, &bad, 1u);
                    accepted = atom_expr(arena, elements, 7u);
                    Atom *sentinel = atom_symbol(arena, "unchanged");
                    finished = sentinel; finished_refusal = NULL;
                    pass = pass && !cetta_lbnf_document_v1_finish_read(arena, accepted,
                        4000000u, &finished, &finished_refusal) && finished == sentinel &&
                        form(finished_refusal, "LbnfRefusedV1", 3u);
                    if (pass) ++finished_checks;
                }
            }
        }
        if (pass) {
            bool lowered = cetta_lbnf_document_v1_lower(arena, document, atom_string(arena, "test"),
                atom_symbol(arena, "LbnfDefaultStartV1"), policy, 4000000u, &result, &refusal);
            pass = cases[i].reason ? !lowered && form(refusal, "LbnfRefusedV1", 3u) &&
                !strcmp(atom_name_cstr(refusal->expr.elems[1]), cases[i].reason) &&
                form(refusal->expr.elems[2], "LbnfSourceSpanV1", 4u) &&
                word(result.language, "unchanged") : lowered;
            if (pass && cases[i].reason && !strcmp(cases[i].reason, "empty-grammar"))
                pass = atom_eq(refusal->expr.elems[2],
                    data(arena, "(LbnfSourceSpanV1 0 0 1 1)"));
        }
        ppnative_v1_result_free(&glr); ppnative_v1_result_free(&gll);
        cetta_parser_token_scan_v1_free(&scan);
        if (!pass) {
            fprintf(stderr, "LBNF document control %zu: %s\n", i, cases[i].source);
            if (refusal) { atom_print(refusal, stderr); fputc('\n', stderr); }
            goto done;
        }
    }
    {
        /* A long right-recursive list fits the linear default budget: a
         * completion is decided by the next non-layout token, so the
         * definitions do not each complete the list early and cascade. */
        enum { DEFINITIONS = 3000u };
        static const char rule[] = "X. E ::= \"x\";\n";
        size_t length = (sizeof(rule) - 1u) * DEFINITIONS;
        char *source = malloc(length + 1u);
        CettaParserTokenScanV1 scan;
        PPNativeV1Result gll, glr;
        cetta_parser_token_scan_v1_init(&scan);
        ppnative_v1_result_init(&gll); ppnative_v1_result_init(&glr);
        for (size_t i = 0u; source && i < DEFINITIONS; ++i)
            memcpy(source + i * (sizeof(rule) - 1u), rule, sizeof(rule) - 1u);
        if (source) source[length] = '\0';
        bool pass = source && cetta_parser_token_runtime_v1_scan(&runtime,
                (const uint8_t *)source, length, 4000000u, &scan, error, size) &&
            scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED &&
            cetta_parser_token_runtime_v1_parse(&runtime, &scan, false, 4000000u,
                4096u, 128u, &gll, error, size) &&
            cetta_parser_token_runtime_v1_parse(&runtime, &scan, true, 4000000u,
                4096u, 128u, &glr, error, size) &&
            gll.outcome == PPNATIVE_V1_COMPLETED && glr.outcome == PPNATIVE_V1_COMPLETED &&
            gll.accepted && glr.accepted && !strcmp(gll.forest_digest, glr.forest_digest);
        if (!pass)
            fprintf(stderr, "LBNF long list: GLL=%u/%u GLR=%u/%u %s %s\n",
                gll.outcome, gll.accepted, glr.outcome, glr.accepted, gll.detail, glr.detail);
        ppnative_v1_result_free(&glr); ppnative_v1_result_free(&gll);
        cetta_parser_token_scan_v1_free(&scan);
        free(source);
        if (!pass) goto done;
    }
    printf("(LbnfAuthoredReaderLoweringV1Summary %zu 0)\n", sizeof(cases) / sizeof(*cases) + 1u);
    printf("(LbnfReaderResultDataV1Summary %zu 0)\n", finished_checks);
    ok = true;
done:
    cetta_parser_token_runtime_v1_free(&runtime);
    cetta_ld_parser_pack_v1_free(&compiled);
    cetta_ld_parser_profile_v1_free(&profile);
    return ok;
}

static bool check_token_profile(Arena *arena, char *error, size_t size) {
    static const char source[] =
        "(GSLTParserProfileLayerV2 \"sample\" \"Root\" LNil LNil"
        " (ParserTokenLayerV1 (SyntaxLexicalPolicyV1 bnfc-documented)"
        " (LCons (RegularTokenDeclV1 \"Word\" (token Word) (rx-char (cp 97))"
        " TokenTextV1 (SyntaxOriginV1 \"source\" 1))"
        " (LCons (RegularTriviaDeclV1 layout"
        " (rx-seq (rx-char (cp 32)) (rx-star (rx-char (cp 32))))"
        " (SyntaxOriginV1 \"source\" 5)) LNil))"
        " (LCons (ParserBlockCommentV1 \"{-\" \"-}\" (SyntaxOriginV1 \"source\" 2)) LNil)"
        " (LCons (ParserProductionMetadataV1 (site source 3) ParserVisibleV1"
        " (BnfcConstructorV1 \"Wrap\" \"Root\" (LCons \"Word\" LNil)))"
        " (LCons (ParserProductionMetadataV1 (site source 4) ParserVisibleV1"
        " (BnfcConstructorV1 \"Wrap\" \"Root\" (LCons \"Word\" LNil))) LNil))"
        " (LCons \"Root\" LNil)))";
    static const char other[] =
        "(ParserTokenLayerV1 (SyntaxLexicalPolicyV1 bnfc-2.9.6.3-c-flex-2.6.4)"
        " LNil LNil LNil LNil)";
    static const char bad[] =
        "(ParserTokenLayerV1 (SyntaxLexicalPolicyV1 imaginary-policy) LNil LNil LNil LNil)";
    CettaOpLangV1Document document;
    CettaOpLangV1Status ingress;
    CettaLdParserPackV1Status status;
    CettaLdParserProfileV1 profile;
    CettaParserTokenProfileV1 direct;
    bool ok = false;
    (void)arena;
    cetta_op_lang_v1_document_init(&document);
    cetta_ld_parser_profile_v1_init(&profile);
    cetta_parser_token_profile_v1_init(&direct);
    if (!cetta_op_lang_v1_parse_document_bytes(&document, (const uint8_t *)source,
            sizeof(source) - 1u, 1000000u, 1000000u, &ingress, error, size) ||
        !cetta_ld_parser_profile_v1_decode(&profile, &document, 100000u,
            &status, error, size) || !profile.tokens ||
        profile.tokens->policy_kind != CETTA_BNFC_DOCUMENTED_V1 ||
        profile.tokens->declaration_len != 2u || profile.tokens->comment_len != 1u ||
        profile.tokens->production_len != 2u || profile.tokens->entrypoint_len != 1u)
        goto done;
    cetta_op_lang_v1_document_free(&document);
    if (!form(profile.tokens->document, "ParserTokenLayerV1", 5u) ||
        !arena_owns_ptr(&profile.tokens->arena, profile.tokens->document)) {
        snprintf(error, size, "regular-token profile borrowed its ingress document");
        goto done;
    }
    {
        static const char language_source[] =
            "(GSLTLanguageDefWireV1 \"sample\""
            " (LCons (TypeDecl \"Root\" CarrierAst) (LCons (TypeDecl \"Word\" CarrierTokenRaw) LNil))"
            " (LCons (GrammarRule \"Wrap\" \"Root\" (LCons (TermSimple \"x\" (TBase \"Word\")) LNil)"
            " (LCons (SyntaxTerminal \"word\") (LCons (SyntaxNonTerminal \"x\") LNil)) EvalNone)"
            " (LCons (GrammarRule \"Wrap\" \"Root\" (LCons (TermSimple \"x\" (TBase \"Word\")) LNil)"
            " (LCons (SyntaxTerminal \"word\") (LCons (SyntaxNonTerminal \"x\") LNil)) EvalNone) LNil)) LNil LNil)";
        CettaOperationalLanguageDefV1 wire;
        CettaLanguageDefCoreV1 language;
        CettaLdParserPackV1 compiled;
        CettaLdCoreV1Status core_status;
        bool compiled_ok = false;
        cetta_op_lang_v1_init(&wire);
        cetta_language_def_core_v1_init(&language);
        cetta_ld_parser_pack_v1_init(&compiled);
        if (cetta_op_lang_v1_parse_bytes(&wire, (const uint8_t *)language_source,
                sizeof(language_source) - 1u, 1000000u, 1000000u, &ingress, error, size) &&
            cetta_language_def_core_v1_decode(&language, &wire, 100000u,
                &core_status, error, size) &&
            cetta_language_def_parser_pack_v1_compile(&compiled, &language,
                wire.authority_sha256, &profile, 1000000u, &status, error, size) &&
            compiled.tokenized && compiled.pack.production_len == 5u &&
            compiled.token_bindings.extension.production_len == 3u &&
            ppnative_v1_start_is_closed_extended(&compiled.pack, compiled.start_state,
                &compiled.token_bindings.extension, error, size)) {
            uint32_t occurrences = 0u;
            for (uint32_t i = 0u; i < compiled.pack.production_len; ++i)
                if (form(compiled.pack.productions[i].identity->expr.elems[1],
                         "pp-occurrence", 1u)) ++occurrences;
            compiled_ok = occurrences == 2u &&
                language.terms[0].label.len == 4u &&
                !memcmp(language.terms[0].label.bytes, "Wrap", 4u);
            CettaParserTokenRuntimeV1 runtime;
            CettaParserTokenScanV1 scan;
            PPNativeV1Result gll, glr;
            cetta_parser_token_runtime_v1_init(&runtime);
            cetta_parser_token_scan_v1_init(&scan);
            ppnative_v1_result_init(&gll);
            ppnative_v1_result_init(&glr);
            static const char input[] = " worda ";
            compiled_ok = compiled_ok &&
                cetta_parser_token_runtime_v1_prepare(&runtime, &compiled, profile.tokens,
                    1000000u, error, size) &&
                cetta_parser_token_runtime_v1_scan(&runtime, (const uint8_t *)input,
                    sizeof(input) - 1u, 1000000u, &scan, error, size) &&
                scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED &&
                scan.lattice.source_pass_count == 1u && scan.lattice.edge_len == 5u &&
                cetta_parser_token_runtime_v1_parse(&runtime, &scan, false,
                    1000000u, 512u, 128u, &gll, error, size) &&
                cetta_parser_token_runtime_v1_parse(&runtime, &scan, true,
                    1000000u, 512u, 128u, &glr, error, size) &&
                gll.accepted && glr.accepted && gll.semantic_result_len == 1u &&
                glr.semantic_result_len == 1u &&
                atom_eq(gll.semantic_results[0], glr.semantic_results[0]) &&
                strcmp(gll.forest_digest, glr.forest_digest) == 0 &&
                cetta_parser_token_runtime_v1_materialize(&runtime, &scan, &gll,
                    100000u, error, size) &&
                cetta_parser_token_runtime_v1_materialize(&runtime, &scan, &glr,
                    100000u, error, size) &&
                atom_eq(gll.canonical_forest, glr.canonical_forest);
            uint32_t seen = 0u;
            if (compiled_ok) {
                uint64_t identities = 0u;
                for (uint32_t c = 0u; c < gll.forest.choice_len; ++c) {
                    uint32_t id = gll.forest.choices[c].production_index;
                    if (id < compiled.pack.production_len &&
                        form(compiled.pack.productions[id].identity->expr.elems[1],
                            "pp-occurrence", 1u)) identities |= UINT64_C(1) << id;
                }
                for (; identities; identities &= identities - 1u) ++seen;
                compiled_ok = seen == 2u;
            }
            if (!compiled_ok) {
                fprintf(stderr, "token runtime: scan=%u edges=%u; GLL=%u/%u/%u GLR=%u/%u/%u occurrences=%u; %s\n",
                    scan.outcome, scan.lattice.edge_len, gll.outcome, gll.accepted, gll.semantic_result_len,
                    glr.outcome, glr.accepted, glr.semantic_result_len, seen, error);
            }
            PPNativeV1Result exhausted_gll, exhausted_glr;
            ppnative_v1_result_init(&exhausted_gll);
            ppnative_v1_result_init(&exhausted_glr);
            bool exhausted_ok = compiled_ok &&
                cetta_parser_token_runtime_v1_parse(&runtime, &scan, false,
                    1u, 512u, 128u, &exhausted_gll, error, size) &&
                cetta_parser_token_runtime_v1_parse(&runtime, &scan, true,
                    1u, 512u, 128u, &exhausted_glr, error, size) &&
                exhausted_gll.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT &&
                exhausted_glr.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT &&
                !exhausted_gll.accepted && !exhausted_glr.accepted;
            if (compiled_ok && !exhausted_ok)
                fprintf(stderr, "token runtime: an exhausted budget was not reported as a recognizer limit (GLL=%u/%u GLR=%u/%u)\n",
                    exhausted_gll.outcome, exhausted_gll.accepted,
                    exhausted_glr.outcome, exhausted_glr.accepted);
            compiled_ok = exhausted_ok;
            ppnative_v1_result_free(&exhausted_gll);
            ppnative_v1_result_free(&exhausted_glr);
            Atom *gll_ast = NULL, *glr_ast = NULL;
            compiled_ok = compiled_ok &&
                form(gll.semantic_results[0], "result", 2u) &&
                form(glr.semantic_results[0], "result", 2u) &&
                cetta_parser_token_ast_v1_project(arena, profile.tokens, &profile.start_sort,
                    &scan.source.view, gll.semantic_results[0]->expr.elems[1],
                    512u, 100000u, &gll_ast, error, size) &&
                cetta_parser_token_ast_v1_project(arena, profile.tokens, &profile.start_sort,
                    &scan.source.view, glr.semantic_results[0]->expr.elems[1],
                    512u, 100000u, &glr_ast, error, size) && atom_eq(gll_ast, glr_ast) &&
                form(gll_ast, "LbnfAstNodeV1", 4u) &&
                !strcmp(gll_ast->expr.elems[2]->ground.sval, "Wrap");
            if (compiled_ok) printf("(LbnfGenericAstProjectionV1NativeSummary 4 0)\n");
            static const char comment_input[] = "word{- one {- nested -}a";
            compiled_ok = compiled_ok &&
                cetta_parser_token_runtime_v1_scan(&runtime, (const uint8_t *)comment_input,
                    sizeof(comment_input) - 1u, 1000000u, &scan, error, size) &&
                scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED;
            static const char bad_input[] = "wordb";
            compiled_ok = compiled_ok &&
                cetta_parser_token_runtime_v1_scan(&runtime, (const uint8_t *)bad_input,
                    sizeof(bad_input) - 1u, 1000000u, &scan, error, size) &&
                scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_REJECTED && scan.failure_scalar == 4u;
            ppnative_v1_result_free(&gll);
            ppnative_v1_result_free(&glr);
            cetta_parser_token_scan_v1_free(&scan);
            cetta_parser_token_runtime_v1_free(&runtime);
            if (compiled_ok) printf("(ParserTokenRuntimeV1NativeSummary 9 0)\n");
            Atom *retained = profile.tokens->productions;
            Atom *first = retained->expr.elems[1];
            profile.tokens->productions = atom_expr3(arena, atom_symbol(arena, "LCons"), first,
                atom_expr3(arena, atom_symbol(arena, "LCons"), first, atom_symbol(arena, "LNil")));
            PPABIV1Production *retained_productions = compiled.pack.productions;
            compiled_ok = compiled_ok &&
                !cetta_language_def_parser_pack_v1_compile(&compiled, &language,
                    wire.authority_sha256, &profile, 1000000u, &status, error, size) &&
                strstr(error, "occurrence") && compiled.pack.productions == retained_productions;
            profile.tokens->productions = retained;
        }
        cetta_ld_parser_pack_v1_free(&compiled);
        cetta_language_def_core_v1_free(&language);
        cetta_op_lang_v1_free(&wire);
        if (!compiled_ok) {
            if (!error[0]) snprintf(error, size, "tokenized compilation lost constructor or occurrence identity");
            goto done;
        }
        error[0] = '\0';
        printf("(TokenProfileCompilationV1NativeSummary 3 0)\n");
    }
    if (!cetta_op_lang_v1_parse_document_bytes(&document, (const uint8_t *)other,
            sizeof(other) - 1u, 1000000u, 1000000u, &ingress, error, size) ||
        !cetta_parser_token_profile_v1_decode(&direct, document.root, 100000u, error, size) ||
        direct.policy_kind != CETTA_BNFC_C_FLEX_2_9_6_3_V1) goto done;
    cetta_op_lang_v1_document_free(&document);
    Atom *retained = direct.document;
    if (!cetta_op_lang_v1_parse_document_bytes(&document, (const uint8_t *)bad,
            sizeof(bad) - 1u, 1000000u, 1000000u, &ingress, error, size) ||
        cetta_parser_token_profile_v1_decode(&direct, document.root, 100000u, error, size) ||
        !strstr(error, "policy") || direct.document != retained) {
        snprintf(error, size, "unsupported policy did not refuse atomically");
        goto done;
    }
    if (cetta_parser_token_profile_v1_decode(&direct, document.root, 0u, error, size) ||
        direct.document != retained) {
        snprintf(error, size, "zero work budget replaced the token profile");
        goto done;
    }
    printf("(ParserTokenProfileV1NativeSummary 5 0)\n");
    error[0] = '\0';
    ok = true;
done:
    cetta_op_lang_v1_document_free(&document);
    cetta_ld_parser_profile_v1_free(&profile);
    cetta_parser_token_profile_v1_free(&direct);
    return ok;
}

static CettaDeterministicPrimitiveClassV1 text_equal_class(
    void *context, const char *head, uint32_t count) {
    (void)context;
    return count == 2u && !strcmp(head, "ldpp-v1:text-equal")
        ? CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_ATOM
        : CETTA_DETERMINISTIC_PRIMITIVE_CLASS_V1_NONE;
}

static CettaDeterministicPrimitiveResultV1 text_equal(
    void *context, const char *head, Atom *const *arguments, uint32_t count,
    Arena *arena, Atom **out, char *error, size_t error_size) {
    (void)context;
    (void)error;
    (void)error_size;
    if (count != 2u || strcmp(head, "ldpp-v1:text-equal"))
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_NOT_HANDLED;
    *out = atom_symbol(arena, atom_eq(arguments[0], arguments[1]) ? "True" : "False");
    return CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED;
}

static bool check_token_parser_compiler(Arena *arena, char *error, size_t size) {
    const char *sources[] = {
        "langdef/bnf/language_def_parser_compiler_v1.metta",
        "langdef/bnf/token_parser_compiler_v1.metta",
    };
    const CettaDeterministicVocabularyV1 vocabulary = {
        .primitive = text_equal, .classify = text_equal_class,
    };
    static const struct { const char *call; const char *expected; } cases[] = {
        {
            "(ldpt-v1:compile-rule (ParserProductionInputV1 (site A 1)"
            " (GrammarRule \"Same\" \"Root\" LNil LNil EvalNone)))",
            "(pp-production (pp-occurrence (site A 1)) (pp-def \"Root\")"
            " pp-items-nil (pa-apply CstRuleV1 (pa-cons (pa-const \"Same\") pa-nil)))",
        },
        {
            "(ldpt-v1:compile-rule (ParserProductionInputV1 (site A 2)"
            " (GrammarRule \"Same\" \"Root\" LNil LNil EvalNone)))",
            "(pp-production (pp-occurrence (site A 2)) (pp-def \"Root\")"
            " pp-items-nil (pa-apply CstRuleV1 (pa-cons (pa-const \"Same\") pa-nil)))",
        },
        {
            "(ldpt-v1:compile-rule (ParserProductionInputV1 (site A 3)"
            " (GrammarRule \"Keyword\" \"Root\" LNil"
            " (LCons (SyntaxTerminal (bnf-v1:text-cons 97"
            " (bnf-v1:text-cons 98 (bnf-v1:text-nil)))) LNil) EvalNone)))",
            "(pp-production (pp-occurrence (site A 3)) (pp-def \"Root\")"
            " (pp-items-cons (pp-nonterminal (pp-keyword (bnf-v1:text-cons 97"
            " (bnf-v1:text-cons 98 (bnf-v1:text-nil))))) pp-items-nil)"
            " (pa-apply CstRuleV1 (pa-cons (pa-const \"Keyword\") pa-nil)))",
        },
        {
            "(ldpt-v1:compile-rule (ParserProductionInputV1 (site A 4)"
            " (GrammarRule \"Empty\" \"Root\" LNil"
            " (LCons (SyntaxTerminal (bnf-v1:text-nil)) LNil) EvalNone)))",
            "(pp-production (pp-occurrence (site A 4)) (pp-def \"Root\")"
            " pp-items-nil (pa-apply CstRuleV1 (pa-cons (pa-const \"Empty\") pa-nil)))",
        },
        {
            "(ldpt-v1:compile-rule (ParserProductionInputV1 (site A 5)"
            " (GrammarRule \"Wrap\" \"Root\" (LCons (TermSimple \"x\" (TBase \"Token\")) LNil)"
            " (LCons (SyntaxTerminal (bnf-v1:text-cons 97 (bnf-v1:text-nil)))"
            " (LCons (SyntaxNonTerminal \"x\") LNil)) EvalNone)))",
            "(pp-production (pp-occurrence (site A 5)) (pp-def \"Root\")"
            " (pp-items-cons (pp-nonterminal (pp-keyword (bnf-v1:text-cons 97 (bnf-v1:text-nil))))"
            " (pp-items-cons (pp-nonterminal (pp-def \"Token\")) pp-items-nil))"
            " (pa-apply CstRuleV1 (pa-cons (pa-const \"Wrap\")"
            " (pa-cons (pa-slot (q-succ q-zero)) pa-nil))))",
        },
        {
            "(ldpt-v1:compile-entry \"Root\")",
            "(pp-production (pp-label (pp-entry (pp-def \"Root\")) \"token-parser:whole-source\")"
            " (pp-entry (pp-def \"Root\")) (pp-items-cons (pp-nonterminal (pp-def \"Root\"))"
            " (pp-items-cons (pp-nonterminal pp-layout)"
            " (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil))) (pa-slot q-zero))",
        },
        {
            "(ldpt-v1:compile-layout pp-layout)",
            "(LCons (pp-production (pp-label pp-layout \"token-parser:layout-empty\") pp-layout"
            " pp-items-nil (pa-const TokenLayoutV1))"
            " (LCons (pp-production (pp-label pp-layout \"token-parser:layout-cons\") pp-layout"
            " (pp-items-cons (pp-nonterminal pp-trivia)"
            " (pp-items-cons (pp-nonterminal pp-layout) pp-items-nil))"
            " (pa-const TokenLayoutV1)) LNil))",
        },
    };
    CettaDeterministicEquationPlanV1 *plan = NULL;
    CettaDeterministicEquationStatusV1 status;
    bool ok = false;
    if (!cetta_deterministic_equation_plan_v1_load(sources, 2u, &vocabulary,
            &plan, &status, error, size)) goto done;
    for (size_t i = 0u; i < sizeof(cases) / sizeof(*cases); ++i) {
        Atom *call = NULL, *expected = NULL, *actual = NULL;
        if (!fh_ground_term_v1_parse(arena, (const uint8_t *)cases[i].call,
                strlen(cases[i].call), &call, error, size) ||
            !fh_ground_term_v1_parse(arena, (const uint8_t *)cases[i].expected,
                strlen(cases[i].expected), &expected, error, size) ||
            !cetta_deterministic_equation_plan_v1_apply(plan, call, text_equal, NULL,
                arena, 1024u, 100000u, &actual, &status, error, size) ||
            !atom_eq(actual, expected)) {
            if (!error[0]) snprintf(error, size, "token parser compiler case %zu", i);
            goto done;
        }
    }
    {
        static const char bad[] = "(ldpt-v1:compile-rule (ParserProductionInputV1 (site A 6)"
            " (GrammarRule \"Bad\" \"Root\" (LCons (TermSimple \"x\" (TBase \"Token\")) LNil)"
            " (LCons (SyntaxNonTerminal \"y\") LNil) EvalNone)))";
        Atom *call = NULL, *actual = NULL;
        if (!fh_ground_term_v1_parse(arena, (const uint8_t *)bad, sizeof(bad) - 1u,
                &call, error, size) ||
            cetta_deterministic_equation_plan_v1_apply(plan, call, text_equal, NULL,
                arena, 1024u, 100000u, &actual, &status, error, size) ||
            status != CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE) {
            snprintf(error, size, "mismatched parameter reference was admitted");
            goto done;
        }
    }
    printf("(TokenParserCompilerV1NativeSummary 8 0)\n");
    error[0] = '\0';
    ok = true;
done:
    cetta_deterministic_equation_plan_v1_free(plan);
    return ok;
}

static bool program(const CettaDeterministicEquationPlanV1 *compiler, Arena *arena,
                     const PPABIV1Pack *pack, Atom *regex, RSDFAV1Program *out,
                     char *error, size_t error_size) {
    Atom *call = atom_expr3(arena, atom_symbol(arena, "rtv1:compile"),
                            atom_symbol(arena, "token"), regex);
    Atom *answers = NULL, *cursor;
    Answer *sorted = NULL;
    Atom **terms = NULL;
    RSNFAV1Plan nfa;
    RSDFAV1Plan dfa;
    RSDFAV1BuildOutcome outcome;
    CettaDeterministicEquationStatusV1 status;
    size_t len = 0u;
    bool ok = false;
    rsnfa_v1_plan_init(&nfa);
    rsdfa_v1_plan_init(&dfa);
    if (!cetta_deterministic_equation_plan_v1_apply(compiler, call, NULL, NULL,
            arena, 1024u, 100000u, &answers, &status, error, error_size)) goto done;
    for (cursor = answers; form(cursor, "LCons", 2u); cursor = cursor->expr.elems[2]) ++len;
    if (!word(cursor, "LNil") || !len) goto done;
    sorted = calloc(len, sizeof(*sorted));
    terms = calloc(len, sizeof(*terms));
    if (!sorted || !terms) goto done;
    cursor = answers;
    for (size_t i = 0u; i < len; ++i, cursor = cursor->expr.elems[2]) {
        uint8_t *bytes = NULL;
        size_t byte_len;
        sorted[i].term = cursor->expr.elems[1];
        if (!fh_ground_term_v1_render(sorted[i].term, &bytes, &byte_len,
                error, error_size)) goto done;
        sorted[i].text = (char *)bytes;
    }
    qsort(sorted, len, sizeof(*sorted), compare_answer);
    for (size_t i = 0u; i < len; ++i) terms[i] = sorted[i].term;
    if (!rsnfa_v1_plan_load(pack, terms, len, &nfa, error, error_size) ||
        !rsdfa_v1_plan_build(&nfa.nfa, 4096u, 16384u, &dfa, &outcome,
            error, error_size) || outcome != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_plan_export_program(&dfa, out, error, error_size)) goto done;
    ok = true;
done:
    rsdfa_v1_plan_free(&dfa);
    rsnfa_v1_plan_free(&nfa);
    if (sorted) for (size_t i = 0u; i < len; ++i) free(sorted[i].text);
    free(sorted);
    free(terms);
    return ok;
}

static bool check_policy_data(Arena *arena, CettaRegularTokenCompilerV1 *regular,
                               char *error, size_t size) {
    const char *paths[] = {"langdef/bnf/bnfc_lexical_policy_v1.metta"};
    CettaDeterministicEquationPlanV1 *policy = NULL;
    CettaDeterministicEquationStatusV1 status;
    RSDFAV1Program program;
    CettaLpNativeUtf8ScalarBuffer buffer;
    uint32_t checks = 0u;
    bool ok = false;
    rsdfa_v1_program_init(&program);
    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_deterministic_equation_plan_v1_load(paths, 1u, NULL, &policy,
            &status, error, size)) goto done;
    static const char *names[] = {"bnfc-documented", "bnfc-2.9.6.3-c-flex-2.6.4"};
    for (uint32_t mode = 0u; mode < 2u; ++mode) {
        Atom *call = atom_expr2(arena, atom_symbol(arena, "bnfc-v1:letter"), atom_symbol(arena, names[mode]));
        Atom *regex = NULL;
        if (!cetta_deterministic_equation_plan_v1_apply(policy, call, NULL, NULL,
                arena, 2048u, 1000000u, &regex, &status, error, size) ||
            !cetta_regular_token_compiler_v1_program(regular, arena, atom_symbol(arena, "letter-edge"),
                regex, &program, error, size)) goto done;
        for (uint32_t cp = 0u; cp < 256u; ++cp) {
            uint8_t bytes[2] = {(uint8_t)cp, 0u};
            uint32_t len = 1u;
            if (cp >= 128u) { bytes[0] = 0xc0u | (cp >> 6u); bytes[1] = 0x80u | (cp & 63u); len = 2u; }
            bool expected = (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                (!mode && cp >= 192u && cp <= 255u && cp != 215u && cp != 247u);
            RSDFAV1Token token;
            RSDFAV1CursorScanResult scan;
            if (!cetta_lp_native_utf8_scalar_buffer_decode(&buffer, bytes, len, error, size) ||
                !rsdfa_v1_program_scan_cursor_longest(&program, &buffer.view, 0u, 1000u,
                    &token, 1u, &scan, error, size) || scan.accept_len != (expected ? 1u : 0u)) {
                snprintf(error, size, "BNFC letter table mismatch %u/%u", mode, cp); goto done;
            }
            ++checks;
        }
        rsdfa_v1_program_free(&program);
    }
    static const struct { const char *kind, *input; bool accepts; } literals[] = {
        {"String", "\"a\\nλ\"", true}, {"String", "\"\\q\"", false},
        {"String", "\"line\nbreak\"", false}, {"Char", "'λ'", true},
        {"Char", "'\\n'", true}, {"Char", "'\n'", true},
        {"Char", "'\\\"'", false}, {"Char", "'two'", false},
        {"Integer", "123", true}, {"Double", "12.3e-4", true},
        {"Double", "12.3e+4", false}, {"Ident", "ÿ_9'", true},
    };
    for (size_t i = 0u; i < sizeof(literals) / sizeof(*literals); ++i) {
        Atom *call = atom_expr3(arena, atom_symbol(arena, "bnfc-v1:builtin"),
            atom_symbol(arena, literals[i].kind), atom_symbol(arena, "bnfc-documented"));
        Atom *regex = NULL;
        RSDFAV1Token token;
        RSDFAV1CursorScanResult scan;
        if (!cetta_deterministic_equation_plan_v1_apply(policy, call, NULL, NULL,
                arena, 2048u, 1000000u, &regex, &status, error, size) ||
            !cetta_regular_token_compiler_v1_program(regular, arena, atom_symbol(arena, "literal-edge"),
                regex, &program, error, size) ||
            !cetta_lp_native_utf8_scalar_buffer_decode(&buffer,
                (const uint8_t *)literals[i].input, strlen(literals[i].input), error, size) ||
            !rsdfa_v1_program_scan_cursor_longest(&program, &buffer.view, 0u, 10000u,
                &token, 1u, &scan, error, size) ||
            (scan.accept_len && token.end_scalar == buffer.view.scalar_len) != literals[i].accepts) {
            if (!error[0]) snprintf(error, size, "BNFC documented literal mismatch %zu", i);
            goto done;
        }
        ++checks;
        rsdfa_v1_program_free(&program);
    }
    printf("(BnfcLexicalPolicyDataV1NativeSummary %u 0)\n", checks);
    ok = true;
done:
    cetta_deterministic_equation_plan_v1_free(policy);
    rsdfa_v1_program_free(&program);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return ok;
}

int main(void) {
    static const char *regexes[] = {
        "rx-eps", "rx-empty", "rx-eof", "rx-any", "(rx-char (cp 97))",
        "(rx-class letter)", "(rx-class not-newline)",
        "(rx-alt (rx-char (cp 97)) (rx-char (cp 98)))",
        "(rx-seq (rx-char (cp 97)) (rx-star (rx-char (cp 98))))",
        "(rx-star (rx-alt (rx-char (cp 97)) rx-eps))",
        "(rx-seq (rx-star (rx-alt (rx-char (cp 97)) (rx-char (cp 98)))) rx-eof)",
        "(rx-star (rx-seq (rx-char (cp 97)) (rx-char (cp 98))))",
        "(rx-alt (rx-seq (rx-char (cp 97)) rx-eof) (rx-char (cp 98)))",
    };
    const char *paths[] = {"langdef/bnf/regular_token_value_compiler_v1.metta"};
    SymbolTable symbols;
    Arena arena;
    PPABIV1Pack classes;
    CettaDeterministicEquationPlanV1 *compiler = NULL;
    CettaRegularTokenCompilerV1 *regular = NULL;
    CettaDeterministicEquationStatusV1 status;
    RSDFAV1Program dfa;
    uint32_t checks = 0u;
    char error[512] = {0};
    int exit_status = 1;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    ppabi_v1_pack_init(&classes);
    rsdfa_v1_program_init(&dfa);
    classes.class_clauses = calloc(2u, sizeof(*classes.class_clauses));
    if (!classes.class_clauses) goto done;
    classes.class_clause_len = 2u;
    classes.class_clauses[0].class_identity = atom_symbol(&arena, "letter");
    classes.class_clauses[0].kind = PPABI_V1_CLASS_POINT;
    classes.class_clauses[0].point = 'a';
    classes.class_clauses[1].class_identity = atom_symbol(&arena, "not-newline");
    classes.class_clauses[1].kind = PPABI_V1_CLASS_EXCEPT;
    classes.class_clauses[1].excluded_points = malloc(sizeof(uint32_t));
    if (!classes.class_clauses[1].excluded_points) goto done;
    classes.class_clauses[1].excluded_points[0] = '\n';
    classes.class_clauses[1].excluded_len = 1u;
    if (!check_token_profile(&arena, error, sizeof(error))) goto done;
    if (!check_token_parser_compiler(&arena, error, sizeof(error))) goto done;
    if (!check_counted_scalar_text(error, sizeof(error))) goto done;
    if (!check_lbnf_document(&arena, error, sizeof(error))) goto done;
    if (!cetta_deterministic_equation_plan_v1_load(paths, 1u, NULL, &compiler,
            &status, error, sizeof(error))) goto done;
    if (!cetta_regular_token_compiler_v1_load(&classes, 1000000u, &regular, error, sizeof(error))) goto done;
    if (!check_policy_data(&arena, regular, error, sizeof(error))) goto done;
    for (size_t i = 0u; i < sizeof(regexes) / sizeof(*regexes); ++i) {
        Atom *regex = NULL;
        if (!fh_ground_term_v1_parse(&arena, (const uint8_t *)regexes[i], strlen(regexes[i]),
                &regex, error, sizeof(error)) ||
            !program(compiler, &arena, &classes, regex, &dfa, error, sizeof(error))) goto done;
        for (uint32_t len = 0u, words = 1u; len <= 5u; ++len, words *= 3u) {
            for (uint32_t w = 0u; w < words; ++w) {
                uint8_t text[5] = {0};
                uint32_t digits = w;
                RSDFAV1ByteToken token;
                RSDFAV1ByteCursorScanResult scan;
                uint32_t expected_end = 0u;
                bool expected = false;
                for (uint32_t j = 0u; j < len; ++j, digits /= 3u)
                    text[j] = (uint8_t)('a' + digits % 3u);
                for (uint32_t end = 0u; end <= len; ++end) {
                    if (reference(regex, text, 0u, end, len)) {
                        expected = true;
                        expected_end = end;
                    }
                }
                if (!rsdfa_v1_program_scan_byte_cursor_longest(&dfa, text, len, 0u,
                        1000u, &token, 1u, &scan, error, sizeof(error)) ||
                    scan.outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED ||
                    scan.accept_len != (expected ? 1u : 0u) ||
                    (expected && (token.tag != 0u || token.start_byte != 0u ||
                                  token.end_byte != expected_end))) {
                    snprintf(error, sizeof(error), "language mismatch: regex=%zu word=%u length=%u", i, w, len);
                    goto done;
                }
                ++checks;
            }
        }
        rsdfa_v1_program_free(&dfa);
    }
    static const char *differences[] = {
        "(rx-range (cp 97) (cp 99))",
        "(rx-minus (rx-range (cp 97) (cp 99)) (rx-char (cp 98)))",
        "(rx-minus (rx-star (rx-range (cp 97) (cp 99))) (rx-star (rx-char (cp 98))))",
        "(rx-minus (rx-seq (rx-char (cp 97)) (rx-char (cp 98))) (rx-char (cp 97)))",
        "(rx-seq (rx-minus (rx-star (rx-range (cp 97) (cp 99))) rx-eps) (rx-char (cp 99)))",
        "(rx-minus (rx-minus (rx-star (rx-range (cp 97) (cp 99))) (rx-star (rx-char (cp 98)))) (rx-char (cp 97)))",
        "(rx-minus (rx-star (rx-range (cp 97) (cp 99))) (rx-star (rx-range (cp 97) (cp 99))))",
    };
    uint32_t difference_checks = 0u;
    for (size_t i = 0u; i < sizeof(differences) / sizeof(*differences); ++i) {
        Atom *regex = NULL;
        if (!fh_ground_term_v1_parse(&arena, (const uint8_t *)differences[i], strlen(differences[i]),
                &regex, error, sizeof(error)) ||
            !cetta_regular_token_compiler_v1_program(regular, &arena, atom_symbol(&arena, "token"),
                regex, &dfa, error, sizeof(error))) goto done;
        for (uint32_t len = 0u, words = 1u; len <= 5u; ++len, words *= 3u) {
            for (uint32_t w = 0u; w < words; ++w) {
                uint8_t text[5] = {0}; uint32_t digits = w;
                RSDFAV1ByteToken token;
                RSDFAV1ByteCursorScanResult scan;
                bool expected = false; uint32_t expected_end = 0u;
                for (uint32_t j = 0u; j < len; ++j, digits /= 3u) text[j] = (uint8_t)('a' + digits % 3u);
                for (uint32_t end = 0u; end <= len; ++end)
                    if (reference(regex, text, 0u, end, len)) { expected = true; expected_end = end; }
                if (!rsdfa_v1_program_scan_byte_cursor_longest(&dfa, text, len, 0u, 1000u,
                        &token, 1u, &scan, error, sizeof(error)) ||
                    scan.accept_len != (expected ? 1u : 0u) || (expected && token.end_byte != expected_end)) {
                    snprintf(error, sizeof(error), "difference mismatch %zu/%u/%u", i, w, len); goto done;
                }
                ++difference_checks;
            }
        }
        rsdfa_v1_program_free(&dfa);
    }
    printf("(RegularTokenDifferenceV1NativeSummary %u 0)\n", difference_checks);
    {
        Atom *bad = atom_expr3(&arena, atom_symbol(&arena, "rtv1:compile"),
                               atom_symbol(&arena, "token"), atom_symbol(&arena, "not-a-regex"));
        Atom *result = NULL;
        if (cetta_deterministic_equation_plan_v1_apply(compiler, bad, NULL, NULL, &arena,
                1024u, 10000u, &result, &status, error, sizeof(error)) ||
            status != CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE) {
            snprintf(error, sizeof(error), "unknown regular-expression constructor was admitted");
            goto done;
        }
        ++checks;
    }
    printf("(RegularTokenValueCompilerV1NativeSummary %u 0)\n", checks);
    exit_status = 0;
done:
    if (exit_status) fprintf(stderr, "regular-token compiler gate: %s\n", error);
    rsdfa_v1_program_free(&dfa);
    cetta_deterministic_equation_plan_v1_free(compiler);
    cetta_regular_token_compiler_v1_free(regular);
    ppabi_v1_pack_free(&classes);
    arena_free(&arena);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return exit_status;
}
