#!/usr/bin/env python3
"""Compare native occurrence delivery with the executable Lean log model."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scenarios(cases, steps, seed):
    samples = [
        [{"op": "init", "clients": 2, "limit": 3}, {"op": "peek", "client": 0}]
        + [{"op": "append", "value": value} for value in [7, 7, 9]]
        + [{"op": "pull", "client": 0} for _ in range(3)]
        + [{"op": "peek", "client": 0}, {"op": "trim"}, {"op": "append", "value": 10},
           {"op": "peek", "client": 1}, {"op": "cancel", "client": 0},
           {"op": "pull", "client": 1}, {"op": "trim"},
           {"op": "append", "value": 10}, {"op": "complete"}]
        + [{"op": "pull", "client": 1} for _ in range(5)]
        + [{"op": "trim"}, {"op": "append", "value": 11}],
        [{"op": "init", "clients": 1, "limit": 20}]
        + [{"op": "append", "value": value} for value in range(8)]
        + [{"op": "pull", "client": 0} for _ in range(5)]
        + [{"op": "trim"}]
        + [{"op": "append", "value": value} for value in range(8, 20)]
        + [{"op": "pull", "client": 0}, {"op": "trim"}] * 20,
        [{"op": "init", "clients": 0, "limit": 0},
         {"op": "append", "value": 0}, {"op": "trim"},
         {"op": "complete"}, {"op": "append", "value": 1}],
        [{"op": "init", "clients": 0, "limit": 3}]
        + [{"op": "append", "value": 5}, {"op": "trim"}] * 40,
        [{"op": "init", "clients": 3, "limit": 2},
         {"op": "peek", "client": 0}, {"op": "cancel", "client": 1},
         {"op": "append", "value": 0}, {"op": "complete"},
         {"op": "pull", "client": 0}, {"op": "pull", "client": 0},
         {"op": "peek", "client": 1}, {"op": "pull", "client": 2},
         {"op": "trim"}, {"op": "pull", "client": 2}],
    ]
    rng = random.Random(seed)
    for _ in range(cases):
        clients = rng.randrange(9)
        sample = [{"op": "init", "clients": clients, "limit": rng.randrange(33)}]
        for index in range(steps):
            choices = ["append"] * 3 + ["trim"] * 2
            if clients:
                choices += ["pull"] * 4 + ["peek"] * 2
                if index > steps * 2 // 3:
                    choices += ["cancel"]
            if index > steps * 3 // 4:
                choices += ["complete"]
            operation = rng.choice(choices)
            request = {"op": operation}
            if operation == "append":
                request["value"] = rng.randrange(5)
            elif operation in ["pull", "peek", "cancel"]:
                request["client"] = rng.randrange(clients)
            sample.append(request)
        samples.append(sample)
    return samples


def native_input(commands):
    lines = []
    for command in commands:
        op = command["op"]
        fields = [op]
        if op == "init":
            fields += [str(command["clients"]), str(command["limit"])]
        elif op == "append":
            fields += [str(command["value"])]
        elif op in ["peek", "pull", "cancel"]:
            fields += [str(command["client"])]
        lines.append(" ".join(fields))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--lean-root", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--cases", type=int, default=128)
    parser.add_argument("--steps", type=int, default=192)
    parser.add_argument("--seed", type=int, default=71010)
    parser.add_argument("--mutant", action="append", default=[], type=Path)
    args = parser.parse_args()
    if args.cases < 1 or args.steps < 1:
        parser.error("cases and steps must be positive")
    args.binary = args.binary.resolve()
    args.lean_root = args.lean_root.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    reference = args.lean_root / "scripts/replay_shared_occurrence_log.lean"
    model = args.lean_root / "Mettapedia/Machines/Cursor/SharedOccurrenceLog.lean"
    identities = {"native_sha256": sha256(args.binary), "lean_client_sha256": sha256(reference),
                  "lean_model_sha256": sha256(model)}
    samples = scenarios(args.cases, args.steps, args.seed)
    inputs = []
    for index, sample in enumerate(samples):
        path = args.out / f"scenario-{index:04d}.jsonl"
        path.write_text("".join(json.dumps(command, separators=(",", ":")) + "\n" for command in sample))
        inputs.append(path)
    command = ["lake", "env", "lean", "--run", str(reference), *map(str, inputs)]
    lean = subprocess.run(command, cwd=args.lean_root, text=True, capture_output=True)
    (args.out / "lean.stdout.jsonl").write_text(lean.stdout)
    (args.out / "lean.stderr.log").write_text(lean.stderr)
    if lean.returncode:
        raise RuntimeError(f"Lean replay failed with exit {lean.returncode}; evidence retained")
    expected = [json.loads(line) for line in lean.stdout.splitlines()]
    operations = sum(map(len, samples))
    if len(expected) != operations:
        raise AssertionError(f"Lean returned {len(expected)} observations for {operations} operations")
    offset = 0
    for index, sample in enumerate(samples):
        native = subprocess.run([str(args.binary), "--replay"], input=native_input(sample),
                                text=True, capture_output=True, env=os.environ)
        (args.out / f"native-{index:04d}.stdout.jsonl").write_text(native.stdout)
        (args.out / f"native-{index:04d}.stderr.log").write_text(native.stderr)
        if native.returncode or native.stderr:
            raise RuntimeError(f"native replay failed in scenario {index}: {native.returncode}")
        observed = [json.loads(line) for line in native.stdout.splitlines()]
        wanted = expected[offset:offset + len(sample)]
        if observed != wanted:
            for step in range(max(len(observed), len(wanted))):
                got = observed[step] if step < len(observed) else None
                want = wanted[step] if step < len(wanted) else None
                if got != want:
                    raise AssertionError(f"scenario {index}, step {step}: {got} != {want}")
        offset += len(sample)
    mutants = []
    for path in args.mutant:
        path = path.resolve()
        result = subprocess.run([str(path), "--replay"], input=native_input(samples[0]),
                                text=True, capture_output=True)
        stem = args.out / ("mutant-" + path.name)
        stem.with_suffix(".stdout.log").write_text(result.stdout)
        stem.with_suffix(".stderr.log").write_text(result.stderr)
        try:
            observations = [json.loads(line) for line in result.stdout.splitlines()]
        except json.JSONDecodeError:
            observations = None
        rejected = result.returncode != 0 or observations != expected[:len(samples[0])]
        mutants.append({"name": path.name, "sha256": sha256(path), "exit": result.returncode,
                        "rejected": rejected})
        if not rejected:
            raise AssertionError(f"negative control {path.name} was not detected")
    assert identities["native_sha256"] == sha256(args.binary)
    assert identities["lean_client_sha256"] == sha256(reference)
    assert identities["lean_model_sha256"] == sha256(model)
    receipt = {"scenarios": len(samples), "operations": operations, "seed": args.seed,
               "every_reply_and_logical_residual_matches": True, "mutants": mutants,
               **identities,
               "scope": "Fixed-cohort occurrence delivery in admitted finite numeric ranges; not producer evaluation, native root discovery or compiled-code verification."}
    (args.out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
