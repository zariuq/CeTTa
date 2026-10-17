#include "native/module_value_v1.h"
#include "native/module_value_internal_v1.h"
#include "native/module_fields_v1.h"
#include "native/syntax_gslt_v1.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static bool check(bool condition, const char *name) {
    ++checks;
    if (!condition) fprintf(stderr, "FAIL: %s\n", name);
    return condition;
}

static Atom *data(Arena *arena, const char *source) {
    Atom **values = NULL;
    int count = parse_metta_text(source, arena, &values);
    Atom *out = count == 1 ? values[0] : NULL;
    free(values);
    return out;
}

static Atom *ast(Arena *arena, const char *head, Atom *const *args, size_t count) {
    ModuleValuesV1 values = {.items = (Atom **)args, .len = count};
    Atom *span_args[] = {atom_int(arena, 0), atom_int(arena, 10), atom_int(arena, 0), atom_int(arena, 10)};
    Atom *fields[] = {atom_string(arena, "source-category"), atom_string(arena, head),
        module_make_v1(arena, "LbnfSourceSpanV1", span_args, 4u), module_list_v1(arena, &values)};
    return module_make_v1(arena, "LbnfAstNodeV1", fields, 4u);
}

static Atom *token(Arena *arena, const char *text) {
    Atom *fields[] = {atom_string(arena, "Ident"), atom_string(arena, "TokenIdentV1"),
        atom_string(arena, text), atom_string(arena, text),
        atom_symbol(arena, "span"), atom_symbol(arena, "position")};
    return module_make_v1(arena, "LbnfAstTokenV1", fields, 6u);
}

static Atom *list_ast(Arena *arena, Atom *const *args, size_t count) {
    ModuleValuesV1 values = {.items = (Atom **)args, .len = count};
    Atom *fields[] = {atom_string(arena, "list"), atom_symbol(arena, "span"), module_list_v1(arena, &values)};
    return module_make_v1(arena, "LbnfAstListV1", fields, 3u);
}

static Atom *theory_ast(Arena *arena, const char *theory_name) {
    Atom *a[] = {token(arena, "T")};
    Atom *cat = ast(arena, "IdCat", a, 1u);
    a[0] = token(arena, "A");
    Atom *head = ast(arena, "Id", a, 1u);
    a[0] = token(arena, "a");
    Atom *literal = ast(arena, "Terminal", a, 1u);
    a[0] = literal;
    Atom *rule_args[] = {head, cat, list_ast(arena, a, 1u)};
    Atom *rule = ast(arena, "Rule", rule_args, 3u);
    a[0] = rule;
    Atom *definitions = list_ast(arena, a, 1u);
    a[0] = definitions;
    Atom *grammar = ast(arena, "MkGrammar", a, 1u);
    Atom *add_args[] = {ast(arena, "TheoryInstEmpty", NULL, 0u), grammar};
    Atom *body = ast(arena, "TheoryInstAddTerms", add_args, 2u);
    a[0] = token(arena, theory_name);
    Atom *name = ast(arena, "NameVar", a, 1u);
    Atom *theory_args[] = {name, list_ast(arena, NULL, 0u), body};
    Atom *theory = ast(arena, "BaseTheoryDecl", theory_args, 3u);
    a[0] = theory;
    return ast(arena, "ProgTheoryDecl", a, 1u);
}

static Atom *add_theory(Arena *arena, Atom *module, const char *name, Atom *parameters, Atom *builder) {
    Atom *fields[] = {atom_string(arena, name), parameters, builder};
    Atom *theory = module_make_v1(arena, "ModuleTheoryV1", fields, 3u);
    Atom **args = arena_alloc(arena, module->expr.len * sizeof(*args));
    for (size_t i = 0; i < module->expr.len; ++i) args[i] = module->expr.elems[i];
    args[4] = atom_expr3(arena, atom_symbol(arena, "LCons"), theory, args[4]);
    return atom_expr(arena, args, module->expr.len);
}

static bool assemble(Arena *arena, Atom *module, const char *theory, Atom *arguments,
    const char *instance, Atom **out, Atom **refusal) {
    return cetta_module_value_v1_assemble(arena, module, atom_string(arena, theory), arguments,
        atom_symbol(arena, "LNil"), CETTA_GSLT_ORIGIN_VIEW_V1, atom_string(arena, instance), 100000u, out, refusal);
}

static size_t declaration_count(Atom *p) {
    if (!module_form_v1(p, "ModulePresentationV1", 5u)) return SIZE_MAX;
    size_t n = 0; Atom *list = p->expr.elems[3];
    while (module_form_v1(list, "LCons", 2u)) { ++n; list = list->expr.elems[2]; }
    return atom_is_symbol(list, "LNil") ? n : SIZE_MAX;
}

static bool theory_controls(Arena *arena, Atom *module) {
    bool ok = true; Atom *nil = atom_symbol(arena, "LNil"), *p = NULL, *q = NULL, *failure = NULL;
    bool first = assemble(arena, module, "Simple", nil, "first", &p, &failure);
    ok &= check(first && declaration_count(p) == 1u, "an authored theory reaches a finite presentation");
    if (!first) return false;
    ok &= check(assemble(arena, module, "Simple", nil, "second", &q, &failure) &&
        !atom_eq(p->expr.elems[3], q->expr.elems[3]), "distinct instance keys produce distinct declaration identities");
    const char *operations[] = {"ModuleJoinV1", "ModuleMeetV1", "ModuleDifferenceV1"};
    for (size_t i = 0; i < 3u; ++i) {
        Atom *reference_args[] = {atom_string(arena, "s")};
        Atom *ref = module_make_v1(arena, "ModuleRefV1", reference_args, 1u);
        Atom *op_args[] = {ref, ref}, *apply_args[] = {atom_string(arena, "Simple"), nil};
        Atom *let_args[] = {reference_args[0], module_make_v1(arena, "ModuleApplyV1", apply_args, 2u),
            module_make_v1(arena, operations[i], op_args, 2u)};
        Atom *m = add_theory(arena, module, "Shared", nil, module_make_v1(arena, "ModuleLetV1", let_args, 3u));
        ok &= check(assemble(arena, m, "Shared", nil, "shared", &q, &failure) && declaration_count(q) == (i == 2u ? 0u : 1u), operations[i]);
    }
    Atom *twice = data(arena, "(ModuleJoinV1 (ModuleApplyV1 \"Simple\" LNil) (ModuleApplyV1 \"Simple\" LNil))");
    Atom *m = add_theory(arena, module, "Twice", nil, twice), *sentinel = atom_symbol(arena, "unchanged"), *out = sentinel;
    failure = NULL;
    ok &= check(!assemble(arena, m, "Twice", nil, "twice", &out, &failure) && out == sentinel &&
        module_form_v1(failure, "ModuleRefusedV1", 3u) && atom_is_symbol(failure->expr.elems[1], "declaration-conflict"),
        "two fresh calls do not accidentally share their same-named declarations");
    Atom *parameters = data(arena, "(LCons (ModuleParameterV1 \"s\" \"Simple\") LNil)");
    m = add_theory(arena, module, "Inherited", parameters, data(arena, "(ModuleRefV1 \"s\")"));
    Atom *actual = atom_expr3(arena, atom_symbol(arena, "LCons"), p, nil);
    ok &= check(assemble(arena, m, "Inherited", actual, "inherited", &q, &failure) &&
        atom_eq(p->expr.elems[3], q->expr.elems[3]), "supplied argument identities survive application");
    out = sentinel; failure = NULL;
    ok &= check(!assemble(arena, m, "Inherited", nil, "bad-arguments", &out, &failure) && out == sentinel &&
        atom_is_symbol(failure->expr.elems[1], "argument-count"), "bad argument count is a structured atomic refusal");
    m = add_theory(arena, m, "Freed", nil, data(arena, "(ModuleFreeV1 \"Inherited\")"));
    ok &= check(assemble(arena, m, "Freed", nil, "free", &q, &failure) && declaration_count(q) == 1u,
        "free recursively assembles finite parameter dependencies");
    m = add_theory(arena, module, "Cycle", nil, data(arena, "(ModuleApplyV1 \"Cycle\" LNil)"));
    out = sentinel; failure = NULL;
    ok &= check(!assemble(arena, m, "Cycle", nil, "cycle", &out, &failure) && out == sentinel &&
        atom_is_symbol(failure->expr.elems[1], "cyclic-theory-application"), "recursive theory application cannot masquerade as finite assembly");
    Atom *rename = data(arena,
        "(ModuleReplaceV1 (ModuleRefV1 \"s\") (LCons (ModuleReplacementV1 LNil \"A\" (TBase \"T\") "
        "(ModuleDeclarationV1 99 (ModuleTermV1 \"C\" (TBase \"T\") (LCons (LbnfTerminalV1 \"c\") LNil) False))) LNil))");
    m = add_theory(arena, module, "Renamed", parameters, rename);
    ok &= check(assemble(arena, m, "Renamed", actual, "rename", &q, &failure) && declaration_count(q) == 1u &&
        atom_eq(q->expr.elems[3]->expr.elems[1]->expr.elems[1], p->expr.elems[3]->expr.elems[1]->expr.elems[1]) &&
        atom_eq(q->expr.elems[3]->expr.elems[1]->expr.elems[3]->expr.elems[1], atom_string(arena, "C")),
        "replacement changes notation and constructor name while preserving origin");
    return ok;
}

static bool interpretation_controls(Arena *arena, Atom *module) {
    Atom *nil = atom_symbol(arena, "LNil"), *p = NULL, *failure = NULL, *result = NULL;
    Atom *method = atom_symbol(arena, "IdentityInclusionV1");
    bool ok = check(assemble(arena, module, "Simple", nil, "interface", &p, &failure),
        "interface source assembles");
    if (!p) return false;
    ok &= check(cetta_module_value_v1_interpret(arena, p, p, method, 100000u, &result, &failure) &&
        module_form_v1(result, "ModuleInterpretationFoundV1", 4u), "identity method produces a checked correspondence");
    Atom *complete = result;
    Atom *empty = data(arena, "(ModulePresentationV1 \"empty\" OriginViewV1 LNil LNil LNil)");
    ok &= check(cetta_module_value_v1_interpret(arena, p, empty, method, 100000u, &result, &failure) &&
        module_form_v1(result, "ModuleInterpretationUnknownV1", 2u), "missing declarations are unknown to the method, not a universal rejection");
    ok &= check(cetta_module_value_v1_interpret(arena, p, p, atom_symbol(arena, "FutureMethodV1"),
        100000u, &result, &failure) && module_form_v1(result, "ModuleInterpretationUnknownV1", 2u) &&
        atom_is_symbol(result->expr.elems[2], "method-not-implemented"), "the method vocabulary remains open");
    bool budget_ok = true;
    for (uint32_t budget = 0u; budget < 256u; ++budget) {
        Atom *sentinel = atom_symbol(arena, "untouched"), *attempt = sentinel, *reason = NULL;
        bool found = cetta_module_value_v1_interpret(arena, p, p, method, budget, &attempt, &reason);
        if (!found) budget_ok &= attempt == sentinel && reason != NULL;
        else if (module_form_v1(attempt, "ModuleInterpretationFoundV1", 4u)) budget_ok &= atom_eq(attempt, complete);
        else budget_ok &= module_form_v1(attempt, "ModuleInterpretationUnknownV1", 2u) &&
            !module_form_v1(attempt->expr.elems[2], "ModuleUnmatchedDeclarationV1", 3u);
    }
    ok &= check(budget_ok, "every short budget preserves atomic output and never invents a missing declaration");
    Atom *rule = data(arena, "(ModuleRewriteV1 \"Step\" LNil (PApp \"A\" LNil) (PApp \"A\" LNil))");
    Atom *first_fields[] = {data(arena, "(SyntaxOriginV1 \"rules\" \"first\")"), atom_symbol(arena, "first"), rule};
    Atom *second_fields[] = {data(arena, "(SyntaxOriginV1 \"rules\" \"second\")"), atom_symbol(arena, "second"), rule};
    Atom *first = module_make_v1(arena, "ModuleEntryV1", first_fields, 3u);
    Atom *second = module_make_v1(arena, "ModuleEntryV1", second_fields, 3u);
    Atom *one_fields[] = {p->expr.elems[1], p->expr.elems[2],
        atom_expr3(arena, atom_symbol(arena, "LCons"), first, p->expr.elems[3]), nil, nil};
    Atom *one = module_make_v1(arena, "ModulePresentationV1", one_fields, 5u);
    one_fields[2] = atom_expr3(arena, atom_symbol(arena, "LCons"), second, one_fields[2]);
    Atom *two = module_make_v1(arena, "ModulePresentationV1", one_fields, 5u);
    ok &= check(cetta_module_value_v1_interpret(arena, two, one, method, 100000u, &result, &failure) &&
        module_form_v1(result, "ModuleInterpretationUnknownV1", 2u), "one rule occurrence cannot witness two required occurrences");
    ok &= check(cetta_module_value_v1_interpret(arena, one, two, method, 100000u, &result, &failure) &&
        module_form_v1(result, "ModuleInterpretationFoundV1", 4u), "additional rule occurrences are permitted without claiming conservativity");
    Arena source, target;
    arena_init(&source); arena_init(&target);
    Atom *input = atom_deep_copy(&source, p), *owned = NULL, *reason = NULL;
    bool held = cetta_module_value_v1_interpret(&target, input, input, method, 100000u, &owned, &reason);
    arena_free(&source);
    ok &= check(held && atom_eq(owned, complete), "interpretation evidence survives source arena teardown");
    arena_free(&target);
    return ok;
}

static bool language_controls(Arena *arena, Atom *module) {
    bool ok = true; Atom *nil = atom_symbol(arena, "LNil"), *p = NULL, *failure = NULL;
    Atom *body = data(arena,
        "(ModuleAddRewritesV1 (ModuleAddEquationsV1 (ModuleAddTermsV1 ModuleEmptyV1 "
        "(LCons (ModuleDeclarationV1 0 (ModuleTermV1 \"A\" (TBase \"T\") (LCons (LbnfTerminalV1 \"a\") LNil) False)) "
        "(LCons (ModuleDeclarationV1 1 (ModuleTermV1 \"B\" (TBase \"T\") (LCons (LbnfTerminalV1 \"b\") LNil) False)) "
        "(LCons (ModuleDeclarationV1 2 (ModuleTermV1 \"Pair\" (TBase \"T\") "
        "(LCons (LbnfTerminalV1 \"(\") (LCons (ModuleNonTerminalV1 (TBase \"T\")) "
        "(LCons (LbnfTerminalV1 \",\") (LCons (ModuleNonTerminalV1 (TBase \"T\")) (LCons (LbnfTerminalV1 \")\") LNil))))) False)) LNil)))) "
        "(LCons (ModuleDeclarationV1 3 (ModuleEquationV1 \"commutes\" LNil "
        "(PApp \"Pair\" (LCons (FVar \"x\") (LCons (FVar \"y\") LNil))) "
        "(PApp \"Pair\" (LCons (FVar \"y\") (LCons (FVar \"x\") LNil))))) LNil)) "
        "(LCons (ModuleDeclarationV1 4 (ModuleRewriteV1 \"Step\" LNil (PApp \"A\" LNil) (PApp \"B\" LNil))) "
        "(LCons (ModuleDeclarationV1 5 (ModuleRewriteV1 \"UnderPair\" (LCons (ModuleCongruenceNamesV1 \"src\" \"tgt\") LNil) "
        "(PApp \"Pair\" (LCons (FVar \"src\") (LCons (FVar \"other\") LNil))) "
        "(PApp \"Pair\" (LCons (FVar \"tgt\") (LCons (FVar \"other\") LNil))))) LNil)))");
    Atom *m = body ? add_theory(arena, module, "Laws", nil, body) : NULL;
    bool built = m && assemble(arena, m, "Laws", nil, "laws", &p, &failure);
    ok &= check(built && declaration_count(p) == 6u, "complete authored terms, equations and conditional rewrites assemble");
    if (!built) return false;
    CettaLbnfLoweredV1 syntax = {0}; Atom *language = NULL;
    Atom *core_first = NULL;
    ok &= check(cetta_module_value_v1_core(arena, p, atom_string(arena, "Laws"), 100000u,
        &core_first, &failure), "core construction does not request a parser or lexical policy");
    bool lowered = cetta_module_value_v1_language(arena, p, atom_string(arena, "Laws"), atom_string(arena, "T"),
        data(arena, "(SyntaxLexicalPolicyV1 bnfc-documented)"), 100000u, &syntax, &language, &failure);
    ok &= check(lowered && module_form_v1(language, "GSLTLanguageDefWireV1", 5u), "theory declarations lower to the existing LanguageDef carrier");
    if (!lowered) return false;
    ok &= check(core_first && atom_eq(core_first, language), "requesting syntax leaves the exact core unchanged");
    Atom *rule_view = NULL;
    bool viewed = cetta_module_value_v1_rewrite_rules(arena, p, atom_string(arena, "Laws"),
        100000u, &rule_view, &failure);
    ok &= check(viewed && module_form_v1(rule_view, "ModuleRewriteRulesV1", 2u) &&
        atom_eq(rule_view->expr.elems[1], core_first), "rule export retains the exact authoritative core");
    if (viewed) {
        ModuleValuesV1 occurrences = {0}; ModuleWorkV1 work = {.arena = arena, .remaining = 100000u};
        bool complete = module_unlist_v1(&work, rule_view->expr.elems[2], &occurrences);
        ok &= check(complete && occurrences.len == 2u &&
            module_form_v1(occurrences.items[0], "ModuleRuleOccurrenceV1", 2u) &&
            atom_eq(occurrences.items[0]->expr.elems[2], core_first->expr.elems[5]->expr.elems[1]) &&
            !atom_eq(occurrences.items[0]->expr.elems[1], occurrences.items[1]->expr.elems[1]),
            "rule export preserves order and distinct source occurrences");
        module_clear_v1(&occurrences);
    }
    Arena foreign; arena_init(&foreign);
    Atom *foreign_presentation = atom_deep_copy(&foreign, p), *owned_core = NULL;
    bool owned = cetta_module_value_v1_core(arena, foreign_presentation, atom_string(&foreign, "Laws"), 100000u,
        &owned_core, &failure);
    arena_free(&foreign);
    ok &= check(owned && atom_eq(owned_core, core_first), "core laws and signature survive the source arena's teardown");
    Atom *equations = data(arena,
        "(LCons (Equation \"commutes\" (LCons (TypeBinding \"x\" (TBase \"T\")) (LCons (TypeBinding \"y\" (TBase \"T\")) LNil)) "
        "LNil (PApp \"Pair\" (LCons (FVar \"x\") (LCons (FVar \"y\") LNil))) "
        "(PApp \"Pair\" (LCons (FVar \"y\") (LCons (FVar \"x\") LNil)))) LNil)");
    ok &= check(atom_eq(language->expr.elems[4], equations), "equations retain orientation, variables and inferred types as data");
    Atom *rewrites = data(arena,
        "(LCons (RewriteRule \"Step\" LNil LNil (PApp \"A\" LNil) (PApp \"B\" LNil)) "
        "(LCons (RewriteRule \"UnderPair\" "
        "(LCons (TypeBinding \"src\" (TBase \"T\")) (LCons (TypeBinding \"other\" (TBase \"T\")) (LCons (TypeBinding \"tgt\" (TBase \"T\")) LNil))) "
        "(LCons (Congruence (FVar \"src\") (FVar \"tgt\")) LNil) "
        "(PApp \"Pair\" (LCons (FVar \"src\") (LCons (FVar \"other\") LNil))) "
        "(PApp \"Pair\" (LCons (FVar \"tgt\") (LCons (FVar \"other\") LNil)))) LNil))");
    ok &= check(atom_eq(language->expr.elems[5], rewrites), "conditional rewrites bind premise results without introducing arbitrary RHS variables");
    Atom *bad = data(arena,
        "(ModuleAddRewritesV1 (ModuleApplyV1 \"Simple\" LNil) "
        "(LCons (ModuleDeclarationV1 9 (ModuleRewriteV1 \"Bad\" LNil (PApp \"A\" LNil) (FVar \"fresh\"))) LNil))");
    m = add_theory(arena, module, "Bad", nil, bad); p = NULL;
    built = assemble(arena, m, "Bad", nil, "bad", &p, &failure);
    Atom *sentinel = atom_symbol(arena, "untouched"); language = sentinel; failure = NULL;
    CettaLbnfLoweredV1 prior = syntax;
    ok &= check(built && !cetta_module_value_v1_language(arena, p, atom_string(arena, "Bad"), atom_string(arena, "T"),
        data(arena, "(SyntaxLexicalPolicyV1 bnfc-documented)"), 100000u, &syntax, &language, &failure) &&
        language == sentinel && syntax.language == prior.language && module_form_v1(failure, "ModuleRefusedV1", 3u) &&
        atom_is_symbol(failure->expr.elems[1], "unbound-law-variable"), "unbound rewrite variables cannot publish a partial LanguageDef or parser syntax");
    return ok;
}

static bool core_controls(Arena *arena, Atom *module) {
    bool ok = true;
    Atom *nil = atom_symbol(arena, "LNil"), *p = NULL, *language = NULL, *failure = NULL;
    ok &= check(cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_term_admit_data", 2u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_term_admit_data", 0u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_term_admit_data", 3u) &&
        cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_pattern_at_data", 3u) &&
        cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_pattern_at_data", 4u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_pattern_at_data", 2u) &&
        cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_term_at_data", 5u) &&
        cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_term_at_data", 6u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_term_at_data", 4u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_term_admit", 2u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_pattern_at", 4u) &&
        !cetta_syntax_gslt_data_arg_v1("__cetta_lib_language_def_rewrite_term_at", 6u),
        "data operands are protected while computed-input operations retain their contract");
    Atom *missing = data(arena,
        "(ModulePresentationV1 \"removed\" OriginViewV1 "
        "(LCons (ModuleEntryV1 (SyntaxOriginV1 \"law-source\" \"site:9\") "
        "(ModuleLawKeyV1 ModuleRewriteV1 \"UsesMissing\") "
        "(ModuleRewriteV1 \"UsesMissing\" LNil (PApp \"Missing\" LNil) (PApp \"Missing\" LNil))) LNil) LNil LNil)");
    Atom *unpublished = atom_symbol(arena, "unpublished-core"); language = unpublished;
    ok &= check(!cetta_module_value_v1_core(arena, missing, atom_string(arena, "Dangling"), 100000u,
        &language, &failure) && language == unpublished && module_form_v1(failure, "ModuleRefusedV1", 3u) &&
        atom_is_symbol(failure->expr.elems[1], "dangling-law-constructor") &&
        atom_eq(failure->expr.elems[2], data(arena, "(SyntaxOriginV1 \"law-source\" \"site:9\")")),
        "a removed constructor gives a positioned dependency refusal without publishing a core");
    failure = NULL;
    Atom *m = add_theory(arena, module, "Empty", nil, atom_symbol(arena, "ModuleEmptyV1"));
    bool built = assemble(arena, m, "Empty", nil, "empty", &p, &failure);
    ok &= check(built && cetta_module_value_v1_core(arena, p, atom_string(arena, "Empty"), 100000u, &language, &failure) &&
        atom_eq(language, data(arena, "(GSLTLanguageDefWireV1 \"Empty\" LNil LNil LNil LNil)")),
        "an empty signature is a core language, not an empty-grammar parser error");
    m = add_theory(arena, module, "Signature", nil,
        data(arena, "(ModuleAddExportsV1 ModuleEmptyV1 (LCons (ModuleExportV1 (TBase \"Opaque\") (TBase \"Opaque\")) LNil))"));
    built = assemble(arena, m, "Signature", nil, "signature", &p, &failure);
    ok &= check(built && cetta_module_value_v1_core(arena, p, atom_string(arena, "Signature"), 100000u, &language, &failure) &&
        atom_eq(language, data(arena, "(GSLTLanguageDefWireV1 \"Signature\" (LCons (TypeDecl \"Opaque\" CarrierAst) LNil) LNil LNil LNil)")),
        "an exported sort does not require a concrete production");
    Atom *sentinel = atom_symbol(arena, "core-unchanged");
    language = sentinel; failure = NULL;
    CettaLbnfLoweredV1 syntax = {.language = sentinel};
    ok &= check(built && !cetta_module_value_v1_language(arena, p, atom_string(arena, "Signature"), atom_string(arena, "Opaque"),
        data(arena, "(SyntaxLexicalPolicyV1 bnfc-documented)"), 100000u, &syntax, &language, &failure) &&
        language == sentinel && syntax.language == sentinel && module_form_v1(failure, "LbnfRefusedV1", 3u) &&
        atom_is_symbol(failure->expr.elems[1], "empty-grammar"), "parser refusal cannot erase an independently constructed core");
    m = add_theory(arena, module, "OpaqueArgument", nil, data(arena,
        "(ModuleAddTermsV1 ModuleEmptyV1 (LCons (ModuleDeclarationV1 0 (ModuleTermV1 \"Wrap\" (TBase \"T\") "
        "(LCons (ModuleNonTerminalV1 (TBase \"Opaque\")) LNil) False)) LNil))"));
    built = assemble(arena, m, "OpaqueArgument", nil, "opaque-argument", &p, &failure);
    ok &= check(built && cetta_module_value_v1_core(arena, p, atom_string(arena, "OpaqueArgument"), 100000u, &language, &failure) &&
        module_form_v1(language->expr.elems[3]->expr.elems[1], "GrammarRule", 5u),
        "a constructor may have a declared argument sort without a syntax production");
    m = add_theory(arena, module, "Lists", nil, data(arena,
        "(ModuleAddTermsV1 ModuleEmptyV1 (LCons (ModuleDeclarationV1 0 (ModuleTermV1 \"A\" (TBase \"T\") "
        "(LCons (LbnfTerminalV1 \"a\") LNil) False)) (LCons (ModuleDeclarationV1 1 "
        "(ModuleListMacroV1 separator (TBase \"T\") \",\" False)) LNil)))"));
    built = assemble(arena, m, "Lists", nil, "lists", &p, &failure);
    Atom *listed = NULL, *listed_with_syntax = NULL;
    ok &= check(built && cetta_module_value_v1_core(arena, p, atom_string(arena, "Lists"), 100000u, &listed, &failure) &&
        cetta_module_value_v1_language(arena, p, atom_string(arena, "Lists"), atom_string(arena, "[T]"),
        data(arena, "(SyntaxLexicalPolicyV1 bnfc-documented)"), 100000u, &syntax, &listed_with_syntax, &failure) &&
        atom_eq(listed, listed_with_syntax), "shared list notation expands identically with and without parser preparation");
    m = add_theory(arena, module, "Token", nil, data(arena,
        "(ModuleAddTermsV1 ModuleEmptyV1 (LCons (ModuleDeclarationV1 0 (LbnfTokenV1 \"Raw\" (LbnfRegLetterV1) True)) LNil))"));
    built = assemble(arena, m, "Token", nil, "token", &p, &failure);
    ok &= check(built && cetta_module_value_v1_core(arena, p, atom_string(arena, "Token"), 100000u, &language, &failure) &&
        atom_eq(language, data(arena, "(GSLTLanguageDefWireV1 \"Token\" (LCons (TypeDecl \"Raw\" CarrierTokenRaw) LNil) LNil LNil LNil)")),
        "a token sort is core data before any lexical automaton is constructed");
    failure = NULL; language = sentinel;
    ok &= check(!cetta_module_value_v1_core(arena, p, atom_string(arena, "Budget"), 0u, &language, &failure) &&
        language == sentinel && module_form_v1(failure, "ModuleRefusedV1", 3u) &&
        atom_is_symbol(failure->expr.elems[1], "incomplete"), "core exhaustion is an atomic incomplete outcome");
    failure = NULL;
    ok &= check(!cetta_module_value_v1_core(arena, data(arena,
        "(ModulePresentationV1 \"bad\" OriginViewV1 (LCons (ModuleEntryV1 broken key value) LNil) LNil LNil)"),
        atom_string(arena, "Bad"), 100000u, &language, &failure) && language == sentinel &&
        module_form_v1(failure, "ModuleRefusedV1", 3u) && atom_is_symbol(failure->expr.elems[1], "malformed-entry"),
        "malformed origins cannot publish an authoritative core");
    return ok;
}

static bool derived_category_controls(Arena *arena) {
    bool ok = true;
    const char *categories[] = {
        "(ModuleProductCategoryV1 LNil)",
        "(ModuleProductCategoryV1 (LCons (TBase \"T\") LNil))",
        "(ModuleProductCategoryV1 (LCons (TBase \"T\") (LCons (TBase \"T\") LNil)))",
        "(ModuleProductCategoryV1 (LCons (TBase \"T\") (LCons (TBase \"U\") LNil)))",
        "(ModuleProductCategoryV1 (LCons (TBase \"U\") (LCons (TBase \"T\") LNil)))",
        "(ModuleProductCategoryV1 (LCons (TBase \"T:1:U\") LNil))",
        "(ModuleProductCategoryV1 (LCons (TBase \"T1\") LNil))",
        "(ModuleProductCategoryV1 (LCons (TBase \"λ\") LNil))"
    };
    Atom *names[sizeof(categories) / sizeof(categories[0])];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        ModuleWorkV1 work = {.arena = arena, .remaining = 1000u};
        names[i] = module_category_name_v1(&work, data(arena, categories[i]));
        ok &= check(names[i] && !work.refusal, "a finite tuple category has a portable ordinary-sort name");
        for (size_t j = 0; j < i; ++j)
            ok &= check(names[i] && names[j] && !atom_eq(names[i], names[j]),
                "tuple names retain arity, order, repeated slots and delimiter-bearing component identities");
    }
    ModuleWorkV1 work = {.arena = arena, .remaining = 1000u};
    ok &= check(!module_category_name_v1(&work, data(arena, "(TBase \"$module:tuple:0;\")")) &&
        module_form_v1(work.refusal, "ModuleRefusedV1", 3u) &&
        atom_is_symbol(work.refusal->expr.elems[1], "derived-category-collision"),
        "ordinary authored sorts cannot capture the derived tuple namespace");

    Arena source; arena_init(&source);
    Atom *presentation = data(&source,
        "(ModulePresentationV1 \"tuple-source\" OriginViewV1 "
        "(LCons (ModuleEntryV1 (SyntaxOriginV1 \"source\" \"atom\") key "
        "(ModuleTermV1 \"A\" (TBase \"T\") (LCons (LbnfTerminalV1 \"a\") LNil) False)) "
        "(LCons (ModuleEntryV1 (SyntaxOriginV1 \"source\" \"pair\") key "
        "(ModuleTermV1 \"Pair\" (ModuleProductCategoryV1 (LCons (TBase \"T\") (LCons (TBase \"T\") LNil))) "
        "(LCons (ModuleNonTerminalV1 (TBase \"T\")) (LCons (ModuleNonTerminalV1 (TBase \"T\")) LNil)) False)) LNil)) LNil LNil)");
    Atom *sentinel = atom_symbol(arena, "unpublished-tuple"), *core = sentinel, *failure = NULL;
    bool built = cetta_module_value_v1_core(arena, presentation, atom_string(arena, "Tuple"),
        100000u, &core, &failure);
    ok &= check(built && module_form_v1(core, "GSLTLanguageDefWireV1", 5u),
        "tuple notation reaches the authoritative core without a parser compiler");
    Atom *expected = built ? atom_deep_copy(arena, core) : NULL;
    bool budgets_ok = true, refused = false, completed = false;
    for (uint32_t budget = 0u; budget < 256u; ++budget) {
        Atom *out = sentinel; failure = NULL;
        if (cetta_module_value_v1_core(arena, presentation, atom_string(arena, "Tuple"), budget, &out, &failure)) {
            completed = true;
            budgets_ok &= expected && atom_eq(out, expected);
        } else {
            refused = true;
            budgets_ok &= out == sentinel &&
                ((module_form_v1(failure, "ModuleRefusedV1", 3u) &&
                  atom_is_symbol(failure->expr.elems[1], "incomplete")) ||
                 (module_form_v1(failure, "LbnfRefusedV1", 3u) &&
                  atom_is_symbol(failure->expr.elems[1], "resource-limit")));
        }
    }
    ok &= check(budgets_ok && refused && completed,
        "every tuple elaboration budget boundary either completes identically or refuses atomically");
    Atom *entries = presentation->expr.elems[3];
    Atom *malformed = data(&source,
        "(ModuleEntryV1 (SyntaxOriginV1 \"source\" \"malformed\") key "
        "(ModuleTermV1 7 (TBase \"T\") LNil False))");
    entries->expr.elems[2]->expr.elems[2] = atom_expr3(&source, atom_symbol(&source, "LCons"),
        malformed, atom_symbol(&source, "LNil"));
    Atom *out = sentinel; failure = NULL;
    ok &= check(!cetta_module_value_v1_core(arena, presentation, atom_string(arena, "Bad"), 100000u,
        &out, &failure) && out == sentinel && module_form_v1(failure, "ModuleRefusedV1", 3u),
        "a malformed sibling constructor cannot crash tuple freshness checking or publish a core");
    arena_free(&source);
    ok &= check(built && expected && atom_eq(core, expected),
        "derived tuple core storage survives source arena teardown");
    return ok;
}

static bool layer_controls(Arena *arena) {
    bool ok = true;
    Arena source; arena_init(&source);
    CettaLdTypeDeclV1 type = {.name = {.bytes = (uint8_t *)"T", .len = 1u},
        .carrier = CETTA_LD_CARRIER_AST_V1};
    CettaLdGrammarRuleV1 term = {.label = {.bytes = (uint8_t *)"A", .len = 1u},
        .category = {.bytes = (uint8_t *)"T", .len = 1u}};
    CettaLanguageDefCoreV1 authority = {.types = &type, .type_len = 1u, .terms = &term, .term_len = 1u};
    Atom *schema = data(&source, "(GSLTLanguageDefWireV1 \"Schema\" (LCons (TypeDecl \"T\" CarrierAst) LNil) "
        "(LCons (GrammarRule \"A\" \"T\" LNil (LCons (SyntaxTerminal \"a\") LNil) EvalNone) LNil) LNil LNil)");
    Atom *base = data(&source, "(GSLTLanguageDefWireV1 \"Base\" LNil LNil LNil LNil)");
    Atom *key = data(&source, "(SyntaxOriginV1 \"schema\" \"fields\")");
    Atom *fields = data(&source, "(LCons (A) (LCons (A) LNil))");
    Atom *layer = cetta_module_attach_layer_v1(arena, base, key, schema, &authority,
        atom_string(&source, "T"), fields);
    Atom *unknown = cetta_module_attach_layer_v1(arena, base, key, schema, &authority,
        atom_string(&source, "Unknown"), atom_symbol(&source, "LNil"));
    ok &= check(module_form_v1(unknown, "ModuleRefusedV1", 3u),
        "an empty field batch cannot evade declared-category checking");
    Atom *bad = data(&source, "(ModuleExtendedLanguageV1 (GSLTLanguageDefWireV1 \"Base\" LNil LNil LNil LNil) "
        "(LCons (ModuleLayerV1 (SyntaxOriginV1 \"bad\" \"fields\") "
        "(GSLTLanguageDefWireV1 \"Schema\" LNil LNil LNil LNil) \"T\" not-a-list) LNil))");
    Atom *bad_observation = cetta_module_base_core_v1(arena, bad);
    ok &= check(module_form_v1(bad_observation, "ModuleRefusedV1", 3u),
        "raw layer decoding refuses a malformed physical field list");
    Atom *transported = cetta_module_join_layers_over_v1(arena, schema, layer, layer);
    Atom *bad_target = cetta_module_join_layers_over_v1(arena,
        atom_symbol(&source, "BadTarget"), layer, layer);
    ok &= check(module_form_v1(bad_target, "ModuleRefusedV1", 3u),
        "explicit layer transport refuses a malformed target envelope");
    arena_free(&source);
    Atom *retained = cetta_module_layer_fields_v1(arena, layer,
        data(arena, "(SyntaxOriginV1 \"schema\" \"fields\")"));
    ok &= check(module_form_v1(layer, "ModuleExtendedLanguageV1", 2u) &&
        atom_eq(retained, data(arena, "(LCons (A) (LCons (A) LNil))")),
        "layer schemas, origins and duplicate field occurrences survive source-arena teardown");
    Atom *shared = cetta_module_join_layers_v1(arena, layer, layer);
    ok &= check(atom_eq(shared, layer), "the same layer occurrence is shared without duplicating its fields");
    ok &= check(module_form_v1(transported, "ModuleExtendedLanguageV1", 2u) &&
        atom_eq(transported->expr.elems[2], layer->expr.elems[2]) &&
        atom_eq(transported->expr.elems[1],
            layer->expr.elems[2]->expr.elems[1]->expr.elems[2]),
        "transport owns the target core and retains the exact shared rows after source teardown");
    return ok;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena input, output;
    arena_init(&input); arena_init(&output);
    bool ok = true;
    Atom *a[] = {token(&input, "Example")};
    Atom *name = ast(&input, "NameVar", a, 1u);
    Atom *theory = theory_ast(&input, "Simple");
    a[0] = theory;
    Atom *args[] = {list_ast(&input, NULL, 0u), name, list_ast(&input, a, 1u)};
    Atom *source = ast(&input, "ModuleImpl", args, 3u), *value = NULL, *refusal = NULL;
    ok &= check(cetta_module_value_v1_project(&output, atom_string(&input, "example-source"),
        source, 10000u, &value, &refusal), "project a theory through the labelled reader carrier");
    Atom *expected = data(&output,
        "(ModuleValueV1 \"example-source\" \"Example\" LNil "
        "(LCons (ModuleTheoryV1 \"Simple\" LNil "
        "(ModuleAddTermsV1 ModuleEmptyV1 "
        "(LCons (ModuleDeclarationV1 0 (ModuleTermV1 \"A\" (TBase \"T\") "
        "(LCons (LbnfTerminalV1 \"a\") LNil) False)) LNil))) LNil) LNil LNil "
        "(LCons (ModuleSourceSiteV1 0 term (LbnfSourceSpanV1 0 10 0 10)) LNil))");
    ok &= check(value && expected && atom_eq(value, expected), "compact declarations and source map have independent expected values");
    if (value) ok &= theory_controls(&output, value);
    if (value) ok &= interpretation_controls(&output, value);
    if (value) ok &= language_controls(&output, value);
    if (value) ok &= core_controls(&output, value);
    ok &= derived_category_controls(&output);
    ok &= layer_controls(&output);
    Atom *sentinel = atom_symbol(&output, "sentinel"), *unchanged = sentinel;
    ok &= check(!cetta_module_value_v1_project(&output, atom_string(&input, "example-source"),
        source, 0u, &unchanged, &refusal) && unchanged == sentinel &&
        module_form_v1(refusal, "ModuleRefusedV1", 3u) &&
        atom_is_symbol(refusal->expr.elems[1], "incomplete"), "exhaustion is explicit and publication is atomic");
    Atom *twice[] = {theory, theory};
    args[2] = list_ast(&input, twice, 2u);
    Atom *duplicate = ast(&input, "ModuleImpl", args, 3u);
    refusal = NULL;
    ok &= check(!cetta_module_value_v1_project(&output, atom_string(&input, "example-source"),
        duplicate, 10000u, &unchanged, &refusal) && unchanged == sentinel &&
        atom_is_symbol(refusal->expr.elems[1], "duplicate-theory"), "duplicate theory names cannot silently overwrite");
    Atom *asts[] = {source, source};
    Atom *accept_args[] = {atom_symbol(&input, "parser"), atom_symbol(&input, "binding"),
        atom_symbol(&input, "source"), atom_expr(&input, asts, 2u)};
    Atom *ambiguous = module_make_v1(&input, "LangDef:AuthoredParseAccepted", accept_args, 4u);
    Atom *result = NULL;
    ok &= check(cetta_module_value_v1_finish_read(&output, atom_string(&input, "example-source"),
        ambiguous, 10000u, &result, &refusal) && module_form_v1(result, "ModuleAmbiguousReadV1", 1u) &&
        atom_eq(result->expr.elems[1], ambiguous), "ambiguous readings retain every alternative");
    arena_free(&input);
    ok &= check(value && atom_eq(value, expected), "returned declarations survive source arena teardown");
    arena_free(&output);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    printf("(ModuleValuesV1Summary %u %u)\n", checks, ok ? 0u : 1u);
    return ok ? 0 : 1;
}
