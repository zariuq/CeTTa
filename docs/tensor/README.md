# Native tensors and Python interoperability

This document defines the initial library boundary. A native `lib_tensor` is
planned; this repository does not yet provide packed numerical tensors or
native tensor/NumPy conversion. The existing Python interface can already
retain and call NumPy and PyTorch objects as opaque foreign values.

## One numerical value model

A dense numerical tensor has an element type and a shape. Rank zero describes
a scalar, rank one a vector, rank two a matrix, and higher ranks a numerical
array. Shape `[]` has one element; a shape containing a zero dimension has no
elements. General heterogeneous MeTTa expressions remain term containers.

The implementation should separate the tensor view from its storage owner:

```
MeTTa programs
    |
lib_tensor: constructors, views, operations
    |
Tensor: dtype, shape, strides, offset, device, access mode
    |
Storage: allocation, byte extent, owner, release operation
    |
native CPU kernels / optional kernel providers

optional Python adapter <--> NumPy / PyTorch
lib_vector              --> tensor operations + retrieval/index operations
```

The native library must operate without Python. An optional BLAS or other
native kernel provider may accelerate an operation while preserving its
declared arithmetic and observation rules. Python interoperability is an
adapter; it does not supply the required CPU implementation.

The current `lib/petta/lib_vector.metta` is a list-based utility library with
dot product, norm and cosine operations. Keep its existing interface usable.
New packed vector operations should use the tensor substrate. Retrieval
indexes, embedding-model adapters and persistence are separate consumers of
that substrate.

## Operation boundaries

Use explicit operations for elementwise multiplication, dot product, matrix
multiplication and general contraction. A numerical container does not select
an arbitrary coefficient algebra. In particular, matrix multiplication is
ordered, and mathematical semiring associativity does not justify reordering
floating-point computation.

The first native slice should provide checked CPU allocation, homogeneous
storage, contiguous construction, shape inspection, copy, contiguous reshape,
map and reductions. Add strided views, broadcasting, dot and matrix kernels
against their own contracts. Public APIs should distinguish an incompatible
shape from an invalid physical buffer and from an unsupported dtype/device.

Default value operations should preserve their inputs. Shared mutation must be
explicit: a view retains its storage owner, exposes whether it is writable,
and records which aliases can observe updates. Dense empty arrays and rank-zero
values require dedicated cases; they must not dereference absent storage.

Before allocation or access, check dimension, stride and byte-offset arithmetic
for native integer overflow. Validate every addressed element against the
owner's byte extent, including negative strides and zero-length axes. Saved
views, callbacks and in-flight operations must retain their owner across arena
reset and collection. Integrate these obligations with the existing grounded
payload and foreign-release machinery.

## Python modules that work today

Python-enabled builds provide `py-atom` for qualified object/module lookup,
`py-dot` for attribute access, and ordinary application of resolved callables.
The following PeTTa expression creates a real NumPy array, keeps its Python
identity, and invokes a method:

```metta
!(let $a ((py-atom numpy.arange) 6)
   ((py-dot $a sum)))
```

For a module object, use `(py-atom numpy)`, followed by `py-dot`. Arbitrary
Python objects remain opaque foreign values. Passing one back into Python
retains the same object; it does not copy its numerical storage into CeTTa.
Registered HE Python modules use the existing module lifecycle and export-view
machinery. A Python package must be installed for the interpreter selected by
the build and available on its module search path.

Runnable PeTTa examples are in `examples/python/numpy_views.metta` and
`examples/python/torch_views.metta`; neither depends on the other package.

The current direct call path accepts positional arguments. A small ordinary
Python helper can supply keyword arguments without changing the existing
`py-call` dialect rules:

```python
def call_with_keywords(function, positional, pairs):
    return function(*positional, **dict(pairs))
```

Make that helper available as an importable module, then call its qualified
name through `py-atom`. This is useful immediately; a future optional
`lib_py` facade may package the convenience API and structured errors.
`examples/python/numpy_keywords.metta` demonstrates this with the supplied
`call_helpers.py`. From the repository root, run it with a Python-enabled
binary and `PYTHONPATH=examples/python`.

## Native interchange boundary

Follow the [Array API](https://data-apis.org/array-api/latest/) where its shape,
dtype, promotion and operation definitions apply. Specify CeTTa deviations
explicitly rather than treating NumPy and PyTorch behavior as interchangeable.

Use [DLPack](https://dmlc.github.io/dlpack/latest/python_spec.html) for the
planned dense tensor interchange. Import and export must state whether storage
is copied or shared, retain the producer owner, negotiate supported versions
and devices, and consume each managed capsule once. A request prohibiting a
copy must fail if sharing is impossible. GPU synchronization and autograd
participation require additional contracts; exporting numerical data does not
transport an arbitrary autograd graph.

The native core must neither depend on a Python object being alive implicitly
nor assume that an external view is writable. Start with explicit copying and
read-only CPU sharing, qualify release and alias behavior, then admit writable
sharing and device providers. Tensor wire/storage formats must contain portable
values and metadata, never process pointers or Python handles.

## Checked groundwork and remaining implementation

MeTTapedia's `Mettapedia.Algebra.TensorShape` models arbitrary-rank dense values
with checked construction, reshape, map and elementwise lifting. The model
preserves ordered entries, distinguishes shape/count/physical-length failures,
and covers rank-zero and empty values. The existing `FiniteCoordinateBuffer`
supplies the rank-two ordered matrix laws; connecting that representation to
the new arbitrary-rank buffer is the next proof bridge.

These establish value-model properties. Native allocation and buffer bounds,
grounded payload ownership, dtype/promotion semantics, strided views, numerical
kernels and DLPack adapters remain implementation and qualification work.

Build the next slices in that order: native storage and owner contracts;
checked view/bounds and numeric kernels; optional Python import/export; then
retrieval indexes and model adapters. The full tensor library can grow through
these boundaries without making Python a runtime requirement.
