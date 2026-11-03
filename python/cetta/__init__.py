"""CeTTa's native engine, structured values and retained answer bags."""
from enum import IntEnum
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from collections.abc import Mapping
import weakref
import sys
import os

from . import _native


def _note_cleanup(error, message):
    try:
        BaseException.add_note(error, message)
    except BaseException:
        pass


class AtomKind(IntEnum):
    SYMBOL = 0
    VARIABLE = 1
    EXPRESSION = 2
    INTEGER = 3
    FLOAT = 4
    BOOLEAN = 5
    STRING = 6
    BIG_INTEGER = 7
    RATIONAL = 8
    RESOURCE = 9


@dataclass(frozen=True, slots=True)
class Symbol:
    """A symbolic name, distinct from an ordinary Python string."""
    name: str

    def __post_init__(self):
        if not isinstance(self.name, str):
            raise TypeError("a symbol's name must be a string")

    def __str__(self):
        return self.name


@dataclass(frozen=True)
class Callback:
    """A host function's arity, multiplicity and argument conversion."""
    function: object
    arity: int
    generator: bool = False
    unpack: bool = True

    def __post_init__(self):
        if not callable(self.function):
            raise TypeError("callback function must be callable")
        if type(self.arity) is not int or self.arity < 0:
            raise ValueError("callback arity must be a non-negative integer")


class _CallbackRegistration:
    def __init__(self, engine, descriptor):
        self.engine = weakref.ref(engine)
        self.descriptor = descriptor

    def _open(self, handles):
        engine = self.engine()
        if engine is None or engine._closed:
            raise RuntimeError("callback engine is closed")
        invocation = _CallbackInvocation(engine, self.descriptor, tuple(Atom(h) for h in handles))
        engine._invocations[id(invocation)] = invocation
        return invocation


class _CallbackInvocation:
    def __init__(self, engine, descriptor, arguments):
        self.engine = engine
        self.descriptor = descriptor
        self.arguments = arguments
        self.iterator = None
        self.started = False
        self.done = False
        self.closed = False
        self.running = False
        self.close_requested = False

    def _pull(self):
        if self.closed or self.done:
            return 1, None
        self.running = True
        failure = None
        try:
            if not self.started:
                self.started = True
                arguments = tuple(a.to_python() for a in self.arguments) if self.descriptor.unpack else self.arguments
                value = self.descriptor.function(*arguments)
                if self.descriptor.generator:
                    self.iterator = iter(value)
                else:
                    self.done = True
                    return 0, self.engine.atom(value)._handle
            try:
                value = next(self.iterator)
            except StopIteration:
                self.done = True
                return 1, None
            return 0, self.engine.atom(value)._handle
        except BaseException as error:
            failure = error
            raise
        finally:
            self.running = False
            if self.close_requested:
                try:
                    self.close()
                except BaseException as cleanup:
                    if failure is None:
                        raise
                    _note_cleanup(failure, "Deferred callback close also failed.")

    def close(self):
        if self.running:
            self.close_requested = True
            return
        if self.closed:
            return
        self.closed = True
        try:
            close = getattr(self.iterator, "close", None)
            if close is not None:
                close()
        finally:
            self.engine._invocations.pop(id(self), None)


CettaError = _native.Error
Buffer = _native.Buffer
from ._arrays import SparseArray


class EvaluationError(CettaError):
    def __init__(self, status, answers):
        super().__init__(f"CeTTa: {status}")
        self.status = status
        self.answers = answers


class Answers(list):
    """An answer bag; duplicate entries and directive groups are preserved."""

    def __init__(self, values, sizes):
        super().__init__(values)
        self._sizes = tuple(sizes)

    @property
    def groups(self):
        groups = []
        start = 0
        for size in self._sizes:
            groups.append(self[start:start + size])
            start += size
        return groups

    def to_python(self):
        return [value.to_python() for value in self]

    def __repr__(self):
        shown = ", ".join(repr(value) for value in self[:8])
        suffix = f", … ({len(self)} answers)" if len(self) > 8 else ""
        return f"[{shown}{suffix}]"


class Atom:
    __slots__ = ("_handle", "_bindings", "_delayed")

    def __init__(self, handle):
        self._handle = handle
        self._bindings = None
        self._delayed = None

    @property
    def bindings(self):
        return self._bindings if self._bindings is not None else Bindings(())

    @property
    def delayed(self):
        return self._delayed

    def __str__(self):
        return _native.format(self._handle)

    def __repr__(self):
        preview = self._preview()
        if len(preview) > 512:
            preview = preview[:509] + "…"
        return f"Atom({preview!r})"

    def _preview(self, depth=0):
        kind = self.kind
        if kind == AtomKind.EXPRESSION:
            count = _native.arity(self._handle)
            if depth >= 2:
                return f"(… {count} children)"
            shown = [Atom(_native.child(self._handle, index))._preview(depth + 1)
                     for index in range(min(count, 4))]
            if count > 4:
                shown.append(f"… {count} children")
            return "(" + " ".join(shown) + ")"
        if kind in (AtomKind.RESOURCE, AtomKind.BIG_INTEGER):
            return f"<{kind.name.lower()}>"
        if kind in (AtomKind.STRING, AtomKind.SYMBOL, AtomKind.VARIABLE, AtomKind.RATIONAL):
            size = _native.text_length(self._handle)
            if size > 160:
                return f"<{kind.name.lower()}: {size} bytes>"
        return str(self)

    def __eq__(self, other):
        if not isinstance(other, Atom):
            return NotImplemented
        return _native.equal(self._handle, other._handle)

    __hash__ = None

    @property
    def kind(self):
        return AtomKind(_native.kind(self._handle))

    @property
    def children(self):
        kind, value = _native.inspect(self._handle)
        if kind != AtomKind.EXPRESSION:
            raise TypeError("only expressions have children")
        return tuple(Atom(child) for child in value)

    def to_python(self):
        kind, value = _native.inspect(self._handle)
        if kind == AtomKind.SYMBOL:
            return Symbol(value)
        if kind == AtomKind.EXPRESSION:
            return tuple(Atom(child).to_python() for child in value)
        if kind == AtomKind.BIG_INTEGER:
            return int(value)
        if kind == AtomKind.RATIONAL:
            return Fraction(value)
        if kind == AtomKind.RESOURCE:
            buffer = _native.atom_buffer(self._handle)
            if buffer is not None:
                from ._arrays import to_array
                return to_array(buffer)
            sparse = _native.sparse_export(self._handle)
            if sparse is not None:
                return SparseArray._from_export(sparse)
            return self
        if kind == AtomKind.VARIABLE:
            return self
        return value

    @property
    def buffer(self):
        value = _native.atom_buffer(self._handle)
        if value is None:
            raise TypeError("the atom does not hold a dense tensor")
        return value

    def to_numpy(self, *, copy=None):
        from ._arrays import to_array
        return to_array(self.buffer, copy=copy)

    def to_sparse(self, *, copy=None):
        exported = _native.sparse_export(self._handle)
        if exported is None:
            raise TypeError("the atom does not hold a sparse tensor")
        return SparseArray._from_export(exported, copy=copy)

    def as_bool(self):
        """Convert a Boolean or PeTTa's true/false symbols explicitly."""
        kind, value = _native.inspect(self._handle)
        if kind == AtomKind.BOOLEAN:
            return value
        if kind == AtomKind.SYMBOL and value in ("true", "false"):
            return value == "true"
        raise TypeError("the atom is not a Boolean or a true/false symbol")


class Engine:
    def __init__(self, *, language="petta", profile=None, resource_root=None):
        self._closed = True
        self._close_native = _native.close
        self._callbacks = []
        self._invocations = {}
        if resource_root is None:
            bundled = Path(__file__).with_name("resources")
            if (bundled / "lib").is_dir():
                resource_root = str(bundled)
            else:
                checkout = Path(__file__).resolve().parents[2]
                if (checkout / "lib").is_dir():
                    resource_root = str(checkout)
        self._handle = _native.engine(language, profile, resource_root)
        self._closed = False

    def close(self):
        if not self._closed:
            self._closed = True
            failure = None
            try:
                self._close_native(self._handle)
            except BaseException as error:
                failure = error
            for invocation in list(self._invocations.values()):
                try:
                    invocation.close()
                except BaseException as error:
                    if failure is None:
                        failure = error
                    else:
                        _note_cleanup(failure, "Another callback close failed while closing the engine.")
            self._callbacks.clear()
            if failure is not None:
                raise failure

    def __del__(self):
        if hasattr(self, "_closed"):
            self.close()

    def register_callback(self, name, descriptor):
        if not isinstance(descriptor, Callback):
            raise TypeError("expected a Callback descriptor")
        registration = _CallbackRegistration(self, descriptor)
        self._callbacks.append(registration)
        try:
            handle = _native.register_callback(self._handle, name, registration,
                                              descriptor.arity, descriptor.generator)
        except BaseException:
            self._callbacks.remove(registration)
            raise
        return Atom(handle)

    def __enter__(self):
        if self._closed:
            error = CettaError("engine-closed")
            error.status = "engine-closed"
            raise error
        return self

    def __exit__(self, *_):
        self.close()

    def parse(self, source):
        return Atom(_native.parse(self._handle, source))

    def symbol(self, name):
        return Atom(_native.scalar(self._handle, AtomKind.SYMBOL, name))

    def variable(self, name):
        return Atom(_native.scalar(self._handle, AtomKind.VARIABLE, name))

    def atom(self, value):
        if isinstance(value, SparseArray):
            return self.tensor(value)
        if isinstance(value, Space):
            value = value.as_atom()
        if isinstance(value, Atom):
            return value
        if isinstance(value, Symbol):
            return self.symbol(value.name)
        if isinstance(value, Buffer):
            return Atom(_native.buffer_atom(self._handle, value))
        numpy = sys.modules.get("numpy")
        if numpy is not None:
            if isinstance(value, numpy.ndarray):
                return self.tensor(value)
            if isinstance(value, numpy.generic):
                return self.atom(value.item())
        if isinstance(value, bool):
            kind = AtomKind.BOOLEAN
        elif isinstance(value, int):
            if value < -(2**63) or value >= 2**63:
                return Atom(_native.scalar(self._handle, AtomKind.BIG_INTEGER, str(value)))
            kind = AtomKind.INTEGER
        elif isinstance(value, Fraction):
            return Atom(_native.scalar(self._handle, AtomKind.RATIONAL,
                                       f"{value.numerator}/{value.denominator}"))
        elif isinstance(value, float):
            kind = AtomKind.FLOAT
        elif isinstance(value, str):
            kind = AtomKind.STRING
        elif isinstance(value, (tuple, list)):
            return self.expression(*value)
        else:
            raise TypeError(f"cannot convert {type(value).__name__} to a CeTTa atom")
        return Atom(_native.scalar(self._handle, kind, value))

    def tensor(self, value, *, copy=None):
        if isinstance(value, SparseArray):
            return Atom(value._atom(self._handle, copy))
        from ._arrays import from_array
        return Atom(_native.buffer_atom(self._handle, from_array(value, copy=copy)))

    def tensor_from_coo(self, shape, coordinates, values, *, duplicates="reject", dtype=None, copy=None):
        from ._arrays import sparse_from_arrays
        return Atom(sparse_from_arrays(self._handle, shape, values, coordinates=coordinates,
            duplicates=duplicates, dtype=dtype, copy=copy))

    def tensor_from_csr(self, shape, indptr, indices, values, *, duplicates="reject", dtype=None, copy=None):
        from ._arrays import sparse_from_arrays
        return Atom(sparse_from_arrays(self._handle, shape, values, indptr=indptr, indices=indices,
            layout="csr", duplicates=duplicates, dtype=dtype, copy=copy))

    def tensor_from_csc(self, shape, indptr, indices, values, *, duplicates="reject", dtype=None, copy=None):
        from ._arrays import sparse_from_arrays
        return Atom(sparse_from_arrays(self._handle, shape, values, indptr=indptr, indices=indices,
            layout="csc", duplicates=duplicates, dtype=dtype, copy=copy))

    def expression(self, *children):
        values = [self.atom(child) for child in children]
        return Atom(_native.expression(self._handle, [value._handle for value in values]))

    def _answers(self, status, values, sizes):
        answers = Answers((Atom(value) for value in values), sizes)
        if status != "ok":
            raise EvaluationError(status, answers)
        return answers

    def _execute(self, value, document, fuel):
        return self._answers(*_native.execute(self._handle, value, document, fuel))

    def run(self, source, *, fuel=-1):
        return self._execute(source, True, fuel)

    def load(self, path, *, fuel=-1):
        """Run a file; imports during the load resolve beside that file."""
        return self._answers(*_native.load(self._handle, path, fuel))

    def import_(self, spec, target=None, *, fuel=-1):
        """Invoke import! with a MeTTa module specification."""
        if isinstance(target, Space):
            target = target.as_atom()
        elif target is not None:
            target = self.parse(target) if isinstance(target, str) else self.atom(target)
        if isinstance(spec, os.PathLike):
            value = self.atom(os.fspath(spec))
        else:
            value = self.parse(spec) if isinstance(spec, str) else self.atom(spec)
        return self._answers(*_native.import_module(self._handle,
            None if target is None else target._handle, value._handle, fuel))

    def eval(self, expression, *, fuel=-1):
        value = self.parse(expression) if isinstance(expression, str) else self.atom(expression)
        return self._execute(value._handle, False, fuel)

    def call(self, name, *arguments, fuel=-1):
        head = self.symbol(name) if isinstance(name, str) else self.atom(name)
        values = [self.atom(value) for value in arguments]
        return self._answers(*_native.call(self._handle, head._handle,
                                           [value._handle for value in values], fuel))

    def query(self, expression, *, fuel=-1):
        value = self.parse(expression) if isinstance(expression, str) else self.atom(expression)
        return Query(self, _native.query_open(self._handle, value._handle, fuel))

    @property
    def space(self):
        return Space(self, _native.space(self._handle, True))

    def new_space(self):
        return Space(self, _native.space(self._handle, False))

    def space_from_atom(self, value):
        return Space(self, _native.space_from_atom(self._handle, self.atom(value)._handle))


class Space:
    """An engine-owned unordered collection of atoms."""
    def __init__(self, engine, handle):
        self._engine = engine
        self._handle = handle

    def as_atom(self):
        return Atom(_native.space_atom(self._handle))

    def __len__(self):
        return _native.space_count(self._handle)

    def add_atom(self, value):
        return _native.space_mutate(self._handle, self._engine.atom(value)._handle, False)

    def remove_atom(self, value):
        return _native.space_mutate(self._handle, self._engine.atom(value)._handle, True)

    def match(self, pattern, template=None, *, fuel=-1):
        if isinstance(pattern, str) and isinstance(template, str):
            parts = self._engine.parse(f"({pattern} {template})").children
            if len(parts) != 2:
                raise ValueError("pattern and template must each contain exactly one term")
            pattern, template = parts
        else:
            pattern = self._engine.parse(pattern) if isinstance(pattern, str) else self._engine.atom(pattern)
            if template is not None:
                template = self._engine.parse(template) if isinstance(template, str) else self._engine.atom(template)
        return Query(self._engine, _native.space_match(self._handle, pattern._handle,
                     None if template is None else template._handle, fuel))

    def get_atoms(self):
        return self.match(self._engine.variable("atom"))

    def __iter__(self):
        return self.get_atoms()


class Bindings(Mapping):
    """Variable/value bindings. Variable objects use scoped identity."""
    def __init__(self, pairs, constraints=()):
        self._pairs = tuple(pairs)
        self.constraints = tuple(constraints)

    def __len__(self):
        return len(self._pairs)

    def __iter__(self):
        return (key for key, _ in self._pairs)

    def __getitem__(self, key):
        matches = [value for variable, value in self._pairs
                   if (str(variable).lstrip("$") == key.lstrip("$") if isinstance(key, str)
                       else variable == key)]
        if len(matches) != 1:
            raise KeyError(key)
        return matches[0]

    def __repr__(self):
        shown = ", ".join(f"{key}: {value!r}" for key, value in self._pairs[:8])
        return "{" + shown + (", …" if len(self) > 8 else "") + "}"


@dataclass(frozen=True, slots=True)
class QueryStep:
    state: str
    answers: Answers
    physical_steps: int
    source_steps: int
    source_remaining: int | None


class Query:
    """An owned, closable answer stream. Iteration advances it automatically."""
    def __init__(self, engine, handle):
        self._engine = engine
        self._handle = handle
        self._closed = False
        self._finished = False

    def __iter__(self):
        return self

    def __next__(self):
        if self._closed or self._finished:
            raise StopIteration
        while True:
            try:
                step = self.step(4096)
            except StopIteration as error:
                raise RuntimeError("a callback raised StopIteration outside stream completion") from error
            if step.answers:
                return step.answers[0]
            if step.state == "complete":
                raise StopIteration

    def step(self, allowance=4096):
        if not isinstance(allowance, int) or not 0 <= allowance < 2**64:
            raise ValueError("allowance must be an unsigned 64-bit integer")
        try:
            result, state, physical, source, remaining, bindings, delayed, conditional = _native.query_step(
                self._handle, allowance)
            status, values, groups = result
            answers = Answers((Atom(value) for value in values), groups)
            if answers and bindings is not None:
                fields = Atom(bindings).children
                pairs = fields[1].children
                constraints = fields[2].children
                answers[0]._bindings = Bindings((tuple(pair.children) for pair in pairs),
                                               (tuple(pair.children) for pair in constraints))
                answers[0]._delayed = Atom(delayed) if delayed is not None else None
            if status != "ok":
                raise EvaluationError(status, answers)
            name = ("answer", "paused", "complete", "stopped")[state]
            self._finished = name in ("complete", "stopped")
            return QueryStep(name, answers, physical, source, remaining)
        except BaseException as error:
            try:
                self.cancel()
                self.close()
            except BaseException as cleanup:
                _note_cleanup(error, "Query cleanup also failed.")
            raise

    def cancel(self):
        if not self._closed:
            _native.query_cancel(self._handle)

    def close(self):
        if not self._closed:
            self._closed = True
            _native.query_close(self._handle)

    def __enter__(self):
        if self._closed:
            error = CettaError("query-closed")
            error.status = "query-closed"
            raise error
        return self

    def __exit__(self, *_):
        self.close()

    def batch(self, count):
        if not isinstance(count, int) or count < 0:
            raise ValueError("batch size must be a non-negative integer")
        return self._collect(count)

    def all(self):
        return self._collect(None)

    def _collect(self, count):
        values = []
        try:
            while count is None or len(values) < count:
                try:
                    values.append(next(self))
                except StopIteration:
                    break
        except BaseException as error:
            try:
                prefix = values + list(getattr(error, "answers", ()))
                error.answers = Answers(prefix, [len(prefix)])
            except BaseException:
                pass
            raise
        return Answers(values, [len(values)])

    def to_python(self):
        return self.all().to_python()


__all__ = ["Engine", "Space", "Atom", "AtomKind", "Answers", "Bindings", "Query", "QueryStep", "Symbol", "Callback", "Buffer", "CettaError", "EvaluationError"]
