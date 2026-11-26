#!/usr/bin/env python3
"""A real process-stream exchange with a Lean REPL (leanprover-community/repl).

An external-service example, not a test: it needs a built REPL and the Lean
toolchain the REPL was built with.  It sends dependent requests through one
process, tests invalid commands and environment rollback, and drives two
tactic steps using returned proof-state identifiers.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/support"))
from check_process_stream import IMPORTS  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--repl", type=Path, required=True,
                        help="an already built leanprover-community/repl executable")
    parser.add_argument("--lean", default="lean",
                        help="Lean executable matching the REPL's build toolchain")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/process-stream-lean-repl")
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    def run(name, body, *, raw=False):
        source = output / f"{name}.metta"
        source.write_text(IMPORTS + body + "\n")
        result = subprocess.run([str(binary), "--lang", "petta"] +
                                (["--no-results"] if raw else []) + [str(source)],
                                cwd=ROOT, capture_output=True, timeout=30)
        (output / f"{name}.stdout").write_bytes(result.stdout)
        (output / f"{name}.stderr").write_bytes(result.stderr)
        assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
        return result

    repl = args.repl.resolve()
    assert repl.is_file() and os.access(repl, os.X_OK), repl
    sysroot = subprocess.check_output([args.lean, "--print-prefix"], text=True).strip()
    environment = f'(("LEAN_SYSROOT" {json.dumps(sysroot)}) ("PATH" {json.dumps(os.defpath)}))'
    result = run("lean-repl", f'''!(let* (($p (proc:spawn ({json.dumps(str(repl))})
      ((mode stream) (max-bytes 1048576) (timeout-ms 15000) (lifetime owner) (env {environment}))))
      ($base (stream-test:request $p (JsonObject ((JsonPair "cmd" "import Lean\\ndef clientSeed : Nat := 37")))))
      ($env (json:get $base "env"))
      ($value (stream-test:request $p (JsonObject ((JsonPair "cmd" "#eval clientSeed + 5") (JsonPair "env" $env)))))
      ($bad (stream-test:request $p (JsonObject ((JsonPair "cmd" "example : clientSeed = 38 := by rfl") (JsonPair "env" $env)))))
      ($good (stream-test:request $p (JsonObject ((JsonPair "cmd" "theorem checkedSeed : clientSeed = 37 := rfl") (JsonPair "env" $env)))))
      ($rollback (stream-test:request $p (JsonObject ((JsonPair "cmd" "#check checkedSeed") (JsonPair "env" $env)))))
      ($kept (stream-test:request $p (JsonObject ((JsonPair "cmd" "#check checkedSeed") (JsonPair "env" (json:get $good "env"))))))
      ($proof (stream-test:request $p (JsonObject ((JsonPair "cmd" "example : True ∧ True := by exact And.intro True.intro True.intro")
         (JsonPair "allTactics" True) (JsonPair "env" $env)))))
      ($first (index-atom (index-atom (json:get $proof "tactics") 1) 0))
      ($split (stream-test:request $p (JsonObject ((JsonPair "tactic" "constructor") (JsonPair "proofState" (json:get $first "proofState"))))))
      ($finish (stream-test:request $p (JsonObject ((JsonPair "tactic" "all_goals trivial") (JsonPair "proofState" (json:get $split "proofState"))))))
      ($end (proc:close-input $p)) ($done (proc:await $p)) ($closed (proc:close $p))) ())''', raw=True)
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(replies) == 9, (replies, result.stderr)
    def errors(reply):
        return [m for m in reply.get("messages", []) if m.get("severity") == "error"]
    assert all(isinstance(row.get("env"), int) for row in replies[:7]), replies
    assert any(m.get("data") == "42" for m in replies[1].get("messages", [])), replies[1]
    assert errors(replies[2]) and errors(replies[4]), replies
    assert all(not errors(replies[i]) for i in (0, 1, 3, 5, 6)), replies
    assert len(replies[7].get("goals", [])) == 2 and replies[8].get("goals") == [], replies[7:]
    assert not any(row.get("sorries") for row in replies), replies
    print("(ProcessStreamLeanReplSummary 9 0)")


if __name__ == "__main__":
    main()
