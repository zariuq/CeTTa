#!/usr/bin/env python3
"""Exercise partial interpretation discovery and explicit module adaptation."""
import argparse
from pathlib import Path

from gslt2parse_schema_v1 import Symbol
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed
from test_module_values_v1 import HOSTILE_CONSTRUCTORS, reader_load, rows
from test_parser_artifacts_v1 import run

ROOT = Path(__file__).resolve().parents[1]


def controls(binary, language, output):
    output.mkdir(parents=True, exist_ok=True)
    bindings = [
        ("$reader", reader_load(None)),
        ("$module", form("module:read-file", "$reader", text("interpretations"),
                         text(str(ROOT / "tests/module_values/Interpretations.module")))),
        ("$closed", form("langdef:close-parser", "$reader")),
    ]
    names = ("Required", "Extra", "DifferentNotation", "AlternateLawName", "Renamed",
             "MissingEquation", "DifferentStep", "WrongDirection", "Example")
    for name in names:
        bindings.append(("$" + name, form("module:assemble", "$module", text(name), "LNil",
                                         "LNil", "OriginViewV1", text("instance:" + name))))
    found = ("Required", "Extra", "DifferentNotation", "AlternateLawName", "Example")
    unknown = ("Renamed", "MissingEquation", "DifferentStep", "WrongDirection")
    observed = []
    for name in found + unknown:
        variable = "$interpret-" + name
        bindings.append((variable, form("module:try-interpret", "$Required", "$" + name, "IdentityInclusionV1")))
        observed.append(variable)
    for name in found + unknown:
        variable = "$use-" + name
        bindings.append((variable, form("module:assemble", "$module", text("Use"),
                                        form("LCons", "$" + name, "LNil"), "LNil",
                                        "OriginViewV1", text("use:" + name))))
        observed.append(variable)
    bindings += [
        ("$other-method", form("module:try-interpret", "$Required", "$Required", "FutureMethodV1")),
        ("$core", form("module:core", "$Example", text("Example"))),
        ("$authority", form("langdef:load-language-value", "$core")),
        ("$step", '(langdef:rewrite-term-at-data $authority "T" "T" 8 100000 (LangDef:RelationRows) (A))'),
        ("$authority-closed", form("langdef:close-language", "$authority")),
    ]
    observed += ["$other-method", "$step"]
    for name in ("BindingNotation", "EraseBinding", "RenameBinder"):
        bindings.append(("$" + name, form("module:assemble", "$module", text(name), "LNil",
                                         "LNil", "OriginViewV1", text("binding:" + name))))
        observed.append("$" + name)
    source = ('!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n' + HOSTILE_CONSTRUCTORS +
              "!" + lets(bindings, form("function", form("return", form("ModuleInterpretationsV1", *observed)))) + "\n")
    results = run(binary, language, output, "interpretations", source)
    values = [v for v in results if headed(v, "ModuleInterpretationsV1", len(observed))]
    assert len(values) == 1, results
    values = values[0][1:]
    checks = 0
    for index, name in enumerate(found + unknown):
        result = values[index]
        if name in found:
            assert headed(result, "ModuleInterpretationFoundV1", 4), (name, result)
            matches = rows(result[4])
            assert len(matches) == 6, (name, matches)
            checks += 2
        else:
            assert headed(result, "ModuleInterpretationUnknownV1", 2), (name, result)
            assert headed(result[2], "ModuleUnmatchedDeclarationV1", 3), (name, result)
            if name == "WrongDirection":
                assert result[2][1] == Symbol("rewrites"), result
            if name == "MissingEquation":
                assert result[2][1] == Symbol("equations"), result
            checks += 2
        applied = values[len(found + unknown) + index]
        assert headed(applied, "ModulePresentationV1" if name in found else "ModuleParameterUnknownV1",
                      5 if name in found else 3), (name, applied)
        checks += 1
    assert headed(values[-5], "ModuleInterpretationUnknownV1", 2), values[-5]
    assert headed(values[-4], "LangDef:ContextualTermResults", 2), values[-4]
    assert values[-4][1] == Symbol("LangDef:ContextComplete"), values[-4]
    assert headed(values[-4][2], "LangDef:ContextualTermResult", 2), values[-4]
    assert values[-4][2][1] == (Symbol("B"),), values[-4]
    assert headed(values[-3], "ModulePresentationV1", 5), values[-3]
    for value in values[-2:]:
        assert headed(value, "ModuleInterpretationUnknownV1", 2) and value[1] == Symbol("BindingShapeV1"), value
    checks += 5
    print(f"ModuleInterpretationsV1Summary {language} {checks} 0")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    args = parser.parse_args()
    for language in args.language or ("he", "petta", "prime"):
        controls(args.cetta.resolve(), language, args.output / language)


if __name__ == "__main__":
    main()
