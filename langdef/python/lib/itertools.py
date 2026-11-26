# The itertools module, as CPython 3.15's Modules/itertoolsmodule.c defines
# it: each iterator a type of this module whose __next__ is the C object's
# state machine, its arguments read as Argument Clinic (or PyArg_ParseTuple*)
# reads them, its messages the C's.  Iterator objects cannot be pickled
# (CPython 3.14 removed that).
"""Functional tools for creating and using iterators."""

_clinic_args = _cetta_prelude("_clinic_args")
_NULL = _cetta_null()
_GenericAlias = type(list[int])
# an iterator's "nothing yet" (the C's NULL in its state); _NULL itself
# cannot be held in a slot, where it reads as no value
_UNSET = object()


def _index(x):
    # PyNumber_AsSsize_t, the 'n' format: an int, else __index__
    t = type(x)
    if t is int:
        return x
    f = getattr(t, "__index__", None)
    if f is None:
        raise TypeError("'" + t.__name__ + "' object cannot be interpreted as an integer")
    r = f(x)
    if not isinstance(r, int):
        raise TypeError("__index__ returned non-int (type " + type(r).__name__ + ")")
    return r


def _positional(name, args, lo, hi):
    # _PyArg_CheckPositional and PyArg_UnpackTuple
    n = len(args)
    if n < lo:
        raise TypeError(name + " expected " + ("" if lo == hi else "at least ") + str(lo)
                        + " argument" + ("" if lo == 1 else "s") + ", got " + str(n))
    if n > hi:
        raise TypeError(name + " expected " + ("" if lo == hi else "at most ") + str(hi)
                        + " argument" + ("" if hi == 1 else "s") + ", got " + str(n))


def _no_keywords(name, cls, base, kwargs):
    # _PyArg_NoKeywords, asked of the type itself and of a subclass that
    # keeps its __init__
    if kwargs and (cls is base or cls.__init__ is base.__init__):
        raise TypeError(name + "() takes no keyword arguments")


def _given(v, default):
    return default if v is _NULL else v


def _eq(a, b):
    # PyObject_RichCompareBool(a, b, Py_EQ): an object equals itself
    return a is b or bool(a == b)


class chain:
    """Iterate over each given iterable in turn."""
    __slots__ = ("_source", "_active")

    def __new__(cls, *args, **kwargs):
        _no_keywords("chain", cls, chain, kwargs)
        self = object.__new__(cls)
        self._source = iter(args)
        self._active = None
        return self

    @classmethod
    def from_iterable(cls, iterable, /):
        self = object.__new__(cls)
        self._source = iter(iterable)
        self._active = None
        return self

    __class_getitem__ = classmethod(_GenericAlias)

    def __iter__(self):
        return self

    def __next__(self):
        while self._source is not None:
            if self._active is None:
                try:
                    iterable = next(self._source)
                except BaseException:
                    self._source = None
                    raise
                try:
                    self._active = iter(iterable)
                except BaseException:
                    self._source = None
                    raise
            try:
                return next(self._active)
            except StopIteration:
                self._active = None
        raise StopIteration


class count:
    """Count up from start by step, without end."""
    __slots__ = ("_cnt", "_step")

    def __new__(cls, *args, **kwargs):
        start, step = _clinic_args("count", ("start", "step"), 0, 2, args, kwargs)
        start = _given(start, 0)
        step = _given(step, 1)
        for x in (start, step):
            t = type(x)
            if not (hasattr(t, "__index__") or hasattr(t, "__int__") or hasattr(t, "__float__")
                    or isinstance(x, complex)):
                raise TypeError("a number is required")
        self = object.__new__(cls)
        self._cnt = start
        self._step = step
        return self

    def __iter__(self):
        return self

    def __next__(self):
        r = self._cnt
        self._cnt = r + self._step
        return r

    def __repr__(self):
        name = type(self).__name__
        if isinstance(self._step, int) and self._step == 1:
            return name + "(" + repr(self._cnt) + ")"
        return name + "(" + repr(self._cnt) + ", " + repr(self._step) + ")"


class cycle:
    """Repeat the elements of an iterable without end."""
    __slots__ = ("_it", "_saved", "_index", "_firstpass")

    def __new__(cls, *args, **kwargs):
        _no_keywords("cycle", cls, cycle, kwargs)
        _positional("cycle", args, 1, 1)
        self = object.__new__(cls)
        self._it = iter(args[0])
        self._saved = []
        self._index = 0
        self._firstpass = False
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if self._it is not None:
            try:
                item = next(self._it)
            except StopIteration:
                self._it = None
            else:
                if not self._firstpass:
                    self._saved.append(item)
                return item
        if not self._saved:
            raise StopIteration
        item = self._saved[self._index]
        self._index += 1
        if self._index >= len(self._saved):
            self._index = 0
        return item


class repeat:
    """Give an object again and again, times times or without end."""
    __slots__ = ("_element", "_cnt")

    def __new__(cls, *args, **kwargs):
        element, times = _clinic_args("repeat", ("object", "times"), 1, 2, args, kwargs)
        self = object.__new__(cls)
        self._element = element
        if times is _NULL:
            self._cnt = -1
        else:
            n = _index(times)
            self._cnt = n if n >= 0 else 0
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if self._cnt == 0:
            raise StopIteration
        if self._cnt > 0:
            self._cnt -= 1
        return self._element

    def __length_hint__(self):
        if self._cnt == -1:
            raise TypeError("len() of unsized object")
        return self._cnt

    def __repr__(self):
        name = type(self).__name__
        if self._cnt == -1:
            return name + "(" + repr(self._element) + ")"
        return name + "(" + repr(self._element) + ", " + str(self._cnt) + ")"


class accumulate:
    """Running totals, or the results of a binary function."""
    __slots__ = ("_it", "_func", "_total", "_initial")

    def __new__(cls, *args, **kwargs):
        iterable, func, initial = _clinic_args("accumulate", ("iterable", "func", "initial"), 1, 2, args, kwargs)
        self = object.__new__(cls)
        self._it = iter(iterable)
        self._func = _given(func, None)
        self._total = _UNSET
        self._initial = _given(initial, None)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if self._initial is not None:
            self._total = self._initial
            self._initial = None
            return self._total
        val = next(self._it)
        if self._total is _UNSET:
            self._total = val
            return val
        total = self._total + val if self._func is None else self._func(self._total, val)
        self._total = total
        return total


class batched:
    """Tuples of n items taken in turn; the last may be shorter."""
    __slots__ = ("_it", "_n", "_strict")

    def __new__(cls, *args, **kwargs):
        iterable, n, strict = _clinic_args("batched", ("iterable", "n", "strict"), 2, 2, args, kwargs)
        n = _index(n)
        if n < 1:
            raise ValueError("n must be at least one")
        self = object.__new__(cls)
        self._it = iter(iterable)
        self._n = n
        self._strict = bool(_given(strict, False))
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if self._it is None:
            raise StopIteration
        items = []
        for _ in range(self._n):
            try:
                items.append(next(self._it))
            except StopIteration:
                break
            except BaseException:
                self._it = None
                raise
        if len(items) < self._n:
            if not items:
                self._it = None
                raise StopIteration
            if self._strict:
                self._it = None
                raise ValueError("batched(): incomplete batch")
        return tuple(items)


class compress:
    """The items of data whose selector is true."""
    __slots__ = ("_data", "_selectors")

    def __new__(cls, *args, **kwargs):
        data, selectors = _clinic_args("compress", ("data", "selectors"), 2, 2, args, kwargs)
        self = object.__new__(cls)
        self._data = iter(data)
        self._selectors = iter(selectors)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            datum = next(self._data)
            if next(self._selectors):
                return datum


class dropwhile:
    """Skip items while the predicate holds, then give the rest."""
    __slots__ = ("_func", "_it", "_start")

    def __new__(cls, *args, **kwargs):
        _no_keywords("dropwhile", cls, dropwhile, kwargs)
        _positional("dropwhile", args, 2, 2)
        self = object.__new__(cls)
        self._func = args[0]
        self._it = iter(args[1])
        self._start = False
        return self

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            item = next(self._it)
            if self._start:
                return item
            if not self._func(item):
                self._start = True
                return item


class takewhile:
    """Give items while the predicate holds."""
    __slots__ = ("_func", "_it", "_stop")

    def __new__(cls, *args, **kwargs):
        _no_keywords("takewhile", cls, takewhile, kwargs)
        _positional("takewhile", args, 2, 2)
        self = object.__new__(cls)
        self._func = args[0]
        self._it = iter(args[1])
        self._stop = False
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if self._stop:
            raise StopIteration
        item = next(self._it)
        if self._func(item):
            return item
        self._stop = True
        raise StopIteration


class filterfalse:
    """The items for which the function is false (or which are false)."""
    __slots__ = ("_func", "_it")

    def __new__(cls, *args, **kwargs):
        _no_keywords("filterfalse", cls, filterfalse, kwargs)
        _positional("filterfalse", args, 2, 2)
        self = object.__new__(cls)
        self._func = args[0]
        self._it = iter(args[1])
        return self

    def __iter__(self):
        return self

    def __next__(self):
        while True:
            item = next(self._it)
            ok = bool(item) if self._func is None or self._func is bool else self._func(item)
            if not ok:
                return item


class starmap:
    """The function applied to each item's elements as arguments."""
    __slots__ = ("_func", "_it")

    def __new__(cls, *args, **kwargs):
        _no_keywords("starmap", cls, starmap, kwargs)
        _positional("starmap", args, 2, 2)
        self = object.__new__(cls)
        self._func = args[0]
        self._it = iter(args[1])
        return self

    def __iter__(self):
        return self

    def __next__(self):
        args = next(self._it)
        if type(args) is not tuple:
            args = tuple(args)
        return self._func(*args)


class pairwise:
    """Successive overlapping pairs."""
    __slots__ = ("_it", "_old", "_result")

    def __new__(cls, *args, **kwargs):
        _no_keywords("pairwise", cls, pairwise, kwargs)
        _positional("pairwise", args, 1, 1)
        self = object.__new__(cls)
        self._it = iter(args[0])
        self._old = _UNSET
        return self

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        if it is None:
            raise StopIteration
        if self._old is _UNSET:
            try:
                self._old = next(it)
            except BaseException:
                self._it = None
                raise
        old = self._old
        try:
            new = next(it)
        except BaseException:
            self._it = None
            self._old = _UNSET
            raise
        self._old = new
        return (old, new)


class islice:
    """Selected items, as a slice of a sequence would select them."""
    __slots__ = ("_it", "_next", "_stop", "_step", "_cnt")

    def __new__(cls, *args, **kwargs):
        _no_keywords("islice", cls, islice, kwargs)
        _positional("islice", args, 2, 4)
        start, stop, step = 0, -1, 1
        if len(args) == 2:
            if args[1] is not None:
                stop = _islice_index(args[1])
                if stop == -1:
                    raise ValueError("Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.")
        else:
            if args[1] is not None:
                start = _islice_index(args[1])
            if args[2] is not None:
                stop = _islice_index(args[2])
                if stop == -1:
                    raise ValueError("Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.")
        if start < 0 or stop < -1:
            raise ValueError("Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.")
        if len(args) == 4 and args[3] is not None:
            step = _islice_index(args[3])
        if step < 1:
            raise ValueError("Step for islice() must be a positive integer or None.")
        self = object.__new__(cls)
        self._it = iter(args[0])
        self._next = start
        self._stop = stop
        self._step = step
        self._cnt = 0
        return self

    def __iter__(self):
        return self

    def __next__(self):
        it = self._it
        if it is None:
            raise StopIteration
        try:
            while self._cnt < self._next:
                next(it)
                self._cnt += 1
            if self._stop != -1 and self._cnt >= self._stop:
                raise StopIteration
            item = next(it)
        except BaseException:
            self._it = None
            raise
        self._cnt += 1
        self._next += self._step
        if self._stop != -1 and self._next > self._stop:
            self._next = self._stop
        return item


def _islice_index(x):
    # PyNumber_AsSsize_t with its error cleared: -1 for anything but an
    # index in range
    try:
        n = _index(x)
    except TypeError:
        return -1
    if n > 9223372036854775807 or n < -9223372036854775808:
        return -1
    return n


class _grouper:
    __slots__ = ("_parent", "_tgtkey")

    def __iter__(self):
        return self

    def __next__(self):
        gbo = self._parent
        if gbo._currgrouper is not self:
            raise StopIteration
        if gbo._currvalue is _UNSET:
            gbo._step()
        if not _eq(self._tgtkey, gbo._currkey):
            raise StopIteration
        r = gbo._currvalue
        gbo._currvalue = _UNSET
        gbo._currkey = _UNSET
        return r


class groupby:
    """Keys and groups of consecutive items with equal keys."""
    __slots__ = ("_it", "_keyfunc", "_tgtkey", "_currkey", "_currvalue", "_currgrouper")

    def __new__(cls, *args, **kwargs):
        iterable, key = _clinic_args("groupby", ("iterable", "key"), 1, 2, args, kwargs)
        self = object.__new__(cls)
        self._it = iter(iterable)
        self._keyfunc = _given(key, None)
        self._tgtkey = _UNSET
        self._currkey = _UNSET
        self._currvalue = _UNSET
        self._currgrouper = None
        return self

    def __iter__(self):
        return self

    def _step(self):
        value = next(self._it)
        key = value if self._keyfunc is None else self._keyfunc(value)
        self._currvalue = value
        self._currkey = key

    def __next__(self):
        self._currgrouper = None
        while True:
            if self._currkey is _UNSET:
                pass
            elif self._tgtkey is _UNSET:
                break
            elif not _eq(self._tgtkey, self._currkey):
                break
            self._step()
        self._tgtkey = self._currkey
        g = object.__new__(_grouper)
        g._parent = self
        g._tgtkey = self._tgtkey
        self._currgrouper = g
        return (self._currkey, g)


_LINKCELLS = 57


class _tee_dataobject:
    __slots__ = ("_it", "_values", "_nextlink", "_running")

    def _getitem(self, i):
        if i < len(self._values):
            return self._values[i]
        # the lead iterator: fetch more
        if self._running:
            raise RuntimeError("cannot re-enter the tee iterator")
        self._running = True
        try:
            value = next(self._it)
        finally:
            self._running = False
        self._values.append(value)
        return value

    def _jumplink(self):
        if self._nextlink is None:
            self._nextlink = _new_dataobject(self._it)
        return self._nextlink


def _new_dataobject(it):
    d = object.__new__(_tee_dataobject)
    d._it = it
    d._values = []
    d._nextlink = None
    d._running = False
    return d


class _tee:
    """An iterator that shares its input with its copies."""
    __slots__ = ("_dataobj", "_index")

    def __new__(cls, iterable, /):
        return _tee_fromiterable(iterable)

    def __iter__(self):
        return self

    def __next__(self):
        if self._index >= _LINKCELLS:
            self._dataobj = self._dataobj._jumplink()
            self._index = 0
        value = self._dataobj._getitem(self._index)
        self._index += 1
        return value

    def __copy__(self):
        t = object.__new__(_tee)
        t._dataobj = self._dataobj
        t._index = self._index
        return t


def _tee_fromiterable(iterable):
    it = iter(iterable)
    if type(it) is _tee:
        return it.__copy__()
    t = object.__new__(_tee)
    t._dataobj = _new_dataobject(it)
    t._index = 0
    return t


def tee(*args, **kwargs):
    """Give n independent iterators over one iterable."""
    if kwargs:
        raise TypeError("tee() takes no keyword arguments")
    _positional("tee", args, 1, 2)
    n = 2 if len(args) == 1 else _index(args[1])
    if n < 0:
        raise ValueError("n cannot be negative")
    if n == 0:
        return ()
    it = iter(args[0])
    copyfunc = getattr(it, "__copy__", None)
    first = it if copyfunc is not None else _tee_fromiterable(it)
    copyfunc = first.__copy__
    return (first,) + tuple(copyfunc() for _ in range(n - 1))


class zip_longest:
    """Tuples of the iterables' items, the shorter ones filled out."""
    __slots__ = ("_its", "_active", "_fillvalue")

    def __new__(cls, *args, **kwargs):
        fillvalue = None
        if kwargs:
            if len(kwargs) != 1 or "fillvalue" not in kwargs:
                raise TypeError("zip_longest() got an unexpected keyword argument")
            fillvalue = kwargs["fillvalue"]
        self = object.__new__(cls)
        self._its = [iter(a) for a in args]
        self._active = len(self._its)
        self._fillvalue = fillvalue
        return self

    def __iter__(self):
        return self

    def __next__(self):
        if not self._its or self._active == 0:
            raise StopIteration
        result = []
        for i, it in enumerate(self._its):
            if it is None:
                result.append(self._fillvalue)
                continue
            try:
                result.append(next(it))
            except StopIteration:
                self._active -= 1
                if self._active == 0:
                    raise
            except BaseException:
                self._active = 0
                raise
            else:
                continue
            self._its[i] = None
            result.append(self._fillvalue)
        return tuple(result)


class product:
    """The Cartesian product of the iterables, as nested loops give it."""
    __slots__ = ("_gen",)

    def __new__(cls, *args, **kwargs):
        repeat = 1
        if kwargs:
            repeat = _index(_given(_clinic_args("product", ("repeat",), 0, 0, (), kwargs)[0], 1))
            if repeat < 0:
                raise ValueError("repeat argument cannot be negative")
        pools = [tuple(a) for a in args] * repeat
        self = object.__new__(cls)
        self._gen = _product(pools)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return next(self._gen)


def _product(pools):
    n = len(pools)
    for p in pools:
        if not p:
            return
    indices = [0] * n
    yield tuple(p[0] for p in pools)
    while True:
        i = n - 1
        while i >= 0:
            indices[i] += 1
            if indices[i] < len(pools[i]):
                break
            indices[i] = 0
            i -= 1
        if i < 0:
            return
        yield tuple(pools[k][indices[k]] for k in range(n))


class combinations:
    """The r-length combinations of the iterable's items, in order."""
    __slots__ = ("_gen",)

    def __new__(cls, *args, **kwargs):
        iterable, r = _clinic_args("combinations", ("iterable", "r"), 2, 2, args, kwargs)
        pool = tuple(iterable)
        r = _index(r)
        if r < 0:
            raise ValueError("r cannot be negative")
        self = object.__new__(cls)
        self._gen = _combinations(pool, r)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return next(self._gen)


def _combinations(pool, r):
    n = len(pool)
    if r > n:
        return
    indices = list(range(r))
    yield tuple(pool[i] for i in indices)
    while True:
        i = r - 1
        while i >= 0 and indices[i] == i + n - r:
            i -= 1
        if i < 0:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple(pool[i] for i in indices)


class combinations_with_replacement:
    """The r-length combinations, items repeated."""
    __slots__ = ("_gen",)

    def __new__(cls, *args, **kwargs):
        iterable, r = _clinic_args("combinations_with_replacement", ("iterable", "r"), 2, 2, args, kwargs)
        pool = tuple(iterable)
        r = _index(r)
        if r < 0:
            raise ValueError("r must be non-negative")
        self = object.__new__(cls)
        self._gen = _combinations_with_replacement(pool, r)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return next(self._gen)


def _combinations_with_replacement(pool, r):
    n = len(pool)
    if not n and r:
        return
    indices = [0] * r
    yield tuple(pool[i] for i in indices)
    while True:
        i = r - 1
        while i >= 0 and indices[i] == n - 1:
            i -= 1
        if i < 0:
            return
        v = indices[i] + 1
        for j in range(i, r):
            indices[j] = v
        yield tuple(pool[i] for i in indices)


class permutations:
    """The r-length orderings of the iterable's items."""
    __slots__ = ("_gen",)

    def __new__(cls, *args, **kwargs):
        iterable, r = _clinic_args("permutations", ("iterable", "r"), 1, 2, args, kwargs)
        pool = tuple(iterable)
        n = len(pool)
        if r is _NULL or r is None:
            r = n
        else:
            r = _index(r)
            if r < 0:
                raise ValueError("r must be non-negative")
        self = object.__new__(cls)
        self._gen = _permutations(pool, r)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return next(self._gen)


def _permutations(pool, r):
    n = len(pool)
    if r > n:
        return
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    yield tuple(pool[i] for i in indices[:r])
    while n:
        i = r - 1
        while i >= 0:
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                break
            i -= 1
        else:
            return
