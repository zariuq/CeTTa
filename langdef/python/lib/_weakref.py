# The _weakref module, as CPython 3.15's Modules/_weakref.c and
# Objects/weakrefobject.c define it, on a runtime whose objects are never
# freed: a weak reference's referent stays alive, so a reference never dies
# and its callback is never called.  Which objects accept weak references,
# the sharing of basic references, their equality, hash and repr follow
# CPython.
"""Weak-reference support module."""

_heap_type_info = _cetta_prelude("_weakref_type_info")
_qualified = _cetta_prelude("_fqn")

# per referent (by id: no object is ever freed, so no id is reused), the
# weak references and proxies made to it, oldest first
_refs = {}


def _check_weakrefable(ob):
    if not _heap_type_info(type(ob)):
        raise TypeError("cannot create weak reference to '" + type(ob).__name__ + "' object")


class ReferenceType:
    __module__ = "weakref"
    __slots__ = ("_ob", "_callback", "_hash")

    def __new__(cls, ob, callback=None, /, *args, **kwargs):
        _check_weakrefable(ob)
        rs = _refs.setdefault(id(ob), [])
        if callback is None and cls is ReferenceType:
            for r in rs:
                if type(r) is ReferenceType and r._callback is None:
                    return r
        self = object.__new__(cls)
        self._ob = ob
        self._callback = callback
        self._hash = None
        rs.append(self)
        return self

    def __init__(self, ob, callback=None, /, *args, **kwargs):
        pass

    def __call__(self):
        return self._ob

    @property
    def __callback__(self):
        return self._callback

    def __eq__(self, other):
        if not isinstance(other, ReferenceType):
            return NotImplemented
        if self._ob is other._ob:
            return True
        return self._ob == other._ob

    def __ne__(self, other):
        r = self.__eq__(other)
        return r if r is NotImplemented else not r

    def __hash__(self):
        if self._hash is None:
            self._hash = hash(self._ob)
        return self._hash

    def __repr__(self):
        ob = self._ob
        name = getattr(ob, "__name__", None)
        text = "<weakref at " + hex(id(self)) + "; to '" + _qualified(type(ob)) + "' at " + hex(id(ob))
        return text + (" (" + name + ")>" if isinstance(name, str) else ">")

    __class_getitem__ = classmethod(type(list[int]))


ref = ReferenceType


class ProxyType:
    __module__ = "weakref"
    __slots__ = ("_ob", "_callback")

    def __getattribute__(self, name):
        return getattr(object.__getattribute__(self, "_ob"), name)

    def __setattr__(self, name, value):
        setattr(object.__getattribute__(self, "_ob"), name, value)

    def __delattr__(self, name):
        delattr(object.__getattribute__(self, "_ob"), name)

    def __repr__(self):
        ob = object.__getattribute__(self, "_ob")
        return ("<weakproxy at " + hex(id(self)) + "; to '" + _qualified(type(ob)) + "' at "
                + hex(id(ob)) + ">")

    def __str__(self):
        return str(object.__getattribute__(self, "_ob"))

    def __bool__(self):
        return bool(object.__getattribute__(self, "_ob"))

    def __len__(self):
        return len(object.__getattribute__(self, "_ob"))

    def __iter__(self):
        return iter(object.__getattribute__(self, "_ob"))

    def __next__(self):
        return next(object.__getattribute__(self, "_ob"))

    def __contains__(self, x):
        return x in object.__getattribute__(self, "_ob")

    def __getitem__(self, k):
        return object.__getattribute__(self, "_ob")[k]

    def __setitem__(self, k, v):
        object.__getattribute__(self, "_ob")[k] = v

    def __delitem__(self, k):
        del object.__getattribute__(self, "_ob")[k]

    def __eq__(self, other):
        return object.__getattribute__(self, "_ob") == other

    def __ne__(self, other):
        return object.__getattribute__(self, "_ob") != other

    def __lt__(self, other):
        return object.__getattribute__(self, "_ob") < other

    def __le__(self, other):
        return object.__getattribute__(self, "_ob") <= other

    def __gt__(self, other):
        return object.__getattribute__(self, "_ob") > other

    def __ge__(self, other):
        return object.__getattribute__(self, "_ob") >= other

    def __add__(self, other):
        return object.__getattribute__(self, "_ob") + other

    def __sub__(self, other):
        return object.__getattribute__(self, "_ob") - other

    def __mul__(self, other):
        return object.__getattribute__(self, "_ob") * other

    def __index__(self):
        return object.__getattribute__(self, "_ob").__index__()

    __hash__ = None


class CallableProxyType(ProxyType):
    __module__ = "weakref"
    __slots__ = ()

    def __call__(self, *args, **kwargs):
        return object.__getattribute__(self, "_ob")(*args, **kwargs)


def proxy(object_, callback=None, /):
    """Create a proxy object that weakly references 'object'."""
    _check_weakrefable(object_)
    cls = CallableProxyType if callable(object_) else ProxyType
    rs = _refs.setdefault(id(object_), [])
    if callback is None:
        for r in rs:
            if type(r) is cls and object.__getattribute__(r, "_callback") is None:
                return r
    p = object.__new__(cls)
    object.__setattr__(p, "_ob", object_)
    object.__setattr__(p, "_callback", callback)
    rs.append(p)
    return p


def getweakrefcount(object_, /):
    """Return the number of weak references to 'object'."""
    return len(_refs.get(id(object_), ()))


def getweakrefs(object_, /):
    """Return a list of all weak reference objects pointing to 'object'."""
    return list(_refs.get(id(object_), ()))


def _remove_dead_weakref(dct, key, /):
    """Atomically remove key from dict if it points to a dead weakref."""
    # no reference dies here
    if not isinstance(dct, dict):
        raise TypeError("_remove_dead_weakref() argument 1 must be dict, not " + type(dct).__name__)
