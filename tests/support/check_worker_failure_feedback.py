"""Evaluator failures retain completion/effect information without stream loss."""
import sys
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import S, E
from value_worker_client import Worker, WorkerEvaluationError, WorkerUnavailable


def main():
    worker = Worker(Path(sys.argv[1]).resolve(), ROOT / "tests/support/worker_failure_fixture.metta",
                    "worker-failure:run", language="petta", profile=True)
    try:
        identity = worker.process.pid
        for command, count in [("Empty", 0), ("Many", 2), ("Error", 1)]:
            try:
                worker.call(S(command))
            except WorkerEvaluationError as error:
                assert error.completion == S("complete") and error.answer_count == count, error
                assert error.raised_error is False, error
                if command == "Error":
                    assert error.diagnostic[0] == S("Error"), error
                else:
                    assert error.diagnostic == (), error
            else:
                raise AssertionError((command, "failure silently treated as success"))
            assert worker.call(S("Echo")) == S("Echo")
        try:
            worker.call(S("Opaque"))
        except WorkerEvaluationError as error:
            assert error.detail == S("unencodable-result"), error
        else:
            raise AssertionError("an opaque Space escaped the portable protocol")
        assert worker.call(S("Echo")) == S("Echo")
        # A durable commit before an empty frontier is neither rollback nor a
        # transport failure. The worker stays alive and does not replay it.
        with tempfile.TemporaryDirectory(prefix="worker-effect-feedback-") as directory:
            try:
                worker.call(E("AfterEffect", directory))
            except WorkerEvaluationError as error:
                assert error.completion == S("complete") and error.answer_count == 0, error
            else:
                raise AssertionError("empty result after durable effect was silently successful")
            assert worker.call(E("InspectEffect", directory)) == E("WorkerDurableEffectV1", 1, "committed")
        assert worker.process.pid == identity
    finally:
        worker.close()
    # An invalid status is still a damaged protocol, not a correlated
    # application error. It must retire this process and its pending state.
    with tempfile.TemporaryDirectory(prefix="worker-bad-status-") as directory:
        fake = Path(directory) / "fake.py"
        fake.write_text("import sys,struct\n"
                        + "sys.path.insert(0," + repr(str(ROOT / "tools")) + ")\n"
                        + "from space_service_client import E,S,encode,decode\n"
                        + "header=sys.stdin.buffer.read(8)\n"
                        + "request=decode(sys.stdin.buffer.read(struct.unpack('>I',header[4:])[0]))\n"
                        + "data=encode(E('reply',request[1],S('invalid-status'),S('ignored')))\n"
                        + "sys.stdout.buffer.write(b'CSP1'+struct.pack('>I',len(data))+data)\n"
                        + "sys.stdout.buffer.flush()\n")
        invalid = Worker(Path(sys.executable), fake, "unused")
        try:
            try:
                invalid.call(S("Echo"))
            except WorkerUnavailable:
                assert invalid.process is None
            else:
                raise AssertionError("invalid transport status was retained as an application outcome")
        finally:
            invalid.close()
    print("PASS: worker failure feedback preserves completion/count/diagnostic, stream state/effects; invalid transport retires")


if __name__ == "__main__":
    main()
