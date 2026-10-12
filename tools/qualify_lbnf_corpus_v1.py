#!/usr/bin/env python3
"""Compare public LBNF parsers and full evidence with frozen BNFC controls.

The exact C AST projection retains categories, constructor labels, field order,
token bytes, and binary64 bits. Only source positions and production identity
are erased from that projection; the complete original forest is saved too.
The common domain is UTF-8 input without NUL, C-int-range Integer fields, finite
binary64 Double fields, and one-byte Char fields. Out-of-domain observations are
reported explicitly and do not silently pass the qualification gate.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from gslt2parse_schema_v1 import render
from lbnf_glr_completion_v1 import build_glr_completion, classify_glr_completion
from lbnf_reference_ast_v1 import (GoldenReader, InputDomainError, build_visitor,
                                  golden_comparable, normalize_ast)
from test_lbnf_values_v1 import (CFLEX, ROOT, form, headed, lets, library_import,
                                llist_forest, lower_read, output_values, query, text)

sys.path.insert(0, str(ROOT / "scripts"))
from qualify_lbnf_reference_v1 import revised_fixture_inventory


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stable_ast(tree):
    return json.dumps(tree, ensure_ascii=True, separators=(",", ":"))


def evidence_sha256(value):
    """Hash the canonical S-expression without recursing through forest lists."""
    digest = hashlib.sha256()
    stack, pieces = [value], []
    while stack:
        item = stack.pop()
        if isinstance(item, str):
            pieces.append(item)
        elif isinstance(item, tuple):
            stack.append(")")
            for index in range(len(item) - 1, -1, -1):
                stack.append(item[index])
                if index:
                    stack.append(" ")
            stack.append("(")
        else:
            pieces.append(render(item))
        if len(pieces) >= 1024:
            digest.update("".join(pieces).encode())
            pieces.clear()
    digest.update("".join(pieces).encode())
    return digest.hexdigest()


def corpus_program(grammar, sources):
    observations = [(f"$observed{index}", form("langdef:parse-authored-file-evidence",
                     "$parser", text(str(source)), "1000000"))
                    for index, source in enumerate(sources)]
    body = lets([("$parser", form("langdef:load-syntax-gslt-parser", "$syntax")),
                 *observations,
                 ("$_closed", form("langdef:close-authored-parser", "$parser"))],
                form("LbnfCorpusBatchV1", *(name for name, _ in observations)))
    return library_import() + query(lets([
        ("$syntax", lower_read(grammar, "Corpus", policy=CFLEX, file=True))],
        form("case", "$syntax", form(
            form(form("SyntaxGSLTAdmittedV1", "$entry"), body),
            form("$refused", form("LbnfCorpusGrammarRefusedV1", "$refused"))))))


def matching_erratum(grammar, source, errata):
    paths = {"grammar": grammar, "input": source, "golden": source.with_suffix(".out")}
    for entry in errata:
        if all(path.parts[-len(Path(entry[kind]).parts):] == Path(entry[kind]).parts
               for kind, path in paths.items()):
            if all(sha256(path) == entry["sha256"][kind] for kind, path in paths.items()):
                return entry
    return None


def saved_golden(reference, typed, layout, grammar, errata):
    if reference.get("golden_tree") is None:
        return {"golden_status": "absent"}
    result = {"golden_status": "unresolved"}
    if typed is None or layout is None:
        return result
    try:
        golden = GoldenReader(reference["golden_tree"], layout).document()
        if golden_comparable(golden) == golden_comparable(typed["ast"]):
            return {"golden_status": "match"}
        result["golden_difference"] = "saved AST differs from the current typed C AST"
    except ValueError as exc:
        result["golden_difference"] = str(exc)
    entry = matching_erratum(grammar, Path(reference["input"]), errata)
    if entry is not None:
        result.update(golden_status="verified-erratum", golden_erratum=entry)
    return result


def completion_accepts(completion):
    return completion is not None and (completion["status"] == 0 or completion["ambiguous"])


def observe(value, source_hash, reference, typed_reference, completion=None):
    """Compare one public observation with the frozen C reference.

    Where the deterministic reference rejects, `completion` is the GLR
    completion's run on the same input: a rejection then matches only when
    the completion rejects too, and an acceptance matches only when the
    completion accepts with exactly the tree CeTTa's forest contains."""
    row = {"source_sha256": source_hash, "matches": False,
           "reference_status": reference["reference"]["status"]}
    if completion is not None:
        row["reference_completion"] = {key: completion[key] for key in
                                       ("algorithm", "status", "ambiguous")}
        if completion["typed"] is not None and row["reference_status"] != 0:
            typed_reference = completion["typed"]
    if headed(value, "LbnfCorpusGrammarRefusedV1", 1):
        refusal = value[1]
        row["verdict"] = "grammar-refused"
        if headed(refusal, "LbnfRefusedV1", 3):
            row["cause"] = refusal[1].text
            row["positioned"] = headed(refusal[2], "LbnfSourceSpanV1", 4)
            row["allowed_refusal"] = row["cause"] in ("layout", "define") and row["positioned"]
        return row
    forest, trees = None, None
    if headed(value, "LangDef:AuthoredParseEvidenceAccepted", 6):
        row["verdict"] = "accept"
        row["observed_source_sha256"] = value[3].text
        row["forest_sha256"] = value[4].text
        forest, trees = value[5], value[6]
    elif headed(value, "LangDef:AuthoredParseEvidenceRejected", 7):
        row["verdict"] = "reject"
        row["observed_source_sha256"] = value[3].text
        row["forest_sha256"] = value[6].text
        row["farthest_byte"] = value[4]
        forest = value[7]
    elif headed(value, "LangDef:AuthoredLexicalRefusedV1", 5):
        row["verdict"] = "reject"
        row["observed_source_sha256"] = value[3].text
        row["lexical_witness"] = render(value[5])
        row["matches"] = (row["reference_status"] == 1 and
                          row["observed_source_sha256"] == source_hash and
                          not completion_accepts(completion))
        return row
    else:
        row["verdict"] = "incomplete"
        row["outcome"] = render(value)[:2000]
        return row
    if row["observed_source_sha256"] != source_hash or not headed(forest, "pf-v1", 5):
        row["verdict"] = "invalid-evidence"
        return row
    row["packed_node_count"] = len(llist_forest(forest[4]))
    row["packed_choice_count"] = len(llist_forest(forest[5]))
    if row["verdict"] == "reject":
        row["matches"] = (row["reference_status"] == 1 and
                          not completion_accepts(completion))
        return row
    if not isinstance(trees, tuple) or not trees or typed_reference is None:
        row["outcome"] = "missing projected trees or typed C oracle"
        return row
    if typed_reference.get("format") != "BnfcTypedAstV1" or typed_reference.get("double_bytes") != 8:
        row["outcome"] = "unexpected typed C oracle domain"
        return row
    try:
        normalized = [normalize_ast(tree, typed_reference["int_bits"]) for tree in trees]
    except InputDomainError as exc:
        row["verdict"] = "outside-common-domain"
        row["domain_witness"] = str(exc)
        return row
    row["projected_tree_count"] = len(normalized)
    row["projected_tree_multiplicities"] = dict(Counter(map(stable_ast, normalized)))
    row["reference_tree_present"] = typed_reference["ast"] in normalized
    row["matches"] = ((row["reference_status"] == 0 or
                       (completion is not None and completion["typed"] is not None)) and
                      row["reference_tree_present"])
    if not row["matches"]:
        row["typed_reference"] = typed_reference["ast"]
        row["typed_candidate"] = normalized
    return row


def run_batch(binary, grammar, sources, language, output, timeout):
    output.mkdir(parents=True, exist_ok=True)
    client = output / "client.metta"
    client.write_text(corpus_program(grammar, sources))
    command = [str(binary), "--lang", language]
    if language != "prime":
        command += ["--profile", "extended"]
    command += [str(client)]
    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=timeout)
        status, stdout, stderr = result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as exc:
        status, stdout, stderr = "timeout", exc.stdout or b"", exc.stderr or b""
    (output / "stdout").write_bytes(stdout)
    (output / "stderr").write_bytes(stderr)
    receipt = {"command": command, "status": status, "seconds": time.monotonic() - started,
               "stdout_bytes": len(stdout), "stdout_sha256": hashlib.sha256(stdout).hexdigest(),
               "stderr_sha256": hashlib.sha256(stderr).hexdigest()}
    (output / "run.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if status or stderr:
        raise ValueError("public corpus process did not finish cleanly: " + str(receipt))
    values = output_values(stdout.decode("utf-8", "strict"))
    if len(values) != 1:
        raise ValueError("public corpus batch returned " + str(len(values)) + " observations")
    value = values[0]
    if headed(value, "LbnfCorpusGrammarRefusedV1", 1):
        return [value] * len(sources)
    if not headed(value, "LbnfCorpusBatchV1", len(sources)):
        raise ValueError("public corpus batch was not completed data: " + render(value)[:1000])
    return list(value[1:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--language", choices=("prime", "he", "petta"), default="prime")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--batch-size", type=int, default=12)
    parser.add_argument("--input", type=Path, action="append", default=[])
    parser.add_argument("--fixture-revision", type=Path, action="append", default=[])
    parser.add_argument("--saved-tree-errata", type=Path,
                        default=ROOT / "tools/lbnf_saved_tree_errata_v1.json")
    parser.add_argument("--glr-completion-bison", type=Path,
                        help="pinned Bison used to complete each frozen C reference with GLR")
    args = parser.parse_args()
    if not 1 <= args.batch_size <= 64:
        parser.error("batch size must be between 1 and 64")
    args.cetta, args.reference, args.output = (path.resolve() for path in
                                             (args.cetta, args.reference, args.output))
    args.output.mkdir(parents=True, exist_ok=True)
    sys.setrecursionlimit(20000)
    revisions = [json.loads(path.read_text()) for path in args.fixture_revision]
    errata_document = json.loads(args.saved_tree_errata.read_text())
    if errata_document.get("format") != "BnfcSavedTreeErrataV1":
        raise ValueError("unknown saved-tree errata format")
    errata = errata_document["entries"]
    inventory = revised_fixture_inventory(json.loads(
        (args.reference / "fixtures.json").read_text()), revisions)
    drift = [row["path"] for row in inventory if not Path(row["path"]).is_file() or
             sha256(Path(row["path"])) != row["sha256"]]
    if drift:
        raise ValueError("frozen inputs changed: " + repr(drift))
    replacements = {(row["grammar"], row["replacement_input"]["input"]): row["replacement_input"]
                    for revision in revisions for row in revision["reference_receipts"]}
    frozen = {str(args.cetta): sha256(args.cetta), **{str(ROOT / path): sha256(ROOT / path)
              for path in ("lib/lib_lbnf.metta", "lib/petta/lib_lbnf.metta",
                           "lib/langdef.metta", "lib/petta/langdef.metta")}}
    frozen_tools = {str(path): sha256(path) for path in (Path(__file__),
                    ROOT / "tools/lbnf_reference_ast_v1.py",
                    ROOT / "tools/lbnf_glr_completion_v1.py", args.saved_tree_errata)}
    summary = json.loads((args.reference / "summary.json").read_text())
    selected = {str(path.resolve()) for path in args.input}
    seen, receipts = set(), []
    for grammar_row in summary["grammars"]:
        reference_path = Path(grammar_row["receipt"])
        authority = json.loads(reference_path.read_text())
        grammar = Path(authority["grammar"])
        inputs = [replacements.get((str(grammar), row["input"]), row)
                  for row in authority.get("inputs", []) if not selected or row["input"] in selected]
        if not inputs:
            continue
        seen.update(row["input"] for row in inputs)
        identity = hashlib.sha256(str(grammar).encode()).hexdigest()[:16]
        directory = args.output / (grammar.stem + "-" + identity)
        oracle, layout = None, None
        if any(row["reference"]["status"] == 0 for row in inputs):
            oracle, layout = build_visitor(reference_path.parent, directory / "typed-reference")
        completion_oracle = None
        if args.glr_completion_bison and any(row["reference"]["status"] != 0 for row in inputs):
            completed = build_glr_completion(reference_path.parent, directory / "glr-completion",
                                             args.glr_completion_bison)
            completion_oracle, _ = build_visitor(completed, directory / "glr-typed-reference")
        for offset in range(0, len(inputs), args.batch_size):
            batch = inputs[offset:offset + args.batch_size]
            batch_directory = directory / (args.language + "-batch-" + str(offset))
            try:
                observations = run_batch(args.cetta, grammar, [Path(row["input"]) for row in batch],
                                         args.language, batch_directory, args.timeout)
            except (ValueError, UnicodeError, RecursionError) as exc:
                observations = [None] * len(batch)
                process_error = str(exc)
            for index, (reference, value) in enumerate(zip(batch, observations)):
                source = Path(reference["input"])
                if sha256(source) != reference["sha256"]:
                    raise ValueError("input differs from its frozen reference: " + str(source))
                typed = None
                if value is not None and reference["reference"]["status"] == 0:
                    result = subprocess.run([str(oracle), str(source)], capture_output=True,
                                            timeout=args.timeout)
                    (batch_directory / ("reference-" + str(index) + ".stdout")).write_bytes(result.stdout)
                    (batch_directory / ("reference-" + str(index) + ".stderr")).write_bytes(result.stderr)
                    if result.returncode or result.stderr:
                        raise ValueError("typed C oracle did not agree with its successful frozen run")
                    typed = json.loads(result.stdout)
                completion = None
                if value is not None and reference["reference"]["status"] != 0 and completion_oracle:
                    result = subprocess.run([str(completion_oracle), str(source)],
                                            capture_output=True, timeout=args.timeout)
                    (batch_directory / ("completion-" + str(index) + ".stdout")).write_bytes(result.stdout)
                    (batch_directory / ("completion-" + str(index) + ".stderr")).write_bytes(result.stderr)
                    completion = classify_glr_completion(result)
                try:
                    row = observe(value, reference["sha256"], reference, typed,
                                  completion) if value is not None else {
                        "verdict": "incomplete", "matches": False, "process_error": process_error}
                except (ValueError, IndexError, AttributeError, RecursionError) as exc:
                    row = {"verdict": "invalid-evidence", "matches": False, "observation_error": str(exc)}
                row.update(grammar=str(grammar), input=str(source), language=args.language,
                           batch=str(batch_directory), observation=index)
                if value is not None:
                    row["complete_evidence_sha256"] = evidence_sha256(value)
                row["has_stored_ast_golden"] = reference.get("golden_tree") is not None
                if row.get("allowed_refusal") and row["has_stored_ast_golden"]:
                    row["golden_status"] = "excluded-scoped-" + row["cause"]
                else:
                    row.update(saved_golden(reference, typed, layout, grammar, errata))
                receipts.append(row)
                print(json.dumps({key: row.get(key) for key in
                    ("input", "verdict", "cause", "matches", "projected_tree_count",
                     "packed_choice_count", "golden_status")}), flush=True)
            (args.output / (args.language + "-receipts.json")).write_text(json.dumps(receipts, indent=2) + "\n")
    if selected - seen:
        parser.error("selected input is not in the frozen parser receipts")
    if any(sha256(Path(path)) != digest for path, digest in frozen.items()):
        raise ValueError("candidate binary or libraries changed during qualification")
    if any(sha256(Path(path)) != digest for path, digest in frozen_tools.items()):
        raise ValueError("qualification tools or errata changed during the run")
    result = {"format": "LbnfCorpusQualificationV1", "phase": "pinned-c-typed-ast",
              "language": args.language,
              "partial_selection": bool(selected), "frozen_candidate_and_libraries": frozen,
              "frozen_tools": frozen_tools,
              "input_count": len(receipts), "verdicts": dict(Counter(row["verdict"] for row in receipts)),
              "allowed_grammar_refusals": sum(bool(row.get("allowed_refusal")) for row in receipts),
              "saved_ast_golden_counts": dict(Counter(row["golden_status"] for row in receipts)),
              "glr_completion_bison": str(args.glr_completion_bison or ""),
              "matched_by_glr_completion": [row["input"] for row in receipts
                                            if row["matches"] and row.get("reference_completion") and
                                            row["verdict"] == "accept"],
              "fixture_drift": drift,
              "mismatches": [row["input"] for row in receipts
                             if (not row["matches"] and not row.get("allowed_refusal")) or
                             row["golden_status"] == "unresolved"]}
    (args.output / (args.language + "-summary.json")).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    return bool(result["mismatches"])


if __name__ == "__main__":
    raise SystemExit(main())
