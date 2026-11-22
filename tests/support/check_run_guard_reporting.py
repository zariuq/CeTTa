"""Check physical termination separately from unfinalized guest outcomes."""
import argparse
import json
from pathlib import Path
import resource
import signal
import subprocess
import tempfile

from native_observation import process_signal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--artifacts", type=Path, default=Path("runtime"))
    args = parser.parse_args()
    cases = [("empty", 0, None), ("complete", 0, None), ("fault", 2, None),
             ("raw-unknown", 1, 0), ("cpu-unknown", 128 + signal.SIGXCPU, signal.SIGXCPU),
             ("oversized-status", 1, 0), ("raw-zero", 1, 0),
             ("raw-cpu-status", 1, 128 + signal.SIGXCPU),
             ("raw-kill-status", 1, 128 + signal.SIGKILL),
             ("signal", 128 + signal.SIGTERM, signal.SIGTERM),
             ("cpu", 128 + signal.SIGXCPU, signal.SIGXCPU),
             ("kill", 128 + signal.SIGKILL, signal.SIGKILL),
             ("large-catalogue", 1, 0), ("catalogue-changed", 1, 0), ("closed-channel", 1, 1)]
    with tempfile.TemporaryDirectory(prefix="process-receipts-", dir=args.artifacts.resolve()) as directory:
        work = Path(directory)
        for mode, expected, physical in cases:
            observation = work / (mode + ".jsonl")
            report = Path(str(observation) + ".process.json")
            result = subprocess.run([str(args.binary.resolve()), "--probe", mode, str(report)],
                capture_output=True, timeout=10,
                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
            assert result.returncode == expected, (mode, result.returncode, expected)
            signalled = mode in ("signal", "cpu", "kill", "cpu-unknown")
            assert process_signal(result.returncode, observation) == (physical if signalled else None), mode
            if physical is None:
                assert not report.exists(), mode
                continue
            found = json.loads(report.read_text())
            assert found["contract_exit"] == 1 and not found["document_framing_ok"]
            assert found["dialect_exit"] is None
            assert not found["query_details_available"] and found["queries"] == []
            assert found["queries_unsettled"] == found["queries_declared"]
            if mode.endswith("unknown"):
                assert not found["query_catalogue_available"] and found["queries_declared"] is None
            if mode == "large-catalogue":
                assert found["queries_declared"] == 2**64-1 and report.stat().st_size < 1024
            # A mismatched status or a guest's complete report is no signal
            # receipt, even if its integer happens to use the shell convention.
            if signalled:
                assert process_signal(1, observation) is None
                found["document_framing_ok"] = True
                report.write_text(json.dumps(found))
                assert process_signal(expected, observation) is None
    print("PASS: 15 process outcomes retain signals, reject raw lookalike exits and keep unknown frontiers explicit")


if __name__ == "__main__":
    main()
