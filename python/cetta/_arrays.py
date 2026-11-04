"""Optional numerical conversion over the retained native buffer protocol."""
from . import _native


def _numpy():
    try:
        import numpy
    except ImportError as error:
        raise ImportError("NumPy conversion requires the cetta[numpy] extra") from error
    return numpy


def _immutable_owner(array, numpy):
    current = array
    visited = set()
    while current is not None and id(current) not in visited:
        visited.add(id(current))
        if isinstance(current, _native.Buffer) or type(current) is bytes:
            return current
        if isinstance(current, numpy.ndarray):
            current = current.base
        elif isinstance(current, memoryview):
            current = current.obj
        else:
            return None
    return None


def from_array(value, *, copy=None):
    if copy is not None and type(copy) is not bool:
        raise TypeError("copy must be True, False or None")
    numpy = _numpy()
    array = numpy.asarray(value)
    codes = {("i", 8): "i64", ("u", 8): "index", ("u", 1): "u8",
             ("f", 8): "f64", ("f", 4): "f32", ("b", 1): "bool"}
    dtype = codes.get((array.dtype.kind, array.dtype.itemsize))
    if dtype is None:
        raise BufferError(f"unsupported tensor dtype: {array.dtype}")
    byte_order = {"=": 0, "|": 0, "<": 1, ">": 2}[array.dtype.byteorder]
    if not array.size:
        return _native.buffer_from_bytes(b"", dtype, array.shape, array.strides, 0, False, byte_order)
    if copy is not True:
        owner = _immutable_owner(array, numpy)
        if owner is not None:
            address = array.__array_interface__["data"][0]
            try:
                if isinstance(owner, _native.Buffer):
                    return _native.buffer_at_address(owner, dtype, array.shape, array.strides, address, byte_order)
                return _native.buffer_from_bytes(owner, dtype, array.shape, array.strides, address, True, byte_order)
            except (_native.Error, BufferError) as error:
                if copy is False:
                    raise BufferError("array layout cannot be borrowed without a copy") from error
        if copy is False:
            raise BufferError("array backing is mutable or lacks a retained immutable owner")
    if dtype == "bool":
        array = array.astype(numpy.uint8)
    packed = array.tobytes(order="C")
    return _native.buffer_from_bytes(packed, dtype, array.shape, None, 0, False, byte_order)


def to_array(buffer, *, copy=None):
    if copy is not None and type(copy) is not bool:
        raise TypeError("copy must be True, False or None")
    numpy = _numpy()
    view = numpy.asarray(memoryview(buffer))
    return numpy.array(view, copy=True, order="K") if copy is True else view


def _copy_mode(copy):
    if copy is not None and type(copy) is not bool:
        raise TypeError("copy must be True, False or None")
    return 0 if copy is None else 1 if copy else 2


def _indices(value, copy):
    numpy = _numpy()
    array = numpy.asarray(value)
    if array.ndim == 1 and not array.size:
        array = array.astype(numpy.uint64)
    if array.ndim != 1 or array.dtype.kind not in "iu":
        raise ValueError("sparse indices must be an integer vector")
    if array.dtype.kind == "i" and numpy.any(array < 0):
        raise ValueError("sparse indices must be nonnegative")
    if array.dtype.itemsize != 8:
        if copy is False:
            raise BufferError("sparse index width requires a copy")
        array = array.astype(numpy.uint64)
    return from_array(array, copy=copy)


def sparse_from_arrays(engine, shape, values, *, coordinates=None, indptr=None,
                       indices=None, layout="coo", duplicates="reject", dtype=None, copy=None):
    mode = _copy_mode(copy)
    if layout not in ("coo", "csr", "csc"):
        raise ValueError("sparse layout must be coo, csr or csc")
    policies = {"reject": 0, "first": 1, "last": 2, "sum": 3}
    if duplicates not in policies:
        raise ValueError("duplicate policy must be reject, first, last or sum")
    numpy = _numpy()
    array = numpy.asarray(values)
    if dtype is not None and array.dtype != numpy.dtype(dtype):
        if copy is False:
            raise BufferError("changing sparse value dtype requires a copy")
        array = array.astype(dtype)
    if array.ndim != 1:
        raise ValueError("sparse values must be a vector")
    value_buffer = from_array(array, copy=copy)
    coordinate_buffer = None if coordinates is None else _indices(coordinates, copy)
    pointer_buffer = None if indptr is None else _indices(indptr, copy)
    index_buffer = None if indices is None else _indices(indices, copy)
    try:
        return _native.tensor_from_sparse(engine, tuple(shape), {"coo": 1, "csr": 2, "csc": 3}[layout],
            value_buffer, coordinate_buffer, pointer_buffer, index_buffer, policies[duplicates], mode)
    except _native.Error as error:
        if error.status == "buffer-incompatible":
            raise BufferError("sparse layout requires a data copy or supported component types") from error
        raise


class SparseArray:
    """Retained sparse component views, including explicitly stored zeros."""
    __slots__ = ("_shape", "_layout", "_buffers", "_arrays")

    def __new__(cls, *args, **kwargs):
        raise TypeError("sparse arrays are obtained from Atom.to_sparse()")

    @classmethod
    def _from_export(cls, exported, *, copy=None):
        _copy_mode(copy)
        layout, shape, *buffers = exported
        obj = object.__new__(cls)
        obj._shape, obj._layout = shape, layout
        obj._buffers = tuple(buffers)
        obj._arrays = tuple(None if b is None else to_array(b, copy=copy) for b in buffers)
        if copy is True:
            # Writable host copies must reenter through stable-value conversion.
            obj._buffers = None
        return obj

    @property
    def shape(self): return self._shape
    @property
    def layout(self): return self._layout
    @property
    def values(self): return self._arrays[0]
    @property
    def coordinates(self): return self._arrays[1]
    @property
    def indptr(self): return self._arrays[2]
    @property
    def indices(self): return self._arrays[3]
    @property
    def dtype(self): return self.values.dtype
    @property
    def nnz(self): return self.values.size

    def __repr__(self):
        return f"SparseArray(shape={self.shape}, dtype={self.dtype}, layout={self.layout!r}, nnz={self.nnz})"

    def __array__(self, dtype=None, copy=None):
        raise TypeError("sparse arrays require explicit to_numpy(fill=...) conversion")

    def _atom(self, engine, copy):
        mode = _copy_mode(copy)
        if self._buffers is not None:
            return _native.tensor_from_sparse(engine, self.shape, {"coo": 1, "csr": 2, "csc": 3}[self.layout],
                *self._buffers, 0, mode)
        return sparse_from_arrays(engine, self.shape, self.values, coordinates=self.coordinates,
            indptr=self.indptr, indices=self.indices, layout=self.layout, copy=copy)

    def to_numpy(self, *, fill):
        """Explicitly fill absent coordinates to obtain a dense NumPy array."""
        numpy = _numpy()
        result = numpy.full(self.shape, fill, dtype=self.dtype)
        flat = result.reshape(-1)
        if self.layout == "coo":
            flat[self.coordinates] = self.values
        else:
            for major in range(len(self.indptr) - 1):
                first, last = int(self.indptr[major]), int(self.indptr[major + 1])
                minor = self.indices[first:last]
                keys = major * self.shape[1] + minor if self.layout == "csr" else minor * self.shape[1] + major
                flat[keys] = self.values[first:last]
        return result
