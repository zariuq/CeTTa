"""Exact point selection reuses the owning captured Space matcher."""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="point-query-") as directory:
        worker = Worker(args.worker.resolve(), ROOT / "tests/support/point_query_fixture.metta",
                        "point-test:run", language="petta")
        try:
            answer = worker.call(directory)
            assert answer[0] == S("PointQueryV1"), answer
            row = answer[1]
            assert row[0:3] == (S("StoreQueryRow"), 0, E("Result", S("same"))), answer
            assert len(row[3]) == 1 and row[3][0][0] == S("StoreOccurrenceV1") and row[3][0][2] == "one", answer
            assert answer[2:5] == (S("StoreQueryCheckpointed"), S("StoreQueryEnd"), S("StoreAbsent")), answer
            assert answer[5] == (row[2],), answer
        finally:
            worker.close()
    print("PASS: exact point MeTTa query preserves identity, captured owner lifetime, checkpoint and absence")


if __name__ == "__main__":
    main()
