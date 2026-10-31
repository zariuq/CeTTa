"""Finite MeTTa polling reference: progress, completion, replay and cancellation."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    args = parser.parse_args()
    worker = Worker(args.worker.resolve(), ROOT / "tests/support/external_stream_reference.metta",
                    "external-ref:run", language="petta")
    checks = 0
    try:
        def step(state, event):
            answer = worker.call(E("Step", state, event))
            assert answer[0] == S("StreamPollV1"), answer
            return answer[1:]
        initial = E("StreamReadV1", 2, 0, (), S("Running"), S("NotRequested"), S("Unsettled"))
        kind, state = step(initial, S("Wait"))
        assert kind == S("Progress") and state == initial
        kind, state = step(state, E("Frame", 0, 0))
        assert kind == E("Item", 0) and state[2:4] == (1, (0,))
        checks += 2
        before = state
        kind, state = step(state, E("Frame", 0, 0))
        assert kind == S("Progress") and state == before
        kind, state = step(state, E("Frame", 1, 1))
        assert kind == E("Item", 1) and state[2:4] == (2, (1, 0))
        kind, state = step(state, S("End"))
        assert kind == S("Complete") and state[4] == S("Complete")
        checks += 3
        for event in [S("Error"), S("Drop"), S("End"), E("Frame", 1, 1), E("Frame", 0, 1)]:
            kind, state = step(initial, event)
            assert kind == S("Incomplete") and state[2] == 0
            again, same = step(state, E("Frame", 0, 0))
            assert again == S("Incomplete") and same == state
            checks += 2
        kind, state = step(initial, S("CancelAccepted"))
        assert kind == S("Progress") and state[4:] == (S("Running"), S("Requested"), S("Unsettled")), (kind, state)
        kind, state = step(state, S("Cancelled"))
        assert kind == S("Incomplete") and state[4:] == (S("Cancelled"), S("Requested"), S("Settled"))
        checks += 2
        empty = E("StreamReadV1", 0, 0, (), S("Running"), S("NotRequested"), S("Unsettled"))
        assert step(empty, S("Wait")) == (S("Progress"), empty)
        assert step(empty, S("End"))[0] == S("Complete")
        checks += 2
    finally:
        worker.close()
    print(f"PASS: {checks} executable MeTTa stream-reference checks")


if __name__ == "__main__":
    main()
