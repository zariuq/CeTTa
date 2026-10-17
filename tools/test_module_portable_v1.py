#!/usr/bin/env python3
"""Compare native module ingress with independently authored Lean fixtures."""
import argparse
import subprocess
from pathlib import Path

from gslt2parse_schema_v1 import Symbol, StringLiteral, parse_sexprs, render
from test_composable_syntax_v1 import form, lets, text
from test_parser_artifacts_v1 import HOST_ENDPOINTS
from test_lbnf_values_v1 import headed, output_values
from test_module_values_v1 import rows

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/module-portable-v1")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    count = 0
    for host in ("he", "petta"):
        library = ROOT / ("lib/lib_module.metta" if host == "he" else "lib/petta/lib_module.metta")
        prefix = "!(import! &self " + text(str(library)) + ")\n"
        prefix += "!(import! &self " + text(str(library.parent / "str.metta")) + ")\n"

        def check(name, expression, predicate):
            nonlocal count
            program = prefix + "!" + lets([("$value", expression)], form("function", form("return", "$value"))) + "\n"
            source = args.output / f"{host}-{name}.metta"
            source.write_text(program)
            endpoint, profile = HOST_ENDPOINTS[host]
            proc = subprocess.run([str(args.cetta.resolve()), "--lang", endpoint, "--profile", profile, str(source.resolve())],
                                  cwd=ROOT, text=True, capture_output=True, timeout=180)
            (args.output / f"{host}-{name}.stdout").write_text(proc.stdout)
            (args.output / f"{host}-{name}.stderr").write_text(proc.stderr)
            if proc.returncode:
                raise AssertionError((host, name, proc.returncode, proc.stderr))
            # PeTTa displays control characters literally inside strings. Adapt
            # that display for the test's JSON-string reader without changing
            # either dialect's source-string interpretation or the module data.
            displayed = []
            quoted_string = escaped = False
            for char in proc.stdout:
                if quoted_string and ord(char) < 32:
                    displayed.append(f"\\u{ord(char):04x}")
                else:
                    displayed.append(char)
                if escaped:
                    escaped = False
                elif quoted_string and char == "\\":
                    escaped = True
                elif char == '"':
                    quoted_string = not quoted_string
            result = output_values("".join(displayed))
            if len(result) != 1 or not predicate(result[0]):
                raise AssertionError((host, name, result))
            count += 1

        def check_core(name, expression, reference):
            check(name, expression, lambda value: value == reference)

        for name in ("regex", "complete"):
            reference = parse_sexprs((ROOT / f"tests/module_portable/{name}.sexpr").read_text())[0]
            loaded = form("module:read-file", "ModulePortableV1", text("portable"), text(str(ROOT / f"tests/module_portable/{name}.module")))
            check_core("portable-" + name, lets([("$module", loaded)], form("module:core", "$module", text("core"))), reference)
            check_core("roundtrip-" + name, lets([
                ("$original", loaded),
                ("$encoded", form("module:write-text", "ModulePortableV1", "$original")),
                ("$restored", form("module:read-text", "ModulePortableV1", text("restored"), "$encoded")),
            ], form("module:core", "$restored", text("core"))), reference)

        decorated = form("module:read-file", "ModulePortableV1", text("same"), text(str(ROOT / "tests/module_portable/decorated.module")))
        check("decorated-roundtrip", lets([
            ("$original", decorated),
            ("$encoded", form("module:write-text", "ModulePortableV1", "$original")),
            ("$restored", form("module:read-text", "ModulePortableV1", text("same"), "$encoded")),
            ("$reencoded", form("module:write-text", "ModulePortableV1", "$restored")),
        ], form("PortableRoundtripV1", form("==", "$encoded", "$reencoded"))),
            lambda value: headed(value, "PortableRoundtripV1", 1) and value[1] in (Symbol("True"), Symbol("true")))

        reference = parse_sexprs((ROOT / "tests/module_portable/regex.sexpr").read_text())[0]
        readable = lets([
            ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
            ("$module", form("module:read-file", "$reader", text("regex"), text(str(ROOT / "langdef/regex/RegexCore.module")))),
            ("$closed", form("langdef:close-parser", "$reader")),
            ("$theory", form("module:assemble", "$module", text("RegexDerivatives"), "LNil", "LNil", "OriginViewV1", text("regex:1"))),
        ], form("module:core", "$theory", text("RegexDerivatives")))
        check_core("readable-exact", readable, reference)
        for name, constructor, expected, rule in (
            ("nullable-zero", "rx:zero", "rx:false", "NULL-ZERO"),
            ("nullable-epsilon", "rx:epsilon", "rx:true", "NULL-EPSILON"),
            ("nullable-bad", "NoRegex", None, None),
        ):
            def result_matches(value, expected=expected, rule=rule):
                if expected is None:
                    return headed(value, "LangDef:ContextualCodecFault", 3) and value[2] == Symbol("unknown_constructor")
                return (headed(value, "LangDef:ContextualTermResults", 2) and
                        value[2][1] == (Symbol(expected),) and value[2][2][2] == StringLiteral(rule))
            check(name, lets([
                ("$core", readable),
                ("$authority", form("langdef:load-language-value", "$core")),
                ("$result", form("langdef:rewrite-term-at-data", "$authority", text("Bool"), text("Bool"),
                    "8", "100000", "(LangDef:RelationRows)", form("rx:nullable", form(constructor)))),
                ("$closed", form("langdef:close-language", "$authority")),
            ], "$result"), result_matches)
        typed = [
            ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
            ("$module", form("module:read-file", "$reader", text("typed"), text(str(ROOT / "tests/module_portable/Typed.module")))),
            ("$theory", form("module:assemble", "$module", text("Example"), "LNil", "LNil", "OriginViewV1", text("typed:1"))),
        ]
        check("typed-equation-application", lets(typed, form("module:core", "$theory", text("Typed"))),
            lambda value: headed(value, "LanguageDefV2", 5) and len(rows(value[2])) == 1 and
                len(rows(value[3])) == 2 and len(rows(value[4])) == 1 and len(rows(value[5])) == 1 and
                rows(value[4])[0][1] == StringLiteral("reflexive"))
        check("typed-bundle", lets(typed, form("module:rewrite-rules", "$theory", text("Typed"))),
            lambda value: headed(value, "ModuleRewriteRulesV1", 2) and len(rows(value[2])) == 1 and
                rows(value[2])[0][2][1] == StringLiteral("step"))
        check("typed-execution", lets(typed + [
            ("$core", form("module:core", "$theory", text("Typed"))),
            ("$authority", form("langdef:load-language-value", "$core")),
            ("$result", form("langdef:rewrite-term-at-data", "$authority", text("T"), text("T"),
                              "8", "100000", "(LangDef:RelationRows)", "(S (Z))")),
            ("$closed", form("langdef:close-language", "$authority")),
        ], "$result"), lambda value: headed(value, "LangDef:ContextualTermResults", 2) and
                value[2][1] == (Symbol("Z"),))
        check("readable-consumer-obligation", lets([
            ("$reader", form("langdef:load-baked-parser", text("module-core-reader"))),
            ("$module", form("module:read-file", "$reader", text("regex"), text(str(ROOT / "langdef/regex/RegexCore.module")))),
            ("$theory", form("module:assemble", "$module", text("RegexDerivatives"), "LNil", "LNil", "OriginViewV1", text("regex:1"))),
        ], form("module:language", "$theory", text("RegexDerivatives"), text("Regex"), "(SyntaxLexicalPolicyV1 bnfc-documented)")),
            lambda value: headed(value, "ModuleInterpretationUnknownV1", 2))

        nul_symbol = '(module:codepoints (LCons 65 (LCons 0 (LCons 66 LNil))))'
        symbol_source = '(module:document (ExtendedLanguageDefV1 "symbols/1" (LanguageDefV2 "Symbols" LNil LNil LNil LNil) (module:symbol ' + nul_symbol + ')))'
        check("counted-symbol", lets([
            ("$module", form("module:read-text", "ModulePortableV1", text("symbols"), text(symbol_source))),
        ], form("module:write-text", "ModulePortableV1", "$module")),
            lambda value: isinstance(value, StringLiteral) and nul_symbol in value.text)
        for name, marker, document in (
            ("nul-carrier-tag", "CarrierAst", '(LanguageDefV2 "NulTag" (LCons (TypeDecl "T" TAG) LNil) LNil LNil LNil)'),
            ("nul-core-head", "LanguageDefV2", '(TAG "NulTag" LNil LNil LNil LNil)'),
        ):
            points = "LNil"
            for cp in reversed([*(ord(c) for c in marker), 0, 88]):
                points = form("LCons", str(cp), points)
            name_term = form("module:codepoints", points)
            encoded_symbol = form("module:symbol", name_term)
            payload = document.replace("TAG", encoded_symbol)
            if name == "nul-core-head":
                arguments = "LNil"
                for argument in reversed([text("NulTag"), "LNil", "LNil", "LNil", "LNil"]):
                    arguments = form("LCons", argument, arguments)
                payload = form("module:application", name_term, arguments)
            encoded = "(module:document " + payload + ")"
            check(name, lets([
                ("(ModulePortableValueV1 $site $payload)", form("module:read-text", "ModulePortableV1", text("nul-tag"), text(encoded))),
            ], form("langdef:load-language-value", "$payload")),
                lambda value: headed(value, "Error", 2))
        raw_source = '(module:document "A\0B")'
        # Raw control bytes are not quoted-text syntax; portable codepoint
        # escapes above preserve the value without weakening that grammar.
        check("raw-control-source", lets([
            ("$source", form("str:from-codepoints", "(" + " ".join(str(ord(c)) for c in raw_source) + ")")),
        ], form("module:read-text", "ModulePortableV1", text("counted"), "$source")),
            lambda value: headed(value, "Error", 2))
        check("future-payload", form("module:read-text", "ModulePortableV1", text("future"),
              text('(module:document (FutureLayerV7 "opaque" 18446744073709551616))')),
              lambda value: headed(value, "ModulePortableValueV1", 2) and value[2][2] == 18446744073709551616)
        check("future-schema", form("module:read-text", "ModulePortableV1", text("future"),
              text('(module:document (Record (Field "mettail" "module/2") (Field "payload" (FutureTerm 7))))')),
              lambda value: headed(value, "ModulePortableValueV1", 2))
        check("structural-limit", form("module:write-text", "ModulePortableV1", "(X " * 513 + "Z" + ")" * 513),
              lambda value: headed(value, "ModuleRefusedV1", 3) and value[1] == Symbol("incomplete"))

        for name, source in (
            ("command", "!(module:document (LanguageDefV2 \"X\" LNil LNil LNil LNil))"),
            ("extra-document", "(module:document LNil) (module:document LNil)"),
            ("surrogate", "(module:document (module:codepoints (LCons 55296 LNil)))"),
            ("reserved-arity", "(module:document (module:symbol \"x\" \"y\"))"),
        ):
            check(name, form("module:read-text", "ModulePortableV1", text("negative"), text(source)),
                  lambda value: headed(value, "ModuleRefusedV1", 3) or headed(value, "Error", 2))

        def quoted(value):
            if isinstance(value, StringLiteral):
                return value
            if isinstance(value, Symbol):
                return (Symbol("Record"), (Symbol("Field"), StringLiteral("symbol"), StringLiteral(value.text)))
            if isinstance(value, int):
                return (Symbol("Integer"), value)
            return (Symbol("Record"), (Symbol("Field"), StringLiteral("head"), StringLiteral(value[0].text)),
                    (Symbol("Field"), StringLiteral("arguments"), (Symbol("List"), *(quoted(v) for v in value[1:]))))

        def envelope(dependencies="(List)", exports=None):
            exports = exports or form("List", form("Record", form("Field", text("name"), text("Regex")),
                form("Field", text("spec"), render(quoted(reference)))))
            return form("module:document", form("Record", form("Field", text("dependencies"), dependencies),
                form("Field", text("exports"), exports), form("Field", text("mettail"), text("module/1")),
                form("Field", text("name"), text("RegexModule"))))

        check_core("envelope-core", lets([("$module", form("module:read-text", "ModulePortableV1", text("envelope"), text(envelope())))],
            form("module:core", "$module", text("Regex"))), reference)
        commitment = form("Bytes", *("0" for _ in range(32)))
        duplicate_deps = form("List", *(form("Record", form("Field", text("uri"), text(uri)),
            form("Field", text("commitment"), commitment)) for uri in ("deps/A.module", "file:deps/A.module")))
        check("duplicate-dependencies", form("module:read-text", "ModulePortableV1", text("bad"), text(envelope(duplicate_deps))),
              lambda value: headed(value, "ModuleRefusedV1", 3))
        nul_uri = '(module:codepoints (LCons 65 (LCons 0 (LCons 66 LNil))))'
        inert_dependency = form("List", form("Record", form("Field", text("uri"), nul_uri),
            form("Field", text("commitment"), commitment)))
        check("inert-dependency-path", lets([
            ("$module", form("module:read-text", "ModulePortableV1", text("inert"), text(envelope(inert_dependency)))),
        ], form("module:write-text", "ModulePortableV1", "$module")),
            lambda value: isinstance(value, StringLiteral) and nul_uri in value.text)
        check("opaque-export", form("module:read-text", "ModulePortableV1", text("opaque"), text(envelope(exports=
            '(List (Record (Field "name" "Metadata") (Field "spec" (List (Integer 18446744073709551616) (Bytes 0 255) Nil))))'))),
            lambda value: headed(value, "ModulePortableValueV1", 2))
        check("bad-value", form("module:read-text", "ModulePortableV1", text("bad"), text(envelope(exports=
            '(List (Record (Field "name" "Metadata") (Field "spec" (Bytes 256))))'))),
            lambda value: headed(value, "ModuleRefusedV1", 3))
        check("duplicate-schema-field", form("module:read-text", "ModulePortableV1", text("bad"),
            text(envelope().replace('(Field "mettail" "module/1")', '(Field "mettail" "module/1") (Field "mettail" "module/1")'))),
            lambda value: headed(value, "ModuleRefusedV1", 3))
    print(f"(ModulePortableV1Summary {count} 0)")


if __name__ == "__main__":
    main()
