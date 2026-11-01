#!/usr/bin/env python3
"""Ordinary MeTTa edits publish through the existing durable collection path."""
import json
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


def run(binary, folder, mode):
    fixture = Path(__file__).with_name("space_workspace_fixture.metta").resolve()
    source = folder / (mode.lower() + ".metta")
    source.write_text(
        f"!(import! &self {json.dumps(str(fixture))})\n"
        f"!(workspace-fixture:run ({mode} {json.dumps(str(folder / 'store'))}))\n"
    )
    result = subprocess.run([str(binary), "--lang", "petta", str(source)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    marker = {"Write": "WORKSPACE_WRITE_OK", "Read": "WORKSPACE_REOPEN_OK",
              "Remove": "WORKSPACE_REMOVE_OK", "ReadAfter": "WORKSPACE_REMOVE_REOPEN_OK"}[mode]
    assert marker in result.stdout, result.stdout + result.stderr
    assert "SHOULD_NOT_EXECUTE" not in result.stdout, result.stdout


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="space-workspace-") as temporary:
        folder = Path(temporary)
        (folder / "store").mkdir(mode=0o700)
        run(binary, folder, "Write")
        run(binary, folder, "Read")
        samples = []
        if args.worker:
            sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
            from value_worker_client import Worker
            from space_service_client import E, S
            worker = Worker(args.worker.resolve(), Path(__file__).with_name("space_workspace_fixture.metta").resolve(),
                            "workspace-fixture:run", language="petta", profile=True)
            try:
                for turn in range(101):
                    assert worker.call(E("Cycle", str(folder / "store"))) == S("WORKSPACE_CYCLE_OK")
                    if turn % 25 == 0:
                        process = Path("/proc") / str(worker.process.pid)
                        rss = next(line for line in (process / "status").read_text().splitlines() if line.startswith("VmRSS:"))
                        samples.append({"round": turn, "rss_kib": int(rss.split()[1]),
                                        "descriptors": len(list((process / "fd").iterdir())), "metrics": worker.last_metrics})
                assert samples[-1]["descriptors"] == samples[0]["descriptors"], samples
            finally:
                worker.close()
        run(binary, folder, "Remove")
        run(binary, folder, "ReadAfter")
        if args.output:
            args.output.write_text(json.dumps({"checks": "workspace edits, replay, data-view validation, closure and reopen",
                                              "lifecycle_samples": samples}, indent=2) + "\n")
        if samples:
            print(json.dumps({"lifecycle_samples": samples}, indent=2))
    print("PASS: ordinary MeTTa workspace edits, replay, view validation, closure and fresh-process reopen")
