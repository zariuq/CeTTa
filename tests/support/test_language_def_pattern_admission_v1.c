#include "native/language_def_pattern_admission_v1.h"
#include "native/language_def_pattern_atom_v1.h"
#include "native/language_def_ground_term_v1.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed, failed;
static void expect(bool ok, const char *name) {
    if (ok) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}

static Atom *data(Arena *arena, const char *source) {
    Atom **items = NULL;
    int n = parse_metta_text(source, arena, &items);
    Atom *out = n == 1 ? items[0] : NULL;
    free(items);
    return out;
}

static const char core[] =
    "(GSLTLanguageDefWireV1 \"BindingAdmission\" "
    "(LCons (TypeDecl \"E\" CarrierAst) (LCons (TypeDecl \"Name\" CarrierAst) "
    "(LCons (TypeDecl \"ListE\" CarrierAst) LNil))) "
    "(LCons (GrammarRule \"K\" \"E\" LNil LNil EvalNone) "
    "(LCons (GrammarRule \"X\" \"Name\" LNil LNil EvalNone) "
    "(LCons (GrammarRule \"Ref\" \"E\" (LCons (TermSimple \"name\" (TBase \"Name\")) LNil) LNil EvalNone) "
    "(LCons (GrammarRule \"Lam\" \"E\" (LCons (TermAbstractionNamed (BSome \"x\") \"body\" "
    "(TArrow (TBase \"Name\") (TBase \"E\"))) LNil) LNil EvalNone) "
    "(LCons (GrammarRule \"Same\" \"E\" (LCons (TermAbstractionNamed BNone \"body\" (TBase \"E\")) LNil) LNil EvalNone) "
    "(LCons (GrammarRule \"Multi\" \"E\" (LCons (TermMultiAbstractionNamed (LCons \"x\" (LCons \"y\" LNil)) \"body\" "
    "(TArrow (TMultiBinder (TBase \"Name\")) (TBase \"E\"))) LNil) LNil EvalNone) "
    "(LCons (GrammarRule \"Fn\" \"E\" (LCons (TermSimple \"fn\" (TArrow (TBase \"Name\") (TBase \"E\"))) LNil) LNil EvalNone) "
    "(LCons (GrammarRule \"Vec\" \"ListE\" (LCons (TermSimple \"items\" "
    "(TCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" (TBase \"E\"))) LNil) LNil EvalNone) LNil)))))))) "
    "LNil LNil)";

static void admission(Arena *arena, CettaLanguageDefCoreV1 *language, const char *category,
                       const char *source, CettaLdPatternAdmissionV1Status wanted, const char *label) {
    CettaLdPatternV1 pattern = {0};
    CettaLdPatternAtomV1Status wire_status;
    CettaLdPatternAdmissionV1Status status;
    char error[256];
    bool decoded = cetta_ld_pattern_atom_v1_decode(&pattern, data(arena, source), 256u, 100000u,
        &wire_status, error, sizeof(error));
    bool accepted = decoded && cetta_language_def_pattern_v1_admit(language, category, &pattern,
        256u, 100000u, &status, error, sizeof(error));
    bool ok = decoded && status == wanted && accepted == (wanted == CETTA_LD_PA_V1_OK);
    if (!ok) fprintf(stderr, "%s: %s\n", label, error);
    expect(ok, label);
    cetta_ld_pattern_v1_free(&pattern);
}

static void codecs(Arena *arena) {
    const char *wires[] = {
        "(PApp \"K\" LNil)", "(FVar \"open\")", "(Var 0)", "(Var 184467440737095516160000)",
        "(PLam BNone (Var 0))", "(PLam (BSome \"x\") (Var 1))",
        "(PMultiLam 2 (LCons \"outer\" (LCons \"inner\" LNil)) (Var 1))",
        "(PSubst (PLam BNone (Var 1)) (FVar \"free\"))",
        "(PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" (LCons (Var 0) LNil) RNone)",
        "(PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag\" LNil (RSome \"rest\"))",
        "(PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashSet\" LNil RNone)",
    };
    CettaLdPatternAtomV1Status status;
    char error[256];
    for (size_t i = 0; i < sizeof(wires) / sizeof(*wires); ++i) {
        Atom *original = data(arena, wires[i]);
        CettaLdPatternV1 pattern = {0};
        bool decoded = cetta_ld_pattern_atom_v1_decode(&pattern, original, 256u, 100000u, &status, error, sizeof(error));
        Atom *encoded = decoded ? cetta_ld_pattern_atom_v1_encode(arena, &pattern, 256u, 100000u, &status, error, sizeof(error)) : NULL;
        expect(encoded && atom_eq(original, encoded), "complete Pattern carrier roundtrips without executing it");
        cetta_ld_pattern_v1_free(&pattern);
    }
    const char *bad[] = {"(Var -1)", "(Var 1.5)", "(PLam BNone)", "(PLam BBad (Var 0))",
        "(PMultiLam 2 (LCons 3 LNil) (Var 0))", "(PSubst (Var 0) bad)",
        "(PCollection \"unknown\" LNil RNone)", "(PApp \"K\" (LCons (Var 0) bad))"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        CettaLdPatternV1 pattern = {0};
        Atom *old = data(arena, "(PApp \"K\" LNil)");
        bool seeded = cetta_ld_pattern_atom_v1_decode(&pattern, old, 256u, 100000u, &status, error, sizeof(error));
        bool rejected = !cetta_ld_pattern_atom_v1_decode(&pattern, data(arena, bad[i]), 256u, 100000u, &status, error, sizeof(error));
        Atom *retained = cetta_ld_pattern_atom_v1_encode(arena, &pattern, 256u, 100000u, &status, error, sizeof(error));
        expect(seeded && rejected && retained && atom_eq(old, retained), "malformed Pattern decode is atomic");
        cetta_ld_pattern_v1_free(&pattern);
    }
}

static Atom *ast_node(Arena *arena, const char *label, Atom **children, uint32_t count) {
    Atom *list = atom_symbol(arena, "LNil");
    while (count) {
        Atom *entry[] = {atom_symbol(arena, "LCons"), children[--count], list};
        list = atom_expr(arena, entry, 3u);
    }
    Atom *items[] = {atom_symbol(arena, "LbnfAstNodeV1"), atom_string(arena, "E"), atom_string(arena, label),
        data(arena, "(LbnfSourceSpanV1 0 100 0 100)"), list};
    return atom_expr(arena, items, 5u);
}

static Atom *name_token(Arena *arena, int scalar) {
    Atom *nil[] = {atom_symbol(arena, "bnf-v1:text-nil")};
    Atom *text[] = {atom_symbol(arena, "bnf-v1:text-cons"), atom_int(arena, scalar), atom_expr(arena, nil, 1u)};
    Atom *items[] = {atom_symbol(arena, "LbnfAstTokenV1"), atom_string(arena, "Name"), atom_symbol(arena, "UserToken"),
        atom_expr(arena, text, 3u), data(arena, "(LbnfTokenPositionV1 1 1)"),
        data(arena, "(LbnfSourceSpanV1 0 1 0 1)"), atom_int(arena, 0)};
    return atom_expr(arena, items, 7u);
}

static void projections(CettaLanguageDefCoreV1 *original) {
    Arena source, result, expected;
    arena_init(&source); arena_init(&result); arena_init(&expected);
    CettaLanguageDefCoreV1 language = *original;
    CettaLdTypeDeclV1 types[3]; memcpy(types, original->types, sizeof(types));
    types[1].carrier = CETTA_LD_CARRIER_TOKEN_LABEL_V1;
    language.types = types;
    CettaLdGrammarRuleV1 rules[4] = {original->terms[0], original->terms[2], original->terms[3], original->terms[3]};
    CettaLdSyntaxItemV1 ref = {.kind = CETTA_LD_SYNTAX_NONTERMINAL_V1, .as.text = rules[1].params[0].body_name};
    CettaLdSyntaxItemV1 lam[] = {
        {.kind = CETTA_LD_SYNTAX_NONTERMINAL_V1, .as.text = rules[2].params[0].names.binder.value},
        {.kind = CETTA_LD_SYNTAX_NONTERMINAL_V1, .as.text = rules[2].params[0].body_name}};
    rules[1].syntax_pattern = (CettaLdSyntaxItemListV1){&ref, 1u};
    rules[2].syntax_pattern = (CettaLdSyntaxItemListV1){lam, 2u}; rules[3] = rules[2];
    language.terms = rules; language.term_len = 4u;
    Atom *profile = data(&source, "(LangDef:NamedTokensV1 (LCons \"Name\" LNil))");
    Atom *x = name_token(&source, 'x'), *y = name_token(&source, 'y');
    Atom *reference = ast_node(&source, "Ref", &x, 1u);
    Atom *fields[] = {y, reference};
    Atom *inner = ast_node(&source, "Lam", fields, 2u);
    fields[0] = x; fields[1] = inner;
    Atom *outer = ast_node(&source, "Lam", fields, 2u);
    Atom *wanted = data(&expected, "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 1) LNil))) LNil))) LNil))");
    Atom *pattern = NULL, *locations = NULL;
    const Atom *blame = NULL;
    CettaLdGroundTermV1Status status;
    char error[256];
    bool ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, outer,
        UINT32_MAX, 100000u, &pattern, &locations, &blame, &status, error, sizeof(error));
    if (!ok) fprintf(stderr, "projection: %s\n", error);
    expect(ok && atom_eq(pattern, wanted), "named projection preserves outer binding and accepts equal-valued distinct declarations");
    bool budgets = true;
    for (uint64_t budget = 1u; budget <= 300u; ++budget) {
        Arena limited; arena_init(&limited);
        Atom *p = wanted, *s = wanted;
        bool accepted = cetta_language_def_pattern_v1_project_ast(&limited, &language, "E", profile, outer,
            UINT32_MAX, budget, &p, &s, &blame, &status, error, sizeof(error));
        budgets = budgets && (accepted ? atom_eq(p, wanted) :
            status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT && p == wanted && s == wanted);
        arena_free(&limited);
    }
    expect(budgets, "all bounded projection attempts either publish the complete term or leave both outputs untouched");
    Atom *sentinel = wanted, *other = wanted;
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, reference,
        UINT32_MAX, 10000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(!ok && status == CETTA_LD_GROUND_TERM_V1_SCOPE_MISMATCH && sentinel == wanted && other == wanted && blame,
        "unbound token reference gives positioned atomic refusal");
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, outer,
        1u, 10000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(!ok && status == CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT, "projection depth exhaustion is not semantic rejection");
    Atom *malformed = ast_node(&source, "Lam", &x, 1u);
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, malformed,
        100u, 10000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(!ok && status == CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH, "missing source body cannot become a term");
    Atom *wrong_span = atom_deep_copy(&source, x);
    wrong_span->expr.elems[5] = data(&source, "(LbnfSourceSpanV1 101 102 101 102)");
    fields[0] = wrong_span; fields[1] = reference;
    malformed = ast_node(&source, "Lam", fields, 2u);
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, malformed,
        100u, 10000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(!ok && status == CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT, "binder spelling cannot claim an out-of-parent source span");
    fields[0] = name_token(&source, 0); fields[1] = reference;
    malformed = ast_node(&source, "Lam", fields, 2u);
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, malformed,
        100u, 10000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(!ok && status == CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE, "embedded NUL cannot truncate a binding identity");
    /* Notation can put the body before the spelling. Parameter labels, not
     * positional heuristics, locate both fields. */
    CettaLdSyntaxItemV1 swapped[] = {lam[1], lam[0]};
    rules[2].syntax_pattern.items = swapped; rules[3].syntax_pattern.items = swapped;
    fields[0] = reference; fields[1] = x;
    Atom *reordered = ast_node(&source, "Lam", fields, 2u);
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, reordered,
        100u, 10000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(ok && atom_eq(sentinel, data(&expected, "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 0) LNil))) LNil))")),
        "declaration metadata resolves reordered notation fields");
    rules[2].syntax_pattern.items = lam; rules[3].syntax_pattern.items = lam;
    Atom *unicode = name_token(&source, 0x3bbu);
    Atom *unicode_reference = ast_node(&source, "Ref", &unicode, 1u);
    fields[0] = unicode; fields[1] = unicode_reference;
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile,
        ast_node(&source, "Lam", fields, 2u), 100u, 10000u,
        &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(ok && atom_eq(sentinel, data(&expected, "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 0) LNil))) LNil))")),
        "Unicode source names retain their exact identity");
    CettaLdGrammarRuleV1 mixed[5]; memcpy(mixed, rules, sizeof(rules));
    CettaLdTermParamV1 different_parameter = rules[2].params[0];
    CettaLdTypeExprV1 other_sort = {.kind = CETTA_LD_TYPE_BASE_V1, .as.base = types[2].name};
    different_parameter.type.as.arrow.domain = &other_sort;
    mixed[4] = rules[2]; mixed[4].params = &different_parameter;
    mixed[4].label = (CettaLdTextV1){(uint8_t *)"OtherLam", 8u};
    types[2].carrier = CETTA_LD_CARRIER_TOKEN_LABEL_V1;
    language.terms = mixed; language.term_len = 5u;
    Atom *other_token = name_token(&source, 'x'); other_token->expr.elems[1] = atom_string(&source, "ListE");
    fields[0] = other_token; fields[1] = reference;
    Atom *mixed_body = ast_node(&source, "OtherLam", fields, 2u);
    fields[0] = x; fields[1] = mixed_body;
    Atom *mixed_profile = data(&source, "(LangDef:NamedTokensV1 (LCons \"Name\" (LCons \"ListE\" LNil)))");
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", mixed_profile,
        ast_node(&source, "Lam", fields, 2u), 100u, 10000u,
        &sentinel, &other, &blame, &status, error, sizeof(error));
    expect(ok && atom_eq(sentinel, data(&expected, "(PApp \"Lam\" (LCons (PLam BNone (PApp \"OtherLam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 1) LNil))) LNil))) LNil))")),
        "category namespaces remain separate while every binder contributes an index level");
    language.terms = rules; language.term_len = 4u;
    Atom *deep = reference;
    for (unsigned i = 0u; i < 10000u; ++i) {
        fields[0] = x; fields[1] = deep; deep = ast_node(&source, "Lam", fields, 2u);
    }
    ok = cetta_language_def_pattern_v1_project_ast(&result, &language, "E", profile, deep,
        UINT32_MAX, 10000000u, &sentinel, &other, &blame, &status, error, sizeof(error));
    Atom *cursor = sentinel;
    unsigned depth = 0u;
    while (ok && cursor && cursor->kind == ATOM_EXPR && cursor->expr.len == 3u &&
           atom_is_symbol(cursor->expr.elems[0], "PApp") && atom_eq(cursor->expr.elems[1], atom_string(&expected, "Lam"))) {
        cursor = cursor->expr.elems[2]->expr.elems[1]->expr.elems[2]; ++depth;
    }
    expect(ok && depth == 10000u && atom_eq(cursor, data(&expected, "(PApp \"Ref\" (LCons (Var 0) LNil))")),
        "ten thousand source binders project without C recursion or capture");
    arena_free(&source);
    expect(atom_eq(pattern, wanted) && locations && locations->kind == ATOM_EXPR && locations->expr.len == 8u,
        "projected Pattern and all source locations survive source arena teardown");
    arena_free(&result); arena_free(&expected);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols); g_symbols = &symbols;
    Arena arena; arena_init(&arena);
    codecs(&arena);
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Status wire_status;
    CettaLdCoreV1Status core_status;
    char error[256] = {0};
    cetta_op_lang_v1_init(&wire); cetta_language_def_core_v1_init(&language);
    bool loaded = cetta_op_lang_v1_parse_bytes(&wire, (const uint8_t *)core, strlen(core),
        100000u, 100000u, &wire_status, error, sizeof(error)) &&
        cetta_language_def_core_v1_decode(&language, &wire, 100000u, &core_status, error, sizeof(error));
    if (!loaded) fprintf(stderr, "%s\n", error);
    expect(loaded, "decode binding LanguageDef without another signature inventory");
    if (loaded) {
        projections(&language);
        admission(&arena, &language, "E", "(PApp \"K\" LNil)", CETTA_LD_PA_V1_OK, "constructor positive");
        admission(&arena, &language, "E", "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 0) LNil))) LNil))",
            CETTA_LD_PA_V1_OK, "heterogeneous binder uses its declared domain");
        admission(&arena, &language, "E", "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 1) LNil))) LNil))) LNil))",
            CETTA_LD_PA_V1_OK, "nested binder preserves the outer reference");
        admission(&arena, &language, "E", "(PApp \"Same\" (LCons (PLam BNone (Var 0)) LNil))",
            CETTA_LD_PA_V1_OK, "same-sort binding shorthand");
        admission(&arena, &language, "E", "(PApp \"Multi\" (LCons (PMultiLam 2 LNil (PApp \"Ref\" (LCons (Var 1) LNil))) LNil))",
            CETTA_LD_PA_V1_OK, "multi-binder context uses its full arity");
        admission(&arena, &language, "E", "(PSubst (PApp \"Ref\" (LCons (Var 0) LNil)) (PApp \"X\" LNil))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "typed substitution schema is not a closed object");
        admission(&arena, &language, "E", "(PApp \"Fn\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 0) LNil))) LNil))",
            CETTA_LD_PA_V1_OK, "ordinary arrow data uses core lambda typing without binder-profile promotion");
        admission(&arena, &language, "ListE", "(PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" (LCons (PApp \"K\" LNil) LNil) RNone)",
            CETTA_LD_PA_V1_OK, "declared bare collection positive");
        admission(&arena, &language, "E", "(Var 0)", CETTA_LD_PA_V1_SCOPE_MISMATCH, "escaped root index refused");
        admission(&arena, &language, "E", "(FVar \"x\")", CETTA_LD_PA_V1_SCOPE_MISMATCH, "free name is not silently bound");
        admission(&arena, &language, "E", "(PApp \"Lam\" (LCons (PLam BNone (Var 0)) LNil))",
            CETTA_LD_PA_V1_TYPE_MISMATCH, "in-scope index at wrong sort refused");
        admission(&arena, &language, "E", "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 1) LNil))) LNil))",
            CETTA_LD_PA_V1_SCOPE_MISMATCH, "index cannot reach beyond one binder");
        admission(&arena, &language, "E", "(PApp \"Lam\" (LCons (PApp \"K\" LNil) LNil))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "binding wrapper cannot be erased");
        admission(&arena, &language, "E", "(PApp \"Lam\" (LCons (PLam (BSome \"x\") (PApp \"K\" LNil)) LNil))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "display name does not become semantic binder identity");
        admission(&arena, &language, "E", "(PApp \"Multi\" (LCons (PMultiLam 1 LNil (PApp \"Ref\" (LCons (Var 1) LNil))) LNil))",
            CETTA_LD_PA_V1_SCOPE_MISMATCH, "multi-binder off-by-one refused");
        admission(&arena, &language, "E", "(PSubst (PApp \"Ref\" (LCons (Var 1) LNil)) (PApp \"X\" LNil))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "open substitution schema is not an object either");
        admission(&arena, &language, "E", "(PSubst (Var 0) (PLam BNone (Var 0)))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "object admission never invents a substitution-domain type");
        admission(&arena, &language, "E", "(PApp \"Fn\" (LCons (PLam (BSome \"display\") (PApp \"Ref\" (LCons (Var 0) LNil))) LNil))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "canonical binder metadata applies to ordinary arrows too");
        admission(&arena, &language, "ListE", "(PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" LNil (RSome \"rest\"))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "open collection schema does not enter a closed carrier");
        admission(&arena, &language, "ListE", "(PApp \"Vec\" (LCons (PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" LNil RNone) LNil))",
            CETTA_LD_PA_V1_REPRESENTATION_MISMATCH, "bare collection is not a wrapped constructor");
        CettaLdPatternV1 pattern = {0}; CettaLdPatternAtomV1Status ps;
        bool decoded = cetta_ld_pattern_atom_v1_decode(&pattern, data(&arena,
            "(PApp \"Lam\" (LCons (PLam BNone (PApp \"Ref\" (LCons (Var 0) LNil))) LNil))"),
            256u, 100000u, &ps, error, sizeof(error));
        bool bounded = decoded;
        for (uint64_t budget = 1u; budget < 100u; ++budget) {
            CettaLdPatternAdmissionV1Status status;
            bool result = cetta_language_def_pattern_v1_admit(&language, "E", &pattern, 256u, budget, &status, error, sizeof(error));
            bounded = bounded && ((result && status == CETTA_LD_PA_V1_OK) || (!result && status == CETTA_LD_PA_V1_RESOURCE_LIMIT));
        }
        expect(bounded, "work exhaustion never masquerades as invalid typing");
        cetta_ld_pattern_v1_free(&pattern);
        /* Membership is existential over declarations; it neither selects an
         * occurrence for projection nor collapses the authored row list. */
        CettaLdGrammarRuleV1 declarations[2] = {language.terms[2], language.terms[2]};
        CettaLanguageDefCoreV1 overloaded = language;
        overloaded.terms = declarations; overloaded.term_len = 2u;
        declarations[0].label = language.terms[0].label;
        declarations[1] = language.terms[0];
        admission(&arena, &overloaded, "E", "(PApp \"K\" LNil)", CETTA_LD_PA_V1_OK,
            "a later matching signature can supply the derivation");
        declarations[0] = declarations[1];
        admission(&arena, &overloaded, "E", "(PApp \"K\" LNil)", CETTA_LD_PA_V1_OK,
            "equal-valued distinct constructor declarations do not prevent admission");
        declarations[0] = declarations[1] = language.terms[7];
        admission(&arena, &overloaded, "ListE", "(PCollection \"Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec\" LNil RNone)",
            CETTA_LD_PA_V1_OK, "duplicate collection declarations retain existential typing");
        CettaLdTypeDeclV1 types[2] = {language.types[0], language.types[0]};
        overloaded.types = types; overloaded.type_len = 2u;
        declarations[0] = language.terms[0];
        admission(&arena, &overloaded, "E", "(PApp \"K\" LNil)", CETTA_LD_PA_V1_OK,
            "sort membership does not require a unique declaration occurrence");
    }
    cetta_language_def_core_v1_free(&language); cetta_op_lang_v1_free(&wire);
    arena_free(&arena); g_symbols = NULL; symbol_table_free(&symbols);
    printf("(LanguageDefPatternAdmissionV1Summary %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
