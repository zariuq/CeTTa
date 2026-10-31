"""Request-owned persistent Space cursors in one long-lived MeTTa worker."""
import argparse
from collections import Counter
import json
import struct
import subprocess
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode, decode
from value_worker_client import Worker


def sample(worker, round_number):
    directory = Path("/proc") / str(worker.process.pid)
    fds = len(list((directory / "fd").iterdir())) if directory.exists() else None
    rss = None
    if directory.exists():
        status = dict(line.split(":", 1) for line in (directory / "status").read_text().splitlines())
        rss = int(status["VmRSS"].split()[0])
    return {"round": round_number, "descriptors": fds, "rss_kib": rss, "metrics": worker.last_metrics}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=500)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds >= 1
    module = ROOT / "tests/support/durable_cursor_fixture.metta"
    bad = encode(E("work", "negative-control", S("FalseAssertion")))
    negative = subprocess.run([str(args.worker.resolve()), "--lang", "petta", str(module), "cursor-fixture:run"],
                              input=b"CSP1" + struct.pack(">I", len(bad)) + bad,
                              capture_output=True, timeout=30)
    frame = negative.stdout.find(b"CSP1")
    assert frame >= 0 and len(negative.stdout) >= frame + 8, negative
    length = struct.unpack(">I", negative.stdout[frame + 4:frame + 8])[0]
    reply = decode(negative.stdout[frame + 8:frame + 8 + length])
    assert reply[:3] == (S("reply"), "negative-control", S("error")), negative
    with tempfile.TemporaryDirectory(prefix="persistent-query-worker-") as directory:
        path = Path(directory) / "store"
        path.mkdir(mode=0o700)
        worker = Worker(args.worker.resolve(), module,
                        "cursor-fixture:run", language="petta", profile=True)
        try:
            assert worker.call(E("Create", str(path))) == S("Created")
            x, y = Var(0), Var(1)
            cases = [
                (E("edge", S("alice"), x), x),
                (E("rel", x, x), E("pair", x, x)),
                (E("danger"), E("danger")),
                (E("edge", S("alice"), x), ()),
                (E("edge", S("alice"), x), E("pair", Var(2), Var(2))),
                (E("edge", S("alice"), x), E("Error", S("source"), S("datum"))),
                (E("absent", x), x),
                (x, x),
                (E(",", E("edge", S("alice"), x), E("edge", S("alice"), y)), E("pair", x, y)),
            ]
            for pattern, template in cases:
                reference = worker.call(E("Reference", str(path), pattern, template))
                answer = worker.call(E("QuerySpec", str(path), pattern, template))
                assert reference[0] == S("ReferenceRows") and answer[0] == S("Rows"), (reference, answer)
                assert Counter(encode(value) for value in reference[1]) == Counter(encode(row[1]) for row in answer[1]), (reference, answer)
            # The exhaustive reference deliberately creates session-lived Space
            # values. Measure the cursor route in its own fresh process.
            worker.close()
            worker = Worker(args.worker.resolve(), module, "cursor-fixture:run", language="petta", profile=True)
            assert worker.call(E("Guards", str(path))) == S("GuardsPassed")
            original = None
            samples = []
            for index in range(args.rounds + 64):
                answer = worker.call(E("Query", str(path), bool(index % 2)))
                assert answer[0] == S("Rows") and len(answer[1]) == 5, answer
                bodies = [row[1] for row in answer[1]]
                identities = [row[2][0] for row in answer[1]]
                assert all(row[0] == S("Row") and len(row[2]) == 1 for row in answer[1])
                assert len({encode(identity) for identity in identities}) == 5
                assert bodies[:2] == [E("edge", S("alice"), S("bob"))] * 2, bodies
                relation = bodies[2]
                assert relation[0] == S("rel") and isinstance(relation[1], Var) and relation[1] == relation[2], relation
                assert bodies[3] == E("danger"), bodies
                assert bodies[4] == E("=", E("inert-rule"), E("println!", S("ACCIDENTAL_EXECUTION"))), bodies
                normalized = encode(answer)
                if original is None:
                    original = normalized
                else:
                    assert normalized == original
                assert worker.call(E("Cancel", str(path), bool(index % 2))) == S("Cancelled")
                assert worker.call(E("Abandon", str(path))) == S("Abandoned")
                # Retire the preceding request arena before measuring.
                assert worker.call(S("Echo")) == S("Echo")
                if index == 63 or (index >= 64 and ((index - 63) % 50 == 0 or index == args.rounds + 63)):
                    samples.append(sample(worker, index - 63))
            descriptors = [item["descriptors"] for item in samples if item["descriptors"] is not None]
            assert not descriptors or max(descriptors) == min(descriptors), samples
            permanent = [item["metrics"]["permanent_bytes"] for item in samples]
            assert max(permanent) == min(permanent), samples
            report = {"rounds_after_warmup": args.rounds, "samples": samples,
                      "scope": "fixed KB, completed/explicitly closed/automatically retired/cancelled/abandoned cursors"}
            if args.output:
                args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            worker.close()


if __name__ == "__main__":
    main()
