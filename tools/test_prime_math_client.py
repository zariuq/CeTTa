#!/usr/bin/env python3
"""Drive the real Prime service from HE and PeTTa MeTTa host programmes."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--godel-root", type=Path, help="also load its real CommandSpec discovery API, without starting an agent")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/math-client-qualification")
    args = parser.parse_args()
    binary = args.binary.resolve()
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = args.output.resolve() / run
    output.mkdir(parents=True)
    results = []
    for language in ("he", "petta"):
        workspace = output / language
        workspace.mkdir()
        library = ROOT / ("lib/petta/math_space_client.metta" if language == "petta" else "lib/math_space_client.metta")
        source = f'!(import! &self {json.dumps(str(library))})\n'
        if args.godel_root and language == "petta":
            source += f'!(import! &self {json.dumps(str(args.godel_root.resolve() / "src/api_registry.metta"))})\n'
        registry = bool(args.godel_root and language == "petta")
        if registry:
            source += '''!(let $spec (api:command "math:client-call")
 (case $spec (((CommandSpec 1 "math" "math:client-call" 2 2 $usage $description "")
  (system:write stdout "REGISTRY_OK\\n")) ($other (system:write stdout "REGISTRY_FAILED\\n")))))\n'''
        source += f'!(let* (($client (math:client-open {json.dumps(str(binary))} {json.dumps(str(workspace))} 10))\n'
        requests = [
            ({"command": "create", "name": "host-client"}, "data"),
            ({"command": "declare", "source": "(hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))"}, "data"),
            ({"command": "goal", "goal": "identity", "checker": "type", "statement": "(-> (n : Nat) (id Nat n n))", "description": "Identity."}, "data"),
            ({"command": "propose", "candidate": "wrong", "goal": "identity", "strategy": "proposal", "proof": "(lam n (refl (S n)))"}, "candidate"),
            ({"command": "check", "candidate": "wrong", "budget": 100000}, "rejected-evidence"),
            ({"command": "status", "goal": "identity", "budget": 100000}, "open"),
            ({"command": "propose", "candidate": "good", "goal": "identity", "strategy": "reflexivity", "proof": "(lam n (refl n))"}, "candidate"),
            ({"command": "check", "candidate": "good", "budget": 100000}, "checked"),
            ({"command": "publish", "attempt": "good", "name": "identity", "budget": 100000}, "published"),
            ({"command": "save", "path": "project.json", "budget": 100000}, "saved"),
            ({"command": "load", "path": "project.json", "budget": 100000}, "loaded"),
            ({"command": "status", "goal": "identity", "budget": 100000}, "checked"),
            ({"command": "save", "path": "../outside.json", "budget": 100000}, "refused"),
        ]
        for index, (request, _) in enumerate(requests):
            wire = json.dumps({"id": str(index), **request})
            source += f'  ($reply{index} (math:client-call $client {json.dumps(wire)}))\n'
            source += f'  ($printed{index} (system:write stdout (str:concat (json:stringify $reply{index}) "\\n")))\n'
        source += '  ($closed (math:client-close $client))) ())\n'
        path = output / f"{language}.metta"
        path.write_text(source)
        command = [str(binary), "--lang", language, "--import-mode", "ancestor-walk", "--no-results", str(path)]
        result = subprocess.run(command, cwd=args.godel_root or ROOT, text=True, capture_output=True, timeout=45)
        (output / f"{language}.stdout").write_text(result.stdout)
        (output / f"{language}.stderr").write_text(result.stderr)
        assert result.returncode == 0 and not result.stderr, result
        lines = result.stdout.splitlines()
        if registry:
            assert lines.pop(0) == "REGISTRY_OK", result.stdout
        replies = [json.loads(line) for line in lines]
        assert len(replies) == len(requests), result.stdout
        assert [r["state"] for r in replies] == [state for _, state in requests], replies
        results.append({"language": language, "exchanges": replies, "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        print(f"PASS {language}: {len(replies)} native workspace exchanges")
    report = {"schema": "prime.math-client.qualification.v1", "passed": True,
              "godelclaw_discovery_executed": bool(args.godel_root),
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "results": results}
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Evidence:", output)


if __name__ == "__main__":
    main()
