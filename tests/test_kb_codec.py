"""Completed open syntax stays data across a native value boundary."""
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKER = os.environ.get("CETTA_VALUE_WORKER")
if WORKER:
    sys.path.insert(0, str(Path(os.environ.get("CETTA_CHANNEL_ROOT", ROOT)) / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, Var, encode, decode


@unittest.skipUnless(WORKER, "CETTA_VALUE_WORKER is required")
class CodecTests(unittest.TestCase):
    def setUp(self):
        self.worker = Worker(WORKER, ROOT / "tests/support/kb_codec_probe.metta", "kb-codec:run",
                             language="petta", import_mode="relative")
        self.addCleanup(self.worker.close)

    def test_open_callable_syntax_roundtrips_without_execution(self):
        x, y = Var(31), Var(32)
        for value in [x, E("+", x, x), E("+", x, y), E("=", E("f", x), E("+", x, x)),
                      E("data", E("*", x, 2)), E("Error", "source", "datum"), E("+", 2, 3),
                      E("NativeHandle", "disk-store", 42)]:
            with self.subTest(value=value):
                answer = self.worker.call(E("Roundtrip", value))
                self.assertEqual(answer[0], S("StoreDecoded"), answer)
                self.assertEqual(encode(answer[1]), encode(value))

    def test_computed_argument_runs_once_then_stays_inert(self):
        for value in [E("+", Var(5), Var(5)), E("+", 2, 3), E("term", "value")]:
            with self.subTest(value=value):
                result = self.worker.call(E("Produced", value))
                self.assertEqual(result[0], S("CodecObserved"), result)
                self.assertEqual(result[2], 1)
                self.assertEqual(result[1][0], S("StoreEncoded"))
                raw = result[1][1]
                decoded = decode(raw.encode("utf-8") if isinstance(raw, str) else raw)
                self.assertEqual(encode(decoded), encode(value))

    def test_transaction_preserves_completed_open_and_callable_data(self):
        for value in [E("+", Var(1), Var(1)), E("+", 2, 3), E("kb-codec:datum-call"),
                      E("Error", "source", "data")]:
            with self.subTest(value=value):
                result = self.worker.call(E("TransactionProduced", value))
                self.assertEqual(result[0], S("CodecEffects"))
                self.assertEqual(result[2], (S("produced"),))
                self.assertEqual(result[1][0], S("StoreEncoded"))
                body = result[1][1]
                self.assertEqual(encode(decode(body.encode() if isinstance(body, str) else body)), encode(value))

    def test_error_datum_sibling_does_not_reenter_completed_arguments(self):
        for right in ["x", E("Error", "source", "data")]:
            result = self.worker.call(E("ErrorSibling", E("kb-codec:datum-call"), right))
            self.assertEqual(result[2], (S("produced"), S("produced")))

    def test_nested_transactions_and_new_equations_keep_value_roles(self):
        for mode in ["NestedTransaction", "TransactionAdded"]:
            value = E("+", 2, 3)
            with self.subTest(mode=mode):
                result = self.worker.call(E(mode, value))
                self.assertEqual(result[0], S("StoreEncoded"))
                body = result[1]
                self.assertEqual(encode(decode(body.encode() if isinstance(body, str) else body)), encode(value))


if __name__ == "__main__":
    unittest.main()
