#!/usr/bin/env python3
"""Freeze external LBNF fixtures and run the pinned BNFC C reference.

Source grammars, inputs and expected output are read in place. Generated
parsers and command receipts belong to the caller's output directory.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(path, family):
    return {"path": str(path.resolve()), "sha256": digest(path),
            "bytes": path.stat().st_size, "family": family}


def run(command, directory, stem, timeout=120, input_bytes=None):
    try:
        result = subprocess.run(command, cwd=directory, input=input_bytes,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, check=False)
        status, stdout, stderr = result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as exc:
        status, stdout, stderr = "timeout", exc.stdout or b"", exc.stderr or b""
    (directory / (stem + ".stdout")).write_bytes(stdout)
    (directory / (stem + ".stderr")).write_bytes(stderr)
    return {"command": [str(x) for x in command], "status": status,
            "stdout": stdout.decode("utf-8", "replace"),
            "stderr": stderr.decode("utf-8", "replace")}


def abstract_tree(output):
    start = re.search(r"^[ \t]*\[Abstract Syntax\][ \t]*\r?$", output,
                      flags=re.MULTILINE)
    if start is None:
        return None
    tail = output[start.end():]
    end = re.search(r"^[ \t]*\[Linearized tree\][ \t]*\r?$", tail,
                    flags=re.MULTILINE | re.IGNORECASE)
    return tail[:end.start()].strip() if end else tail.strip()


def normalized_diagnostic(text, bnfc_root):
    """Ignore checkout location and spacing, not diagnostic words or sites."""
    prefix = str(bnfc_root.resolve() / "testing") + os.sep
    return " ".join(text.replace(prefix, "").split())


def input_expectation(source, reference):
    if source.name.startswith("bad"):
        return "reject", reference["status"] == 1
    if source.name.startswith("good") or source.with_suffix(".out").exists():
        return "accept", (reference["status"] == 0 and
                          abstract_tree(reference["stdout"]) is not None)
    return None, None


def fixtures(args):
    items = {}

    def add(path, family):
        path = path.resolve()
        if path not in items:
            items[path] = record(path, family)

    for path in sorted(args.bnfc_root.rglob("*.cf")):
        add(path, "bnfc-grammar")
    for directory in ("fail-lbnf", "succeed-lbnf", "regression-tests"):
        for path in sorted((args.bnfc_root / "testing" / directory).rglob("*")):
            if path.is_file() and path.suffix in (".err", ".out", ".in"):
                add(path, "bnfc-" + directory)
    for path in args.grammar:
        add(path, "client-grammar")
    for root in args.module_root:
        for path in sorted(root.rglob("*.module")):
            add(path, "module-input")
    for root in args.rho_root:
        for path in sorted(root.rglob("*.rho")):
            add(path, "rho-input")
    return list(items.values())


def fixture_matches(current, frozen):
    return sorted(current, key=lambda x: x["path"]) == sorted(
        frozen, key=lambda x: x["path"])


def frozen_fixture_additions(current, frozen):
    current_by_path = {row["path"]: row for row in current}
    frozen_by_path = {row["path"]: row for row in frozen}
    for path, row in frozen_by_path.items():
        if current_by_path.get(path) != row:
            raise RuntimeError("frozen fixture changed or disappeared: " + path)
    return [row for row in current if row["path"] not in frozen_by_path]


def revised_fixture_inventory(frozen, revisions):
    """Apply explicit input revisions without changing the original inventory."""
    replacements = {}
    original = {row["path"]: row for row in frozen}
    for revision in revisions:
        if revision.get("format") != "LbnfInputRevisionV1":
            raise RuntimeError("unknown fixture revision format")
        previous, replacement = revision["original"], revision["replacement"]
        path = previous["path"]
        if path in replacements:
            raise RuntimeError("duplicate fixture revision: " + path)
        if previous != original.get(path):
            raise RuntimeError("revision does not match the frozen fixture: " + path)
        if (replacement["path"] != path or
                replacement["family"] != previous["family"] or
                previous["family"] in ("bnfc-grammar", "client-grammar")):
            raise RuntimeError("only the same input fixture may be revised: " + path)
        if previous["sha256"] == replacement["sha256"]:
            raise RuntimeError("fixture revision has not changed: " + path)
        replacements[path] = replacement
    return [replacements.get(row["path"], row) for row in frozen]


def record_input_revision(reference, source, output):
    """Rerun one explicitly selected input and retain its original receipt."""
    inventory_path = reference / "fixtures.json"
    inventory = json.loads(inventory_path.read_text())
    selected = [row for row in inventory if row["path"] == str(source)]
    if len(selected) != 1:
        raise RuntimeError("input is not uniquely present in the frozen inventory")
    previous = selected[0]
    replacement = record(source, previous["family"])
    revision = {"format": "LbnfInputRevisionV1", "original": previous,
                "replacement": replacement,
                "recorded_at": datetime.now(timezone.utc).isoformat(),
                "original_inventory_sha256": digest(inventory_path),
                "reference_receipts": []}
    revised_fixture_inventory(inventory, [revision])
    for row in inventory:
        if row["path"] != str(source) and digest(Path(row["path"])) != row["sha256"]:
            raise RuntimeError("another frozen fixture changed: " + row["path"])
    tools = json.loads((reference / "tools.json").read_text())
    for name, tool in tools.items():
        if digest(Path(tool["path"])) != tool["sha256"]:
            raise RuntimeError("frozen reference tool changed: " + name)
    authorities = []
    for grammar in json.loads((reference / "summary.json").read_text())["grammars"]:
        receipt_path = Path(grammar["receipt"])
        receipt = json.loads(receipt_path.read_text())
        for original_input in receipt.get("inputs", []):
            if original_input["input"] == str(source):
                if original_input["sha256"] != previous["sha256"]:
                    raise RuntimeError("original input receipt disagrees with its inventory")
                if digest(Path(receipt["grammar"])) != receipt["source_sha256"]:
                    raise RuntimeError("reference grammar changed")
                authorities.append((receipt_path, receipt, original_input))
    if not authorities:
        raise RuntimeError("no original parser receipt exists for this input")
    output.mkdir(parents=True, exist_ok=False)
    for index, (receipt_path, receipt, original_input) in enumerate(authorities):
        executable = Path(original_input["reference"]["command"][0])
        parser_hash = digest(executable)
        result = run([str(executable), str(source)], output, "input-" + str(index))
        if digest(source) != replacement["sha256"] or digest(executable) != parser_hash:
            raise RuntimeError("input or reference parser changed during the revision run")
        revision["reference_receipts"].append({
            "grammar": receipt["grammar"], "grammar_sha256": receipt["source_sha256"],
            "original_receipt": str(receipt_path),
            "original_receipt_sha256": digest(receipt_path),
            "original_input": original_input,
            "parser_sha256": parser_hash,
            "replacement_input": {"input": str(source), "sha256": replacement["sha256"],
                                  "reference": result,
                                  "abstract_tree": abstract_tree(result["stdout"])}})
    (output / "revision.json").write_text(json.dumps(revision, indent=2) + "\n")
    return revision


def expected_inputs(grammar):
    """BNFC regression fixtures use either a directory or a file stem."""
    if "regression-tests" not in grammar.parts:
        return []
    inputs = sorted(grammar.parent.glob("good*.in"))
    inputs += sorted(grammar.parent.glob("bad*.in"))
    if grammar.with_suffix(".in").is_file():
        inputs.append(grammar.with_suffix(".in"))
    return sorted(set(inputs))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bnfc-root", type=Path, required=True)
    parser.add_argument("--grammar", type=Path, action="append", default=[])
    parser.add_argument("--module-root", type=Path, action="append", default=[])
    parser.add_argument("--rho-root", type=Path, action="append", default=[])
    parser.add_argument("--module-grammar", type=Path)
    parser.add_argument("--rho-grammar", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--run-inputs", action="store_true")
    parser.add_argument("--revise-input", type=Path)
    parser.add_argument("--revision-output", type=Path)
    args = parser.parse_args()
    args.bnfc_root = args.bnfc_root.resolve()
    args.output = args.output.resolve()
    if args.output == args.bnfc_root or args.bnfc_root in args.output.parents:
        parser.error("output must be outside the external source checkout")
    if args.run_inputs and not args.generate:
        parser.error("--run-inputs requires --generate")
    if args.revise_input:
        if not args.revision_output or args.generate or args.run_inputs:
            parser.error("--revise-input requires --revision-output and no generation flags")
        revision_output = args.revision_output.resolve()
        if revision_output == args.bnfc_root or args.bnfc_root in revision_output.parents:
            parser.error("revision output must be outside the external source checkout")
        revision = record_input_revision(args.output, args.revise_input.resolve(), revision_output)
        print(json.dumps({"original": revision["original"],
                          "replacement": revision["replacement"],
                          "revision": str(revision_output / "revision.json")}))
        return 0
    if args.revision_output:
        parser.error("--revision-output requires --revise-input")
    args.output.mkdir(parents=True, exist_ok=True)
    inventory = fixtures(args)
    inventory_path = args.output / "fixtures.json"
    additions = []
    if inventory_path.exists():
        frozen = json.loads(inventory_path.read_text())
        additions = frozen_fixture_additions(inventory, frozen)
        inventory = frozen
    else:
        inventory_path.write_text(json.dumps(inventory, indent=2) + "\n")
    tools = {}
    for name in ("bnfc", "flex", "bison", "gcc", "make"):
        executable = shutil.which(name)
        if executable is None:
            raise RuntimeError("reference tool missing: " + name)
        receipt = run([executable, "--version"], args.output, "tool-" + name)
        if receipt["status"] != 0:
            raise RuntimeError("reference tool version failed: " + name)
        tools[name] = {"path": executable, "sha256": digest(Path(executable)),
                       "version": receipt["stdout"].strip()}
    if tools["bnfc"]["version"] != "2.9.6.3":
        raise RuntimeError("BNFC 2.9.6.3 is required")
    tools_path = args.output / "tools.json"
    if tools_path.exists() and json.loads(tools_path.read_text()) != tools:
        raise RuntimeError("reference tools changed after they were frozen")
    tools_path.write_text(json.dumps(tools, indent=2) + "\n")
    summary = {"fixture_counts": {}, "tools": tools, "grammars": [],
               "discovered_after_freeze": additions,
               "grammar_checks": {"valid": 0, "invalid": 0, "incomplete": 0},
               "valid_c_backend_limits": [], "error_cause_mismatches": [],
               "input_expectation_mismatches": []}
    for row in inventory:
        family = row["family"]
        summary["fixture_counts"][family] = summary["fixture_counts"].get(family, 0) + 1
    if args.generate:
        for row in inventory:
            if row["family"] not in ("bnfc-grammar", "client-grammar"):
                continue
            grammar = Path(row["path"])
            identity = hashlib.sha256(str(grammar).encode()).hexdigest()[:16]
            directory = args.output / (grammar.stem + "-" + identity)
            directory.mkdir(exist_ok=True)
            receipt_path = directory / "receipt.json"
            if receipt_path.exists():
                receipt = json.loads(receipt_path.read_text())
                if receipt["source_sha256"] != row["sha256"]:
                    raise RuntimeError("generated parser source changed")
            else:
                generated = run([tools["bnfc"]["path"], "--c", "-m", "-o",
                                 str(directory), str(grammar)], directory, "generate")
                receipt = {"grammar": str(grammar), "source_sha256": row["sha256"],
                           "generate": generated, "inputs": []}
                if grammar.with_suffix(".err").exists():
                    receipt["expected_error"] = grammar.with_suffix(".err").read_text()
                receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
            if "check" not in receipt:
                receipt["check"] = run([tools["bnfc"]["path"], "--check",
                                         str(grammar)], directory, "check")
            check_status = receipt["check"]["status"]
            summary["grammar_checks"]["valid" if check_status == 0 else
                                       "invalid" if check_status == 1 else
                                       "incomplete"] += 1
            if check_status == 0 and receipt["generate"]["status"] != 0:
                summary["valid_c_backend_limits"].append(str(grammar))
            if "expected_error" in receipt:
                actual = receipt["check"]["stdout"] + "\n" + receipt["check"]["stderr"]
                receipt["error_cause_matches"] = (check_status == 1 and
                    normalized_diagnostic(receipt["expected_error"], args.bnfc_root)
                    in normalized_diagnostic(actual, args.bnfc_root))
                if not receipt["error_cause_matches"]:
                    summary["error_cause_mismatches"].append(str(grammar))
            for result in receipt["inputs"]:
                result["abstract_tree"] = abstract_tree(result["reference"]["stdout"])
                golden = Path(result["input"]).with_suffix(".out")
                if golden.exists():
                    result["golden_tree"] = abstract_tree(golden.read_text())
            if args.run_inputs and receipt["generate"]["status"] == 0:
                selected = expected_inputs(grammar)
                if args.module_grammar and grammar == args.module_grammar.resolve():
                    selected += [Path(x["path"]) for x in inventory
                                 if x["family"] == "module-input"]
                if args.rho_grammar and grammar == args.rho_grammar.resolve():
                    selected += [Path(x["path"]) for x in inventory
                                 if x["family"] == "rho-input"]
                if selected and "build" not in receipt:
                    receipt["build"] = run([tools["make"]["path"], "-j2",
                                            "CC=" + tools["gcc"]["path"]], directory, "build")
                if selected and receipt.get("build", {}).get("status") == 0:
                    executables = [x for x in directory.glob("Test*")
                                   if x.is_file() and os.access(x, os.X_OK)]
                    if len(executables) != 1:
                        raise RuntimeError("reference driver is not unique")
                    already = {x["input"] for x in receipt["inputs"]}
                    for source in sorted(set(selected)):
                        if str(source) in already:
                            continue
                        input_id = hashlib.sha256(str(source).encode()).hexdigest()[:16]
                        checked = run([str(executables[0]), str(source)], directory,
                                      "input-" + input_id)
                        result = {"input": str(source), "sha256": digest(source),
                                  "reference": checked,
                                  "abstract_tree": abstract_tree(checked["stdout"])}
                        golden = source.with_suffix(".out")
                        if golden.exists():
                            result["golden_tree"] = abstract_tree(golden.read_text())
                        receipt["inputs"].append(result)
            for result in receipt["inputs"]:
                expected, matches = input_expectation(Path(result["input"]),
                                                      result["reference"])
                result["expected_verdict"] = expected
                result["expected_verdict_matches"] = matches
                if matches is False:
                    summary["input_expectation_mismatches"].append(result["input"])
            receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
            summary["grammars"].append({"grammar": str(grammar),
                                        "status": receipt["generate"]["status"],
                                        "receipt": str(receipt_path)})
    summary["discovered_after_freeze"] = frozen_fixture_additions(
        fixtures(args), inventory)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({"fixtures": summary["fixture_counts"],
                      "generated": len(summary["grammars"]),
                      "discovered_after_freeze": len(summary["discovered_after_freeze"])}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
