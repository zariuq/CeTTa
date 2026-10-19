#!/usr/bin/env python3
"""Exercise module rules with the existing pinned PeTTaChainer in CeTTa."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import shutil

from gslt2parse_schema_v1 import Symbol, StringLiteral, Variable, parse_sexprs, render
from module_chainer_v1 import export_rules, render_metta
from test_composable_syntax_v1 import form, lets, text
from test_lbnf_values_v1 import headed, output_values
from test_parser_artifacts_v1 import run
from test_module_values_v1 import reader_load

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from petta_chainer_compat import load_manifest, materialize_sources, materialize_executable


def exported(binary, reader, output, theory):
    bindings = [("$reader", reader_load(reader)),
        ("$module", form("module:read-file", "$reader", text("chainer-source"),
            text(str(ROOT / "tests/module_values/RewriteBundles.module")))),
        ("$naturals", form("module:assemble", "$module", text("Naturals"), "LNil", "LNil",
            "OriginViewV1", text("chainer-naturals"))),
        ("$sequences", form("module:assemble", "$module", text("Sequences"), "LNil", "LNil",
            "OriginViewV1", text("chainer-sequences"))),
        ("$derived", form("module:read-file", "$reader", text("chainer-derived-source"),
            text(str(ROOT / "tests/module_values/DerivedSubstitution.module")))),
        ("$catalog", '(LCons (ModuleCatalogEntryV1 "RewriteBundles" $module) LNil)'),
        ("$rules", form("module:assemble", "$derived" if theory in ("NamedZero", "Combined") else "$module", text(theory),
            "(LCons $naturals (LCons $sequences LNil))" if theory == "Together" else "(LCons $naturals LNil)",
            "$catalog", "OriginViewV1", text("chainer-" + theory))),
        ("$view", form("module:rewrite-rules", "$rules", text(theory))),
        ("$close", form("langdef:close-parser", "$reader"))]
    program = '!(import! &self ' + text(str(ROOT / "lib/lib_module.metta")) + ')\n!' + \
        lets(bindings, "(function (return $view))") + "\n"
    values = [v for v in run(binary, "petta", output, theory, program)
              if headed(v, "ModuleRewriteRulesV1", 2)]
    if len(values) != 1:
        raise AssertionError(("ordered module rule export", values))
    return values[0]


def controls(binary, reader, output, chainer, petta):
    image = output / "chainer-sources"
    image.mkdir(parents=True, exist_ok=True)
    manifest = load_manifest(ROOT / "tests/petta/chainer_compat/manifest.json")
    materialize_sources(image, manifest, chainer, petta)
    shutil.copyfile(ROOT / "lib/petta/langdef.metta", image / "lib/langdef.metta")
    image_binary = materialize_executable(binary, image)
    views = {name: exported(binary, reader, output, name)
             for name in ("Together", "DuplicatePaths", "NamedZero", "Combined")}
    calls = []
    for name, view in views.items():
        authority = Variable("authority" + name)
        rules = export_rules(view, authority, StringLiteral(name), Symbol("moduleKb" + name),
                             parse_sexprs("(CTV (STV 1.0 1.0) (STV 0.0 1.0))")[0])
        setup = [("$authority" + name, form("langdef:load-language-value", render(view[1])))]
        setup += [("$installed" + str(i), "(collapse " + render_metta(rule) + ")")
                  for i, rule in enumerate(rules)]
        # The native authority must remain live during every query.
        def query(sort, source):
            return '(collapse (query 100 moduleKb' + name + ' (: $proof (ModuleRewriteStepV1 ' + \
                text(name) + ' ' + text(sort) + ' $origin ' + source + ' $target) $tv)))'
        observed = [("$root", query("Nat", "(NatAdd (NatZ) (NatS (NatZ)))")),
                    ("$context", query("Nat", "(NatS (NatAdd (NatZ) (NatZ)))")),
                    ("$wrong", query("Nat", "(SeqEmpty)"))]
        observed += [("$admitted", form("cpu-call", "100", "langdef:admit-term-data",
            "($authority" + name + ' "Nat" (NatAdd (NatZ) (NatS (NatZ))))'))]
        setup += observed + [("$closed", form("langdef:close-language", "$authority" + name))]
        calls.append("!" + lets(setup, '(function (return (ModuleChainerControlV1 ' + text(name) +
                     ' $root $context $wrong $installed0 $admitted)))'))
    program = '!(import! &self langdef)\n' + \
        '!(import! &self ' + text(str(image / "petta_chainer.metta")) + ')\n' + \
        "\n".join(calls) + "\n"
    source = image / "module-chainer.metta"
    source.write_text(program)
    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1", PYTHONDONTWRITEBYTECODE="1")
    completed = subprocess.run([str(image_binary), "--lang", "petta", "--profile", "extended",
        "--space-engine", "native", str(source)], cwd=image, env=env,
        capture_output=True, text=True, timeout=300)
    (output / "chainer.stdout").write_text(completed.stdout)
    (output / "chainer.stderr").write_text(completed.stderr)
    if completed.returncode:
        raise AssertionError(("existing chainer execution", completed.returncode, completed.stderr))
    records = [v for v in output_values(completed.stdout) if headed(v, "ModuleChainerControlV1", 6)]
    if len(records) != len(views):
        raise AssertionError(("chainer records", records, completed.stderr))
    for record in records:
        name, root, context, wrong, installed, admitted = record[1:]
        assert root and wrong == () and installed, record
        assert admitted == (Symbol("LangDef:TermAdmitted"),), record
        assert all(v[2][-1] == (Symbol("NatS"), (Symbol("NatZ"),)) for v in root), root
        if name.text == "Together":
            assert len(root) == 1 and len(context) == 1, record
            assert all(v[2][-1] == (Symbol("NatS"), (Symbol("NatZ"),)) for v in context), record
        elif name.text == "NamedZero":
            assert len(root) == 1 and context == (), record
        else:
            assert len(root) == 2 and context == (), record
            assert root[0][2][3] != root[1][2][3], root
            proof = render(root)
            labels = ("AddZeroAgain", "AddZero") if name.text == "DuplicatePaths" else ("AddZeroNamed", "QualifiedSlot")
            assert all(label in proof for label in labels), root
    print(f"ModuleChainerSummary {len(views)} 0")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--reader-artifact", type=Path)
    parser.add_argument("--chainer-root", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    controls(args.cetta.resolve(), args.reader_artifact.resolve() if args.reader_artifact else None, args.output.resolve(),
             args.chainer_root.resolve(), args.petta_root.resolve())


if __name__ == "__main__":
    main()
