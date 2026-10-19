#!/usr/bin/env python3
"""Module binding declarations, Pattern carriers and closed-term admission."""
import argparse
import re
from pathlib import Path

from test_parser_artifacts_v1 import run
from test_composable_syntax_v1 import form, lets, text
from test_module_values_v1 import HOSTILE_CONSTRUCTORS, rows
from test_lbnf_values_v1 import headed
from gslt2parse_schema_v1 import StringLiteral, Symbol, parse_sexprs

ROOT = Path(__file__).resolve().parents[1]


def controls(binary, language, output):
    def assemble(name):
        return form("module:assemble", "$module", text(name), "LNil", "LNil",
                    "OriginViewV1", text("bindings:" + name))

    def admit(value, category="Expr", authority="$authority"):
        return form("langdef:admit-pattern-data", authority, text(category), value)

    nested = '(PApp "Lam" (LCons (PLam BNone (PApp "Lam" (LCons (PLam BNone ' \
             '(PApp "Ref" (LCons (Var 1) LNil))) LNil))) LNil))'
    renamed = '(PApp "Abs" (LCons (PLam BNone (PApp "Ref" (LCons (Var 0) LNil))) LNil))'
    artifact = text(str(output / (language + "-bindings.tpp")))
    bindings = [
        ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
        ("$module", form("module:read-file", "$reader", text("binding-values"),
                         text(str(ROOT / "tests/module_values/BindingValues.module")))),
        ("$closed-reader", form("langdef:close-parser", "$reader")),
        ("$presentation", assemble("Syntax")),
        ("$core", form("module:core", "$presentation", text("Bindings"))),
        ("$language", form("module:language", "$presentation", text("Bindings"), text("Expr"),
                           "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$prepared-core", form("module:language-core", "$language")),
        ("$syntax", form("module:language-syntax", "$language")),
        ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
        ("$parsed", form("langdef:parser-parse-text", "$parser", text("lam x.lam y.ref(x)"))),
        ("$repeated", form("langdef:parser-parse-text", "$parser", text("lam x.lam y.ref(x)"))),
        ("$frozen", form("langdef:freeze-parser", "$parser", artifact)),
        ("$restored", form("langdef:load-parser-artifact", artifact)),
        ("$restored-parse", form("langdef:parser-parse-text", "$restored", text("lam x.lam y.ref(x)"))),
        ("$authority", form("langdef:load-language-value", "$core")),
        ("$admitted", admit(nested)),
        ("$wrong-sort", admit('(PApp "Lam" (LCons (PLam BNone (Var 0)) LNil))')),
        ("$escaped", admit('(PApp "Lam" (LCons (PLam BNone (PApp "Ref" (LCons (Var 1) LNil))) LNil))')),
        ("$free", admit('(FVar "x")')),
        ("$metadata", admit('(PApp "Lam" (LCons (PLam (BSome "x") (PApp "Zero" LNil)) LNil))')),
        ("$schema", admit('(PSubst (Var 0) (PApp "Zero" LNil))')),
        ("$renamed", assemble("Renamed")),
        ("$renamed-core", form("module:core", "$renamed", text("Renamed"))),
        ("$renamed-authority", form("langdef:load-language-value", "$renamed-core")),
        ("$renamed-admitted", admit(renamed, authority="$renamed-authority")),
        ("$missing", assemble("MissingBinder")),
        ("$missing-core", form("module:core", "$missing", text("Missing"))),
        ("$ambiguous", assemble("AmbiguousBinder")),
        ("$ambiguous-core", form("module:core", "$ambiguous", text("Ambiguous"))),
        ("$binding-tuple", assemble("BindingTuple")),
        ("$binding-tuple-core", form("module:core", "$binding-tuple", text("BindingTuple"))),
        ("$binding-law", assemble("BindingLaw")),
        ("$binding-law-core", form("module:core", "$binding-law", text("BindingLaw"))),
        ("$closed-authority", form("langdef:close-language", "$authority")),
        ("$closed-renamed", form("langdef:close-language", "$renamed-authority")),
        ("$closed-parser", form("langdef:close-parser", "$parser")),
        ("$closed-restored", form("langdef:close-parser", "$restored")),
    ]
    names = ("core", "prepared-core", "parsed", "repeated", "restored-parse", "admitted",
             "wrong-sort", "escaped", "free", "metadata", "schema", "renamed-core",
             "renamed-admitted", "missing-core", "ambiguous-core", "binding-tuple-core", "binding-law-core")
    record = "(ModuleBindingsV1 " + " ".join("$" + name for name in names) + ")"
    prefix = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n'
    body = "!" + lets(bindings, "(function (return " + record + "))") + "\n"
    results = [v for v in run(binary, language, output, "bindings", prefix + body)
               if headed(v, "ModuleBindingsV1", len(names))]
    if len(results) != 1:
        raise AssertionError((language, "binding result", results))
    result = dict(zip(names, results[0][1:]))
    checks = 0

    def expect(condition, message):
        nonlocal checks
        if not condition:
            raise AssertionError((language, message, result))
        checks += 1

    expect(headed(result["core"], "GSLTLanguageDefWireV1", 5), "source binding reaches core")
    expect(result["core"] == result["prepared-core"], "parser compilation does not change binding authority")
    binders = [r for r in rows(result["core"][3]) if r[1] == StringLiteral("Lam")]
    parameter = (Symbol("TermAbstractionNamed"), (Symbol("BSome"), StringLiteral("x")),
                 StringLiteral("argument:1"), (Symbol("TArrow"),
                 (Symbol("TBase"), StringLiteral("Name")), (Symbol("TBase"), StringLiteral("Expr"))))
    expect(len(binders) == 1 and rows(binders[0][3]) == [parameter],
           "spelling field becomes declared binder domain, not a second object argument")
    expect((Symbol("SyntaxNonTerminal"), StringLiteral("x")) in rows(binders[0][4]),
           "source binder spelling is retained as notation metadata")
    expect(headed(result["parsed"], "ParserAcceptedV1", 2), "binding concrete syntax parses")
    expect(result["parsed"] == result["repeated"] == result["restored-parse"], "live/repeated/restored parser agreement")
    expect(headed(result["admitted"], "LangDef:TermAdmitted", 0), "nested outer reference is typed")
    for key, status in (("wrong-sort", "type_mismatch"), ("escaped", "scope_mismatch"),
                        ("free", "scope_mismatch"), ("metadata", "representation_mismatch"),
                        ("schema", "representation_mismatch")):
        expect(headed(result[key], "LangDef:TermRejected", 2) and result[key][1] == Symbol(status), key)
    renamed_rows = [r for r in rows(result["renamed-core"][3]) if r[1] == StringLiteral("Abs")]
    expect(len(renamed_rows) == 1 and renamed_rows[0][3] == binders[0][3], "replacement retains binding domain and body")
    expect(headed(result["renamed-admitted"], "LangDef:TermAdmitted", 0), "renamed binding constructor remains usable")
    for key in ("missing-core", "ambiguous-core", "binding-tuple-core"):
        expect(headed(result[key], "ModuleRefusedV1", 3) and result[key][1] == Symbol("unresolved-binding-elaboration"), key)
    expect(headed(result["binding-law-core"], "GSLTLanguageDefWireV1", 5),
           "source binder fields lower to semantic abstraction arguments")
    hostile = HOSTILE_CONSTRUCTORS + '(= (PApp $h $a) corrupted-pattern)\n' \
        '(= (PLam $n $b) corrupted-lambda)\n(= (Var $n) corrupted-index)\n' \
        '(= (TermAbstractionNamed $n $b $t) corrupted-binding)\n'
    hostile_results = [v for v in run(binary, language, output, "bindings-hostile", prefix + hostile + body)
                       if headed(v, "ModuleBindingsV1", len(names))]
    expect(hostile_results == results, "host equations cannot reinterpret module or Pattern data")
    print(f"PASS: {language} binding core, admission, replacement, parser reuse and inert boundary ({checks})")
    return checks


def projections(binary, language, output):
    profile = '(LangDef:NamedTokensV1 (LCons "Name" LNil))'
    cases = {
        "outer": "lam x.lam y.ref(x)",
        "shadow": "lam x.lam x.ref(x)",
        "alpha": "lam a.lam b.ref(a)",
        "siblings": "pair(lam x.ref(x),lam y.ref(y))",
        "free": "lam x.ref(y)",
        "sibling-leak": "pair(lam x.ref(x),ref(x))",
        "rejected": "lam .",
    }
    artifact = text(str(output / (language + "-named-bindings.tpp")))
    bindings = [
        ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
        ("$module", form("module:read-file", "$reader", text("binding-values"),
                         text(str(ROOT / "tests/module_values/BindingValues.module")))),
        ("$closed-reader", form("langdef:close-parser", "$reader")),
        ("$theory", form("module:assemble", "$module", text("NamedSyntax"), "LNil", "LNil",
                         "OriginViewV1", text("named-bindings"))),
        ("$language", form("module:language", "$theory", text("NamedBindings"), text("Expr"),
                           "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$core", form("module:language-core", "$language")),
        ("$syntax", form("module:language-syntax", "$language")),
        ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
        ("$authority", form("langdef:load-language-value", "$core")),
        ("$deep-parse", form("langdef:parser-parse-text", "$parser", text("lam x." * 2100 + "z"))),
        ("$deep-projection", form("langdef:project-parser-pattern-result", "$authority", text("Expr"), profile, "$deep-parse")),
        ("$deep-term", form("langdef:projected-term", "$deep-projection", "0")),
        ("$deep-admission", form("langdef:admit-pattern-data", "$authority", text("Expr"), "$deep-term")),
    ]
    for key, source in cases.items():
        bindings += [
            ("$parse-" + key, form("langdef:parser-parse-text", "$parser", text(source))),
            ("$" + key, form("langdef:project-parser-pattern-result", "$authority", text("Expr"), profile, "$parse-" + key)),
        ]
    for key in ("outer", "shadow", "alpha", "siblings"):
        bindings += [
            ("$term-" + key, form("langdef:projected-term", "$" + key, "0")),
            ("$admit-" + key, form("langdef:admit-pattern-data", "$authority", text("Expr"), "$term-" + key)),
        ]
    bindings += [
        ("$missing-profile", form("langdef:project-parser-pattern-result", "$authority", text("Expr"),
                                  "(LangDef:NamedTokensV1 LNil)", "$parse-outer")),
        ("$duplicate-input", '(let (ParserAcceptedV1 ($tree) $span) $parse-outer '
                              '(function (return (ParserAcceptedV1 ($tree $tree) $span))))'),
        ("$duplicate", form("langdef:project-parser-pattern-result", "$authority", text("Expr"), profile, "$duplicate-input")),
        ("$bad-batch-input", '(let (ParserAcceptedV1 ($tree) $span) $parse-outer '
                              '(function (return (ParserAcceptedV1 ($tree malformed-ast) $span))))'),
        ("$bad-batch", form("langdef:project-parser-pattern-result", "$authority", text("Expr"), profile, "$bad-batch-input")),
        ("$frozen", form("langdef:freeze-parser", "$parser", artifact)),
        ("$restored", form("langdef:load-parser-artifact", artifact)),
        ("$parse-restored", form("langdef:parser-parse-text", "$restored", text(cases["outer"]))),
        ("$restored-projection", form("langdef:project-parser-pattern-result", "$authority", text("Expr"), profile, "$parse-restored")),
        ("$closed-authority", form("langdef:close-language", "$authority")),
        ("$closed-parser", form("langdef:close-parser", "$parser")),
        ("$closed-restored", form("langdef:close-parser", "$restored")),
    ]
    names = ("outer", "shadow", "alpha", "siblings", "free", "sibling-leak", "parse-rejected", "rejected",
             "term-outer", "term-shadow", "term-alpha", "term-siblings", "admit-outer", "admit-shadow",
             "admit-alpha", "admit-siblings", "missing-profile", "restored-projection", "duplicate", "bad-batch", "deep-admission")
    record = "(ModuleNamedProjectionV1 " + " ".join("$" + key for key in names) + ")"
    prefix = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n'
    body = "!" + lets(bindings, "(function (return " + record + "))") + "\n"
    result_rows = [v for v in run(binary, language, output, "named-projection", prefix + body)
                   if headed(v, "ModuleNamedProjectionV1", len(names))]
    if len(result_rows) != 1:
        raise AssertionError((language, "named projection record", result_rows))
    results = dict(zip(names, result_rows[0][1:]))
    count = 0

    def expect(condition, label):
        nonlocal count
        if not condition:
            raise AssertionError((language, label, results))
        count += 1

    outer = parse_sexprs('(PApp "Lam" (LCons (PLam BNone (PApp "Lam" (LCons '
                        '(PLam BNone (PApp "Ref" (LCons (Var 1) LNil))) LNil))) LNil))')[0]
    shadow = parse_sexprs('(PApp "Lam" (LCons (PLam BNone (PApp "Lam" (LCons '
                         '(PLam BNone (PApp "Ref" (LCons (Var 0) LNil))) LNil))) LNil))')[0]
    expect(results["term-outer"] == outer, "outer reference crosses one inner binder")
    expect(results["term-shadow"] == shadow, "nearest equal name shadows the outer binder")
    expect(results["term-alpha"] == outer, "alpha renaming retains canonical bound indices")
    for key in ("outer", "shadow", "alpha", "siblings"):
        expect(headed(results["admit-" + key], "LangDef:TermAdmitted", 0), "projected " + key + " is admitted")
        expect(headed(results[key], "LangDef:ProjectedTermsV1", 2), key + " has a projected alternative")
    for key in ("free", "sibling-leak"):
        expect(headed(results[key], "LangDef:ProjectionRejectedV1", 4) and results[key][1] == Symbol("scope_mismatch"),
               key + " cannot acquire an invented binding")
    expect(results["rejected"] == results["parse-rejected"], "parser outcome passes through unchanged")
    expect(headed(results["missing-profile"], "LangDef:ProjectionRejectedV1", 4) and
           results["missing-profile"][1] == Symbol("unsupported_profile"), "name meaning requires its explicit profile")
    expect(results["outer"] == results["restored-projection"], "restoration retains scope projection and spans")
    expect(headed(results["deep-admission"], "LangDef:TermIncompleteV1", 2) and
           results["deep-admission"][1] == Symbol("PatternAtomResourceLimit"), "admission depth bound is not a false semantic rejection")
    expect(headed(results["duplicate"], "LangDef:ProjectedTermsV1", 2) and
           results["duplicate"][1] == results["outer"][1] + results["outer"][1],
           "equal-valued distinct input alternatives retain multiplicity and order")
    expect(headed(results["bad-batch"], "LangDef:ProjectionRejectedV1", 4) and results["bad-batch"][2] == 1,
           "a malformed second alternative refuses the whole batch at its occurrence")
    sources = results["outer"][1][0][2]
    binders = [row for row in sources if headed(row, "LangDef:BinderSourceV1", 4)]
    expect(len(binders) == 2 and {row[2] for row in binders} == {StringLiteral("x"), StringLiteral("y")},
           "binder spelling occurrences and source spans survive semantic argument removal")
    expect(all(headed(row[3], "LbnfSourceSpanV1", 4) and headed(row[4], "LbnfTokenPositionV1", 2)
               for row in binders), "position tokens retain binder coordinates")
    hostile = HOSTILE_CONSTRUCTORS + '(= (PApp $h $a) corrupted-pattern)\n' \
        '(= (PLam $n $b) corrupted-lambda)\n(= (Var $n) corrupted-index)\n' \
        '(= (LangDef:NamedTokensV1 $x) corrupted-profile)\n'
    replay = [v for v in run(binary, language, output, "named-projection-hostile", prefix + hostile + body)
              if headed(v, "ModuleNamedProjectionV1", len(names))]
    expect(replay == result_rows, "host equations cannot evaluate profiles or projected patterns")
    example = (ROOT / "examples/module_binding_values_v1.metta").read_text().replace(
        '"../lib/lib_module.metta"', text(str(ROOT / "lib/lib_module.metta")))
    demo = [v for v in run(binary, language, output, "binding-example", example)
            if headed(v, "ModuleBindingExampleV1", 6)]
    expect(len(demo) == 1 and demo[0][1] == outer and headed(demo[0][2], "LangDef:TermAdmitted", 0) and
           headed(demo[0][3], "LangDef:ProjectionRejectedV1", 4) and demo[0][3][1] == Symbol("scope_mismatch") and
           headed(demo[0][5], "LangDef:ContextualResults", 2) and headed(demo[0][6], "LangDef:TermAdmitted", 0),
           "the checked-in example is a working public client")
    print(f"PASS: {language} named projection, scope, alpha, ownership and restoration ({count})")
    return count


def rewrites(binary, language, output):
    prefix = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n'
    bindings = [
        ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
        ("$module", form("module:read-file", "$reader", text("binding-values"),
                         text(str(ROOT / "tests/module_values/BindingValues.module")))),
        ("$reader-closed", form("langdef:close-parser", "$reader")),
    ]
    inputs = {
        "Copy": "lam x.ref(x)",
        "Weaken": "lam x.ref(x)",
        "Reference": "lam x.ref(x)",
        "WeakenRenamed": "fun x->ref(x)",
    }
    names = []
    for kind, source in inputs.items():
        block = [
            ("$theory", form("module:assemble", "$module", text("Binding" + kind), "LNil", "LNil",
                             "OriginViewV1", text("binding-rule:" + kind))),
            ("$language", form("module:language", "$theory", text(kind), text("Expr"),
                               "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            ("$core", form("module:language-core", "$language")),
            ("$syntax", form("module:language-syntax", "$language")),
            ("$authority", form("langdef:load-language-value", "$core")),
            ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
            ("$parsed", form("langdef:parser-parse-text", "$parser", text(source))),
            ("$projected", form("langdef:project-parser-pattern-result", "$authority", text("Expr"),
                                '(LangDef:NamedTokensV1 (LCons "Name" LNil))', "$parsed")),
            ("$term", form("langdef:projected-term", "$projected", "0")),
            ("$before-" + kind, form("langdef:admit-pattern-data", "$authority", text("Expr"), "$term")),
            ("$result-" + kind, form("langdef:rewrite-pattern-at-data", "$authority", "4", "100000",
                                    "(LangDef:RelationRows)", "$term")),
            ("$reduct", form("langdef:contextual-result-term", "$result-" + kind, "0")),
            ("$after-" + kind, form("langdef:admit-pattern-data", "$authority", text("Expr"), "$reduct")),
            ("$authority-closed", form("langdef:close-language", "$authority")),
            ("$parser-closed", form("langdef:close-parser", "$parser")),
        ]
        locals_ = {name for name, _ in block if not name.startswith(("$before-", "$result-", "$after-"))}
        def qualify(source):
            return re.sub(r'\$[A-Za-z][A-Za-z0-9_-]*',
                          lambda match: match[0] + "-" + kind if match[0] in locals_ else match[0], source)
        bindings += [(qualify(name), qualify(value)) for name, value in block]
        names += ["before-" + kind, "result-" + kind, "after-" + kind]
    bindings += [
        ("$bad-index", form("langdef:contextual-result-term", "$result-Copy", "1")),
        ("$negative-index", form("langdef:contextual-result-term", "$result-Copy", "-1")),
        ("$bad-envelope", form("langdef:contextual-result-term", "malformed-result", "0")),
    ]
    names += ["bad-index", "negative-index", "bad-envelope"]
    for kind in ("Escape", "Capture", "RepeatedContexts"):
        bindings += [
            ("$theory-" + kind, form("module:assemble", "$module", text("Binding" + kind), "LNil", "LNil",
                             "OriginViewV1", text("unresolved:" + kind))),
            ("$unresolved-" + kind, form("module:core", "$theory-" + kind, text(kind))),
        ]
        names += ["unresolved-" + kind]
    body = "!" + lets(bindings, "(function (return (ModuleBindingRewritesV1 " +
                      " ".join("$" + name for name in names) + ")))") + "\n"
    records = [v for v in run(binary, language, output, "binding-rewrites", prefix + body)
               if headed(v, "ModuleBindingRewritesV1", len(names))]
    if len(records) != 1:
        raise AssertionError((language, "binding rewrites record", records))
    values = dict(zip(names, records[0][1:]))
    expected = {
        "Copy": '(PApp "Lam" (LCons (PLam BNone (PApp "Pair" (LCons '
                '(PApp "Ref" (LCons (Var 0) LNil)) (LCons (PApp "Ref" (LCons (Var 0) LNil)) LNil)))) LNil))',
        "Weaken": '(PApp "Lam" (LCons (PLam BNone (PApp "Lam" (LCons '
                  '(PLam BNone (PApp "Ref" (LCons (Var 1) LNil))) LNil))) LNil))',
        "Reference": '(PApp "Lam" (LCons (PLam BNone (PApp "Zero" LNil)) LNil))',
        "WeakenRenamed": '(PApp "Abs" (LCons (PLam BNone (PApp "Abs" (LCons '
                         '(PLam BNone (PApp "Ref" (LCons (Var 1) LNil))) LNil))) LNil))',
    }
    count = 0

    def expect(condition, message):
        nonlocal count
        if not condition:
            raise AssertionError((language, message, values))
        count += 1

    for kind, wanted in expected.items():
        result = values["result-" + kind]
        expect(headed(values["before-" + kind], "LangDef:TermAdmitted", 0), kind + " projected input is typed")
        expect(headed(result, "LangDef:ContextualResults", 2) and
               result[1] == Symbol("LangDef:ContextComplete") and
               headed(result[2], "LangDef:ContextualResult", 2) and result[2][1] == parse_sexprs(wanted)[0],
               kind + " exact scoped reduct")
        expect(headed(values["after-" + kind], "LangDef:TermAdmitted", 0), kind + " output remains typed and closed")
    for kind in ("Escape", "Capture", "RepeatedContexts"):
        value = values["unresolved-" + kind]
        expect(headed(value, "ModuleRefusedV1", 3) and value[1] == Symbol("unresolved-scope-transport"),
               kind + " needs a further scope interpretation, not an unsound inferred correspondence")
    for name in ("bad-index", "negative-index", "bad-envelope"):
        expect(headed(values[name], "LangDef:ContextualFault", 2) and values[name][1] == Symbol("bad_argument"),
               name + " cannot select or fabricate a result")
    hostile = HOSTILE_CONSTRUCTORS + '(= (PApp $h $a) corrupted-pattern)\n' \
        '(= (PLam $n $b) corrupted-lambda)\n(= (Var $n) corrupted-index)\n'
    replay = [v for v in run(binary, language, output, "binding-rewrites-hostile", prefix + hostile + body)
              if headed(v, "ModuleBindingRewritesV1", len(names))]
    expect(replay == records, "host equations cannot execute guest binding rules or reinterpret their patterns")
    print(f"PASS: {language} binding module -> parser -> projection -> admission -> explicit scoped rewrite ({count})")
    return count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/module-bindings-public")
    parser.add_argument("--language", choices=("he", "petta", "prime"), action="append")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks = sum(controls(args.cetta.resolve(), lang, output) + projections(args.cetta.resolve(), lang, output) +
                 rewrites(args.cetta.resolve(), lang, output)
                 for lang in args.language or ("he", "petta", "prime"))
    print(f"(ModuleBindingsV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
