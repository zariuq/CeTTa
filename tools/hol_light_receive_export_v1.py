"""Receive a complete structured HOL Light proof closure.

The existing source adapter checks graph accounting and groups paired type
definitions. Logical checking belongs entirely to the supplied executable
receiver. The resulting receipt binds the inputs, receiver and complete
node/root accounting; it is not itself a proof or an admission credential.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from hol_light_prepare_export_v1 import group_type_definitions
from hol_light_proof_export_v1 import inspect_closure, sha256


def check_readout(accounting: dict, events: int, stdout: str) -> dict:
    """Require full coverage as well as a successful receiving verdict."""
    outputs = [json.loads(line) for line in stdout.splitlines() if line.strip()]
    if (len(outputs) != 2 or set(outputs[0]) != {"checkedRoots"}
            or outputs[1].get("status") != "Checked"):
        raise ValueError("receiver did not return roots and a checked summary")
    roots, summary = outputs[0]["checkedRoots"], outputs[1]
    if not isinstance(roots, list):
        raise ValueError("receiver root readout is not a list")
    requested = [(root["name"], root["id"]) for root in accounting["roots"]]
    received = [(root["name"], root["sourceId"]) for root in roots]
    if received != requested:
        raise ValueError("receiving readout does not cover exactly the requested roots")
    expected = {
        "events": events,
        "theorems": accounting["nodes"],
        "retainedReceipts": events,
        "sourceNodes": accounting["nodes"],
        "sourcePremiseOccurrences": accounting["premise_occurrences"],
    }
    for name, value in expected.items():
        if type(summary.get(name)) is not int or summary[name] != value:
            raise ValueError(f"incomplete receiving accounting: {name}")
    if type(summary.get("objectAxioms")) is not int or summary["objectAxioms"] < 0:
        raise ValueError("receiver omitted its object-axiom accounting")
    for root in roots:
        if type(root.get("sourceId")) is not int or root["sourceId"] < 0:
            raise ValueError("receiver returned an invalid source address")
        for field in ("cacheIndex", "hypotheses", "objectAxioms"):
            if type(root.get(field)) is not int or root[field] < 0:
                raise ValueError(f"receiver omitted root {field}")
        if root["cacheIndex"] >= accounting["nodes"]:
            raise ValueError("receiver root is outside its checked cache")
        if root["objectAxioms"] > summary["objectAxioms"]:
            raise ValueError("root dependencies exceed the receiving dependency union")
    return {"checkedRoots": roots, "summary": summary}


def receive(prefix: Path, receiver: Path, output: Path, theory: Path | None = None) -> dict:
    prefix, receiver, output = prefix.resolve(), receiver.resolve(strict=True), output.resolve()
    sources = {suffix.removeprefix("."): prefix.with_suffix(suffix)
               for suffix in (".nodes.jsonl", ".roots.jsonl", ".signature.json")}
    if theory is not None:
        sources["selected-theory.json"] = theory.resolve(strict=True)
    roots = [json.loads(line) for line in sources["roots.jsonl"].read_text().splitlines()
             if line.strip()]
    before = {name: sha256(path) for name, path in sources.items()}
    receiver_hash = sha256(receiver)
    accounting = inspect_closure(prefix, [root["name"] for root in roots])
    # A fresh directory prevents an earlier successful receipt from surviving
    # a later refusal under the same output location.
    output.mkdir(parents=True, exist_ok=False)
    prepared = output / "received.events.jsonl"
    if prepared in sources.values() or receiver == prepared:
        raise ValueError("receiving output would overwrite an input")
    events = 0
    with sources["nodes.jsonl"].open() as source, prepared.open("w") as target:
        for event in group_type_definitions(json.loads(line) for line in source):
            target.write(json.dumps(event, separators=(",", ":")) + "\n")
            events += 1
    prepared_hash = sha256(prepared)
    command = [str(receiver), str(prepared)]
    if theory is not None:
        command.extend(["--theory", str(sources["selected-theory.json"])])
    command.extend(["--roots", str(sources["roots.jsonl"])])
    checked = subprocess.run(command, capture_output=True, text=True)
    (output / "receiver.stdout").write_text(checked.stdout)
    (output / "receiver.stderr").write_text(checked.stderr)
    if checked.returncode:
        raise ValueError(f"receiving calculus refused the export (exit {checked.returncode})")
    readout = check_readout(accounting, events, checked.stdout)
    if (before != {name: sha256(path) for name, path in sources.items()}
            or receiver_hash != sha256(receiver) or prepared_hash != sha256(prepared)):
        raise ValueError("receiving inputs or executable changed during checking")
    receipt = {
        "schema": "hol-light-received-closure-v1",
        "status": "Checked",
        "sourceArtifactsSha256": before,
        "preparedEventsSha256": prepared_hash,
        "receivingExecutableSha256": receiver_hash,
        "sourceAccounting": accounting,
        "receivingReadout": readout,
        "receiptIsAdmissionCredential": False,
        "runtimeImplementationCorrectnessClaimed": False,
    }
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-prefix", type=Path, required=True)
    parser.add_argument("--receiver", type=Path, required=True)
    parser.add_argument("--theory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    receipt = receive(args.export_prefix, args.receiver, args.output, args.theory)
    print(json.dumps(receipt["receivingReadout"]))


if __name__ == "__main__":
    main()
