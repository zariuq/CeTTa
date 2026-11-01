"""The same range/dependency lessons embedded, reopened and served."""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S
from check_native_kb_service import Server, Client
from check_space_query import rows, bag, open_query, drain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="range-lessons-") as directory:
        root = Path(directory)
        store = root / "store"
        store.mkdir(mode=0o700)
        fixture = ROOT / "tests/support/scalar_range_curriculum_fixture.metta"
        with_owner = Worker(args.worker.resolve(), fixture, "range-lesson-test:run", language="petta")
        try:
            plans = with_owner.call(S("Plans"))
            answer = with_owner.call(E("At", str(store)))
        finally:
            with_owner.close()
        assert answer[0] == S("RangeCurriculumV1"), answer
        expected = [rows(result) for result in answer[1]]
        assert list(map(len, expected)) == [3, 2, 3], expected
        reopened = Worker(args.worker.resolve(), fixture, "range-lesson-test:run", language="petta")
        try:
            assert reopened.call(E("At", str(store))) == answer
        finally:
            reopened.close()
        server = Server(args.worker.resolve(), root, module=ROOT / "tests/support/space_query_server.metta", entry="query-test:configuration")
        client = None
        try:
            server.start()
            client = Client(server.path)
            view, _ = client.snapshot()
            for plan, wanted in zip(plans, expected):
                got, _ = drain(client, open_query(client, view, plan), 1, 2)
                assert bag(got) == bag(wanted), (got, wanted)
            client.drop(view)
        finally:
            if client:
                client.close()
            server.stop()
    print("PASS: range/dependency curriculum counts3/2/3 preserve identities across fresh process and service")


if __name__ == "__main__":
    main()
