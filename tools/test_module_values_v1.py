#!/usr/bin/env python3
"""Exercise compact module values through the shared language consumers."""
import argparse
import json
from pathlib import Path

from test_parser_artifacts_v1 import run
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from gslt2parse_schema_v1 import StringLiteral, Symbol, parse_sexprs, render

ROOT = Path(__file__).resolve().parents[1]
HOSTILE_CONSTRUCTORS = ('(= (GSLTLanguageDefWireV1 $n $t $c $e $r) corrupted-core)\n'
                        '(= (TypeDecl $n $c) corrupted-type)\n'
                        '(= (GrammarRule $n $c $p $s $e) corrupted-term)\n'
                        '(= (Equation $n $v $p $l $r) corrupted-equation)\n'
                        '(= (RewriteRule $n $v $p $l $r) corrupted-rewrite)\n')


def rows(value):
    out = []
    while headed(value, "LCons", 2):
        out.append(value[1])
        value = value[2]
    if value != Symbol("LNil"):
        raise AssertionError(("expected finite declaration list", value))
    return out


def list_value(*values):
    out = Symbol("LNil")
    for value in reversed(values):
        out = (Symbol("LCons"), value, out)
    return out


def pattern(head, *arguments):
    return (Symbol("PApp"), StringLiteral(head), list_value(*arguments))


def reader_load(artifact):
    return form("langdef:load-parser-artifact", text(str(artifact))) if artifact else \
        form("langdef:load-baked-parser", text("module-core-reader"))


def rewrite_bundle_controls(binary, language, output, reader_artifact,
                            emit_build=None, baked_parser=None, baked_reference=None):
    def assemble(theory, arguments="LNil"):
        return form("module:assemble", "$module", text(theory), arguments,
                    "LNil", "OriginViewV1", text("rewrite-bundles:" + theory))

    def rewrite(authority, category, term, fuel="8"):
        return form("langdef:rewrite-term-at-data", authority, text(category), text(category),
                    fuel, "100000", "(LangDef:RelationRows)", term)

    natural_argument = "(LCons $naturals LNil)"
    authored_extension = '''(ModuleValueV1 "authored-extra" "ExtraModule"
      (LCons (ModuleImportFromV1 "Together" "RewriteBundles") LNil)
      (LCons (ModuleTheoryV1 "Extra" (LCons (ModuleParameterV1 "base" "Together") LNil)
        (ModuleAddTermsV1 (ModuleRefV1 "base")
          (LCons (ModuleDeclarationV1 0 (ModuleTermV1 "NatOne" (TBase "Nat")
            (LCons (LbnfTerminalV1 (bnf-v1:text-cons 111 (bnf-v1:text-cons 110
              (bnf-v1:text-cons 101 (bnf-v1:text-nil))))) LNil) False)) LNil))) LNil)
      LNil LNil LNil)'''
    authored_zero = '''(ModuleValueV1 "authored-zero" "ZeroModule"
      (LCons (ModuleImportFromV1 "Together" "RewriteBundles") LNil)
      (LCons (ModuleTheoryV1 "ZeroNotation" (LCons (ModuleParameterV1 "base" "Together") LNil)
        (ModuleAddTermsV1 (ModuleRefV1 "base")
          (LCons (ModuleDeclarationV1 0 (ModuleTermV1 "NatZ" (TBase "Nat")
            (LCons (LbnfTerminalV1 (bnf-v1:text-cons 122 (bnf-v1:text-nil))) LNil) False)) LNil))) LNil)
      LNil LNil LNil)'''
    bindings = [
        ("$reader", reader_load(reader_artifact)),
        ("$module", form("module:read-file", "$reader", text("rewrite-bundles"),
                         text(str(ROOT / "tests/module_values/RewriteBundles.module")))),
        ("$closed-reader", form("langdef:close-parser", "$reader")),
        ("$naturals", assemble("Naturals")),
        ("$sequences", assemble("Sequences")),
        ("$natural-core", form("module:core", "$naturals", text("Naturals"))),
        ("$arithmetic", assemble("Arithmetic", natural_argument)),
        ("$inspected", form("module:inspect", "$arithmetic")),
        ("$arithmetic-core", form("module:core", "$arithmetic", text("Arithmetic"))),
        ("$duplicate", assemble("DuplicatePaths", natural_argument)),
        ("$duplicate-core", form("module:core", "$duplicate", text("Duplicate"))),
        ("$rule-view", form("module:rewrite-rules", "$duplicate", text("Duplicate"))),
        ("$shared", assemble("SharedTwice", natural_argument)),
        ("$shared-core", form("module:core", "$shared", text("Shared"))),
        ("$meet", assemble("SharedSignature", natural_argument)),
        ("$meet-core", form("module:core", "$meet", text("Meet"))),
        ("$dangling", assemble("DanglingRules", natural_argument)),
        ("$dangling-core", form("module:core", "$dangling", text("Dangling"))),
        ("$additional", assemble("AdditionalBehavior", natural_argument)),
        ("$additional-core", form("module:core", "$additional", text("Additional"))),
        ("$sequence-rules", assemble("SequenceRules", "(LCons $sequences LNil)")),
        ("$sequence-core", form("module:core", "$sequence-rules", text("Sequences"))),
        ("$independent", assemble("Independent", "(LCons $naturals (LCons $sequences LNil))")),
        ("$independent-core", form("module:core", "$independent", text("Independent"))),
        ("$together", assemble("Together", "(LCons $naturals (LCons $sequences LNil))")),
        ("$together-language", form("module:language", "$together", text("Together"),
                                   text("Nat"), "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$together-core", form("module:language-core", "$together-language")),
        ("$syntax", form("module:language-syntax", "$together-language")),
        ("$independent-zero", form("lbnf:lower-document",
            '(LbnfDocumentV1 "independent-zero" (LCons (LbnfDefinitionV1 0 '
            '(LbnfRuleV1 "NatZ" "Nat" (LCons (LbnfTerminalV1 '
            '(bnf-v1:text-cons 122 (bnf-v1:text-nil))) LNil) False)) LNil))',
            text("IndependentZero"), text("Nat"), "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$unshared-join", form("langdef:join-syntax-gslts", text("UnsharedNaturals"), text("Nat"),
                               "$syntax", "$independent-zero")),
        ("$zero-extension", form("module:assemble", authored_zero, text("ZeroNotation"), "(LCons $together LNil)",
                                 '(LCons (ModuleCatalogEntryV1 "RewriteBundles" $module) LNil)', "OriginViewV1", text("authored-zero:instance"))),
        ("$zero-language", form("module:language", "$zero-extension", text("ZeroNotation"), text("Nat"),
                                "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$zero-syntax", form("module:language-syntax", "$zero-language")),
        ("$ambiguous-syntax", form("langdef:join-syntax-gslts", text("AmbiguousNaturals"), text("Nat"),
                                  "$syntax", "$zero-syntax")),
        ("$ambiguous-parser", form("langdef:load-baked-parser", text("module-ambiguous")) if baked_parser
            else form("langdef:compile-parser", "$ambiguous-syntax", "auto")),
        ("$ambiguous-parse", form("langdef:parser-parse-text", "$ambiguous-parser", text("z"))),
        ("$ambiguous-evidence", form("langdef:parse-authored-text-evidence", "$ambiguous-parser", text("z"), "10000")),
        ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
        ("$parsed", form("langdef:parser-parse-text", "$parser", text("s(add(z,z))"))),
        ("$repeated", form("langdef:parser-parse-text", "$parser", text("s(add(z,z))"))),
        ("$parser-evidence", form("langdef:parse-authored-text-evidence", "$parser", text("s(add(z,z))"), "10000")),
        ("$frozen", form("langdef:freeze-parser", "$parser", text(str(output / (language + "-rewrite-bundles.tpp"))))),
        ("$restored", form("langdef:load-parser-artifact", text(str(output / (language + "-rewrite-bundles.tpp"))))),
        ("$restored-parse", form("langdef:parser-parse-text", "$restored", text("s(add(z,z))"))),
        ("$restored-evidence", form("langdef:parse-authored-text-evidence", "$restored", text("s(add(z,z))"), "10000")),
        ("$arithmetic-authority", form("langdef:load-language-value", "$arithmetic-core")),
        ("$duplicate-authority", form("langdef:load-language-value", "$duplicate-core")),
        ("$sequence-authority", form("langdef:load-language-value", "$sequence-core")),
        ("$independent-authority", form("langdef:load-language-value", "$independent-core")),
        ("$additional-authority", form("langdef:load-language-value", "$additional-core")),
        ("$together-authority", form("langdef:load-language-value", "$together-core")),
        ("$ambiguous-projected", form("langdef:project-parser-result", "$together-authority", text("Nat"),
                                     "$ambiguous-parse")),
        ("$projected", form("langdef:project-parser-result", "$together-authority", text("Nat"), "$parsed")),
        ("$restored-projected", form("langdef:project-parser-result", "$together-authority", text("Nat"), "$restored-parse")),
        ("$actual-term", form("langdef:projected-term", "$projected", "0")),
        ("$bad-index", form("langdef:projected-term", "$projected", "-1")),
        ("$missing-index", form("langdef:projected-term", "$projected", "1")),
        ("$bad-outcome", form("langdef:project-parser-result", "$together-authority", text("Nat"),
                              "(ParserAcceptedV1 () (ParserSpanV1 0 0 0 0))")),
        ("$actual-admission", form("langdef:admit-term-data", "$together-authority", text("Nat"), "$actual-term")),
        ("$actual-rewrite", rewrite("$together-authority", "Nat", "$actual-term")),
        ("$extra", form("module:assemble", authored_extension, text("Extra"), "(LCons $together LNil)",
                        '(LCons (ModuleCatalogEntryV1 "RewriteBundles" $module) LNil)', "OriginViewV1", text("authored-extra:instance"))),
        ("$extra-language", form("module:language", "$extra", text("Extra"), text("Nat"),
                                 "(SyntaxLexicalPolicyV1 bnfc-documented)")),
        ("$extra-syntax", form("module:language-syntax", "$extra-language")),
        ("$joined-syntax", form("langdef:join-syntax-gslts", text("JoinedNaturals"), text("Nat"), "$syntax", "$extra-syntax")),
        ("$joined-parser", form("langdef:load-baked-parser", text(baked_parser)) if baked_parser
            else form("langdef:compile-parser", "$joined-syntax", "auto")),
        ("$joined-parse", form("langdef:parser-parse-text", "$joined-parser", text("s(add(z,one))"))),
        ("$joined-evidence", form("langdef:parse-authored-text-evidence", "$joined-parser", text("s(add(z,one))"), "10000")),
        ("$extra-core", form("module:language-core", "$extra-language")),
        ("$extra-authority", form("langdef:load-language-value", "$extra-core")),
        ("$joined-projected", form("langdef:project-parser-result", "$extra-authority", text("Nat"), "$joined-parse")),
        ("$joined-term", form("langdef:projected-term", "$joined-projected", "0")),
        ("$joined-admission", form("langdef:admit-term-data", "$extra-authority", text("Nat"), "$joined-term")),
        ("$joined-rewrite", rewrite("$extra-authority", "Nat", "$joined-term")),
        ("$duplicate-projected", form("langdef:project-parser-result", "$together-authority", text("Nat"),
            '(ParserAcceptedV1 ((LbnfAstNodeV1 "Nat" "NatZ" (LbnfSourceSpanV1 0 1 0 1) LNil) '
            '(LbnfAstNodeV1 "Nat" "NatZ" (LbnfSourceSpanV1 0 1 0 1) LNil)) (ParserSpanV1 0 1 0 1))')),
        ("$wrong-projection", form("langdef:project-parser-result", "$together-authority", text("Seq"), "$parsed")),
        ("$mixed-projection", form("langdef:project-parser-result", "$together-authority", text("Nat"),
            '(ParserAcceptedV1 ((LbnfAstNodeV1 "Nat" "NatZ" (LbnfSourceSpanV1 0 1 0 1) LNil) '
            '(LbnfAstNodeV1 "Nat" "Invented" (LbnfSourceSpanV1 0 1 0 1) LNil)) (ParserSpanV1 0 1 0 1))')),
        ("$rejected-projection", form("langdef:project-parser-result", "$together-authority", text("Nat"), "(ParserRejectedV1 2 2 (ExpectedV1))")),
        ("$incomplete-projection", form("langdef:project-parser-result", "$together-authority", text("Nat"), "(ParserIncompleteV1 2 2 \"budget\")")),
        ("$cyclic-projection", form("langdef:project-parser-result", "$together-authority", text("Nat"), "(ParserCyclicV1 True)")),
        ("$zero", rewrite("$arithmetic-authority", "Nat", "(NatZ)")),
        ("$sum", rewrite("$arithmetic-authority", "Nat", "(NatAdd (NatZ) (NatS (NatZ)))")),
        ("$sum-together", rewrite("$together-authority", "Nat", "(NatAdd (NatZ) (NatS (NatZ)))")),
        ("$sum-independent", rewrite("$independent-authority", "Nat", "(NatAdd (NatZ) (NatS (NatZ)))")),
        ("$sum-pattern", form("langdef:rewrite-pattern-at-data", "$arithmetic-authority", "8", "100000",
                              "(LangDef:RelationRows)",
                              '(PApp "NatAdd" (LCons (PApp "NatZ" LNil) '
                              '(LCons (PApp "NatS" (LCons (PApp "NatZ" LNil) LNil)) LNil)))')),
        ("$successor", rewrite("$arithmetic-authority", "Nat", "(NatAdd (NatS (NatZ)) (NatZ))")),
        ("$no-implicit-context", rewrite("$arithmetic-authority", "Nat", "(NatS (NatAdd (NatZ) (NatZ)))")),
        ("$independent-no-context", rewrite("$independent-authority", "Nat", "(NatS (NatAdd (NatZ) (NatZ)))")),
        ("$context", rewrite("$together-authority", "Nat", "(NatS (NatAdd (NatZ) (NatZ)))")),
        ("$context-budget", rewrite("$together-authority", "Nat", "(NatS (NatAdd (NatZ) (NatZ)))", "1")),
        ("$two-paths", rewrite("$duplicate-authority", "Nat", "(NatAdd (NatZ) (NatS (NatZ)))")),
        ("$sequence-step", rewrite("$sequence-authority", "Seq", "(SeqJoin (SeqEmpty) (SeqCons (SeqEmpty)))")),
        ("$sequence-together", rewrite("$together-authority", "Seq", "(SeqJoin (SeqEmpty) (SeqCons (SeqEmpty)))")),
        ("$changed-zero", rewrite("$additional-authority", "Nat", "(NatZ)")),
        ("$wrong-sort", form("langdef:admit-term-data", "$together-authority", text("Nat"), "(SeqEmpty)")),
        ("$close-arithmetic", form("langdef:close-language", "$arithmetic-authority")),
        ("$close-duplicate", form("langdef:close-language", "$duplicate-authority")),
        ("$close-sequence", form("langdef:close-language", "$sequence-authority")),
        ("$close-independent", form("langdef:close-language", "$independent-authority")),
        ("$close-additional", form("langdef:close-language", "$additional-authority")),
        ("$close-together", form("langdef:close-language", "$together-authority")),
        ("$close-extra", form("langdef:close-language", "$extra-authority")),
        ("$close-parser", form("langdef:close-parser", "$parser")),
        ("$close-ambiguous-parser", form("langdef:close-parser", "$ambiguous-parser")),
        ("$close-joined-parser", form("langdef:close-parser", "$joined-parser")),
        ("$surviving-parse", form("langdef:parser-parse-text", "$restored", text("s(add(z,z))"))),
        ("$close-restored", form("langdef:close-parser", "$restored")),
    ]
    observed = ["naturals", "natural-core", "arithmetic", "inspected", "arithmetic-core", "duplicate",
                "duplicate-core", "shared", "shared-core", "meet-core", "dangling", "dangling-core",
                "together", "together-core", "independent-core", "zero", "sum", "sum-together", "sum-independent", "sum-pattern", "successor",
                "no-implicit-context", "independent-no-context", "context", "context-budget", "two-paths",
                "sequence-step", "sequence-together", "changed-zero", "wrong-sort", "parsed", "repeated",
                "frozen", "restored-parse", "parser-evidence", "restored-evidence", "surviving-parse",
                "rule-view", "projected", "restored-projected", "actual-term", "actual-admission", "actual-rewrite",
                "duplicate-projected", "wrong-projection", "mixed-projection", "rejected-projection",
                "incomplete-projection", "cyclic-projection", "joined-syntax", "joined-parse",
                "joined-projected", "joined-term", "joined-admission", "joined-rewrite", "joined-evidence",
                "bad-index", "missing-index", "bad-outcome", "ambiguous-syntax", "ambiguous-parse",
                "ambiguous-projected", "ambiguous-evidence", "unshared-join"]
    record = "(ModuleRewriteBundlesV1 " + " ".join("$" + name for name in observed) + ")"
    program = ('!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n'
               '!(import! &self ' + text(str(ROOT / "lib/lib_lbnf.metta")) + ')\n!') + \
        lets(bindings, "(function (return " + record + "))") + "\n"
    values = [v for v in run(binary, language, output, "rewrite-bundles", program)
              if headed(v, "ModuleRewriteBundlesV1", len(observed))]
    if len(values) != 1:
        raise AssertionError((language, "rewrite bundle workflow", values))
    result = dict(zip(observed, values[0][1:]))
    baked_fields = ("joined-parse", "joined-evidence", "joined-projected", "joined-term",
                    "joined-admission", "joined-rewrite", "ambiguous-parse", "ambiguous-projected",
                    "ambiguous-evidence")
    baked_result = tuple([Symbol("ModuleBakedWorkflowV1")] + [result[key] for key in baked_fields])
    if emit_build:
        emit_build.mkdir(parents=True, exist_ok=True)
        for filename, content in (("composed-syntax.metta", render(result["joined-syntax"]) + "\n"),
                                  ("ambiguous-syntax.metta", render(result["ambiguous-syntax"]) + "\n"),
                                  ("expected.metta", render(baked_result) + "\n")):
            path = emit_build / filename
            if path.exists() and path.read_text() != content:
                raise AssertionError((language, "profiles disagree on the module build input", filename))
            path.write_text(content)
        (emit_build / "manifest.json").write_text(json.dumps({
            "format": "ParserBuildManifestV1", "parsers": [{
                "name": "module-composed", "format": "syntax", "source": "composed-syntax.metta",
                "strategy": "general"}, {
                "name": "module-ambiguous", "format": "syntax", "source": "ambiguous-syntax.metta",
                "strategy": "general"}]} , indent=2) + "\n")
    if baked_reference:
        expected = parse_sexprs(baked_reference.read_text())
        if expected != [baked_result]:
            raise AssertionError((language, "baked module differs from live values, evidence, projection, admission or rewriting"))
    checks = 0

    def expect(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError((language, message))

    def terms(key, status="LangDef:ContextComplete"):
        value = result[key]
        if not isinstance(value, tuple) or value[:2] != (Symbol("LangDef:ContextualTermResults"), Symbol(status)):
            raise AssertionError((language, key, "contextual outcome", value))
        return [item[1] for item in value[2:]]

    def trace(key, index=0):
        value = result[key][index + 2]
        if not headed(value, "LangDef:ContextualTermResult", 2):
            raise AssertionError((language, key, "result/trace shape", value))
        return value[2]

    nat = (Symbol("TBase"), StringLiteral("Nat"))
    var = lambda name: (Symbol("FVar"), StringLiteral(name))
    binding = lambda name: (Symbol("TypeBinding"), StringLiteral(name), nat)
    arithmetic_rules = list_value(
        (Symbol("RewriteRule"), StringLiteral("AddZero"), list_value(binding("value")), Symbol("LNil"),
         pattern("NatAdd", pattern("NatZ"), var("value")), var("value")),
        (Symbol("RewriteRule"), StringLiteral("AddSuccessor"), list_value(binding("left"), binding("right")), Symbol("LNil"),
         pattern("NatAdd", pattern("NatS", var("left")), var("right")),
         pattern("NatS", pattern("NatAdd", var("left"), var("right")))))
    expect(result["arithmetic-core"][5] == arithmetic_rules,
           "textual bundle lowers to independently authored core rewrite rows")
    expect(result["inspected"] == result["arithmetic"],
           "inspection observes the same data without interpretation or construction")
    expect(result["arithmetic-core"][2:4] == result["natural-core"][2:4],
           "adding rules retains the supplied signature")
    expect(result["shared-core"][2:] == result["arithmetic-core"][2:],
           "sharing an assembled rule bundle is idempotent, not a duplicate derivation")
    expect(result["meet-core"][2:] == result["natural-core"][2:],
           "meeting a rule bundle with its shared ancestor retains the signature only")
    expect(len(rows(result["dangling"][3])) == 2 and
           headed(result["dangling-core"], "ModuleRefusedV1", 3) and
           result["dangling-core"][1] == Symbol("dangling-law-constructor") and
           headed(result["dangling-core"][2], "SyntaxOriginV1", 2),
           "difference retains laws and exposes their removed dependencies")
    natural_entries = rows(result["naturals"][3])
    joined_entries = rows(result["together"][3])
    expect(all(entry in joined_entries for entry in natural_entries) and
           len([entry for entry in joined_entries if entry in natural_entries]) == len(natural_entries),
           "explicitly shared signature origins survive composition once")
    joined_rules = rows(result["together-core"][5])
    context_rule = (Symbol("RewriteRule"), StringLiteral("UnderSuccessor"),
                    list_value(binding("source"), binding("target")),
                    list_value((Symbol("Congruence"), var("source"), var("target"))),
                    pattern("NatS", var("source")), pattern("NatS", var("target")))
    expect(joined_rules[:2] == rows(arithmetic_rules) and joined_rules[2] == context_rule,
           "composition retains the exact premise and binding declarations")
    expect(terms("zero") == [], "an irreducible term has a complete empty result, not a rejection")
    expect(terms("sum") == [(Symbol("NatS"), (Symbol("NatZ"),))] and
           result["sum"] == result["sum-together"],
           "the tested root rewrite is unchanged when the explicit context bundle is also present")
    expect(result["sum"] == result["sum-independent"],
           "the disjoint sequence bundle retains the tested arithmetic reduction and trace")
    expect(result["no-implicit-context"] == result["independent-no-context"],
           "disjoint composition does not insert the contextual rule")
    expect([rule[1] for rule in rows(result["independent-core"][5])] ==
           [StringLiteral("AddZero"), StringLiteral("AddSuccessor"), StringLiteral("JoinEmpty")],
           "the independent join contains exactly its declared rules and no context bundle")
    expect(result["sum-pattern"] ==
           (Symbol("LangDef:ContextualResults"), Symbol("LangDef:ContextComplete"),
            (Symbol("LangDef:ContextualResult"), pattern("NatS", pattern("NatZ")), trace("sum"))),
           "the inert Pattern entry retains the same reduction and proof trace as the term entry")
    expect(terms("successor") == [(Symbol("NatS"), (Symbol("NatAdd"), (Symbol("NatZ"),), (Symbol("NatZ"),)))],
           "the second arithmetic rule executes its declared substitution")
    expect(terms("no-implicit-context") == [], "constructor nesting does not silently add context closure")
    expect(terms("context") == [(Symbol("NatS"), (Symbol("NatZ"),))],
           "an explicit contextual rule carries the nested reduction")
    context_trace = trace("context")
    expect(context_trace[:3] == (Symbol("LangDef:RuleTrace"), 2, StringLiteral("UnderSuccessor")) and
           headed(context_trace[3], "LangDef:CongruenceEvidence", 2) and
           context_trace[3][2] == (Symbol("LangDef:RuleTrace"), 0, StringLiteral("AddZero")),
           "contextual output retains its exact outer and nested rule evidence")
    expect(terms("context-budget", "LangDef:ContextFuelExhausted") == [],
           "insufficient contextual fuel is incomplete, not no applicable rule")
    expect(terms("two-paths") == [(Symbol("NatS"), (Symbol("NatZ"),))] * 2 and
           [trace("two-paths", i)[2] for i in range(2)] == [StringLiteral("AddZero"), StringLiteral("AddZeroAgain")],
           "equal-valued independent rules retain source order and multiplicity")
    duplicate_rules = [entry for entry in rows(result["duplicate"][3])
                       if headed(entry[3], "ModuleRewriteV1", 4)]
    view = result["rule-view"]
    expect(headed(view, "ModuleRewriteRulesV1", 2) and view[1] == result["duplicate-core"] and
           [row[2] for row in rows(view[2])] == rows(view[1][5]) and
           [row[1] for row in rows(view[2])] == [entry[1] for entry in duplicate_rules],
           "chainer rule observation retains the exact core, source origins, order and multiplicity")
    expect([entry[3][1] for entry in duplicate_rules] ==
           [StringLiteral("AddZero"), StringLiteral("AddSuccessor"), StringLiteral("AddZeroAgain")] and
           duplicate_rules[0][1] != duplicate_rules[2][1],
           "equal rewrite results do not merge independent source declarations")
    expect(terms("sequence-step") == terms("sequence-together") == [(Symbol("SeqCons"), (Symbol("SeqEmpty"),))] and
           trace("sequence-step")[2] == trace("sequence-together")[2] == StringLiteral("JoinEmpty"),
           "disjoint syntax retains sequence behavior with the correct rule name")
    expect(trace("sequence-step")[1] == 0 and trace("sequence-together")[1] == 3,
           "a local rule index is interpreted against its actual core, not treated as global identity")
    expect(terms("changed-zero") == [(Symbol("NatS"), (Symbol("NatZ"),))],
           "adding a same-language rewrite can invent an old-term step and is not automatically conservative")
    expect(headed(result["wrong-sort"], "LangDef:TermRejected", 2),
           "composition does not admit a sequence term as a natural")
    expect(headed(result["parsed"], "ParserAcceptedV1", 2) and result["parsed"] == result["repeated"],
           "the composed definition has a reusable parser independent of rewriting")
    expect(headed(result["frozen"], "ParserArtifactV1", 2) and result["restored-parse"] == result["parsed"],
           "restoring the bundle's syntax preserves values and source spans")
    expect(result["parser-evidence"] == result["restored-evidence"],
           "explicit packed parser evidence is unchanged by artifact restoration")
    expect(result["surviving-parse"] == result["parsed"],
           "closing the live parser does not invalidate its restored independent handle")
    expect(headed(result["projected"], "LangDef:ProjectedTermsV1", 2) and
           result["projected"] == result["restored-projected"] and
           result["actual-term"] == (Symbol("NatS"), (Symbol("NatAdd"), (Symbol("NatZ"),), (Symbol("NatZ"),))),
           "the actual live and restored ASTs project generically through the authoritative core")
    expect(headed(result["actual-admission"], "LangDef:TermAdmitted", 0) and
           result["actual-rewrite"] == result["context"],
           "the term parsed from module syntax is admitted and rewritten with the same declared trace")
    projected_entry = result["projected"][1][0]
    sources = projected_entry[2]
    expect(len(sources) == 4 and sources[0][1:4] == (0, -1, -1) and
           sources[0][4] == (Symbol("LbnfSourceSpanV1"), 0, 11, 0, 11) and
           [row[1:4] for row in sources[1:]] == [(1, 0, 0), (2, 1, 0), (3, 1, 1)],
           "compact source-map edges recover every typed term position without retaining the AST")
    duplicate_entries = result["duplicate-projected"][1]
    expect(len(duplicate_entries) == 2 and duplicate_entries[0] == duplicate_entries[1],
           "equal-valued parser alternatives keep their ordered multiplicity after projection")
    expect(headed(result["ambiguous-parse"], "ParserAcceptedV1", 2) and
           len(result["ambiguous-parse"][1]) == 2 and
           headed(result["ambiguous-projected"], "LangDef:ProjectedTermsV1", 2) and
           len(result["ambiguous-projected"][1]) == 2 and
           result["ambiguous-projected"][1][0] == result["ambiguous-projected"][1][1] and
           result["ambiguous-projected"][1][0][1] == (Symbol("NatZ"),),
           "independent compatible productions retain two real derivations and two equal typed terms")
    expect(headed(result["unshared-join"], "SyntaxGSLTRefusedV1", 2) and
           result["unshared-join"][1] == Symbol("label-conflict"),
           "equal category spellings do not silently identify independently authored signature origins")
    expect(headed(result["joined-syntax"], "SyntaxGSLTAdmittedV1", 1) and
           result["joined-term"] == (Symbol("NatS"), (Symbol("NatAdd"), (Symbol("NatZ"),), (Symbol("NatOne"),))) and
           headed(result["joined-admission"], "LangDef:TermAdmitted", 0) and
           terms("joined-rewrite") == [(Symbol("NatS"), (Symbol("NatOne"),))],
           "a MeTTa-authored module fragment shares the signature, joins syntax and reaches typed rewriting")
    expect(headed(result["wrong-projection"], "LangDef:ProjectionRejectedV1", 4) and
           result["wrong-projection"][1:3] == (Symbol("type_mismatch"), 0),
           "projection refuses an incorrect selected category as positioned data")
    expect(headed(result["mixed-projection"], "LangDef:ProjectionRejectedV1", 4) and
           result["mixed-projection"][1:3] == (Symbol("unknown_constructor"), 1),
           "a bad alternative cannot publish an accepted batch prefix")
    for key in ("bad-index", "missing-index", "bad-outcome"):
        expect(headed(result[key], "LangDef:ProjectionRejectedV1", 4) and
               result[key][1:3] == (Symbol("bad_argument"), -1),
               "malformed projection operands remain explicit data rather than evaluator faults")
    expect(result["rejected-projection"] == (Symbol("ParserRejectedV1"), 2, 2, (Symbol("ExpectedV1"),)) and
           result["incomplete-projection"] == (Symbol("ParserIncompleteV1"), 2, 2, StringLiteral("budget")) and
           result["cyclic-projection"] in ((Symbol("ParserCyclicV1"), Symbol("True")),
                                           (Symbol("ParserCyclicV1"), Symbol("true"))),
           "projection preserves rejected, incomplete and cyclic parser outcomes")
    term_equations = ('(= (NatZ) host-zero)\n'
                      '(= (NatS $value) host-successor)\n'
                      '(= (NatAdd $left $right) host-addition)\n'
                      '(= (SeqEmpty) host-empty)\n'
                      '(= (SeqCons $value) host-cons)\n'
                      '(= (SeqJoin $left $right) host-join)\n'
                      '(= (PApp $head $arguments) host-pattern)\n'
                      '(= (LangDef:RelationRows) host-relations)\n'
                      '(= (LbnfAstNodeV1 $c $l $s $v) host-ast)\n'
                      '(= (LangDef:ProjectedTermV1 $t $s) host-projection)\n'
                      '(= (LangDef:ProjectedTermsV1 $t $s) host-projections)\n'
                      '(= (LangDef:TermSourceV1 $i $p $c $s $v) host-source)\n')
    import_line, rest = program.split("\n", 1)
    hostile = [v for v in run(binary, language, output, "rewrite-bundles-hostile",
                             import_line + "\n" + HOSTILE_CONSTRUCTORS + term_equations + rest)
               if headed(v, "ModuleRewriteBundlesV1", len(observed))]
    expect(hostile == values, "guest equations cannot interpret declaration data or explicit object-language terms")
    print(f"PASS: {language} composed rewrite bundles, independent core rows, conditional traces, multiplicity and non-interference controls")
    return checks


def mixed_profile_controls(binary, language, output):
    # The complete core may serve several partial consumers. Its binding
    # declaration must not prevent use of unrelated constructor terms.
    core = '''(GSLTLanguageDefWireV1 "MixedProfiles"
      (LCons (TypeDecl "Nat" CarrierAst) (LCons (TypeDecl "Scoped" CarrierAst) LNil))
      (LCons (GrammarRule "Z" "Nat" LNil LNil EvalNone)
      (LCons (GrammarRule "S" "Nat" (LCons (TermSimple "n" (TBase "Nat")) LNil) LNil EvalNone)
      (LCons (GrammarRule "Binder" "Scoped"
        (LCons (TermAbstractionNamed (BSome "x") "body" (TBase "Nat")) LNil) LNil EvalNone) LNil)))
      LNil
      (LCons (RewriteRule "Step" (LCons (TypeBinding "n" (TBase "Nat")) LNil) LNil
        (PApp "S" (LCons (FVar "n") LNil)) (FVar "n")) LNil))'''
    parsed = '''(ParserAcceptedV1 ((LbnfAstNodeV1 "Nat" "S"
      (LbnfSourceSpanV1 0 4 0 4) (LCons (LbnfAstNodeV1 "Nat" "Z"
        (LbnfSourceSpanV1 2 3 2 3) LNil) LNil))) (ParserSpanV1 0 4 0 4))'''
    bindings = [
        ("$authority", form("langdef:load-language-value", core)),
        ("$admitted", form("langdef:admit-term-data", "$authority", text("Nat"), "(S (Z))")),
        ("$projected", form("langdef:project-parser-result", "$authority", text("Nat"), parsed)),
        ("$term", form("langdef:projected-term", "$projected", "0")),
        ("$rewritten", form("langdef:rewrite-term-at-data", "$authority", text("Nat"), text("Nat"),
                            "8", "10000", "(LangDef:RelationRows)", "$term")),
        ("$binding", form("langdef:admit-term-data", "$authority", text("Scoped"), "(Binder (Z))")),
        ("$closed", form("langdef:close-language", "$authority")),
    ]
    body = "!" + lets(bindings,
        "(function (return (MixedConsumerProfileV1 $admitted $term $rewritten $binding)))") + "\n"
    prefix = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n'
    result = [v for v in run(binary, language, output, "mixed-consumer-profile", prefix + body)
              if headed(v, "MixedConsumerProfileV1", 4)]
    if len(result) != 1:
        raise AssertionError((language, "mixed consumer record", result))
    row = result[0]
    assert headed(row[1], "LangDef:TermAdmitted", 0), row
    assert row[2] == (Symbol("S"), (Symbol("Z"),)), row
    assert headed(row[3], "LangDef:ContextualTermResults", 2), row
    assert row[3][1] == Symbol("LangDef:ContextComplete"), row
    assert headed(row[3][2], "LangDef:ContextualTermResult", 2) and row[3][2][1] == (Symbol("Z"),), row
    assert headed(row[4], "LangDef:TermRejected", 2) and row[4][1] == Symbol("unsupported_profile"), row
    hostile = [v for v in run(binary, language, output, "mixed-consumer-hostile",
                              prefix + HOSTILE_CONSTRUCTORS + '(= (S $x) corrupted-term)\n' + body)
               if headed(v, "MixedConsumerProfileV1", 4)]
    assert hostile == result, (language, "mixed consumer data boundary", hostile, result)
    print(f"PASS: {language} mixed-profile admission, projection, rewrite, refusal and inert data")
    return 7


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--reader-artifact", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/module-values-public")
    parser.add_argument("--language", choices=("he", "petta", "prime"), action="append")
    parser.add_argument("--emit-build", type=Path)
    parser.add_argument("--baked-parser")
    parser.add_argument("--baked-reference", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks = 0
    for language in args.language or ("he", "petta", "prime"):
        bindings = [
            ("$reader", reader_load(args.reader_artifact.resolve() if args.reader_artifact else None)),
            ("$module", form("module:read-file", "$reader", text("recipes"), text(str(ROOT / "tests/module_values/Recipes.module")))),
            ("$closed-reader", form("langdef:close-parser", "$reader")),
            ("$presentation", form("module:assemble", "$module", text("Example"), "LNil", "LNil", "OriginViewV1", text("recipes:example:1"))),
            ("$core-first", form("module:core", "$presentation", text("Example"))),
            ("$language", form("module:language", "$presentation", text("Example"), text("T"), "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            ("$core", form("module:language-core", "$language")),
            ("$syntax", form("module:language-syntax", "$language")),
            ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
            ("$parsed", form("langdef:parser-parse-text", "$parser", text("C"))),
            ("$repeated", form("langdef:parser-parse-text", "$parser", text("C"))),
            ("$authority", form("langdef:load-language-value", "$core")),
            ("$admitted", form("langdef:admit-term-data", "$authority", text("T"), "(C)")),
            ("$rewritten", form("langdef:rewrite-term-at-data", "$authority", text("T"), text("T"), "100", "10000", "(LangDef:RelationRows)", "(C)")),
            ("$computed-admitted", form("langdef:admit-term", "$authority", text("T"), "(module-test:source)")),
            ("$computed-rewritten", form("langdef:rewrite-term-at", "$authority", text("T"), text("T"), "100", "10000", "(LangDef:RelationRows)", "(module-test:source)")),
            ("$closed-authority", form("langdef:close-language", "$authority")),
            ("$closed-parser", form("langdef:close-parser", "$parser")),
            ("$signature", form("module:assemble", "$module", text("SignatureOnly"), "LNil", "LNil", "OriginViewV1", text("recipes:signature:1"))),
            ("$signature-core", form("module:core", "$signature", text("Signature"))),
        ]
        program = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n' + \
            '(= (module-test:source) (C))\n!' + lets(bindings,
            "(function (return (ModuleWorkflowV1 $module $presentation $core $syntax $parsed $repeated $admitted $rewritten $core-first $signature-core $computed-admitted $computed-rewritten)))") + "\n"
        values = [v for v in run(args.cetta.resolve(), language, output, "workflow", program) if headed(v, "ModuleWorkflowV1", 12)]
        if len(values) != 1:
            raise AssertionError((language, "workflow result", values))
        result = values[0]
        if not headed(result[1], "ModuleValueV1", 7) or not headed(result[2], "ModulePresentationV1", 5):
            raise AssertionError((language, "compact assembly", result[:3]))
        if not headed(result[3], "GSLTLanguageDefWireV1", 5) or not headed(result[5], "ParserAcceptedV1", 2) or result[5] != result[6]:
            raise AssertionError((language, "language and repeated parsing", result[3:7]))
        if not headed(result[7], "LangDef:TermAdmitted", 0):
            raise AssertionError((language, "term admission", result[7]))
        if result[3] != result[9]:
            raise AssertionError((language, "parser preparation changed the authoritative core", result[3], result[9]))
        expected_signature = (Symbol("GSLTLanguageDefWireV1"), StringLiteral("Signature"),
            (Symbol("LCons"), (Symbol("TypeDecl"), StringLiteral("Opaque"), Symbol("CarrierAst")), Symbol("LNil")),
            Symbol("LNil"), Symbol("LNil"), Symbol("LNil"))
        if result[10] != expected_signature:
            raise AssertionError((language, "a signature does not require concrete syntax", result[10]))
        rewritten = result[8]
        if not headed(rewritten, "LangDef:ContextualTermResults", 2) or rewritten[1] != Symbol("LangDef:ContextComplete") or \
                not headed(rewritten[2], "LangDef:ContextualTermResult", 2) or rewritten[2][1] != (Symbol("D"),):
            raise AssertionError((language, "declared rewrite", rewritten))
        if result[11] != result[7] or result[12] != rewritten:
            raise AssertionError((language, "computed helper operands retain the original consumer contract", result[11:]))
        import_line, rest = program.split("\n", 1)
        hostile = [v for v in run(args.cetta.resolve(), language, output, "hostile-constructors",
                   import_line + "\n" + HOSTILE_CONSTRUCTORS + rest) if headed(v, "ModuleWorkflowV1", 12)]
        if hostile != values:
            raise AssertionError((language, "guest equations interpreted core declaration data", hostile, values))
        checks += 11
        checks += mixed_profile_controls(args.cetta.resolve(), language, output)
        print(f"PASS: {language} core-first module, independent signature, hostile constructors, reused parser, term admission and declared rewrite")
        checks += rewrite_bundle_controls(args.cetta.resolve(), language, output,
                                          args.reader_artifact.resolve() if args.reader_artifact else None,
                                          args.emit_build, args.baked_parser, args.baked_reference)
    print(f"(ModuleWorkflowV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
