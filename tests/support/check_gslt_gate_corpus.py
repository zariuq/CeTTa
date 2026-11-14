#!/usr/bin/env python3
"""Capture production gates and retain qualified attempts across interruptions."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

import gslt_invocation as invocation


def gate_identity(root):
    paths = [root / "Makefile", *sorted((root / "mk").glob("*.mk"))]
    paths += [p for p in sorted((root / "tests").rglob("*"))
              if p.is_file() and p.suffix in (".py", ".sh", ".metta", ".expected", ".json", ".c")]
    return invocation.digest(json.dumps([(str(p.relative_to(root)), invocation.digest(p.read_bytes()))
                                          for p in paths]).encode())


def successful_attempt(path, identity):
    try:
        receipt = json.loads((path / "gate.json").read_text())
        if receipt["status"] != 0 or receipt["identity"] != identity:
            return False
        records = sorted(path.glob("*.invocation.json"))
        if not records:
            return False
        for record_path in records:
            with invocation.restored_inputs(record_path) as record:
                if record.get("source_gate_status") != 0 or record["process"] != {
                        "state": "exited", "returncode": 0}:
                    return False
        return True
    except (OSError, ValueError, KeyError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--make", default="make")
    parser.add_argument("--gates", nargs="+", required=True)
    parser.add_argument("--capture-only", action="store_true")
    parser.add_argument("--lane", choices=["native", "specs"], default="native")
    args = parser.parse_args()
    root = invocation.ROOT
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    with (work / "owner.lock").open("a") as owner:
        try:
            fcntl.flock(owner, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit("another qualification owns this work directory")
        state_path = work / "state.json"
        state = json.loads(state_path.read_text()) if state_path.exists() else {}
        identity = gate_identity(root)
        selected, failed = [], False
        for gate in args.gates:
            previous = state.get(gate)
            attempt = Path(previous) if previous else None
            if attempt is not None and successful_attempt(attempt, identity):
                print(f"REUSE: {gate}", flush=True)
            else:
                attempt = work / "attempts" / (gate + "." + uuid.uuid4().hex)
                attempt.mkdir(parents=True)
                invocation.save_json(attempt / "gate.json", dict(status="pending", identity=identity))
                state[gate] = str(attempt)
                invocation.save_json(state_path, state)
                env = dict(os.environ, CETTA_GSLT_RECORD_DIR=str(attempt), CETTA_GSLT_SOURCE_GATE=gate)
                runner = ("sh tests/support/record_petta_direct_run.sh '" + str(attempt).replace("'", "'\\''")
                          + "' $(words env CETTA_PETTA_SEARCH_MACHINE=1 $(CETTA_BIN_INVOKE) --lang petta) "
                            "env CETTA_PETTA_SEARCH_MACHINE=1 $(CETTA_BIN_INVOKE) --lang petta")
                command = [args.make, "--no-print-directory", gate, "CETTA_PETTA_DIRECT_RUN_V1=" + runner]
                with (attempt / "gate.log").open("w") as log:
                    run = subprocess.run(command, cwd=root, env=env, stdout=log,
                                         stderr=subprocess.STDOUT, close_fds=False)
                invocation.main(["finish-gate", str(attempt), gate, str(run.returncode)])
                invocation.save_json(attempt / "gate.json", dict(status=run.returncode, identity=identity,
                                                                 command=command))
            if successful_attempt(attempt, identity):
                records = sorted(attempt.glob("*.invocation.json"))
                selected.extend(records)
                print(f"QUALIFIED SOURCE: {gate} ({len(records)} invocations)", flush=True)
            else:
                print(f"UNQUALIFIED SOURCE: {gate}; retained {attempt.relative_to(work)}", flush=True)
                failed = True
        if args.capture_only:
            return int(failed)
        # Symlink only selected successful attempts. Failed and interrupted
        # attempts remain in place with their own receipts and logs.
        selection = work / ("selection." + uuid.uuid4().hex)
        selection.mkdir()
        for record in selected:
            (selection / record.name).symlink_to(record)
        result = subprocess.run([sys.executable, str(root / "tests/support/check_gslt_through_machine.py"),
            args.binary, "--fire", "transform", "--lane", args.lane,
            "--corpus", str(selection), "--keep", str(work / "runs" / args.lane),
            "--report", str(work / (args.lane + "-report.txt"))], cwd=root)
        return int(failed or result.returncode != 0)


if __name__ == "__main__":
    sys.exit(main())
