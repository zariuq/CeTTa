#!/usr/bin/env python3
"""Exercise an agent's mathematical workspace against the real Prime checker."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import fcntl
import json
from pathlib import Path
import subprocess
from concurrent.futures import ThreadPoolExecutor
import uuid

from math_space import MathSpaceSession, ROOT, WorkspaceError


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/math-agent-qualification")
    args = parser.parse_args()
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = args.output.resolve() / run
    output.mkdir(parents=True)
    binary = args.binary.resolve()
    seed, saved = output / "selector-seed.json", output / "selector-project.json"
    source = output / "seed.metta"
    source.write_text('!(import! &self math_space_files)\n'
                      '!(bind! &p (math:new "selector"))\n'
                      '!(let $t (math:theory &p) (import! $t natural_top_k))\n'
                      f'!(math:save &p {json.dumps(str(seed))} 1000000)\n')
    result = subprocess.run([str(binary), "--lang", "prime", str(source)], cwd=ROOT,
                            capture_output=True, text=True, timeout=30)
    (output / "seed.stdout").write_text(result.stdout)
    assert result.returncode == 0 and not result.stderr and "Math:Saved" in result.stdout, result
    records = []
    report = {"schema": "prime.math-agent.qualification.v1", "passed": False,
              "binary_sha256": digest(binary),
              "sources": {str(p.relative_to(ROOT)): digest(p)
                          for p in (ROOT / "lib/prime").glob("math_space*.metta")}}

    def call(session, command, state=None, contains=None, **fields):
        request = {"id": str(len(records) + 1), "command": command, **fields}
        reply = session.request(request)
        records.append({"request": request, "reply": reply})
        if state is not None:
            assert reply["state"] == state, records[-1]
        if contains is not None:
            assert contains in reply["result"], records[-1]
        assert reply["coverage"] == "complete", records[-1]
        return reply

    statement = "(-> (b : Nat) (k : Nat) (rs : Rows) (id B (leq (len (topk b k rs)) k) tt))"
    rows = "(RCons (row Z (S Z) pa) (RCons (row (S Z) (S (S Z)) pb) RNil))"
    expected = "(RCons (row (S Z) (S (S Z)) pb) RNil)"
    try:
        with MathSpaceSession(binary, output) as session:
            call(session, "create", contains='"temporary"', name="temporary")
            call(session, "summary", contains='"temporary"')
            call(session, "load", "loaded", path=str(seed), budget=1000000)
            call(session, "summary", contains='"selector"')
            call(session, "inspect", contains="topk", name="topk", characters=2000)
            call(session, "run", contains=expected, expression=f"(topk (S (S Z)) (S Z) {rows})")
            call(session, "goal", goal="bound", checker="type", statement=statement,
                 description="The integer selector respects its output budget.")
            call(session, "propose", "candidate", candidate="wrong", goal="bound", strategy="proposal",
                 proof="(lam b (lam k (lam rs (refl ff))))")
            call(session, "check", "rejected-evidence", candidate="wrong", budget=1000000)
            call(session, "status", "open", goal="bound", budget=1000000)
            call(session, "propose", "candidate", candidate="limited", goal="bound", strategy="library",
                 proof="topk-bound")
            call(session, "check", "unfinished", candidate="limited", budget=0)
            call(session, "propose", "candidate", candidate="known", goal="bound", strategy="library",
                 proof="topk-bound")
            call(session, "read", contains="Math:Candidate", section="candidates", key="known", characters=2000)
            call(session, "check", "checked", candidate="known", budget=1000000)
            call(session, "publish", "published", attempt="known", name="selector-bound", budget=1000000)
            call(session, "declare", contains="Established", source='''(hol:define certified-topk
  (-> (b : Nat) (k : Nat) (rs : Rows)
    (sigma (out : Rows) (id B (leq (len out) k) tt)))
  (= (certified-topk $b $k $rs) (pair (topk $b $k $rs) (selector-bound $b $k $rs))))''')
            call(session, "goal", goal="certificate", checker="type",
                 statement="(-> (b : Nat) (k : Nat) (rs : Rows) (sigma (out : Rows) (id B (leq (len out) k) tt)))",
                 description="The function returns its result with a checked bound certificate.")
            call(session, "propose", "candidate", candidate="certificate", goal="certificate",
                 strategy="declared-program", proof="certified-topk")
            call(session, "check", "checked", candidate="certificate", budget=1000000)
            call(session, "run", contains=expected,
                 expression=f"(fst (certified-topk (S (S Z)) (S Z) {rows}))")
            call(session, "note", note="next", text="Try a sharper eligibility property.", source="agent-test")
            call(session, "link", source="certificate", relation="uses", target="bound")
            call(session, "save", "saved", path=str(saved), budget=1000000)
            try:
                call(session, "save", path=str(output.parent / "outside.json"), budget=1000000)
            except WorkspaceError as error:
                assert error.reply["reason"] == "path-outside-workspace", error.reply
            else:
                raise AssertionError("filesystem controller policy was bypassed")
        with MathSpaceSession(binary, output) as restarted:
            call(restarted, "load", "loaded", path=str(saved), budget=1000000)
            call(restarted, "status", "checked", goal="bound", budget=1000000)
            call(restarted, "resume", "checked", previous="limited", attempt="after-restart", budget=1000000)
            call(restarted, "run", contains=expected,
                 expression=f"(fst (certified-topk (S (S Z)) (S Z) {rows}))")
            call(restarted, "view", contains="Try a sharper", section="notes", cursor="start", rows=5, characters=1000)
            for budget in (-1, 0.5, "1000", True):
                call(restarted, "check", "refused", candidate="missing", budget=budget)
            call(restarted, "summary", "refused", unexpected="field")
            call(restarted, "eval", "refused")
            call(restarted, "declare", "refused", source="(hol:axiom cheat (all prop (lam p p)))")
            call(restarted, "summary", contains='"selector"')
            call(restarted, "note", note="unicode", text="𝒜" * 20000, source="unicode-boundary")
            with ThreadPoolExecutor(max_workers=4) as pool:
                parallel = list(pool.map(lambda index: restarted.request(
                    {"id": f"parallel-{index}", "command": "summary"}), range(12)))
            assert {row["id"] for row in parallel} == {f"parallel-{index}" for index in range(12)}
            assert all('"selector"' in row["result"] for row in parallel)
        # A held writer lock makes a real native save wait. A controller timeout
        # must report an uncertain save and close the session, never a theorem.
        blocked = output / "blocked-save.json"
        with open(str(blocked) + ".lock", "a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            with MathSpaceSession(binary, output, timeout=0.2) as waiting:
                try:
                    waiting.request({"id": "timeout", "command": "save", "path": str(blocked), "budget": 1000000})
                except WorkspaceError as error:
                    assert error.reply["state"] == "uncertain", error.reply
                    assert waiting.process.poll() is not None
                else:
                    raise AssertionError("held writer lock did not bound the request")
            fcntl.flock(lock, fcntl.LOCK_UN)
        malformed = ['{', '[]', '{"id":"duplicate","command":"summary","id":"changed"}',
                     '{"id":"after-errors","command":"summary"}']
        raw = subprocess.run([str(binary), "--lang", "prime", "--no-results",
                              str(ROOT / "examples/math_space_agent.metta")],
                             input="\n".join(malformed) + "\n", cwd=ROOT,
                             text=True, capture_output=True, timeout=30)
        assert raw.returncode == 0 and not raw.stderr, raw
        responses = [json.loads(line) for line in raw.stdout.splitlines()]
        assert len(responses) == 4 and [r["state"] for r in responses[:3]] == ["refused"] * 3, responses
        assert responses[3]["id"] == "after-errors" and responses[3]["state"] == "data", responses
        assert "0x" not in raw.stdout
        with MathSpaceSession(binary, output, reply_characters=80) as bounded:
            clipped = bounded.request({"id": "bounded", "command": "summary"})
            assert clipped["coverage"] == "clipped" and len(clipped["result"]) == 80, clipped
            assert clipped["characters"] > 80 and clipped["state"] == "data", clipped
        assert report["binary_sha256"] == digest(binary)
        assert all(digest(ROOT / name) == sha for name, sha in report["sources"].items())
        report["parallel_exchanges"] = parallel
        report["malformed_requests"] = responses
        report["blocked_save_timeout"] = "uncertain; session closed"
        report["bounded_reply"] = clipped
        report["passed"] = True
        print(f"Math agent: {len(records)} native exchanges passed, including restart and proof reuse")
    finally:
        report["exchanges"] = records
        (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output)


if __name__ == "__main__":
    main()
