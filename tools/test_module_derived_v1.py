#!/usr/bin/env python3
"""Qualify derived authoring syntax through existing LanguageDef consumers."""
import argparse
from pathlib import Path

from gslt2parse_schema_v1 import StringLiteral, Symbol, Variable
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from module_chainer_v1 import export_rules
from test_module_values_v1 import HOSTILE_CONSTRUCTORS, list_value, pattern, reader_load, rows
from test_parser_artifacts_v1 import run, run_file

ROOT = Path(__file__).resolve().parents[1]


def controls(binary, language, output):
    bindings = [
        ("$reader", reader_load(None)),
        ("$base", form("module:read-file", "$reader", text("base"),
                       text(str(ROOT / "tests/module_values/RewriteBundles.module")))),
        ("$module", form("module:read-file", "$reader", text("derived"),
                         text(str(ROOT / "tests/module_values/DerivedSubstitution.module")))),
        ("$tuple-module", form("module:read-file", "$reader", text("tuples"),
                               text(str(ROOT / "tests/module_values/DerivedTuples.module")))),
        ("$closed-reader", form("langdef:close-parser", "$reader")),
        ("$naturals", form("module:assemble", "$base", text("Naturals"), "LNil", "LNil",
                           "OriginViewV1", text("shared:naturals"))),
        ("$sequences", form("module:assemble", "$base", text("Sequences"), "LNil", "LNil",
                            "OriginViewV1", text("shared:sequences"))),
    ]
    theories = ("NamedZero", "Repeated", "Nested", "Qualified", "Simultaneous", "Combined", "NamedRenamed", "MissingTarget", "WrongSort")
    for name in theories:
        arguments = "(LCons $naturals (LCons $sequences LNil))" if name == "WrongSort" else "(LCons $naturals LNil)"
        bindings += [
            ("$" + name, form("module:assemble", "$module", text(name), arguments,
                             '(LCons (ModuleCatalogEntryV1 "RewriteBundles" $base) LNil)',
                             "OriginViewV1", text("derived:" + name))),
            ("$" + name + "-core", form("module:core", "$" + name, text(name))),
        ]
    tuple_theories = ("TupleSyntax", "TupleRenamed", "WrongComponents", "CollidingHead", "NestedComponents")
    for name in tuple_theories:
        bindings += [
            ("$" + name, form("module:assemble", "$tuple-module", text(name), "(LCons $NamedZero LNil)",
                             '(LCons (ModuleCatalogEntryV1 "RewriteBundles" $base) LNil)', "OriginViewV1", text("tuples:" + name))),
            ("$" + name + "-core", form("module:core", "$" + name, text(name))),
        ]
    bindings += [
        ("$renamed-authority", form("langdef:load-language-value", "$NamedRenamed-core")),
        ("$renamed-result", form("langdef:rewrite-term-at-data", "$renamed-authority", text("Nat"), text("Nat"),
                                "8", "100000", "(LangDef:RelationRows)", "(Plus (NatZ) (NatS (NatZ)))")),
        ("$renamed-close", form("langdef:close-language", "$renamed-authority")),
        ("$renamed-tuple-authority", form("langdef:load-language-value", "$TupleRenamed-core")),
        ("$renamed-tuple-admitted", form("langdef:admit-term-data", "$renamed-tuple-authority", text("Wrapped"),
                                       "(PairBox (PairOf (NatZ) (NatS (NatZ))))")),
        ("$renamed-tuple-old", form("langdef:admit-term-data", "$renamed-tuple-authority", text("Wrapped"),
                                  "(PairBox (NatPair (NatZ) (NatS (NatZ))))")),
        ("$renamed-tuple-close", form("langdef:close-language", "$renamed-tuple-authority")),
        ("$tuple-language", form("module:language", "$TupleSyntax", text("TupleSyntax"), text("Wrapped"),
                                 "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$tuple-syntax", form("module:language-syntax", "$tuple-language")),
        ("$tuple-parser", form("langdef:compile-parser", "$tuple-syntax", "general")),
        ("$tuple-parse", form("langdef:parser-parse-text", "$tuple-parser", text("wrap(z,s(z))"))),
        ("$tuple-authority", form("langdef:load-language-value", "$TupleSyntax-core")),
        ("$tuple-projection", form("langdef:project-parser-result", "$tuple-authority", text("Wrapped"), "$tuple-parse")),
        ("$tuple-term", form("langdef:projected-term", "$tuple-projection", "0")),
        ("$tuple-admitted", form("langdef:admit-term-data", "$tuple-authority", text("Wrapped"), "$tuple-term")),
        ("$tuple-wrong-sort", form("langdef:admit-term-data", "$tuple-authority", text("Nat"), "$tuple-term")),
        ("$tuple-native-rewrite", form("langdef:rewrite-term-at-data", "$tuple-authority", text("Nat"), text("Nat"),
                                      "8", "100000", "(LangDef:RelationRows)", "(NatAdd (NatZ) (NatS (NatZ)))")),
        ("$unit-language", form("module:language", "$TupleSyntax", text("TupleSyntax"), text("WrappedUnit"),
                                "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$unit-syntax", form("module:language-syntax", "$unit-language")),
        ("$unit-parser", form("langdef:compile-parser", "$unit-syntax", "general")),
        ("$unit-parse", form("langdef:parser-parse-text", "$unit-parser", text("box unit"))),
        ("$unit-projection", form("langdef:project-parser-result", "$tuple-authority", text("WrappedUnit"), "$unit-parse")),
        ("$unit-term", form("langdef:projected-term", "$unit-projection", "0")),
        ("$unit-admitted", form("langdef:admit-term-data", "$tuple-authority", text("WrappedUnit"), "$unit-term")),
        ("$unit-close-parser", form("langdef:close-parser", "$unit-parser")),
        ("$tuple-close-parser", form("langdef:close-parser", "$tuple-parser")),
        ("$tuple-close-authority", form("langdef:close-language", "$tuple-authority")),
    ]
    bindings += [
        ("$language", form("module:language", "$NamedZero", text("NamedZero"), text("Nat"),
                           "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$syntax", form("module:language-syntax", "$language")),
        ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
        ("$parse", form("langdef:parser-parse-text", "$parser", text("add(z,s(z))"))),
        ("$parse-again", form("langdef:parser-parse-text", "$parser", text("add(z,s(z))"))),
        ("$freeze", form("langdef:freeze-parser", "$parser", text(str(output / (language + "-derived.tpp"))))),
        ("$restored", form("langdef:load-parser-artifact", text(str(output / (language + "-derived.tpp"))))),
        ("$authority", form("langdef:load-language-value", "$NamedZero-core")),
        ("$projected", form("langdef:project-parser-result", "$authority", text("Nat"), "$parse")),
        ("$restored-parse", form("langdef:parser-parse-text", "$restored", text("add(z,s(z))"))),
        ("$restored-projected", form("langdef:project-parser-result", "$authority", text("Nat"), "$restored-parse")),
        ("$term", form("langdef:projected-term", "$projected", "0")),
        ("$admitted", form("langdef:admit-term-data", "$authority", text("Nat"), "$term")),
        ("$rewritten", form("langdef:rewrite-term-at-data", "$authority", text("Nat"), text("Nat"),
                            "8", "100000", "(LangDef:RelationRows)", "$term")),
        ("$close-authority", form("langdef:close-language", "$authority")),
        ("$close-parser", form("langdef:close-parser", "$parser")),
        ("$close-restored", form("langdef:close-parser", "$restored")),
        ("$combined-authority", form("langdef:load-language-value", "$Combined-core")),
        ("$duplicate-results", form("langdef:rewrite-term-at-data", "$combined-authority", text("Nat"), text("Nat"),
                                    "8", "100000", "(LangDef:RelationRows)", "$term")),
        ("$rule-view", form("module:rewrite-rules", "$Combined", text("Combined"))),
        ("$close-combined", form("langdef:close-language", "$combined-authority")),
    ]
    observed = ["$module"] + ["$" + n + "-core" for n in theories + tuple_theories] + [
        "$parse", "$parse-again", "$projected", "$restored-projected", "$admitted", "$rewritten",
        "$duplicate-results", "$rule-view", "$tuple-projection", "$tuple-term", "$tuple-admitted",
        "$tuple-wrong-sort", "$tuple-native-rewrite", "$unit-term", "$unit-admitted",
        "$renamed-result", "$renamed-tuple-admitted", "$renamed-tuple-old"]
    program = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n!' + lets(
        bindings, "(function (return (ModuleDerivedControlV1 " + " ".join(observed) + ")))") + "\n"

    def execute(source, name):
        records = [v for v in run(binary, language, output, name, source)
                   if headed(v, "ModuleDerivedControlV1", len(observed))]
        if len(records) != 1:
            raise AssertionError((language, "missing derived result", records))
        return dict(zip(observed, records[0][1:]))

    result = execute(program, "derived")
    checks = 0

    def expect(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError((language, message, result))

    var = (Symbol("FVar"), StringLiteral("value"))
    binding = (Symbol("TypeBinding"), StringLiteral("value"), (Symbol("TBase"), StringLiteral("Nat")))
    left = pattern("NatAdd", pattern("NatZ"), var)
    expected_lefts = {"NamedZero": left, "Repeated": pattern("NatAdd", var, var),
                      "Nested": left, "Qualified": left}
    expect(headed(result["$module"], "ModuleValueV1", 7), "source declarations remain compact data")
    for name, expected in expected_lefts.items():
        core = result["$" + name + "-core"]
        expect(headed(core, "GSLTLanguageDefWireV1", 5), name + " reaches the existing core")
        rule = rows(core[5])[0]
        expect(rule[2] == list_value(binding) and rule[4] == expected and rule[5] == var,
               name + " lowers to an independently specified rule, with no local hole in the law context")
    simultaneous = rows(result["$Simultaneous-core"][5])[0]
    slot = (Symbol("FVar"), StringLiteral("slot"))
    expect(simultaneous[4] == pattern("NatAdd", pattern("NatS", slot), var) and
           simultaneous[5] == var and
           [entry[1] for entry in rows(simultaneous[2])] == [StringLiteral("slot"), StringLiteral("value")],
           "replacement names remain ambient variables and are inserted once, not recursively substituted")
    for name, reason in (("MissingTarget", "unresolved-substitution-target"), ("WrongSort", "dangling-law-constructor")):
        value = result["$" + name + "-core"]
        expect(headed(value, "ModuleRefusedV1", 3) and value[1] == Symbol(reason) and
               headed(value[2], "SyntaxOriginV1", 2), name + " is an origin-positioned refusal")
    expect(result["$parse"] == result["$parse-again"] and headed(result["$parse"], "ParserAcceptedV1", 2),
           "repeated parsing reuses preparation")
    expect(result["$projected"] == result["$restored-projected"], "restoration preserves projected terms and source positions")
    expect(result["$admitted"] == (Symbol("LangDef:TermAdmitted"),), "projected term is admitted by the core")
    rewritten = result["$rewritten"]
    renamed_result = result["$renamed-result"]
    expect(headed(renamed_result, "LangDef:ContextualTermResults", 2) and
           renamed_result[2][1] == (Symbol("NatS"), (Symbol("NatZ"),)),
           "constructor transport also changes the references inside named substitution")
    expect(result["$renamed-tuple-admitted"] == (Symbol("LangDef:TermAdmitted"),),
           "transported tuple syntax admits its renamed constructor")
    expect(not headed(result["$renamed-tuple-old"], "LangDef:TermAdmitted", 0),
           "a transported tuple does not retain an accidental old constructor alias")
    expect(result["$TupleRenamed-core"][4:6] == result["$TupleSyntax-core"][4:6],
           "renaming tuple syntax retains the selected old-rule observation")
    expect(headed(rewritten, "LangDef:ContextualTermResults", 2) and
           rewritten[2][1] == (Symbol("NatS"), (Symbol("NatZ"),)), "derived rule actually executes")
    duplicate = result["$duplicate-results"]
    expect(len(duplicate) == 4 and duplicate[2][1] == duplicate[3][1] and duplicate[2][2] != duplicate[3][2],
           "independent equal-result rules retain distinct traces")
    view = result["$rule-view"]
    expect(headed(view, "ModuleRewriteRulesV1", 2) and len(rows(view[2])) == 2 and
           rows(view[2])[0][1] != rows(view[2])[1][1], "chainer handoff retains distinct origins")
    tuple_core = result["$TupleSyntax-core"]
    expect(headed(tuple_core, "GSLTLanguageDefWireV1", 5), "a second independently authored module supplies tuple syntax")
    expect(tuple_core[4:6] == result["$NamedZero-core"][4:6], "tuple formation retains the inherited equations and rewrite rows exactly")
    tuple_rule = next(rule for rule in rows(tuple_core[3]) if rule[1] == StringLiteral("NatPair"))
    expect(tuple_rule[2] == StringLiteral("$module:tuple:2:3:Nat:3:Nat;") and
           [parameter[2] for parameter in rows(tuple_rule[3])] == [(Symbol("TBase"), StringLiteral("Nat"))] * 2,
           "tuple elaboration uses an ordinary sort and an ordered ordinary constructor signature")
    unit_rule = next(rule for rule in rows(tuple_core[3]) if rule[1] == StringLiteral("UnitTuple"))
    expect(unit_rule[2] == StringLiteral("$module:tuple:0;") and not rows(unit_rule[3]), "the empty tuple has a nullary signature")
    expect(result["$unit-term"] == (Symbol("UnitBox"), (Symbol("UnitTuple"),)) and
           result["$unit-admitted"] == (Symbol("LangDef:TermAdmitted"),),
           "empty tuple notation also parses, projects and admits through the core")
    expect(result["$tuple-term"] == (Symbol("PairBox"), (Symbol("NatPair"), (Symbol("NatZ"),),
                                                        (Symbol("NatS"), (Symbol("NatZ"),)))) and
           result["$tuple-admitted"] == (Symbol("LangDef:TermAdmitted"),), "tuple syntax actually parses, projects and admits")
    expect(not headed(result["$tuple-wrong-sort"], "LangDef:TermAdmitted", 0), "the derived sort does not masquerade as a component sort")
    expect(result["$tuple-native-rewrite"][2][1] == rewritten[2][1], "the composed core still executes the first module's derived rule")
    for name, reason in (("WrongComponents", "tuple-signature-conflict"),
                         ("CollidingHead", "tuple-constructor-conflict"),
                         ("NestedComponents", "tuple-component-profile")):
        value = result["$" + name + "-core"]
        expect(headed(value, "ModuleRefusedV1", 3) and value[1] == Symbol(reason), name + " has no silently invented tuple meaning")
    import_line, rest = program.split("\n", 1)
    hostile = HOSTILE_CONSTRUCTORS + '(= (ModuleNamedSubstitutionV1 $b $r $n) host-substitution)\n'
    expect(execute(import_line + "\n" + hostile + rest, "derived-hostile") == result,
           "host equations do not interpret the authored substitution")
    print(f"ModuleDerivedSummary {language} {checks} 0")
    return checks


def handoff_example(binary, language, output):
    values = run_file(binary, language, output, "derived-example",
                      ROOT / "examples/module_derived_values_v1.metta")
    records = [value for value in values if headed(value, "ModuleDerivedExampleV1", 9)]
    assert len(records) == 1, (language, records)
    core, tuple_term, tuple_admitted, nat_term, nat_admitted, step, view, first, repeated = records[0][1:]
    assert headed(core, "GSLTLanguageDefWireV1", 5), core
    assert tuple_term == (Symbol("PairBox"), (Symbol("NatPair"), (Symbol("NatZ"),),
                                            (Symbol("NatS"), (Symbol("NatZ"),)))), tuple_term
    assert tuple_admitted == nat_admitted == (Symbol("LangDef:TermAdmitted"),), records
    assert nat_term == (Symbol("NatAdd"), (Symbol("NatZ"),), (Symbol("NatS"), (Symbol("NatZ"),))), nat_term
    assert headed(step, "LangDef:ContextualTermResults", 2) and step[1] == Symbol("LangDef:ContextComplete"), step
    assert step[2][1] == (Symbol("NatS"), (Symbol("NatZ"),)), step
    assert first == repeated and headed(first, "ParserAcceptedV1", 2), (first, repeated)
    assert view[1] == core and len(rows(view[2])) == 1, view
    exported = export_rules(view, Variable("authority"), StringLiteral("derived"), Symbol("derivedKb"), Symbol("evidence"))
    assert len(exported) == 1, exported
    print(f"ModuleDerivedExampleSummary {language} 1 0")


def composed_controls(binary, language, output, baked=None):
    def source_list(values):
        result = "LNil"
        for value in reversed(values):
            result = form("LCons", value, result)
        return result

    bindings = [("$reader", reader_load(None))]
    stems = ("RewriteBundles", "DerivedSubstitution", "DerivedTuples", "BindingValues",
             "SourceNotes", "SearchHints", "ComposedDerived")
    for stem in stems:
        source = "build-module:composed-binding" if stem == "ComposedDerived" else "build-import:" + stem
        bindings.append(("$" + stem, form("module:read-file", "$reader", text(source),
            text(str(ROOT / "tests/module_values" / (stem + ".module"))))))
    catalog = source_list([form("ModuleCatalogEntryV1", text(stem), "$" + stem) for stem in stems[:4]])
    bindings += [
        ("$reader-closed", form("langdef:close-parser", "$reader")),
        ("$theory", form("module:assemble", "$ComposedDerived", text("Complete"), "LNil", catalog,
                         "OriginViewV1", text("build-instance:composed-binding"))),
        ("$core", form("module:core", "$theory", text("composed-binding"))),
    ]
    for tag, base, theory, schema, category, field in (
        ("notes", "RewriteBundles", "Naturals", "SourceNotes", "Note", "SourceNote"),
        ("hints", "BindingValues", "BindingWeaken", "SearchHints", "Hint", "SearchDepth"),
    ):
        schema_theory = "Notes" if tag == "notes" else "Hints"
        bindings += [
            (f"${tag}-base", form("module:assemble", "$" + base, text(theory), "LNil", "LNil",
                                  "OriginViewV1", text("client-base:" + tag))),
            (f"${tag}-base-core", form("module:core", f"${tag}-base", text(tag))),
            (f"${tag}-schema", form("module:assemble", "$" + schema, text(schema_theory), "LNil", "LNil",
                                    "OriginViewV1", text("client-schema:" + tag))),
            (f"${tag}-schema-core", form("module:core", f"${tag}-schema", text(schema))),
            (f"${tag}-layer", form("module:attach-layer", f"${tag}-base-core",
                form("SyntaxOriginV1", text(tag), text("layer")), f"${tag}-schema-core", text(category),
                source_list([form(field)]))),
        ]
    bindings += [
        ("$decorated", form("module:join-layers-over", "$core", "$notes-layer", "$hints-layer")),
        ("$erased", form("module:base-core", "$decorated")),
        ("$authority", form("langdef:load-language-value", "$erased")),
        ("$rules", form("module:rewrite-rules", "$theory", text("composed-binding"))),
    ]
    observed = ["$core", "$decorated", "$erased", "$rules"]
    samples = (("binding", "Expr", "lam x.ref(x)"), ("nat", "Nat", "add(z,s(z))"),
               ("tuple", "Wrapped", "wrap(z,s(z))"))
    for tag, category, sample in samples:
        def v(suffix):
            return f"${tag}-{suffix}"
        path = text(str(output / f"{language}-composed-{tag}.tpp"))
        projection_head = "langdef:project-parser-pattern-result" if tag == "binding" else "langdef:project-parser-result"
        projection_args = ["$authority", text(category)]
        if tag == "binding":
            projection_args.append('(LangDef:NamedTokensV1 (LCons "Name" LNil))')
        bindings += [
            (v("language"), form("module:language", "$theory", text("composed-binding"), text(category),
                                 "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            (v("syntax"), form("module:language-syntax", v("language"))),
            (v("parser"), form("langdef:compile-parser", v("syntax"), "general")),
            (v("parsed"), form("langdef:parser-parse-text", v("parser"), text(sample))),
            (v("again"), form("langdef:parser-parse-text", v("parser"), text(sample))),
            (v("evidence"), form("langdef:parse-authored-text-evidence", v("parser"), text(sample), "10000")),
            (v("projected"), form(projection_head, *projection_args, v("parsed"))),
            (v("term"), form("langdef:projected-term", v("projected"), "0")),
            (v("admitted"), form("langdef:admit-pattern-data" if tag == "binding" else "langdef:admit-term-data",
                                  "$authority", text(category), v("term"))),
            (v("frozen"), form("langdef:freeze-parser", v("parser"), path)),
            (v("closed"), form("langdef:close-parser", v("parser"))),
            (v("restored"), form("langdef:load-parser-artifact-checked", path)),
            (v("restored-parse"), form("langdef:parser-parse-text", v("restored"), text(sample))),
            (v("restored-evidence"), form("langdef:parse-authored-text-evidence", v("restored"), text(sample), "10000")),
            (v("restored-projected"), form(projection_head, *projection_args, v("restored-parse"))),
            (v("restored-closed"), form("langdef:close-parser", v("restored"))),
        ]
        observed += [v(key) for key in ("parsed", "again", "evidence", "term", "admitted", "projected",
                                       "restored-parse", "restored-evidence", "restored-projected")]
    bindings += [
        ("$binding-step", form("langdef:rewrite-pattern-at-data", "$authority", "4", "100000",
                               "(LangDef:RelationRows)", "$binding-term")),
        ("$binding-reduct", form("langdef:contextual-result-term", "$binding-step", "0")),
        ("$binding-reduct-admitted", form("langdef:admit-pattern-data", "$authority", text("Expr"), "$binding-reduct")),
        ("$nat-step", form("langdef:rewrite-term-at-data", "$authority", text("Nat"), text("Nat"),
                           "4", "100000", "(LangDef:RelationRows)", "$nat-term")),
    ]
    observed += ["$binding-step", "$binding-reduct", "$binding-reduct-admitted", "$nat-step"]
    if baked:
        bindings += [
            ("$baked", form("langdef:load-baked-parser", text(baked))),
            ("$baked-parse", form("langdef:parser-parse-text", "$baked", text(samples[0][2]))),
            ("$baked-evidence", form("langdef:parse-authored-text-evidence", "$baked", text(samples[0][2]), "10000")),
            ("$baked-projected", form("langdef:project-parser-pattern-result", "$authority", text("Expr"),
                                      '(LangDef:NamedTokensV1 (LCons "Name" LNil))', "$baked-parse")),
            ("$baked-close", form("langdef:close-parser", "$baked")),
        ]
        observed += ["$baked-parse", "$baked-evidence", "$baked-projected"]
    bindings.append(("$authority-closed", form("langdef:close-language", "$authority")))
    program = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n!' + lets(
        bindings, "(function (return (ModuleComposedV1 " + " ".join(observed) + ")))") + "\n"
    records = [v for v in run(binary, language, output, "composed", program)
               if headed(v, "ModuleComposedV1", len(observed))]
    assert len(records) == 1, (language, records)
    result = dict(zip(observed, records[0][1:]))
    checks = 0

    def expect(condition, reason):
        nonlocal checks
        checks += 1
        assert condition, (language, reason, result)

    expect(result["$erased"] == result["$core"], "layer union preserves the explicit target")
    expect(len(rows(result["$decorated"][2])) == 2, "both independently authored fields retained")
    expect(len(rows(result["$rules"][2])) == 2, "derived and binding rules exported explicitly")
    for tag, _, _ in samples:
        expect(headed(result[f"${tag}-parsed"], "ParserAcceptedV1", 2), tag + " parsed")
        expect(result[f"${tag}-parsed"] == result[f"${tag}-again"] == result[f"${tag}-restored-parse"], tag + " reuse")
        expect(result[f"${tag}-evidence"] == result[f"${tag}-restored-evidence"], tag + " packed evidence")
        expect(result[f"${tag}-projected"] == result[f"${tag}-restored-projected"], tag + " positioned projection")
        expect(headed(result[f"${tag}-admitted"], "LangDef:TermAdmitted", 0), tag + " admission")
    expected = pattern("Lam", (Symbol("PLam"), Symbol("BNone"), pattern("Lam",
        (Symbol("PLam"), Symbol("BNone"), pattern("Ref", (Symbol("Var"), 1))))))
    expect(result["$binding-reduct"] == expected, "binding rewrite does not capture the outer reference")
    expect(headed(result["$binding-reduct-admitted"], "LangDef:TermAdmitted", 0), "reduct admitted")
    expect(result["$nat-step"][2][1] == (Symbol("NatS"), (Symbol("NatZ"),)), "derived substitution executes")
    if baked:
        for suffix in ("parse", "evidence", "projected"):
            live_suffix = "parsed" if suffix == "parse" else suffix
            expect(result["$baked-" + suffix] == result["$binding-" + live_suffix], "baked " + suffix)
    import_line, rest = program.split("\n", 1)
    hostile = HOSTILE_CONSTRUCTORS + '(= (PApp $h $a) bad-pattern)\n(= (PLam $n $b) bad-binder)\n'
    hostile_records = [v for v in run(binary, language, output, "composed-hostile", import_line + "\n" + hostile + rest)
                       if headed(v, "ModuleComposedV1", len(observed))]
    expect(hostile_records == records, "guest declarations and result terms remain inert")
    print(f"ModuleComposedSummary {language} {checks} 0")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--language", choices=("he", "petta", "prime"), action="append")
    parser.add_argument("--baked-parser")
    parser.add_argument("--composed-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for language in args.language or ("he", "petta", "prime"):
        if not args.composed_only:
            controls(args.cetta.resolve(), language, output)
            handoff_example(args.cetta.resolve(), language, output)
        composed_controls(args.cetta.resolve(), language, output, args.baked_parser)


if __name__ == "__main__":
    main()
