import gc
import importlib.util
import unittest
import weakref
from concurrent.futures import ThreadPoolExecutor

from cetta import Buffer, Callback, Engine

HAS_NUMPY = importlib.util.find_spec("numpy") is not None
if HAS_NUMPY:
    import numpy as np


@unittest.skipUnless(HAS_NUMPY, "NumPy is an optional extra")
class NumPyTests(unittest.TestCase):
    def test_native_dtype_shape_and_values(self):
        with Engine() as engine:
            for dtype in (np.int64, np.uint64, np.uint8, np.float64, np.float32, np.bool_):
                with self.subTest(dtype=dtype):
                    source = np.asarray([[0, 1], [1, 0]], dtype=dtype)
                    atom = engine.tensor(source)
                    result = atom.to_numpy()
                    self.assertEqual(result.dtype, source.dtype)
                    self.assertEqual(result.shape, source.shape)
                    np.testing.assert_array_equal(result, source)
                    self.assertFalse(result.flags.writeable)

    def test_foreign_byte_order_preserves_views_values_and_mutable_copies(self):
        order = ">" if np.little_endian else "<"
        with Engine() as engine:
            engine.import_("(library tensor)")
            for code in ("i8", "u8", "f4", "f8"):
                dtype = np.dtype(order + code)
                source = np.asarray([[0, 1, 2], [3, 4, 5]], dtype=dtype)
                with self.subTest(dtype=dtype):
                    with self.assertRaises(BufferError):
                        engine.tensor(source, copy=False)
                    copied = engine.tensor(source)
                    source[0, 0] = 99
                    self.assertEqual(copied.to_numpy().dtype, dtype)
                    self.assertEqual(copied.to_numpy()[0, 0], 0)
                    backing = np.arange(12, dtype=dtype).tobytes()
                    immutable = np.frombuffer(backing, dtype=dtype).reshape(3, 4)[::-1, ::2]
                    atom = engine.tensor(immutable, copy=False)
                    result = atom.to_numpy(copy=False)
                    self.assertTrue(np.shares_memory(immutable, result))
                    self.assertEqual(result.dtype, dtype)
                    self.assertEqual(result.strides, immutable.strides)
                    np.testing.assert_array_equal(result, immutable)
                    self.assertEqual(engine.call("tensor:sum", atom).to_python(), [30])
                    native = engine.tensor(immutable.astype(dtype.newbyteorder("=")))
                    self.assertEqual(atom, native)
                    writable = atom.to_numpy(copy=True)
                    self.assertEqual(writable.dtype, dtype)
                    self.assertTrue(writable.flags.writeable)
                    self.assertFalse(np.shares_memory(result, writable))
        del atom, backing, immutable
        gc.collect()
        np.testing.assert_array_equal(result, [[8, 10], [4, 6], [0, 2]])

    def test_foreign_bits_and_reinterpretation_are_not_numeric_casts(self):
        order = ">" if np.little_endian else "<"
        words = [0x3FF0000000000000, 0x8000000000000000, 0x7FF8000000001234]
        foreign = np.frombuffer(np.asarray(words, dtype=order + "u8").tobytes(), dtype=order + "f8")
        with Engine() as engine:
            atom = engine.tensor(foreign, copy=False)
            self.assertEqual(atom.to_numpy().tobytes(), foreign.tobytes())
            native = engine.tensor(np.asarray(words, dtype=np.uint64).view(np.float64))
            self.assertEqual(atom, native)
            for base in (atom.to_numpy(), native.to_numpy()):
                reinterpreted = base.view(base.dtype.newbyteorder())
                result = engine.tensor(reinterpreted, copy=False).to_numpy(copy=False)
                self.assertEqual(result.dtype, reinterpreted.dtype)
                self.assertEqual(result.tobytes(), reinterpreted.tobytes())
                self.assertTrue(np.shares_memory(base, result))
                np.testing.assert_array_equal(result, reinterpreted)

    def test_foreign_operands_use_native_tensor_operations(self):
        order = ">" if np.little_endian else "<"
        with Engine() as engine:
            engine.import_("(library tensor)")
            for code in ("f4", "f8"):
                matrix = np.asarray([[1, 2, 4], [8, 16, 32]], dtype=order + code)
                query = np.asarray([1, 2, 4], dtype=order + code)
                for foreign_matrix, foreign_query in ((True, True), (True, False), (False, True)):
                    with self.subTest(code=code, matrix=foreign_matrix, query=foreign_query):
                        m = matrix if foreign_matrix else matrix.astype(matrix.dtype.newbyteorder("="))
                        q = query if foreign_query else query.astype(query.dtype.newbyteorder("="))
                        answer = engine.call("tensor:matvec", engine.tensor(m), engine.tensor(q))[0]
                        np.testing.assert_array_equal(answer.to_numpy(), [21, 168])
                ids = engine.tensor(np.asarray([2, 0], dtype=order + "u8"))
                gathered = engine.call("tensor:gather", engine.tensor(query), ids)[0]
                np.testing.assert_array_equal(gathered.to_numpy(), [4, 1])
            engine.register_callback("host:reverse-array", Callback(lambda value: value[::-1], 1))
            source = np.frombuffer(np.asarray([1, 2, 3], dtype=order + "i8").tobytes(), dtype=order + "i8")
            answer = engine.call("host:reverse-array", engine.tensor(source, copy=False))[0]
            self.assertEqual(answer.to_numpy().dtype, source.dtype)
            self.assertTrue(np.shares_memory(source, answer.to_numpy()))
            np.testing.assert_array_equal(answer.to_numpy(), [3, 2, 1])

    def test_foreign_scalar_empty_zero_stride_and_shared_engines(self):
        order = ">" if np.little_endian else "<"
        dtype = np.dtype(order + "i8")
        with Engine() as left, Engine() as right:
            scalar = np.frombuffer(np.asarray(7, dtype=dtype).tobytes(), dtype=dtype).reshape(())
            result = left.tensor(scalar, copy=False).to_numpy(copy=False)
            self.assertEqual(result.shape, ())
            self.assertEqual(result.dtype, dtype)
            self.assertEqual(result.item(), 7)
            empty = np.frombuffer(b"", dtype=dtype).reshape(2, 0, 3)
            result = left.tensor(empty, copy=False).to_numpy(copy=False)
            self.assertEqual(result.shape, empty.shape)
            self.assertEqual(result.dtype, dtype)
            broadcast = np.broadcast_to(scalar, (4,))
            atom = left.tensor(broadcast, copy=False)
            other = right.atom(atom.buffer)
            self.assertEqual(other.to_numpy().strides, (0,))
            self.assertTrue(np.shares_memory(atom.to_numpy(), other.to_numpy()))
            left.import_("(library tensor)")
            right.import_("(library tensor)")
            with ThreadPoolExecutor(max_workers=2) as pool:
                a = pool.submit(left.call, "tensor:sum", atom)
                b = pool.submit(right.call, "tensor:sum", other)
                self.assertEqual(a.result(timeout=10).to_python(), [28])
                self.assertEqual(b.result(timeout=10).to_python(), [28])
            left.close()
            self.assertEqual(right.call("tensor:sum", other).to_python(), [28])

    def test_mutable_input_copies_and_never_copy_refuses(self):
        source = np.arange(6, dtype=np.float64)
        with Engine() as engine:
            result = engine.tensor(source).to_numpy()
            source[0] = 100
            self.assertEqual(result[0], 0)
            self.assertFalse(np.shares_memory(source, result))
            with self.assertRaises(BufferError):
                engine.tensor(source, copy=False)
            source.flags.writeable = False
            with self.assertRaises(BufferError):
                engine.tensor(source, copy=False)

    def test_immutable_bytes_are_borrowed_and_retained(self):
        backing = np.arange(10, dtype=np.int64).tobytes()
        source = np.frombuffer(backing, dtype=np.int64)[8:1:-2]
        with Engine() as engine:
            atom = engine.tensor(source, copy=False)
            result = atom.to_numpy(copy=False)
            self.assertTrue(np.shares_memory(source, result))
            self.assertEqual(result.strides, source.strides)
        del atom, source, backing
        gc.collect()
        np.testing.assert_array_equal(result, [8, 6, 4, 2])

    def test_native_views_reuse_storage(self):
        with Engine() as engine:
            initial = engine.tensor(np.arange(12, dtype=np.float64).reshape(3, 4))
            source = initial.to_numpy().T[::-1, ::2]
            result = engine.tensor(source, copy=False).to_numpy(copy=False)
            self.assertEqual(result.strides, source.strides)
            self.assertTrue(np.shares_memory(source, result))
            np.testing.assert_array_equal(result, source)
            with self.assertRaises(ValueError):
                result.flags.writeable = True

    def test_independent_writable_copy(self):
        with Engine() as engine:
            atom = engine.tensor(np.arange(3, dtype=np.int64))
            result = atom.to_numpy(copy=True)
            self.assertTrue(result.flags.writeable)
            self.assertFalse(np.shares_memory(atom.to_numpy(), result))
            result[0] = 99
            self.assertEqual(atom.to_numpy()[0], 0)

    def test_scalar_and_empty_buffers(self):
        with Engine() as engine:
            scalar = engine.tensor(np.asarray(7, dtype=np.int64)).to_numpy()
            self.assertEqual(scalar.shape, ())
            self.assertEqual(scalar.item(), 7)
            empty = engine.tensor(np.empty((2, 0, 3), dtype=np.float32)).to_numpy()
            self.assertEqual(empty.shape, (2, 0, 3))
            self.assertEqual(empty.nbytes, 0)

    def test_noncontiguous_mutable_input_has_correct_values(self):
        source = np.arange(30, dtype=np.float64).reshape(5, 6).T[::2, ::-1]
        with Engine() as engine:
            result = engine.tensor(source).to_numpy()
            np.testing.assert_array_equal(result, source)
            self.assertFalse(np.shares_memory(result, source))

    def test_numpy_callbacks_use_the_same_tensor(self):
        with Engine() as engine:
            engine.register_callback("host:double-array", Callback(lambda array: array * 2, 1))
            source = np.arange(6, dtype=np.float64)
            answer = engine.call("host:double-array", source)[0]
            np.testing.assert_array_equal(answer.to_numpy(), source * 2)
            engine.run("!(import! &self (library tensor))")
            self.assertEqual(engine.call("tensor:sum", answer).to_python(), [30.0])

    def test_shared_buffer_can_enter_independent_engines(self):
        with Engine() as left, Engine() as right:
            atom = left.tensor(np.arange(4, dtype=np.float64))
            buffer = atom.buffer
            self.assertIsInstance(buffer, Buffer)
            other = right.atom(buffer)
            self.assertTrue(np.shares_memory(atom.to_numpy(), other.to_numpy()))
        del atom, other, buffer
        gc.collect()

    def test_numeric_reinterpretation_does_not_copy(self):
        with Engine() as engine:
            source = engine.tensor(np.asarray([1.0, -0.0], dtype=np.float64)).to_numpy()
            bits = source.view(np.uint64)
            result = engine.tensor(bits, copy=False).to_numpy()
            self.assertTrue(np.shares_memory(source, result))
            np.testing.assert_array_equal(bits, result)
            source = engine.tensor(np.asarray([1, 2, 3], dtype=np.float32)).to_numpy()
            selected = source[:2].view(np.float64)
            result = engine.tensor(selected, copy=False).to_numpy()
            self.assertTrue(np.shares_memory(source, result))
            np.testing.assert_array_equal(selected, result)

    def test_buffer_protocol_rejects_writes_and_direct_construction(self):
        with self.assertRaises(TypeError):
            Buffer()
        with Engine() as engine:
            buffer = engine.tensor(np.arange(4, dtype=np.int64)).buffer
            view = memoryview(buffer)
            self.assertTrue(view.readonly)
            self.assertEqual(view.format, "q")
            self.assertEqual(view.tolist(), [0, 1, 2, 3])
            with self.assertRaises(TypeError):
                view[0] = 5

    def test_boolean_views_validate_newly_selected_cells(self):
        source = np.frombuffer(b"\x00\x02", dtype=np.bool_)[:1]
        with Engine() as engine:
            atom = engine.tensor(source, copy=False)
            np.testing.assert_array_equal(atom.to_numpy(), [False])
            engine.run("!(import! &self (library tensor))")
            answer = engine.call("tensor:view", atom, (1,), (1,), 1)
            self.assertEqual(str(answer[0]), "(Tensor:Fault invalid-value)")

    def test_live_numpy_view_does_not_keep_python_engine(self):
        engine = Engine()
        view = engine.tensor(np.arange(4, dtype=np.float64)).to_numpy()
        reference = weakref.ref(engine)
        del engine
        gc.collect()
        self.assertIsNone(reference())
        np.testing.assert_array_equal(view, [0, 1, 2, 3])

    def test_incompatible_layout_and_dtype_refuse_without_copy(self):
        with Engine() as engine:
            with self.assertRaises(BufferError):
                engine.tensor(np.arange(3, dtype=np.complex128))
            raw = b"x" + np.arange(3, dtype=np.int64).tobytes()
            unaligned = np.frombuffer(raw, dtype=np.int64, count=3, offset=1)
            with self.assertRaises(BufferError):
                engine.tensor(unaligned, copy=False)
            np.testing.assert_array_equal(engine.tensor(unaligned).to_numpy(), [0, 1, 2])


if __name__ == "__main__":
    unittest.main()
