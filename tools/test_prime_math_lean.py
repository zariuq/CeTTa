#!/usr/bin/env python3
"""Join Prime workspace persistence with independently checked Lean receipts."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import uuid

from math_space import MathSpaceSession, ROOT, WorkspaceError


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--lean-root", type=Path, required=True)
    parser.add_argument("--receipt-report", type=Path, required=True,
                        help="successful check_ai_mathematician_receipts.py report")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/math-lean-qualification")
    args = parser.parse_args()
    receipt_report = json.loads(args.receipt_report.read_text())
    assert receipt_report["passed"] is True
    receipts = args.receipt_report.resolve().parent
    lean_root = args.lean_root.resolve()
    assert digest(lean_root / ".cetta/lean-native/itp-guard") == receipt_report["guard_sha256"]
    assert digest(lean_root / ".cetta/lean-native/lib/MegLib.olean") == receipt_report["module_sha256"]
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = args.output.resolve() / run
    output.mkdir(parents=True)
    binary = args.binary.resolve()
    checkpoint = output / "project.json"
    exchanges = []
    report = {"schema": "prime.math-lean.qualification.v1", "passed": False,
              "binary_sha256": digest(binary), "guard_sha256": receipt_report["guard_sha256"],
              "sources": {str(p.relative_to(ROOT)): digest(p)
                          for p in (ROOT / "lib/prime").glob("math_space*.metta")}}

    def call(session, command, state, **fields):
        request = {"id": str(len(exchanges) + 1), "command": command, **fields}
        response = session.request(request)
        exchanges.append({"request": request, "reply": response})
        assert response["state"] == state, exchanges[-1]
        assert response["coverage"] == "complete", exchanges[-1]
        return response

    def replay(session, path, state):
        return call(session, "lean-replay", state, target="orIL", certificate=str(path),
                    milliseconds=10000, bytes=65536)

    try:
        # The controller explicitly chooses this external artifact directory.
        # Checkpoint writes below use a distinct, fresh path inside it.
        with MathSpaceSession(binary, receipts, lean_root=lean_root) as session:
            call(session, "declare", "data", source="(hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))")
            call(session, "goal", "data", goal="native-false", checker="type",
                 statement="(id Nat Z (S Z))", description="Foreign success is not native authority.")
            checked = replay(session, Path(receipt_report["honest_certificate"]), "checked-foreign")
            assert "Megalodon.or" in checked["result"] and "compiled-source-order" in checked["result"]
            assert checked["backend_files"] and all(not Path(name).is_absolute() for name in checked["backend_files"])
            call(session, "status", "open", goal="native-false", budget=10000)
            call(session, "publish", "missing", attempt="3", name="forged-native", budget=10000)
            for name in ("self-reference/0.olean", "future-reference/0.olean", "changed-type.olean", "axiom.olean"):
                replay(session, receipts / name, "rejected-foreign-evidence")
            checkpoint = receipts / f"prime-project-{run}.json"
            call(session, "save", "saved", path=str(checkpoint), budget=10000)
        with MathSpaceSession(binary, receipts, lean_root=lean_root) as restarted:
            call(restarted, "load", "loaded", path=str(checkpoint), budget=10000)
            call(restarted, "status", "open", goal="native-false", budget=10000)
            notes = call(restarted, "view", "data", section="notes", cursor="start", rows=1, characters=6000)
            assert "Math:ReceiptObservation" in notes["result"] and "honest/0.olean" in notes["result"]
            replay(restarted, Path(receipt_report["honest_certificate"]), "checked-foreign")
        # Detect a changed backend without changing the actual application's files.
        fixture = output / "changed-backend"
        bundle = fixture / ".cetta/lean-native"
        (bundle / "lib").mkdir(parents=True)
        original = lean_root / ".cetta/lean-native"
        for name in ("itp-guard", "lib/Bridge.olean", "lib/CettaProofTactics.olean"):
            (bundle / name).symlink_to(original / name)
        shutil.copyfile(original / "lib/MegLib.olean", bundle / "lib/MegLib.olean")
        shutil.copyfile(original / "sysroot.txt", bundle / "sysroot.txt")
        with MathSpaceSession(binary, receipts, lean_root=fixture) as changed:
            with (bundle / "lib/MegLib.olean").open("ab") as stream:
                stream.write(b"changed-test-fixture")
            try:
                replay(changed, Path(receipt_report["honest_certificate"]), "checked-foreign")
            except WorkspaceError as error:
                assert error.reply["reason"] == "selected-lean-backend-changed", error.reply
            else:
                raise AssertionError("changed selected backend was not refused")
        with MathSpaceSession(binary, receipts) as unconfigured:
            replay(unconfigured, Path(receipt_report["honest_certificate"]), "unavailable")
        assert digest(binary) == report["binary_sha256"]
        assert all(digest(ROOT / name) == sha for name, sha in report["sources"].items())
        report["passed"] = True
        print(f"Prime/Lean: {len(exchanges)} exchanges passed; receipt replay, restart, scoped authority and backend changes")
    finally:
        report["exchanges"] = exchanges
        report["checkpoint"] = str(checkpoint)
        (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output)


if __name__ == "__main__":
    main()
