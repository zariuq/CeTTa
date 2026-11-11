#!/usr/bin/env python3
"""Reference/uniqueness lesson, including fresh-process durable inspection."""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S


def worker(binary):
    return Worker(binary, ROOT / "examples/kb/constraint_curriculum.metta",
                  "constraint-curriculum:run", language="petta")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="constraint-curriculum-") as temporary:
        path = str(Path(temporary) / "store")
        first = worker(args.worker.resolve())
        try:
            installed = first.call(E("Install", path))
            assert installed[0] == S("db:committed"), installed
            result = first.call(E("Exercise", path))
        finally:
            first.close()
        assert result[0] == S("ConstraintCurriculumV1"), result
        assert result[1][0] == S("db:committed"), result
        assert result[2] == E("Error", S("constraint"), S("duplicate-edge")), result
        assert result[3] == E("Error", S("constraint"), S("missing-reference")), result
        assert result[4] == E("Error", S("constraint"), S("referenced-entity")), result
        assert result[5][0] == S("db:committed"), result
        assert result[6][0] == S("db:committed"), result
        assert set(result[7]) == {E("entity", S("b")), E("entity", S("c"))}, result
        reopened = worker(args.worker.resolve())
        try:
            assert set(reopened.call(E("Inspect", path))) == {
                E("entity", S("b")), E("entity", S("c"))}
            replay = reopened.call(E("Install", path))
            assert replay[0] == S("db:replayed"), replay
        finally:
            reopened.close()
    print("PASS: atomic uniqueness/reference refusal, retraction and fresh-process replay")


if __name__ == "__main__":
    main()
