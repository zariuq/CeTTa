#!/usr/bin/env python3
"""Account for frozen external grammars through the public LBNF interfaces."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from gslt2parse_schema_v1 import Symbol
from test_lbnf_values_v1 import (DOCUMENTED, ROOT, form, headed, library_import,
                                llist, lower_read, output_values, query, text)

sys.path.insert(0, str(ROOT / "scripts"))
from qualify_lbnf_reference_v1 import revised_fixture_inventory


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected_cause(diagnostic):
    causes = [
        ("mentions no terminals", "no-terminals"),
        ("grammar contains no rules", "empty-grammar"),
        ("empty abstract syntax", "empty-abstract-syntax"),
        ("Bad list construction", "bad-list-construction"),
        ("duplicate token definitions", "duplicate-token-definition"),
        ("conflict with non-terminals", "token-category-conflict"),
        ("illegal token names", "numeric-token-suffix"),
        ("Empty line comment delimiter", "empty-comment-delimiter"),
        ("layout", "layout"),
        ("tokens accept nothing", "empty-token"),
        ("conflicting types", "constructor-signature-conflict"),
        ("tokens accept the empty string", "nullable-token"),
        ("undefined rule label", "undefined-rule-label"),
        ("syntax error", "source-syntax"),
        ("In the definition", "define"),
    ]
    for fragment, cause in causes:
        if fragment in diagnostic:
            return cause
    return None


def grammar_receipt(binary, grammar, reference, output, language, timeout):
    identity = hashlib.sha256(str(grammar).encode()).hexdigest()[:16]
    directory = output / (grammar.stem + "-" + identity)
    directory.mkdir(parents=True, exist_ok=True)
    program = library_import()
    program += query(form("lbnf:read-file-evidence", text(str(grammar)), "1000000"))
    program += query(lower_read(grammar, name="Fixture", file=True))
    source = directory / "client.metta"
    source.write_text(program)
    command = [str(binary), "--lang", language]
    if language != "prime":
        command += ["--profile", "extended"]
    command += [str(source)]
    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=timeout)
        status, stdout, stderr = result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as exc:
        status, stdout, stderr = "timeout", exc.stdout or b"", exc.stderr or b""
    (directory / (language + ".stdout")).write_bytes(stdout)
    (directory / (language + ".stderr")).write_bytes(stderr)
    receipt = {"grammar": str(grammar), "language": language, "command": command,
               "status": status, "seconds": time.monotonic() - started,
               "reference_check_status": reference["check"]["status"],
               "reference_generate_status": reference["generate"]["status"],
               "read": "incomplete", "lower": "incomplete", "matches": False}
    if status != 0 or stderr:
        return receipt
    try:
        values = output_values(stdout.decode("utf-8", "strict"))
    except (ValueError, UnicodeError, RecursionError) as exc:
        receipt["observation_error"] = str(exc)
        return receipt
    if len(values) != 2:
        receipt["observation_count"] = len(values)
        return receipt
    read, lowered = values
    if headed(read, "LbnfReadEvidenceV1", 5):
        receipt["read"] = "accepted"
        receipt["source_sha256"] = read[2].text
        receipt["reader_forest_sha256"] = read[3].text
        if not headed(read[4], "pf-v1", 5) or not headed(read[5], "LbnfDocumentV1", 2):
            receipt["read"] = "incomplete"
            return receipt
        receipt["definition_count"] = len(llist(read[5][2]))
    elif (headed(read, "LangDef:AuthoredParseEvidenceRejected", 7)
          or headed(read, "LangDef:AuthoredLexicalRefusedV1", 5)):
        receipt["read"] = "rejected"
    else:
        receipt["reader_outcome"] = repr(read)[:1000]
        return receipt
    if headed(lowered, "SyntaxGSLTAdmittedV1", 1):
        receipt["lower"] = "admitted"
        syntax = lowered[1]
        if not headed(syntax, "SyntaxGSLTV1", 4):
            return receipt
        receipt["production_count"] = len(llist(syntax[1][3]))
        receipt["token_count"] = len(llist(syntax[2][5][2]))
    elif headed(lowered, "LbnfRefusedV1", 3):
        receipt["lower"] = "refused"
        receipt["cause"] = lowered[1].text
        receipt["positioned"] = headed(lowered[2], "LbnfSourceSpanV1", 4)
        receipt["diagnostic"] = lowered[3].text
    elif (headed(lowered, "LbnfClientReadFailureV1", 1)
          and receipt["read"] == "rejected"):
        receipt["lower"] = "source-rejected"
        receipt["cause"] = "source-syntax"
    else:
        receipt["lower_outcome"] = repr(lowered)[:1000]
        return receipt
    if reference["check"]["status"] == 0:
        receipt["matches"] = (receipt["read"] == "accepted" and
            (receipt["lower"] == "admitted" or
             (receipt.get("cause") in ("layout", "define") and receipt.get("positioned"))))
    else:
        receipt["expected_cause"] = expected_cause(reference.get("expected_error", ""))
        receipt["matches"] = (receipt.get("cause") == receipt["expected_cause"] and
            (receipt["lower"] == "source-rejected" or receipt.get("positioned", False)))
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--language", choices=("prime", "he", "petta"), default="prime")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--grammar", type=Path, action="append", default=[])
    parser.add_argument("--fixture-revision", type=Path, action="append", default=[])
    args = parser.parse_args()
    args.cetta = args.cetta.resolve()
    args.reference = args.reference.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    sys.setrecursionlimit(20000)
    inventory = json.loads((args.reference / "fixtures.json").read_text())
    revision_receipts = [json.loads(path.read_text()) for path in args.fixture_revision]
    inventory = revised_fixture_inventory(inventory, revision_receipts)
    summary = json.loads((args.reference / "summary.json").read_text())
    authorities = {item["grammar"]: json.loads(Path(item["receipt"]).read_text())
                   for item in summary["grammars"]}
    selected = {str(path.resolve()) for path in args.grammar}
    grammars = [item for item in inventory if item["family"] in
                ("bnfc-grammar", "client-grammar") and
                (not selected or item["path"] in selected)]
    if selected - {item["path"] for item in grammars}:
        parser.error("selected grammar is not in the frozen inventory")
    fixture_drift = []
    for item in inventory:
        path = Path(item["path"])
        current = sha256(path) if path.is_file() else None
        if current != item["sha256"]:
            fixture_drift.append({"path": item["path"], "family": item["family"],
                                  "frozen_sha256": item["sha256"], "current_sha256": current})
    if any(row["path"] in {item["path"] for item in grammars} for row in fixture_drift):
        raise RuntimeError("a selected grammar changed after fixture freeze")
    (args.output / "fixture-drift.json").write_text(json.dumps(fixture_drift, indent=2) + "\n")
    frozen_binary = sha256(args.cetta)
    frozen_library = {str(path.relative_to(ROOT)): sha256(path) for path in
                      (ROOT / "lib/lib_lbnf.metta", ROOT / "lib/petta/lib_lbnf.metta")}
    receipts = []
    for item in grammars:
        grammar = Path(item["path"])
        receipt = grammar_receipt(args.cetta, grammar, authorities[item["path"]],
                                  args.output, args.language, args.timeout)
        receipts.append(receipt)
        (args.output / (args.language + "-receipts.json")).write_text(
            json.dumps(receipts, indent=2) + "\n")
        print(json.dumps({key: receipt.get(key) for key in
              ("grammar", "read", "lower", "cause", "expected_cause", "matches", "seconds")}),
              flush=True)
    if sha256(args.cetta) != frozen_binary or any(
            sha256(ROOT / path) != value for path, value in frozen_library.items()):
        raise RuntimeError("candidate binary or library changed during qualification")
    result = {"language": args.language, "candidate_sha256": frozen_binary,
              "library_sha256": frozen_library, "grammar_count": len(grammars),
              "read_counts": dict(Counter(row["read"] for row in receipts)),
              "lower_counts": dict(Counter(row["lower"] for row in receipts)),
              "fixture_drift": fixture_drift,
              "fixture_revisions": [str(path.resolve()) for path in args.fixture_revision],
              "mismatches": [row["grammar"] for row in receipts if not row["matches"]]}
    (args.output / (args.language + "-summary.json")).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    return bool(result["mismatches"] or fixture_drift)


if __name__ == "__main__":
    raise SystemExit(main())
