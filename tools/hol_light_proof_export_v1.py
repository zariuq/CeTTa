"""Export shared, structured proof closures from a pinned HOL Light source.

This is an untrusted source adapter. The pin and structural checks identify
the artifact and its dependency closure; they do not validate its proofs.
The original source tree is read only. The existing ProofTrace patch is
applied to one generated kernel file and selected by the ordinary loader.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


SOURCE_REVISION = "924faf0c3471a04174ffdeb8576ce7cabcb92d64"
THEOREM_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_']*(?:\.[A-Za-z_][A-Za-z0-9_']*)*\Z")
PARENT_POSITIONS = {
    "REFL": (), "TRANS": (0, 1), "MK_COMB": (0, 1), "ABS": (0,),
    "BETA": (), "ASSUME": (), "EQ_MP": (0, 1),
    "DEDUCT_ANTISYM_RULE": (0, 1), "INST": (0,), "INST_TYPE": (0,),
    "AXIOM": (), "DEFINITION": (), "TYPE_DEFINITION": (0,),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_snapshot(source: Path, helper: Path) -> dict:
    revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if revision != SOURCE_REVISION:
        raise ValueError(f"HOL Light source pin mismatch: {revision}")
    changed = subprocess.check_output(
        ["git", "diff", "HEAD", "--name-only", "--", "*.ml", "*.mli"],
        cwd=source, text=True).splitlines()
    if changed:
        raise ValueError(f"modified HOL Light sources: {changed}")
    files = [
        "fusion.ml", "bool.ml", "class.ml", "nums.ml", "lists.ml",
        "hol.ml", "hol_lib.ml", "hol_loader.ml", "ProofTrace/fusion.ml.diff",
        "ProofTrace/proofs.ml",
    ]
    source_files = subprocess.check_output(
        ["git", "ls-files", "--", "*.ml", "*.mli"], cwd=source, text=True).splitlines()
    source_manifest = {name: sha256(source / name) for name in source_files}
    return {"revision": revision, "files": {name: sha256(source / name) for name in files},
            "tracked_sources": source_manifest,
            "closure_helper_sha256": sha256(helper),
            "export_adapter_sha256": sha256(Path(__file__).resolve())}


def inspect_closure(prefix: Path, requested: list[str]) -> dict:
    """Check complete graph accounting, independently of printed success.

    This checks structure only. Rule side conditions and theorem conclusions
    belong to the receiving calculus, not this exporter.
    """
    if not requested or len(requested) != len(set(requested)):
        raise ValueError("provide distinct, nonempty requested roots")
    nodes = {}
    with prefix.with_suffix(".nodes.jsonl").open() as stream:
        for line in stream:
            node = json.loads(line)
            index = node["id"]
            if type(index) is not int or index < 0 or index in nodes:
                raise ValueError("duplicate or invalid proof-node identity")
            positions = PARENT_POSITIONS.get(node["rule"])
            if positions is None:
                raise ValueError(f"unknown recorded source rule: {node['rule']}")
            parents = [node["arguments"][position] for position in positions]
            if any(type(parent) is not int or parent not in nodes for parent in parents):
                raise ValueError(f"missing or unordered parents for {index}")
            nodes[index] = {"rule": node["rule"], "parents": parents}
    roots = [json.loads(line) for line in prefix.with_suffix(".roots.jsonl").read_text().splitlines()]
    if [root["name"] for root in roots] != requested:
        raise ValueError("the exported roots do not exactly cover the requested theorems")
    if any(type(root["id"]) is not int or root["id"] < 0 for root in roots):
        raise ValueError("invalid theorem-root identity")
    signature = json.loads(prefix.with_suffix(".signature.json").read_text())
    for category in ("types", "constants"):
        names = [entry[0] for entry in signature[category]]
        if len(names) != len(set(names)) or any(not isinstance(name, str) for name in names):
            raise ValueError(f"duplicate or invalid signature {category}")
    for roots_key, allowed in (("definition_roots", {"DEFINITION", "TYPE_DEFINITION"}),
                               ("axiom_roots", {"AXIOM"})):
        identities = signature[roots_key]
        if any(type(index) is not int or index < 0 for index in identities):
            raise ValueError(f"invalid {roots_key}")
        if len(identities) != len(set(identities)):
            raise ValueError(f"duplicate {roots_key}")
        recorded = {index for index, node in nodes.items() if node["rule"] in allowed}
        if set(identities) != recorded:
            raise ValueError(f"incomplete or mismatched {roots_key}")
    reachable = set()
    pending = [root["id"] for root in roots] + signature["definition_roots"] + signature["axiom_roots"]
    while pending:
        index = pending.pop()
        if index not in nodes:
            raise ValueError(f"missing root or premise {index}")
        if index in reachable:
            continue
        reachable.add(index)
        pending.extend(nodes[index]["parents"])
    if reachable != nodes.keys():
        raise ValueError("export includes unreachable proof nodes")
    return {"roots": roots, "nodes": len(nodes),
            "premise_occurrences": sum(len(node["parents"]) for node in nodes.values()),
            "signature_types": len(signature["types"]),
            "signature_constants": len(signature["constants"]),
            "rules": dict(sorted(Counter(node["rule"] for node in nodes.values()).items())),
            "axiom_nodes": [index for index, node in nodes.items() if node["rule"] == "AXIOM"],
            "definition_nodes": [index for index, node in nodes.items()
                                 if node["rule"] in {"DEFINITION", "TYPE_DEFINITION"}]}


def export(source: Path, helper: Path, output: Path, theorems: list[str],
           library_through: str | None = None) -> dict:
    if not theorems or len(set(theorems)) != len(theorems):
        raise ValueError("provide distinct theorem names")
    if any(not THEOREM_NAME.fullmatch(name) for name in theorems):
        raise ValueError("theorem names must be OCaml qualified identifiers")
    before = source_snapshot(source, helper)
    output.mkdir(parents=True, exist_ok=True)
    selected_library = None
    if library_through is not None:
        library_text = (source / "hol_lib.ml").read_text()
        marker = 'loads ' + json.dumps(library_through) + ';;'
        if library_text.count(marker) != 1:
            raise ValueError("the selected library endpoint is not unique in hol_lib.ml")
        selected_library = output / "hol.prooftrace.lib.ml"
        selected_library.write_text(library_text[:library_text.index(marker) + len(marker)] + "\n")
    kernel = output / "fusion.prooftrace.ml"
    with (output / "kernel-patch.log").open("w") as log:
        subprocess.run(
            ["patch", "--batch", "--output", str(kernel),
             str(source / "fusion.ml"), str(source / "ProofTrace/fusion.ml.diff")],
            cwd=source, stdout=log, stderr=subprocess.STDOUT, check=True)
    original_init = (source / "hol.ml").read_text()
    anchor = "file_loader := fun s -> Toploop.use_file Format.std_formatter s;;"
    if original_init.count(anchor) != 1:
        raise ValueError("source loader anchor is not unique")
    library_redirect = ("else if Filename.basename s = \"hol_lib.ml\" then "
                        "Sys.getenv \"PRIME_HOL_PROOFTRACE_LIBRARY\" "
                        if selected_library else "")
    replacement = (
        "file_loader := fun s -> Toploop.use_file Format.std_formatter "
        "(if Filename.basename s = \"fusion.ml\" then "
        "Sys.getenv \"PRIME_HOL_PROOFTRACE_KERNEL\" " + library_redirect + "else s);;\n"
        "use_file_raise_failure := true;;")
    init = output / "hol.prooftrace.init.ml"
    init.write_text(original_init.replace(anchor, replacement))
    prefix = output / "closure"
    driver = output / "export.ml"
    driver.write_text(
        "loadt " + json.dumps(str(helper), ensure_ascii=False) + ";;\n"
        "dump_structured_proof_closures " + json.dumps(str(prefix), ensure_ascii=False) + " [\n" +
        ";\n".join("(" + json.dumps(name) + ", " + name + ")" for name in theorems) +
        "\n];;\nexit 0;;\n")
    environment = dict(os.environ, HOLLIGHT_DIR=str(source), HOLLIGHT_USE_MODULE="0",
                       MIK_HOL_LIGHT_LIBRARY_ONLY="1", PRIME_HOL_PROOFTRACE_KERNEL=str(kernel))
    if selected_library:
        environment["PRIME_HOL_PROOFTRACE_LIBRARY"] = str(selected_library)
    with (output / "source-export.log").open("w") as log:
        subprocess.run(
            [str(source / "ocaml-hol"), "-init", str(init), "-I", str(source)],
            input="loadt " + json.dumps(str(driver), ensure_ascii=False) + ";;\n",
            cwd=source, env=environment, text=True, stdout=log, stderr=subprocess.STDOUT, check=True)
    accounting = inspect_closure(prefix, theorems)
    after = source_snapshot(source, helper)
    if before != after:
        raise ValueError("HOL Light source or export helper changed during export")
    receipt = {"format": "hol-light-structured-proof-closure-v1", "source": before,
               "library_through": library_through,
               "accounting": accounting, "proofs_checked": False,
               "artifacts": {path.name: sha256(path) for path in [
                   kernel, init, driver, prefix.with_suffix(".nodes.jsonl"),
                   prefix.with_suffix(".roots.jsonl"), prefix.with_suffix(".signature.json")]}}
    if selected_library:
        receipt["artifacts"][selected_library.name] = sha256(selected_library)
    (output / "export.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--closure-helper", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--theorem", required=True, action="append")
    parser.add_argument("--library-through", help="load the pinned library prefix through this module")
    arguments = parser.parse_args()
    receipt = export(arguments.source.resolve(), arguments.closure_helper.resolve(),
                     arguments.output.resolve(), arguments.theorem, arguments.library_through)
    print(json.dumps(receipt["accounting"], sort_keys=True))


if __name__ == "__main__":
    main()
