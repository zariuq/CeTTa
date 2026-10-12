#!/usr/bin/env python3
"""Exercise authored LBNF values and the public parser lifecycle."""

import argparse
from itertools import count
from pathlib import Path
import subprocess
import sys

from gslt2parse_schema_v1 import Symbol, StringLiteral, parse_sexprs
from test_composable_syntax_v1 import form, lets, text

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTED = "(SyntaxLexicalPolicyV1 bnfc-documented)"
CFLEX = "(SyntaxLexicalPolicyV1 bnfc-2.9.6.3-c-flex-2.6.4)"
LOWER_SCOPES = count()


def headed(value, name, arity=None):
    return (isinstance(value, tuple) and value and value[0] == Symbol(name)
            and (arity is None or len(value) == arity + 1))


def llist(value):
    out = []
    while headed(value, "LCons", 2):
        out.append(value[1])
        value = value[2]
    if value != Symbol("LNil"):
        raise AssertionError("malformed finite list")
    return out


def scalar_text(value):
    if isinstance(value, StringLiteral):
        return value.text
    out = []
    while headed(value, "bnf-v1:text-cons", 2):
        cp = value[1]
        if not isinstance(cp, int) or not 0 <= cp <= 0x10ffff or 0xd800 <= cp <= 0xdfff:
            raise AssertionError("invalid scalar payload")
        out.append(chr(cp))
        value = value[2]
    if not headed(value, "bnf-v1:text-nil", 0):
        raise AssertionError("malformed scalar text")
    return "".join(out)


def output_values(stdout):
    values = []
    for line in stdout.splitlines():
        if not line.strip() or line == "true":
            continue
        body = line[1:-1] if line.startswith("[") and line.endswith("]") else line
        values.extend(parse_sexprs(body))
    return [value for value in values if value != ()]


def normalized_syntax(inspection):
    if not headed(inspection, "SyntaxGSLTInspectionV1", 4):
        raise AssertionError(inspection)
    language, profile, policy, _origins = inspection[1:]
    if not headed(language, "GSLTLanguageDefWireV1", 5) or not headed(profile, "GSLTParserProfileLayerV2", 5):
        raise AssertionError("wrong syntax carrier")
    tokens = profile[5]
    if not headed(tokens, "ParserTokenLayerV1", 5):
        raise AssertionError("missing token profile")
    declarations = []
    for row in llist(tokens[2]):
        if headed(row, "RegularTokenDeclV1", 5):
            declarations.append(row[:5])
        elif headed(row, "RegularTriviaDeclV1", 3):
            declarations.append(row[:3])
        else:
            raise AssertionError("unknown lexical declaration")
    comments = [row[:-1] for row in llist(tokens[3])]
    projections = [row[2:] for row in llist(tokens[4])]
    # Source sites, origins, occurrence keys, and the caller's envelope name
    # are incidental here. Ordered grammar rows, signatures, lexical rules,
    # visibility, AST projection, start category and entrypoints are not.
    return (language[2:], profile[2:5], policy, tokens[1], tuple(declarations),
            tuple(comments), tuple(projections), tokens[5])


def lower_read(source, name="Sample", start="LbnfDefaultStartV1", policy=DOCUMENTED,
               file=False, namespace=None):
    operation = "lbnf:read-file" if file else "lbnf:read-text"
    scope = next(LOWER_SCOPES)
    read, binding, origin, document, other = (
        f"$lbnf{scope}_{role}" for role in ("read", "binding", "source", "document", "other"))
    pattern, operand = document, document
    if namespace is not None:
        declaration_namespace, definitions = f"$lbnf{scope}_namespace", f"$lbnf{scope}_definitions"
        pattern = form("LbnfDocumentV1", declaration_namespace, definitions)
        operand = form("LbnfDocumentV1", text(namespace), definitions)
    return lets([(read, form(operation, text(str(source))))],
        form("case", form("lbnf:finish-read", read), form(
            form(form("LbnfReadValueV1", binding, origin, pattern),
                 form("lbnf:lower-document", operand, text(name), start, policy)),
            form(other, form("LbnfClientReadFailureV1", other)))))


def query(value):
    return "!" + value + "\n"


def library_import():
    return query(form("import!", "&self", text(str(ROOT / "lib/lib_lbnf.metta"))))


def parsed(syntax, source, evidence=False):
    operation = "langdef:parse-authored-text-evidence" if evidence else "langdef:parse-authored-text"
    args = ["$parser", text(source)] + (["100000"] if evidence else [])
    parse = lets([("$parser", form("langdef:load-syntax-gslt-parser", syntax)),
                 ("$result", form(operation, *args)),
                 ("$_closed", form("langdef:close-authored-parser", "$parser"))], "$result")
    return form("case", syntax, form(
        form(form("SyntaxGSLTAdmittedV1", "$entry"), parse),
        form("$not-admitted", "$not-admitted")))


def tree(value):
    if headed(value, "LbnfAstNodeV1", 4):
        return (value[2].text, tuple(tree(child) for child in llist(value[4])))
    if headed(value, "LbnfAstListV1", 3):
        return ("list", tuple(tree(child) for child in llist(value[3])))
    if headed(value, "LbnfAstTokenV1", 6):
        return (value[1].text, scalar_text(value[3]))
    raise AssertionError("not a labelled AST")


def accepted_trees(value):
    if headed(value, "LangDef:AuthoredParseAccepted", 4):
        return [tree(ast) for ast in value[4]]
    if headed(value, "LangDef:AuthoredParseEvidenceAccepted", 6):
        return [tree(ast) for ast in value[6]]
    raise AssertionError("expected completed parse acceptance: " + repr(value)[:240])


def public_controls(binary, scratch):
    program = library_import()
    names = []

    def observe(name, expression):
        names.append(name)
        return query(expression)

    left = 'Pair. E ::= "(" E "," E ")"; X. E ::= "x";'
    right = 'Y. E ::= "y";'
    bindings = [("$left", lower_read(left, "Left")), ("$right", lower_read(right, "Right")),
                ("$joined", form("langdef:join-syntax-gslts", text("Joined"), text("E"), "$left", "$right"))]
    program += observe("combined", lets(bindings, parsed("$joined", "(x,y)", True)))
    program += observe("inspection", lets(bindings, form("langdef:inspect-syntax-gslt", "$joined")))
    for name, source in [("left-alone", left), ("right-alone", right)]:
        program += observe(name, lets([("$syntax", lower_read(source))], parsed("$syntax", "(x,y)")))
    program += observe("repeated", lets(bindings + [("$again", form("langdef:join-syntax-gslts",
        text("Joined"), text("E"), "$joined", "$left"))],
        form("langdef:inspect-syntax-gslt", "$again")))
    program += observe("mixed-policy", lets([("$a", lower_read('X. E ::= "x";')),
        ("$b", lower_read('Y. E ::= "y";', policy=CFLEX))], form("langdef:join-syntax-gslts",
        text("Never"), text("E"), "$a", "$b")))
    lexical_cases = [
        ("keyword", 'Word. E ::= Ident;', 'Keyword. E ::= "if";', "if"),
        ("longer", 'Gt. E ::= ">"; Pair. E ::= E E;', 'Shift. E ::= ">>";', ">>"),
    ]
    for name, old, added, source in lexical_cases:
        lexical = [("$a", lower_read(old, "A")), ("$b", lower_read(added, "B")),
                   ("$joined", form("langdef:join-syntax-gslts", text("Joined"), text("E"), "$a", "$b"))]
        for suffix, value in [("before", "$a"), ("after", "$joined")]:
            program += observe(name + "-" + suffix, lets(lexical, parsed(value, source, True)))
    program += observe("lists-coercions", lets([("$syntax", lower_read(
        'Root. Root ::= [E]; separator E ","; X. E1 ::= Integer; coercions E 1;'))],
        parsed("$syntax", "1,(2)")))
    program += observe("internal-keyword", lets([("$syntax", lower_read(
        'internal Hidden. E ::= "reserved"; W. E ::= Ident;'))], parsed("$syntax", "reserved")))
    program += observe("position", lets([("$syntax", lower_read(
        'Root. Root ::= Word; position token Word letter+;'))], parsed("$syntax", "\na")))
    duplicate = [("$a", lower_read('X. E ::= "x";', "A", namespace="independent:A")),
                 ("$b", lower_read('X. E ::= "x";', "B", namespace="independent:B")),
                 ("$joined", form("langdef:join-syntax-gslts", text("Joined"), text("E"), "$a", "$b"))]
    program += observe("distinct-occurrences", lets(duplicate, parsed("$joined", "x", True)))
    program += observe("distinct-inspection", lets(duplicate, form("langdef:inspect-syntax-gslt", "$joined")))
    shared = [("$a", lower_read('X. E ::= "x";', "A")),
              ("$b", lower_read('X. E ::= "x";', "B")),
              ("$joined", form("langdef:join-syntax-gslts", text("Joined"), text("E"), "$a", "$b"))]
    program += observe("same-declarations", lets(shared, form("langdef:inspect-syntax-gslt", "$joined")))
    for name, first, second in [
        ("join-parameter-conflict", 'X. E ::= Integer;', 'X. E ::= Ident;'),
        ("join-category-conflict", 'X. E ::= "x";', 'X. F ::= "f";'),
    ]:
        program += observe(name, lets([("$a", lower_read(first, "A")),
            ("$b", lower_read(second, "B"))], form("langdef:join-syntax-gslts",
            text("Never"), text("E"), "$a", "$b")))
    program += observe("atomic-join-refusal", lets(bindings + [
        ("$parser", form("langdef:load-syntax-gslt-parser", "$joined")),
        ("$conflict", lower_read('X. E ::= Integer;', "Conflict")),
        ("$_refused", form("langdef:join-syntax-gslts", text("Never"), text("E"), "$left", "$conflict")),
        ("$result", form("langdef:parse-authored-text", "$parser", text("(x,y)"))),
        ("$_closed", form("langdef:close-authored-parser", "$parser"))], "$result"))
    associative = [("$a", lower_read(left, "A")), ("$b", lower_read(right, "B")),
                   ("$c", lower_read('Z. E ::= "z";', "C"))]
    for side in ("left", "right"):
        ab = form("langdef:join-syntax-gslts", text("AB"), text("E"), "$a", "$b")
        bc = form("langdef:join-syntax-gslts", text("BC"), text("E"), "$b", "$c")
        composition = form("langdef:join-syntax-gslts", text("ABC"), text("E"),
            "$ab" if side == "left" else "$a", "$c" if side == "left" else "$bc")
        program += observe("associative-" + side, lets(associative + [
            ("$ab" if side == "left" else "$bc", ab if side == "left" else bc),
            ("$joined", composition)], form("langdef:inspect-syntax-gslt", "$joined")))
    for name, source, reason in [
        ("missing", 'X. E ::= Missing;', "missing-production"),
        ("signature", 'X. E ::= "x"; X. F ::= "f";', "constructor-signature-conflict"),
        ("nullable", 'X. E ::= "x"; token Word letter*;', "nullable-token"),
        ("layout", 'X. E ::= "x"; layout toplevel;', "layout"),
        ("define", 'X. E ::= "x"; define f x = x;', "define"),
    ]:
        program += observe(name, lower_read(source))
    program += '(= (LbnfRuleV1 $a $b $c $d) (println! "LBNF-UNEXPECTED-EFFECT"))\n'
    program += '(= (LbnfDocumentV1 $a $b) (println! "LBNF-UNEXPECTED-EFFECT"))\n'
    program += observe("read-data", "(lbnf:read-text \"X. E ::= \\\"x\\\";\")")
    program += observe("lower-data", lower_read('X. E ::= "x";'))
    program += observe("inspect-data", lets([("$document", "(lbnf:bootstrap-document)")],
        form("lbnf:inspect-document", "$document")))
    answers = run_profile(binary, scratch, "public-client", program)
    for language, values in answers.items():
        if len(names) != 29 or len(values) != len(names):
            raise AssertionError((language, "missing controls", len(names), len(values)))
        rows = dict(zip(names, values))
        assert accepted_trees(rows["combined"]) == [("Pair", (("X", ()), ("Y", ())))]
        assert headed(rows["inspection"], "SyntaxGSLTInspectionV1", 4)
        assert headed(rows["left-alone"], "LangDef:AuthoredParseRejected", 5)
        assert headed(rows["right-alone"], "LangDef:AuthoredLexicalRefusedV1", 5)
        assert headed(rows["right-alone"][5], "ParserLexicalWitnessV1", 4)
        assert rows["repeated"] == rows["inspection"]
        assert headed(rows["mixed-policy"], "SyntaxGSLTRefusedV1", 2)
        assert rows["mixed-policy"][1] == Symbol("policy-mismatch")
        assert accepted_trees(rows["keyword-before"])[0][0] == "Word"
        assert accepted_trees(rows["keyword-after"]) == [("Keyword", ())]
        assert accepted_trees(rows["longer-before"]) == [("Pair", (("Gt", ()), ("Gt", ())))]
        assert accepted_trees(rows["longer-after"]) == [("Shift", ())]
        assert accepted_trees(rows["lists-coercions"]) == [
            ("Root", (("list", (("X", (("Integer", "1"),)), ("X", (("Integer", "2"),)))),))]
        assert accepted_trees(rows["internal-keyword"]) == [("W", (("Ident", "reserved"),))]
        position_ast = rows["position"][4][0][4][1]
        assert headed(position_ast, "LbnfAstTokenV1", 6)
        assert position_ast[4] == (Symbol("LbnfTokenPositionV1"), 2, 1)
        assert accepted_trees(rows["distinct-occurrences"]) == [("X", ())]
        tokens = rows["distinct-inspection"][2][5]
        identities = [row[1] for row in llist(tokens[4])]
        assert len(identities) == 2 and identities[0] != identities[1]
        forest = rows["distinct-occurrences"][5]
        choices = llist_forest(forest[5])
        occurrences = {choice[2] for choice in choices if headed(choice, "pf-choice", 5)
                       and headed(choice[2], "pp-occurrence", 1)}
        assert occurrences == {(Symbol("pp-occurrence"), identity) for identity in identities}
        shared_metadata = rows["same-declarations"][2][5][4]
        assert len(llist(shared_metadata)) == 1
        for name in ("join-parameter-conflict", "join-category-conflict"):
            value = rows[name]
            assert headed(value, "SyntaxGSLTRefusedV1", 2)
            assert value[1] == Symbol("constructor-signature-conflict")
            witness = value[2]
            assert headed(witness, "SyntaxConflictWitnessV1", 2)
            assert all(headed(entry, "SyntaxDeclarationOccurrenceV1", 4) for entry in witness[1:])
            assert witness[1][2] != witness[2][2]
        assert accepted_trees(rows["atomic-join-refusal"]) == [("Pair", (("X", ()), ("Y", ())))]
        assert rows["associative-left"] == rows["associative-right"]
        for name, reason in [("missing", "missing-production"),
            ("signature", "constructor-signature-conflict"), ("nullable", "nullable-token"),
            ("layout", "layout"), ("define", "define")]:
            assert headed(rows[name], "LbnfRefusedV1", 3) and rows[name][1] == Symbol(reason)
            assert headed(rows[name][2], "LbnfSourceSpanV1", 4)
        assert headed(rows["read-data"], "LbnfReadValueV1", 3)
        assert headed(rows["read-data"][3], "LbnfDocumentV1", 2)
        assert headed(rows["lower-data"], "SyntaxGSLTAdmittedV1", 1)
        assert headed(rows["inspect-data"], "LbnfDocumentV1", 2)
        print(f"(LbnfPublicClientV1Summary {language} {len(rows)} 0)")


def llist_forest(value):
    out = []
    while headed(value, "cons", 2):
        out.append(value[1]); value = value[2]
    if value != Symbol("nil"):
        raise AssertionError("malformed forest relation")
    return out


def run_profile(binary, scratch, name, program):
    path = scratch / (name + ".metta")
    path.write_text(program)
    answers = {}
    for language, profile in [("prime", None), ("he", "extended"), ("petta", "extended")]:
        command = [str(binary.resolve()), "--lang", language]
        if profile:
            command += ["--profile", profile]
        result = subprocess.run(command + [str(path)], cwd=ROOT,
                                capture_output=True, text=True, timeout=120)
        (scratch / (name + "-" + language + ".stdout")).write_text(result.stdout)
        (scratch / (name + "-" + language + ".stderr")).write_text(result.stderr)
        if result.returncode or result.stderr or "LBNF-UNEXPECTED-EFFECT" in result.stdout:
            raise AssertionError((language, result.returncode, result.stderr))
        answers[language] = output_values(result.stdout)
    if answers["prime"] != answers["he"] or answers["he"] != answers["petta"]:
        raise AssertionError("public profile values disagree")
    return answers


def bootstrap(binary, bnfc_root, scratch):
    original = lets([("$doc", "(lbnf:bootstrap-document)"),
                     ("$syntax", form("lbnf:lower-document", "$doc", text("Bootstrap"),
                                      text("Grammar"), DOCUMENTED))],
                    form("langdef:inspect-syntax-gslt", "$syntax"))
    external = lower_read(bnfc_root / "source/src/BNFC.cf", "Bootstrap", text("Grammar"), file=True)
    program = library_import() + query(original)
    program += query(lets([("$syntax", external)], form("langdef:inspect-syntax-gslt", "$syntax")))
    answers = run_profile(binary, scratch, "self-bootstrap", program)
    for language, values in answers.items():
        if len(values) != 2 or normalized_syntax(values[0]) != normalized_syntax(values[1]):
            (scratch / ("bootstrap-" + language + "-mismatch.txt")).write_text(repr(values))
            raise AssertionError(language + ": normalized bootstrap grammar/profile/projection mismatch")
        print(f"(LbnfSelfBootstrapV1Summary {language} 1 0)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--bnfc-root", type=Path)
    parser.add_argument("--scratch", type=Path, required=True)
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    sys.setrecursionlimit(10000)
    public_controls(args.cetta, args.scratch)
    if args.bnfc_root:
        bootstrap(args.cetta, args.bnfc_root, args.scratch)


if __name__ == "__main__":
    main()
