# The Python data model, as CPython 3.13 behaves: the protocols the core's
# operations reach when a class of the program is involved.  This module runs
# before every program, in globals of its own where the runtime's intrinsics
# (the names beginning _cetta_) are bound.  Its names that do not begin with
# an underscore, and its dunder names, join the builtins.

_NULL = _cetta_null()

# the type checks this module makes are CPython's C-level ones
# (PyObject_TypeCheck, PyType_IsSubtype): a class's MRO, never its
# __instancecheck__ or __subclasscheck__, which the builtins of these names
# honour for the program (the builtins take the runtime's, not these)
isinstance = _cetta_isinstance
issubclass = _cetta_issubclass
_builtin_isinstance = _cetta_builtin("isinstance")


# ---------------------------------------------------------------- lookups

# _PyType_Lookup: the first class in the method resolution order whose
# namespace has the name (the runtime's, behind its method cache)
_mro_lookup = _cetta_mro_lookup


def _bmeth_get(d, obj, tp=None):
    # the __get__ of the builtin types' member descriptors (wrap_descr_get):
    # None stands for no instance
    return _cetta_bmeth_get(d, _NULL if obj is None else obj, tp)


def _descr_get(get, descr, obj, tp):
    # tp_descr_get: a builtin member's own (_NULL for no instance, None an
    # instance), a __get__ written in Python with None for no instance
    if get is _bmeth_get:
        return _cetta_bmeth_get(descr, obj, tp)
    return get(descr, None if obj is _NULL else obj, tp)


def _check_hook(cls, name, x):
    # object_recursive_isinstance, object_issubclass: CLS's own
    # __instancecheck__ or __subclasscheck__ (type's own, the default, is
    # left to the runtime), its answer's truth; else _NULL
    tp = type(cls)
    f = _mro_lookup(tp, name)
    if f is _NULL or f is _mro_lookup(type, name):
        return _NULL
    return bool(_lookup_special(cls, name)(x))


def _lookup_special(obj, name):
    # _PyObject_LookupSpecial: on the type, bound through its descriptor
    tp = type(obj)
    res = _mro_lookup(tp, name)
    if res is _NULL:
        return _NULL
    if type(res) is _function:
        return _cetta_bind(res, obj)
    get = _mro_lookup(type(res), "__get__")
    if get is _NULL:
        return res
    if get is _bmeth_get:
        return _cetta_bmeth_get(res, obj, tp)
    return _descr_get(get, res, obj, tp)


_function = type(_lookup_special)


def _fqn(t):
    # PyType_GetFullyQualifiedName (the %T of CPython's messages):
    # MODULE.QUALNAME, the qualname alone in builtins and __main__
    m = _cetta_type_module(t)
    q = _cetta_type_field(t, "qualname")
    if not isinstance(m, str) or m == "builtins" or m == "__main__":
        return q
    return m + "." + q


def _type_name(tp):
    # the type's tp_name, the name CPython's messages give it: a builtin type
    # of another module than builtins is MODULE.NAME, any other type its name
    return _cetta_type_field(tp, "tp_name")


def _clinic_args(fname, names, minpos, maxpos, args, kwargs, posonly=0, minkw=0, varpos=False):
    # _PyArg_UnpackKeywords, Argument Clinic's reading of a builtin's
    # arguments: NAMES its parameters (the first POSONLY by position only,
    # those after MAXPOS by keyword only), the first MINPOS required, and
    # MINKW of the keyword-only ones; each argument given, else _NULL.  With
    # VARPOS, positional arguments past MAXPOS are the caller's *args
    nargs = len(args)
    nkwargs = len(kwargs)
    maxargs = len(names)
    fn = fname + "()"
    if nkwargs == 0 and minkw == 0 and minpos <= nargs and (varpos or nargs <= maxpos):
        return list(args[:maxpos]) + [_NULL] * (maxargs - min(nargs, maxpos))
    if not varpos and nargs + nkwargs > maxargs:
        raise TypeError(fn + " takes at most " + str(maxargs) + " " + ("keyword " if nargs == 0 else "")
                        + "argument" + ("" if maxargs == 1 else "s") + " (" + str(nargs + nkwargs) + " given)")
    if varpos and nargs > maxpos:
        args = args[:maxpos]
        nargs = maxpos
    if nargs > maxpos:
        if maxpos == 0:
            raise TypeError(fn + " takes no positional arguments")
        raise TypeError(fn + " takes " + ("at most" if minpos < maxpos else "exactly") + " " + str(maxpos)
                        + " positional argument" + ("" if maxpos == 1 else "s") + " (" + str(nargs) + " given)")
    minposonly = posonly if posonly < minpos else minpos
    if nargs < minposonly:
        raise TypeError(fn + " takes " + ("at least" if minposonly < maxpos else "exactly") + " " + str(minposonly)
                        + " positional argument" + ("" if minposonly == 1 else "s") + " (" + str(nargs) + " given)")
    reqlimit = maxpos + minkw if minkw else minpos
    buf = list(args) + [_NULL] * (maxargs - nargs)
    left = nkwargs
    i = nargs if nargs > posonly else posonly
    while i < maxargs:
        if left:
            v = kwargs.get(names[i], _NULL)
        elif i >= reqlimit:
            break
        else:
            v = _NULL
        buf[i] = v
        if v is not _NULL:
            left -= 1
        elif i < minpos or (maxpos <= i and i < reqlimit):
            raise TypeError(fn + " missing required argument '" + names[i] + "' (pos " + str(i + 1) + ")")
        i += 1
    if left > 0:
        for i in range(posonly, nargs):
            if names[i] in kwargs:
                raise TypeError("argument for " + fn + " given by name ('" + names[i] + "') and position ("
                                + str(i + 1) + ")")
        keywords = list(names[posonly:])
        for k in kwargs:
            if k not in keywords:
                s = _cetta_suggestion(k, keywords)
                if s is not None:
                    raise TypeError(fn + " got an unexpected keyword argument '" + k + "'. Did you mean '" + s + "'?")
                raise TypeError(fn + " got an unexpected keyword argument '" + k + "'")
        raise TypeError("invalid keyword argument for " + fn)
    return buf


def _is_data_descriptor(descr):
    tp = type(descr)
    return _mro_lookup(tp, "__set__") is not _NULL or _mro_lookup(tp, "__delete__") is not _NULL


# ---------------------------------------------------------------- attributes

def _object_getattribute(obj, name):
    # PyObject_GenericGetAttr: a data descriptor of the class; else the
    # instance's own attribute; else a descriptor or a plain class attribute
    tp = type(obj)
    descr = _mro_lookup(tp, name)
    get = _NULL
    if descr is not _NULL:
        get = _mro_lookup(type(descr), "__get__")
        if get is not _NULL and _is_data_descriptor(descr):
            return _descr_get(get, descr, obj, tp)
    d = _cetta_object_dict(obj)
    if d is not None:
        # the dict's own storage (a dict subclass's methods are not asked)
        d = _cetta_base(d)
        if name in d:
            return d[name]
    if get is not _NULL:
        if type(descr) is _function:
            # a function found for an instance is bound to it (None too: the
            # methods builtin types get from here stand for C methods)
            return _cetta_bind(descr, obj)
        if get is _bmeth_get:
            return _cetta_bmeth_get(descr, obj, tp)
        return _descr_get(get, descr, obj, tp)
    if descr is not _NULL:
        return descr
    raise _cetta_attribute_error(obj, name)


def _object_setattr(obj, name, value):
    # PyObject_GenericSetAttr: a data descriptor's __set__, else the
    # instance's __dict__
    tp = type(obj)
    descr = _mro_lookup(tp, name)
    if descr is not _NULL:
        setter = _mro_lookup(type(descr), "__set__")
        if setter is not _NULL:
            setter(descr, obj, value)
            return None
    d = _cetta_object_dict(obj)
    if d is None:
        raise _no_dict_error(obj, tp, name, descr)
    _cetta_base(d)[name] = value
    return None


def _object_delattr(obj, name):
    tp = type(obj)
    descr = _mro_lookup(tp, name)
    if descr is not _NULL:
        deleter = _mro_lookup(type(descr), "__delete__")
        if deleter is not _NULL:
            deleter(descr, obj)
            return None
    d = _cetta_object_dict(obj)
    if d is None:
        raise _no_dict_error(obj, tp, name, descr)
    d = _cetta_base(d)
    if name not in d:
        raise _cetta_attribute_error(obj, name)
    del d[name]
    return None


def _no_dict_error(obj, tp, name, descr):
    # an instance without a __dict__: the class's attribute is read-only;
    # a new one has no place
    if descr is not _NULL:
        return AttributeError("'" + _type_name(tp) + "' object attribute '" + name + "' is read-only")
    return _cetta_attribute_error_text("'" + _type_name(tp) + "' object has no attribute '" + name
                                       + "' and no __dict__ for setting new attributes", obj, name)


def _type_getattribute(tp, name):
    # type_getattro: a data descriptor of the metaclass; else the class's own
    # attribute (through its descriptor, for no instance); else the
    # metaclass's attribute
    meta = type(tp)
    meta_attr = _mro_lookup(meta, name)
    meta_get = _NULL
    if meta_attr is not _NULL:
        meta_get = _mro_lookup(type(meta_attr), "__get__")
        if meta_get is not _NULL and _is_data_descriptor(meta_attr):
            return _descr_get(meta_get, meta_attr, tp, meta)
    attr = _mro_lookup(tp, name)
    if attr is not _NULL:
        local_get = _mro_lookup(type(attr), "__get__")
        if local_get is not _NULL:
            return _descr_get(local_get, attr, _NULL, tp)
        return attr
    if meta_get is not _NULL:
        return _descr_get(meta_get, meta_attr, tp, meta)
    if meta_attr is not _NULL:
        return meta_attr
    raise _cetta_attribute_error(tp, name)


def _type_setattr(tp, name, value):
    if not _cetta_type_field(tp, "heap"):
        raise TypeError("cannot set " + repr(name) + " attribute of immutable type '" + _type_name(tp) + "'")
    descr = _mro_lookup(type(tp), name)
    if descr is not _NULL:
        setter = _mro_lookup(type(descr), "__set__")
        if setter is not _NULL:
            setter(descr, tp, value)
            return None
    _cetta_set_type_attr(tp, name, value)
    return None


def _type_delattr(tp, name):
    # type_setattro with no value: the metaclass's descriptor, else the
    # class's own namespace
    if not _cetta_type_field(tp, "heap"):
        raise TypeError("cannot set " + repr(name) + " attribute of immutable type '" + _type_name(tp) + "'")
    descr = _mro_lookup(type(tp), name)
    if descr is not _NULL:
        deleter = _mro_lookup(type(descr), "__delete__")
        if deleter is not _NULL:
            deleter(descr, tp)
            return None
    if not _cetta_del_type_attr(tp, name):
        raise _cetta_attribute_error(tp, name)
    return None


def _getattr(obj, name):
    # slot_tp_getattr_hook: __getattribute__, then __getattr__ when that
    # raised AttributeError
    tp = type(obj)
    getattribute = _mro_lookup(tp, "__getattribute__")
    hook = _mro_lookup(tp, "__getattr__")
    if hook is _NULL:
        return getattribute(obj, name)
    try:
        return getattribute(obj, name)
    except AttributeError:
        return hook(obj, name)


def _setattr(obj, name, value):
    _mro_lookup(type(obj), "__setattr__")(obj, name, value)
    return None


def _delattr(obj, name):
    _mro_lookup(type(obj), "__delattr__")(obj, name)
    return None


def _attribute_name(name, what):
    if type(name) is not str:
        raise TypeError("attribute name must be string, not '" + _type_name(type(name)) + "'")
    return name


def getattr(obj, name, *default):
    _attribute_name(name, "getattr")
    if len(default) > 1:
        raise TypeError("getattr expected at most 3 arguments, got " + str(2 + len(default)))
    if not default:
        return _cetta_getattr(obj, name)
    try:
        return _cetta_getattr(obj, name)
    except AttributeError:
        return default[0]


def hasattr(obj, name):
    _attribute_name(name, "hasattr")
    try:
        _cetta_getattr(obj, name)
        return True
    except AttributeError:
        return False


def setattr(obj, name, value):
    _attribute_name(name, "setattr")
    _cetta_setattr(obj, name, value)
    return None


def delattr(obj, name):
    _attribute_name(name, "delattr")
    _cetta_delattr(obj, name)
    return None


# ---------------------------------------------------------------- descriptors

def _function_get(f, obj, tp=None):
    if obj is None:
        return f
    return _cetta_bind(f, obj)


def _staticmethod_get(sm, obj, tp=None):
    return _cetta_field(sm, "func")


def _classmethod_get(cm, obj, tp=None):
    if tp is None:
        tp = type(obj)
    return _cetta_bind(_cetta_field(cm, "func"), tp)


def _getset_get(d, obj, tp=None):
    if obj is None:
        return d
    if _cetta_is_python_object(d):
        # a computed attribute written in the prelude (_computed)
        return _cetta_field(d, "getter")(obj)
    return _cetta_getset_get(d, obj)


def _getset_set(d, obj, value):
    if _cetta_is_python_object(d):
        raise AttributeError("attribute '" + _cetta_field(d, "member") + "' of '"
                             + _descriptor_owner_name(d) + "' objects is not writable")
    _cetta_getset_set(d, obj, value)
    return None


# ---------------------------------------------------------------- classes

def _calculate_meta(meta, bases):
    # _PyType_CalculateMetaclass: the most derived of the metaclasses
    winner = meta
    for base in bases:
        tmp = type(base)
        if issubclass(winner, tmp):
            continue
        if issubclass(tmp, winner):
            winner = tmp
            continue
        raise TypeError("metaclass conflict: the metaclass of a derived class must be "
                        "a (non-strict) subclass of the metaclasses of all its bases")
    return winner


def _mro_tail(bases):
    # C3 after the class itself: the merge of its bases' orders and of the
    # bases.  A class's order is fixed when it is made: the class, then this
    seqs = []
    for base in bases:
        seqs.append(list(_cetta_type_field(base, "mro")))
    seqs.append(list(bases))
    result = []
    while True:
        rest = []
        for s in seqs:
            if s:
                rest.append(s)
        seqs = rest
        if not seqs:
            return tuple(result)
        candidate = _NULL
        for s in seqs:
            head = s[0]
            blocked = False
            for t in seqs:
                if head in t[1:]:
                    blocked = True
                    break
            if not blocked:
                candidate = head
                break
        if candidate is _NULL:
            # set_mro_error: the heads of the orders still unmerged, each once,
            # by __name__ (class_name)
            names = []
            for s in seqs:
                n = _cetta_type_field(s[0], "name")
                if n not in names:
                    names.append(n)
            raise TypeError("Cannot create a consistent method resolution order (MRO) for bases "
                            + ", ".join(names))
        result.append(candidate)
        for s in seqs:
            if s[0] is candidate:
                del s[0]


def _mapping_get_optional(mapping, key):
    # PyMapping_GetOptionalItem: the item, else _NULL when the key is missing
    try:
        return mapping[key]
    except KeyError:
        return _NULL


def _update_bases(bases):
    # update_bases (PEP 560): a base that is no class gives the entries its
    # __mro_entries__ returns
    new_bases = None
    i = 0
    for base in bases:
        if isinstance(base, type):
            if new_bases is not None:
                new_bases.append(base)
            i += 1
            continue
        meth = getattr(base, "__mro_entries__", _NULL)
        if meth is _NULL:
            if new_bases is not None:
                new_bases.append(base)
            i += 1
            continue
        new_base = meth(bases)
        if not isinstance(new_base, tuple):
            raise TypeError("__mro_entries__ must return a tuple")
        if new_bases is None:
            new_bases = list(bases[:i])
        new_bases.extend(new_base)
        i += 1
    return bases if new_bases is None else tuple(new_bases)


def __build_class__(func, name, *orig_bases, metaclass=_NULL, **kwds):
    # builtin___build_class__
    bases = _update_bases(orig_bases)
    if metaclass is _NULL:
        if bases:
            meta = type(bases[0])
        else:
            meta = type
        meta = _calculate_meta(meta, bases)
    elif isinstance(metaclass, type):
        meta = _calculate_meta(metaclass, bases)
    else:
        meta = metaclass
    prepare = getattr(meta, "__prepare__", _NULL)
    if prepare is _NULL:
        ns = {}
    else:
        ns = prepare(name, bases, **kwds)
    cell = _cetta_run_class_body(func, ns)
    if bases is not orig_bases:
        ns["__orig_bases__"] = orig_bases
    cls = meta(name, bases, ns, **kwds)
    if cell is not None and isinstance(cls, type):
        held = _cetta_cell_get(cell)
        if held is not cls:
            if held is _NULL:
                raise RuntimeError("__class__ not set defining " + repr(name) + " as " + repr(cls)
                                   + ". Was __classcell__ propagated to type.__new__?")
            raise TypeError("__class__ set to " + repr(held) + " defining " + repr(name)
                            + " as " + repr(cls))
    return cls


def _type_prepare(meta, name, bases, **kwds):
    return {}


def _best_base(bases):
    # best_base: the base whose solid base is the most derived; the bases'
    # solid bases must lie on one line of inheritance
    base = winner = _NULL
    for b in bases:
        candidate = _cetta_type_field(b, "solid")
        if winner is _NULL:
            winner = candidate
            base = b
        elif issubclass(winner, candidate):
            pass
        elif issubclass(candidate, winner):
            winner = candidate
            base = b
        else:
            raise TypeError("multiple bases have instance lay-out conflict")
    return base


def _mangle(classname, name):
    # _Py_Mangle: __private becomes _Class__private (not __dunder__, not a
    # dotted name, not under a class of underscores only)
    if not name.startswith("__") or name.endswith("__") or "." in name:
        return name
    stripped = classname.lstrip("_")
    if not stripped:
        return name
    return "_" + stripped + name


def _type_new_slots(name, bases, base, d, slots):
    # type_new_slots: the slot names (checked, mangled, sorted) and whether
    # the class adds a __dict__ and weak references
    may_add_dict = not _cetta_type_field(base, "has_dict")
    may_add_weak = not _cetta_type_field(base, "has_weakref")
    if slots is _NULL:
        return (), may_add_dict, may_add_weak and not _cetta_type_field(base, "varsize")
    slots = (slots,) if isinstance(slots, str) else tuple(slots)
    add_dict = add_weak = False
    for s in slots:
        if not isinstance(s, str):
            raise TypeError("__slots__ items must be strings, not '" + _type_name(type(s)) + "'")
        if not s.isidentifier():
            raise TypeError("__slots__ must be identifiers")
        if s == "__dict__":
            if not may_add_dict or add_dict:
                raise TypeError("__dict__ slot disallowed: we already got one")
            add_dict = True
        if s == "__weakref__":
            if not may_add_weak or add_weak:
                raise TypeError("__weakref__ slot disallowed: we already got one")
            add_weak = True
    names = []
    for s in slots:
        if (add_dict and s == "__dict__") or (add_weak and s == "__weakref__"):
            continue
        m = _mangle(name, s)
        if m in d and m not in ("__qualname__", "__classcell__", "__classdictcell__"):
            raise ValueError(repr(m) + " in __slots__ conflicts with class variable")
        names.append(m)
    names.sort()
    # type_new_descriptors: a slot of its own under a base whose instances
    # hold items (int, bytes) has no place, but after a tuple's
    if names and _cetta_type_field(base, "varsize") and not issubclass(base, tuple):
        raise TypeError("arbitrary __slots__ not supported for subtype of '" + _type_name(base) + "'")
    # the bases after the first may bring a __dict__ or weak references
    if len(bases) > 1 and ((may_add_dict and not add_dict) or (may_add_weak and not add_weak)):
        for b in bases:
            if b is base:
                continue
            if may_add_dict and not add_dict and _cetta_type_field(b, "has_dict"):
                add_dict = True
            if may_add_weak and not add_weak and _cetta_type_field(b, "has_weakref"):
                add_weak = True
            if (may_add_dict and not add_dict) or (may_add_weak and not add_weak):
                continue
            break
    return tuple(names), add_dict, add_weak


# tp_subclasses: each class's direct subclasses in the order they were
# made (a class is never freed here, so its id names it); the builtin types'
# own builtin subclasses (bool under int) are not listed
_subclasses = {}


def _type_subclasses(cls):
    # type.__subclasses__
    return list(_subclasses.get(id(cls), ()))


def _type_mro(cls):
    # type.mro: the class's C3 order (mro_implementation)
    return [cls] + list(_mro_tail(cls.__bases__))


def _type_new(meta, name, bases, ns, **kwds):
    # type.__new__: the new class, its order computed, its __set_name__ and
    # its bases' __init_subclass__ called
    if type(name) is not str:
        raise TypeError("type.__new__() argument 1 must be str, not " + _type_name(type(name)))
    if bases:
        # type_new_get_bases: a base that is no type but offers
        # __mro_entries__ is refused; the most derived metaclass of the bases
        # wins, and one with a __new__ of its own makes the class instead
        for b in bases:
            if not isinstance(b, type) and getattr(b, "__mro_entries__", _NULL) is not _NULL:
                raise TypeError("type() doesn't support MRO entry resolution; use types.new_class()")
        winner = _calculate_meta(meta, bases)
        if winner is not meta:
            if _mro_lookup(winner, "__new__") is not _mro_lookup(type, "__new__"):
                return winner.__new__(winner, name, bases, ns, **kwds)
            meta = winner
    else:
        bases = (object,)
    seen = []
    for base in bases:
        if not isinstance(base, type):
            raise TypeError("bases must be types")
        if not _cetta_type_field(base, "subclassable"):
            raise TypeError("type '" + _type_name(base) + "' is not an acceptable base type")
        if base in seen:
            raise TypeError("duplicate base class " + _type_name(base))
        seen.append(base)
    base = _best_base(bases)
    d = dict(ns)
    qualname = d.pop("__qualname__", name)
    cell = d.pop("__classcell__", _NULL)
    classdict_cell = d.pop("__classdictcell__", _NULL)
    slots = d.get("__slots__", _NULL)
    slot_names, add_dict, add_weak = _type_new_slots(name, bases, base, d, slots)
    if add_dict:
        d["__dict__"] = _cetta_getset("__dict__")
    if add_weak:
        d["__weakref__"] = _cetta_getset("__weakref__")
    if "__doc__" not in d:
        d["__doc__"] = None
    if "__eq__" in d and "__hash__" not in d:
        d["__hash__"] = None
    new = d.get("__new__", _NULL)
    if new is not _NULL and type(new) is _function:
        d["__new__"] = staticmethod(new)
    for hook in ("__init_subclass__", "__class_getitem__"):
        f = d.get(hook, _NULL)
        if f is not _NULL and type(f) is _function:
            d[hook] = classmethod(f)
    cls = _cetta_new_type(meta, name, qualname, bases, d, _mro_tail(bases))
    for b in bases:
        # type_ready_add_subclasses
        _subclasses.setdefault(id(b), []).append(cls)
    _cetta_set_field(cls, "base", base)
    _cetta_set_field(cls, "solid", cls if slot_names else _cetta_type_field(base, "solid"))
    _cetta_set_field(cls, "has_dict", bool(add_dict or _cetta_type_field(base, "has_dict")))
    _cetta_set_field(cls, "has_weakref", bool(add_weak or _cetta_type_field(base, "has_weakref")))
    _cetta_set_field(cls, "varsize", _cetta_type_field(base, "varsize"))
    for s in slot_names:
        _cetta_set_type_attr(cls, s, _slot_member(cls, s, slots.get(s) if isinstance(slots, dict) else None))
    if cell is not _NULL:
        _cetta_cell_set(cell, cls)
    if classdict_cell is not _NULL:
        # type_new_set_classdictcell: the scopes that see the class read its dict
        _cetta_cell_set(classdict_cell, d)
    for key, value in list(d.items()):
        set_name = _mro_lookup(type(value), "__set_name__")
        if set_name is not _NULL:
            set_name(value, cls, key)
    _super_attribute(cls, cls, "__init_subclass__")(**kwds)
    return cls


def _type_init(cls, *args, **kwargs):
    return None


def _type_call(cls, *args, **kwargs):
    # type.__call__: __new__, then __init__ when __new__ made an instance of
    # the class
    if cls is type:
        if len(args) == 1 and not kwargs:
            return _cetta_type_of(args[0])
        if len(args) != 3:
            raise TypeError("type() takes 1 or 3 arguments")
    obj = _cetta_getattr(cls, "__new__")(cls, *args, **kwargs)
    if not isinstance(obj, cls):
        return obj
    result = _mro_lookup(type(obj), "__init__")(obj, *args, **kwargs)
    if result is not None:
        raise TypeError("__init__() should return None, not '" + _type_name(type(result)) + "'")
    return obj


def _type_repr(cls):
    module = _cetta_type_module(cls)
    qualname = _cetta_type_field(cls, "qualname")
    if module == "builtins":
        return "<class '" + qualname + "'>"
    return "<class '" + module + "." + qualname + "'>"


# ---------------------------------------------------------------- objects

def _object_new(cls, *args, **kwargs):
    # object_new: extra arguments are an error unless the class has its own
    # __init__ and not its own __new__; tp_new_wrapper's check first: the
    # most derived base that is not a class whose __new__ is written in
    # Python (slot_tp_new: a builtin type never is, whatever language the
    # runtime writes its __new__ in) must make its instances with object's
    if not isinstance(cls, type):
        raise TypeError("object.__new__(X): X is not a type object (" + _type_name(type(cls)) + ")")
    staticbase = cls
    heap = _cetta_type_field(staticbase, "heap")
    while heap and type(_mro_lookup(staticbase, "__new__")) is staticmethod:
        staticbase = _cetta_type_field(staticbase, "base")
        heap = staticbase is not None and _cetta_type_field(staticbase, "heap")
    # a builtin type whose instances calling it does not make (tp_new NULL)
    if staticbase is not None and not heap and _cetta_type_field(staticbase, "noinstances"):
        raise TypeError("cannot create '" + _type_name(cls) + "' instances")
    if staticbase is not None and _mro_lookup(staticbase, "__new__") is not _mro_lookup(object, "__new__"):
        raise TypeError("object.__new__(" + _type_name(cls) + ") is not safe, use " + _type_name(staticbase)
                        + ".__new__()")
    if args or kwargs:
        if _mro_lookup(cls, "__new__") is not _mro_lookup(object, "__new__"):
            raise TypeError("object.__new__() takes exactly one argument (the type to instantiate)")
        if _mro_lookup(cls, "__init__") is _mro_lookup(object, "__init__"):
            raise TypeError(_type_name(cls) + "() takes no arguments")
    return _cetta_new_object(cls)


def _object_init(self, *args, **kwargs):
    # object_init: extra arguments are an error unless the class has its own
    # __new__ and not its own __init__
    if args or kwargs:
        cls = type(self)
        if _mro_lookup(cls, "__init__") is not _mro_lookup(object, "__init__"):
            raise TypeError("object.__init__() takes exactly one argument (the instance to initialize)")
        if _mro_lookup(cls, "__new__") is _mro_lookup(object, "__new__"):
            raise TypeError(_type_name(cls) + "() takes no arguments")
    return None


def _object_repr(self):
    tp = type(self)
    module = _cetta_type_module(tp)
    qualname = _cetta_type_field(tp, "qualname")
    if module != "builtins":
        qualname = module + "." + qualname
    return "<" + qualname + " object at " + _cetta_address(self) + ">"


def _object_str(self):
    return _cetta_repr(self)


def _object_eq(self, other):
    if self is other:
        return True
    return NotImplemented


def _object_ne(self, other):
    result = _mro_lookup(type(self), "__eq__")(self, other)
    if result is NotImplemented:
        return result
    return not result


def _object_hash(self):
    return _cetta_identity_hash(self)


def _object_init_subclass(cls, **kwargs):
    if kwargs:
        raise TypeError(_type_name(cls) + ".__init_subclass__() takes no keyword arguments")
    return None


# ---------------------------------------------------------------- super

def _super_attribute(thisclass, obj, name):
    # super(thisclass, obj).name: the first class after thisclass in the order
    # of obj's class (obj itself when it is a subclass), its descriptor bound
    if isinstance(obj, type) and issubclass(obj, thisclass):
        start = obj
    else:
        start = type(obj)
    mro = _cetta_type_field(start, "mro")
    i = 0
    while i < len(mro) and mro[i] is not thisclass:
        i += 1
    i += 1
    while i < len(mro):
        d = _cetta_type_dict(mro[i])
        if name in d:
            res = d[name]
            get = _mro_lookup(type(res), "__get__")
            if get is not _NULL:
                if obj is start:
                    return _descr_get(get, res, _NULL, start)
                return _descr_get(get, res, obj, start)
            return res
        i += 1
    return _NULL


def _super_new(cls, *args, **kwargs):
    if len(args) == 0:
        raise RuntimeError("super(): no arguments")
    thisclass = args[0]
    if not isinstance(thisclass, type):
        raise TypeError("super() argument 1 must be a type, not " + _type_name(type(thisclass)))
    su = _cetta_new_object(cls)
    _cetta_set_field(su, "thisclass", thisclass)
    if len(args) > 1:
        obj = args[1]
        if not (isinstance(obj, thisclass) or (isinstance(obj, type) and issubclass(obj, thisclass))):
            raise TypeError("super(type, obj): obj must be an instance or subtype of type")
        _cetta_set_field(su, "obj", obj)
    else:
        _cetta_set_field(su, "obj", None)
    return su


def _super_init(su, *args, **kwargs):
    return None


def _super_getattribute(su, name):
    obj = _cetta_field(su, "obj")
    if obj is not None and name != "__class__":
        res = _super_attribute(_cetta_field(su, "thisclass"), obj, name)
        if res is not _NULL:
            return res
    return _object_getattribute(su, name)


def _super_call(super_type, cell_value, first):
    # super() without arguments: super(__class__, the first argument)
    return super_type(cell_value, first)


# ---------------------------------------------------------------- mappingproxy

# Objects/descrobject.c: a read-only view of a mapping; what a class's
# __dict__ gives
_mappingproxy = _cetta_builtin_type("mappingproxy")


def _mapping_check(o):
    # PyMapping_Check: the type subscripts (mp_subscript).  Of the builtin
    # types the runtime holds natively, those that do
    if _cetta_is_python_object(o):
        return _mro_lookup(type(o), "__getitem__") is not _NULL
    return isinstance(o, (dict, str, bytes, bytearray, range, memoryview, list, tuple))


def _mappingproxy_new(cls, *args, **kwargs):
    # mappingproxy_new(mapping), read as Argument Clinic reads it
    mapping = _clinic_args("mappingproxy", ("mapping",), 1, 1, args, kwargs)[0]
    if not _mapping_check(mapping) or isinstance(mapping, (list, tuple)):
        raise TypeError("mappingproxy() argument must be a mapping, not " + _type_name(type(mapping)))
    p = _cetta_new_object(_mappingproxy)
    _cetta_set_field(p, "mapping", mapping)
    return p


def _mappingproxy_len(self):
    return len(_cetta_field(self, "mapping"))


def _mappingproxy_getitem(self, key):
    return _cetta_field(self, "mapping")[key]


def _mappingproxy_contains(self, key):
    return key in _cetta_field(self, "mapping")


def _mappingproxy_or(left, right):
    # nb_or, either side a proxy: the mappings or-ed
    if isinstance(left, _mappingproxy):
        left = _cetta_field(left, "mapping")
    if isinstance(right, _mappingproxy):
        right = _cetta_field(right, "mapping")
    return left | right


def _mappingproxy_ror(self, other):
    return _mappingproxy_or(other, self)


def _mappingproxy_ior(self, other):
    raise TypeError("'|=' is not supported by mappingproxy; use '|' instead")


def _mappingproxy_get(self, *args):
    if len(args) < 1:
        raise TypeError("get expected at least 1 argument, got 0")
    if len(args) > 2:
        raise TypeError("get expected at most 2 arguments, got " + str(len(args)))
    return _cetta_field(self, "mapping").get(args[0], args[1] if len(args) == 2 else None)


def _mappingproxy_keys(self):
    return _cetta_field(self, "mapping").keys()


def _mappingproxy_values(self):
    return _cetta_field(self, "mapping").values()


def _mappingproxy_items(self):
    return _cetta_field(self, "mapping").items()


def _mappingproxy_copy(self):
    return _cetta_field(self, "mapping").copy()


def _mappingproxy_reversed(self):
    return _cetta_field(self, "mapping").__reversed__()


def _mappingproxy_iter(self):
    return iter(_cetta_field(self, "mapping"))


def _mappingproxy_hash(self):
    return hash(_cetta_field(self, "mapping"))


def _mappingproxy_str(self):
    return str(_cetta_field(self, "mapping"))


def _mappingproxy_repr(self):
    return "mappingproxy(" + repr(_cetta_field(self, "mapping")) + ")"


# tp_richcompare: the mapping compared with the other
def _mappingproxy_eq(self, other):
    return _cetta_field(self, "mapping") == other


def _mappingproxy_ne(self, other):
    return _cetta_field(self, "mapping") != other


def _mappingproxy_lt(self, other):
    return _cetta_field(self, "mapping") < other


def _mappingproxy_le(self, other):
    return _cetta_field(self, "mapping") <= other


def _mappingproxy_gt(self, other):
    return _cetta_field(self, "mapping") > other


def _mappingproxy_ge(self, other):
    return _cetta_field(self, "mapping") >= other


for _name, _f in (("__new__", staticmethod(_mappingproxy_new)), ("__len__", _mappingproxy_len),
                  ("__getitem__", _mappingproxy_getitem), ("__contains__", _mappingproxy_contains),
                  ("__or__", _mappingproxy_or), ("__ror__", _mappingproxy_ror), ("__ior__", _mappingproxy_ior),
                  ("get", _mappingproxy_get), ("keys", _mappingproxy_keys), ("values", _mappingproxy_values),
                  ("items", _mappingproxy_items), ("copy", _mappingproxy_copy),
                  ("__reversed__", _mappingproxy_reversed), ("__iter__", _mappingproxy_iter),
                  ("__hash__", _mappingproxy_hash), ("__str__", _mappingproxy_str), ("__repr__", _mappingproxy_repr),
                  ("__eq__", _mappingproxy_eq), ("__ne__", _mappingproxy_ne), ("__lt__", _mappingproxy_lt),
                  ("__le__", _mappingproxy_le), ("__gt__", _mappingproxy_gt), ("__ge__", _mappingproxy_ge)):
    _cetta_set_type_attr(_mappingproxy, _name, _f)


# ---------------------------------------------------------------- descriptors of builtin types

# A builtin type's member (PyMemberDef, read only: the object's field) and its
# computed attribute (PyGetSetDef: a getter's answer), for the builtin types
# written here.  The runtime's own computed attributes are (py:getset NAME).
_member_descriptor = _cetta_builtin_type("member_descriptor")
_getset_descriptor = type(_cetta_getset("__dict__"))


def _member(owner, name, field):
    m = _cetta_new_object(_member_descriptor)
    _cetta_set_field(m, "owner", owner)
    _cetta_set_field(m, "member", name)
    _cetta_set_field(m, "field", field)
    _cetta_set_type_attr(owner, name, m)


def _computed(owner, name, getter):
    g = _cetta_new_object(_getset_descriptor)
    _cetta_set_field(g, "owner", owner)
    _cetta_set_field(g, "member", name)
    _cetta_set_field(g, "getter", getter)
    _cetta_set_type_attr(owner, name, g)


def _slot_member(owner, name, doc):
    # a __slots__ name's member descriptor: the instance's field slot:NAME,
    # read, written and deleted through it
    m = _cetta_new_object(_member_descriptor)
    _cetta_set_field(m, "owner", owner)
    _cetta_set_field(m, "member", name)
    _cetta_set_field(m, "field", "slot:" + name)
    _cetta_set_field(m, "writable", True)
    _cetta_set_field(m, "doc", doc)
    return m


def _member_check(m, obj):
    # descr_check: the instance must be the owner's
    owner = _cetta_field(m, "owner")
    if not isinstance(obj, owner):
        raise TypeError("descriptor '" + _cetta_field(m, "member") + "' for '" + _type_name(owner)
                        + "' objects doesn't apply to a '" + _type_name(type(obj)) + "' object")


def _member_get(m, obj, tp=None):
    # member_get: the owner's member of an instance (an unset slot is
    # missing); without an instance, the descriptor
    if obj is None:
        return m
    _member_check(m, obj)
    i = _cetta_field(m, "index")
    if i is not None:
        # a struct sequence's field (its member reads ob_item[i]): an item
        # of its tuple, or of the fields beyond it
        n = _cetta_field(m, "visible")
        return _cetta_field(obj, "value")[i] if i < n else _cetta_field(obj, "hidden")[i - n]
    if _cetta_field(m, "writable") is True:
        v = _cetta_slot_value(obj, _cetta_field(m, "field"))
        if v is _NULL:
            raise AttributeError("'" + _type_name(type(obj)) + "' object has no attribute '"
                                 + _cetta_field(m, "member") + "'")
        return v
    return _cetta_field(obj, _cetta_field(m, "field"))


def _member_set(m, obj, value):
    # member_set (PyMember_SetOne): a slot written; a builtin type's member is
    # read-only
    _member_check(m, obj)
    if _cetta_field(m, "writable") is not True:
        raise AttributeError("readonly attribute")
    _cetta_set_field(obj, _cetta_field(m, "field"), value)
    return None


def _member_delete(m, obj):
    # deleting an unset slot is an AttributeError whose message is its name
    _member_check(m, obj)
    if _cetta_field(m, "writable") is not True:
        raise AttributeError("readonly attribute")
    field = _cetta_field(m, "field")
    if _cetta_slot_value(obj, field) is _NULL:
        raise AttributeError(_cetta_field(m, "member"))
    _cetta_clear_field(obj, field)
    return None


def _descriptor_owner_name(d):
    # the owner's tp_name: a class made in Python has its bare name, a builtin
    # type of another module its dotted one
    return _type_name(_cetta_field(d, "owner"))


def _member_repr(m):
    return "<member '" + _cetta_field(m, "member") + "' of '" + _descriptor_owner_name(m) + "' objects>"


def _member_reduce(m):
    # descr_reduce: the attribute of the owner
    return (getattr, (_cetta_field(m, "owner"), _cetta_field(m, "member")))


_cetta_set_type_attr(_member_descriptor, "__get__", _member_get)
_cetta_set_type_attr(_member_descriptor, "__set__", _member_set)
_cetta_set_type_attr(_member_descriptor, "__delete__", _member_delete)
_cetta_set_type_attr(_member_descriptor, "__repr__", _member_repr)
_cetta_set_type_attr(_member_descriptor, "__reduce__", _member_reduce)
# descr_members and descr_getsets: the owner and the name read-only members,
# the doc (PyMemberDef's) and the qualified name computed
_member(_member_descriptor, "__objclass__", "owner")
_member(_member_descriptor, "__name__", "member")
_computed(_member_descriptor, "__doc__", lambda m: _cetta_field(m, "doc"))
_computed(_member_descriptor, "__qualname__",
          lambda m: _cetta_field(m, "owner").__qualname__ + "." + _cetta_field(m, "member"))


# range's and slice's start, stop and step are members (read-only); a slice's
# indices are the host's PySlice_Unpack and PySlice_AdjustIndices
def _slice_indices(self, length, /):
    return _cetta_slice_indices(self, length)


def _three_parts_reduce(self):
    return (type(self), (self.start, self.stop, self.step))


for _t in (range, slice):
    for _n in ("start", "stop", "step"):
        _member(_t, _n, _n)
    _cetta_set_type_attr(_t, "__reduce__", _three_parts_reduce)
_cetta_set_type_attr(slice, "indices", _slice_indices)


# ---------------------------------------------------------------- builtin types' own members

# the runtime's descriptors for what CPython writes in C: list.append,
# int.__add__, dict.fromkeys, ... (method_get, wrapperdescr_get, classmethod_get)
def _bmeth_name(d):
    return _cetta_bmeth_info(d, "name")


def _bmeth_qualname(d):
    return _cetta_bmeth_info(d, "qualname")


def _bmeth_objclass(d):
    return _cetta_bmeth_info(d, "objclass")


def _bmeth_self(d):
    return _cetta_bmeth_info(d, "self")


for _t in (_cetta_builtin_type("method_descriptor"), _cetta_builtin_type("wrapper_descriptor"),
           _cetta_builtin_type("classmethod_descriptor")):
    _cetta_set_type_attr(_t, "__get__", _bmeth_get)
    _computed(_t, "__name__", _bmeth_name)
    _computed(_t, "__qualname__", _bmeth_qualname)
    _computed(_t, "__objclass__", _bmeth_objclass)
_computed(_cetta_builtin_type("method-wrapper"), "__name__", _bmeth_name)
_computed(_cetta_builtin_type("method-wrapper"), "__self__", _bmeth_self)


def _bmeth_module(d):
    return _cetta_bmeth_info(d, "module")


# builtin functions and the builtin methods bound to a value (meth_getsets,
# meth_members): len.__name__, [].append.__qualname__, print.__self__
for _name, _f in (("__name__", _bmeth_name), ("__qualname__", _bmeth_qualname), ("__self__", _bmeth_self),
                  ("__module__", _bmeth_module)):
    _computed(_cetta_builtin_type("builtin_function_or_method"), _name, _f)


# ---------------------------------------------------------------- unions (PEP 604)

# Objects/unionobject.c: int | str, a typing.Union (types.UnionType since 3.14)
_union_type = _cetta_builtin_type("Union")
_generic_alias_type = _cetta_builtin_type("GenericAlias")
_typealias_type = _cetta_builtin_type("TypeAliasType")


def _make_union(args):
    u = _cetta_new_object(_union_type)
    _cetta_set_field(u, "args", args)
    _cetta_set_field(u, "parameters", _NULL)
    return u


def _union_is_unionable(obj):
    return (obj is None or isinstance(obj, type) or isinstance(obj, _generic_alias_type)
            or type(obj) is _union_type or type(obj) is _typealias_type or type(obj) is _sentinel)


def _union_types(obj):
    # get_types: None stands for its type; a union for its members
    if obj is None:
        obj = type(None)
    if type(obj) is _union_type:
        return obj, _cetta_field(obj, "args")
    return obj, (obj,)


def _union_is_same(left, right):
    if isinstance(left, _generic_alias_type) and isinstance(right, _generic_alias_type):
        return left == right
    return left is right


def _union_merge(items1, items2):
    # the members of items2 not already in items1, after them; None when
    # there are none
    result = None
    for arg in items2:
        duplicate = False
        for item in items1:
            if _union_is_same(item, arg):
                duplicate = True
                break
        if duplicate:
            continue
        if result is None:
            result = list(items1)
        result.append(arg)
    return None if result is None else tuple(result)


def _union_type_or(self, other):
    # _Py_union_type_or: the nb_or of types, unions, generic aliases and type
    # aliases
    if not _union_is_unionable(self) or not _union_is_unionable(other):
        return NotImplemented
    self, items1 = _union_types(self)
    other, items2 = _union_types(other)
    merged = _union_merge(items1, items2)
    if merged is None:
        return self
    return _make_union(merged)


def _union_type_ror(self, other):
    return _union_type_or(other, self)


def _typing_repr_item(p):
    # union_repr_item and ga_repr_item: a class by its qualified name, the
    # module first unless it is builtins; anything else by its repr
    if p is ...:
        return "..."
    if hasattr(p, "__origin__") and hasattr(p, "__args__"):
        return repr(p)
    qualname = getattr(p, "__qualname__", _NULL)
    if qualname is _NULL:
        return repr(p)
    module = getattr(p, "__module__", _NULL)
    if module is _NULL or module is None:
        return repr(p)
    if isinstance(module, str) and module == "builtins":
        return str(qualname)
    return str(module) + "." + str(qualname)


def _union_repr(self):
    out = ""
    for p in _cetta_field(self, "args"):
        if out:
            out += " | "
        out += "None" if p is type(None) else _typing_repr_item(p)
    return out


def _union_eq(a, b):
    if type(b) is not _union_type:
        return NotImplemented
    return set(_cetta_field(a, "args")) == set(_cetta_field(b, "args"))


def _union_ne(a, b):
    if type(b) is not _union_type:
        return NotImplemented
    return set(_cetta_field(a, "args")) != set(_cetta_field(b, "args"))


def _union_hash(self):
    return hash(frozenset(_cetta_field(self, "args")))


def _union_parameters(self):
    params = _cetta_field(self, "parameters")
    if params is _NULL:
        params = _make_parameters(_cetta_field(self, "args"))
        _cetta_set_field(self, "parameters", params)
    return params


def _union_getitem(self, item):
    newargs = _subs_parameters(self, _cetta_field(self, "args"), _union_parameters(self), item)
    if not newargs:
        return _make_union(newargs)
    res = newargs[0]
    for arg in newargs[1:]:
        res = res | arg
    return res


def _union_getattribute(self, name):
    # union_getattro: __module__ is its type's
    if isinstance(name, str) and name == "__module__":
        return getattr(type(self), name)
    return _object_getattribute(self, name)


for _name, _f in (("__or__", _union_type_or), ("__ror__", _union_type_ror), ("__repr__", _union_repr),
                  ("__eq__", _union_eq), ("__ne__", _union_ne), ("__hash__", _union_hash),
                  ("__getitem__", _union_getitem), ("__getattribute__", _union_getattribute)):
    _cetta_set_type_attr(_union_type, _name, _f)
_member(_union_type, "__args__", "args")
_computed(_union_type, "__parameters__", _union_parameters)
_cetta_set_type_attr(type, "__or__", _union_type_or)
_cetta_set_type_attr(type, "__ror__", _union_type_ror)


# ---------------------------------------------------------------- generic aliases (PEP 585)

# Objects/genericaliasobject.c: list[int], a types.GenericAlias
def _ga_setup(ga, origin, args, starred):
    if not isinstance(args, tuple):
        args = (args,)
    _cetta_set_field(ga, "origin", origin)
    _cetta_set_field(ga, "args", args)
    _cetta_set_field(ga, "parameters", _NULL)
    _cetta_set_field(ga, "starred", starred)
    return ga


def _generic_alias(origin, args):
    # Py_GenericAlias, the __class_getitem__ of the builtin generic classes
    return _ga_setup(_cetta_new_object(_generic_alias_type), origin, args, False)


def _ga_new(cls, *args, **kwargs):
    if kwargs:
        raise TypeError("GenericAlias() takes no keyword arguments")
    if len(args) != 2:
        raise TypeError("GenericAlias expected 2 arguments, got " + str(len(args)))
    return _ga_setup(_cetta_new_object(cls), args[0], args[1], False)


def _ga_repr(self):
    out = "*" if _cetta_field(self, "starred") else ""
    out += _typing_repr_item(_cetta_field(self, "origin")) + "["
    args = _cetta_field(self, "args")
    first = True
    for p in args:
        if not first:
            out += ", "
        first = False
        if type(p) is list:
            # a ParamSpec's list of argument types
            inner = ""
            for q in p:
                if inner:
                    inner += ", "
                inner += _typing_repr_item(q)
            out += "[" + inner + "]"
        else:
            out += _typing_repr_item(p)
    if not args:
        out += "()"
    return out + "]"


_GA_OWN = ("__class__", "__origin__", "__args__", "__unpacked__", "__parameters__",
           "__typing_unpacked_tuple_args__", "__mro_entries__", "__reduce_ex__", "__reduce__")
_GA_BLOCKED = ("__bases__", "__copy__", "__deepcopy__")


def _ga_getattribute(self, name):
    # ga_getattro: the alias's own attributes; any other is its origin's
    if isinstance(name, str) and name not in _GA_BLOCKED and name not in _GA_OWN:
        return getattr(_cetta_field(self, "origin"), name)
    return _object_getattribute(self, name)


def _ga_eq(a, b):
    if not isinstance(b, _generic_alias_type):
        return NotImplemented
    if _cetta_field(a, "starred") != _cetta_field(b, "starred"):
        return False
    if not (_cetta_field(a, "origin") == _cetta_field(b, "origin")):
        return False
    return _cetta_field(a, "args") == _cetta_field(b, "args")


def _ga_ne(a, b):
    if not isinstance(b, _generic_alias_type):
        return NotImplemented
    return _ga_eq(a, b) is not True


def _ga_hash(self):
    return hash(_cetta_field(self, "origin")) ^ hash(_cetta_field(self, "args"))


def _ga_call(self, *args, **kwargs):
    obj = _cetta_field(self, "origin")(*args, **kwargs)
    # set_orig_class
    try:
        obj.__orig_class__ = self
    except (AttributeError, TypeError):
        pass
    return obj


def _ga_mro_entries(self, bases):
    return (_cetta_field(self, "origin"),)


def _ga_parameters(self):
    params = _cetta_field(self, "parameters")
    if params is _NULL:
        params = _make_parameters(_cetta_field(self, "args"))
        _cetta_set_field(self, "parameters", params)
    return params


def _ga_unpacked_tuple_args(self):
    if _cetta_field(self, "starred") and _cetta_field(self, "origin") is tuple:
        return _cetta_field(self, "args")
    return None


def _ga_getitem(self, item):
    newargs = _subs_parameters(self, _cetta_field(self, "args"), _ga_parameters(self), item)
    res = _generic_alias(_cetta_field(self, "origin"), newargs)
    _cetta_set_field(res, "starred", _cetta_field(self, "starred"))
    return res


def _ga_iter(self):
    # *list[int]: the alias starred, once
    yield _ga_setup(_cetta_new_object(_generic_alias_type), _cetta_field(self, "origin"),
                    _cetta_field(self, "args"), True)


def _make_parameters(args):
    # _Py_make_parameters: the type variables the arguments hold, each once
    params = []
    for t in args:
        if isinstance(t, type):
            continue
        if hasattr(t, "__typing_subst__"):
            _identity_add(params, t)
        else:
            sub = getattr(t, "__parameters__", _NULL)
            if sub is not _NULL and isinstance(sub, tuple):
                for t2 in sub:
                    _identity_add(params, t2)
    return tuple(params)


def _identity_add(items, x):
    for y in items:
        if y is x:
            return None
    items.append(x)
    return None


def _identity_index(items, x):
    i = 0
    for y in items:
        if y is x:
            return i
        i += 1
    return -1


def _unpacked_tuple_args(arg):
    if (isinstance(arg, _generic_alias_type) and _cetta_field(arg, "starred")
            and _cetta_field(arg, "origin") is tuple):
        return _cetta_field(arg, "args")
    r = getattr(arg, "__typing_unpacked_tuple_args__", _NULL)
    if r is _NULL or r is None:
        return None
    return r


def _unpack_args(item):
    newargs = []
    for x in (item if isinstance(item, tuple) else (item,)):
        if not isinstance(x, type):
            sub = _unpacked_tuple_args(x)
            if sub is not None and isinstance(sub, tuple) and not (len(sub) and sub[-1] is ...):
                newargs.extend(sub)
                continue
        newargs.append(x)
    return tuple(newargs)


def _is_unpacked_typevartuple(arg):
    if isinstance(arg, type):
        return False
    r = getattr(arg, "__typing_is_unpacked_typevartuple__", _NULL)
    return r is not _NULL and bool(r)


def _subs_tvars(obj, params, argitems):
    # subs_tvars: a generic argument's own type variables substituted
    subparams = getattr(obj, "__parameters__", _NULL)
    if subparams is _NULL or not isinstance(subparams, tuple) or not subparams:
        return obj
    subargs = []
    for arg in subparams:
        iparam = _identity_index(params, arg)
        if iparam >= 0:
            param = params[iparam]
            arg = argitems[iparam]
            if _mro_lookup(type(param), "__iter__") is not _NULL and isinstance(arg, tuple):
                subargs.extend(arg)
                continue
        subargs.append(arg)
    return obj[tuple(subargs)]


def _subs_parameters(self, args, parameters, item):
    # _Py_subs_parameters: each of the parameters replaced by its argument
    nparams = len(parameters)
    if nparams == 0:
        raise TypeError(repr(self) + " is not a generic class")
    item = _unpack_args(item)
    for param in parameters:
        prepare = getattr(param, "__typing_prepare_subst__", _NULL)
        if prepare is not _NULL and prepare is not None:
            item = prepare(self, item if isinstance(item, tuple) else (item,))
    argitems = item if isinstance(item, tuple) else (item,)
    nitems = len(argitems)
    if nitems != nparams:
        raise TypeError("Too " + ("many" if nitems > nparams else "few") + " arguments for "
                        + repr(self) + "; actual " + str(nitems) + ", expected " + str(nparams))
    newargs = []
    for original in args:
        if isinstance(original, type):
            newargs.append(original)
            continue
        unpack = _is_unpacked_typevartuple(original)
        subst = getattr(original, "__typing_subst__", _NULL)
        if subst is not _NULL:
            arg = subst(argitems[_identity_index(parameters, original)])
        else:
            arg = _subs_tvars(original, parameters, argitems)
        if unpack:
            if not isinstance(arg, tuple):
                raise TypeError("expected __typing_subst__ of " + _type_name(type(original))
                                + " objects to return a tuple, not " + _type_name(type(arg)))
            newargs.extend(arg)
        else:
            newargs.append(arg)
    return tuple(newargs)


def _call_typing(name, *args, **kwargs):
    # call_typing_func_object, call_typing_args_kwargs: the typing module's
    # own function
    import typing
    return getattr(typing, name)(*args, **kwargs)


for _name, _f in (("__new__", staticmethod(_ga_new)), ("__repr__", _ga_repr),
                  ("__getattribute__", _ga_getattribute), ("__eq__", _ga_eq), ("__ne__", _ga_ne),
                  ("__hash__", _ga_hash), ("__call__", _ga_call), ("__mro_entries__", _ga_mro_entries),
                  ("__getitem__", _ga_getitem), ("__iter__", _ga_iter),
                  ("__or__", _union_type_or), ("__ror__", _union_type_ror)):
    _cetta_set_type_attr(_generic_alias_type, _name, _f)
_member(_generic_alias_type, "__origin__", "origin")
_member(_generic_alias_type, "__args__", "args")
_member(_generic_alias_type, "__unpacked__", "starred")
_computed(_generic_alias_type, "__parameters__", _ga_parameters)
_computed(_generic_alias_type, "__typing_unpacked_tuple_args__", _ga_unpacked_tuple_args)
for _t in (list, dict, tuple, set, frozenset, enumerate, _mappingproxy, BaseExceptionGroup):
    _cetta_set_type_attr(_t, "__class_getitem__", classmethod(_generic_alias))


# ---------------------------------------------------------------- type parameters (PEP 695)

# Objects/typevarobject.c: the TypeVar, ParamSpec, TypeVarTuple and
# TypeAliasType objects that generic definitions and the type statement make,
# through the compiler's intrinsics (_intrinsic_*).  What CPython asks of the
# typing module (substitution, unions with a type variable, Unpack), asks it
# here too.
_typevar_type = _cetta_builtin_type("TypeVar")
_paramspec_type = _cetta_builtin_type("ParamSpec")
_paramspecargs_type = _cetta_builtin_type("ParamSpecArgs")
_paramspeckwargs_type = _cetta_builtin_type("ParamSpecKwargs")
_typevartuple_type = _cetta_builtin_type("TypeVarTuple")
_nodefault_type = _cetta_builtin_type("NoDefaultType")
_NoDefault = _cetta_new_object(_nodefault_type)
_generic_type = _cetta_builtin_type("Generic")


def _nodefault_new(cls, *args, **kwargs):
    if args or kwargs:
        raise TypeError("NoDefaultType takes no arguments")
    return _NoDefault


def _nodefault_repr(self):
    return "typing.NoDefault"


def _nodefault_reduce(self):
    return "NoDefault"


def _typeparam_new(tp, name):
    p = _cetta_new_object(tp)
    _cetta_set_field(p, "name", name)
    _cetta_set_field(p, "default_value", _NULL)
    _cetta_set_field(p, "evaluate_default", _NULL)
    return p


def _typevar_alloc(name, evaluate_bound, evaluate_constraints):
    # typevar_alloc for _Py_make_typevar: variance inferred, nothing evaluated
    tv = _typeparam_new(_typevar_type, name)
    _cetta_set_field(tv, "bound", _NULL)
    _cetta_set_field(tv, "evaluate_bound", evaluate_bound)
    _cetta_set_field(tv, "constraints", _NULL)
    _cetta_set_field(tv, "evaluate_constraints", evaluate_constraints)
    _cetta_set_field(tv, "covariant", False)
    _cetta_set_field(tv, "contravariant", False)
    _cetta_set_field(tv, "infer_variance", True)
    return tv


def _lazy_field(p, field, evaluate, absent):
    # a value kept once its evaluate function has given it
    v = _cetta_field(p, field)
    if v is not _NULL:
        return v
    f = _cetta_field(p, evaluate)
    if f is _NULL:
        return absent
    v = f()
    _cetta_set_field(p, field, v)
    return v


def _typevar_bound(self):
    return _lazy_field(self, "bound", "evaluate_bound", None)


def _typevar_constraints(self):
    return _lazy_field(self, "constraints", "evaluate_constraints", ())


def _typeparam_default(self):
    return _lazy_field(self, "default_value", "evaluate_default", _NoDefault)


def _typeparam_has_default(self):
    if _cetta_field(self, "evaluate_default") is not _NULL:
        return True
    d = _cetta_field(self, "default_value")
    return d is not _NULL and d is not _NoDefault


def _variance_repr(self):
    # typevar_repr, paramspec_repr
    name = _cetta_field(self, "name")
    if _cetta_field(self, "infer_variance"):
        return name
    if _cetta_field(self, "covariant"):
        return "+" + name
    if _cetta_field(self, "contravariant"):
        return "-" + name
    return "~" + name


def _typeparam_reduce(self):
    return _cetta_field(self, "name")


def _typevar_typing_subst(self, arg):
    return _call_typing("_typevar_subst", self, arg)


def _typevar_typing_prepare_subst(self, alias, args):
    params = alias.__parameters__
    i = params.index(self)
    if i < len(args):
        return args
    if i == len(args):
        dflt = _typeparam_default(self)
        if dflt is not _NoDefault:
            return args + (dflt,)
    raise TypeError("Too few arguments for " + str(alias) + "; actual " + str(len(args))
                    + ", expected at least " + str(i + 1))


def _make_typing_union(self, other):
    # make_union: a type variable's |, the typing module's Union
    return _call_typing("_make_union", self, other)


def _make_typing_union_r(self, other):
    return _call_typing("_make_union", other, self)


def _subclass_refused(kind):
    def refuse(self, bases):
        raise TypeError("Cannot subclass an instance of " + kind)
    return refuse


def _paramspec_alloc(name):
    ps = _typeparam_new(_paramspec_type, name)
    _cetta_set_field(ps, "bound", None)
    _cetta_set_field(ps, "covariant", False)
    _cetta_set_field(ps, "contravariant", False)
    _cetta_set_field(ps, "infer_variance", True)
    return ps


def _paramspec_attr(tp, origin):
    a = _cetta_new_object(tp)
    _cetta_set_field(a, "origin", origin)
    return a


def _paramspec_args(self):
    return _paramspec_attr(_paramspecargs_type, self)


def _paramspec_kwargs(self):
    return _paramspec_attr(_paramspeckwargs_type, self)


def _paramspecattr_repr(self, suffix):
    origin = _cetta_field(self, "origin")
    if type(origin) is _paramspec_type:
        return _cetta_field(origin, "name") + suffix
    return repr(origin) + suffix


def _paramspecargs_repr(self):
    return _paramspecattr_repr(self, ".args")


def _paramspeckwargs_repr(self):
    return _paramspecattr_repr(self, ".kwargs")


def _paramspecattr_eq(a, b):
    if type(a) is not type(b):
        return NotImplemented
    return _cetta_field(a, "origin") == _cetta_field(b, "origin")


def _paramspecattr_ne(a, b):
    if type(a) is not type(b):
        return NotImplemented
    return _cetta_field(a, "origin") != _cetta_field(b, "origin")


def _paramspec_typing_subst(self, arg):
    return _call_typing("_paramspec_subst", self, arg)


def _paramspec_typing_prepare_subst(self, alias, args):
    return _call_typing("_paramspec_prepare_subst", self, alias, args)


def _typevartuple_alloc(name):
    # typevartuple_alloc for _Py_make_typevartuple: no bound, variance inferred
    tvt = _typeparam_new(_typevartuple_type, name)
    _cetta_set_field(tvt, "bound", None)
    _cetta_set_field(tvt, "covariant", False)
    _cetta_set_field(tvt, "contravariant", False)
    _cetta_set_field(tvt, "infer_variance", True)
    return tvt


def _caller_module():
    # caller(): the module of the code running, else None
    try:
        return _cetta_frame(0).f_globals.get("__name__")
    except ValueError:
        return None


def _typeparam_args(fname, args, kwargs, names, varpos):
    # a type parameter's constructor arguments (Argument Clinic): the name a
    # str, the variances read as truths and not contradicting each other
    vals = _clinic_args(fname, names, 1, 1, args, kwargs, varpos=varpos)
    name = vals[0]
    if not isinstance(name, str):
        raise TypeError(fname + "() argument 'name' must be str, not " + _arg_type_name(name))
    got = {}
    for k, v in zip(names, vals):
        got[k] = v
    covariant = got["covariant"] is not _NULL and bool(got["covariant"])
    contravariant = got["contravariant"] is not _NULL and bool(got["contravariant"])
    infer_variance = got["infer_variance"] is not _NULL and bool(got["infer_variance"])
    if covariant and contravariant:
        raise ValueError("Bivariant types are not supported.")
    if infer_variance and (covariant or contravariant):
        raise ValueError("Variance cannot be specified with infer_variance.")
    bound = None if got["bound"] is _NULL else got["bound"]
    default = _NoDefault if got["default"] is _NULL else got["default"]
    return name, bound, default, covariant, contravariant, infer_variance


def _typeparam_made(p, bound, default, covariant, contravariant, infer_variance):
    # a constructed type parameter: its fields, and the caller's module
    _cetta_set_field(p, "bound", bound)
    _cetta_set_field(p, "default_value", default)
    _cetta_set_field(p, "covariant", covariant)
    _cetta_set_field(p, "contravariant", contravariant)
    _cetta_set_field(p, "infer_variance", infer_variance)
    module = _caller_module()
    if module is not None:
        _cetta_object_dict(p)["__module__"] = module
    return p


def _typevar_new(cls, *args, **kwargs):
    # typevar_new_impl: TypeVar(name, *constraints, bound=None,
    # default=NoDefault, covariant=False, contravariant=False,
    # infer_variance=False)
    name, bound, default, co, contra, infer = _typeparam_args(
        "typevar", args, kwargs, ("name", "bound", "default", "covariant", "contravariant", "infer_variance"), True)
    if bound is not None:
        bound = _call_typing("_type_check", bound, "Bound must be a type.")
    constraints = tuple(args[1:])
    if len(constraints) == 1:
        raise TypeError("A single constraint is not allowed")
    if constraints and bound is not None:
        raise TypeError("Constraints cannot be combined with bound=...")
    tv = _typevar_alloc(name, _NULL, _NULL)
    _typeparam_made(tv, _NULL if bound is None else bound, default, co, contra, infer)
    if constraints:
        _cetta_set_field(tv, "constraints", constraints)
    return tv


def _paramspec_new(cls, *args, **kwargs):
    # paramspec_new_impl: ParamSpec(name, *, bound=None, default=NoDefault,
    # covariant=False, contravariant=False, infer_variance=False)
    name, bound, default, co, contra, infer = _typeparam_args(
        "paramspec", args, kwargs, ("name", "bound", "default", "covariant", "contravariant", "infer_variance"), False)
    return _typeparam_made(_paramspec_alloc(name), bound, default, co, contra, infer)


def _typevartuple_new(cls, *args, **kwargs):
    # typevartuple_impl: TypeVarTuple(name, *, bound=None, covariant=False,
    # contravariant=False, infer_variance=False, default=NoDefault)
    name, bound, default, co, contra, infer = _typeparam_args(
        "typevartuple", args, kwargs, ("name", "bound", "covariant", "contravariant", "infer_variance", "default"), False)
    return _typeparam_made(_typevartuple_alloc(name), bound, default, co, contra, infer)


def _typevartuple_repr(self):
    # typevartuple_repr: the name, marked by its variance when not inferred
    return _variance_repr(self)


def _typevartuple_iter(self):
    # *Ts: the typing module's Unpack[Ts]
    import typing
    yield typing.Unpack[self]


def _typevartuple_typing_subst(self, arg):
    raise TypeError("Substitution of bare TypeVarTuple is not supported")


def _typevartuple_typing_prepare_subst(self, alias, args):
    return _call_typing("_typevartuple_prepare_subst", self, alias, args)


# the compiler's intrinsics (Python/intrinsics.c)
def _intrinsic_typevar(name):
    return _typevar_alloc(name, _NULL, _NULL)


def _intrinsic_typevar_with_bound(name, evaluate_bound):
    return _typevar_alloc(name, evaluate_bound, _NULL)


def _intrinsic_typevar_with_constraints(name, evaluate_constraints):
    return _typevar_alloc(name, _NULL, evaluate_constraints)


def _intrinsic_paramspec(name):
    return _paramspec_alloc(name)


def _intrinsic_typevartuple(name):
    return _typevartuple_alloc(name)


def _intrinsic_set_typeparam_default(param, evaluate_default):
    # _Py_set_typeparam_default
    if type(param) in (_typevar_type, _paramspec_type, _typevartuple_type):
        _cetta_set_field(param, "evaluate_default", evaluate_default)
        return param
    raise TypeError("Expected a type param, got " + repr(param))


def _intrinsic_set_function_type_params(func, type_params):
    func.__type_params__ = type_params
    return func


def _intrinsic_subscript_generic(params):
    # INTRINSIC_SUBSCRIPT_GENERIC (_Py_subscript_generic): a generic class's
    # base, typing._GenericAlias(Generic, params), each TypeVarTuple unpacked
    return _call_typing("_GenericAlias", _generic_type, _unpack_typevartuples(params))


def _generic_class_getitem_hook(cls, *args, **kwargs):
    # Generic.__class_getitem__ (generic_class_getitem): the typing module's
    return _call_typing("_generic_class_getitem", cls, *args, **kwargs)


def _generic_init_subclass_hook(cls, *args, **kwargs):
    # Generic.__init_subclass__ (generic_init_subclass): the typing module's
    return _call_typing("_generic_init_subclass", cls, *args, **kwargs)


def _intrinsic_unpack_one(value):
    # UNPACK_SEQUENCE 1: a TypeVarTuple's default *X is X's one item
    (item,) = value
    return item


def _intrinsic_typealias(name, type_params, compute_value):
    # _Py_make_typealias: the qualified name is the value function's, which
    # the compiler gave the alias's place (f.<locals>.A, C.A)
    ta = _cetta_new_object(_typealias_type)
    _cetta_set_field(ta, "name", name)
    _cetta_set_field(ta, "qualname", name if compute_value is _NULL else _cetta_field(compute_value, "qualname"))
    if type_params is None or (isinstance(type_params, tuple) and not type_params):
        type_params = _NULL
    _cetta_set_field(ta, "type_params", type_params)
    _cetta_set_field(ta, "compute_value", compute_value)
    _cetta_set_field(ta, "value", _NULL)
    _cetta_set_field(ta, "module", _NULL)
    return ta


def _typealias_new(cls, name, value, *, type_params=_NULL, qualname=None):
    # typealias_new_impl: TypeAliasType(name, value, *, type_params=(), qualname=None)
    if type(name) is not str and not isinstance(name, str):
        raise TypeError("TypeAliasType() argument 'name' must be str, not " + _type_name(type(name)))
    if type_params is not _NULL and not isinstance(type_params, tuple):
        raise TypeError("type_params must be a tuple")
    if qualname is not None and not isinstance(qualname, str):
        raise TypeError("qualname must be a string")
    ta = _intrinsic_typealias(name, type_params, _NULL)
    if qualname is not None:
        _cetta_set_field(ta, "qualname", qualname)
    _cetta_set_field(ta, "value", value)
    return ta


def _typealias_value(self):
    v = _cetta_field(self, "value")
    if v is _NULL:
        v = _cetta_field(self, "compute_value")()
        _cetta_set_field(self, "value", v)
    return v


def _typealias_type_params(self):
    tps = _cetta_field(self, "type_params")
    return () if tps is _NULL else tps


def _unpack_typevartuples(params):
    # a TypeVarTuple among the parameters stands as typing.Unpack of it
    if any(type(p) is _typevartuple_type for p in params):
        import typing
        return tuple(typing.Unpack[p] if type(p) is _typevartuple_type else p for p in params)
    return params


def _typealias_parameters(self):
    tps = _cetta_field(self, "type_params")
    return () if tps is _NULL else _unpack_typevartuples(tps)


def _typealias_module(self):
    m = _cetta_field(self, "module")
    if m is not _NULL:
        return m
    f = _cetta_field(self, "compute_value")
    if f is not _NULL:
        mod = getattr(f, "__module__", None)
        if mod is not None:
            return mod
    return None


def _typealias_repr(self):
    return _cetta_field(self, "qualname")


def _typealias_getitem(self, args):
    if _cetta_field(self, "type_params") is _NULL:
        raise TypeError("Only generic type aliases are subscriptable")
    return _generic_alias(self, args)


for _name, _f in (("__new__", staticmethod(_nodefault_new)), ("__repr__", _nodefault_repr),
                  ("__reduce__", _nodefault_reduce)):
    _cetta_set_type_attr(_nodefault_type, _name, _f)
for _t, _kind, _subst, _prepare in ((_typevar_type, "TypeVar", _typevar_typing_subst, _typevar_typing_prepare_subst),
                                    (_paramspec_type, "ParamSpec", _paramspec_typing_subst, _paramspec_typing_prepare_subst),
                                    (_typevartuple_type, "TypeVarTuple", _typevartuple_typing_subst,
                                     _typevartuple_typing_prepare_subst)):
    for _name, _f in (("__reduce__", _typeparam_reduce), ("has_default", _typeparam_has_default),
                      ("__typing_subst__", _subst), ("__typing_prepare_subst__", _prepare),
                      ("__mro_entries__", _subclass_refused(_kind))):
        _cetta_set_type_attr(_t, _name, _f)
    _cetta_set_type_attr(_t, "__module__", "typing")
    _member(_t, "__name__", "name")
    _computed(_t, "__default__", _typeparam_default)
for _t in (_typevar_type, _paramspec_type):
    _cetta_set_type_attr(_t, "__repr__", _variance_repr)
    _cetta_set_type_attr(_t, "__or__", _make_typing_union)
    _cetta_set_type_attr(_t, "__ror__", _make_typing_union_r)
    _member(_t, "__covariant__", "covariant")
    _member(_t, "__contravariant__", "contravariant")
    _member(_t, "__infer_variance__", "infer_variance")
_computed(_typevar_type, "__bound__", _typevar_bound)
_computed(_typevar_type, "__constraints__", _typevar_constraints)
_member(_paramspec_type, "__bound__", "bound")
_computed(_paramspec_type, "args", _paramspec_args)
_computed(_paramspec_type, "kwargs", _paramspec_kwargs)
_cetta_set_type_attr(_typevartuple_type, "__repr__", _typevartuple_repr)
_cetta_set_type_attr(_typevartuple_type, "__iter__", _typevartuple_iter)
for _name, _field in (("__bound__", "bound"), ("__covariant__", "covariant"), ("__contravariant__", "contravariant"),
                      ("__infer_variance__", "infer_variance")):
    _member(_typevartuple_type, _name, _field)
for _t, _f in ((_typevar_type, _typevar_new), (_paramspec_type, _paramspec_new),
               (_typevartuple_type, _typevartuple_new)):
    _cetta_set_type_attr(_t, "__new__", staticmethod(_f))
for _t, _kind, _r in ((_paramspecargs_type, "ParamSpecArgs", _paramspecargs_repr),
                      (_paramspeckwargs_type, "ParamSpecKwargs", _paramspeckwargs_repr)):
    for _name, _f in (("__repr__", _r), ("__eq__", _paramspecattr_eq), ("__ne__", _paramspecattr_ne),
                      ("__mro_entries__", _subclass_refused(_kind))):
        _cetta_set_type_attr(_t, _name, _f)
    _cetta_set_type_attr(_t, "__module__", "typing")
    _member(_t, "__origin__", "origin")
for _name, _f in (("__new__", staticmethod(_typealias_new)), ("__repr__", _typealias_repr),
                  ("__reduce__", _typeparam_reduce), ("__getitem__", _typealias_getitem),
                  ("__or__", _union_type_or), ("__ror__", _union_type_ror)):
    _cetta_set_type_attr(_typealias_type, _name, _f)
_member(_typealias_type, "__name__", "name")
_member(_typealias_type, "__qualname__", "qualname")
_computed(_typealias_type, "__value__", _typealias_value)
_computed(_typealias_type, "__type_params__", _typealias_type_params)
_computed(_typealias_type, "__parameters__", _typealias_parameters)
_computed(_typealias_type, "__module__", _typealias_module)
# typing.Generic (typevarobject.c): subscription and subclassing are the
# typing module's
_cetta_set_type_attr(_generic_type, "__class_getitem__", classmethod(_generic_class_getitem_hook))
_cetta_set_type_attr(_generic_type, "__init_subclass__", classmethod(_generic_init_subclass_hook))


def _typing_idfunc(_, x, /):
    # _typing._idfunc
    return x


def _make_typing_module():
    # the _typing module (Modules/_typingmodule.c): the typing machinery's
    # types the interpreter builds in, for the typing module
    m = _new_module("_typing")
    _cetta_set_field(m, "builtin", True)
    d = _cetta_object_dict(m)
    d["__doc__"] = "Primitives and accelerators for the typing module.\n"
    for name, value in (("_idfunc", _typing_idfunc), ("TypeVar", _typevar_type), ("ParamSpec", _paramspec_type),
                        ("ParamSpecArgs", _paramspecargs_type), ("ParamSpecKwargs", _paramspeckwargs_type),
                        ("TypeVarTuple", _typevartuple_type), ("TypeAliasType", _typealias_type),
                        ("Generic", _generic_type), ("Union", _union_type), ("NoDefault", _NoDefault)):
        d[name] = value
    return m


# ---------------------------------------------------------------- property

def _property_new(cls, *args, **kwargs):
    return _cetta_new_object(cls)


def _property_init(self, fget=None, fset=None, fdel=None, doc=None):
    _cetta_set_field(self, "fget", fget)
    _cetta_set_field(self, "fset", fset)
    _cetta_set_field(self, "fdel", fdel)
    if doc is None and fget is not None:
        doc = getattr(fget, "__doc__", None)
    _cetta_set_field(self, "doc", doc)
    return None


def _property_name(self, obj):
    # property_name: the name it was given (__set_name__), else its getter's
    # __name__; without either, "property of 'C' object", C the qualname
    # (PyType_GetQualName)
    name = _cetta_field(self, "name")
    qualname = _cetta_type_field(type(obj), "qualname")
    if name is None:
        fget = _cetta_field(self, "fget")
        if fget is not None:
            name = getattr(fget, "__name__", None)
    if name is None:
        return "property of " + repr(qualname) + " object"
    return "property " + repr(name) + " of " + repr(qualname) + " object"


def _property_get(self, obj, tp=None):
    if obj is None:
        return self
    fget = _cetta_field(self, "fget")
    if fget is None:
        raise AttributeError(_property_name(self, obj) + " has no getter")
    return fget(obj)


def _property_set(self, obj, value):
    fset = _cetta_field(self, "fset")
    if fset is None:
        raise AttributeError(_property_name(self, obj) + " has no setter")
    fset(obj, value)
    return None


def _property_delete(self, obj):
    fdel = _cetta_field(self, "fdel")
    if fdel is None:
        raise AttributeError(_property_name(self, obj) + " has no deleter")
    fdel(obj)
    return None


def _property_set_name(self, owner, name):
    _cetta_set_field(self, "name", name)
    return None


def _property_copy(self, fget, fset, fdel):
    p = type(self)(fget, fset, fdel, _cetta_field(self, "doc"))
    _cetta_set_field(p, "name", _cetta_field(self, "name"))
    return p


def _property_getter(self, f):
    return _property_copy(self, f, _cetta_field(self, "fset"), _cetta_field(self, "fdel"))


def _property_setter(self, f):
    return _property_copy(self, _cetta_field(self, "fget"), f, _cetta_field(self, "fdel"))


def _property_deleter(self, f):
    return _property_copy(self, _cetta_field(self, "fget"), _cetta_field(self, "fset"), f)


# ---------------------------------------------------------------- exceptions

def _exception_new(cls, *args, **kwargs):
    return _cetta_exception_new(cls, args)


def _exception_init(self, *args, **kwargs):
    if kwargs:
        raise TypeError(_type_name(type(self)) + "() takes no keyword arguments")
    _cetta_set_field(self, "args", args)
    return None


# OSError (Objects/exceptions.c): OSError_new reads two to five arguments as
# errno, strerror, filename, winerror and filename2 (args keeps the first two
# when a filename is given), OSError itself becoming the subclass its errno
# names (the errno numbers of Linux); a subclass with its own __init__ (and
# OSError's __new__) has them read by OSError.__init__ instead
_ERRNO_TYPES = {}


def _oserror_uses_init(cls):
    return (_mro_lookup(cls, "__init__") is not _mro_lookup(OSError, "__init__")
            and _mro_lookup(cls, "__new__") is _mro_lookup(OSError, "__new__"))


def _oserror_fields(self, args):
    # oserror_parse_args, oserror_init
    n = len(args)
    if n < 2 or n > 5:
        return
    _cetta_set_field(self, "errno", args[0])
    _cetta_set_field(self, "strerror", args[1])
    filename = args[2] if n >= 3 else None
    if filename is not None:
        _cetta_set_field(self, "filename", filename)
        if n == 5 and args[4] is not None:
            _cetta_set_field(self, "filename2", args[4])
        _cetta_set_field(self, "args", args[:2])


def _oserror_new(cls, *args, **kwargs):
    if _oserror_uses_init(cls):
        return _cetta_exception_new(cls, ())
    if kwargs:
        raise TypeError(_type_name(cls) + "() takes no keyword arguments")
    if cls is OSError and len(args) >= 2 and len(args) <= 5 and isinstance(args[0], int):
        cls = _ERRNO_TYPES.get(args[0], OSError)
    self = _cetta_exception_new(cls, args)
    _oserror_fields(self, args)
    return self


def _oserror_init(self, *args, **kwargs):
    if not _oserror_uses_init(type(self)):
        return None
    if kwargs:
        raise TypeError(_type_name(type(self)) + "() takes no keyword arguments")
    _cetta_set_field(self, "args", args)
    _oserror_fields(self, args)
    return None


def _oserror_str(self):
    # OSError_str: "[Errno N] strerror: 'filename'" (and " -> 'filename2'")
    filename = self.filename
    if filename is not None:
        head = "[Errno " + str(self.errno) + "] " + str(self.strerror) + ": " + repr(filename)
        if self.filename2 is not None:
            return head + " -> " + repr(self.filename2)
        return head
    if self.errno is not None and self.strerror is not None:
        return "[Errno " + str(self.errno) + "] " + str(self.strerror)
    return _exception_str(self)


def _exception_add_note(self, note):
    # BaseException.add_note
    if not isinstance(note, str):
        raise TypeError("note must be a str, not '" + type(note).__name__ + "'")
    try:
        notes = self.__notes__
    except AttributeError:
        notes = []
        self.__notes__ = notes
    if not isinstance(notes, list):
        raise TypeError("Cannot add note: __notes__ is not a list")
    notes.append(note)


def _exception_str(self):
    return _cetta_exception_str(self)


def _exception_repr(self):
    return _cetta_exception_repr(self)


# ---------------------------------------------------------------- operators

def _dunder(x, name, *args):
    # a special method looked up on the class (never the instance) and called;
    # NotImplemented when the class has none
    f = _mro_lookup(type(x), name)
    if f is _NULL:
        return NotImplemented
    if type(f) is _function:
        return f(x, *args)
    get = _mro_lookup(type(f), "__get__")
    if get is not _NULL:
        f = get(f, x, type(x))
    return f(*args)


def _overrides(sub, base, name):
    return _mro_lookup(sub, name) is not _mro_lookup(base, name)


# The builtin sequences' + and * are their sequence slots (sq_concat,
# sq_repeat and the in-place forms), not number slots: an operator tries the
# number slots of both operands first, a sequence's own last (PyNumber_Add,
# PyNumber_Multiply).
_SEQ_SLOT_NAMES = ("__add__", "__mul__", "__rmul__", "__iadd__", "__imul__")
# the types whose C structure has sequence methods, repeat or not
# (PyNumber_InPlaceMultiply asks the right operand only when the left has none)
_SEQUENCE_METHOD_TYPES = (list, tuple, str, bytes, bytearray, dict, set, frozenset, range)


def _number_dunder(x, name, other):
    # an operator's number slot: the method NAME of x's class, called;
    # NotImplemented when there is none (a sequence's slot wrapper is none)
    f = _mro_lookup(type(x), name)
    if f is _NULL or (name in _SEQ_SLOT_NAMES and _cetta_is_seq_slot(f)):
        return NotImplemented
    if type(f) is _function:
        return f(x, other)
    get = _mro_lookup(type(f), "__get__")
    if get is not _NULL:
        f = get(f, x, type(x))
    return f(other)


def _seq_slot(t, name):
    # the sequence slot a class has for NAME: its builtin sequence's slot
    # wrapper, unless a class on the way defines NAME itself
    f = _mro_lookup(t, name)
    if f is not _NULL and _cetta_is_seq_slot(f):
        return f
    return _NULL


def _sequence_repeat(seq, n, repeat):
    # sequence_repeat: N's index the count, else the TypeError
    if not isinstance(n, int) and _mro_lookup(type(n), "__index__") is _NULL:
        raise TypeError("can't multiply sequence by non-int of type '" + _type_name(type(n)) + "'")
    count = _index(n)
    if count > 9223372036854775807 or count < -9223372036854775808:
        raise OverflowError("cannot fit '" + _type_name(type(n)) + "' into an index-sized integer")
    return repeat(seq, count)


def _binary_op1(a, b, name, rname):
    # binary_op1: the right operand's reflected method first when its class
    # is a proper subclass that overrides it; else the left's method, then
    # the right's reflected one when the classes differ
    ta = type(a)
    tb = type(b)
    reflected_tried = False
    if tb is not ta and issubclass(tb, ta) and _overrides(tb, ta, rname):
        reflected_tried = True
        r = _number_dunder(b, rname, a)
        if r is not NotImplemented:
            return r
    r = _number_dunder(a, name, b)
    if r is not NotImplemented:
        return r
    if tb is not ta and not reflected_tried:
        return _number_dunder(b, rname, a)
    return NotImplemented


def _binary_op(a, b, name, rname, symbol):
    # PyNumber_Add and its kin: binary_op1, then for + the left operand's
    # concatenation, for * the left's repetition, else the right's
    r = _binary_op1(a, b, name, rname)
    if r is not NotImplemented:
        return r
    if symbol == "+":
        f = _seq_slot(type(a), "__add__")
        if f is not _NULL:
            return f(a, b)
    elif symbol == "*":
        f = _seq_slot(type(a), "__mul__")
        if f is not _NULL:
            return _sequence_repeat(a, b, f)
        f = _seq_slot(type(b), "__mul__")
        if f is not _NULL:
            return _sequence_repeat(b, a, f)
    raise TypeError("unsupported operand type(s) for " + symbol + ": '" + _type_name(type(a))
                    + "' and '" + _type_name(type(b)) + "'")


def _inplace_op(a, b, iname, name, rname, symbol):
    # PyNumber_InPlaceAdd and its kin: the in-place number slot, binary_op1,
    # then for += the left's in-place concatenation (else its concatenation),
    # for *= the left's in-place repetition (else its repetition), or when
    # the left has no sequence methods the right's repetition
    r = _number_dunder(a, iname, b)
    if r is not NotImplemented:
        return r
    r = _binary_op1(a, b, name, rname)
    if r is not NotImplemented:
        return r
    if symbol == "+":
        f = _seq_slot(type(a), "__iadd__")
        if f is _NULL:
            f = _seq_slot(type(a), "__add__")
        if f is not _NULL:
            return f(a, b)
    elif symbol == "*":
        f = _seq_slot(type(a), "__imul__")
        if f is _NULL:
            f = _seq_slot(type(a), "__mul__")
        if f is not _NULL:
            return _sequence_repeat(a, b, f)
        if not issubclass(type(a), _SEQUENCE_METHOD_TYPES):
            f = _seq_slot(type(b), "__mul__")
            if f is not _NULL:
                return _sequence_repeat(b, a, f)
    text = "**=" if symbol == "** or pow()" else symbol + "="
    raise TypeError("unsupported operand type(s) for " + text + ": '" + _type_name(type(a))
                    + "' and '" + _type_name(type(b)) + "'")


_swapped = {"__lt__": "__gt__", "__gt__": "__lt__", "__le__": "__ge__", "__ge__": "__le__",
            "__eq__": "__eq__", "__ne__": "__ne__"}
_compare_text = {"__lt__": "<", "__gt__": ">", "__le__": "<=", "__ge__": ">="}


def _richcompare(a, b, name):
    # do_richcompare: the right operand's swapped method first when its class
    # is a proper subclass; the default for == and != is identity
    ta = type(a)
    tb = type(b)
    swapped = _swapped[name]
    checked = False
    if tb is not ta and issubclass(tb, ta):
        checked = True
        r = _dunder(b, swapped, a)
        if r is not NotImplemented:
            return r
    r = _dunder(a, name, b)
    if r is not NotImplemented:
        return r
    if not checked:
        r = _dunder(b, swapped, a)
        if r is not NotImplemented:
            return r
    if name == "__eq__":
        return a is b
    if name == "__ne__":
        return a is not b
    raise TypeError("'" + _compare_text[name] + "' not supported between instances of '"
                    + _type_name(ta) + "' and '" + _type_name(tb) + "'")


def _eq(a, b):
    # PyObject_RichCompareBool: identity first, then == and its truth
    if a is b:
        return True
    return _truth(_richcompare(a, b, "__eq__"))


def _unary_op(x, name, symbol):
    r = _dunder(x, name)
    if r is NotImplemented and _mro_lookup(type(x), name) is _NULL:
        raise TypeError("bad operand type for " + symbol + ": '" + _type_name(type(x)) + "'")
    return r


def _index_value(x, what):
    if type(x) is int or type(x) is bool:
        return x
    f = _mro_lookup(type(x), "__index__")
    if f is _NULL:
        raise TypeError("'" + _type_name(type(x)) + "' object cannot be interpreted as an integer")
    return f(x)


def _truth(x):
    # slot_nb_bool: __bool__, which must give a bool; else __len__; else true
    if x is True or x is False:
        return x
    if not _cetta_is_python_object(x):
        return _cetta_native_truth(x)
    f = _mro_lookup(type(x), "__bool__")
    if f is not _NULL:
        r = _dunder(x, "__bool__")
        if r is not True and r is not False:
            raise TypeError(_fqn(type(x)) + ".__bool__() must return a bool, not " + _fqn(type(r)))
        return r
    if _mro_lookup(type(x), "__len__") is not _NULL:
        return _len(x) != 0
    return True


def _len(x):
    if _mro_lookup(type(x), "__len__") is _NULL:
        raise TypeError("object of type '" + _type_name(type(x)) + "' has no len()")
    n = _index_value(_dunder(x, "__len__"), "len")
    if n < 0:
        raise ValueError("__len__() should return >= 0")
    return n


def _iter(x):
    # PyObject_GetIter: __iter__, which must give an iterator; else the old
    # protocol of __getitem__ from 0
    f = _mro_lookup(type(x), "__iter__")
    if f is not _NULL:
        if f is None:
            raise TypeError("'" + _type_name(type(x)) + "' object is not iterable")
        it = _dunder(x, "__iter__")
        if _mro_lookup(type(it), "__next__") is _NULL and _cetta_is_python_object(it):
            raise TypeError(_fqn(type(x)) + ".__iter__() must return an iterator, not " + _fqn(type(it)))
        return it
    if _mro_lookup(type(x), "__getitem__") is not _NULL:
        return _cetta_sequence_iterator(x)
    raise TypeError("'" + _type_name(type(x)) + "' object is not iterable")


def _next(it):
    if not _cetta_is_python_object(it):
        return _cetta_native_next(it)
    if _mro_lookup(type(it), "__next__") is _NULL:
        raise TypeError("'" + _type_name(type(it)) + "' object is not an iterator")
    return _dunder(it, "__next__")


def _contains(container, item):
    if _mro_lookup(type(container), "__contains__") is not _NULL:
        return _truth(_dunder(container, "__contains__", item))
    it = _iter(container)
    while True:
        try:
            x = _next(it)
        except StopIteration:
            return False
        if _eq(x, item):
            return True


def _getitem(container, key):
    if _mro_lookup(type(container), "__getitem__") is _NULL:
        if container is type:
            # PyObject_GetItem's special case: type[int]
            return _generic_alias(type, key)
        if isinstance(container, type):
            f = getattr(container, "__class_getitem__", _NULL)
            if f is not _NULL:
                return f(key)
            raise TypeError("type '" + _type_name(container) + "' is not subscriptable")
        raise TypeError("'" + _type_name(type(container)) + "' object is not subscriptable")
    return _dunder(container, "__getitem__", key)


def _setitem(container, key, value):
    if _mro_lookup(type(container), "__setitem__") is _NULL:
        raise TypeError("'" + _type_name(type(container)) + "' object does not support item assignment")
    _dunder(container, "__setitem__", key, value)
    return None


def _delitem(container, key):
    if _mro_lookup(type(container), "__delitem__") is _NULL:
        raise TypeError("'" + _type_name(type(container)) + "' object does not support item deletion")
    _dunder(container, "__delitem__", key)
    return None


def _hash(x):
    f = _mro_lookup(type(x), "__hash__")
    if f is None or f is _NULL:
        raise TypeError("unhashable type: '" + _type_name(type(x)) + "'")
    h = _dunder(x, "__hash__")
    if type(h) is not int and type(h) is not bool:
        raise TypeError("__hash__ method should return an integer")
    return h


def _abs(x):
    return _unary_op(x, "__abs__", "abs()")


# ---------------------------------------------------------------- the builtin types' namespaces

_object_new_static = staticmethod(_object_new)
_cetta_set_type_attr(object, "__new__", _object_new_static)
_cetta_set_type_attr(object, "__init__", _object_init)
_cetta_set_type_attr(object, "__getattribute__", _object_getattribute)
_cetta_set_type_attr(object, "__setattr__", _object_setattr)
_cetta_set_type_attr(object, "__delattr__", _object_delattr)
_cetta_set_type_attr(object, "__repr__", _object_repr)
_cetta_set_type_attr(object, "__str__", _object_str)
_cetta_set_type_attr(object, "__eq__", _object_eq)
_cetta_set_type_attr(object, "__ne__", _object_ne)
_cetta_set_type_attr(object, "__hash__", _object_hash)
_cetta_set_type_attr(object, "__init_subclass__", classmethod(_object_init_subclass))

# ---------------------------------------------------------------- formatting

def _format(value, spec):
    # PyObject_Format: an empty spec formats a str as itself and an int as
    # str(); else the type's __format__, which must return a str
    if spec == "":
        t = type(value)
        if t is str:
            return value
        if t is int:
            return str(value)
    meth = _lookup_special(value, "__format__")
    if meth is _NULL:
        raise TypeError("Type " + _type_name(type(value)) + " doesn't define __format__")
    result = meth(spec)
    if not isinstance(result, str):
        raise TypeError(_fqn(type(value)) + ".__format__() must return a str, not " + _fqn(type(result)))
    return result


def callable(obj):
    # PyCallable_Check
    return _cetta_callable(obj)


def aiter(async_iterable, /):
    # PyObject_GetAIter: type(x).__aiter__(x), which must give an async
    # iterator
    t = type(async_iterable)
    f = _mro_lookup(t, "__aiter__")
    if f is _NULL:
        raise TypeError("'" + _type_name(t) + "' object is not an async iterable")
    it = f(async_iterable)
    if _mro_lookup(type(it), "__anext__") is _NULL:
        raise TypeError("aiter() returned not an async iterator of type '" + _type_name(type(it)) + "'")
    return it


async def _anext_default(awaitable, default):
    # anext_awaitable: the iterator's next item, else DEFAULT when it ends
    try:
        return await awaitable
    except StopAsyncIteration:
        return default


def anext(aiterator, default=_NULL, /):
    # builtin_anext_impl: type(x).__anext__(x), awaited by the caller
    t = type(aiterator)
    f = _mro_lookup(t, "__anext__")
    if f is _NULL:
        raise TypeError("'" + _type_name(t) + "' object is not an async iterator")
    awaitable = f(aiterator)
    if default is _NULL:
        return awaitable
    return _anext_default(awaitable, default)




def id(obj):
    return _cetta_id(obj)


def format(value, format_spec=""):
    if not isinstance(format_spec, str):
        raise TypeError("format() argument 2 must be str, not " + _type_name(type(format_spec)))
    return _format(value, format_spec)


def ascii(obj):
    # repr() with every character above ASCII escaped
    r = repr(obj)
    out = []
    for ch in r:
        n = ord(ch)
        if n < 128:
            out.append(ch)
        elif n < 0x100:
            out.append("\\x" + _hex_digits(n, 2))
        elif n < 0x10000:
            out.append("\\u" + _hex_digits(n, 4))
        else:
            out.append("\\U" + _hex_digits(n, 8))
    return "".join(out)


def _hex_digits(n, width):
    d = _int_digits(n, 16)
    return "0" * (width - len(d)) + d


def _int_digits(n, base):
    # the digits of abs(n) in base 2, 8, 10 or 16, lower case
    return _cetta_int_digits(n, base)


def _int_bit_length(self):
    return _cetta_int_bit_length(self)


def _int_bit_count(self):
    return _cetta_int_bit_count(self)


def pow(base, exp, mod=None):
    # builtin pow: base ** exp, or for ints modular (long_pow)
    if mod is None:
        return base ** exp
    if isinstance(base, int) and isinstance(exp, int) and isinstance(mod, int):
        return _cetta_int_pow_mod(base, exp, mod)
    if isinstance(base, (int, float)) and isinstance(exp, (int, float)) and isinstance(mod, (int, float)):
        raise TypeError("pow() 3rd argument not allowed unless all arguments are integers")
    f = _mro_lookup(type(base), "__pow__")
    if f is _NULL:
        raise TypeError("unsupported operand type(s) for ** or pow(): '" + _type_name(type(base)) + "', '"
                        + _type_name(type(exp)) + "', '" + _type_name(type(mod)) + "'")
    return f(base, exp, mod)


def _base_text(x, base, prefix):
    n = _index_value(x, None)
    text = prefix + _int_digits(n, base)
    if n < 0:
        return "-" + text
    return text


def bin(x):
    return _base_text(x, 2, "0b")


def oct(x):
    return _base_text(x, 8, "0o")


def hex(x):
    return _base_text(x, 16, "0x")


def _parse_spec(obj, spec, default_type, default_align):
    # parse_internal_render_format_spec:
    # [[fill]align][sign][z][#][0][width][,|_][.precision][type]
    n = len(spec)
    pos = 0
    fill = " "
    align = default_align
    fill_given = False
    align_given = False
    if n - pos >= 2 and spec[pos + 1] in "<>=^":
        align = spec[pos + 1]
        fill = spec[pos]
        fill_given = True
        align_given = True
        pos += 2
    elif n - pos >= 1 and spec[pos] in "<>=^":
        align = spec[pos]
        align_given = True
        pos += 1
    sign = ""
    if n - pos >= 1 and spec[pos] in " +-":
        sign = spec[pos]
        pos += 1
    no_neg_0 = False
    if n - pos >= 1 and spec[pos] == "z":
        no_neg_0 = True
        pos += 1
    alternate = False
    if n - pos >= 1 and spec[pos] == "#":
        alternate = True
        pos += 1
    if not fill_given and n - pos >= 1 and spec[pos] == "0":
        fill = "0"
        if not align_given and default_align == ">":
            align = "="
        pos += 1
    start = pos
    while pos < n and spec[pos] in "0123456789":
        pos += 1
    width = int(spec[start:pos]) if pos > start else -1
    thousands = ""
    if n - pos >= 1 and spec[pos] == ",":
        thousands = ","
        pos += 1
    if n - pos >= 1 and spec[pos] == "_":
        if thousands:
            raise ValueError("Cannot specify both ',' and '_'.")
        thousands = "_"
        pos += 1
    if n - pos >= 1 and spec[pos] == ",":
        if thousands == "_":
            raise ValueError("Cannot specify both ',' and '_'.")
    precision = -1
    if n - pos >= 1 and spec[pos] == ".":
        pos += 1
        start = pos
        while pos < n and spec[pos] in "0123456789":
            pos += 1
        if pos == start:
            raise ValueError("Format specifier missing precision")
        precision = int(spec[start:pos])
    if n - pos > 1:
        raise ValueError("Invalid format specifier '" + spec + "' for object of type '"
                         + _type_name(type(obj)) + "'")
    t = default_type
    if n - pos == 1:
        t = spec[pos]
    group = 0
    if thousands:
        if t in "defgEG%F" or t == "":
            group = 3
        elif t in "boxX" and thousands == "_":
            group = 4
        else:
            raise ValueError("Cannot specify '" + thousands + "' with '" + t + "'.")
    return [fill, align, alternate, no_neg_0, sign, width, thousands, group, precision, t]


def _unknown_type(t, obj):
    return ValueError("Unknown format code '" + t + "' for object of type '" + _type_name(type(obj)) + "'")


def _group_digits(digits, min_width, sep, group):
    # _PyUnicode_InsertThousandsGrouping: groups of GROUP digits from the
    # right joined by SEP, zeros added (grouped too) to fill MIN_WIDTH
    if min_width < 0:
        min_width = 0
    pieces = []
    remaining = len(digits)
    end = len(digits)
    use_sep = False
    while group:
        ln = min(group, max(remaining, min_width, 1))
        n_zeros = max(0, ln - remaining)
        n_chars = max(0, min(remaining, ln))
        if use_sep:
            pieces.append(sep)
        pieces.append("0" * n_zeros + digits[end - n_chars:end])
        end -= n_chars
        use_sep = True
        remaining -= n_chars
        min_width -= ln
        if remaining <= 0 and min_width <= 0:
            pieces.reverse()
            return "".join(pieces)
        min_width -= len(sep)
    ln = max(remaining, min_width, 1)
    n_zeros = max(0, ln - remaining)
    n_chars = max(0, min(remaining, ln))
    if use_sep:
        pieces.append(sep)
    pieces.append("0" * n_zeros + digits[end - n_chars:end])
    pieces.reverse()
    return "".join(pieces)


def _fill_number(digits, has_decimal, remainder, prefix, sign_char, f, upper):
    # calc_number_widths and fill_number:
    # padding, sign, prefix, padding, grouped digits, point, rest, padding
    fill, align, alternate, no_neg_0, sign, width, thousands, group, precision, t = f
    if sign == "+":
        s = "-" if sign_char == "-" else "+"
    elif sign == " ":
        s = "-" if sign_char == "-" else " "
    else:
        s = "-" if sign_char == "-" else ""
    point = "." if has_decimal else ""
    n_other = len(s) + len(prefix) + len(point) + len(remainder)
    min_width = width - n_other if (fill == "0" and align == "=") else 0
    if digits:
        grouped = _group_digits(digits, min_width, thousands, group)
    else:
        grouped = ""
    padding = width - (n_other + len(grouped))
    left = middle = right = 0
    if padding > 0:
        if align == "<":
            right = padding
        elif align == "^":
            left = padding // 2
            right = padding - left
        elif align == "=":
            middle = padding
        else:
            left = padding
    if upper:
        prefix = prefix.upper()
        grouped = grouped.upper()
    return fill * left + s + prefix + fill * middle + grouped + point + remainder + fill * right


def _object_format(self, format_spec):
    # object.__format__: str(self) for an empty spec, any other is an error
    if not isinstance(format_spec, str):
        raise TypeError("__format__() argument must be str, not " + _type_name(type(format_spec)))
    if format_spec:
        raise TypeError("unsupported format string passed to " + _type_name(type(self)) + ".__format__")
    return str(self)


def _str_format(self, format_spec):
    # format_string_internal
    if not isinstance(format_spec, str):
        raise TypeError("__format__() argument must be str, not " + _type_name(type(format_spec)))
    if format_spec == "":
        return str(self)
    fill, align, alternate, no_neg_0, sign, width, thousands, group, precision, t = _parse_spec(self, format_spec, "s", "<")
    if t != "s":
        raise _unknown_type(t, self)
    if sign:
        if sign == " ":
            raise ValueError("Space not allowed in string format specifier")
        raise ValueError("Sign not allowed in string format specifier")
    if no_neg_0:
        raise ValueError("Negative zero coercion (z) not allowed in string format specifier")
    if alternate:
        raise ValueError("Alternate form (#) not allowed in string format specifier")
    if align == "=":
        raise ValueError("'=' alignment not allowed in string format specifier")
    text = str(self)
    if precision >= 0 and len(text) > precision:
        text = text[:precision]
    padding = width - len(text)
    if padding <= 0:
        return text
    if align == ">":
        return fill * padding + text
    if align == "^":
        left = padding // 2
        return fill * left + text + fill * (padding - left)
    return text + fill * padding


def _int_format(self, format_spec):
    # int.__format__ (_PyLong_FormatAdvancedWriter)
    if not isinstance(format_spec, str):
        raise TypeError("__format__() argument must be str, not " + _type_name(type(format_spec)))
    if format_spec == "":
        return str(self)
    f = _parse_spec(self, format_spec, "d", ">")
    t = f[9]
    if t in "bcdoxXn" and t != "":
        return _format_long(self, f)
    if t in "eEfFgG%" and t != "":
        return _format_float(float(self), f)
    raise _unknown_type(t, self)


def _format_long(value, f):
    # format_long_internal
    fill, align, alternate, no_neg_0, sign, width, thousands, group, precision, t = f
    if precision != -1:
        raise ValueError("Precision not allowed in integer format specifier")
    if no_neg_0:
        raise ValueError("Negative zero coercion (z) not allowed in integer format specifier")
    if t == "c":
        if sign:
            raise ValueError("Sign not allowed with integer format specifier 'c'")
        if alternate:
            raise ValueError("Alternate form (#) not allowed with integer format specifier 'c'")
        n = int(value)
        if n < 0 or n > 0x10ffff:
            raise OverflowError("%c arg not in range(0x110000)")
        return _fill_number("", False, chr(n), "", "", f, False)
    n = int(value)
    if t == "b":
        base, prefix = 2, "0b"
    elif t == "o":
        base, prefix = 8, "0o"
    elif t == "x" or t == "X":
        base, prefix = 16, "0x"
    else:
        base, prefix = 10, ""
    if not alternate:
        prefix = ""
    return _fill_number(_int_digits(n, base), False, "", prefix, "-" if n < 0 else "", f, t == "X")


def _float_format(self, format_spec):
    # float.__format__ (_PyFloat_FormatAdvancedWriter)
    if not isinstance(format_spec, str):
        raise TypeError("__format__() argument must be str, not " + _type_name(type(format_spec)))
    if format_spec == "":
        return str(self)
    f = _parse_spec(self, format_spec, "", ">")
    t = f[9]
    if t == "" or (t in "eEfFgGn%"):
        return _format_float(self, f)
    raise _unknown_type(t, self)


def _format_float(x, f):
    # format_float_internal over PyOS_double_to_string
    fill, align, alternate, no_neg_0, sign, width, thousands, group, precision, t = f
    add_dot_0 = False
    default_precision = 6
    if t == "":
        add_dot_0 = True
        t = "r"
        default_precision = 0
    if t == "n":
        t = "g"
    add_pct = False
    if t == "%":
        t = "f"
        x = x * 100
        add_pct = True
    if precision < 0:
        precision = default_precision
    elif t == "r":
        t = "g"
    text = _double_to_string(x, t, precision, alternate, add_dot_0, no_neg_0)
    if add_pct:
        text = text + "%"
    sign_char = ""
    if text[:1] == "-":
        sign_char = "-"
        text = text[1:]
    i = 0
    while i < len(text) and text[i] in "0123456789":
        i += 1
    digits = text[:i]
    rest = text[i:]
    has_decimal = rest[:1] == "."
    if has_decimal:
        rest = rest[1:]
    return _fill_number(digits, has_decimal, rest, "", sign_char, f, False)


def _double_to_string(x, t, precision, alternate, add_dot_0, no_neg_0):
    # PyOS_double_to_string: 'r' the shortest repr, else C's %e %f %g at the
    # precision, an alternate form keeping the point (and %g's zeros)
    upper = t in "EFG"
    low = t.lower()
    if x != x:
        text = "nan"
    elif x == _INF or x == -_INF:
        text = "inf" if x > 0 else "-inf"
    elif low == "r":
        text = repr(x)
    elif low == "g" and (alternate or add_dot_0):
        # format_float_short: with ".0" to add, the exponent form begins
        # one digit sooner; the alternate form keeps the zeros and the point
        p = precision if precision > 0 else 1
        e_text = _cetta_float_format(x, "e", p - 1)
        exp = int(e_text[e_text.find("e") + 1:])
        limit = p - 1 if add_dot_0 else p
        if -4 <= exp and exp < limit:
            text = _cetta_float_format(x, "f", p - 1 - exp)
        else:
            text = e_text
        if alternate:
            text = _with_point(text)
        else:
            text = _strip_zeros(text)
    elif low == "g":
        text = _cetta_float_format(x, "g", precision if precision > 0 else 1)
    else:
        text = _cetta_float_format(x, low, precision)
        if alternate:
            text = _with_point(text)
    if no_neg_0 and text[:1] == "-" and _is_zero_text(text[1:]):
        text = text[1:]
    if add_dot_0 and x == x and x != _INF and x != -_INF:
        if "." not in text and "e" not in text:
            text = text + ".0"
    if upper:
        text = text.upper()
    return text


def _strip_zeros(text):
    # %g's form: the significand's trailing zeros dropped, and its point
    i = text.find("e")
    if i < 0:
        mant = text
        tail = ""
    else:
        mant = text[:i]
        tail = text[i:]
    if "." in mant:
        j = len(mant)
        while j > 0 and mant[j - 1] == "0":
            j -= 1
        if j > 0 and mant[j - 1] == ".":
            j -= 1
        mant = mant[:j]
    return mant + tail


def _with_point(text):
    if "." in text or "n" in text:
        return text
    i = text.find("e")
    if i < 0:
        return text + "."
    return text[:i] + "." + text[i:]


def _is_zero_text(text):
    for ch in text:
        if ch == "e" or ch == "E":
            return True
        if ch != "0" and ch != ".":
            return False
    return True



def _str_format_method(self, *args, **kwargs):
    # str.format (do_string_format): two levels of fields at most
    return _build_string(self, args, kwargs, 2, [0, 0])


def _str_format_map(self, mapping):
    return _build_string(self, None, mapping, 2, [0, 0])


def _build_string(s, args, kwargs, depth, auto):
    # build_string and do_markup: the literal text, and each replacement
    # field {name!conversion:spec} formatted
    if depth <= 0:
        raise ValueError("Max string recursion exceeded")
    out = []
    pos = 0
    end = len(s)
    while pos < end:
        literal, field, pos = _markup_next(s, pos, end)
        out.append(literal)
        if field is None:
            continue
        name, conversion, spec, expand = field
        obj = _get_field_object(name, args, kwargs, auto)
        if conversion != "":
            if conversion == "r":
                obj = repr(obj)
            elif conversion == "s":
                obj = str(obj)
            elif conversion == "a":
                obj = ascii(obj)
            else:
                raise ValueError("Unknown conversion specifier " + conversion)
        if expand:
            spec = _build_string(spec, args, kwargs, depth - 1, auto)
        out.append(_format(obj, spec))
    return "".join(out)


def _markup_next(s, pos, end):
    # MarkupIterator_next: the literal text up to the next replacement field
    # ({{ and }} each one brace, ending the text after it), then that field
    # (parse_field) as (name, conversion, spec, expand), else None; and where
    # the next part begins
    start = pos
    c = ""
    while pos < end:
        c = s[pos]
        pos += 1
        if c == "{" or c == "}":
            break
    if c != "{" and c != "}":
        return s[start:pos], None, pos
    at_end = pos >= end
    if c == "}" and (at_end or s[pos] != "}"):
        raise ValueError("Single '}' encountered in format string")
    if at_end and c == "{":
        raise ValueError("Single '{' encountered in format string")
    if s[pos] == c:
        return s[start:pos], None, pos + 1
    literal = s[start:pos - 1]
    name, conversion, spec, expand, pos = _parse_field(s, pos, end)
    return literal, (name, conversion, spec, expand), pos


def _parse_field(s, pos, end):
    # parse_field: the field name up to ':', '!' or '}' ([...] skipped
    # whole), then a conversion character, then the spec up to the matching
    # '}' (nested braces counted)
    start = pos
    c = ""
    while pos < end:
        c = s[pos]
        pos += 1
        if c == "{":
            raise ValueError("unexpected '{' in field name")
        if c == "[":
            while pos < end and s[pos] != "]":
                pos += 1
            continue
        if c == "}" or c == ":" or c == "!":
            break
    name = s[start:pos - 1]
    if c == "!" or c == ":":
        conversion = ""
        if c == "!":
            if pos >= end:
                raise ValueError("end of string while looking for conversion specifier")
            conversion = s[pos]
            pos += 1
            if pos < end:
                c = s[pos]
                pos += 1
                if c == "}":
                    return name, conversion, "", False, pos
                if c != ":":
                    raise ValueError("expected ':' after conversion specifier")
        sstart = pos
        count = 1
        expand = False
        while pos < end:
            c = s[pos]
            pos += 1
            if c == "{":
                expand = True
                count += 1
            elif c == "}":
                count -= 1
                if count == 0:
                    return name, conversion, s[sstart:pos - 1], expand, pos
        raise ValueError("unmatched '{' in format spec")
    if c != "}":
        raise ValueError("expected '}' before end of string")
    return name, "", "", False, pos


def _field_index(text):
    # get_integer: the number written in decimal digits, else -1; more
    # digits than an index holds an error as soon as they are read
    if text == "":
        return -1
    acc = 0
    for ch in text:
        d = "0123456789".find(ch)
        if d < 0:
            return -1
        if acc > (9223372036854775807 - d) // 10:
            raise ValueError("Too many decimal digits in format string")
        acc = acc * 10 + d
    return acc


def _field_name_first(name):
    # field_name_split: the part before the first '.' or '[', and where the
    # rest begins
    n = len(name)
    i = 0
    while i < n and name[i] != "." and name[i] != "[":
        i += 1
    return name[:i], i


def _field_name_next(name, i, n):
    # FieldNameIterator_next: the next .attribute as (True, NAME), or [key]
    # as (False, KEY), KEY an int when its digits say so; and where the next
    # begins
    c = name[i]
    i += 1
    if c == ".":
        j = i
        while i < n and name[i] != "." and name[i] != "[":
            i += 1
        if i == j:
            raise ValueError("Empty attribute in format string")
        return True, name[j:i], i
    if c == "[":
        j = i
        while i < n and name[i] != "]":
            i += 1
        if i >= n:
            raise ValueError("Missing ']' in format string")
        key = name[j:i]
        k = _field_index(key)
        if key == "":
            raise ValueError("Empty attribute in format string")
        return False, (key if k == -1 else k), i + 1
    raise ValueError("Only '.' or '[' may follow ']' in format field specifier")


def _get_field_object(name, args, kwargs, auto):
    # field_name_split and get_field_object: an argument by index (counted
    # when omitted) or by keyword, then .attribute and [key] lookups
    n = len(name)
    first, i = _field_name_first(name)
    index = _field_index(first)
    empty = first == ""
    if empty or index != -1:
        if auto[0] == 0:
            auto[0] = 1 if empty else 2
        if auto[0] == 2 and empty:
            raise ValueError("cannot switch from manual field specification to automatic field numbering")
        if auto[0] == 1 and not empty:
            raise ValueError("cannot switch from automatic field numbering to manual field specification")
        if empty:
            index = auto[1]
            auto[1] = auto[1] + 1
    if index == -1:
        if kwargs is None:
            raise KeyError(first)
        obj = kwargs[first]
    else:
        if args is None:
            raise ValueError("Format string contains positional fields")
        if index >= len(args):
            raise IndexError("Replacement index " + str(index) + " out of range for positional args tuple")
        obj = args[index]
    while i < n:
        is_attr, key, i = _field_name_next(name, i, n)
        obj = getattr(obj, key) if is_attr else obj[key]
    return obj


def _percent_format(fmt, args):
    # PyUnicode_Format (3.15): str % args; a tuple gives its items in turn,
    # any other value is the one argument, and a mapping (not a tuple or a
    # str) also serves %(key) conversions.  st: the current arguments, their
    # count (-1 one value, -3 a key's value), the next one's index
    if isinstance(args, tuple):
        st = [args, len(args), 0]
    else:
        st = [args, -1, -2]
    mapping = None
    if _has_getitem(args) and not isinstance(args, tuple) and not isinstance(args, str):
        mapping = args
    out = []
    n = len(fmt)
    pos = 0
    while pos < n:
        if fmt[pos] != "%":
            start = pos
            while pos < n and fmt[pos] != "%":
                pos += 1
            out.append(fmt[start:pos])
            continue
        pos = _percent_arg(fmt, pos + 1, n, st, mapping, out)
    if st[2] < st[1] and mapping is None:
        raise TypeError("not all arguments converted during string formatting (required "
                        + str(0 if st[1] < 0 else st[2]) + ", got " + str(1 if st[1] < 0 else st[1]) + ")")
    return "".join(out)


def _has_getitem(x):
    t = type(x)
    if t is dict or t is list:
        return True
    return _mro_lookup(t, "__getitem__") is not _NULL


def _next_arg(st):
    # unicode_format_getnextarg(allowone=0), for a * width or precision: a
    # tuple's next item only
    i = st[2]
    if i < st[1] and st[1] >= 0:
        st[2] = i + 1
        return st[0][i]
    raise TypeError("not enough arguments for format string (got " + str(1 if st[1] < 0 else st[1]) + ")")


def _next_value(st):
    # unicode_format_getnextarg(allowone=1): a tuple's next item, or the one
    # value (or a key's value) once
    i = st[2]
    if i < st[1]:
        st[2] = i + 1
        return st[0][i] if st[1] >= 0 else st[0]
    raise TypeError("not enough arguments for format string (got " + str(1 if st[1] < 0 else st[1]) + ")")


def _percent_lead(st, key):
    # FORMAT_ERROR's lead: the mapping key, the argument's place among a
    # tuple's (1 the first), or neither for the one value
    if key is not _NULL:
        return "format argument " + repr(key) + ": "
    if st[2] >= 0:
        return "format argument " + str(st[2]) + ": "
    return "format argument: "


def _percent_star(st, key, fmtstart, what):
    # a * width or precision: an int from the arguments
    if st[1] < -1:
        raise ValueError("* cannot be used with a parenthesised mapping key at position " + str(fmtstart))
    v = _next_arg(st)
    if not isinstance(v, int):
        raise TypeError(_percent_lead(st, key) + "* requires int, not " + _fqn(type(v)))
    v = int(v)
    if v > 9223372036854775807 or v < -9223372036854775807:
        raise OverflowError(_percent_lead(st, key) + "too big for " + what)
    return v


def _percent_arg(fmt, pos, n, st, mapping, out):
    # unicode_format_arg and unicode_format_arg_parse: after the % at
    # pos - 1, [(key)][flags][width][.precision][hlL]conversion
    fmtstart = pos - 1
    if pos < n and fmt[pos] == "%":
        out.append("%")
        return pos + 1
    key = _NULL
    if pos < n and fmt[pos] == "(":
        if mapping is None:
            raise TypeError("format requires a mapping, not " + _fqn(type(st[0])))
        pos += 1
        start = pos
        depth = 1
        while depth > 0 and pos < n:
            c = fmt[pos]
            if c == ")":
                depth -= 1
            elif c == "(":
                depth += 1
            pos += 1
        if depth > 0:
            raise ValueError("stray % or incomplete format key at position " + str(fmtstart))
        key = fmt[start:pos - 1]
        st[0] = mapping[key]
        st[1] = -3
        st[2] = -4
    elif st[1] < -1:
        raise ValueError("format requires a parenthesised mapping key at position " + str(fmtstart))
    ljust = sign_flag = blank = alt = zero = False
    ch = None
    exhausted = False
    while True:
        if pos >= n:
            exhausted = True
            break
        ch = fmt[pos]
        pos += 1
        if ch == "-":
            ljust = True
        elif ch == "+":
            sign_flag = True
        elif ch == " ":
            blank = True
        elif ch == "#":
            alt = True
        elif ch == "0":
            zero = True
        else:
            break
    width = -1
    if not exhausted and ch == "*":
        width = _percent_star(st, key, fmtstart, "width")
        if width < 0:
            ljust = True
            width = -width
        if pos < n:
            ch = fmt[pos]
            pos += 1
        else:
            exhausted = True
    elif not exhausted and "0" <= ch <= "9":
        width = ord(ch) - 48
        while True:
            if pos >= n:
                exhausted = True
                break
            ch = fmt[pos]
            pos += 1
            if not "0" <= ch <= "9":
                break
            if width > (9223372036854775807 - (ord(ch) - 48)) // 10:
                raise ValueError("width too big at position " + str(fmtstart))
            width = width * 10 + ord(ch) - 48
    prec = -1
    if not exhausted and ch == ".":
        prec = 0
        if pos < n:
            ch = fmt[pos]
            pos += 1
            if ch == "*":
                prec = _percent_star(st, key, fmtstart, "precision")
                if prec < 0:
                    prec = 0
                if pos < n:
                    ch = fmt[pos]
                    pos += 1
                else:
                    exhausted = True
            elif "0" <= ch <= "9":
                prec = ord(ch) - 48
                while True:
                    if pos >= n:
                        exhausted = True
                        break
                    ch = fmt[pos]
                    pos += 1
                    if not "0" <= ch <= "9":
                        break
                    if prec > (2147483647 - (ord(ch) - 48)) // 10:
                        raise ValueError("precision too big at position " + str(fmtstart))
                    prec = prec * 10 + ord(ch) - 48
        else:
            exhausted = True
    if not exhausted and ch in "hlL":
        if pos < n:
            ch = fmt[pos]
            pos += 1
        else:
            exhausted = True
    if exhausted:
        raise ValueError("stray % at position " + str(fmtstart))
    # unicode_format_arg_format: the value first, then its conversion
    v = _next_value(st)
    is_number = False
    if ch == "s" or ch == "r" or ch == "a":
        if ch == "s":
            text = v if type(v) is str else str(v)
        elif ch == "r":
            text = repr(v)
        else:
            text = ascii(v)
    elif ch in "iduoxX":
        text = _percent_long(v, ch, alt, prec, _percent_lead(st, key))
        is_number = True
    elif ch in "eEfFgG":
        text = _double_to_string(_percent_float(v, ch, _percent_lead(st, key)), ch, 6 if prec < 0 else prec, alt, False, False)
        is_number = True
    elif ch == "c":
        text = _percent_char(v, _percent_lead(st, key))
    else:
        at = str(pos - 1)
        if ch < "\x80" and ch.isalpha():
            raise ValueError("unsupported format %" + ch + " at position " + str(fmtstart))
        if ch == "'":
            raise ValueError("stray % at position " + str(fmtstart) + " or unexpected format character \"'\" at position " + at)
        code = ord(ch)
        if 32 <= code < 127:
            raise ValueError("stray % at position " + str(fmtstart) + " or unexpected format character '" + ch + "' at position " + at)
        hexcode = _int_digits(code, 16).upper().rjust(4, "0")
        if ch.isprintable():
            raise ValueError("stray % at position " + str(fmtstart) + " or unexpected format character '" + ch + "' (U+"
                             + hexcode + ") at position " + at)
        raise ValueError("stray % at position " + str(fmtstart) + " or unexpected format character U+" + hexcode
                         + " at position " + at)
    out.append(_percent_output(text, ch, is_number, ljust, sign_flag, blank, alt, zero, width, prec))
    return pos


def _is_number(v):
    # PyNumber_Check: an int, a float, or __index__, __int__ or __float__
    t = type(v)
    if isinstance(v, int) or isinstance(v, float):
        return True
    return (_mro_lookup(t, "__index__") is not _NULL or _mro_lookup(t, "__int__") is not _NULL
            or _mro_lookup(t, "__float__") is not _NULL)


def _percent_long(v, ch, alt, prec, lead):
    # mainformatlong and _PyUnicode_FormatLong
    if not _is_number(v):
        if ch in "oxX":
            raise TypeError(lead + "%" + ch + " requires an integer, not " + _fqn(type(v)))
        raise TypeError(lead + "%" + ch + " requires a real number, not " + _fqn(type(v)))
    if ch in "oxX":
        try:
            n = _index_value(v, None)
        except TypeError:
            raise TypeError(lead + "%" + ch + " requires an integer, not " + _fqn(type(v)))
    else:
        n = int(v)
    if ch == "o":
        base, prefix = 8, "0o"
    elif ch == "x" or ch == "X":
        base, prefix = 16, "0x"
    else:
        base, prefix = 10, ""
    digits = _int_digits(n, base)
    if prec > len(digits):
        digits = "0" * (prec - len(digits)) + digits
    text = (prefix if alt else "") + digits
    if n < 0:
        text = "-" + text
    if ch == "X":
        text = text.upper()
    return text


def _percent_float(v, ch, lead):
    # formatfloat: PyFloat_AsDouble's value
    if type(v) is float:
        return v
    if isinstance(v, int) or isinstance(v, float):
        return float(v)
    if _mro_lookup(type(v), "__float__") is _NULL and _mro_lookup(type(v), "__index__") is _NULL:
        raise TypeError(lead + "%" + ch + " requires a real number, not " + _fqn(type(v)))
    return float(v)


def _percent_char(v, lead):
    # formatchar: a one-character str, or an index naming a code point
    if isinstance(v, str):
        if len(v) == 1:
            return v
        raise TypeError(lead + "%c requires an integer or a unicode character, not a string of length " + str(len(v)))
    if not isinstance(v, int) and _mro_lookup(type(v), "__index__") is _NULL:
        raise TypeError(lead + "%c requires an integer or a unicode character, not " + _fqn(type(v)))
    v = _index(v)
    if v < 0 or v > 0x10ffff:
        raise OverflowError(lead + "%c argument not in range(0x110000)")
    return chr(v)


def _percent_output(text, ch, is_number, ljust, sign_flag, blank, alt, zero, width, prec):
    # unicode_format_arg_output: precision cuts a string; a number's sign, a
    # zero fill after it (and after 0x), else spaces before them
    fill = "0" if is_number and zero else " "
    if (width == -1 or width <= len(text)) and (prec == -1 or prec >= len(text)) and not sign_flag and not blank:
        return text
    if ch == "s" or ch == "r" or ch == "a":
        if prec >= 0 and len(text) > prec:
            text = text[:prec]
    signchar = ""
    sign = is_number
    if sign:
        if text[:1] == "-" or text[:1] == "+":
            signchar = text[0]
            text = text[1:]
        elif sign_flag:
            signchar = "+"
        elif blank:
            signchar = " "
        else:
            sign = False
    prefix = ""
    if alt and (ch == "x" or ch == "X" or ch == "o"):
        prefix = text[:2]
        text = text[2:]
    body_len = len(signchar) + len(prefix) + len(text)
    if width < body_len:
        width = body_len
    pad = width - body_len
    if ljust:
        return signchar + prefix + text + " " * pad
    if fill == "0":
        return signchar + prefix + "0" * pad + text
    return " " * pad + signchar + prefix + text

_INF = float("inf")

# ---------------------------------------------------------------- str methods

# Objects/unicodeobject.c's methods of str, over the string library's Unicode
# data (Unicode 15.1.0): each code point's property bits (its general
# category, bits 0-29, then the derived properties) and its full case
# mappings.  A method of a str subclass's instance reads its str part.

_U_LETTER = 0x1F                                   # Lu Ll Lt Lm Lo
_U_LT = 1 << 2
_U_NOT_PRINTABLE = (0x1F << 25) | (0x7 << 22)      # Cc Cf Cs Co Cn, Zs Zl Zp
_U_LOWER = 1 << 30
_U_UPPER = 1 << 31
_U_CASED = 1 << 32
_U_CASE_IGNORABLE = 1 << 33
_U_XID_START = 1 << 34
_U_XID_CONTINUE = 1 << 35
_U_DECIMAL = 1 << 36
_U_DIGIT = (1 << 36) | (1 << 37)
_U_NUMERIC = (1 << 36) | (1 << 37) | (1 << 38)
_U_SPACE = (1 << 22) | (1 << 39) | (1 << 40) | (1 << 41)


def _str_arg(x):
    if not isinstance(x, str):
        raise TypeError("must be str, not " + _type_name(type(x)))
    return _cetta_base(x)


def _str_named_arg(x, fname, n):
    # an Argument Clinic str argument
    if not isinstance(x, str):
        raise TypeError(fname + "() argument " + str(n) + " must be str, not " + _type_name(type(x)))
    return _cetta_base(x)


def _str_rfind_text(s, sub):
    # the last place SUB begins in S, else -1
    return _cetta_str_rfind(sub, s)


def _slice_bounds(start, end, length):
    # ADJUST_INDICES: start and end (None, or by __index__) within the string
    start = 0 if start is None else _index(start)
    end = length if end is None else _index(end)
    if end > length:
        end = length
    elif end < 0:
        end += length
        if end < 0:
            end = 0
    if start < 0:
        start += length
        if start < 0:
            start = 0
    return start, end


def _str_isalpha(self):
    s = _cetta_base(self)
    return s != "" and _cetta_str_props_all(_U_LETTER, s)


def _str_isdecimal(self):
    s = _cetta_base(self)
    return s != "" and _cetta_str_props_all(_U_DECIMAL, s)


def _str_isdigit(self):
    s = _cetta_base(self)
    return s != "" and _cetta_str_props_all(_U_DIGIT, s)


def _str_isnumeric(self):
    s = _cetta_base(self)
    return s != "" and _cetta_str_props_all(_U_NUMERIC, s)


def _str_isalnum(self):
    s = _cetta_base(self)
    return s != "" and _cetta_str_props_all(_U_LETTER | _U_NUMERIC, s)


def _str_isspace(self):
    s = _cetta_base(self)
    return s != "" and _cetta_str_props_all(_U_SPACE, s)


def _str_isprintable(self):
    # an empty string is printable; so is a space, no other separator
    return not _cetta_str_props_any(_U_NOT_PRINTABLE, _cetta_base(self).replace(" ", ""))


def _str_isascii(self):
    return _cetta_str_is_ascii(_cetta_base(self))


def _str_islower(self):
    # a lowercase letter, and no uppercase or titlecase one
    s = _cetta_base(self)
    return _cetta_str_props_any(_U_LOWER, s) and not _cetta_str_props_any(_U_UPPER | _U_LT, s)


def _str_isupper(self):
    s = _cetta_base(self)
    return _cetta_str_props_any(_U_UPPER, s) and not _cetta_str_props_any(_U_LOWER | _U_LT, s)


def _str_istitle(self):
    # an uppercase or titlecase letter only after an uncased character, a
    # lowercase one only after a cased one; at least one cased
    cased = False
    previous_is_cased = False
    for c in _cetta_base(self):
        p = _cetta_code_props(ord(c))
        if p & (_U_UPPER | _U_LT):
            if previous_is_cased:
                return False
            previous_is_cased = True
            cased = True
        elif p & _U_LOWER:
            if not previous_is_cased:
                return False
            previous_is_cased = True
            cased = True
        else:
            previous_is_cased = False
    return cased


def _str_isidentifier(self):
    # _PyUnicode_ScanIdentifier: XID_Start or "_", then XID_Continue
    s = _cetta_base(self)
    if s == "":
        return False
    if s[0] != "_" and not (_cetta_code_props(ord(s[0])) & _U_XID_START):
        return False
    return _cetta_str_props_all(_U_XID_CONTINUE, s[1:])


def _str_upper(self):
    return _cetta_str_case("upper", _cetta_base(self))


def _str_lower(self):
    return _cetta_str_case("lower", _cetta_base(self))


def _str_casefold(self):
    return _cetta_str_case("fold", _cetta_base(self))


def _final_sigma(s, i):
    # handle_capital_sigma: a cased letter before it, past case-ignorable
    # ones, and none after it
    j = i - 1
    while j >= 0 and _cetta_code_props(ord(s[j])) & _U_CASE_IGNORABLE:
        j -= 1
    if j < 0 or not (_cetta_code_props(ord(s[j])) & _U_CASED):
        return False
    j = i + 1
    n = len(s)
    while j < n and _cetta_code_props(ord(s[j])) & _U_CASE_IGNORABLE:
        j += 1
    return j == n or not (_cetta_code_props(ord(s[j])) & _U_CASED)


def _lower_at(s, i):
    # the lowercase of S[I], a capital sigma by its context
    c = s[i]
    if c == "Σ":
        return "ς" if _final_sigma(s, i) else "σ"
    return _cetta_str_case("lower", c)


def _str_swapcase(self):
    # do_swapcase: an uppercase letter lowered, a lowercase one raised
    s = _cetta_base(self)
    out = []
    i = 0
    for c in s:
        p = _cetta_code_props(ord(c))
        if p & _U_UPPER:
            out.append(_lower_at(s, i))
        elif p & _U_LOWER:
            out.append(_cetta_str_case("upper", c))
        else:
            out.append(c)
        i += 1
    return "".join(out)


def _str_title(self):
    # do_title: a letter after an uncased character titled, after a cased
    # one lowered
    s = _cetta_base(self)
    out = []
    previous_is_cased = False
    i = 0
    for c in s:
        out.append(_lower_at(s, i) if previous_is_cased else _cetta_str_case("title", c))
        previous_is_cased = bool(_cetta_code_props(ord(c)) & _U_CASED)
        i += 1
    return "".join(out)


def _str_capitalize(self):
    # do_capitalize: the first character titled, the others lowered
    s = _cetta_base(self)
    if s == "":
        return ""
    if "Σ" not in s:
        return _cetta_str_case("title", s[0]) + _cetta_str_case("lower", s[1:])
    out = [_cetta_str_case("title", s[0])]
    i = 1
    while i < len(s):
        out.append(_lower_at(s, i))
        i += 1
    return "".join(out)


def _fill_char(fillchar):
    # convert_uc
    if not isinstance(fillchar, str):
        raise TypeError("The fill character must be a unicode character, not " + _type_name(type(fillchar)))
    fillchar = _cetta_base(fillchar)
    if len(fillchar) != 1:
        raise TypeError("The fill character must be exactly one character long")
    return fillchar


def _str_center(self, width, fillchar=" ", /):
    s = _cetta_base(self)
    width = _index(width)
    fill = _fill_char(fillchar)
    marg = width - len(s)
    if marg <= 0:
        return s
    left = marg // 2 + (marg & width & 1)
    return fill * left + s + fill * (marg - left)


def _str_ljust(self, width, fillchar=" ", /):
    s = _cetta_base(self)
    width = _index(width)
    fill = _fill_char(fillchar)
    return s if len(s) >= width else s + fill * (width - len(s))


def _str_rjust(self, width, fillchar=" ", /):
    s = _cetta_base(self)
    width = _index(width)
    fill = _fill_char(fillchar)
    return s if len(s) >= width else fill * (width - len(s)) + s


def _str_zfill(self, width, /):
    # zeros on the left, after a leading sign
    s = _cetta_base(self)
    width = _index(width)
    if len(s) >= width:
        return s
    fill = width - len(s)
    if s and (s[0] == "+" or s[0] == "-"):
        return s[0] + "0" * fill + s[1:]
    return "0" * fill + s


def _str_strip_by(self, chars, side, name):
    s = _cetta_base(self)
    if chars is None:
        return _cetta_str_trim_props(_U_SPACE, side, s)
    if not isinstance(chars, str):
        raise TypeError(name + " arg must be None or str")
    return _cetta_str_trim_chars(_cetta_base(chars), side, s)


def _str_strip(self, chars=None, /):
    return _str_strip_by(self, chars, "both", "strip")


def _str_lstrip(self, chars=None, /):
    return _str_strip_by(self, chars, "left", "lstrip")


def _str_rstrip(self, chars=None, /):
    return _str_strip_by(self, chars, "right", "rstrip")


def _str_split(self, sep=None, maxsplit=-1):
    # on runs of whitespace without SEP; else on SEP, which may not be empty;
    # at most MAXSPLIT splits from the left
    s = _cetta_base(self)
    maxsplit = _index(maxsplit)
    if sep is None:
        return _cetta_str_split_ws(maxsplit, s)
    if not isinstance(sep, str):
        raise TypeError("must be str or None, not " + _type_name(type(sep)))
    sep = _cetta_base(sep)
    if sep == "":
        raise ValueError("empty separator")
    parts = _cetta_str_split(sep, s)
    if maxsplit < 0 or len(parts) <= maxsplit + 1:
        return parts
    return parts[:maxsplit] + [sep.join(parts[maxsplit:])]


def _str_rsplit(self, sep=None, maxsplit=-1):
    # as split, the splits from the right
    s = _cetta_base(self)
    maxsplit = _index(maxsplit)
    if sep is None:
        return _cetta_str_rsplit_ws(maxsplit, s)
    if not isinstance(sep, str):
        raise TypeError("must be str or None, not " + _type_name(type(sep)))
    sep = _cetta_base(sep)
    if sep == "":
        raise ValueError("empty separator")
    parts = _cetta_str_split(sep, s)
    if maxsplit < 0 or len(parts) <= maxsplit + 1:
        return parts
    keep = len(parts) - maxsplit
    return [sep.join(parts[:keep])] + parts[keep:]


def _str_find_in(s, sub, start, end):
    # any_find_slice: the first place within [start, end), else -1
    start, end = _slice_bounds(start, end, len(s))
    if end - start < len(sub):
        return -1
    if sub == "":
        return start
    i = _cetta_str_find(sub, s[start:end])
    return -1 if i < 0 else start + i


def _str_rfind_in(s, sub, start, end):
    start, end = _slice_bounds(start, end, len(s))
    if end - start < len(sub):
        return -1
    if sub == "":
        return end
    i = _cetta_str_rfind(sub, s[start:end])
    return -1 if i < 0 else start + i


def _str_find(self, sub, start=None, end=None, /):
    return _str_find_in(_cetta_base(self), _str_arg(sub), start, end)


def _str_rfind(self, sub, start=None, end=None, /):
    return _str_rfind_in(_cetta_base(self), _str_arg(sub), start, end)


def _str_index(self, sub, start=None, end=None, /):
    i = _str_find_in(_cetta_base(self), _str_arg(sub), start, end)
    if i < 0:
        raise ValueError("substring not found")
    return i


def _str_rindex(self, sub, start=None, end=None, /):
    i = _str_rfind_in(_cetta_base(self), _str_arg(sub), start, end)
    if i < 0:
        raise ValueError("substring not found")
    return i


def _str_count(self, sub, start=None, end=None, /):
    # the occurrences that do not overlap, within [start, end)
    s = _cetta_base(self)
    sub = _str_arg(sub)
    start, end = _slice_bounds(start, end, len(s))
    if end - start < len(sub):
        return 0
    if sub == "":
        return end - start + 1
    return len(_cetta_str_split(sub, s[start:end])) - 1


def _str_replace(self, old, new, /, count=-1):
    # at most COUNT occurrences of OLD, from the left; an empty OLD stands
    # before each character and at the end
    s = _cetta_base(self)
    old = _str_named_arg(old, "replace", 1)
    new = _str_named_arg(new, "replace", 2)
    count = _index(count)
    if old == "":
        out = []
        for c in s:
            if count != 0:
                out.append(new)
                count -= 1
            out.append(c)
        if count != 0:
            out.append(new)
        return "".join(out)
    parts = _cetta_str_split(old, s)
    if count < 0 or count >= len(parts) - 1:
        return new.join(parts)
    return new.join(parts[:count + 1]) + old + old.join(parts[count + 1:])


def _tailmatch(s, sub, start, end, at_end):
    # tailmatch: SUB at the start (or the end) of [start, end)
    start, end = _slice_bounds(start, end, len(s))
    end -= len(sub)
    if end < start:
        return False
    if sub == "":
        return True
    offset = end if at_end else start
    return s[offset:offset + len(sub)] == sub


def _str_tailmatch(self, affix, start, end, at_end, name):
    s = _cetta_base(self)
    if isinstance(affix, tuple):
        for a in affix:
            if not isinstance(a, str):
                raise TypeError("tuple for " + name + " must only contain str, not " + _type_name(type(a)))
            if _tailmatch(s, _cetta_base(a), start, end, at_end):
                return True
        return False
    if not isinstance(affix, str):
        raise TypeError(name + " first arg must be str or a tuple of str, not " + _type_name(type(affix)))
    return _tailmatch(s, _cetta_base(affix), start, end, at_end)


def _str_startswith(self, prefix, start=None, end=None, /):
    return _str_tailmatch(self, prefix, start, end, False, "startswith")


def _str_endswith(self, suffix, start=None, end=None, /):
    return _str_tailmatch(self, suffix, start, end, True, "endswith")


def _str_removeprefix(self, prefix, /):
    s = _cetta_base(self)
    if not isinstance(prefix, str):
        raise TypeError("removeprefix() argument must be str, not " + _type_name(type(prefix)))
    prefix = _cetta_base(prefix)
    return s[len(prefix):] if s.startswith(prefix) else s


def _str_removesuffix(self, suffix, /):
    s = _cetta_base(self)
    if not isinstance(suffix, str):
        raise TypeError("removesuffix() argument must be str, not " + _type_name(type(suffix)))
    suffix = _cetta_base(suffix)
    return s[:len(s) - len(suffix)] if suffix and s.endswith(suffix) else s


def _str_expandtabs(self, tabsize=8):
    # a tab to the next column that is a multiple of TABSIZE; a line break
    # starts the column count again
    s = _cetta_base(self)
    tabsize = _index(tabsize)
    if "\t" not in s:
        return s
    out = []
    column = 0
    for c in s:
        if c == "\t":
            if tabsize > 0:
                n = tabsize - column % tabsize
                out.append(" " * n)
                column += n
        else:
            out.append(c)
            column = 0 if c == "\n" or c == "\r" else column + 1
    return "".join(out)


def _str_translate(self, table, /):
    # each character by table[ord(c)]: None deletes it, an int is a code
    # point, a str replaces it; a LookupError keeps it
    out = []
    for c in _cetta_base(self):
        try:
            v = table[ord(c)]
        except LookupError:
            out.append(c)
            continue
        if v is None:
            continue
        if isinstance(v, int):
            if v < 0 or v > 0x10FFFF:
                raise ValueError("character mapping must be in range(0x110000)")
            out.append(chr(v))
        elif isinstance(v, str):
            out.append(v)
        else:
            raise TypeError("character mapping must return integer, None or str")
    return "".join(out)


def _str_maketrans(x, y=_NULL, z=_NULL, /):
    # unicode_maketrans_impl
    d = {}
    if y is not _NULL:
        if not isinstance(y, str):
            raise TypeError("maketrans() argument 2 must be str, not " + _type_name(type(y)))
        if z is not _NULL and not isinstance(z, str):
            raise TypeError("maketrans() argument 3 must be str, not " + _type_name(type(z)))
        if not isinstance(x, str):
            raise TypeError("first maketrans argument must be a string if there is a second argument")
        if len(x) != len(y):
            raise ValueError("the first two maketrans arguments must have equal length")
        for a, b in zip(x, y):
            d[ord(a)] = ord(b)
        if z is not _NULL:
            for c in z:
                d[ord(c)] = None
        return d
    if type(x) is not dict:
        raise TypeError("if you give only one argument to maketrans it must be a dict")
    for key, value in x.items():
        if isinstance(key, str):
            if len(key) != 1:
                raise ValueError("string keys in translate table must be of length 1")
            d[ord(key)] = value
        elif isinstance(key, int):
            d[key] = value
        else:
            raise TypeError("keys in translate table must be strings or integers")
    return d


# the line boundaries of str.splitlines (Py_UNICODE_ISLINEBREAK)
_LINE_BREAKS = "\n\r\x0b\x0c\x1c\x1d\x1e\x85  "


def _str_splitlines(self, keepends=False):
    # at each line boundary, \r\n one boundary; the boundaries kept when
    # keepends is true
    s = _cetta_base(self)
    lines = []
    i = 0
    n = len(s)
    start = 0
    while i < n:
        c = s[i]
        if c in _LINE_BREAKS:
            end = i + 1
            if c == "\r" and end < n and s[end] == "\n":
                end += 1
            lines.append(s[start:end] if keepends else s[start:i])
            i = end
            start = end
        else:
            i += 1
    if start < n:
        lines.append(s[start:])
    return lines


def _str_partition(self, sep, /):
    s = _cetta_base(self)
    sep = _str_arg(sep)
    if sep == "":
        raise ValueError("empty separator")
    i = _cetta_str_find(sep, s)
    if i < 0:
        return (s, "", "")
    return (s[:i], sep, s[i + len(sep):])


def _str_rpartition(self, sep, /):
    s = _cetta_base(self)
    sep = _str_arg(sep)
    if sep == "":
        raise ValueError("empty separator")
    i = _cetta_str_rfind(sep, s)
    if i < 0:
        return ("", "", s)
    return (s[:i], sep, s[i + len(sep):])


# ---------------------------------------------------------------- bytes

# A bytes value holds a str of the code points 0..255, one for each byte (its
# Latin-1 reading): its methods that find, split, pad or replace are the str
# methods on that text; the ASCII-only ones (case, the is- tests, whitespace)
# have their own definitions; codecs read and write the bytes themselves.

def _bview(x):
    # the text of a bytes-like argument (bytes, a bytearray), else _NULL
    return _cetta_buffer_view(x)


def _bytes_like(x, fname):
    v = _bview(x)
    if v is _NULL:
        raise TypeError("a bytes-like object is required, not '" + _type_name(type(x)) + "'")
    return v


_CODECS = {"utf_8": "utf-8", "utf8": "utf-8", "u8": "utf-8", "utf": "utf-8", "cp65001": "utf-8",
           "latin_1": "latin-1", "latin1": "latin-1", "iso_8859_1": "latin-1", "iso8859_1": "latin-1",
           "8859": "latin-1", "cp819": "latin-1", "l1": "latin-1", "latin": "latin-1",
           "ascii": "ascii", "us_ascii": "ascii", "646": "ascii", "us": "ascii"}
_ERROR_HANDLERS = ("strict", "ignore", "replace", "backslashreplace", "surrogateescape", "xmlcharrefreplace")


def _codec(encoding):
    c = _CODECS.get(encoding.lower().replace("-", "_").replace(" ", "_"))
    if c is None:
        raise LookupError("unknown encoding: " + encoding)
    return c


def _handler(errors):
    if errors not in _ERROR_HANDLERS:
        raise LookupError("unknown error handler name '" + errors + "'")
    return errors


def _char_escape(c):
    o = ord(c)
    if o < 0x100:
        return "\\x" + _int_digits(o, 16).rjust(2, "0")
    if o < 0x10000:
        return "\\u" + _int_digits(o, 16).rjust(4, "0")
    return "\\U" + _int_digits(o, 16).rjust(8, "0")


def _unicode_encode_error_str(self):
    encoding, obj, start, end, reason = self.args
    if end == start + 1:
        return ("'" + encoding + "' codec can't encode character '" + _char_escape(obj[start])
                + "' in position " + str(start) + ": " + reason)
    return ("'" + encoding + "' codec can't encode characters in position " + str(start) + "-"
            + str(end - 1) + ": " + reason)


def _unicode_decode_error_str(self):
    encoding, obj, start, end, reason = self.args
    if end == start + 1:
        return ("'" + encoding + "' codec can't decode byte 0x" + _int_digits(obj[start], 16).rjust(2, "0")
                + " in position " + str(start) + ": " + reason)
    return ("'" + encoding + "' codec can't decode bytes in position " + str(start) + "-" + str(end - 1)
            + ": " + reason)


def _str_encode(self, encoding="utf-8", errors="strict"):
    # str.encode: the codec's bytes; an unencodable run of characters to the
    # error handler (looked up when there is one)
    s = _cetta_base(self)
    if not isinstance(encoding, str):
        raise TypeError("encode() argument 'encoding' must be str, not " + _type_name(type(encoding)))
    if not isinstance(errors, str):
        raise TypeError("encode() argument 'errors' must be str, not " + _type_name(type(errors)))
    codec = _codec(encoding)
    if codec == "utf-8":
        if not _cetta_str_props_any(1 << 27, s):
            return _cetta_utf8_bytes(s)
        limit, reason = None, "surrogates not allowed"
    elif codec == "ascii":
        if _cetta_str_is_ascii(s):
            return _cetta_bytes(s)
        limit, reason = 128, "ordinal not in range(128)"
    else:
        limit, reason = 256, "ordinal not in range(256)"
    out = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        o = ord(c)
        bad = (0xD800 <= o <= 0xDFFF) if limit is None else o >= limit
        if not bad:
            out.append(_cetta_bytes_view(_cetta_utf8_bytes(c)) if codec == "utf-8" else c)
            i += 1
            continue
        j = i + 1
        while j < n and ((0xD800 <= ord(s[j]) <= 0xDFFF) if limit is None else ord(s[j]) >= limit):
            j += 1
        h = _handler(errors)
        if h == "strict":
            raise UnicodeEncodeError(codec, s, i, j, reason)
        for k in range(i, j):
            ck = s[k]
            if h == "replace":
                out.append("?")
            elif h == "backslashreplace":
                out.append(_char_escape(ck))
            elif h == "xmlcharrefreplace":
                out.append("&#" + str(ord(ck)) + ";")
            elif h == "surrogateescape":
                if not 0xDC80 <= ord(ck) <= 0xDCFF:
                    raise UnicodeEncodeError(codec, s, k, k + 1, reason)
                out.append(chr(ord(ck) - 0xDC00))
        i = j
    return _cetta_bytes("".join(out))


def _utf8_error(v, i):
    # the decoder's error at I (maximal subpart): its end and reason
    n = len(v)
    c = ord(v[i])
    if c < 0xC2 or c > 0xF4:
        return i + 1, "invalid start byte"
    need = 1 if c < 0xE0 else 2 if c < 0xF0 else 3
    lo, hi = 0x80, 0xBF
    if c == 0xE0:
        lo = 0xA0
    elif c == 0xED:
        hi = 0x9F
    elif c == 0xF0:
        lo = 0x90
    elif c == 0xF4:
        hi = 0x8F
    for k in range(1, need + 1):
        if i + k >= n:
            return n, "unexpected end of data"
        b = ord(v[i + k])
        if k == 1:
            ok = lo <= b <= hi
        else:
            ok = 0x80 <= b <= 0xBF
        if not ok:
            return i + k, "invalid continuation byte"
    return i + need + 1, None


def _bytes_decode(self, encoding="utf-8", errors="strict"):
    # bytes.decode: the codec's str; each undecodable part to the error handler
    v = _bytes_like(self, "decode")
    if not isinstance(encoding, str):
        raise TypeError("decode() argument 'encoding' must be str, not " + _type_name(type(encoding)))
    if not isinstance(errors, str):
        raise TypeError("decode() argument 'errors' must be str, not " + _type_name(type(errors)))
    codec = _codec(encoding)
    if codec == "latin-1":
        return v
    if codec == "ascii":
        if _cetta_str_is_ascii(v):
            return v
    else:
        r = _cetta_bytes_utf8(v)
        if r[0] == "ok":
            return r[1]
    out = []
    start = 0
    i = 0
    n = len(v)
    while i < n:
        if codec == "ascii":
            if ord(v[i]) < 128:
                i += 1
                continue
            end, reason = i + 1, "ordinal not in range(128)"
        else:
            end, reason = _utf8_error(v, i) if ord(v[i]) >= 0x80 else (i + 1, None)
            if reason is None:
                i = end
                continue
        out.append(_bytes_segment(v[start:i], codec))
        h = _handler(errors)
        if h == "strict":
            raise UnicodeDecodeError(codec, _cetta_bytes(v), i, end, reason)
        if h == "replace":
            out.append("�")
        elif h == "backslashreplace":
            out.append("".join("\\x" + _int_digits(ord(c), 16).rjust(2, "0") for c in v[i:end]))
        elif h == "surrogateescape":
            out.append("".join(chr(0xDC00 + ord(c)) for c in v[i:end]))
        elif h == "xmlcharrefreplace":
            raise TypeError("don't know how to handle UnicodeDecodeError in error callback")
        start = i = end
    out.append(_bytes_segment(v[start:], codec))
    return "".join(out)


def _bytes_segment(v, codec):
    # a part of the bytes the codec decodes without error
    if codec == "ascii" or v == "":
        return v
    return _cetta_bytes_utf8(v)[1]


def _bytes_new(cls, source=_NULL, encoding=_NULL, errors=_NULL):
    # bytes_new: a str with an encoding, an int's zeros, a bytes-like
    # object's bytes, an iterable's ints (each 0..255)
    if source is _NULL:
        if encoding is not _NULL or errors is not _NULL:
            raise TypeError("encoding or errors without sequence argument")
        return _bytes_made(cls, "")
    if isinstance(source, str):
        if encoding is _NULL:
            raise TypeError("string argument without an encoding")
        return _bytes_made(cls, _cetta_bytes_view(_str_encode(source, encoding, "strict" if errors is _NULL else errors)))
    if encoding is not _NULL:
        raise TypeError("encoding without a string argument")
    if errors is not _NULL:
        raise TypeError("errors without a string argument")
    v = _bview(source)
    if v is not _NULL:
        return _bytes_made(cls, v)
    if isinstance(source, int) or _mro_lookup(type(source), "__index__") is not _NULL:
        n = _index(source)
        if n < 0:
            raise ValueError("negative count")
        return _bytes_made(cls, chr(0) * n)
    try:
        it = iter(source)
    except TypeError:
        raise TypeError("cannot convert '" + _type_name(type(source)) + "' object to bytes")
    out = []
    for x in it:
        n = _index(x)
        if n < 0 or n > 255:
            raise ValueError("bytes must be in range(0, 256)")
        out.append(chr(n))
    return _bytes_made(cls, "".join(out))


def _bytes_made(cls, v):
    b = _cetta_bytes(v)
    if cls is bytes:
        return b
    o = _cetta_new_object(cls)
    _cetta_set_field(o, "value", b)
    return o


def _bytes_wrap(self, v):
    # a result of a bytes method: bytes (bytearray's give a bytearray)
    if isinstance(self, bytearray):
        return _bytearray_of(v)
    return _cetta_bytes(v)


def _bytes_hex(self, sep=_NULL, bytes_per_sep=1):
    v = _bytes_like(self, "hex")
    digits = [_int_digits(ord(c), 16).rjust(2, "0") for c in v]
    if sep is _NULL or not digits:
        return "".join(digits)
    if isinstance(sep, (bytes, bytearray)):
        sep = _bview(sep)
    if not isinstance(sep, str) or len(sep) != 1:
        raise ValueError("sep must be length 1.")
    n = abs(bytes_per_sep) if bytes_per_sep else len(digits)
    groups = []
    if bytes_per_sep > 0:
        i = len(digits)
        while i > 0:
            groups.append("".join(digits[max(0, i - n):i]))
            i -= n
        groups.reverse()
    else:
        for i in range(0, len(digits), n):
            groups.append("".join(digits[i:i + n]))
    return sep.join(groups)


def _fromhex_text(string):
    # _PyBytes_FromHex: pairs of hex digits, spaces between pairs
    if not isinstance(string, str):
        raise TypeError("fromhex() argument must be str, not " + _type_name(type(string)))
    out = []
    i = 0
    n = len(string)
    while i < n:
        c = string[i]
        if c in " \t\n\r\x0b\x0c":
            i += 1
            continue
        hi = _int_digit(c)
        if hi >= 16 or i + 1 >= n or _int_digit(string[i + 1]) >= 16:
            pos = i if hi >= 16 else i + 1
            raise ValueError("non-hexadecimal number found in fromhex() arg at position " + str(pos))
        out.append(chr(hi * 16 + _int_digit(string[i + 1])))
        i += 2
    return "".join(out)


def _bytes_fromhex(cls, string, /):
    # bytes.fromhex (bytes_fromhex_impl): a subclass's from the bytes
    r = _cetta_bytes(_fromhex_text(string))
    return r if cls is bytes else cls(r)


def _bytearray_fromhex(cls, string, /):
    r = _bytearray_of(_fromhex_text(string))
    return r if cls is bytearray else cls(r)


_BYTES_SPACE = " \t\n\r\x0b\x0c"


def _ascii_view(v):
    # the ASCII part of a bytes text: other bytes stand as uncased NULs
    return v if _cetta_str_is_ascii(v) else "".join(c if c < "\x80" else chr(0) for c in v)


def _bytes_method(name, kinds):
    # a bytes method that is the str method on the text: KINDS gives each
    # argument's kind, "b" bytes-like, "f" one fill byte, "o" as it is; a str
    # result is made bytes again
    def method(self, *args):
        v = _bytes_like(self, name)
        conv = []
        for i in range(len(args)):
            a = args[i]
            k = kinds[i] if i < len(kinds) else "o"
            if k == "b":
                conv.append(_bytes_like(a, name))
            elif k == "f":
                f = _bview(a)
                if f is _NULL or len(f) != 1:
                    raise TypeError(name + "() argument " + str(i + 1) + " must be a byte string of length 1, not "
                                    + _type_name(type(a)))
                conv.append(f)
            else:
                conv.append(a)
        r = getattr(str, name)(v, *conv)
        if isinstance(r, str):
            return _bytes_wrap(self, r)
        if isinstance(r, tuple):
            return tuple(_bytes_wrap(self, x) for x in r)
        return r
    method.__name__ = name
    return method


def _bytes_arg_or_int(x):
    # a byte argument of find, count, index: bytes-like or an int 0..255
    v = _bview(x)
    if v is not _NULL:
        return v
    if isinstance(x, int):
        if x < 0 or x > 255:
            raise ValueError("byte must be in range(0, 256)")
        return chr(x)
    raise TypeError("argument should be integer or bytes-like object, not '" + _type_name(type(x)) + "'")


def _bytes_search(name):
    def method(self, sub, *args):
        v = _bytes_like(self, name)
        try:
            return getattr(str, name)(v, _bytes_arg_or_int(sub), *args)
        except ValueError as e:
            if name in ("index", "rindex") and str(e) == "substring not found":
                raise ValueError("subsection not found") from None
            raise
    method.__name__ = name
    return method


def _bytes_tailmatch(name):
    def method(self, affix, *args):
        v = _bytes_like(self, name)
        if isinstance(affix, tuple):
            affix = tuple(_bytes_like(a, name) for a in affix)
        else:
            if _bview(affix) is _NULL:
                raise TypeError(name + " first arg must be bytes or a tuple of bytes, not " + _type_name(type(affix)))
            affix = _bview(affix)
        return getattr(str, name)(v, affix, *args)
    method.__name__ = name
    return method


def _bytes_strip_by(self, chars, side):
    v = _bytes_like(self, "strip")
    if chars is None:
        return _bytes_wrap(self, _cetta_str_trim_chars(_BYTES_SPACE, side, v))
    return _bytes_wrap(self, _cetta_str_trim_chars(_bytes_like(chars, "strip"), side, v))


def _bytes_strip(self, bytes=None, /):
    return _bytes_strip_by(self, bytes, "both")


def _bytes_lstrip(self, bytes=None, /):
    return _bytes_strip_by(self, bytes, "left")


def _bytes_rstrip(self, bytes=None, /):
    return _bytes_strip_by(self, bytes, "right")


def _bytes_split_ws(v, maxsplit, from_right):
    # split on runs of ASCII whitespace
    words = []
    i = 0
    n = len(v)
    while i < n:
        while i < n and v[i] in _BYTES_SPACE:
            i += 1
        if i == n:
            break
        j = i
        while j < n and v[j] not in _BYTES_SPACE:
            j += 1
        words.append((i, j))
        i = j
    if maxsplit < 0 or len(words) <= maxsplit + 1:
        return [v[a:b] for a, b in words]
    if not from_right:
        return [v[a:b] for a, b in words[:maxsplit]] + [v[words[maxsplit][0]:]]
    keep = len(words) - maxsplit
    return [v[:words[keep - 1][1]]] + [v[a:b] for a, b in words[keep:]]


def _bytes_split(self, sep=None, maxsplit=-1):
    v = _bytes_like(self, "split")
    if sep is None:
        return [_bytes_wrap(self, x) for x in _bytes_split_ws(v, _index(maxsplit), False)]
    return [_bytes_wrap(self, x) for x in str.split(v, _bytes_like(sep, "split"), maxsplit)]


def _bytes_rsplit(self, sep=None, maxsplit=-1):
    v = _bytes_like(self, "rsplit")
    if sep is None:
        return [_bytes_wrap(self, x) for x in _bytes_split_ws(v, _index(maxsplit), True)]
    return [_bytes_wrap(self, x) for x in str.rsplit(v, _bytes_like(sep, "rsplit"), maxsplit)]


def _bytes_splitlines(self, keepends=False):
    # lines end at \n, \r or \r\n only
    v = _bytes_like(self, "splitlines")
    lines = []
    i = 0
    start = 0
    n = len(v)
    while i < n:
        c = v[i]
        if c == "\n" or c == "\r":
            end = i + 1
            if c == "\r" and end < n and v[end] == "\n":
                end += 1
            lines.append(v[start:end] if keepends else v[start:i])
            i = start = end
        else:
            i += 1
    if start < n:
        lines.append(v[start:])
    return [_bytes_wrap(self, x) for x in lines]


def _bytes_join(self, iterable, /):
    v = _bytes_like(self, "join")
    parts = []
    i = 0
    for x in iterable:
        p = _bview(x)
        if p is _NULL:
            raise TypeError("sequence item " + str(i) + ": expected a bytes-like object, " + _type_name(type(x)) + " found")
        parts.append(p)
        i += 1
    return _bytes_wrap(self, v.join(parts))


def _bytes_case(how):
    def method(self):
        v = _bytes_like(self, how)
        if how == "upper" or how == "lower":
            return _bytes_wrap(self, _cetta_ascii_case(v, how))
        out = []
        prev_cased = False
        first = True
        for c in v:
            if how == "swapcase":
                out.append(c.lower() if "A" <= c <= "Z" else c.upper() if "a" <= c <= "z" else c)
            elif how == "capitalize":
                out.append(c.upper() if first and "a" <= c <= "z" else c.lower() if not first and "A" <= c <= "Z" else c)
            else:
                cased = "a" <= c <= "z" or "A" <= c <= "Z"
                if cased:
                    out.append(c.lower() if prev_cased else c.upper())
                else:
                    out.append(c)
                prev_cased = cased
            first = False
        return _bytes_wrap(self, "".join(out))
    method.__name__ = how
    return method


def _bytes_test(name):
    # the ASCII is- tests (Py_ISALPHA and its kin)
    def method(self):
        v = _bytes_like(self, name)
        if name == "isascii":
            return _cetta_str_is_ascii(v)
        if name == "isspace":
            return v != "" and all(c in _BYTES_SPACE for c in v)
        a = _ascii_view(v)
        if name in ("islower", "isupper", "istitle"):
            return getattr(str, name)(a)
        return v != "" and _cetta_str_is_ascii(v) and getattr(str, name)(v)
    method.__name__ = name
    return method


def _bytes_translate(self, table, /, delete=b""):
    v = _bytes_like(self, "translate")
    d = _bytes_like(delete, "translate")
    if table is not None:
        t = _bytes_like(table, "translate")
        if len(t) != 256:
            raise ValueError("translation table must be 256 characters long")
    out = []
    for c in v:
        if c in d:
            continue
        out.append(t[ord(c)] if table is not None else c)
    return _bytes_wrap(self, "".join(out))


def _bytes_maketrans(frm, to, /):
    f = _bytes_like(frm, "maketrans")
    t = _bytes_like(to, "maketrans")
    if len(f) != len(t):
        raise ValueError("maketrans arguments must have same length")
    table = [chr(i) for i in range(256)]
    for a, b in zip(f, t):
        table[ord(a)] = b
    return _cetta_bytes("".join(table))


def _bytes_mod(self, args):
    # bytes % args (bytes_format): %s and %b take bytes-like objects, the
    # numbers format as str's do
    v = _bytes_like(self, "%")
    if isinstance(args, tuple):
        items = tuple(_bview(a) if _bview(a) is not _NULL else a for a in args)
    else:
        items = _bview(args) if _bview(args) is not _NULL else args
    return _bytes_wrap(self, v.replace("%b", "%s") % items)


def _bytes_removefix(name):
    def method(self, affix, /):
        v = _bytes_like(self, name)
        return _bytes_wrap(self, getattr(str, name)(v, _bytes_like(affix, name)))
    method.__name__ = name
    return method


def _bytes_getnewargs(self):
    return (_cetta_bytes(_bytes_like(self, "__getnewargs__")),)


# ---------------------------------------------------------------- bytearray

# A bytearray is an object whose field data holds a list of its bytes, ints
# from 0 to 255: a byte is read, replaced or appended in constant time, as in
# CPython's buffer.  The list is made with the object and only ever changed in
# place (an iterator reads it as it is at each step).  The bytes methods read
# its bytes through the buffer view and give a new bytearray.

def _bytearray_data(self):
    return _cetta_field(self, "data")


def _bytearray_of_items(items):
    o = _cetta_new_object(bytearray)
    _cetta_set_field(o, "data", items)
    return o


def _bytearray_of(v):
    # a new bytearray of the bytes of the text V
    return _bytearray_of_items(_cetta_items_of_view(v))


def _bytevalue(x):
    # _getbytevalue: an index from 0 to 255
    n = _index(x)
    if n < 0 or n > 255:
        raise ValueError("byte must be in range(0, 256)")
    return n


def _ssize(x):
    # a Py_ssize_t argument (Argument Clinic's): an index that fits
    n = _index(x)
    if n > 9223372036854775807 or n < -9223372036854775808:
        raise OverflowError("Python int too large to convert to C ssize_t")
    return n


def _subscript_index(index, n, what):
    # PyNumber_AsSsize_t(index, IndexError), counted from the end when
    # negative, within the N items
    i = _index(index)
    if i > 9223372036854775807 or i < -9223372036854775808:
        raise IndexError("cannot fit '" + _type_name(type(index)) + "' into an index-sized integer")
    if i < 0:
        i += n
    if i < 0 or i >= n:
        raise IndexError(what + " index out of range")
    return i


def _is_index(x):
    return isinstance(x, int) or _mro_lookup(type(x), "__index__") is not _NULL


def _is_number(x):
    # PyNumber_Check: an index, an int's or a float's conversion, a complex
    if isinstance(x, complex):
        return True
    t = type(x)
    return (_mro_lookup(t, "__index__") is not _NULL or _mro_lookup(t, "__int__") is not _NULL
            or _mro_lookup(t, "__float__") is not _NULL)


def _bytearray_new(cls, *args, **kwargs):
    # tp_new (PyType_GenericNew): empty; __init__ reads the arguments
    if not isinstance(cls, type):
        raise TypeError("bytearray.__new__(X): X is not a type object (" + _type_name(type(cls)) + ")")
    if not issubclass(cls, bytearray):
        raise TypeError("bytearray.__new__(" + _type_name(cls) + "): " + _type_name(cls)
                        + " is not a subtype of bytearray")
    o = _cetta_new_object(cls)
    _cetta_set_field(o, "data", [])
    return o


def _bytearray_init(self, source=_NULL, encoding=_NULL, errors=_NULL):
    # bytearray___init___impl: emptied first; then a str's encoding, an int's
    # zeros, a bytes-like object's bytes, or an iterable's ints
    data = _bytearray_data(self)
    data.clear()
    if isinstance(source, str):
        if encoding is _NULL:
            raise TypeError("string argument without an encoding")
        data.extend(_cetta_items_of_view(_bview(_str_encode(source, encoding, "strict" if errors is _NULL else errors))))
        return None
    if encoding is not _NULL:
        raise TypeError("encoding without a string argument")
    if errors is not _NULL:
        raise TypeError("errors without a string argument")
    if source is _NULL:
        return None
    if _is_index(source):
        try:
            n = _index(source)
        except TypeError:
            n = _NULL
        if n is not _NULL:
            if n > 9223372036854775807:
                raise OverflowError("cannot fit '" + _type_name(type(source)) + "' into an index-sized integer")
            if n < 0:
                raise ValueError("negative count")
            data.extend([0] * n)
            return None
    v = _bview(source)
    if v is not _NULL:
        data.extend(_cetta_items_of_view(v))
        return None
    try:
        it = iter(source)
    except TypeError:
        raise TypeError("cannot convert '" + _type_name(type(source)) + "' object to bytearray") from None
    for x in it:
        data.append(_bytevalue(x))
    return None


def _bytearray_repr(self):
    # bytearray_repr: the class's name, then the bytes' repr
    return _type_name(type(self)) + "(" + repr(_cetta_bytes(_bview(self))) + ")"


def _bytearray_len(self):
    return len(_bytearray_data(self))


def _bytearray_getitem(self, index):
    # bytearray_subscript: an int, or a new bytearray of a slice's bytes
    data = _bytearray_data(self)
    if isinstance(index, slice):
        return _bytearray_of_items(data[index])
    if not _is_index(index):
        raise TypeError("bytearray indices must be integers or slices, not " + _type_name(type(index)))
    return data[_subscript_index(index, len(data), "bytearray")]


def _bytearray_setitem(self, index, value):
    # bytearray_ass_subscript: an index's byte (the value read before the
    # bound is checked); a slice's bytes
    data = _bytearray_data(self)
    if isinstance(index, slice):
        return _bytearray_assign_slice(self, data, index, value)
    if not _is_index(index):
        raise TypeError("bytearray indices must be integers or slices, not " + _type_name(type(index)))
    i = _index(index)
    if i > 9223372036854775807 or i < -9223372036854775808:
        raise IndexError("cannot fit '" + _type_name(type(index)) + "' into an index-sized integer")
    b = _bytevalue(value)
    n = len(data)
    if i < 0:
        i += n
    if i < 0 or i >= n:
        raise IndexError("bytearray index out of range")
    data[i] = b
    return None


def _bytearray_assign_slice(self, data, index, value):
    # the bounds read first; the value a bytearray other than self as it is,
    # else (neither a number nor a str) made one; a simple slice's bytes
    # replaced, an extended slice's deleted (no bytes) or assigned one by one
    start, stop, step = _cetta_slice_indices(index, len(data))
    if value is self or not isinstance(value, bytearray):
        if _is_number(value) or isinstance(value, str):
            raise TypeError("can assign only bytes, buffers, or iterables of ints in range(0, 256)")
        value = bytearray(value)
    items = _bytearray_data(value)
    if step == 1:
        data[start:max(start, stop)] = items
        return None
    if len(items) == 0:
        del data[index]
        return None
    places = range(start, stop, step)
    if len(items) != len(places):
        raise ValueError("attempt to assign bytes of size " + str(len(items)) + " to extended slice of size "
                         + str(len(places)))
    for k in range(len(places)):
        data[places[k]] = items[k]
    return None


def _bytearray_delitem(self, index):
    data = _bytearray_data(self)
    if isinstance(index, slice):
        del data[index]
        return None
    if not _is_index(index):
        raise TypeError("bytearray indices must be integers or slices, not " + _type_name(type(index)))
    del data[_subscript_index(index, len(data), "bytearray")]
    return None


def _bytearray_iter(self):
    return _cetta_bytearray_iter(_bytearray_data(self))


def _bytearray_contains(self, item):
    return _cetta_bytes_contains(_bview(self), item)


def _bytearray_add(self, other):
    # bytearray_concat: a new bytearray of both operands' bytes
    v = _bview(other)
    if v is _NULL:
        raise TypeError("can't concat " + _type_name(type(other)) + " to " + _type_name(type(self)))
    return _bytearray_of(_bview(self) + v)


def _bytearray_iadd(self, other):
    # bytearray_iconcat: the other's bytes appended
    v = _bview(other)
    if v is _NULL:
        raise TypeError("can't concat " + _type_name(type(other)) + " to " + _type_name(type(self)))
    _bytearray_data(self).extend(_cetta_items_of_view(v))
    return self


def _bytearray_mul(self, count):
    # bytearray_repeat: a new bytearray (none for a count below one)
    n = _ssize(count)
    return _bytearray_of(_bview(self) * n if n > 0 else "")


def _bytearray_imul(self, count):
    # bytearray_irepeat: in place
    n = _ssize(count)
    data = _bytearray_data(self)
    if n <= 0:
        data.clear()
    else:
        data *= n
    return self


def _bytearray_rmod(self, other):
    # bytearray's nb_remainder reflected: only a bytearray formats
    if not isinstance(other, bytearray):
        return NotImplemented
    return _bytes_mod(other, self)


def _bytearray_compare(name):
    # bytearray_richcompare: with any bytes-like object, by their bytes
    def method(self, other):
        v = _bview(other)
        if v is _NULL:
            return NotImplemented
        s = _bview(self)
        if name == "__eq__":
            return s == v
        if name == "__ne__":
            return s != v
        if name == "__lt__":
            return s < v
        if name == "__le__":
            return s <= v
        if name == "__gt__":
            return s > v
        return s >= v
    method.__name__ = name
    return method


def _bytearray_append(self, item, /):
    _bytearray_data(self).append(_bytevalue(item))
    return None


def _bytearray_extend(self, iterable_of_ints, /):
    # bytearray_extend: a bytes-like object's bytes; else an iterable's ints,
    # all read before any is added
    data = _bytearray_data(self)
    v = _bview(iterable_of_ints)
    if v is not _NULL:
        data.extend(_cetta_items_of_view(v))
        return None
    try:
        it = iter(iterable_of_ints)
    except TypeError:
        raise TypeError("can't extend bytearray with " + _type_name(type(iterable_of_ints))) from None
    items = []
    for x in it:
        try:
            items.append(_bytevalue(x))
        except TypeError:
            if isinstance(iterable_of_ints, str):
                raise TypeError("expected iterable of integers; got: 'str'") from None
            raise
    data.extend(items)
    return None


def _bytearray_pop(self, index=-1, /):
    data = _bytearray_data(self)
    i = _ssize(index)
    n = len(data)
    if n == 0:
        raise IndexError("pop from empty bytearray")
    if i < 0:
        i += n
    if i < 0 or i >= n:
        raise IndexError("pop index out of range")
    return data.pop(i)


def _bytearray_remove(self, value, /):
    data = _bytearray_data(self)
    b = _bytevalue(value)
    try:
        i = data.index(b)
    except ValueError:
        raise ValueError("value not found in bytearray") from None
    del data[i]
    return None


def _bytearray_insert(self, index, item, /):
    data = _bytearray_data(self)
    i = _ssize(index)
    data.insert(i, _bytevalue(item))
    return None


def _bytearray_clear(self, /):
    _bytearray_data(self).clear()
    return None


def _bytearray_copy(self, /):
    return _bytearray_of_items(_bytearray_data(self).copy())


def _bytearray_reverse(self, /):
    _bytearray_data(self).reverse()
    return None


def _bytearray_alloc(self, /):
    n = len(_bytearray_data(self))
    return n + 1 if n else 0


def _bytearray_sizeof(self, /):
    return 56 + _bytearray_alloc(self)


def _bytearray_reduce_ex(self, proto=0, /):
    # _common_reduce: before protocol 3 the bytes as Latin-1 text; the
    # instance's attributes, if any
    d = _cetta_object_dict(self) if _cetta_is_python_object(self) and type(self) is not bytearray else None
    state = d if d else None
    if proto < 3:
        return (type(self), (_bview(self), "latin-1"), state)
    return (type(self), (_cetta_bytes(_bview(self)),), state)


def _bytearray_reduce(self, /):
    return _bytearray_reduce_ex(self, 0)


# ---------------------------------------------------------------- modules

_module = _cetta_builtin_type("module")
_sys_modules = {}


def _new_module(name):
    # a module object: its namespace is its __dict__
    m = _cetta_new_object(_module)
    d = _cetta_object_dict(m)
    d["__name__"] = name
    d["__doc__"] = None
    d["__package__"] = None
    d["__loader__"] = None
    d["__spec__"] = None
    return m


def _module_new(cls, *args, **kwargs):
    # new_module: a module of an empty namespace
    m = _cetta_new_object(cls)
    _cetta_object_dict(m)
    return m


def _module_init(self, *args, **kwargs):
    # module___init___impl, module(name, doc=None), read as Argument Clinic
    # reads it: the namespace's names set as module_init_dict sets them
    name, doc = _clinic_args("module", ("name", "doc"), 1, 2, args, kwargs)
    if doc is _NULL:
        doc = None
    if not isinstance(name, str):
        raise TypeError("module() argument 'name' must be str, not " + _type_name(type(name)))
    d = _cetta_object_dict(self)
    d["__name__"] = name
    d["__doc__"] = doc
    d["__package__"] = None
    d["__loader__"] = None
    d["__spec__"] = None


_cetta_set_type_attr(_module, "__new__", staticmethod(_module_new))
_cetta_set_type_attr(_module, "__init__", _module_init)
_member(_module, "__dict__", "dict")
for _name in ("__annotations__", "__annotate__"):
    _cetta_set_type_attr(_module, _name, _cetta_getset(_name))
_cetta_set_type_attr(_cetta_builtin_type("cell"), "cell_contents", _cetta_getset("cell_contents"))


def _spec_initializing(spec):
    # _PyModuleSpec_IsInitializing: whether a module's spec says it is
    # still being imported
    if spec is None:
        return False
    try:
        v = spec._initializing
    except AttributeError:
        return False
    return True if v else False


def _module_repr(self):
    d = _cetta_object_dict(self)
    name = d["__name__"] if "__name__" in d else "?"
    if "__file__" in d:
        return "<module " + repr(name) + " from " + repr(d["__file__"]) + ">"
    if _cetta_field(self, "builtin") is True:
        return "<module " + repr(name) + " (built-in)>"
    return "<module " + repr(name) + ">"


def _module_getattribute(self, name):
    # _Py_module_getattro_impl: the generic lookup (a lazy import's
    # placeholder found there resolved in its place); else a package's
    # pending lazily declared submodule, imported now; then the module's
    # __getattr__ (PEP 562), else "module 'NAME' has no attribute ..."
    d = _cetta_object_dict(self)
    try:
        attr = _object_getattribute(self, name)
    except AttributeError:
        attr = _NULL
    if attr is _NULL:
        attr, recheck = _lazy_try_load_submodule(self, name)
        if attr is _NULL and recheck:
            attr = d.get(name, _NULL)
    if attr is not _NULL:
        if type(attr) is _lazy_import_type:
            return _lazy_reify(attr, name, d)
        return attr
    getattr_hook = d.get("__getattr__", _NULL)
    if type(getattr_hook) is _lazy_import_type:
        # module_get_resolved_dict_item: a placeholder being resolved is absent
        getattr_hook = _NULL if getattr_hook in _lazy_resolving else _lazy_reify(getattr_hook, "__getattr__", d)
    if getattr_hook is not _NULL:
        return getattr_hook(name)
    mod_name = d["__name__"] if "__name__" in d else None
    if isinstance(mod_name, str):
        raise _cetta_attribute_error_text("module '" + mod_name + "' has no attribute '" + name + "'", self, name)
    raise _cetta_attribute_error_text("module has no attribute '" + name + "'", self, name)


# ---------------------------------------------------------------- dir()

def _merge_class_dict(d, aclass):
    # merge_class_dict: a class's __dict__, then each of its __bases__'s
    classdict = getattr(aclass, "__dict__", _NULL)
    if classdict is not _NULL:
        d.update(classdict)
    bases = getattr(aclass, "__bases__", _NULL)
    if bases is not _NULL:
        for i in range(len(bases)):
            _merge_class_dict(d, bases[i])


def _object_dir(self):
    # object.__dir__: the instance's __dict__ (when a dict), its class's
    d = getattr(self, "__dict__", _NULL)
    if d is _NULL or not isinstance(d, dict):
        d = {}
    else:
        d = dict(d)
    cls = getattr(self, "__class__", _NULL)
    if cls is not _NULL:
        _merge_class_dict(d, cls)
    return list(d)


def _type_dir(self):
    # type.__dir__: the class's namespace and its bases'
    d = {}
    _merge_class_dict(d, self)
    return list(d)


def _module_dir(self):
    # module.__dir__: the module's own __dir__ (PEP 562), else its namespace
    d = self.__dict__
    if not isinstance(d, dict):
        raise TypeError("<module>.__dict__ is not a dictionary")
    if "__dir__" in d:
        return d["__dir__"]()
    return list(d)


def _dir(obj):
    # dir(obj) (_dir_object): type(obj).__dir__(obj), sorted
    f = _lookup_special(obj, "__dir__")
    if f is _NULL:
        raise TypeError("object does not provide __dir__")
    names = list(f())
    names.sort()
    return names


def _dir_locals(ns):
    # dir() (_dir_locals): the caller's local names, sorted
    names = list(ns.keys())
    names.sort()
    return names


# ---------------------------------------------------------------- compile(), exec(), eval()

# A code object holds the core code the stages before the run made of a text
# (read as a file, or as eval_input); exec() and eval() run it with a globals
# dict and a locals mapping.
_code_type = _cetta_builtin_type("code")


def _code_name(self):
    return _cetta_field(self, "name")


def _code_filename(self):
    return _cetta_field(self, "filename")


def _code_firstlineno(self):
    return _cetta_code_info(self)[1]


def _code_qualname(self):
    return _cetta_code_info(self)[0]


def _code_argcount(self):
    return _cetta_code_info(self)[2]


def _code_posonlyargcount(self):
    return _cetta_code_info(self)[3]


def _code_kwonlyargcount(self):
    return _cetta_code_info(self)[4]


def _code_varnames(self):
    return _cetta_code_info(self)[5]


def _code_nlocals(self):
    return len(_cetta_code_info(self)[5])


def _code_cellvars(self):
    return _cetta_code_info(self)[6]


def _code_freevars(self):
    return _cetta_code_info(self)[7]


def _frame_repr(self):
    # frame_repr: where the frame is
    return ("<frame at " + _cetta_address(self) + ", file " + repr(self.f_code.co_filename)
            + ", line " + str(self.f_lineno) + ", code " + self.f_code.co_name + ">")


def _code_repr(self):
    # code_repr
    return ("<code object " + _cetta_field(self, "name") + " at " + _cetta_address(self) + ", file \""
            + _cetta_field(self, "filename") + "\", line 1>")


def _source_text(source, funcname, what):
    # _Py_SourceAsString for a str: its text, which may not hold a NUL
    if not isinstance(source, str):
        raise TypeError(funcname + "() arg 1 must be a " + what + " object")
    text = str.__str__(source)
    if chr(0) in text:
        raise SyntaxError("source code string cannot contain null bytes")
    return text


def _read_failure(why):
    # the reader's refusal, "line L, column C: unexpected 'T'; expected ...":
    # where, and the token it refused
    line, offset, token = 1, 1, ""
    if why.startswith("line "):
        head, sep, rest = why.partition(": ")
        a, sep2, b = head[5:].partition(", column ")
        if a.isdigit() and b.isdigit():
            line, offset = int(a), int(b)
        if rest.startswith("unexpected '"):
            token = rest[12:].partition("'")[0]
    return line, offset, token


def _source_line(text, line, mode):
    # the refused line's text with its end of line (a file's text is read as
    # if it ended with one; eval's is not)
    lines = text.split("\n")
    if line < 1 or line > len(lines):
        return None
    if line < len(lines):
        return lines[line - 1] + "\n"
    return lines[line - 1] + ("" if mode == "eval" else "\n")


def _getline(filename, lineno):
    # linecache.getline: a file's line, read as text with universal newlines,
    # with its end of line; "" when there is none
    if not isinstance(filename, str) or not _cetta_fs_exists(filename):
        return ""
    r = _cetta_fs_read(filename)
    if r[0] != "ok" or r[1] is None:
        return ""
    text = r[1].replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")
    if text.endswith("\n"):
        lines.pop()
    if 1 <= lineno <= len(lines):
        return lines[lineno - 1] + "\n"
    return ""


def _show_compile_warnings(filename, warnings):
    # a compilation's SyntaxWarnings, (MESSAGE, LINE, COL, END LINE, END COL)
    # each, as Python/_warnings.c shows them under the default filters (each
    # one, every time): warnings._showwarnmsg_impl writes
    # _formatwarnmsg_impl's text to sys.stderr
    for w in warnings:
        text = str(filename) + ":" + str(w[1]) + ": SyntaxWarning: " + w[0] + "\n"
        line = _getline(filename, w[1])
        if line:
            text += "  " + line.strip() + "\n"
        f = getattr(_sys_modules["sys"], "stderr", None)
        if f is not None:
            try:
                f.write(text)
            except OSError:
                pass


def _compile_text(text, filename, mode):
    # the stages before the run on TEXT; a refusal raised as CPython raises
    # it: the reader's at the token it refused ("invalid syntax", or an
    # unexpected indent), with the line's text; a stage's with its message.
    # The warnings the compilation emits come first.
    r = _cetta_compile(text, filename, mode)
    _show_compile_warnings(filename, r[-1])
    if r[0] == "ok":
        return r[1]
    if r[0] == "read":
        line, offset, token = _read_failure(r[1])
        line_text = _source_line(text, line, mode)
        if token and not token.strip():
            raise IndentationError("unexpected indent", (filename, line, len(token), line_text, line, -1))
        raise SyntaxError("invalid syntax", (filename, line, offset, line_text, line, offset + 1))
    raise SyntaxError(r[1], (filename, r[2], r[3] + 1, None, r[4], r[5] + 1))


def _parse_text(text, filename, mode):
    # the ast stage on TEXT, refused as _compile_text refuses
    r = _cetta_parse(text, filename, mode)
    _show_compile_warnings(filename, r[-1])
    if r[0] == "ok":
        return r[1]
    if r[0] == "read":
        line, offset, token = _read_failure(r[1])
        line_text = _source_line(text, line, mode)
        if token and not token.strip():
            raise IndentationError("unexpected indent", (filename, line, len(token), line_text, line, -1))
        raise SyntaxError("invalid syntax", (filename, line, offset, line_text, line, offset + 1))
    raise SyntaxError(r[1], (filename, r[2], r[3] + 1, None, r[4], r[5] + 1))


def compile(source, filename, mode, flags=0, dont_inherit=False, optimize=-1, *, module=None, _feature_version=-1):
    # builtin_compile_impl: a text compiled, or read to its AST
    # (PyCF_ONLY_AST); an AST compiled.  MODULE names the module the
    # compilation's warnings are attributed to
    if module is not None and not isinstance(module, str):
        raise TypeError("compile() argument 'module' must be str or None, not " + _fqn(type(module)))
    if optimize < -1 or optimize > 2:
        raise ValueError("compile(): invalid optimize value")
    if mode not in ("exec", "eval", "single"):
        raise ValueError("compile() mode must be 'exec', 'eval' or 'single'")
    if not isinstance(filename, str):
        raise TypeError("compile() argument 'filename' must be str, not " + _type_name(type(filename)))
    if type(source) not in (str, bytes, bytearray):
        _ast = __import__("_ast")
        if isinstance(source, _ast.AST):
            if flags & _ast.PyCF_ONLY_AST:
                return source
            expected = {"exec": _ast.Module, "eval": _ast.Expression, "single": _ast.Interactive}[mode]
            if not isinstance(source, expected):
                raise TypeError("expected " + expected.__name__ + " node, got " + type(source).__name__)
            r = _cetta_compile_ast(_ast._to_encoding(source), filename, mode)
            _show_compile_warnings(filename, r[-1])
            if r[0] == "ok":
                return r[1]
            raise SyntaxError(r[1], (filename, r[2], r[3] + 1, None, r[4], r[5] + 1))
    if mode == "single":
        raise NotImplementedError("compile() in mode 'single' (an interactive statement)")
    text = _source_text(source, "compile", "string, bytes or AST")
    if flags & 1024:
        _ast = __import__("_ast")
        return _ast._from_encoding(_parse_text(text, filename, mode))
    return _compile_text(text, filename, mode)


def _with_builtins(globals):
    # __builtins__ put in a globals dict that has none
    if "__builtins__" not in globals:
        globals["__builtins__"] = _cetta_object_dict(_builtins_module)


def _exec(caller_globals, caller_locals, source, /, globals=None, locals=None, *, closure=None):
    # builtin_exec_impl: the caller's globals and locals unless given; a text
    # compiled as a file
    if globals is None:
        globals = caller_globals
        if locals is None:
            locals = caller_locals
    elif locals is None:
        locals = globals
    if not isinstance(globals, dict):
        raise TypeError("exec() globals must be a dict or a frozendict, not " + _type_name(type(globals)))
    if _mro_lookup(type(locals), "__getitem__") is _NULL:
        raise TypeError("locals must be a mapping or None, not " + _type_name(type(locals)))
    _with_builtins(globals)
    if type(source) is _code_type:
        if closure is not None:
            raise TypeError("cannot use a closure with this code object")
        code = source
    else:
        if closure is not None:
            raise TypeError("closure can only be used when source is a code object")
        code = _compile_text(_source_text(source, "exec", "string, bytes or code"), "<string>", "exec")
    _cetta_run_code(code, globals, locals)
    return None


def _eval(caller_globals, caller_locals, source, /, globals=None, locals=None):
    # builtin_eval_impl: a text compiled as eval_input, its leading blanks
    # skipped
    if locals is not None and _mro_lookup(type(locals), "__getitem__") is _NULL:
        raise TypeError("locals must be a mapping")
    if globals is not None and not isinstance(globals, dict):
        if _mro_lookup(type(globals), "__getitem__") is not _NULL:
            raise TypeError("globals must be a real dict or a frozendict; try eval(expr, {}, mapping)")
        raise TypeError("globals must be a dict")
    if globals is None:
        globals = caller_globals
        if locals is None:
            locals = caller_locals
    elif locals is None:
        locals = globals
    _with_builtins(globals)
    if type(source) is _code_type:
        code = source
    else:
        text = _source_text(source, "eval", "string, bytes or code")
        i = 0
        while i < len(text) and (text[i] == " " or text[i] == "\t"):
            i += 1
        code = _compile_text(text[i:], "<string>", "eval")
    return _cetta_run_code(code, globals, locals)


def _main_namespace(path, file):
    # the __main__ module of the program at PATH (as given: sys.argv[0]),
    # FILE its absolute path (__file__, config_run_filename_abspath); its
    # directory first on sys.path
    m = _new_module("__main__")
    d = _cetta_object_dict(m)
    d["__builtins__"] = _builtins_module
    d["__file__"] = file
    _sys_modules["__main__"] = m
    sys = _sys_modules["sys"]
    sys.argv.clear()
    sys.argv.append(path)
    sys.path.clear()
    sys.path.append(_cetta_directory_of(path))
    return d


def _builtins_getattr(name):
    b = _cetta_builtin(name)
    if b is _NULL:
        raise _cetta_attribute_error_text("module 'builtins' has no attribute '" + name + "'", _builtins_module, name)
    return b


def _sys_exit(status=None):
    if status is None:
        raise SystemExit
    raise SystemExit(status)


def _sys_exc_info():
    e = _cetta_handled()
    if e is None:
        return (None, None, None)
    return (type(e), e, e.__traceback__)


def _sys_getframe(depth=0):
    # sys._getframe: the frame DEPTH calls out from its caller's
    return _cetta_frame(depth)


def _sys_exception():
    return _cetta_handled()


# sys.stdlib_module_names, and the reasons some of them are missing
# (Python/stdlib_module_names.h, Lib/_missing_stdlib_info.py), as CPython 3.15 has them
_STDLIB_MODULE_NAMES = frozenset((
    '__future__', '_abc', '_aix_support', '_android_support', '_apple_support', '_ast',
    '_ast_unparse', '_asyncio', '_bisect', '_blake2', '_bz2', '_codecs', '_codecs_cn',
    '_codecs_hk', '_codecs_iso2022', '_codecs_jp', '_codecs_kr', '_codecs_tw', '_collections',
    '_collections_abc', '_colorize', '_compat_pickle', '_contextvars', '_csv', '_ctypes',
    '_curses', '_curses_panel', '_datetime', '_dbm', '_decimal', '_elementtree',
    '_frozen_importlib', '_frozen_importlib_external', '_functools', '_gdbm', '_hashlib', '_heapq',
    '_hmac', '_imp', '_interpchannels', '_interpqueues', '_interpreters', '_io', '_ios_support',
    '_json', '_locale', '_lsprof', '_lzma', '_markupbase', '_math_integer', '_md5',
    '_multibytecodec', '_multiprocessing', '_opcode', '_opcode_metadata', '_operator',
    '_osx_support', '_overlapped', '_pickle', '_posixshmem', '_posixsubprocess', '_py_abc',
    '_py_warnings', '_pydatetime', '_pydecimal', '_pyio', '_pylong', '_pyrepl', '_queue',
    '_random', '_remote_debugging', '_scproxy', '_sha1', '_sha2', '_sha3', '_signal',
    '_sitebuiltins', '_socket', '_sqlite3', '_sre', '_ssl', '_stat', '_statistics', '_string',
    '_strptime', '_struct', '_suggestions', '_symtable', '_sysconfig', '_thread',
    '_threading_local', '_tkinter', '_tokenize', '_tracemalloc', '_types', '_typing', '_uuid',
    '_warnings', '_weakref', '_weakrefset', '_winapi', '_wmi', '_zoneinfo', '_zstd', 'abc',
    'annotationlib', 'antigravity', 'argparse', 'array', 'ast', 'asyncio', 'atexit', 'base64',
    'bdb', 'binascii', 'bisect', 'builtins', 'bz2', 'cProfile', 'calendar', 'cmath', 'cmd', 'code',
    'codecs', 'codeop', 'collections', 'colorsys', 'compileall', 'compression', 'concurrent',
    'configparser', 'contextlib', 'contextvars', 'copy', 'copyreg', 'csv', 'ctypes', 'curses',
    'dataclasses', 'datetime', 'dbm', 'decimal', 'difflib', 'dis', 'doctest', 'email', 'encodings',
    'ensurepip', 'enum', 'errno', 'faulthandler', 'fcntl', 'filecmp', 'fileinput', 'fnmatch',
    'fractions', 'ftplib', 'functools', 'gc', 'genericpath', 'getopt', 'getpass', 'gettext',
    'glob', 'graphlib', 'grp', 'gzip', 'hashlib', 'heapq', 'hmac', 'html', 'http', 'idlelib',
    'imaplib', 'importlib', 'inspect', 'io', 'ipaddress', 'itertools', 'json', 'keyword',
    'linecache', 'locale', 'logging', 'lzma', 'mailbox', 'marshal', 'math', 'mimetypes', 'mmap',
    'modulefinder', 'msvcrt', 'multiprocessing', 'netrc', 'nt', 'ntpath', 'nturl2path', 'numbers',
    'opcode', 'operator', 'optparse', 'os', 'pathlib', 'pdb', 'pickle', 'pickletools', 'pkgutil',
    'platform', 'plistlib', 'poplib', 'posix', 'posixpath', 'pprint', 'profile', 'profiling',
    'pstats', 'pty', 'pwd', 'py_compile', 'pyclbr', 'pydoc', 'pydoc_data', 'pyexpat', 'queue',
    'quopri', 'random', 're', 'readline', 'reprlib', 'resource', 'rlcompleter', 'runpy', 'sched',
    'secrets', 'select', 'selectors', 'shelve', 'shlex', 'shutil', 'signal', 'site', 'smtplib',
    'socket', 'socketserver', 'sqlite3', 'ssl', 'stat', 'statistics', 'string', 'stringprep',
    'struct', 'subprocess', 'symtable', 'sys', 'sysconfig', 'syslog', 'tabnanny', 'tarfile',
    'tempfile', 'termios', 'textwrap', 'this', 'threading', 'time', 'timeit', 'tkinter', 'token',
    'tokenize', 'tomllib', 'trace', 'traceback', 'tracemalloc', 'tty', 'turtle', 'turtledemo',
    'types', 'typing', 'unicodedata', 'unittest', 'urllib', 'uuid', 'venv', 'warnings', 'wave',
    'weakref', 'webbrowser', 'winreg', 'winsound', 'wsgiref', 'xml', 'xmlrpc', 'zipapp', 'zipfile',
    'zipimport', 'zlib', 'zoneinfo',
))


_MISSING_STDLIB_MODULE_MESSAGES = {
    '_overlapped': "Unsupported platform for Windows-only standard library module '_overlapped'",
    '_scproxy': "Unsupported platform for standard library module '_scproxy'",
    '_sqlite3': "Standard library module disabled during build '_sqlite3' was not found",
    '_testconsole': "Unsupported platform for Windows-only standard library module '_testconsole'",
    '_winapi': "Unsupported platform for Windows-only standard library module '_winapi'",
    '_wmi': "Unsupported platform for Windows-only standard library module '_wmi'",
    'msvcrt': "Unsupported platform for Windows-only standard library module 'msvcrt'",
    'nt': "Unsupported platform for Windows-only standard library module 'nt'",
    'winreg': "Unsupported platform for Windows-only standard library module 'winreg'",
    'winsound': "Unsupported platform for Windows-only standard library module 'winsound'",
}


# ---------------------------------------------------------------- struct sequences

# Objects/structseq.c: a struct sequence is a tuple of its first VISIBLE
# fields (the last UNNAMED of them without a name) whose named fields, those
# beyond the tuple too, are read-only members.  The layouts are CPython's
# (builtin_types.metta).  An instance holds its tuple as an instance of a
# subclass of tuple does (its builtin part) and the fields beyond it in a
# tuple of their own
_structseq_layout = {}


def _structseq_make(cls, items):
    # PyStructSequence_New, filled: ITEMS has every field, in order
    visible = _structseq_layout[cls][0]
    ob = _cetta_new_object(cls)
    _cetta_set_field(ob, "value", tuple(items[:visible]))
    _cetta_set_field(ob, "hidden", tuple(items[visible:]))
    return ob


def _structseq_new(cls, *args, **kwargs):
    # structseq_new: a sequence of at least the tuple's length and at most
    # every field; each field beyond it read by name from DICT, else None
    arg, d = _clinic_args("structseq", ("sequence", "dict"), 1, 2, args, kwargs)
    visible, unnamed, names = _structseq_layout[cls]
    total = len(names) + unnamed
    if type(arg) is not list and type(arg) is not tuple:
        # PySequence_Fast: an iterable's items, else its own TypeError
        it = _NULL
        try:
            it = iter(arg)
        except TypeError:
            pass
        if it is _NULL:
            raise TypeError("constructor requires a sequence")
        arg = list(it)
    tp = _type_name(cls)
    if d is not _NULL and not isinstance(d, dict):
        raise TypeError(tp + "() takes a dict as second arg, if any")
    n = len(arg)
    if n < visible or n > total:
        if visible == total:
            raise TypeError(tp + "() takes a " + str(visible) + "-sequence (" + str(n) + "-sequence given)")
        if n < visible:
            raise TypeError(tp + "() takes an at least " + str(visible) + "-sequence (" + str(n) + "-sequence given)")
        raise TypeError(tp + "() takes an at most " + str(total) + "-sequence (" + str(n) + "-sequence given)")
    items = list(arg)
    if d is not _NULL and dict.__len__(d) > 0:
        found = 0
        for i in range(n, total):
            v = dict.get(d, names[i - unnamed], _NULL)
            if v is _NULL:
                v = None
            else:
                found += 1
            items.append(v)
        if dict.__len__(d) > found:
            raise TypeError(tp + "() got duplicate or unexpected field name(s)")
    else:
        items.extend([None] * (total - n))
    return _structseq_make(cls, items)


def _structseq_repr(self):
    # structseq_repr: the type's name, then each item of the tuple named by
    # the member in its place (tp_members read by index: an unnamed item
    # takes the name of a member after it, as CPython's repr gives it)
    cls = type(self)
    names = _structseq_layout[cls][2]
    items = _cetta_field(self, "value")
    return _type_name(cls) + "(" + ", ".join([names[i] + "=" + repr(items[i]) for i in range(len(items))]) + ")"


def _structseq_reduce(self):
    # structseq_reduce: the type, the tuple and a dict of the fields beyond it
    cls = type(self)
    visible, unnamed, names = _structseq_layout[cls]
    hidden = _cetta_field(self, "hidden")
    beyond = {}
    for j in range(len(hidden)):
        beyond[names[visible - unnamed + j]] = hidden[j]
    return (cls, (_cetta_field(self, "value"), beyond))


def _structseq_replace(self, *args, **kwargs):
    # structseq_replace: a copy with the named fields given replaced; a
    # struct sequence with unnamed items has none
    if args:
        raise TypeError("__replace__() takes no positional arguments")
    cls = type(self)
    visible, unnamed, names = _structseq_layout[cls]
    if unnamed:
        raise TypeError("__replace__() is not supported for " + _type_name(cls) + " because it has unnamed field(s)")
    items = list(_cetta_field(self, "value")) + list(_cetta_field(self, "hidden"))
    if kwargs:
        for i in range(len(names)):
            v = kwargs.pop(names[i], _NULL)
            if v is not _NULL:
                items[i] = v
        if kwargs:
            raise TypeError("Got unexpected field name(s): " + repr(list(kwargs)))
    return _structseq_make(cls, items)


def _statresult_new(cls, *args, **kwargs):
    # statresult_new (Modules/posixmodule.c): a float time not given is the
    # integer one
    result = _structseq_new(cls, *args, **kwargs)
    value = _cetta_field(result, "value")
    hidden = list(_cetta_field(result, "hidden"))
    for i in range(3):
        if hidden[i] is None:
            hidden[i] = value[7 + i]
    _cetta_set_field(result, "hidden", tuple(hidden))
    return result


_STRUCTSEQ_NEW = {"stat_result": _statresult_new}


def _structseq_install(cls, visible, unnamed, fields):
    # the type's namespace as PyStructSequence_InitType makes it: the
    # counts, __match_args__ (the tuple's named items), a read-only member
    # per named field, and the type's methods (its __new__ only when its
    # instances are made by calling it)
    names = tuple([f[0] for f in fields])
    _structseq_layout[cls] = (visible, unnamed, names)
    named_visible = visible - unnamed
    for k in range(len(names)):
        m = _cetta_new_object(_member_descriptor)
        _cetta_set_field(m, "owner", cls)
        _cetta_set_field(m, "member", names[k])
        _cetta_set_field(m, "index", k if k < named_visible else visible + k - named_visible)
        _cetta_set_field(m, "visible", visible)
        _cetta_set_field(m, "doc", fields[k][1])
        _cetta_set_type_attr(cls, names[k], m)
    _cetta_set_type_attr(cls, "n_sequence_fields", visible)
    _cetta_set_type_attr(cls, "n_fields", len(names) + unnamed)
    _cetta_set_type_attr(cls, "n_unnamed_fields", unnamed)
    _cetta_set_type_attr(cls, "__match_args__", names[:named_visible])
    if not _cetta_type_field(cls, "noinstances"):
        _cetta_set_type_attr(cls, "__new__", staticmethod(_STRUCTSEQ_NEW.get(_cetta_type_field(cls, "name"), _structseq_new)))
    _cetta_set_type_attr(cls, "__repr__", _structseq_repr)
    _cetta_set_type_attr(cls, "__reduce__", _structseq_reduce)
    _cetta_set_type_attr(cls, "__replace__", _structseq_replace)


for _layout in _cetta_structseq_layouts():
    _structseq_install(*_layout)


def _make_sys():
    sys = _new_module("sys")
    d = _cetta_object_dict(sys)
    _cetta_set_field(sys, "builtin", True)
    d["modules"] = _sys_modules
    d["path"] = []
    d["argv"] = []
    # the struct sequences of a run as `python3.15 PROGRAM` makes them, its
    # str hashes SipHash-1-3 with seed 0 (PYTHONHASHSEED=0), no user site
    # directory on its path (no_user_site), the program's directory first
    # (safe_path False)
    d["version_info"] = _structseq_make(_cetta_builtin_type("version_info"), (3, 15, 2, "final", 0))
    d["flags"] = _structseq_make(_cetta_builtin_type("flags"),
                                 (0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, False, 1, 0, False, 4300, 1, 0, 0, -1))
    d["float_info"] = _structseq_make(_cetta_builtin_type("float_info"),
                                      (1.7976931348623157e+308, 1024, 308, 2.2250738585072014e-308, -1021, -307,
                                       15, 53, 2.220446049250313e-16, 2, 1))
    d["int_info"] = _structseq_make(_cetta_builtin_type("int_info"), (30, 4, 4300, 640))
    d["hash_info"] = _structseq_make(_cetta_builtin_type("hash_info"),
                                     (64, 2305843009213693951, 314159, 0, 1000003, "siphash13", 64, 128, 0))
    d["hexversion"] = 0x030f02f0
    d["maxsize"] = 9223372036854775807
    d["maxunicode"] = 1114111
    d["byteorder"] = "little"
    d["platform"] = "linux"
    d["exit"] = _sys_exit
    d["exc_info"] = _sys_exc_info
    d["_getframe"] = _sys_getframe
    d["exception"] = _sys_exception
    d["getrecursionlimit"] = _sys_getrecursionlimit
    d["intern"] = _sys_intern
    # the modules CPython builds in that the runtime makes itself (sorted,
    # as CPython's tuple is)
    d["builtin_module_names"] = ("_ast", "_io", "_string", "_thread", "_types", "_typing", "_warnings", "_weakref", "builtins", "itertools", "sys")
    d["stdlib_module_names"] = _STDLIB_MODULE_NAMES
    _sys_modules["sys"] = sys
    return sys


def _sys_intern(*args, **kwargs):
    # sys_intern_impl: an exact str is its own interned self; a subclass of
    # str cannot be interned
    if kwargs:
        raise TypeError("intern() takes no keyword arguments")
    if len(args) != 1:
        raise TypeError("intern() takes exactly one argument (" + str(len(args)) + " given)")
    s = args[0]
    if type(s) is str:
        return s
    if isinstance(s, str):
        raise TypeError("can't intern " + _type_name(type(s)))
    raise TypeError("intern() argument must be str, not " + _type_name(type(s)))


def _sys_getrecursionlimit():
    return 1000


# ---------------------------------------------------------------- imports

# lazy imports' state (Python/import.c): sys.set_lazy_imports's mode and
# filter, and whether a resolution imported only a package, leaving its
# declared submodule for later; sys.lazy_modules, the names declared lazily
# and not loaded yet; each package's pending submodules (child name to its
# declaration, or None once the child is imported); the placeholders being
# resolved (an import cycle meets one again)
_lazy_state = {"mode": "normal", "filter": None, "has_lazy_submodules": False}
_lazy_modules = set()
_lazy_pending = {}
_lazy_resolving = set()
_lazy_import_type = _cetta_builtin_type("lazy_import")


def __import__(name, globals=None, locals=None, fromlist=(), level=0):
    # importlib._bootstrap.__import__
    if not isinstance(name, str):
        raise TypeError("module name must be str, not " + _type_name(type(name)))
    if level < 0:
        raise ValueError("level must be >= 0")
    if level == 0 and name == "":
        raise ValueError("Empty module name")
    if level == 0:
        module = _gcd_import(name)
    else:
        package = _calc_package(globals if globals is not None else {})
        module = _gcd_import(_resolve_name(name, package, level))
    if not fromlist:
        if level == 0:
            return _gcd_import(name.partition(".")[0]) if "." in name else module
        if not name:
            return module
        cut_off = len(name) - len(name.partition(".")[0])
        full = module.__name__
        return _sys_modules[full[:len(full) - cut_off]]
    if _has_path(module):
        _handle_fromlist(module, fromlist)
    return module


def _has_path(module):
    return "__path__" in _cetta_object_dict(module)


def _calc_package(globals):
    # the package a relative import is relative to: __package__, else
    # __name__ (itself a package when it has __path__, else its parent)
    package = globals["__package__"] if "__package__" in globals else None
    if package is not None:
        return package
    if "__name__" not in globals:
        raise KeyError("'__name__' not in globals")
    package = globals["__name__"]
    if "__path__" not in globals:
        package = package.rpartition(".")[0]
    return package


def _resolve_name(name, package, level):
    if not package:
        raise ImportError("attempted relative import with no known parent package")
    bits = package.rsplit(".", level - 1)
    if len(bits) < level:
        raise ImportError("attempted relative import beyond top-level package")
    base = bits[0]
    return base + "." + name if name else base


def _gcd_import(name):
    # a module already imported is the one sys.modules holds (its pending
    # lazy registration on its parent completed, PyImport_ImportModuleLevel-
    # Object); else it is found and loaded
    if name in _sys_modules:
        module = _sys_modules[name]
        if module is None:
            raise ModuleNotFoundError("import of " + name + " halted; None in sys.modules", name=name)
        if _lazy_state["has_lazy_submodules"] and not _module_initializing(module):
            _lazy_clear_submodule(name, True)
        return module
    return _find_and_load(name)


def _find_and_load(name, lazy_submodule=False):
    # _find_and_load_unlocked: the parent package first; then the finders in
    # sys.meta_path's order: a builtin module the runtime makes, a module it
    # holds compiled (found by its full name whatever the path, as CPython's
    # frozen modules are), then the parent's __path__ (or sys.path); the
    # module bound on its parent, and its name no longer lazily pending
    # (_imp._set_lazy_attributes).  Loading a lazily declared submodule finds
    # nothing rather than failing when there is no such module
    path = None
    parent = name.rpartition(".")[0]
    if parent:
        if parent not in _sys_modules:
            _gcd_import(parent)
        if name in _sys_modules:
            return _sys_modules[name]
        parent_module = _sys_modules[parent]
        pd = _cetta_object_dict(parent_module)
        if "__path__" not in pd:
            if lazy_submodule:
                return None
            raise ModuleNotFoundError("No module named " + repr(name) + "; " + repr(parent) + " is not a package", name=name)
        path = pd["__path__"]
    if name in _BUILTIN_MODULE_MAKERS:
        module = _BUILTIN_MODULE_MAKERS[name]()
        _sys_modules[name] = module
    else:
        held = _cetta_lib_module(name)
        if held is not None:
            module = _load_builtin(name, held[0], held[1])
        else:
            found = _find_spec(name, path)
            if found is None:
                if lazy_submodule:
                    return None
                raise ModuleNotFoundError("No module named " + repr(name), name=name)
            module = _load(name, found)
    if parent:
        setattr(_sys_modules[parent], name.rpartition(".")[2], module)
    _lazy_modules.discard(name)
    _lazy_clear_submodule(name, False)
    return module


def _find_spec(name, path):
    # the path finder: NAME/__init__.py (a package) or NAME.py in each entry
    tail = name.rpartition(".")[2]
    entries = path if path is not None else _sys_modules["sys"].path
    for entry in entries:
        base = (entry + "/" + tail) if entry else tail
        init = base + "/__init__.py"
        if _cetta_file_exists(init):
            return (init, base)
        if _cetta_file_exists(base + ".py"):
            return (base + ".py", None)
    return None


def _load(name, found):
    # _load_unlocked: the module in sys.modules before its code runs (a
    # circular import sees it), removed again when its code fails
    file, package_dir = found
    r = _cetta_compile_file(file)
    _show_compile_warnings(file, r[2])
    if r[0] != "ok":
        raise SyntaxError(r[1])
    code = r[1]
    module = _new_module(name)
    d = _cetta_object_dict(module)
    d["__file__"] = file
    d["__builtins__"] = _builtins_module
    if package_dir is not None:
        d["__path__"] = [package_dir]
        d["__package__"] = name
    else:
        d["__package__"] = name.rpartition(".")[0]
    return _run_module(name, module, code)


def _load_builtin(name, code, package):
    # a module written in Python that the runtime holds compiled (math): a
    # built-in module, without a file; a package's submodules are held too,
    # so its __path__ is empty
    module = _new_module(name)
    d = _cetta_object_dict(module)
    _cetta_set_field(module, "builtin", True)
    if package:
        d["__path__"] = []
        d["__package__"] = name
    else:
        d["__package__"] = name.rpartition(".")[0]
    return _run_module(name, module, code)


def _run_module(name, module, code):
    # the module in sys.modules before its code runs (a circular import sees
    # it), initializing while it runs (CPython's __spec__._initializing),
    # removed again when its code fails
    _sys_modules[name] = module
    _cetta_set_field(module, "initializing", True)
    try:
        _cetta_exec_module(code, _cetta_object_dict(module))
    except BaseException:
        _cetta_set_field(module, "initializing", False)
        if name in _sys_modules:
            del _sys_modules[name]
        raise
    _cetta_set_field(module, "initializing", False)
    return _sys_modules[name]


def _module_initializing(module):
    return type(module) is _module and _cetta_field(module, "initializing") is True


def _handle_fromlist(module, fromlist, recursive=False):
    # importlib._bootstrap._handle_fromlist: a package's submodules named in
    # the fromlist are imported; a submodule that does not exist is passed
    # over, and any other failure (a module the submodule imports, say) is
    # raised
    for x in fromlist:
        if not isinstance(x, str):
            where = module.__name__ + ".__all__" if recursive else "``from list''"
            raise TypeError("Item in " + where + " must be str, not " + type(x).__name__)
        elif x == "*":
            if not recursive and hasattr(module, "__all__"):
                _handle_fromlist(module, module.__all__, True)
        elif not hasattr(module, x):
            from_name = module.__name__ + "." + x
            try:
                _gcd_import(from_name)
            except ModuleNotFoundError as exc:
                # (sys.modules.get(from_name, _NEEDS_LOADING) is not None; no
                # class can be called this early in the prelude)
                if exc.name == from_name and (from_name not in _sys_modules or _sys_modules[from_name] is not None):
                    continue
                raise
    return module



def _import_from(module, name):
    # IMPORT_FROM: from a lazy import's placeholder, the placeholder of its
    # attribute (_PyEval_LazyImportFrom); else the attribute, else the
    # submodule in sys.modules
    if type(module) is _lazy_import_type:
        return _lazy_import_from(module, name)
    try:
        return getattr(module, name)
    except AttributeError:
        pass
    d = _cetta_object_dict(module) if type(module) is _module else None
    pkgname = d["__name__"] if d is not None and "__name__" in d else None
    if isinstance(pkgname, str):
        full = pkgname + "." + name
        if full in _sys_modules:
            return _sys_modules[full]
    # import_from's ImportError: the module's name and file, the name asked
    # for (name_from); a module still running is most likely a circular import
    path = d["__file__"] if d is not None and "__file__" in d else None
    shown = pkgname if isinstance(pkgname, str) else "<unknown module name>"
    if not isinstance(path, str):
        msg = "cannot import name " + repr(name) + " from " + repr(shown) + " (unknown location)"
        path = None
    elif _module_initializing(module):
        msg = ("cannot import name " + repr(name) + " from partially initialized module " + repr(shown)
               + " (most likely due to a circular import) (" + path + ")")
    else:
        msg = "cannot import name " + repr(name) + " from " + repr(shown) + " (" + path + ")"
    raise ImportError(msg, name=pkgname if isinstance(pkgname, str) else None, path=path, name_from=name)


def _import_star(module, namespace):
    # IMPORT_STAR: __all__, else the names not beginning with _
    d = _cetta_object_dict(module)
    if "__all__" in d:
        names = d["__all__"]
    else:
        names = [k for k in d if not k.startswith("_")]
    for k in names:
        namespace[k] = getattr(module, k)


# ---------------------------------------------------------------- lazy imports

# PEP 810 (Objects/lazyimportobject.c, Python/import.c): a lazy import binds a
# placeholder naming what it imports.  Read from a namespace (a global, a
# name, a module's attribute), a placeholder is resolved: what it names is
# imported and put in its place.  A root placeholder holds a module's
# absolute name and the import's fromlist; a projection, the placeholder it
# was taken from and an attribute's name.  Each keeps the frame that declared
# it, which the errors of its resolution name

def _lazy_new(frame, builtins, frm, attr):
    # _PyLazyImport_New
    if type(frm) is not _lazy_import_type and attr is not None and not isinstance(attr, (str, tuple)):
        raise TypeError("lazy_import: fromlist must be None, a string, or a tuple")
    lz = _cetta_new_lazy()
    _cetta_set_field(lz, "builtins", builtins)
    _cetta_set_field(lz, "frm", frm)
    _cetta_set_field(lz, "attr", attr)
    _cetta_set_field(lz, "frame", frame)
    _cetta_set_field(lz, "active", False)
    return lz


def _lazy_frame():
    # the innermost frame running Python code, else None
    try:
        return _cetta_frame(0)
    except ValueError:
        return None


def _lazy_has_fromlist(attr):
    return attr is not None and (not isinstance(attr, tuple) or len(attr) > 0)


def _lazy_path(lz):
    # lazy_import_path: the dotted name of what resolving it gives (a plain
    # import of a.b gives the package a)
    frm = _cetta_field(lz, "frm")
    if type(frm) is _lazy_import_type:
        return _lazy_path(frm) + "." + _cetta_field(lz, "attr")
    if _lazy_has_fromlist(_cetta_field(lz, "attr")):
        return frm
    return frm.partition(".")[0]


def _lazy_name(lz):
    # lazy_import_name: its name in reprs and messages
    frm = _cetta_field(lz, "frm")
    if type(frm) is _lazy_import_type:
        return _lazy_path(lz)
    if _lazy_has_fromlist(_cetta_field(lz, "attr")):
        return frm + "..."
    return frm


def _lazy_repr(self):
    return "<" + _fqn(type(self)) + " '" + _lazy_name(self) + "'>"


def _lazy_getattribute(self, name):
    # lazy_import_getattro: its type's attributes, else an error naming it
    try:
        return _object_getattribute(self, name)
    except AttributeError:
        pass
    raise AttributeError("cannot access attribute " + repr(name) + " on unresolved lazy import "
                         + repr(_lazy_name(self)))


def _lazy_resolve_method(self):
    # lazy_import.resolve(): what it names, imported, put nowhere
    return _lazy_resolve(self)


def _lazy_loaded_attr(name, attr_name):
    # lazy_import_get_loaded_attr: an attribute of the module NAME when it is
    # imported and initialized (never one that is itself a placeholder), else
    # _NULL
    mod = _sys_modules.get(name, None)
    if mod is None or not isinstance(mod, _module) or _module_initializing(mod):
        return _NULL
    v = _cetta_object_dict(mod).get(attr_name, _NULL)
    return _NULL if type(v) is _lazy_import_type else v


def _import_name(builtins, globals, locals, name, fromlist, level):
    # _PyEval_ImportName: the builtins' __import__
    f = builtins.get("__import__", _NULL)
    if f is _NULL:
        raise ImportError("__import__ not found")
    return f(name, globals, locals, fromlist, level)


def _abs_import_name(name, globals, level):
    # _PyImport_GetAbsName
    if level > 0:
        return _resolve_name(name, _calc_package(globals), level)
    if name == "":
        raise ValueError("Empty module name")
    return name


def _lazy_import_name(name, globals, locals, fromlist, level, lazy):
    # _PyEval_LazyImportName: IMPORT_NAME of an import that is lazy, or that
    # the policy may make lazy (sys.set_lazy_imports("all") at module level,
    # the module's __lazy_modules__), by the builtins' __lazy_import__; else
    # the eager import
    module_level = globals is locals
    if not lazy and _lazy_state["mode"] == "all":
        lazy = module_level
    if not lazy and module_level:
        lazy_modules = globals.get("__lazy_modules__", _NULL)
        if lazy_modules is not _NULL:
            lazy = _abs_import_name(name, globals, level) in lazy_modules
    builtins = _cetta_object_dict(_builtins_module)
    if not lazy:
        return _import_name(builtins, globals, locals, name, fromlist, level)
    f = builtins.get("__lazy_import__", _NULL)
    if f is _NULL:
        raise ImportError("__lazy_import__ not found")
    if f is __lazy_import__:
        return _lazy_import_module_level(name, builtins, globals, locals, fromlist, level)
    return f(name, globals, locals, fromlist, level)


def _lazy_import_module_level(name, builtins, globals, locals, fromlist, level):
    # _PyImport_LazyImportModuleLevelObject: the placeholder of a lazy import
    # made at module level (the filter may make it eager after all); its name
    # pending in sys.lazy_modules, and each dotted name's parents told of
    # their pending submodule
    if not isinstance(name, str):
        raise TypeError("module name must be a string, got " + _fqn(type(name)))
    if level < 0:
        raise ValueError("level must be >= 0")
    abs_name = _abs_import_name(name, globals, level)
    frame = _lazy_frame()
    if frame is None or frame.f_globals is not frame.f_locals:
        raise SyntaxError("'lazy import' is only allowed at module level")
    if isinstance(fromlist, str):
        fromlist = (fromlist,)
    filt = _lazy_state["filter"]
    if filt is not None:
        if not filt(globals.get("__name__", None), abs_name, fromlist):
            return __import__(name, globals, locals, fromlist, level)
    if isinstance(fromlist, tuple):
        for item in fromlist:
            if not isinstance(item, str):
                raise TypeError("Item in ``from list'' must be str, not " + _type_name(type(item)))
    res = _lazy_new(frame, builtins, abs_name, fromlist)
    existing = _lazy_modules_add(abs_name)
    if isinstance(fromlist, tuple) and len(fromlist) > 0:
        for item in fromlist:
            _lazy_register_from(existing, abs_name, item)
    else:
        _lazy_register_on_parent(abs_name, res)
    return res


def _lazy_modules_add(name):
    # lazy_modules_add: NAME pending in sys.lazy_modules unless it is imported
    # and initialized; the module sys.modules holds, if any
    mod = _sys_modules.get(name, None)
    if mod is None or _module_initializing(mod):
        _lazy_modules.add(name)
    return mod


def _lazy_register_from(existing, abs_name, attr):
    # register_from_lazy_on_parent: `from M import NAME` records M.NAME as a
    # possible submodule of M, unless M's namespace holds NAME already
    if existing is not None and isinstance(existing, _module) and attr in _cetta_object_dict(existing):
        return
    fromname = abs_name + "." + attr
    _lazy_modules_add(fromname)
    _lazy_register_on_parent(fromname, None)


def _lazy_register_on_parent(name, source):
    # register_lazy_on_parent: each dotted prefix of NAME records the next
    # part as its pending submodule: the declaration, or None when that
    # module is imported (a from-import's names never displace a declaration)
    end = len(name)
    while True:
        dot = name.rfind(".", 0, end)
        if dot < 0:
            return
        parent = name[:dot]
        child = name[dot + 1:end]
        submodules = _lazy_pending.get(parent, _NULL)
        if submodules is _NULL:
            submodules = {}
            _lazy_pending[parent] = submodules
        value = None if _sys_modules.get(name[:end], None) is not None else source
        if source is None:
            if child not in submodules:
                submodules[child] = value
        else:
            submodules[child] = value
        end = dot


def _lazy_import_from(lz, name):
    # _PyEval_LazyImportFrom: `from m import name` takes NAME from m at once
    # when m is imported and initialized; else a projection of the placeholder
    attr = _cetta_field(lz, "attr")
    if isinstance(attr, tuple) and len(attr) > 0:
        v = _lazy_loaded_attr(_cetta_field(lz, "frm"), name)
        if v is not _NULL:
            return v
    return _lazy_new(_lazy_frame(), None, lz, name)


def _lazy_resolve(lz):
    return _lazy_resolve_impl(lz)[0]


def _lazy_resolve_impl(lz):
    # lazy_import_resolve_impl: the root placeholder's import (a plain import
    # of a dotted name whose parents are packages imports only the top one,
    # its submodules pending until reached), then the attributes recorded from
    # the root to LZ taken in turn; meeting LZ again during its own resolution
    # is an import cycle; resolved, its name leaves sys.lazy_modules.  With the
    # value, the module the default import gave
    root = lz
    first = None
    while type(_cetta_field(root, "frm")) is _lazy_import_type:
        first = root
        root = _cetta_field(root, "frm")
    root_name = _cetta_field(root, "frm")
    if lz in _lazy_resolving:
        raise ImportCycleError("cannot import name " + repr(_lazy_name(lz))
                               + " (most likely due to a circular import)", name=root_name)
    _lazy_resolving.add(lz)
    imported_module = None
    try:
        root_attr = _cetta_field(root, "attr")
        if first is not None and isinstance(root_attr, tuple) and len(root_attr) > 0:
            fromlist = (_cetta_field(first, "attr"),)
        else:
            fromlist = root_attr
        frame = _lazy_frame()
        globals = frame.f_globals if frame is not None else None
        builtins = _cetta_field(root, "builtins")
        import_func = builtins.get("__import__", _NULL)
        if import_func is _NULL:
            raise ImportError("__import__ not found")
        default_import = import_func is __import__
        name = root_name
        top_only = False
        if (first is None and (fromlist is None or (isinstance(fromlist, tuple) and len(fromlist) == 0))
                and default_import and builtins is _cetta_object_dict(_builtins_module)):
            dot = name.find(".")
            if dot >= 0:
                regular = True
                complete = True
                i = dot
                while i >= 0 and regular:
                    prefix = name[:i]
                    cached = _lazy_loaded_attr(prefix, "__name__")
                    if cached is _NULL:
                        complete = False
                        break
                    regular = (_lazy_loaded_attr(prefix, "__path__") is not _NULL and isinstance(cached, str)
                               and cached == prefix)
                    i = name.find(".", i + 1)
                if regular and (not complete or _lazy_loaded_attr(name, "__name__") is _NULL):
                    name = name[:dot]
                    top_only = True
        obj = import_func(name, globals, globals, fromlist, 0)
        if top_only:
            _cetta_set_field(root, "active", True)
            _lazy_state["has_lazy_submodules"] = True
            if not (type(obj) is _module and "__path__" in _cetta_object_dict(obj)):
                obj = import_func(root_name, globals, globals, fromlist, 0)
        elif default_import:
            _lazy_clear_submodule(name, False)
        obj = _lazy_resolve_result(obj)
        if isinstance(obj, _module) and default_import:
            imported_module = obj
        if first is not None:
            obj = _lazy_replay_from(obj, lz)
    except BaseException as e:
        _lazy_resolving.discard(lz)
        _lazy_add_cause(e, lz)
        raise
    _lazy_resolving.discard(lz)
    _lazy_modules.discard(_lazy_path(lz))
    return obj, imported_module


def _lazy_resolve_result(obj):
    # a placeholder an import hook or an attribute lookup gave, resolved too
    if type(obj) is _lazy_import_type:
        return _lazy_resolve(obj)
    return obj


def _lazy_replay_from(mod, lz):
    # lazy_import_replay_from: the attributes recorded from the root to LZ
    # taken in turn from what the root's import gave
    frm = _cetta_field(lz, "frm")
    if type(frm) is not _lazy_import_type:
        return mod
    base = _lazy_replay_from(mod, frm)
    return _lazy_resolve_result(_import_from(base, _cetta_field(lz, "attr")))


def _lazy_add_cause(e, lz):
    # lazy_import_add_exception_cause: a resolution's error names where the
    # lazy import was declared: a note when the error has a cause or a
    # context already, else a cause, an ImportError whose traceback is the
    # declaration
    frame = _cetta_field(lz, "frame")
    if frame is None:
        return
    name = _lazy_name(lz)
    code = frame.f_code
    lineno = frame.f_lineno
    if e.__cause__ is not None or e.__context__ is not None or e.__suppress_context__:
        note = ("lazy import of '" + name + "' declared in " + code.co_name + " at " + code.co_filename
                + ":" + str(lineno))
        notes = getattr(e, "__notes__", None)
        if notes is None or note not in notes:
            e.add_note(note)
        return
    cause = ImportError("lazy import of '" + name + "' raised an exception during resolution")
    tb = _cetta_new_object(_traceback_type)
    _cetta_set_field(tb, "frame", frame)
    _cetta_set_field(tb, "lineno", lineno)
    _cetta_set_field(tb, "next", None)
    cause.__traceback__ = tb
    e.__cause__ = cause


def _lazy_reify(lz, name, namespace):
    # _PyLazyImport_Reify: the placeholder resolved and, where NAMESPACE still
    # holds it, put in its place (a namespace that cannot be written to
    # keeps it)
    value = _lazy_resolve_impl(lz)[0]
    if type(namespace) is dict:
        if namespace.get(name, _NULL) is lz:
            namespace[name] = value
    elif _mro_lookup(type(namespace), "__setitem__") is not _NULL:
        if _mapping_get_optional(namespace, name) is lz:
            namespace[name] = value
    return value


def _lazy_try_load_submodule(module, attr_name):
    # _PyImport_TryLoadLazySubmodule: a package's lazily declared submodule
    # ATTR_NAME, imported now and bound on it; with it, whether the package
    # has pending submodules at all (the attribute is then looked for again)
    d = _cetta_object_dict(module)
    mod_name = d.get("__name__", _NULL)
    if not isinstance(mod_name, str):
        return _NULL, False
    pending = _lazy_pending.get(mod_name, _NULL)
    if pending is _NULL:
        return _NULL, False
    source = pending.get(attr_name, _NULL)
    if source is _NULL:
        return _NULL, True
    full_name = mod_name + "." + attr_name
    try:
        if source is not None and (_cetta_field(source, "active") is True or "__getattr__" not in d):
            mod = _lazy_load_child(source, full_name)
        else:
            mod = _sys_modules.get(full_name, None)
            if mod is None:
                mod = _find_and_load(full_name, True)
    except BaseException:
        loaded = _sys_modules.get(full_name, None)
        if loaded is not None:
            if d.get(attr_name, _NULL) is loaded:
                del d[attr_name]
            if attr_name not in pending:
                pending[attr_name] = source
        raise
    if mod is None:
        return _NULL, True
    if not _module_initializing(mod):
        d[attr_name] = mod
        if attr_name in pending:
            del pending[attr_name]
    return mod, True


def _lazy_load_child(decl, name):
    # _PyLazyImport_LoadChild: the dotted NAME resolved as an aliased import
    # (the whole name imported, then its parts taken in turn), as DECL
    # declared it; reaching a module that is not a package resolves DECL's
    # own name instead, which refuses an invalid dotted import
    lz = _lazy_new(None, _cetta_field(decl, "builtins"), name, None)
    for part in name.split(".")[1:]:
        lz = _lazy_new(None, None, lz, part)
    _cetta_set_field(lz, "frame", _cetta_field(decl, "frame"))
    result = _lazy_resolve(lz)
    decl_name = _cetta_field(decl, "frm")
    if isinstance(result, _module) and decl_name != name and "__path__" not in _cetta_object_dict(result):
        return _lazy_load_child(decl, decl_name)
    return result


def _lazy_clear_submodule(name, bind):
    # _PyImport_ClearLazySubmodule: an imported module leaves its parent's
    # pending submodules (bound on the parent first when BIND), or stays
    # there without its declaration while the parent is still initializing
    if not _lazy_pending:
        return
    parent, dot, child = name.rpartition(".")
    if not dot:
        return
    pending = _lazy_pending.get(parent, _NULL)
    if pending is _NULL or child not in pending:
        return
    if _module_initializing(_sys_modules.get(name, None)):
        return
    module = _sys_modules.get(parent, None)
    initializing = _module_initializing(module)
    if bind and not initializing and type(module) is _module and child in pending:
        value = _sys_modules.get(name, None)
        if value is not None:
            md = _cetta_object_dict(module)
            if child not in md:
                md[child] = value
    if initializing:
        pending[child] = None
    else:
        del pending[child]


def __lazy_import__(*args, **kwargs):
    # builtin___lazy_import__(name, globals=None, locals=None, fromlist=(),
    # level=0): the placeholder a lazy import statement would make; omitted
    # globals are the caller's, omitted locals the globals, and the builtins
    # those of the globals' __builtins__
    name, globals, locals, fromlist, level = _clinic_args(
        "__lazy_import__", ("name", "globals", "locals", "fromlist", "level"), 1, 5, args, kwargs)
    if globals is _NULL:
        frame = _lazy_frame()
        if frame is None:
            raise TypeError("__lazy_import__() missing globals when called without a frame")
        globals = frame.f_globals
    if locals is _NULL:
        locals = globals
    if fromlist is _NULL:
        fromlist = None
    level = 0 if level is _NULL else _index(level)
    if type(globals) is not dict:
        raise TypeError("expect dict for globals, got " + _fqn(type(globals)))
    builtins = globals.get("__builtins__", _NULL)
    if builtins is _NULL:
        raise ValueError("unable to get builtins for lazy import")
    if isinstance(builtins, _module):
        builtins = _cetta_object_dict(builtins)
    return _lazy_import_module_level(name, builtins, globals, locals, fromlist, level)


def _sys_set_lazy_imports(*args, **kwargs):
    mode = _clinic_args("set_lazy_imports", ("mode",), 1, 1, args, kwargs)[0]
    if not isinstance(mode, str):
        raise TypeError("mode must be a string: 'normal' or 'all'")
    if mode != "normal" and mode != "all":
        raise ValueError("mode must be 'normal' or 'all'")
    _lazy_state["mode"] = mode


def _sys_get_lazy_imports():
    return _lazy_state["mode"]


def _sys_set_lazy_imports_filter(*args, **kwargs):
    filt = _clinic_args("set_lazy_imports_filter", ("filter",), 1, 1, args, kwargs)[0]
    if filt is not None and not callable(filt):
        raise ValueError("filter provided but is not callable")
    _lazy_state["filter"] = filt


def _sys_get_lazy_imports_filter():
    return _lazy_state["filter"]


for _name, _f in (("__repr__", _lazy_repr), ("__getattribute__", _lazy_getattribute),
                  ("resolve", _lazy_resolve_method)):
    _cetta_set_type_attr(_lazy_import_type, _name, _f)


# ---------------------------------------------------------------- generators

def _generator_maker():
    yield None


_generator = type(_generator_maker())


def _generator_iter(self):
    return self


def _generator_next(self):
    return _cetta_gen_send(self, None)


def _generator_send(self, value):
    return _cetta_gen_send(self, value)


def _generator_throw(self, typ, val=_NULL, tb=_NULL):
    return _cetta_gen_throw(self, _thrown(typ, val, tb))


def _thrown(typ, val=_NULL, tb=_NULL):
    # _gen_throw: an exception class is made an instance (of the value, of
    # its items when a tuple); an instance takes no separate value
    if val is _NULL:
        val = None
    if tb is not _NULL and tb is not None:
        raise TypeError("throw() third argument must be a traceback object")
    if isinstance(typ, type) and issubclass(typ, BaseException):
        if val is None:
            exc = typ()
        elif isinstance(val, typ):
            exc = val
        elif isinstance(val, tuple):
            exc = typ(*val)
        else:
            exc = typ(val)
    elif isinstance(typ, BaseException):
        if val is not None:
            raise TypeError("instance exception may not have a separate value")
        exc = typ
    else:
        raise TypeError("exceptions must be classes or instances deriving from BaseException, not "
                        + _type_name(type(typ)))
    return exc


def _generator_close(self):
    return _cetta_gen_close(self)


_cetta_set_type_attr(object, "__class__", _cetta_getset("__class__"))
_cetta_set_type_attr(type, "__new__", staticmethod(_type_new))
_cetta_set_type_attr(type, "__init__", _type_init)
_cetta_set_type_attr(type, "__subclasses__", _type_subclasses)
_cetta_set_type_attr(type, "mro", _type_mro)
_cetta_set_type_attr(type, "__call__", _type_call)
_cetta_set_type_attr(type, "__prepare__", classmethod(_type_prepare))
_cetta_set_type_attr(type, "__getattribute__", _type_getattribute)
_cetta_set_type_attr(type, "__setattr__", _type_setattr)
_cetta_set_type_attr(type, "__delattr__", _type_delattr)
_cetta_set_type_attr(type, "__repr__", _type_repr)
for _name in ("__name__", "__qualname__", "__module__", "__mro__", "__bases__", "__base__", "__dict__", "__doc__", "__type_params__",
              "__annotations__", "__annotate__"):
    _cetta_set_type_attr(type, _name, _cetta_getset(_name))
_cetta_set_type_attr(_function, "__get__", _function_get)
for _name in ("__name__", "__qualname__", "__module__", "__doc__", "__defaults__", "__kwdefaults__",
              "__globals__", "__closure__", "__dict__", "__annotations__", "__annotate__", "__type_params__",
              "__code__"):
    _cetta_set_type_attr(_function, _name, _cetta_getset(_name))
_cetta_set_type_attr(staticmethod, "__get__", _staticmethod_get)


def _sm_init(name):
    # sm_init / cm_init (staticmethod(f) itself is made by the runtime; this
    # is __init__ called again or by a subclass): the callable kept, its
    # names copied (functools_wraps)
    def init(self, *args, **kwargs):
        if kwargs:
            raise TypeError(name + "() takes no keyword arguments")
        if len(args) != 1:
            raise TypeError(name + " expected 1 argument, got " + str(len(args)))
        _cetta_set_field(self, "func", args[0])
        d = _cetta_object_dict(self)
        for attr in ("__module__", "__name__", "__qualname__", "__doc__"):
            v = getattr(args[0], attr, _NULL)
            if v is not _NULL:
                d[attr] = v
    return init


def _sm_isabstract(self):
    # sm_get___isabstractmethod__: _PyObject_IsAbstract of the callable
    return bool(getattr(_cetta_field(self, "func"), "__isabstractmethod__", False))


def _sm_annotations(self):
    return _cetta_field(self, "func").__annotations__


def _sm_annotate(self):
    return _cetta_field(self, "func").__annotate__


def _staticmethod_call(self, *args, **kwargs):
    return _cetta_field(self, "func")(*args, **kwargs)


for _t, _tn in ((staticmethod, "staticmethod"), (classmethod, "classmethod")):
    _cetta_set_type_attr(_t, "__init__", _sm_init(_tn))
    _cetta_set_type_attr(_t, "__repr__", (lambda tn: lambda self: "<" + tn + "(" + repr(_cetta_field(self, "func")) + ")>")(_tn))
    _cetta_set_type_attr(_t, "__class_getitem__", classmethod(_generic_alias))
    _member(_t, "__func__", "func")
    _member(_t, "__wrapped__", "func")
    _computed(_t, "__isabstractmethod__", _sm_isabstract)
    _computed(_t, "__annotations__", _sm_annotations)
    _computed(_t, "__annotate__", _sm_annotate)
    _cetta_set_type_attr(_t, "__dict__", _cetta_getset("__dict__"))
_cetta_set_type_attr(staticmethod, "__call__", _staticmethod_call)
_cetta_set_type_attr(int, "bit_length", _int_bit_length)
_cetta_set_type_attr(int, "bit_count", _int_bit_count)
_cetta_set_type_attr(str, "partition", _str_partition)
_cetta_set_type_attr(str, "splitlines", _str_splitlines)
_cetta_set_type_attr(str, "rpartition", _str_rpartition)
_cetta_set_type_attr(str, "rsplit", _str_rsplit)
for _name, _f in (("isalpha", _str_isalpha), ("isdecimal", _str_isdecimal), ("isdigit", _str_isdigit),
                  ("isnumeric", _str_isnumeric), ("isalnum", _str_isalnum), ("isspace", _str_isspace),
                  ("isprintable", _str_isprintable), ("isascii", _str_isascii), ("islower", _str_islower),
                  ("isupper", _str_isupper), ("istitle", _str_istitle), ("isidentifier", _str_isidentifier),
                  ("upper", _str_upper), ("lower", _str_lower), ("casefold", _str_casefold),
                  ("swapcase", _str_swapcase), ("title", _str_title), ("capitalize", _str_capitalize),
                  ("center", _str_center), ("ljust", _str_ljust), ("rjust", _str_rjust), ("zfill", _str_zfill),
                  ("strip", _str_strip), ("lstrip", _str_lstrip), ("rstrip", _str_rstrip), ("split", _str_split),
                  ("find", _str_find), ("rfind", _str_rfind), ("index", _str_index), ("rindex", _str_rindex),
                  ("count", _str_count), ("replace", _str_replace), ("startswith", _str_startswith),
                  ("endswith", _str_endswith), ("removeprefix", _str_removeprefix),
                  ("removesuffix", _str_removesuffix), ("expandtabs", _str_expandtabs),
                  ("translate", _str_translate)):
    _cetta_set_type_attr(str, _name, _f)
_cetta_set_type_attr(str, "maketrans", staticmethod(_str_maketrans))
_cetta_set_type_attr(str, "encode", _str_encode)
_cetta_set_type_attr(bytes, "__new__", staticmethod(_bytes_new))
_cetta_set_type_attr(bytes, "fromhex", classmethod(_bytes_fromhex))
_cetta_set_type_attr(bytes, "__getnewargs__", _bytes_getnewargs)
_cetta_set_type_attr(bytearray, "fromhex", classmethod(_bytearray_fromhex))
# the methods bytes and bytearray share (bytes_methods.c), each one function
_shared = [("decode", _bytes_decode), ("hex", _bytes_hex), ("maketrans", staticmethod(_bytes_maketrans)),
           ("__mod__", _bytes_mod)]
for _name, _kinds in (("replace", "bbo"), ("partition", "b"), ("rpartition", "b"), ("center", "of"),
                     ("ljust", "of"), ("rjust", "of"), ("zfill", "o"), ("expandtabs", "o")):
    _shared.append((_name, _bytes_method(_name, _kinds)))
for _name in ("find", "rfind", "index", "rindex", "count"):
    _shared.append((_name, _bytes_search(_name)))
for _name in ("startswith", "endswith"):
    _shared.append((_name, _bytes_tailmatch(_name)))
for _name in ("removeprefix", "removesuffix"):
    _shared.append((_name, _bytes_removefix(_name)))
for _name in ("upper", "lower", "swapcase", "capitalize", "title"):
    _shared.append((_name, _bytes_case(_name)))
for _name in ("isalpha", "isalnum", "isdigit", "isspace", "islower", "isupper", "istitle", "isascii"):
    _shared.append((_name, _bytes_test(_name)))
_shared.extend((("strip", _bytes_strip), ("lstrip", _bytes_lstrip), ("rstrip", _bytes_rstrip),
                ("split", _bytes_split), ("rsplit", _bytes_rsplit), ("splitlines", _bytes_splitlines),
                ("join", _bytes_join), ("translate", _bytes_translate)))
for _name, _f in _shared:
    _cetta_set_type_attr(bytes, _name, _f)
    _cetta_set_type_attr(bytearray, _name, _f)
# bytearray's own (bytearrayobject.c)
_cetta_set_type_attr(bytearray, "__new__", staticmethod(_bytearray_new))
for _name, _f in (("__init__", _bytearray_init), ("__repr__", _bytearray_repr), ("__str__", _bytearray_repr),
                  ("__len__", _bytearray_len), ("__getitem__", _bytearray_getitem),
                  ("__setitem__", _bytearray_setitem), ("__delitem__", _bytearray_delitem),
                  ("__iter__", _bytearray_iter), ("__contains__", _bytearray_contains),
                  ("__add__", _bytearray_add), ("__iadd__", _bytearray_iadd), ("__mul__", _bytearray_mul),
                  ("__rmul__", _bytearray_mul), ("__imul__", _bytearray_imul), ("__rmod__", _bytearray_rmod),
                  ("append", _bytearray_append), ("extend", _bytearray_extend), ("pop", _bytearray_pop),
                  ("remove", _bytearray_remove), ("insert", _bytearray_insert), ("clear", _bytearray_clear),
                  ("copy", _bytearray_copy), ("reverse", _bytearray_reverse), ("__alloc__", _bytearray_alloc),
                  ("__sizeof__", _bytearray_sizeof), ("__reduce__", _bytearray_reduce),
                  ("__reduce_ex__", _bytearray_reduce_ex)):
    _cetta_set_type_attr(bytearray, _name, _f)
for _name in ("__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__"):
    _cetta_set_type_attr(bytearray, _name, _bytearray_compare(_name))
_cetta_set_type_attr(UnicodeEncodeError, "__str__", _unicode_encode_error_str)
_cetta_set_type_attr(UnicodeDecodeError, "__str__", _unicode_decode_error_str)
_cetta_set_type_attr(_module, "__repr__", _module_repr)
_cetta_set_type_attr(_module, "__getattribute__", _module_getattribute)
_cetta_set_type_attr(object, "__dir__", _object_dir)
_cetta_set_type_attr(type, "__dir__", _type_dir)
_cetta_set_type_attr(_module, "__dir__", _module_dir)
_cetta_set_type_attr(_code_type, "__repr__", _code_repr)
_computed(_code_type, "co_name", _code_name)
_computed(_code_type, "co_filename", _code_filename)
_computed(_code_type, "co_firstlineno", _code_firstlineno)
for _name, _f in (("co_qualname", _code_qualname), ("co_argcount", _code_argcount),
                  ("co_posonlyargcount", _code_posonlyargcount), ("co_kwonlyargcount", _code_kwonlyargcount),
                  ("co_varnames", _code_varnames), ("co_nlocals", _code_nlocals),
                  ("co_cellvars", _code_cellvars), ("co_freevars", _code_freevars)):
    _computed(_code_type, _name, _f)
_frame_type = _cetta_builtin_type("frame")
_traceback_type = _cetta_builtin_type("traceback")
for _name in ("tb_frame", "tb_lineno", "tb_next", "tb_lasti"):
    _cetta_set_type_attr(_traceback_type, _name, _cetta_getset(_name))
for _name in ("f_code", "f_globals", "f_builtins", "f_locals", "f_lineno", "f_back", "f_lasti", "f_trace", "f_generator"):
    _cetta_set_type_attr(_frame_type, _name, _cetta_getset(_name))
_cetta_set_type_attr(_frame_type, "__repr__", _frame_repr)
_builtins_module = _new_module("builtins")
_cetta_set_field(_builtins_module, "builtin", True)
_cetta_object_dict(_builtins_module)["__getattr__"] = _builtins_getattr
_sys_modules["builtins"] = _builtins_module
_make_sys()
# sys's lazy import controls (PEP 810): sys.lazy_modules is the set itself
_sd = _cetta_object_dict(_sys_modules["sys"])
_sd["lazy_modules"] = _lazy_modules
for _name, _f in (("set_lazy_imports", _sys_set_lazy_imports), ("get_lazy_imports", _sys_get_lazy_imports),
                  ("set_lazy_imports_filter", _sys_set_lazy_imports_filter),
                  ("get_lazy_imports_filter", _sys_get_lazy_imports_filter)):
    _sd[_name] = _f
_cetta_set_type_attr(object, "__format__", _object_format)
_cetta_set_type_attr(str, "format", _str_format_method)
for _name in ("real", "imag", "numerator", "denominator"):
    _cetta_set_type_attr(int, _name, _cetta_getset(_name))
for _name in ("real", "imag"):
    _cetta_set_type_attr(float, _name, _cetta_getset(_name))
_cetta_set_type_attr(str, "format_map", _str_format_map)
_cetta_set_type_attr(str, "__mod__", _percent_format)
_cetta_set_type_attr(str, "__format__", _str_format)
_cetta_set_type_attr(int, "__format__", _int_format)
_cetta_set_type_attr(float, "__format__", _float_format)
_cetta_set_type_attr(_generator, "__iter__", _generator_iter)
_cetta_set_type_attr(_generator, "__next__", _generator_next)
_cetta_set_type_attr(_generator, "send", _generator_send)
_cetta_set_type_attr(_generator, "throw", _generator_throw)
_cetta_set_type_attr(_generator, "close", _generator_close)
for _name in ("__name__", "__qualname__", "gi_running", "gi_suspended", "gi_yieldfrom"):
    _cetta_set_type_attr(_generator, _name, _cetta_getset(_name))
_cetta_set_type_attr(classmethod, "__get__", _classmethod_get)
_cetta_set_type_attr(type(_cetta_getset("__dict__")), "__get__", _getset_get)
_cetta_set_type_attr(type(_cetta_getset("__dict__")), "__set__", _getset_set)
_cetta_set_type_attr(super, "__new__", staticmethod(_super_new))
_cetta_set_type_attr(super, "__init__", _super_init)
_cetta_set_type_attr(super, "__getattribute__", _super_getattribute)
_cetta_set_type_attr(property, "__new__", staticmethod(_property_new))
_cetta_set_type_attr(property, "__init__", _property_init)
_cetta_set_type_attr(property, "__get__", _property_get)
# (property objects can be made from here on)
for _t in (UnicodeEncodeError, UnicodeDecodeError):
    for _i, _name in ((0, "encoding"), (1, "object"), (2, "start"), (3, "end"), (4, "reason")):
        _cetta_set_type_attr(_t, _name, property((lambda i: lambda self: self.args[i])(_i)))
_cetta_set_type_attr(property, "__set__", _property_set)
_cetta_set_type_attr(property, "__delete__", _property_delete)
_cetta_set_type_attr(property, "__set_name__", _property_set_name)
_cetta_set_type_attr(property, "getter", _property_getter)
_cetta_set_type_attr(property, "setter", _property_setter)
_cetta_set_type_attr(property, "deleter", _property_deleter)
for _name in ("fget", "fset", "fdel", "__doc__"):
    _cetta_set_type_attr(property, _name, _cetta_getset(_name))
_cetta_set_type_attr(BaseException, "__new__", staticmethod(_exception_new))
_cetta_set_type_attr(BaseException, "__init__", _exception_init)
_cetta_set_type_attr(BaseException, "__str__", _exception_str)
_cetta_set_type_attr(BaseException, "__repr__", _exception_repr)
_cetta_set_type_attr(BaseException, "add_note", _exception_add_note)


def _exception_with_traceback(self, tb):
    # BaseException.with_traceback: the traceback set, the exception returned
    self.__traceback__ = tb
    return self


_cetta_set_type_attr(BaseException, "with_traceback", _exception_with_traceback)
for _name in ("args", "__context__", "__cause__", "__suppress_context__", "__traceback__"):
    _cetta_set_type_attr(BaseException, _name, _cetta_getset(_name))
_cetta_set_type_attr(StopIteration, "value", _cetta_getset("value"))
_cetta_set_type_attr(SystemExit, "code", _cetta_getset("code"))
_cetta_set_type_attr(NameError, "name", _cetta_getset("name"))
_cetta_set_type_attr(AttributeError, "name", _cetta_getset("name"))
_cetta_set_type_attr(AttributeError, "obj", _cetta_getset("obj"))


def _import_error_str(self):
    # ImportError_str: msg when it is a str, else BaseException's
    m = self.msg
    if type(m) is str:
        return m
    return BaseException.__str__(self)


def _import_error_repr(self):
    # ImportError_repr: BaseException's, then name= and path= when given
    has_name, has_path = _cetta_has_field(self, "name"), _cetta_has_field(self, "path")
    base = BaseException.__repr__(self)
    if not has_name and not has_path:
        return base
    text = base[:-1]
    hasargs = len(self.args) != 0
    if has_name:
        text += (", " if hasargs else "") + "name=" + repr(self.name)
        hasargs = True
    if has_path:
        text += (", " if hasargs else "") + "path=" + repr(self.path)
    return text + ")"


def _import_error_reduce(self):
    # ImportError_reduce: (type, args), or with the state of its __dict__,
    # name, path and name_from
    d = getattr(self, "__dict__", None)
    given = [k for k in ("name", "path", "name_from") if _cetta_has_field(self, k)]
    if given:
        state = dict(d) if d else {}
        for k in given:
            state[k] = getattr(self, k)
        return (type(self), self.args, state)
    if d:
        return (type(self), self.args, d)
    return (type(self), self.args)


for _name in ("msg", "name", "path", "name_from"):
    _cetta_set_type_attr(ImportError, _name, _cetta_getset(_name))
_cetta_set_type_attr(ImportError, "__str__", _import_error_str)
_cetta_set_type_attr(ImportError, "__repr__", _import_error_repr)
_cetta_set_type_attr(ImportError, "__reduce__", _import_error_reduce)
_cetta_set_type_attr(OSError, "__new__", staticmethod(_oserror_new))
_cetta_set_type_attr(OSError, "__init__", _oserror_init)
_cetta_set_type_attr(OSError, "__str__", _oserror_str)
for _name in ("errno", "strerror", "filename", "filename2"):
    _cetta_set_type_attr(OSError, _name, _cetta_getset(_name))
for _n, _t in ((11, BlockingIOError), (114, BlockingIOError), (115, BlockingIOError), (10, ChildProcessError),
               (32, BrokenPipeError), (108, BrokenPipeError), (103, ConnectionAbortedError),
               (111, ConnectionRefusedError), (104, ConnectionResetError), (17, FileExistsError),
               (2, FileNotFoundError), (21, IsADirectoryError), (20, NotADirectoryError), (4, InterruptedError),
               (13, PermissionError), (1, PermissionError), (3, ProcessLookupError), (110, TimeoutError)):
    _ERRNO_TYPES[_n] = _t
for _name in ("msg", "filename", "lineno", "offset", "text", "end_lineno", "end_offset", "print_file_and_line"):
    _cetta_set_type_attr(SyntaxError, _name, _cetta_getset(_name))


# ---------------------------------------------------------------- sets
# set and frozenset (Objects/setobject.c): their methods over the runtime's set
# table, which probes, grows and keeps removed places as CPython's does, so
# that every result iterates in CPython's order.  A result is built as a set
# and made a frozenset when the operation's left operand is one
# (make_new_set_basetype).

def _anyset(x):
    return isinstance(x, (set, frozenset))


def _set_of_kind(so, s):
    if isinstance(so, set):
        return s
    return _cetta_frozenset_of(s)


def _set_copy_new(so):
    # set_copy: a new table, so's members merged into it (set_merge)
    s = set()
    _cetta_set_merge(s, so)
    return s


def _set_copy(so):
    # frozenset_copy: an exact frozenset is itself
    if type(so) is frozenset:
        return so
    return _set_of_kind(so, _set_copy_new(so))


def _set_add(so, key):
    _cetta_set_add(so, key)


def _set_contains(so, key):
    # set_contains: a set key is looked up as the frozenset of its members
    try:
        return _cetta_set_contains(so, key)
    except TypeError:
        if not isinstance(key, set):
            raise
        return _cetta_set_contains(so, frozenset(key))


def _set_discard(so, key):
    try:
        _cetta_set_discard(so, key)
    except TypeError:
        if not isinstance(key, set):
            raise
        _cetta_set_discard(so, frozenset(key))


def _set_remove(so, key):
    try:
        found = _cetta_set_discard(so, key)
    except TypeError:
        if not isinstance(key, set):
            raise
        found = _cetta_set_discard(so, frozenset(key))
    if not found:
        raise KeyError(key)


def _set_pop(so):
    return _cetta_set_pop(so)


def _set_clear(so):
    _cetta_set_clear(so)


def _set_len(so):
    return len(_cetta_base(so))


def _set_iter(so):
    return iter(_cetta_base(so))


def _frozenset_hash(so):
    return hash(_cetta_base(so))


def _set_update(so, *others):
    for other in others:
        _cetta_set_update(so, other)


def _set_union(so, *others):
    result = _set_copy_new(so)
    for other in others:
        if other is so:
            continue
        _cetta_set_update(result, other)
    return _set_of_kind(so, result)


def _set_or(so, other):
    if not _anyset(so) or not _anyset(other):
        return NotImplemented
    result = _set_copy_new(so)
    if so is not other:
        _cetta_set_update(result, other)
    return _set_of_kind(so, result)


def _set_ror(so, other):
    return _set_or(other, so)


def _set_ior(so, other):
    if not _anyset(other):
        return NotImplemented
    _cetta_set_update(so, other)
    return so


def _set_intersection(so, other):
    # the members of the smaller set the larger holds, in the smaller's order;
    # for another iterable, its items so holds, until the result is as large
    # as so
    if so is other:
        return _set_of_kind(so, _set_copy_new(so))
    kind = so
    result = set()
    if _anyset(other):
        if len(other) > len(so):
            so, other = other, so
        for key in other:
            if _cetta_set_contains(so, key):
                _cetta_set_add(result, key)
        return _set_of_kind(kind, result)
    for key in other:
        if _cetta_set_contains(so, key):
            _cetta_set_add(result, key)
            if len(result) >= len(so):
                break
    return _set_of_kind(kind, result)


def _set_intersection_multi(so, *others):
    if not others:
        return _set_of_kind(so, _set_copy_new(so))
    result = so
    for other in others:
        result = _set_intersection(result, other)
    return result


def _set_intersection_update(so, *others):
    # the intersection, whose table so then takes (set_swap_bodies)
    _cetta_set_swap_bodies(so, _set_intersection_multi(so, *others))


def _set_and(so, other):
    if not _anyset(so) or not _anyset(other):
        return NotImplemented
    return _set_intersection(so, other)


def _set_rand(so, other):
    return _set_and(other, so)


def _set_iand(so, other):
    if not _anyset(other):
        return NotImplemented
    _cetta_set_swap_bodies(so, _set_intersection(so, other))
    return so


def _set_isdisjoint(so, other):
    if so is other:
        return len(so) == 0
    if type(other) is set or type(other) is frozenset:
        if len(other) > len(so):
            so, other = other, so
        for key in other:
            if _cetta_set_contains(so, key):
                return False
        return True
    for key in other:
        if _cetta_set_contains(so, key):
            return False
    return True


def _set_difference_update_internal(so, other):
    # other's members removed (the intersection's, when other is more than
    # eight times larger); a table more than a quarter removed places is
    # made anew
    if so is other:
        _cetta_set_clear(so)
        return
    if _anyset(other) and (len(other) >> 3) > len(so):
        other = _set_intersection(so, other)
    for key in other:
        _cetta_set_discard(so, key)
    mask, fill, used = _cetta_set_table(so)
    if fill - used > mask // 4:
        _cetta_set_resize(so, used * 2 if used > 50000 else used * 4)


def _set_copy_and_difference(so, other):
    result = _set_copy_new(so)
    _set_difference_update_internal(result, other)
    return _set_of_kind(so, result)


def _set_difference(so, other):
    # a copy less other's members when so is more than four times larger or
    # other is neither a set nor a dict; else so's members other lacks, in
    # so's order
    if not _anyset(other) and type(other) is not dict:
        return _set_copy_and_difference(so, other)
    if (len(so) >> 2) > len(other):
        return _set_copy_and_difference(so, other)
    result = set()
    if type(other) is dict:
        for key in so:
            if key not in other:
                _cetta_set_add(result, key)
    else:
        for key in so:
            if not _cetta_set_contains(other, key):
                _cetta_set_add(result, key)
    return _set_of_kind(so, result)


def _set_difference_multi(so, *others):
    if not others:
        return _set_of_kind(so, _set_copy_new(so))
    result = None
    for other in others:
        if result is None:
            result = _set_difference(so, other)
        else:
            _set_difference_update_internal(result, other)
    return result


def _set_difference_update(so, *others):
    for other in others:
        _set_difference_update_internal(so, other)


def _set_sub(so, other):
    if not _anyset(so) or not _anyset(other):
        return NotImplemented
    return _set_difference(so, other)


def _set_rsub(so, other):
    return _set_sub(other, so)


def _set_isub(so, other):
    if not _anyset(other):
        return NotImplemented
    _set_difference_update_internal(so, other)
    return so


def _set_symmetric_difference_update(so, other):
    # each of other's members removed from so when there, else added, in
    # other's order (another iterable made a set first)
    if so is other:
        _cetta_set_clear(so)
        return
    if type(other) is not dict and not _anyset(other):
        o = set()
        _cetta_set_update(o, other)
        other = o
    for key in other:
        if not _cetta_set_discard(so, key):
            _cetta_set_add(so, key)


def _set_symmetric_difference(so, other):
    # a new set of other's members, updated from so
    result = set()
    _cetta_set_update(result, other)
    _set_symmetric_difference_update(result, so)
    return _set_of_kind(so, result)


def _set_xor(so, other):
    if not _anyset(so) or not _anyset(other):
        return NotImplemented
    return _set_symmetric_difference(so, other)


def _set_rxor(so, other):
    return _set_xor(other, so)


def _set_ixor(so, other):
    if not _anyset(other):
        return NotImplemented
    _set_symmetric_difference_update(so, other)
    return so


def _set_issubset(so, other):
    if not _anyset(other):
        return len(_set_intersection(so, other)) == len(so)
    if len(so) > len(other):
        return False
    for key in so:
        if not _cetta_set_contains(other, key):
            return False
    return True


def _set_issuperset(so, other):
    if _anyset(other):
        return _set_issubset(other, so)
    for key in other:
        if not _cetta_set_contains(so, key):
            return False
    return True


def _set_lt(so, other):
    if not _anyset(other):
        return NotImplemented
    if len(so) >= len(other):
        return False
    return _set_issubset(so, other)


def _set_le(so, other):
    if not _anyset(other):
        return NotImplemented
    return _set_issubset(so, other)


def _set_gt(so, other):
    if not _anyset(other):
        return NotImplemented
    if len(so) <= len(other):
        return False
    return _set_issuperset(so, other)


def _set_ge(so, other):
    if not _anyset(other):
        return NotImplemented
    return _set_issuperset(so, other)


def _list_index(self, value, start=0, stop=9223372036854775807, /):
    # list.index: the first place in [start, stop) holding an item equal to
    # VALUE (the bounds as a slice's), else ValueError
    n = len(self)
    start = _index(start)
    stop = _index(stop)
    if start < 0:
        start = max(start + n, 0)
    if stop < 0:
        stop += n
    i = start
    while i < stop and i < len(self):
        if self[i] == value:
            return i
        i += 1
    raise ValueError(repr(value) + " is not in list")


def _dict_fromkeys(cls, iterable, value=None):
    # _PyDict_FromKeys: a new mapping of cls, each key in the iterable's order
    d = cls()
    for key in iterable:
        d[key] = value
    return d


def _dict_update_arg(d, arg):
    # dict_update_arg, for a dict D the host fills: a mapping when it has
    # keys(), else an iterable of pairs
    if hasattr(arg, "keys"):
        _dict_merge(d, arg)
    else:
        _dict_merge_seq2(d, arg)
    return None


def _dict_merge(d, b):
    # dict_merge: a dict that iterates as dict does gives its own entries;
    # any other mapping b.keys(), then b[key] for each
    if isinstance(b, dict) and type(b).__iter__ is dict.__iter__:
        for k, v in dict.items(b):
            d[k] = v
        return
    for k in list(b.keys()):
        d[k] = b[k]


def _dict_seq2_element(item, i):
    # PySequence_Fast(item, "object is not iterable") for update sequence
    # element #i (3.15): its items as a list; an element that is not
    # iterable a TypeError, any TypeError given a note naming the element
    if type(item) is list or type(item) is tuple:
        return list(item)
    it = _NULL
    try:
        it = iter(item)
    except TypeError:
        pass
    if it is _NULL:
        e = TypeError("object is not iterable")
        e.add_note("Cannot convert dictionary update sequence element #" + str(i) + " to a sequence")
        raise e
    try:
        return list(it)
    except TypeError as e:
        e.add_note("Cannot convert dictionary update sequence element #" + str(i) + " to a sequence")
        raise


def _dict_merge_seq2(d, seq):
    # PyDict_MergeFromSeq2: each element a sequence of two, key and value
    i = 0
    for item in seq:
        fast = item if type(item) is list or type(item) is tuple else _dict_seq2_element(item, i)
        if len(fast) != 2:
            raise ValueError("dictionary update sequence element #" + str(i) + " has length "
                             + str(len(fast)) + "; 2 is required")
        d[fast[0]] = fast[1]
        i += 1


def _index(item):
    # _PyNumber_Index: an int as it is; else the type's __index__, which must
    # give an int
    if isinstance(item, int):
        return item
    f = _lookup_special(item, "__index__")
    if f is _NULL:
        raise TypeError("'" + _type_name(type(item)) + "' object cannot be interpreted as an integer")
    result = f()
    if not isinstance(result, int):
        raise TypeError(_fqn(type(item)) + ".__index__() must return an int, not " + _fqn(type(result)))
    return result


def _round(number, ndigits=None):
    # builtin_round_impl: type(number).__round__, with ndigits when given
    f = _lookup_special(number, "__round__")
    if f is _NULL:
        raise TypeError("type " + _type_name(type(number)) + " doesn't define __round__ method")
    if ndigits is None:
        return f()
    return f(ndigits)


def _number_long(o):
    # PyNumber_Long for an object: __int__, which must give an int; else
    # __index__; else a str subclass's text (no __trunc__ since 3.14)
    t = type(o)
    f = _mro_lookup(t, "__int__")
    if f is not _NULL:
        result = f(o)
        if type(result) is int:
            return result
        if not isinstance(result, int):
            raise TypeError(_fqn(t) + ".__int__() must return an int, not " + _fqn(type(result)))
        return int.__int__(result)
    if _mro_lookup(t, "__index__") is not _NULL:
        return _index(o)
    if isinstance(o, str):
        return _int_new(o)
    raise TypeError("int() argument must be a string, a bytes-like object or a real number, not '"
                    + _type_name(t) + "'")


def _int_new(x=_NULL, /, base=_NULL):
    # long_new_impl: int(x) as PyNumber_Long; int(x, base) the text of a str
    # in that base
    if x is _NULL:
        if base is not _NULL:
            raise TypeError("int() missing string argument")
        return 0
    if base is _NULL:
        if not isinstance(x, str):
            return _number_long(x)
        b = 10
    else:
        b = _index(base)
        if (b != 0 and b < 2) or b > 36:
            raise ValueError("int() base must be >= 2 and <= 36, or 0")
        if not isinstance(x, str):
            raise TypeError("int() can't convert non-string with explicit base")
    r = _int_parse(str.__str__(x), b)
    if r is _NULL:
        # PyLong_FromUnicodeObject: the base asked for, the argument's repr
        raise ValueError("invalid literal for int() with base " + str(b) + ": " + repr(x)[:200])
    return r


def _int_digit(c):
    o = ord(c)
    if 48 <= o <= 57:
        return o - 48
    if 97 <= o <= 122:
        return o - 87
    if 65 <= o <= 90:
        return o - 55
    return 99


def _int_parse(s, base):
    # PyLong_FromString: blanks around; a sign; base 0 takes the prefix's
    # base (a bare leading 0 only for zero); the base's prefix, an underscore
    # after it; digits, single underscores between; _NULL when not an int
    t = s.strip()
    i = 0
    neg = False
    if t[:1] == "+" or t[:1] == "-":
        neg = t[:1] == "-"
        i = 1
    zero_only = False
    p = t[i + 1:i + 2].lower()
    if base == 0:
        if t[i:i + 1] != "0":
            base = 10
        elif p == "x":
            base = 16
        elif p == "o":
            base = 8
        elif p == "b":
            base = 2
        else:
            zero_only = True
            base = 10
    if t[i:i + 1] == "0" and ((base == 16 and p == "x") or (base == 8 and p == "o") or (base == 2 and p == "b")):
        i += 2
        if t[i:i + 1] == "_":
            i += 1
    n = len(t)
    if i >= n:
        return _NULL
    value = 0
    prev_underscore = True
    while i < n:
        c = t[i]
        if c == "_":
            if prev_underscore:
                return _NULL
            prev_underscore = True
        else:
            d = _int_digit(c)
            if d >= base:
                return _NULL
            value = value * base + d
            prev_underscore = False
        i += 1
    if prev_underscore:
        return _NULL
    if zero_only and value != 0:
        return _NULL
    return -value if neg else value


def _number_float(o):
    # PyNumber_Float for an object: __float__, which must give a float; else
    # __index__; else a str subclass's text
    t = type(o)
    f = _mro_lookup(t, "__float__")
    if f is not _NULL:
        res = f(o)
        if type(res) is float:
            return res
        if not isinstance(res, float):
            raise TypeError(_fqn(t) + ".__float__() must return a float, not " + _fqn(type(res)))
        return float.__float__(res)
    if _mro_lookup(t, "__index__") is not _NULL:
        return float(_index(o))
    if isinstance(o, str):
        return float(str.__str__(o))
    raise TypeError("float() argument must be a string or a real number, not '" + _type_name(t) + "'")


def _dict_update_mapping(d, b):
    # DICT_UPDATE ({**b}): b's entries as a mapping's; an object without
    # keys() is not one
    try:
        _dict_merge(d, b)
        return None
    except AttributeError:
        pass
    raise TypeError("'" + _type_name(type(b)) + "' object is not a mapping")


def _dict_missing(d, key):
    # dict_subscript for an instance of a subclass: type(d).__missing__(d,
    # key) for a missing key, else KeyError
    f = _lookup_special(d, "__missing__")
    if f is _NULL:
        raise KeyError(key)
    return f(key)


for _t in (set, frozenset):
    for _name, _f in (("copy", _set_copy), ("union", _set_union), ("intersection", _set_intersection_multi),
                      ("difference", _set_difference_multi),
                      ("symmetric_difference", _set_symmetric_difference), ("issubset", _set_issubset),
                      ("issuperset", _set_issuperset), ("isdisjoint", _set_isdisjoint),
                      ("__contains__", _set_contains), ("__len__", _set_len), ("__iter__", _set_iter),
                      ("__or__", _set_or), ("__ror__", _set_ror), ("__and__", _set_and), ("__rand__", _set_rand),
                      ("__sub__", _set_sub), ("__rsub__", _set_rsub), ("__xor__", _set_xor),
                      ("__rxor__", _set_rxor), ("__lt__", _set_lt), ("__le__", _set_le),
                      ("__gt__", _set_gt), ("__ge__", _set_ge)):
        _cetta_set_type_attr(_t, _name, _f)
for _name, _f in (("add", _set_add), ("discard", _set_discard), ("remove", _set_remove), ("pop", _set_pop),
                  ("clear", _set_clear), ("update", _set_update),
                  ("intersection_update", _set_intersection_update),
                  ("difference_update", _set_difference_update),
                  ("symmetric_difference_update", _set_symmetric_difference_update),
                  ("__ior__", _set_ior), ("__iand__", _set_iand), ("__isub__", _set_isub),
                  ("__ixor__", _set_ixor)):
    _cetta_set_type_attr(set, _name, _f)
_cetta_set_type_attr(set, "__hash__", None)
_cetta_set_type_attr(frozenset, "__hash__", _frozenset_hash)
_cetta_set_type_attr(dict, "fromkeys", classmethod(_dict_fromkeys))
_cetta_set_type_attr(list, "index", _list_index)


# ---------------------------------------------------------------- match
# The checks a match statement's patterns make (ceval's MATCH_SEQUENCE,
# MATCH_MAPPING, MATCH_KEYS, MATCH_CLASS, and a mapping pattern's **rest).

def _match_sequence(subject):
    # Py_TPFLAGS_SEQUENCE, by the subject's own type: list, tuple, range and
    # their subclasses (not str, bytes or bytearray)
    return issubclass(type(subject), (list, tuple, range))


def _match_mapping(subject):
    # Py_TPFLAGS_MAPPING: dict and its subclasses
    return issubclass(type(subject), dict)


def _match_keys(subject, keys):
    # match_keys: subject.get(key, dummy) for each key in turn; None when one
    # is missing; a key checked twice is a ValueError
    if not keys:
        return ()
    get = subject.get
    seen = set()
    dummy = object()
    values = []
    for key in keys:
        if key in seen:
            raise ValueError("mapping pattern checks duplicate key (" + repr(key) + ")")
        seen.add(key)
        value = get(key, dummy)
        if value is dummy:
            return None
        values.append(value)
    return tuple(values)


# the builtin types whose positional sub-pattern is the subject itself
# (_Py_TPFLAGS_MATCH_SELF, inherited by their subclasses)
_match_self_types = (bool, bytearray, bytes, dict, float, frozenset, int, list, set, str, tuple)


def _match_class_attr(subject, cls, name, seen):
    if name in seen:
        raise TypeError(cls.__name__ + "() got multiple sub-patterns for attribute " + repr(name))
    seen.add(name)
    try:
        return getattr(subject, name)
    except AttributeError:
        return _NULL


def _match_class(subject, cls, nargs, names):
    # match_class: an instance of cls, then its attributes: the positional
    # sub-patterns' by cls.__match_args__ (the subject itself for the types
    # that match themselves), then the keyword sub-patterns'; None when one
    # is missing
    if not isinstance(cls, type):
        raise TypeError("called match pattern must be a class")
    # PyObject_IsInstance: the class's __instancecheck__ is asked
    if not _builtin_isinstance(subject, cls):
        return None
    seen = set()
    attrs = []
    if nargs:
        match_self = False
        try:
            match_args = cls.__match_args__
        except AttributeError:
            match_args = _NULL
        if match_args is not _NULL:
            if type(match_args) is not tuple:
                raise TypeError(cls.__name__ + ".__match_args__ must be a tuple (got "
                                + type(match_args).__name__ + ")")
        else:
            match_args = ()
            match_self = issubclass(cls, _match_self_types)
        allowed = 1 if match_self else len(match_args)
        if allowed < nargs:
            plural = "" if allowed == 1 else "s"
            raise TypeError(cls.__name__ + "() accepts " + str(allowed) + " positional sub-pattern" + plural
                            + " (" + str(nargs) + " given)")
        if match_self:
            attrs.append(subject)
        else:
            for i in range(nargs):
                name = match_args[i]
                if type(name) is not str:
                    raise TypeError("__match_args__ elements must be strings (got " + type(name).__name__ + ")")
                attr = _match_class_attr(subject, cls, name, seen)
                if attr is _NULL:
                    return None
                attrs.append(attr)
    for name in names:
        attr = _match_class_attr(subject, cls, name, seen)
        if attr is _NULL:
            return None
        attrs.append(attr)
    return tuple(attrs)


def _match_rest(subject, keys):
    # a mapping pattern's **rest: a dict of the subject's items, less the keys
    rest = {}
    rest.update(subject)
    for key in keys:
        del rest[key]
    return rest


# ---------------------------------------------------------------- coroutines
# A coroutine (an async def's call) shares a generator's machinery, and its
# send, throw and close; it is not iterable.  Awaiting it, or its
# __await__() (a coroutine_wrapper), drives it.

_coroutine = _cetta_builtin_type("coroutine")


class _coroutine_wrapper:
    # coro.__await__(): an iterator over the coroutine's steps
    def __init__(self, coro):
        self._cw_coro = coro

    def __iter__(self):
        return self

    def __next__(self):
        return _cetta_gen_send(self._cw_coro, None)

    def send(self, value):
        return _cetta_gen_send(self._cw_coro, value)

    def throw(self, typ, val=_NULL, tb=_NULL):
        return _generator_throw(self._cw_coro, typ, val, tb)

    def close(self):
        return _cetta_gen_close(self._cw_coro)


_coroutine_wrapper.__name__ = "coroutine_wrapper"
_coroutine_wrapper.__qualname__ = "coroutine_wrapper"


def _coroutine_await(self):
    return _coroutine_wrapper(self)


def _awaitable_iter(o):
    # _PyCoro_GetAwaitableIter: a coroutine is its own; else its type's
    # __await__(), whose result must be an iterator, not a coroutine
    if type(o) is _coroutine:
        return o
    m = _lookup_special(o, "__await__")
    if m is _NULL:
        raise TypeError("'" + _type_name(type(o)) + "' object can't be awaited")
    return _awaited_iter(o, m())


def _get_awaitable_iter(o, op):
    # GET_AWAITABLE for an object that is not a coroutine; a missing __await__
    # named by where the awaitable came from (_PyEval_FormatAwaitableError)
    m = _lookup_special(o, "__await__")
    if m is _NULL:
        if op == 1:
            raise TypeError("'async with' received an object from __aenter__ that does not implement __await__: "
                            + type(o).__name__)
        if op == 2:
            raise TypeError("'async with' received an object from __aexit__ that does not implement __await__: "
                            + type(o).__name__)
        raise TypeError("'" + _type_name(type(o)) + "' object can't be awaited")
    return _awaited_iter(o, m())


def _awaited_iter(o, res):
    # what o's __await__() gave: an iterator, never a coroutine
    if type(res) is _coroutine:
        raise TypeError(_fqn(type(o)) + ".__await__() must return an iterator, not coroutine")
    if not _cetta_is_iterator(res):
        raise TypeError(_fqn(type(o)) + ".__await__() must return an iterator, not " + _fqn(type(res)))
    return res


def _get_aiter(obj):
    # GET_AITER: type(obj).__aiter__(), whose result must have __anext__
    m = _lookup_special(obj, "__aiter__")
    if m is _NULL:
        raise TypeError("'async for' requires an object with __aiter__ method, got " + type(obj).__name__)
    it = m()
    if _mro_lookup(type(it), "__anext__") is _NULL:
        raise TypeError("'async for' received an object from __aiter__ that does not implement __anext__: "
                        + type(it).__name__)
    return it


def _get_anext(aiter):
    # GET_ANEXT: type(aiter).__anext__(), made an awaitable's iterator
    m = _lookup_special(aiter, "__anext__")
    if m is _NULL:
        raise TypeError("'async for' requires an iterator with __anext__ method, got " + type(aiter).__name__)
    nxt = m()
    try:
        return _awaitable_iter(nxt)
    except TypeError as e:
        raise TypeError("'async for' received an invalid object from __anext__: " + type(nxt).__name__) from e


def _missed_async_special(mgr, name):
    # the async protocol's missing method; the sync protocol suggested when
    # the type has it
    t = type(mgr)
    text = ("'" + _fqn(t) + "' object does not support the asynchronous context manager protocol (missed "
            + name + " method)")
    if _mro_lookup(t, "__enter__") is not _NULL and _mro_lookup(t, "__exit__") is not _NULL:
        text += " but it supports the context manager protocol. Did you mean to use 'with'?"
    return TypeError(text)


def _before_async_with(mgr):
    # async with (codegen 3.14): __aexit__ and __aenter__ on the type, in that
    # order, then __aenter__(): (the bound __aexit__, its result)
    ex = _lookup_special(mgr, "__aexit__")
    if ex is _NULL:
        raise _missed_async_special(mgr, "__aexit__")
    enter = _lookup_special(mgr, "__aenter__")
    if enter is _NULL:
        raise _missed_async_special(mgr, "__aenter__")
    return (ex, enter())


_cetta_set_type_attr(_coroutine, "send", _generator_send)
_cetta_set_type_attr(_coroutine, "throw", _generator_throw)
_cetta_set_type_attr(_coroutine, "close", _generator_close)
_cetta_set_type_attr(_coroutine, "__await__", _coroutine_await)
for _name in ("__name__", "__qualname__", "cr_running", "cr_suspended", "cr_await"):
    _cetta_set_type_attr(_coroutine, _name, _cetta_getset(_name))


# ---------------------------------------------------------------- async generators
# An async generator (an async def that yields) is stepped by awaitables
# (genobject.c): __anext__() and asend() make an asend awaitable, athrow() and
# aclose() an athrow one.  A step's async yield ends the awaitable with its
# value (StopIteration(value)); what an await inside yields passes through;
# the generator's end is StopAsyncIteration (async_gen_unwrap_value).

_async_generator = _cetta_builtin_type("async_generator")


def _agen_unwrap(gen, how, arg):
    # a step, as async_gen_unwrap_value sees it
    try:
        kind, value = _cetta_gen_step(gen, how, arg)
    except BaseException as e:
        if isinstance(e, (StopAsyncIteration, GeneratorExit)):
            _cetta_set_field(gen, "agclosed", True)
        _cetta_set_field(gen, "agrun", False)
        raise
    if kind == "w":
        _cetta_set_field(gen, "agrun", False)
        raise StopIteration(value)
    if kind == "r":
        _cetta_set_field(gen, "agclosed", True)
        _cetta_set_field(gen, "agrun", False)
        raise StopAsyncIteration
    return value


class _async_generator_asend:
    # async_gen_asend_send / async_gen_asend_throw
    def __init__(self, gen, sendval):
        self._gen = gen
        self._sendval = sendval
        self._state = 0

    def __await__(self):
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return self.send(None)

    def send(self, arg):
        if self._state == 2:
            raise RuntimeError("cannot reuse already awaited __anext__()/asend()")
        if self._state == 0:
            if _cetta_field(self._gen, "agrun"):
                self._state = 2
                raise RuntimeError("anext(): asynchronous generator is already running")
            if arg is None:
                arg = self._sendval
            self._state = 1
        _cetta_set_field(self._gen, "agrun", True)
        try:
            return _agen_unwrap(self._gen, "send", arg)
        except BaseException:
            self._state = 2
            raise

    def throw(self, typ, val=_NULL, tb=_NULL):
        if self._state == 2:
            raise RuntimeError("cannot reuse already awaited __anext__()/asend()")
        if self._state == 0:
            if _cetta_field(self._gen, "agrun"):
                self._state = 2
                raise RuntimeError("anext(): asynchronous generator is already running")
            self._state = 1
            _cetta_set_field(self._gen, "agrun", True)
        try:
            return _agen_unwrap(self._gen, "throw", _thrown(typ, val, tb))
        except BaseException:
            _cetta_set_field(self._gen, "agrun", False)
            self._state = 2
            raise

    def close(self):
        self._state = 2


class _async_generator_athrow:
    # async_gen_athrow_send / async_gen_athrow_throw; ARGS None for aclose()
    def __init__(self, gen, args):
        self._gen = gen
        self._args = args
        self._state = 0

    def __await__(self):
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return self.send(None)

    def send(self, arg):
        gen = self._gen
        if self._state == 2:
            raise RuntimeError("cannot reuse already awaited aclose()/athrow()")
        if _cetta_gen_finished(gen):
            self._state = 2
            raise StopIteration
        if self._state == 0:
            if _cetta_field(gen, "agrun"):
                self._state = 2
                if self._args is None:
                    raise RuntimeError("aclose(): asynchronous generator is already running")
                raise RuntimeError("athrow(): asynchronous generator is already running")
            if _cetta_field(gen, "agclosed"):
                self._state = 2
                raise StopAsyncIteration
            if arg is not None:
                raise RuntimeError("can't send non-None value to a just-started coroutine")
            self._state = 1
            _cetta_set_field(gen, "agrun", True)
            if self._args is None:
                _cetta_set_field(gen, "agclosed", True)
                return self._close_step("throw", GeneratorExit())
            typ, val, tb = self._args
            return self._throw_step("throw", _thrown(typ, val, tb))
        if self._args is None:
            return self._close_step("send", arg)
        return self._throw_step("send", arg)

    def throw(self, typ, val=_NULL, tb=_NULL):
        if self._state == 2:
            raise RuntimeError("cannot reuse already awaited aclose()/athrow()")
        if self._args is None:
            return self._close_step("throw", _thrown(typ, val, tb))
        return self._throw_step("throw", _thrown(typ, val, tb))

    def close(self):
        self._state = 2

    def _throw_step(self, how, arg):
        try:
            return _agen_unwrap(self._gen, how, arg)
        except BaseException:
            self._state = 2
            raise

    def _close_step(self, how, arg):
        # aclose(): an async yield is a RuntimeError; the end, or a
        # StopAsyncIteration or GeneratorExit, completes the awaitable
        gen = self._gen
        try:
            kind, value = _cetta_gen_step(gen, how, arg)
        except (StopAsyncIteration, GeneratorExit):
            _cetta_set_field(gen, "agrun", False)
            self._state = 2
            raise StopIteration
        except BaseException:
            _cetta_set_field(gen, "agrun", False)
            self._state = 2
            raise
        if kind == "w":
            _cetta_set_field(gen, "agrun", False)
            self._state = 2
            raise RuntimeError("async generator ignored GeneratorExit")
        if kind == "r":
            _cetta_set_field(gen, "agrun", False)
            self._state = 2
            raise StopIteration
        return value


for _cls, _nm in ((_async_generator_asend, "async_generator_asend"), (_async_generator_athrow, "async_generator_athrow")):
    _cls.__name__ = _nm
    _cls.__qualname__ = _nm


def _agen_aiter(self):
    return self


def _agen_anext(self):
    return _async_generator_asend(self, None)


def _agen_asend(self, value):
    return _async_generator_asend(self, value)


def _agen_athrow(self, typ, val=_NULL, tb=_NULL):
    return _async_generator_athrow(self, (typ, val, tb))


def _agen_aclose(self):
    return _async_generator_athrow(self, None)


for _name, _f in (("__aiter__", _agen_aiter), ("__anext__", _agen_anext), ("asend", _agen_asend),
                  ("athrow", _agen_athrow), ("aclose", _agen_aclose)):
    _cetta_set_type_attr(_async_generator, _name, _f)
for _name in ("__name__", "__qualname__", "ag_running", "ag_suspended", "ag_await"):
    _cetta_set_type_attr(_async_generator, _name, _cetta_getset(_name))


# ---------------------------------------------------------------- exception groups
# BaseExceptionGroup and ExceptionGroup (Objects/exceptions.c), and what
# except* asks of them (ceval's CHECK_EG_MATCH, PREP_RERAISE_STAR).

def _is_sequence(x):
    # PySequence_Check: item access, and no mapping
    if isinstance(x, dict):
        return False
    if isinstance(x, (list, tuple, str, range, bytes)):
        return True
    return _mro_lookup(type(x), "__getitem__") is not _NULL


def _eg_new(cls, *args, **kwargs):
    # BaseExceptionGroup_new: a message and a non-empty sequence of
    # exceptions; BaseExceptionGroup is an ExceptionGroup when all of them are
    # Exceptions, and an ExceptionGroup takes no BaseException
    if len(args) != 2:
        raise TypeError("BaseExceptionGroup.__new__() takes exactly 2 arguments (" + str(len(args)) + " given)")
    message, exceptions = args
    if not isinstance(message, str):
        raise TypeError("BaseExceptionGroup.__new__() argument 1 must be str, not " + type(message).__name__)
    if not _is_sequence(exceptions):
        raise TypeError("second argument (exceptions) must be a sequence")
    excs_str = None
    if not isinstance(exceptions, (list, tuple)):
        excs_str = repr(exceptions)
    excs = tuple(exceptions)
    if not excs:
        raise ValueError("second argument (exceptions) must be a non-empty sequence")
    nested_base = False
    i = 0
    for exc in excs:
        if not isinstance(exc, BaseException):
            raise ValueError("Item " + str(i) + " of second argument (exceptions) is not an exception")
        if not isinstance(exc, Exception):
            nested_base = True
        i += 1
    if cls is ExceptionGroup:
        if nested_base:
            raise TypeError("Cannot nest BaseExceptions in an ExceptionGroup")
    elif cls is BaseExceptionGroup:
        if not nested_base:
            cls = ExceptionGroup
    elif nested_base and issubclass(cls, Exception):
        raise TypeError("Cannot nest BaseExceptions in '" + cls.__name__ + "'")
    self = _cetta_exception_new(cls, args)
    _cetta_set_field(self, "egmsg", message)
    _cetta_set_field(self, "egexcs", excs)
    _cetta_set_field(self, "egexcs_str", excs_str)
    return self


def _eg_str(self):
    n = len(_cetta_field(self, "egexcs"))
    return _cetta_field(self, "egmsg") + " (" + str(n) + " sub-exception" + ("s" if n > 1 else "") + ")"


def _eg_repr(self):
    excs_str = _cetta_field(self, "egexcs_str")
    if excs_str is None:
        if isinstance(self.args[1], list):
            excs_str = repr(list(_cetta_field(self, "egexcs")))
        else:
            excs_str = repr(_cetta_field(self, "egexcs"))
    return type(self).__name__ + "(" + repr(_cetta_field(self, "egmsg")) + ", " + excs_str + ")"


def _eg_message(self):
    return _cetta_field(self, "egmsg")


def _eg_exceptions(self):
    return _cetta_field(self, "egexcs")


def _eg_derive(self, excs):
    return BaseExceptionGroup(_cetta_field(self, "egmsg"), excs)


def _eg_subset(orig, excs):
    # exceptiongroup_subset: derive(), with orig's traceback, context, cause
    # and a copy of its notes
    if not excs:
        return None
    eg = orig.derive(excs)
    if not isinstance(eg, BaseExceptionGroup):
        raise TypeError("derive must return an instance of BaseExceptionGroup")
    tb = orig.__traceback__
    if tb is not None:
        eg.__traceback__ = tb
    eg.__context__ = orig.__context__
    eg.__cause__ = orig.__cause__
    try:
        notes = orig.__notes__
    except AttributeError:
        notes = _NULL
    if notes is not _NULL and _is_sequence(notes):
        eg.__notes__ = list(notes)
    return eg


def _eg_matcher(value):
    # get_matcher_type: 0 by type, 1 by predicate
    if callable(value) and not isinstance(value, type):
        return 1
    if isinstance(value, type) and issubclass(value, BaseException):
        return 0
    if type(value) is tuple:
        for t in value:
            if not (isinstance(t, type) and issubclass(t, BaseException)):
                break
        else:
            return 0
    raise TypeError("expected an exception type, a tuple of exception types, or a callable (other than a class)")


def _eg_check_match(exc, kind, value):
    if kind == 0:
        return isinstance(exc, value)
    if kind == 1:
        return bool(value(exc))
    # the identities of leaf exceptions to keep
    if not isinstance(exc, BaseExceptionGroup):
        return id(exc) in value
    return False


def _eg_split_recursive(exc, kind, value, construct_rest):
    # exceptiongroup_split_recursive: (match, rest)
    if _eg_check_match(exc, kind, value):
        return exc, None
    if not isinstance(exc, BaseExceptionGroup):
        return None, (exc if construct_rest else None)
    match_list = []
    rest_list = []
    for e in _cetta_field(exc, "egexcs"):
        m, r = _eg_split_recursive(e, kind, value, construct_rest)
        if m is not None:
            match_list.append(m)
        if r is not None:
            rest_list.append(r)
    match = _eg_subset(exc, match_list)
    rest = _eg_subset(exc, rest_list) if construct_rest else None
    return match, rest


def _eg_split(self, matcher_value):
    kind = _eg_matcher(matcher_value)
    return _eg_split_recursive(self, kind, matcher_value, True)


def _eg_subgroup(self, matcher_value):
    kind = _eg_matcher(matcher_value)
    return _eg_split_recursive(self, kind, matcher_value, False)[0]


def _check_except_star_type(t):
    # _PyEval_CheckExceptStarTypeValid
    items = t if isinstance(t, tuple) else (t,)
    for exc in items:
        if not (isinstance(exc, type) and issubclass(exc, BaseException)):
            raise TypeError("catching classes that do not inherit from BaseException is not allowed")
    for exc in items:
        if issubclass(exc, BaseExceptionGroup):
            raise TypeError("catching ExceptionGroup with except* is not allowed. Use except instead.")


def _eg_match(exc_value, match_type):
    # CHECK_EG_MATCH (_PyEval_ExceptionGroupMatch): (rest, match); a naked
    # exception that matches is wrapped in a group with an empty message
    _check_except_star_type(match_type)
    if exc_value is None:
        return (None, None)
    if isinstance(exc_value, match_type):
        if isinstance(exc_value, BaseExceptionGroup):
            return (None, exc_value)
        return (None, BaseExceptionGroup("", (exc_value,)))
    if isinstance(exc_value, BaseExceptionGroup):
        pair = exc_value.split(match_type)
        if type(pair) is not tuple:
            raise TypeError(type(exc_value).__name__ + ".split must return a tuple, not " + type(pair).__name__)
        if len(pair) < 2:
            raise TypeError(type(exc_value).__name__ + ".split must return a 2-tuple, got tuple of size "
                            + str(len(pair)))
        return (pair[1], pair[0])
    return (exc_value, None)


def _exc_notes(e):
    try:
        return e.__notes__
    except AttributeError:
        return _NULL


def _same_exception_metadata(e1, e2):
    return (_exc_notes(e1) is _exc_notes(e2) and e1.__traceback__ is e2.__traceback__
            and e1.__cause__ is e2.__cause__ and e1.__context__ is e2.__context__)


def _collect_leaf_ids(exc, ids):
    if exc is None:
        return
    if not isinstance(exc, BaseExceptionGroup):
        ids.add(id(exc))
        return
    for e in _cetta_field(exc, "egexcs"):
        _collect_leaf_ids(e, ids)


def _prep_reraise_star(orig, excs):
    # _PyExc_PrepReraiseStar: what the handlers re-raised keeps its place in
    # orig's nesting; what they raised anew joins it in a new group
    if not excs:
        return None
    if not isinstance(orig, BaseExceptionGroup):
        return excs[0]
    raised = []
    reraised = []
    for e in excs:
        if e is None:
            continue
        if _same_exception_metadata(e, orig):
            reraised.append(e)
        else:
            raised.append(e)
    leaf_ids = set()
    for e in reraised:
        _collect_leaf_ids(e, leaf_ids)
    reraised_eg = _eg_split_recursive(orig, 2, leaf_ids, False)[0]
    if not raised:
        return reraised_eg
    if reraised_eg is not None:
        raised.append(reraised_eg)
    if len(raised) > 1:
        return BaseExceptionGroup("", raised)
    return raised[0]


_cetta_set_type_attr(BaseExceptionGroup, "__new__", staticmethod(_eg_new))
_cetta_set_type_attr(BaseExceptionGroup, "__init__", _exception_init)
for _name, _f in (("__str__", _eg_str), ("__repr__", _eg_repr), ("derive", _eg_derive), ("split", _eg_split),
                  ("subgroup", _eg_subgroup)):
    _cetta_set_type_attr(BaseExceptionGroup, _name, _f)
_cetta_set_type_attr(BaseExceptionGroup, "message", property(_eg_message))
_cetta_set_type_attr(BaseExceptionGroup, "exceptions", property(_eg_exceptions))


# ---------------------------------------------------------------- the report of an uncaught exception
# traceback.TracebackException (compact) and its format(): the exceptions an
# exception was raised from or during, each once, reported first; a raised
# exception under a "Traceback" header (no frames are kept yet); an exception
# group in its box, its exceptions in turn (at most 15 shown, 10 deep).

class _TE:
    def __init__(self, exc, seen):
        self.exc = exc
        self.cause = None
        self.context = None
        self.exceptions = None
        seen.add(id(exc))


def _te_tree(exc):
    seen = set()
    top = _TE(exc, seen)
    queue = [(top, exc)]
    while queue:
        te, e = queue.pop()
        cause = None
        if e.__cause__ is not None and id(e.__cause__) not in seen:
            cause = _TE(e.__cause__, seen)
        context = None
        if (cause is None and not e.__suppress_context__ and e.__context__ is not None
                and id(e.__context__) not in seen):
            context = _TE(e.__context__, seen)
        exceptions = None
        if isinstance(e, BaseExceptionGroup):
            exceptions = []
            for x in e.exceptions:
                exceptions.append(_TE(x, seen))
        te.cause = cause
        te.context = context
        te.exceptions = exceptions
        if cause is not None:
            queue.append((cause, e.__cause__))
        if context is not None:
            queue.append((context, e.__context__))
        if exceptions is not None:
            i = 0
            for x in e.exceptions:
                queue.append((exceptions[i], x))
                i += 1
    return top


class _PrintContext:
    def __init__(self):
        self.depth = 0
        self.need_close = False

    def indent(self):
        return " " * (2 * self.depth)

    def emit(self, out, text, margin="|"):
        prefix = self.indent()
        if self.depth:
            prefix = prefix + margin + " "
        for line in text.splitlines(True):
            out.append(prefix + line)


def _syntax_error_lines(exc, stype):
    # TracebackException._format_syntax_error: where, the line with carets
    # under the error's columns, the message
    lines = []
    filename, lineno, text, offset = exc.filename, exc.lineno, exc.text, exc.offset
    end_lineno, end_offset = exc.end_lineno, exc.end_offset
    suffix = ""
    if lineno is not None:
        lines.append('  File "' + (filename or "<string>") + '", line ' + str(lineno) + "\n")
    elif filename is not None:
        suffix = " (" + str(filename) + ")"
    if isinstance(text, str):
        rtext = text.rstrip("\n")
        ltext = rtext.lstrip(" \n\f")
        spaces = len(rtext) - len(ltext)
        if offset is None:
            lines.append("    " + ltext + "\n")
        elif isinstance(offset, int):
            if lineno == end_lineno:
                end = end_offset if isinstance(end_offset, int) and end_offset != 0 else offset
            else:
                end = len(rtext) + 1
            if text and offset > len(text):
                offset = len(rtext) + 1
            if text and end > len(text):
                end = len(rtext) + 1
            if offset >= end or end < 0:
                end = offset + 1
            colno = offset - 1 - spaces
            end_colno = end - 1 - spaces
            lines.append("    " + ltext + "\n")
            if colno >= 0:
                caretspace = "".join(c if c.isspace() else " " for c in ltext[:colno])
                lines.append("    " + caretspace + "^" * (end_colno - colno) + "\n")
    msg = exc.msg or "<no detail available>"
    lines.append(stype + ": " + str(msg) + suffix + "\n")
    return lines


# methods of other languages and of the mutable types, and what Python has
# instead (traceback._CROSS_LANGUAGE_HINTS)
_CROSS_LANGUAGE_HINTS = {
    "push": ((list, "append", False),),
    "concat": ((list, "extend", False),),
    "addAll": ((list, "extend", False),),
    "contains": ((list, "Use 'x in list'.", True),),
    "add": ((list, "Did you mean to use a 'set' object?", True),
            (frozenset, "Did you mean to use a 'set' object?", True)),
    "toUpperCase": ((str, "upper", False),),
    "toLowerCase": ((str, "lower", False),),
    "trimStart": ((str, "lstrip", False),),
    "trimEnd": ((str, "rstrip", False),),
    "keySet": ((dict, "keys", False),),
    "entrySet": ((dict, "items", False),),
    "entries": ((dict, "items", False),),
    "putAll": ((dict, "update", False),),
    "put": ((dict, "Use d[k] = v.", True),),
    "append": ((tuple, "Did you mean to use a 'list' object?", True),),
    "extend": ((tuple, "Did you mean to use a 'list' object?", True),),
    "insert": ((tuple, "Did you mean to use a 'list' object?", True),),
    "remove": ((tuple, "Did you mean to use a 'list' object?", True),
               (frozenset, "Did you mean to use a 'set' object?", True)),
    "discard": ((frozenset, "Did you mean to use a 'set' object?", True),),
    "update": ((frozenset, "Did you mean to use a 'set' object?", True),),
    "__or__": ((float, "Did you mean to use an 'int' object? Bitwise operators are not supported by 'float'.", True),),
    "__and__": ((float, "Did you mean to use an 'int' object? Bitwise operators are not supported by 'float'.", True),),
    "__xor__": ((float, "Did you mean to use an 'int' object? Bitwise operators are not supported by 'float'.", True),),
    "__lshift__": ((float, "Did you mean to use an 'int' object? Bitwise operators are not supported by 'float'.", True),),
    "__rshift__": ((float, "Did you mean to use an 'int' object? Bitwise operators are not supported by 'float'.", True),),
}


def _cross_language_hint(obj, wrong_name):
    entries = _CROSS_LANGUAGE_HINTS.get(wrong_name)
    if entries is None:
        return None
    for check_type, hint, is_raw in entries:
        if isinstance(obj, check_type):
            return hint if is_raw else "Did you mean '." + hint + "'?"
    return None


def _safe_dir(obj):
    # traceback._get_safe___dir__: obj.__dir__(), sorted, its str names
    try:
        d = obj.__dir__()
    except TypeError:
        d = type(obj).__dir__(obj)
    return sorted([x for x in d if isinstance(x, str)])


def _compute_suggestion_error(exc, tb, wrong_name):
    # traceback._compute_suggestion_error: the names a mistyped one is
    # compared with, then the nearest (_Py_CalculateSuggestions)
    if wrong_name is None or not isinstance(wrong_name, str):
        return None
    if isinstance(exc, AttributeError):
        obj = exc.obj
        try:
            d = _safe_dir(obj)
            hide_underscored = wrong_name[:1] != "_"
            if hide_underscored and tb is not None:
                while tb.tb_next is not None:
                    tb = tb.tb_next
                frame = tb.tb_frame
                if "self" in frame.f_locals and frame.f_locals["self"] is obj:
                    hide_underscored = False
            if hide_underscored:
                d = [x for x in d if x[:1] != "_"]
        except Exception:
            return None
    elif isinstance(exc, ModuleNotFoundError):
        return None
    elif isinstance(exc, ImportError):
        try:
            mod = __import__(exc.name)
            d = _safe_dir(mod)
            if wrong_name[:1] != "_":
                d = [x for x in d if x[:1] != "_"]
        except Exception:
            return None
    else:
        try:
            if tb is None:
                return None
            while tb.tb_next is not None:
                tb = tb.tb_next
            frame = tb.tb_frame
            d = list(frame.f_locals) + list(frame.f_globals) + list(frame.f_builtins)
            d = [x for x in d if isinstance(x, str)]
            if "self" in frame.f_locals:
                self = frame.f_locals["self"]
                try:
                    has_wrong_name = hasattr(self, wrong_name)
                except Exception:
                    has_wrong_name = False
                if has_wrong_name:
                    return "self." + wrong_name
        except Exception:
            return None
    return _cetta_suggestion(wrong_name, d)


def _suggestion_text(exc, s):
    # TracebackException.__init__: what is added to the message of an import,
    # attribute or name error
    tb = exc.__traceback__
    if isinstance(exc, ImportError) and getattr(exc, "name_from", None) is not None:
        suggestion = _compute_suggestion_error(exc, tb, exc.name_from)
        if suggestion:
            s += ". Did you mean: '" + suggestion + "'?" if suggestion.isascii() else ". Did you mean: '" + suggestion + "' (" + ascii(suggestion) + ")?"
    elif isinstance(exc, ModuleNotFoundError):
        module_name = getattr(exc, "name", None)
        if module_name in _STDLIB_MODULE_NAMES:
            s = _MISSING_STDLIB_MODULE_MESSAGES.get(
                module_name, "Standard library module " + repr(module_name) + " was not found")
    elif isinstance(exc, AttributeError) and getattr(exc, "name", None) is not None:
        wrong_name = exc.name
        hint = _cross_language_hint(exc.obj, wrong_name) if hasattr(exc, "obj") else None
        if hint:
            s += ". " + hint
        else:
            suggestion = _compute_suggestion_error(exc, tb, wrong_name)
            if suggestion:
                if suggestion.isascii():
                    s += ". Did you mean '." + suggestion + "' instead of '." + wrong_name + "'?"
                else:
                    s += (". Did you mean '." + suggestion + "' (" + ascii(suggestion) + ") instead of '."
                          + wrong_name + "' (" + ascii(wrong_name) + ")?")
    elif isinstance(exc, NameError) and getattr(exc, "name", None) is not None:
        wrong_name = exc.name
        suggestion = _compute_suggestion_error(exc, tb, wrong_name)
        if suggestion:
            s += ". Did you mean: '" + suggestion + "'?" if suggestion.isascii() else ". Did you mean: '" + suggestion + "' (" + ascii(suggestion) + ")?"
        if wrong_name in _STDLIB_MODULE_NAMES:
            if suggestion:
                s += " Or did you forget to import '" + wrong_name + "'?"
            else:
                s += ". Did you forget to import '" + wrong_name + "'?"
    return s


def _exception_only(exc):
    # format_exception_only: the type (after its module, unless builtins or
    # __main__) and str(exc) when not empty, with what the traceback module
    # suggests, then the notes
    stype = _cetta_exc_type_display(type(exc))
    if isinstance(exc, SyntaxError):
        lines = _syntax_error_lines(exc, stype)
    else:
        try:
            s = str(exc)
        except BaseException:
            s = "<exception str() failed>"
        s = _suggestion_text(exc, s)
        lines = [(stype + ": " + s if s else stype) + "\n"]
    d = _cetta_object_dict(exc)
    notes = d.get("__notes__") if d is not None else None
    if isinstance(notes, (list, tuple)):
        for note in notes:
            for l in str(note).split("\n"):
                lines.append(l + "\n")
    elif notes is not None:
        lines.append(repr(notes) + "\n")
    return "".join(lines)


def _te_format(te, ctx, out):
    output = []
    t = te
    while t is not None:
        e = t.exc
        if t.cause is not None:
            output.append(("\nThe above exception was the direct cause of the following exception:\n\n", t))
            t = t.cause
        elif t.context is not None and not e.__suppress_context__:
            output.append(("\nDuring handling of the above exception, another exception occurred:\n\n", t))
            t = t.context
        else:
            output.append((None, t))
            t = None
    for msg, t in reversed(output):
        if msg is not None:
            ctx.emit(out, msg)
        e = t.exc
        tb = e.__traceback__
        if t.exceptions is None:
            if tb is not None:
                ctx.emit(out, "Traceback (most recent call last):\n")
                ctx.emit(out, _stack_text(tb))
            ctx.emit(out, _exception_only(e))
        elif ctx.depth > 10:
            ctx.emit(out, "... (max_group_depth is 10)\n")
        else:
            toplevel = ctx.depth == 0
            if toplevel:
                ctx.depth += 1
            if tb is not None:
                ctx.emit(out, "Exception Group Traceback (most recent call last):\n", "+" if toplevel else "|")
                ctx.emit(out, _stack_text(tb))
            ctx.emit(out, _exception_only(e))
            num = len(t.exceptions)
            n = num if num <= 15 else 16
            ctx.need_close = False
            for i in range(n):
                last = i == n - 1
                if last:
                    ctx.need_close = True
                truncated = i >= 15
                title = str(i + 1) if not truncated else "..."
                out.append(ctx.indent() + ("+-" if i == 0 else "  ") + "+---------------- " + title + " ----------------\n")
                ctx.depth += 1
                if not truncated:
                    _te_format(t.exceptions[i], ctx, out)
                else:
                    remaining = num - 15
                    ctx.emit(out, "and " + str(remaining) + " more exception" + ("s" if remaining > 1 else "") + "\n")
                if last and ctx.need_close:
                    out.append(ctx.indent() + "+------------------------------------\n")
                    ctx.need_close = False
                ctx.depth -= 1
            if toplevel:
                ctx.depth = 0


_RECURSIVE_CUTOFF = 3


def _stack_text(tb):
    # StackSummary.format: each entry's file, line and function, then the
    # line's text stripped; a run of more than three identical entries (a
    # recursion) cut, "[Previous line repeated N more times]"
    result = []
    last = None
    count = 0
    while tb is not None:
        fr = _cetta_field(tb, "frame")
        filename = _cetta_field(fr, "filename")
        lineno = _cetta_field(tb, "lineno")
        name = _cetta_field(fr, "name")
        key = (filename, lineno, name)
        if key != last:
            if count > _RECURSIVE_CUTOFF:
                result.append(_repeated_text(count - _RECURSIVE_CUTOFF))
            last = key
            count = 0
        count += 1
        if count <= _RECURSIVE_CUTOFF:
            result.append('  File "' + filename + '", line ' + str(lineno) + ", in " + name + "\n")
            line = _getline(filename, lineno) if type(lineno) is int else ""
            if line and line.strip():
                result.append("    " + line.strip() + "\n")
        tb = _cetta_field(tb, "next")
    if count > _RECURSIVE_CUTOFF:
        result.append(_repeated_text(count - _RECURSIVE_CUTOFF))
    return "".join(result)


def _repeated_text(count):
    return "  [Previous line repeated " + str(count) + " more time" + ("s" if count > 1 else "") + "]\n"


def _report_text(exc):
    # one exception, from nothing and in no group: its traceback and its line
    if (exc.__cause__ is None and (exc.__context__ is None or exc.__suppress_context__)
            and not isinstance(exc, BaseExceptionGroup)):
        tb = exc.__traceback__
        head = "Traceback (most recent call last):\n" + _stack_text(tb) if tb is not None else ""
        return head + _exception_only(exc)
    out = []
    _te_format(_te_tree(exc), _PrintContext(), out)
    return "".join(out)


# ---------------------------------------------------------------- io: text streams

# Modules/_io's text layer over three ends: a file, whose text is read whole
# when it is opened and written back whole when it is flushed (the fs library
# reads and writes whole files); the process's standard streams; memory
# (StringIO).  A position is the UTF-8 byte offset of the text before it (the
# cookie CPython gives a UTF-8 text).  Open files are flushed at exit.

_ERRNO_OF_TEXT = {"Operation not permitted": 1, "No such file or directory": 2, "Input/output error": 5,
                  "Permission denied": 13, "Device or resource busy": 16, "File exists": 17,
                  "Not a directory": 20, "Is a directory": 21, "Invalid argument": 22,
                  "Too many open files": 24, "Text file busy": 26, "File too large": 27,
                  "No space left on device": 28, "Read-only file system": 30,
                  "File name too long": 36, "Too many levels of symbolic links": 40}


def _fs_error(why, filename):
    # the fs library's refusal ("cannot open file: No such file or
    # directory") as the OSError its errno names
    strerror = why.rpartition(": ")[2]
    n = _ERRNO_OF_TEXT.get(strerror)
    if n is None:
        return OSError(strerror)
    return OSError(n, strerror, filename)


def _fs_do(result, filename):
    if result[0] == "error":
        raise _fs_error(result[1], filename)
    return result[1]


class _UnsupportedOperation(OSError, ValueError):
    pass


_UnsupportedOperation.__name__ = "UnsupportedOperation"
_UnsupportedOperation.__qualname__ = "UnsupportedOperation"
_UnsupportedOperation.__module__ = "io"
_open_files = []


def _line_end(t, i, newline):
    # the end of the line that starts at I, past its terminator: "\n" for a
    # translated text, any of "\r\n", "\r", "\n" for newline "", else NEWLINE
    n = len(t)
    if newline is None or newline == "\n":
        j = t.find("\n", i)
        return n if j < 0 else j + 1
    if newline == "":
        a = t.find("\n", i)
        b = t.find("\r", i)
        if b < 0 or (a >= 0 and a < b):
            return n if a < 0 else a + 1
        return b + 2 if b + 1 < n and t[b + 1] == "\n" else b + 1
    j = t.find(newline, i)
    return n if j < 0 else j + len(newline)


def _char_utf8_len(c):
    o = ord(c)
    return 1 if o < 0x80 else 2 if o < 0x800 else 3 if o < 0x10000 else 4


class _TextStream:
    # a text stream's state, its methods those of TextIOWrapper and StringIO

    def _state(self):
        if _cetta_field(self, "closed") is True:
            raise ValueError("I/O operation on closed file.")

    def _text(self):
        # the text, its pending writes joined
        t = _cetta_field(self, "text")
        pending = _cetta_field(self, "pending")
        if pending:
            t = t + "".join(pending)
            _cetta_set_field(self, "text", t)
            _cetta_set_field(self, "pending", [])
        return t

    def _need(self, flag, what):
        self._state()
        if _cetta_field(self, flag) is not True:
            raise _UnsupportedOperation(what)

    def readable(self):
        self._state()
        return _cetta_field(self, "readable") is True

    def writable(self):
        self._state()
        return _cetta_field(self, "writable") is True

    def seekable(self):
        self._state()
        return _cetta_field(self, "end") != "std"

    def isatty(self):
        self._state()
        return False

    @property
    def closed(self):
        return _cetta_field(self, "closed") is True

    def read(self, size=-1, /):
        self._need("readable", "not readable")
        if size is not None and not isinstance(size, int):
            raise TypeError("argument should be integer or None, not '" + _type_name(type(size)) + "'")
        if _cetta_field(self, "end") == "std":
            if size is None or size < 0:
                out = []
                while True:
                    line = _cetta_read_line()
                    if line == "":
                        return "".join(out)
                    out.append(line)
            return _stdin_read(size)
        t = self._text()
        i = _cetta_field(self, "pos")
        j = len(t) if size is None or size < 0 else min(len(t), i + size)
        _cetta_set_field(self, "pos", j)
        return t[i:j]

    def readline(self, size=-1, /):
        self._need("readable", "not readable")
        if size is not None and not isinstance(size, int):
            raise TypeError("argument should be integer or None, not '" + _type_name(type(size)) + "'")
        if _cetta_field(self, "end") == "std":
            return _cetta_read_line()
        t = self._text()
        i = _cetta_field(self, "pos")
        j = _line_end(t, i, _cetta_field(self, "newline"))
        if size is not None and size >= 0 and j - i > size:
            j = i + size
        _cetta_set_field(self, "pos", j)
        return t[i:j]

    def readlines(self, hint=-1, /):
        lines = []
        total = 0
        while True:
            line = self.readline()
            if line == "":
                return lines
            lines.append(line)
            total += len(line)
            if hint is not None and hint > 0 and total >= hint:
                return lines

    def __iter__(self):
        self._state()
        return self

    def __next__(self):
        line = self.readline()
        if line == "":
            raise StopIteration
        return line

    def write(self, s, /):
        self._need("writable", "not writable")
        if not isinstance(s, str):
            raise TypeError("write() argument must be str, not " + _type_name(type(s)))
        s = _cetta_base(s)
        n = len(s)
        newline = _cetta_field(self, "newline")
        if newline == "\r" or newline == "\r\n":
            s = s.replace("\n", newline)
        if _cetta_field(self, "end") == "std":
            _cetta_std_write(_cetta_field(self, "stream"), s)
            return n
        size = _cetta_field(self, "size")
        pos = _cetta_field(self, "pos")
        if _cetta_field(self, "append") is True or pos == size:
            # at the end: kept aside, joined when the text is next read
            _cetta_field(self, "pending").append(s)
            size += len(s)
            _cetta_set_field(self, "size", size)
            _cetta_set_field(self, "pos", size)
        else:
            t = self._text()
            t = t[:pos] + s + t[pos + len(s):]
            _cetta_set_field(self, "text", t)
            _cetta_set_field(self, "size", len(t))
            _cetta_set_field(self, "pos", pos + len(s))
        _cetta_set_field(self, "dirty", True)
        return n

    def writelines(self, lines, /):
        self._state()
        for line in lines:
            self.write(line)

    def flush(self):
        self._state()
        if _cetta_field(self, "end") == "file" and _cetta_field(self, "dirty") is True:
            path = _cetta_field(self, "path")
            t = self._text()
            if _cetta_field(self, "append") is True:
                done = _cetta_field(self, "written")
                _fs_do(_cetta_fs_append(path, t[done:]), _cetta_field(self, "name"))
                _cetta_set_field(self, "written", len(t))
            else:
                _fs_do(_cetta_fs_write(path, t), _cetta_field(self, "name"))
            _cetta_set_field(self, "dirty", False)

    def close(self):
        if _cetta_field(self, "closed") is True:
            return None
        try:
            self.flush()
        finally:
            _cetta_set_field(self, "closed", True)
            if self in _open_files:
                _open_files.remove(self)
        return None

    def __enter__(self):
        self._state()
        return self

    def __exit__(self, *args):
        self.close()
        return None

    def tell(self):
        self._state()
        if _cetta_field(self, "end") == "std":
            raise _UnsupportedOperation("underlying stream is not seekable")
        t = self._text()
        return _cetta_str_byte_length(t[:_cetta_field(self, "pos")])

    def seek(self, cookie, whence=0, /):
        self._state()
        if _cetta_field(self, "end") == "std":
            raise _UnsupportedOperation("underlying stream is not seekable")
        cookie = _index(cookie)
        t = self._text()
        if whence == 1:
            if cookie != 0:
                raise _UnsupportedOperation("can't do nonzero cur-relative seeks")
            return self.tell()
        if whence == 2:
            if cookie != 0:
                raise _UnsupportedOperation("can't do nonzero end-relative seeks")
            _cetta_set_field(self, "pos", len(t))
            return _cetta_str_byte_length(t)
        if whence != 0:
            raise ValueError("invalid whence (" + str(whence) + ", should be 0, 1 or 2)")
        if cookie < 0:
            raise ValueError("negative seek position " + str(cookie))
        i = 0
        b = 0
        while i < len(t) and b < cookie:
            b += _char_utf8_len(t[i])
            i += 1
        _cetta_set_field(self, "pos", i)
        return cookie

    def truncate(self, size=None, /):
        self._need("writable", "not writable")
        t = self._text()
        pos = _cetta_field(self, "pos")
        size = pos if size is None else _index(size)
        if size < 0:
            raise ValueError("negative size value " + str(size))
        _cetta_set_field(self, "text", t[:size])
        _cetta_set_field(self, "size", min(size, len(t)))
        _cetta_set_field(self, "dirty", True)
        return size

    def fileno(self):
        self._state()
        raise _UnsupportedOperation("fileno")


class _TextIOWrapper(_TextStream):
    # _io.TextIOWrapper: a file, or a standard stream

    def __repr__(self):
        return ("<_io.TextIOWrapper name=" + repr(_cetta_field(self, "name")) + " mode="
                + repr(_cetta_field(self, "mode")) + " encoding=" + repr(_cetta_field(self, "encoding")) + ">")

    @property
    def name(self):
        return _cetta_field(self, "name")

    @property
    def mode(self):
        return _cetta_field(self, "mode")

    @property
    def encoding(self):
        return _cetta_field(self, "encoding")

    @property
    def errors(self):
        return "strict"

    @property
    def newlines(self):
        return None

    @property
    def line_buffering(self):
        return _cetta_field(self, "end") == "std"


class _StringIO(_TextStream):
    # _io.StringIO: a text in memory, written where the position is

    def __init__(self, initial_value="", newline="\n"):
        if initial_value is None:
            initial_value = ""
        if not isinstance(initial_value, str):
            raise TypeError("initial_value must be str or None, not " + _type_name(type(initial_value)))
        if newline is not None and not isinstance(newline, str):
            raise TypeError("newline must be str or None, not " + _type_name(type(newline)))
        if newline not in (None, "", "\n", "\r", "\r\n"):
            raise ValueError("illegal newline value: " + repr(newline))
        text = _cetta_base(initial_value)
        if newline is None:
            text = text.replace("\r\n", "\n").replace("\r", "\n")
        elif newline == "\r" or newline == "\r\n":
            text = text.replace("\n", newline)
        _init_stream(self, "memory", None, None, None, newline, True, True, False, text, None)

    def __repr__(self):
        return "<_io.StringIO object at " + _cetta_address(self) + ">"

    def getvalue(self):
        self._state()
        return self._text()

    @property
    def line_buffering(self):
        return False

    @property
    def newlines(self):
        return None


_TextIOWrapper.__name__ = 'TextIOWrapper'
_TextIOWrapper.__qualname__ = 'TextIOWrapper'
_TextIOWrapper.__module__ = "_io"
_StringIO.__name__ = 'StringIO'
_StringIO.__qualname__ = 'StringIO'
_StringIO.__module__ = "_io"


def _init_stream(f, end, name, mode, encoding, newline, readable, writable, append, text, path):
    for k, v in (("end", end), ("name", name), ("mode", mode), ("encoding", encoding),
                 ("newline", newline), ("readable", readable), ("writable", writable),
                 ("append", append), ("text", text), ("path", path), ("pos", 0), ("pending", []),
                 ("size", len(text) if text is not None else 0),
                 ("dirty", False), ("closed", False), ("written", len(text) if text is not None else 0)):
        _cetta_set_field(f, k, v)
    return f


def _new_stream(end, name, mode, encoding, newline, readable, writable, append, text, path):
    return _init_stream(_cetta_new_object(_TextIOWrapper), end, name, mode, encoding, newline,
                        readable, writable, append, text, path)


_UTF8_NAMES = ("utf-8", "utf8", "u8", "utf", "cp65001", "locale")


def open(file, mode="r", buffering=-1, encoding=None, errors=None, newline=None, closefd=True, opener=None):
    # _io_open_impl for text: the mode's letters, then the file's text
    if hasattr(file, "__fspath__") and not isinstance(file, (str, bytes)):
        file = file.__fspath__()
    if not isinstance(file, str):
        if isinstance(file, (int, bytes)):
            raise NotImplementedError("open() of a file descriptor or a bytes path")
        raise TypeError("expected str, bytes or os.PathLike object, not " + _type_name(type(file)))
    if not isinstance(mode, str):
        raise TypeError("open() argument 'mode' must be str, not " + _type_name(type(mode)))
    for i in range(len(mode)):
        c = mode[i]
        if c not in "xrwa+tb" or c in mode[i + 1:]:
            raise ValueError("invalid mode: '" + mode + "'")
    creating, reading, writing, appending = "x" in mode, "r" in mode, "w" in mode, "a" in mode
    updating, text_mode, binary = "+" in mode, "t" in mode, "b" in mode
    if text_mode and binary:
        raise ValueError("can't have text and binary mode at once")
    if creating + reading + writing + appending > 1:
        raise ValueError("must have exactly one of create/read/write/append mode")
    if binary and encoding is not None:
        raise ValueError("binary mode doesn't take an encoding argument")
    if binary and errors is not None:
        raise ValueError("binary mode doesn't take an errors argument")
    if binary and newline is not None:
        raise ValueError("binary mode doesn't take a newline argument")
    if creating + reading + writing + appending == 0:
        raise ValueError("Must have exactly one of create/read/write/append mode and at most one plus")
    if binary:
        raise NotImplementedError("open() in binary mode (bytes are not a runtime type yet)")
    if newline is not None and not isinstance(newline, str):
        raise TypeError("TextIOWrapper() argument 'newline' must be str or None, not " + _type_name(type(newline)))
    if newline not in (None, "", "\n", "\r", "\r\n"):
        raise ValueError("illegal newline value: " + newline)
    if encoding is not None:
        if not isinstance(encoding, str):
            raise TypeError("TextIOWrapper() argument 'encoding' must be str or None, not " + _type_name(type(encoding)))
        if encoding.lower().replace("_", "-") not in _UTF8_NAMES:
            raise NotImplementedError("open() with encoding " + repr(encoding) + " (only UTF-8 so far)")
    path = file
    text = ""
    if creating:
        if _cetta_fs_exists(path):
            raise OSError(17, "File exists", file)
        _fs_do(_cetta_fs_write(path, ""), file)
    elif writing:
        _fs_do(_cetta_fs_write(path, ""), file)
    elif appending:
        _fs_do(_cetta_fs_append(path, ""), file)
        if updating:
            text = _fs_do(_cetta_fs_read(path), file)
    if reading or (appending and updating):
        if reading:
            text = _fs_do(_cetta_fs_read(path), file)
        if newline is None:
            text = text.replace("\r\n", "\n").replace("\r", "\n")
    f = _new_stream("file", file, mode, encoding if encoding is not None else "utf-8", newline,
                    reading or updating, writing or appending or creating or updating, appending, text, path)
    if appending:
        _cetta_set_field(f, "pos", len(text))
    if writing or appending or creating or updating:
        _open_files.append(f)
    return f


def _stdin_read(size):
    # read(size) of the standard input: lines until SIZE characters
    out = []
    n = 0
    while n < size:
        line = _cetta_read_line()
        if line == "":
            break
        out.append(line)
        n += len(line)
    return "".join(out)


def input(prompt=None, /):
    # builtin_input_impl: the prompt written to stdout, then a line of
    # stdin without its newline; EOFError at the end of the input
    stdin = _sys.stdin
    if prompt is not None:
        _sys.stdout.write(str(prompt))
        _sys.stdout.flush()
    line = stdin.readline()
    if line == "":
        raise EOFError("EOF when reading a line")
    if line[-1:] == "\n":
        line = line[:-1]
    return line


def _print_to(file, strs, sep, end, flush):
    # builtin_print's writes to a stream: each text, SEP between them, END
    first = True
    for s in strs:
        if not first:
            file.write(sep)
        file.write(s)
        first = False
    file.write(end)
    if flush:
        file.flush()
    return None


def _at_exit():
    # the open files flushed as the interpreter ends (the io objects'
    # finalization)
    for f in list(_open_files):
        try:
            f.flush()
        except BaseException:
            pass
    return None


_stdin = _new_stream("std", "<stdin>", "r", "utf-8", None, True, False, False, None, None)
_cetta_set_field(_stdin, "stream", "stdin")
_stdout = _new_stream("std", "<stdout>", "w", "utf-8", None, False, True, False, None, None)
_cetta_set_field(_stdout, "stream", "stdout")
_stderr = _new_stream("std", "<stderr>", "w", "utf-8", None, False, True, False, None, None)
_cetta_set_field(_stderr, "stream", "stderr")
_sys = _sys_modules["sys"]
for _name, _f in (("stdin", _stdin), ("stdout", _stdout), ("stderr", _stderr),
                  ("__stdin__", _stdin), ("__stdout__", _stdout), ("__stderr__", _stderr)):
    _cetta_object_dict(_sys)[_name] = _f


def _make_io():
    # the io module and _io (Lib/io.py over Modules/_io): the text layer
    m = _new_module("io")
    d = _cetta_object_dict(m)
    for k, v in (("open", open), ("StringIO", _StringIO), ("TextIOWrapper", _TextIOWrapper),
                 ("UnsupportedOperation", _UnsupportedOperation), ("SEEK_SET", 0), ("SEEK_CUR", 1),
                 ("SEEK_END", 2), ("DEFAULT_BUFFER_SIZE", 8192)):
        d[k] = v
    _cetta_set_field(m, "builtin", True)
    _sys_modules["io"] = m
    _sys_modules["_io"] = m
    return m


_make_io()


# (a class is made once type.__call__ is in place: here, at the end)
class _Quitter:
    # site.Quitter: exit() and quit()
    def __init__(self, name, eof):
        self.name = name
        self.eof = eof

    def __repr__(self):
        return "Use " + self.name + "() or " + self.eof + " to exit"

    def __call__(self, code=None):
        raise SystemExit(code)


# site's exit() and quit() (Lib/site.py, setquit)
exit = _Quitter("exit", "Ctrl-D (i.e. EOF)")
quit = _Quitter("quit", "Ctrl-D (i.e. EOF)")


# ---------------------------------------------------------------- template strings

# Objects/templateobject.c and interpolationobject.c (PEP 750): a template
# holds its strings and its interpolations, two tuples; an interpolation its
# value, the text of its expression, its conversion and its format spec.
# Neither type may be subclassed, and their members are read-only
_template = _cetta_builtin_type("Template")
_interpolation = _cetta_builtin_type("Interpolation")
_template_iterator = _cetta_builtin_type("TemplateIter")


def _template_build(strings, interpolations):
    # _PyTemplate_Build (BUILD_TEMPLATE)
    t = _cetta_new_object(_template)
    _cetta_set_field(t, "strings", strings)
    _cetta_set_field(t, "interpolations", interpolations)
    return t


def _interpolation_build(value, expression, conversion, format_spec):
    # _PyInterpolation_Build (BUILD_INTERPOLATION)
    i = _cetta_new_object(_interpolation)
    _cetta_set_field(i, "value", value)
    _cetta_set_field(i, "expression", expression)
    _cetta_set_field(i, "conversion", conversion)
    _cetta_set_field(i, "format_spec", format_spec)
    return i


def _template_new(cls, *args, **kwargs):
    # template_new: strings and interpolations in any order, each checked
    # first; adjacent strings joined, and an empty string between two
    # interpolations and at either end next to one
    if kwargs:
        raise TypeError("Template.__new__ only accepts *args arguments")
    for item in args:
        if not issubclass(type(item), str) and type(item) is not _interpolation:
            raise TypeError("Template.__new__ *args need to be of type 'str' or 'Interpolation', got "
                            + _fqn(type(item)))
    strings = []
    interpolations = []
    last_was_str = False
    for item in args:
        if type(item) is _interpolation:
            if not last_was_str:
                strings.append("")
            interpolations.append(item)
            last_was_str = False
        else:
            if last_was_str:
                strings[-1] = "".join((strings[-1], item))
            else:
                strings.append(item)
            last_was_str = True
    if not last_was_str:
        strings.append("")
    return _template_build(tuple(strings), tuple(interpolations))


def _template_repr(self):
    return (_cetta_type_field(type(self), "name") + "(strings=" + repr(_cetta_field(self, "strings"))
            + ", interpolations=" + repr(_cetta_field(self, "interpolations")) + ")")


def _template_iter(self):
    # template_iter: the strings and the interpolations taken in turn
    it = _cetta_new_object(_template_iterator)
    _cetta_set_field(it, "strings", iter(_cetta_field(self, "strings")))
    _cetta_set_field(it, "interpolations", iter(_cetta_field(self, "interpolations")))
    _cetta_set_field(it, "from_strings", True)
    return it


def _template_iter_next(self):
    # templateiter_next: a string (an empty one passed over for the
    # interpolation after it), then an interpolation, in turn
    if _cetta_field(self, "from_strings"):
        item = next(_cetta_field(self, "strings"), _NULL)
        _cetta_set_field(self, "from_strings", False)
        if item is not _NULL and len(item) == 0:
            item = next(_cetta_field(self, "interpolations"), _NULL)
            _cetta_set_field(self, "from_strings", True)
    else:
        item = next(_cetta_field(self, "interpolations"), _NULL)
        _cetta_set_field(self, "from_strings", True)
    if item is _NULL:
        raise StopIteration
    return item


def _template_add(self, other):
    # _PyTemplate_Concat (the sequence slot sq_concat, tried after both
    # operands' number slots): two templates, the left's last string joined
    # to the right's first
    if type(self) is _template and type(other) is _template:
        left = _cetta_field(self, "strings")
        right = _cetta_field(other, "strings")
        strings = left[:-1] + ("".join((left[-1], right[0])),) + right[1:]
        return _template_build(strings, _cetta_field(self, "interpolations") + _cetta_field(other, "interpolations"))
    raise TypeError('can only concatenate string.templatelib.Template (not "' + _fqn(type(other))
                    + '") to string.templatelib.Template')


def _template_values(self):
    # template_values_get: the interpolations' values
    return tuple([_cetta_field(i, "value") for i in _cetta_field(self, "interpolations")])


def _template_reduce(self):
    # template_reduce: string.templatelib's _template_unpickle of the tuples
    unpickle = getattr(_gcd_import("string.templatelib"), "_template_unpickle")
    return (unpickle, (_cetta_field(self, "strings"), _cetta_field(self, "interpolations")))


def _interpolation_new(cls, *args, **kwargs):
    # interpolation_new(value, expression="", conversion=None, format_spec=""),
    # read as Argument Clinic reads it, each argument checked in turn
    value, expression, conversion, format_spec = _clinic_args(
        "Interpolation", ("value", "expression", "conversion", "format_spec"), 1, 4, args, kwargs)
    if expression is _NULL:
        expression = ""
    elif not issubclass(type(expression), str):
        raise TypeError("Interpolation() argument 'expression' must be str, not " + _arg_type_name(expression))
    if conversion is _NULL:
        conversion = None
    elif conversion is not None:
        if not issubclass(type(conversion), str):
            raise TypeError("Interpolation() argument 'conversion' must be str, not " + _fqn(type(conversion)))
        if len(conversion) != 1 or conversion not in "asr":
            raise ValueError("Interpolation() argument 'conversion' must be one of 's', 'a' or 'r'")
    if format_spec is _NULL:
        format_spec = ""
    elif not issubclass(type(format_spec), str):
        raise TypeError("Interpolation() argument 'format_spec' must be str, not " + _arg_type_name(format_spec))
    i = _cetta_new_object(cls)
    _cetta_set_field(i, "value", value)
    _cetta_set_field(i, "expression", expression)
    _cetta_set_field(i, "conversion", conversion)
    _cetta_set_field(i, "format_spec", format_spec)
    return i


def _arg_type_name(arg):
    # _PyArg_BadArgument's name for the argument given: None, else its tp_name
    return "None" if arg is None else _type_name(type(arg))


def _interpolation_repr(self):
    return (_cetta_type_field(type(self), "name") + "(" + repr(_cetta_field(self, "value")) + ", "
            + repr(_cetta_field(self, "expression")) + ", " + repr(_cetta_field(self, "conversion")) + ", "
            + repr(_cetta_field(self, "format_spec")) + ")")


def _interpolation_reduce(self):
    return (type(self), (_cetta_field(self, "value"), _cetta_field(self, "expression"),
                         _cetta_field(self, "conversion"), _cetta_field(self, "format_spec")))


def _self_iter(self):
    return self


for _name, _f in (("__new__", staticmethod(_template_new)), ("__repr__", _template_repr),
                  ("__iter__", _template_iter), ("__add__", _template_add),
                  ("__reduce__", _template_reduce), ("__class_getitem__", classmethod(_generic_alias))):
    _cetta_set_type_attr(_template, _name, _f)
_member(_template, "strings", "strings")
_member(_template, "interpolations", "interpolations")
_computed(_template, "values", _template_values)
for _name, _f in (("__new__", staticmethod(_interpolation_new)), ("__repr__", _interpolation_repr),
                  ("__reduce__", _interpolation_reduce), ("__class_getitem__", classmethod(_generic_alias))):
    _cetta_set_type_attr(_interpolation, _name, _f)
for _name in ("value", "expression", "conversion", "format_spec"):
    _member(_interpolation, _name, _name)
_cetta_set_type_attr(_interpolation, "__match_args__", ("value", "expression", "conversion", "format_spec"))
_cetta_set_type_attr(_template_iterator, "__iter__", _self_iter)
_cetta_set_type_attr(_template_iterator, "__next__", _template_iter_next)


# ---------------------------------------------------------------- types.SimpleNamespace

# Objects/namespaceobject.c: a namespace's attributes are its instance
# dict (ns_dict, read directly, whatever __dict__ a subclass defines), set
# from one optional mapping or iterable of pairs and from keywords
_namespace = _cetta_builtin_type("SimpleNamespace")


def _namespace_new(cls, *args, **kwargs):
    # namespace_new: the object and its (empty) dict; the arguments are
    # __init__'s
    ns = _cetta_new_object(cls)
    _cetta_object_dict(ns)
    return ns


def _namespace_init(self, *args, **kwargs):
    # namespace_init: PyArg_UnpackTuple under the type's name, a dict made
    # of the argument (dict(arg) unless it is exactly a dict), its keys
    # strings (PyArg_ValidateKeywordArguments)
    if len(args) > 1:
        raise TypeError(type(self).__name__ + " expected at most 1 argument, got " + str(len(args)))
    d = _cetta_object_dict(self)
    if args:
        arg = args[0]
        items = arg if type(arg) is dict else dict(arg)
        for k in items:
            if not isinstance(k, str):
                raise TypeError("keywords must be strings")
        d.update(items)
    d.update(kwargs)


def _namespace_repr(self):
    # namespace_repr: "namespace" for the type itself, else the type's name;
    # each non-empty str key as key=repr(value); "(...)" inside its own repr
    name = "namespace" if type(self) is _namespace else type(self).__name__
    if _cetta_repr_enter(self):
        return name + "(...)"
    try:
        parts = []
        for k, v in _cetta_object_dict(self).items():
            if isinstance(k, str) and k:
                parts.append(str.__str__(k) + "=" + repr(v))
        return name + "(" + ", ".join(parts) + ")"
    finally:
        _cetta_repr_leave(self)


def _namespace_compare(op):
    # namespace_richcompare: two namespaces are equal or unequal as their
    # dicts are; no other comparison is theirs
    def compare(self, other):
        if op is not None and isinstance(self, _namespace) and isinstance(other, _namespace):
            return op(_cetta_object_dict(self), _cetta_object_dict(other))
        return NotImplemented
    return compare


def _namespace_reduce(self):
    return (type(self), (), _cetta_object_dict(self))


def _namespace_replace(self, *args, **kwargs):
    # namespace_replace: a new namespace of the same type, its dict this
    # one's updated with the keywords
    if args:
        raise TypeError("__replace__() takes no positional arguments")
    result = type(self)()
    d = _cetta_object_dict(result)
    d.update(_cetta_object_dict(self))
    d.update(kwargs)
    return result


for _name, _f in (("__new__", staticmethod(_namespace_new)), ("__init__", _namespace_init), ("__repr__", _namespace_repr),
                  ("__lt__", _namespace_compare(None)), ("__le__", _namespace_compare(None)),
                  ("__eq__", _namespace_compare(lambda a, b: a == b)),
                  ("__ne__", _namespace_compare(lambda a, b: a != b)),
                  ("__gt__", _namespace_compare(None)), ("__ge__", _namespace_compare(None)),
                  ("__reduce__", _namespace_reduce), ("__replace__", _namespace_replace)):
    _cetta_set_type_attr(_namespace, _name, _f)
# its __dict__ a read-only member over the dict (namespace_members: ns_dict)
_member(_namespace, "__dict__", "dict")


# ---------------------------------------------------------------- _string

# Objects/unicodeobject.c's _string module, made when first imported: the
# parser of str.format (Objects/stringlib/unicode_format.h) for
# string.Formatter, its parts read one at a time as the format reads them
_formatter_iterator = _cetta_builtin_type("formatteriterator")
_field_name_iterator = _cetta_builtin_type("fieldnameiterator")


def _formatter_parser(s, /):
    # formatter_parser: an iterator over the format string's parts
    if not issubclass(type(s), str):
        raise TypeError("expected str, got " + _type_name(type(s)))
    it = _cetta_new_object(_formatter_iterator)
    _cetta_set_field(it, "str", s)
    _cetta_set_field(it, "pos", 0)
    return it


def _formatter_iter_next(self):
    # formatteriter_next: (literal, field name, format spec, conversion),
    # the last three None after literal text alone, the conversion None when
    # the field has none
    s = _cetta_field(self, "str")
    pos = _cetta_field(self, "pos")
    end = len(s)
    if pos >= end:
        raise StopIteration
    literal, field, pos = _markup_next(s, pos, end)
    _cetta_set_field(self, "pos", pos)
    if field is None:
        return (literal, None, None, None)
    name, conversion, spec, expand = field
    return (literal, name, spec, None if conversion == "" else conversion)


def _formatter_field_name_split(s, /):
    # formatter_field_name_split: the field name's first part (an int when
    # its digits say so) and an iterator over the rest
    if not issubclass(type(s), str):
        raise TypeError("expected str, got " + _type_name(type(s)))
    first, i = _field_name_first(s)
    k = _field_index(first)
    it = _cetta_new_object(_field_name_iterator)
    _cetta_set_field(it, "str", s)
    _cetta_set_field(it, "pos", i)
    return (first if k == -1 else k, it)


def _field_name_iter_next(self):
    # fieldnameiter_next: (True, attribute) or (False, key), in turn
    s = _cetta_field(self, "str")
    pos = _cetta_field(self, "pos")
    n = len(s)
    if pos >= n:
        raise StopIteration
    is_attr, key, pos = _field_name_next(s, pos, n)
    _cetta_set_field(self, "pos", pos)
    return (is_attr, key)


for _t, _f in ((_formatter_iterator, _formatter_iter_next), (_field_name_iterator, _field_name_iter_next)):
    _cetta_set_type_attr(_t, "__iter__", _self_iter)
    _cetta_set_type_attr(_t, "__next__", _f)


def _make_types_module():
    # _types (Modules/_typesmodule.c): the type objects types.py names
    # (CapsuleType's instances come only from C extensions: none here); not
    # here: FrameLocalsProxyType (a frame's f_locals is a snapshot dict, not
    # PEP 667's proxy)
    m = _new_module("_types")
    _cetta_set_field(m, "builtin", True)
    d = _cetta_object_dict(m)
    d["__doc__"] = "Define names for built-in types."
    for name, tname in (("AsyncGeneratorType", "async_generator"), ("BuiltinFunctionType", "builtin_function_or_method"),
                        ("CapsuleType", "PyCapsule"),
                        ("BuiltinMethodType", "builtin_function_or_method"), ("CellType", "cell"),
                        ("ClassMethodDescriptorType", "classmethod_descriptor"), ("CodeType", "code"),
                        ("CoroutineType", "coroutine"), ("EllipsisType", "ellipsis"), ("FrameType", "frame"),
                        ("FunctionType", "function"), ("GeneratorType", "generator"), ("GenericAlias", "GenericAlias"),
                        ("GetSetDescriptorType", "getset_descriptor"), ("LambdaType", "function"),
                        ("LazyImportType", "lazy_import"), ("MappingProxyType", "mappingproxy"),
                        ("MemberDescriptorType", "member_descriptor"), ("MethodDescriptorType", "method_descriptor"),
                        ("MethodType", "method"), ("MethodWrapperType", "method-wrapper"), ("ModuleType", "module"),
                        ("NoneType", "NoneType"), ("NotImplementedType", "NotImplementedType"),
                        ("SimpleNamespace", "SimpleNamespace"), ("TracebackType", "traceback"),
                        ("UnionType", "Union"), ("WrapperDescriptorType", "wrapper_descriptor")):
        d[name] = _cetta_builtin_type(tname)
    return m


def _store_name(ns, name, value):
    # STORE_NAME into a namespace that is not exactly a dict: PyObject_SetItem
    ns[name] = value


def _delete_name(ns, name):
    # DELETE_NAME from such a namespace: PyObject_DelItem, any failure
    # replaced by the NameError (_PyEval_FormatExcCheckArg; _PyErr_FormatV
    # clears the failure first, so it is not the NameError's context)
    try:
        del ns[name]
        return
    except BaseException:
        pass
    raise NameError("name '" + name + "' is not defined", name=name)


def _weakref_type_info(t):
    # whether the instances of T accept weak references (CPython's
    # tp_weaklistoffset, read by the held _weakref)
    return bool(_cetta_type_field(t, "has_weakref"))


def _make_string_module():
    m = _new_module("_string")
    _cetta_set_field(m, "builtin", True)
    d = _cetta_object_dict(m)
    d["__doc__"] = "string helper module"
    d["formatter_field_name_split"] = _formatter_field_name_split
    d["formatter_parser"] = _formatter_parser
    return m


# the builtin modules the runtime makes when they are first imported (the
# others it makes at its start: builtins, sys, _io, _warnings)
_BUILTIN_MODULE_MAKERS = {"_string": _make_string_module, "_typing": _make_typing_module, "_types": _make_types_module}


# the builtin types' members written in C, after the prelude's own
_cetta_install_builtin_members()



# ---------------------------------------------------------------- warnings

# Python/_warnings.c: the filters CPython starts with, the actions, the
# registries a warning is remembered in, and a warning shown through the
# warnings module (imported when first needed) or written directly
_warnings_module = _new_module("_warnings")
_cetta_set_field(_warnings_module, "builtin", True)
_sys_modules["_warnings"] = _warnings_module
_warnings_version = [0]


def _warnings_filters_mutated_lock_held():
    _warnings_version[0] += 1


def _warnings_acquire_lock():
    return None


def _warnings_release_lock():
    return None


def _warnings_attr(name, default):
    # get_warnings_attr: the warnings module's attribute once it is imported
    w = _sys_modules.get("warnings")
    if w is None:
        return default
    return getattr(w, name, default)


def _warnings_check_matched(obj, arg):
    if obj is None:
        return True
    if type(obj) is str:
        return obj == arg
    return bool(obj.match(arg))


def _warnings_get_filter(category, text, lineno, module):
    filters = _warnings_attr("filters", _cetta_object_dict(_warnings_module)["filters"])
    if not isinstance(filters, list):
        raise ValueError("warnings.filters must be a list")
    for i in range(len(filters)):
        item = filters[i]
        if not isinstance(item, tuple) or len(item) != 5:
            raise ValueError("warnings.filters item " + str(i) + " isn't a 5-tuple")
        action, msg, cat, mod, ln = item
        if (_warnings_check_matched(msg, text) and issubclass(category, cat)
                and _warnings_check_matched(mod, module) and (ln == 0 or lineno == ln)):
            return action, item
    action = _warnings_attr("defaultaction", _cetta_object_dict(_warnings_module)["_defaultaction"])
    if not isinstance(action, str):
        raise TypeError("warnings.defaultaction must be a string, not " + repr(type(action).__name__))
    return action, None


def _warnings_already_warned(registry, key, should_set):
    version = registry.get("version")
    if type(version) is not int or version != _warnings_version[0]:
        registry.clear()
        registry["version"] = _warnings_version[0]
    elif registry.get(key):
        return True
    if should_set:
        registry[key] = True
    return False


def _warnings_normalize_module(filename):
    if not filename:
        return "<unknown>"
    if filename[-3:] == ".py":
        return filename[:-3]
    return filename


def _warnings_show(category, text, message, filename, lineno, source):
    # call_show_warning: warnings._showwarnmsg with a WarningMessage, or,
    # before the warnings module is imported, written to sys.stderr directly
    show = _warnings_attr("_showwarnmsg", None)
    if show is None:
        out = _sys_modules["sys"].stderr
        text_out = filename + ":" + str(lineno) + ": " + category.__name__ + ": " + str(text) + "\n"
        line = _getline(filename, lineno)
        if line and line.strip():
            text_out += "  " + line.strip() + "\n"
        out.write(text_out)
        return None
    if not callable(show):
        raise TypeError("warnings._showwarnmsg() must be set to a callable")
    cls = _warnings_attr("WarningMessage", None)
    show(cls(message, category, filename, lineno, None, None, source))
    return None


def _warn_explicit(message, category, filename, lineno, module=None, registry=None,
                   module_globals=None, source=None):
    # warnings_warn_explicit_impl and warn_explicit
    if module_globals is not None and not isinstance(module_globals, dict):
        raise TypeError("module_globals must be a dict, not '" + type(module_globals).__name__ + "'")
    if module is None:
        module = _warnings_normalize_module(filename)
    if registry is not None and not isinstance(registry, dict):
        raise TypeError("'registry' must be a dict or None")
    if isinstance(message, Warning):
        text = str(message)
        category = type(message)
    else:
        text = message
        message = category(message)
    key = (text, category, lineno)
    if registry is not None:
        if _warnings_already_warned(registry, key, False):
            return None
    action, item = _warnings_get_filter(category, text, lineno, module)
    if action == "error":
        raise message
    if action == "ignore":
        return None
    seen = False
    if action != "always" and action != "all":
        if registry is not None:
            registry[key] = True
        if action == "once":
            if registry is None:
                registry = _cetta_object_dict(_warnings_module)["_onceregistry"]
            seen = _warnings_already_warned(registry, (text, category), True)
        elif action == "module":
            if registry is not None:
                seen = _warnings_already_warned(registry, (text, category), True)
        elif action != "default":
            raise RuntimeError("Unrecognized action (" + repr(action) + ") in warnings.filters:\n " + repr(item))
    if not seen:
        _warnings_show(category, text, message, filename, lineno, source)
    return None


def _warnings_category(message, category):
    # get_category
    if isinstance(message, Warning):
        return type(message)
    if category is None:
        return UserWarning
    if not isinstance(category, type):
        raise TypeError("category must be a Warning subclass, not '" + _fqn(type(category)) + "'")
    if not issubclass(category, Warning):
        raise TypeError("category must be a Warning subclass, not class '" + _fqn(category) + "'")
    return category



def _warnings_context(frame):
    # setup_context: a frame's file, line, module name and registry
    if frame is None:
        globals = _cetta_object_dict(_sys_modules["sys"])
        filename, lineno = "<sys>", 0
    else:
        globals = frame.f_globals
        filename, lineno = frame.f_code.co_filename, frame.f_lineno
    registry = globals.get("__warningregistry__")
    if registry is None:
        registry = {}
        globals["__warningregistry__"] = registry
    module = globals.get("__name__")
    if not (module is None or isinstance(module, str)):
        module = "<string>"
    return filename, lineno, module, registry, globals


def _warn(message, category=None, stacklevel=1, source=None, *, skip_file_prefixes=()):
    # warnings_warn_impl: the warning attributed to the frame STACKLEVEL
    # calls out (1 its caller's)
    category = _warnings_category(message, category)
    if not isinstance(skip_file_prefixes, tuple):
        raise TypeError("warn() argument 'skip_file_prefixes' must be tuple, not " + type(skip_file_prefixes).__name__)
    if skip_file_prefixes and stacklevel < 2:
        stacklevel = 2
    try:
        frame = _cetta_frame(stacklevel - 1 if stacklevel > 0 else 0)
    except ValueError:
        frame = None
    filename, lineno, module, registry, globals = _warnings_context(frame)
    return _warn_explicit(message, category, filename, lineno, module, registry, globals, source)


def _warn_at(category, message, filename, lineno, globals):
    # PyErr_WarnEx from the runtime's own operations: where the machine is
    # (no frame: sys's namespace, as setup_context takes it)
    if globals is None:
        globals = _cetta_object_dict(_sys_modules["sys"])
    registry = globals.get("__warningregistry__")
    if registry is None:
        registry = {}
        globals["__warningregistry__"] = registry
    module = globals.get("__name__")
    if not (module is None or isinstance(module, str)):
        module = "<string>"
    return _warn_explicit(message, category, filename, lineno, module, registry, globals)


def _make_warnings():
    d = _cetta_object_dict(_warnings_module)
    d["filters"] = [("default", None, DeprecationWarning, "__main__", 0),
                    ("ignore", None, DeprecationWarning, None, 0),
                    ("ignore", None, PendingDeprecationWarning, None, 0),
                    ("ignore", None, ImportWarning, None, 0),
                    ("ignore", None, ResourceWarning, None, 0)]
    d["_defaultaction"] = "default"
    d["_onceregistry"] = {}
    d["warn"] = _warn
    d["warn_explicit"] = _warn_explicit
    d["_filters_mutated_lock_held"] = _warnings_filters_mutated_lock_held
    d["_acquire_lock"] = _warnings_acquire_lock
    d["_release_lock"] = _warnings_release_lock
    d["_warnings_context"] = None


_make_warnings()


# ---------------------------------------------------------------- frozendict (3.15)

# Objects/dictobject.c's frozendict: a mapping that never changes, hashable
# when its values are, its entries kept in a dict of its own
_frozendict = _cetta_builtin_type("frozendict")


def _frozendict_entries(fd):
    return _cetta_field(fd, "dict")


def _frozendict_make(cls, d):
    fd = _cetta_new_object(cls)
    _cetta_set_field(fd, "dict", d)
    return fd


def _frozendict_new(cls, *args, **kwargs):
    # frozendict_vectorcall: at most one argument, merged as dict.update
    # merges it, then the keywords; an exact frozendict given alone is the
    # result itself
    if len(args) > 1:
        raise TypeError("frozendict expected at most 1 argument, got " + str(len(args)))
    if len(args) == 1 and not kwargs and type(args[0]) is _frozendict and cls is _frozendict:
        return args[0]
    d = {}
    if args:
        _dict_update_arg(d, args[0])
    for k in kwargs:
        d[k] = kwargs[k]
    return _frozendict_make(cls, d)


def _frozendict_len(self):
    return len(_frozendict_entries(self))


def _frozendict_getitem(self, key):
    # _PyDict_Subscript: a subclass's __missing__ for a key it lacks
    d = _frozendict_entries(self)
    if key in d:
        return d[key]
    if type(self) is not _frozendict:
        missing = _mro_lookup(type(self), "__missing__")
        if missing is not _NULL:
            return missing(self, key)
    raise KeyError(key)


def _frozendict_contains(self, key):
    return key in _frozendict_entries(self)


def _frozendict_iter(self):
    return iter(_frozendict_entries(self))


def _frozendict_reversed(self):
    return reversed(_frozendict_entries(self))


def _frozendict_get(self, key, default=None, /):
    return _frozendict_entries(self).get(key, default)


def _frozendict_keys(self):
    return _frozendict_entries(self).keys()


def _frozendict_values(self):
    return _frozendict_entries(self).values()


def _frozendict_items(self):
    return _frozendict_entries(self).items()


def _frozendict_copy(self):
    # frozendict_copy_impl: an exact frozendict is its own copy; a subclass's
    # copy is a frozendict
    if type(self) is _frozendict:
        return self
    return _frozendict_make(_frozendict, dict.copy(_frozendict_entries(self)))


def _frozendict_fromkeys(cls, iterable, value=None, /):
    return cls(dict.fromkeys(iterable, value))


def _frozendict_repr(self):
    d = _frozendict_entries(self)
    name = _cetta_type_field(type(self), "name")
    return name + "()" if not d else name + "(" + repr(d) + ")"


def _frozendict_hash(self):
    # frozendict_hash: frozenset_hash's mixing of each (key, value) pair's
    # tuple hash, so the hash of the frozenset of its items; each pair hashed
    # first, as frozendict_pair_hash hashes each value
    items = _frozendict_entries(self).items()
    for item in items:
        hash(item)
    return hash(frozenset(items))


def _any_dict(o):
    return isinstance(o, dict) or isinstance(o, _frozendict)


def _any_dict_entries(o):
    return _frozendict_entries(o) if isinstance(o, _frozendict) else o


def _frozendict_eq(self, other):
    if not _any_dict(other):
        return NotImplemented
    return _frozendict_entries(self) == _any_dict_entries(other)


def _frozendict_ne(self, other):
    if not _any_dict(other):
        return NotImplemented
    return _frozendict_entries(self) != _any_dict_entries(other)


def _frozendict_or(self, other):
    # frozendict_or, _PyDict_Or: a copy of the left one's kind updated with
    # the right one; an empty exact frozendict or-ed is the other unchanged
    if not _any_dict(other):
        return NotImplemented
    if type(self) is _frozendict:
        if not _frozendict_entries(self) and type(other) is _frozendict:
            return other
        if (type(other) is dict or type(other) is _frozendict) and not _any_dict_entries(other):
            return self
    d = dict.copy(_frozendict_entries(self))
    _dict_update_arg(d, other)
    return _frozendict_make(_frozendict, d)


def _frozendict_ror(self, other):
    # a dict on the left (dict's nb_or takes any dict): a dict
    if not isinstance(other, dict):
        return NotImplemented
    d = dict.copy(other)
    _dict_update_arg(d, self)
    return d


def _frozendict_getnewargs(self):
    return (dict(_frozendict_entries(self)),)


for _name, _f in (("__new__", staticmethod(_frozendict_new)), ("__len__", _frozendict_len),
                  ("__getitem__", _frozendict_getitem), ("__contains__", _frozendict_contains),
                  ("__iter__", _frozendict_iter), ("__reversed__", _frozendict_reversed),
                  ("get", _frozendict_get), ("keys", _frozendict_keys), ("values", _frozendict_values),
                  ("items", _frozendict_items), ("copy", _frozendict_copy),
                  ("fromkeys", classmethod(_frozendict_fromkeys)), ("__repr__", _frozendict_repr),
                  ("__hash__", _frozendict_hash), ("__eq__", _frozendict_eq), ("__ne__", _frozendict_ne),
                  ("__or__", _frozendict_or), ("__ror__", _frozendict_ror),
                  ("__getnewargs__", _frozendict_getnewargs)):
    _cetta_set_type_attr(_frozendict, _name, _f)
_cetta_set_type_attr(_frozendict, "__class_getitem__", classmethod(_generic_alias))
_CROSS_LANGUAGE_HINTS["update"] = ((frozenset, "Did you mean to use a 'set' object?", True),
                                   (_frozendict, "Did you mean to use a 'dict' object?", True))


# ---------------------------------------------------------------- sentinel (3.15)

# Objects/sentinelobject.c: a unique object with a name, shown as its name
# (or as repr=), its module its maker's
_sentinel = _cetta_builtin_type("sentinel")


def _sentinel_new(cls, *args, repr=None, **kwargs):
    for k in kwargs:
        raise TypeError("sentinel() got an unexpected keyword argument '" + k + "'")
    if len(args) != 1:
        raise TypeError("sentinel() takes exactly 1 positional argument (" + str(len(args)) + " given)")
    name = args[0]
    if not isinstance(name, str):
        raise TypeError("sentinel() argument 1 must be str, not " + _type_name(type(name)))
    if repr is not None and not isinstance(repr, str):
        raise TypeError("sentinel() argument 'repr' must be str or None, not " + _type_name(type(repr)))
    try:
        module = _cetta_frame(0).f_globals.get("__name__")
    except ValueError:
        module = None
    s = _cetta_new_object(cls)
    _cetta_set_field(s, "name", name)
    _cetta_set_field(s, "module", module)
    _cetta_set_field(s, "repr", repr)
    return s


def _sentinel_repr(self):
    r = _cetta_field(self, "repr")
    return _cetta_field(self, "name") if r is None else r


def _sentinel_name(self):
    return _cetta_field(self, "name")


def _sentinel_copy(self):
    return self


def _sentinel_deepcopy(self, memo):
    return self


def _sentinel_reduce(self):
    return _cetta_field(self, "name")


for _name, _f in (("__new__", staticmethod(_sentinel_new)), ("__repr__", _sentinel_repr),
                  ("__copy__", _sentinel_copy), ("__deepcopy__", _sentinel_deepcopy),
                  ("__reduce__", _sentinel_reduce), ("__or__", _union_type_or), ("__ror__", _union_type_ror)):
    _cetta_set_type_attr(_sentinel, _name, _f)
_member(_sentinel, "__name__", "name")
_sentinel_module_member = _cetta_new_object(_member_descriptor)
_cetta_set_field(_sentinel_module_member, "owner", _sentinel)
_cetta_set_field(_sentinel_module_member, "member", "__module__")
_cetta_set_field(_sentinel_module_member, "field", "module")
_cetta_set_field(_sentinel_module_member, "writable", True)
_cetta_set_type_attr(_sentinel, "__module__", _sentinel_module_member)
frozendict = _frozendict
sentinel = _sentinel


# the builtins module's namespace (bltinmodule.c, and site's additions): the
# names CPython's has, in its order, each bound to the prelude's object
_BUILTIN_NAMES = (
    '__name__', '__doc__', '__package__', '__loader__', '__spec__', '__build_class__',
    '__import__', '__lazy_import__', 'abs', 'all', 'any', 'ascii', 'bin', 'breakpoint', 'callable',
    'chr', 'compile', 'delattr', 'dir', 'divmod', 'eval', 'exec', 'format', 'getattr', 'globals',
    'hasattr', 'hash', 'hex', 'id', 'input', 'isinstance', 'issubclass', 'iter', 'aiter', 'len',
    'locals', 'max', 'min', 'next', 'anext', 'oct', 'ord', 'pow', 'print', 'repr', 'round',
    'setattr', 'sorted', 'sum', 'vars', 'None', 'Ellipsis', 'NotImplemented', 'False', 'True',
    'bool', 'memoryview', 'bytearray', 'bytes', 'classmethod', 'complex', 'dict', 'enumerate',
    'filter', 'float', 'frozendict', 'frozenset', 'property', 'int', 'list', 'map', 'object',
    'range', 'reversed', 'sentinel', 'set', 'slice', 'staticmethod', 'str', 'super', 'tuple',
    'type', 'zip', '__debug__', 'BaseException', 'BaseExceptionGroup', 'Exception',
    'GeneratorExit', 'KeyboardInterrupt', 'SystemExit', 'ArithmeticError', 'AssertionError',
    'AttributeError', 'BufferError', 'EOFError', 'ImportError', 'LookupError', 'MemoryError',
    'NameError', 'OSError', 'ReferenceError', 'RuntimeError', 'StopAsyncIteration',
    'StopIteration', 'SyntaxError', 'SystemError', 'TypeError', 'ValueError', 'Warning',
    'FloatingPointError', 'OverflowError', 'ZeroDivisionError', 'BytesWarning',
    'DeprecationWarning', 'EncodingWarning', 'FutureWarning', 'ImportWarning',
    'PendingDeprecationWarning', 'ResourceWarning', 'RuntimeWarning', 'SyntaxWarning',
    'UnicodeWarning', 'UserWarning', 'BlockingIOError', 'ChildProcessError', 'ConnectionError',
    'FileExistsError', 'FileNotFoundError', 'InterruptedError', 'IsADirectoryError',
    'NotADirectoryError', 'PermissionError', 'ProcessLookupError', 'TimeoutError',
    'IndentationError', '_IncompleteInputError', 'IndexError', 'KeyError', 'ImportCycleError',
    'ModuleNotFoundError', 'NotImplementedError', 'PythonFinalizationError', 'RecursionError',
    'UnboundLocalError', 'UnicodeError', 'BrokenPipeError', 'ConnectionAbortedError',
    'ConnectionRefusedError', 'ConnectionResetError', 'TabError', 'UnicodeDecodeError',
    'UnicodeEncodeError', 'UnicodeTranslateError', 'ExceptionGroup', 'EnvironmentError', 'IOError',
    'open', 'quit', 'exit', 'copyright', 'credits', 'license', 'help',
)
_BUILTIN_CONSTANTS = {"None": None, "Ellipsis": ..., "NotImplemented": NotImplemented,
                      "False": False, "True": True, "__debug__": True}


def _fill_builtins():
    d = _cetta_object_dict(_builtins_module)
    if "__getattr__" in d:
        del d["__getattr__"]
    for name in _BUILTIN_NAMES:
        if name in d:
            continue
        if name in _BUILTIN_CONSTANTS:
            d[name] = _BUILTIN_CONSTANTS[name]
            continue
        v = _cetta_builtin(name)
        if v is not _NULL:
            d[name] = v


_fill_builtins()
