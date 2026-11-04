import gc
import importlib.util
import unittest
from concurrent.futures import ThreadPoolExecutor

from cetta import Callback, CettaError, Engine, SparseArray, Symbol

HAS_NUMPY = importlib.util.find_spec("numpy") is not None
if HAS_NUMPY:
    import numpy as np


@unittest.skipUnless(HAS_NUMPY, "NumPy is an optional extra")
class SparseTests(unittest.TestCase):
    def immutable(self, values, dtype):
        return np.frombuffer(np.asarray(values, dtype=dtype).tobytes(), dtype=dtype)

    def test_coo_preserves_absence_stored_zero_and_readonly_components(self):
        with Engine() as engine:
            atom = engine.tensor_from_coo((2, 3), [0, 4], [0.0, 7.0])
            sparse = atom.to_sparse()
            self.assertIsInstance(sparse, SparseArray)
            self.assertEqual((sparse.shape, sparse.layout, sparse.nnz), ((2, 3), "coo", 2))
            np.testing.assert_array_equal(sparse.coordinates, [0, 4])
            np.testing.assert_array_equal(sparse.values, [0.0, 7.0])
            np.testing.assert_array_equal(sparse.to_numpy(fill=-1), [[0, -1, -1], [-1, 7, -1]])
            self.assertFalse(sparse.values.flags.writeable)
            with self.assertRaises(ValueError):
                sparse.values.flags.writeable = True
            with self.assertRaises(TypeError):
                atom.to_numpy()
            with self.assertRaises(TypeError):
                np.asarray(sparse)
            engine.import_("(library tensor)")
            self.assertEqual(engine.call("tensor:at", atom, (0, 0)).to_python(), [(Symbol("tensor:present"), 0.0)])
            self.assertEqual(engine.call("tensor:at", atom, (0, 1)).to_python(), [Symbol("tensor:absent")])
            self.assertIsInstance(atom.to_python(), SparseArray)
        np.testing.assert_array_equal(sparse.values, [0, 7])

    def test_all_layouts_share_immutable_foreign_values_and_indices(self):
        order = ">" if np.little_endian else "<"
        expected = [[0, -1, 5, -1], [-1, -1, -1, -1], [-1, 2, -1, 4]]
        for layout in ("coo", "csr", "csc"):
            with self.subTest(layout=layout), Engine() as engine:
                values = self.immutable([0, 5, 2, 4] if layout != "csc" else [0, 2, 5, 4], order + "f8")
                if layout == "coo":
                    indices = self.immutable([0, 2, 9, 11], np.int64)
                    atom = engine.tensor_from_coo((3, 4), indices, values, copy=False)
                else:
                    pointers = self.immutable([0, 2, 2, 4] if layout == "csr" else [0, 1, 2, 3, 4], np.uint64)
                    indices = self.immutable([0, 2, 1, 3] if layout == "csr" else [0, 2, 0, 2], np.int64)
                    constructor = engine.tensor_from_csr if layout == "csr" else engine.tensor_from_csc
                    atom = constructor((3, 4), pointers, indices, values, copy=False)
                sparse = atom.to_sparse(copy=False)
                self.assertEqual(sparse.layout, layout)
                self.assertEqual(sparse.dtype, values.dtype)
                self.assertTrue(np.shares_memory(sparse.values, values))
                self.assertTrue(np.shares_memory(sparse.coordinates if layout == "coo" else sparse.indices, indices))
                if layout != "coo":
                    self.assertTrue(np.shares_memory(sparse.indptr, pointers))
                np.testing.assert_array_equal(sparse.to_numpy(fill=-1), expected)
                self.assertEqual(engine.tensor(sparse, copy=False), atom)
                engine.import_("(library tensor)")
                self.assertEqual(engine.call("tensor:sum", atom).to_python(), [11.0])
                other_layout = "csr" if layout == "csc" else "csc"
                converted = engine.call("tensor:convert", atom, Symbol(other_layout))[0].to_sparse()
                self.assertEqual(converted.dtype, values.dtype)
                np.testing.assert_array_equal(converted.to_numpy(fill=-1), expected)
            del atom, values, indices
            gc.collect()
            np.testing.assert_array_equal(sparse.to_numpy(fill=-1), expected)

    def test_absence_does_not_create_floating_zero_operations(self):
        with Engine() as engine:
            engine.import_("(library tensor)")
            absent = engine.tensor_from_coo((1, 2), [0], [1.0])
            stored_zero = engine.tensor_from_coo((1, 2), [0, 1], [1.0, 0.0])
            query = engine.tensor(np.asarray([1.0, np.nan]))
            for layout in ("coo", "csr", "csc"):
                with self.subTest(layout=layout):
                    a = engine.call("tensor:convert", absent, Symbol(layout))[0].to_sparse()
                    z = engine.call("tensor:convert", stored_zero, Symbol(layout))[0].to_sparse()
                    a_result = engine.call("tensor:matvec", engine.tensor(a, copy=False), query)[0].to_numpy()
                    z_result = engine.call("tensor:matvec", engine.tensor(z, copy=False), query)[0].to_numpy()
                    self.assertEqual(a_result[0], 1.0)
                    self.assertTrue(np.isnan(z_result[0]))

    def test_mutable_inputs_are_copied_and_never_copy_refuses(self):
        coordinates = np.asarray([0, 2], dtype=np.int64)
        values = np.asarray([0, 9], dtype=np.int64)
        with Engine() as engine:
            self.assertEqual(engine.tensor_from_coo((4,), [], [], copy=False).to_sparse().nnz, 0)
            with self.assertRaises(BufferError):
                engine.tensor_from_coo((4,), coordinates, values, copy=False)
            atom = engine.tensor_from_coo((4,), coordinates, values)
            coordinates[0] = 3; values[1] = 123
            sparse = atom.to_sparse()
            np.testing.assert_array_equal(sparse.coordinates, [0, 2])
            np.testing.assert_array_equal(sparse.values, [0, 9])

    def test_sparse_dtypes_and_noncontiguous_value_components(self):
        with Engine() as engine:
            keys = self.immutable([0, 2], np.uint64)
            for dtype in (np.int64, np.uint64, np.uint8, np.float32, np.float64, np.bool_):
                with self.subTest(dtype=dtype):
                    values = self.immutable([0, 1], dtype)
                    sparse = engine.tensor_from_coo((4,), keys, values, copy=False).to_sparse()
                    self.assertEqual(sparse.dtype, values.dtype)
                    self.assertTrue(np.shares_memory(sparse.values, values))
                    np.testing.assert_array_equal(sparse.values, [0, 1])
            order = ">" if np.little_endian else "<"
            values = self.immutable([7, 99, 11, 99], order + "i8")[::2]
            with self.assertRaises(BufferError):
                engine.tensor_from_coo((4,), keys, values, copy=False)
            packed = engine.tensor_from_coo((4,), keys, values).to_sparse()
            self.assertEqual(packed.dtype, values.dtype)
            np.testing.assert_array_equal(packed.values, [7, 11])

    def test_duplicates_use_existing_policies_and_keep_cancelled_zero(self):
        order = ">" if np.little_endian else "<"
        with Engine() as engine:
            for policy, result in (("first", 3), ("last", -8), ("sum", 0)):
                with self.subTest(policy=policy):
                    sparse = engine.tensor_from_coo((2, 3), [4, 0, 4, 4],
                        np.asarray([3, 0, 5, -8], dtype=order + "f4"), duplicates=policy).to_sparse()
                    np.testing.assert_array_equal(sparse.coordinates, [0, 4])
                    np.testing.assert_array_equal(sparse.values, [0, result])
                    self.assertEqual(sparse.dtype, np.dtype(order + "f4"))
                    self.assertEqual(sparse.nnz, 2)
            with self.assertRaises(CettaError):
                engine.tensor_from_coo((2, 3), [4, 0, 4], [1.0, 2.0, 3.0])
            keys = self.immutable([3, 0], np.uint64)
            values = self.immutable([7, 0], np.int64)
            with self.assertRaises(BufferError):
                engine.tensor_from_coo((4,), keys, values, copy=False)
            sparse = engine.tensor_from_coo((4,), keys, values).to_sparse()
            np.testing.assert_array_equal(sparse.coordinates, [0, 3])
            compressed = engine.tensor_from_csr((1, 3), [0, 3], [2, 0, 2],
                np.asarray([3, 0, 5], dtype=order + "f4"), duplicates="sum").to_sparse()
            np.testing.assert_array_equal(compressed.indices, [0, 2])
            np.testing.assert_array_equal(compressed.values, [0, 8])
            rounded = engine.tensor_from_coo((1,), [0, 0, 0],
                np.asarray([2**25, 1, -(2**25)], dtype=order + "f4"), duplicates="sum").to_sparse()
            self.assertEqual((rounded.nnz, rounded.values[0]), (1, 0.0))

    def test_empty_scalar_and_huge_logical_shape_do_not_densify(self):
        with Engine() as engine:
            empty = engine.tensor_from_coo((1 << 40,), self.immutable([], np.uint64),
                self.immutable([], np.float64), copy=False).to_sparse()
            self.assertEqual((empty.shape, empty.nnz, empty.values.nbytes), ((1 << 40,), 0, 0))
            self.assertLess(len(repr(empty)), 140)
            scalar = engine.tensor_from_coo((), [0], [12]).to_sparse()
            self.assertEqual(scalar.to_numpy(fill=0).item(), 12)
            matrix = engine.tensor_from_csr((0, 3), [0], np.asarray([], dtype=np.int64),
                np.asarray([], dtype=np.float32)).to_sparse()
            self.assertEqual(matrix.to_numpy(fill=0).shape, (0, 3))

    def test_malformed_compressed_metadata_and_negative_keys_fail(self):
        with Engine() as engine:
            for copy in (None, True):
                for pointers, indices in (([1, 1, 2], [0, 1]), ([0, 2, 1], [0, 1]),
                                          ([0, 1, 3], [0, 1]), ([0, 1, 2], [0, 3])):
                    with self.subTest(copy=copy, pointers=pointers, indices=indices), self.assertRaises(CettaError):
                        engine.tensor_from_csr((2, 3), pointers, indices, [1.0, 2.0], copy=copy)
            with self.assertRaises(ValueError):
                engine.tensor_from_coo((3,), [-1], [1.0])
            with self.assertRaises(CettaError):
                engine.tensor_from_coo((3,), [3], [1.0])
            with self.assertRaises(CettaError):
                engine.tensor_from_coo((3,), [0, 1], [1.0])
            with self.assertRaises(OverflowError):
                engine.tensor_from_coo((-1,), [0], [1.0])
            self.assertEqual(engine.call("+", 20, 22).to_python(), [42])

    def test_copies_callbacks_and_independent_engines_preserve_sparse_data(self):
        with Engine() as left, Engine() as right:
            atom = left.tensor_from_coo((2, 3), [0, 4], [0.0, 7.0])
            original = atom.to_sparse()
            copied = atom.to_sparse(copy=True)
            self.assertTrue(copied.values.flags.writeable)
            self.assertFalse(np.shares_memory(copied.values, original.values))
            stabilized = right.tensor(copied)
            copied.values[1] = 99
            np.testing.assert_array_equal(stabilized.to_sparse().values, [0, 7])
            left.register_callback("host:sparse", Callback(lambda value: value, 1))
            self.assertEqual(left.call("host:sparse", atom)[0], atom)
            other = right.tensor(original, copy=False)
            left.import_("(library tensor)"); right.import_("(library tensor)")
            with ThreadPoolExecutor(max_workers=2) as pool:
                a = pool.submit(left.call, "tensor:sum", atom)
                b = pool.submit(right.call, "tensor:sum", other)
                self.assertEqual(a.result(timeout=10).to_python(), [7.0])
                self.assertEqual(b.result(timeout=10).to_python(), [7.0])
            left.close()
            self.assertEqual(right.call("tensor:sum", other).to_python(), [7.0])


if __name__ == "__main__":
    unittest.main()
