# Native tensors and Python interoperability

This document defines the library boundary and the first native CPU slice.
`lib_tensor` combines compact C storage and bulk loops with MeTTa composition.
It provides immutable contiguous `i64`, `f64` and `u8` tensors. Native
tensor/NumPy conversion remains future work; the existing Python interface can
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

The first native slice provides checked CPU allocation, homogeneous storage,
contiguous construction, shape inspection, copy, contiguous reshape,
elementwise addition/multiplication, sum and vector dot. Add general map,
strided views, broadcasting and matrix kernels against their own contracts.
Public APIs distinguish an incompatible
shape from an invalid physical buffer and from an unsupported dtype/device.

Default value operations should preserve their inputs. Shared mutation must be
explicit: a view retains its storage owner, exposes whether it is writable,
and records which aliases can observe updates. Dense empty arrays and rank-zero
values require dedicated cases; they must not dereference absent storage.

## Backtracking and value identity

The first slice exposes immutable values only. `tensor:with` returns a new
buffer; `tensor:reshape` shares immutable storage. Neither changes an input
seen by an alternative branch, space, saved continuation or another thread.
Rewinding bindings does not undo external buffer writes. A future reuse or
in-place optimization therefore needs an exclusive write capability, no live
aliases or pending readers, no saved alternative depending on the old value,
and no published value whose contents could change. A unique reference count
alone is insufficient. External writable arrays require copying or a separate
borrow contract; a read-only view does not prevent the exporter from writing.

A native tensor's structural equality and whole-value unification compare
dtype, exact shape and ordered element bits. They ignore storage addresses.
Identical NaN encodings are equal; distinct NaN encodings remain distinct;
positive and negative zero are distinct. `==` follows this tensor value
identity. Numerical tolerance, broadcasting and cross-dtype promotion are
separate operations, not implicit matching rules. Variables can bind a whole
tensor; inspect its shape or export entries to match its components.

Hashes include the same dtype, shape and bit content and remain stable because
published storage is immutable. The current native term store retains tensor
payloads through its existing fallback; compact term encoding and optimized
tensor keys remain separate work. Copies across arenas and parallel branches
retain their payload. Debug printing shows dtype, shape and count without an
address; it is not a reconstructible data format.
Use ordinary names for Rho channels. Rho's existing text-key channel semantics
do not provide content identity for tensors whose debug spelling omits entries.
Arena allocation-pressure accounting includes retained tensor buffers. The
first implementation charges each view conservatively; aliases can overcount
shared storage. This is an allocation charge, not an exact global live-byte
statistic or a memory quota.

`tensor:encode`/`tensor:decode` use the pointer-free CTN1 representation: magic,
dtype, rank, dimensions and ordered element bits in little-endian order. It
preserves NaN payloads and signed zero. Atom-wire and durable-value codecs carry
this encoding as an `A` leaf within their existing size limits. Older readers
do not understand this leaf; service clients must have tensor support before
exchanging it. They must not silently convert it to a printed symbol.

## Native API and composition

Import `lib_tensor` (`(import! &self (library lib_tensor))` in PeTTa,
`(import! &self lib_tensor)` in HE/Prime). Constructors take an explicit dtype, shape and flat
entry expression: `(tensor:from f64 (2 3) (1 2 3 4 5 6))`. Shape `()` is a
scalar, and a zero axis requires no entries. `tensor:full` fills a shape without
building one Atom per element. Explicit `f64` construction converts integer
inputs to binary64, which can round; integer constructors require integer
inputs, with `u8` restricted to 0–255.

The first operations are `shape`, `dtype`, `to-expr`, `reshape`, `copy`, `with`,
`add`, `mul`, `sum`, `dot`, `encode` and `decode`, under the `tensor:` namespace.
`with` uses a zero-based flat index. Elementwise operations require equal
dtype and shape; `dot` requires equal rank-one shapes. Integer arithmetic
refuses overflow. Ordered floating reductions use binary64 multiplication and
addition without FMA contraction; the CPU reference is not a guarantee of
cross-provider or cross-device numerical reproducibility. Refusals return
`(TensorFailure reason)` as data in every dialect.

Native calls process whole buffers. MeTTa defines norm, cosine and retrieval
composition; it does not loop through an Atom for each component of a bulk
kernel. `to-expr` is explicit materialization, never the normal return path.
Operation graphs, fusion, differentiation and optional BLAS/GPU providers
remain later layers. PyTorch supplies initial training; native differentiation
is a long-term target. Sharing numerical data alone does not share its gradient
graph. Seeded random generators and provider-specific deterministic modes must
have explicit contracts when introduced.

The first retrieval client is PeTTa's `lib_retrieval`. It reads records shaped
as `(Embedding id revision profile tensor metadata)` from a native space, then
uses `retrieval:search-dot` with a profile, query vector and count. It rejects
failed or nonfinite candidate scores rather than silently reporting complete
coverage over a successful subset. The selected hits retain IDs, revisions,
profiles, metadata and scores. Top-k preserves source occurrence order and
prefers earlier occurrences at a boundary tie; it does not rank-sort the
returned hits. Each query reads current source records, so deletion has no
derived-index lag. Concurrent revision capture, ANN indexes, persistence policy
and model adapters are separate work. PathMap/MORK compact tensor ingress is
not implemented in this slice; use the native space backend.

Before allocation or access, check dimension and byte-count arithmetic for
native integer overflow. Future strided views must also validate stride and
offset arithmetic against the owner's extent, including negative strides and
zero-length axes. Saved
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

`Mettapedia.Algebra.TensorOwnership` specifies exact bit identity, retained and
pinned ownership, checked functional updates, isolation of saved alternatives,
and the stronger conditions required for any future storage reuse. Its
mathematical byte bound covers scalar and zero-axis cases. These are checked
model properties, not a verification of the physical C reference counter,
allocator, codec or floating-point kernels.

Run `make test-tensor-native BUILD=core` for the native unit checks and the
PeTTa, HE and Prime API fixtures. The PeTTa fixtures also exercise exact
retrieval, profile selection and deletion. `examples/tensor/native_cpu.metta`
is a runnable PeTTa example.

The next slices are checked strided views and additional numeric kernels;
optional Python import/export; then retrieval indexes and model adapters.
Operation graphs and differentiation build above the value layer. The full
tensor library can grow through these boundaries without making Python a
runtime requirement.
