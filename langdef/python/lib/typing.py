"""Type hints at run time: the parameterizing of generic classes that the
interpreter's own type parameters call on (PEP 484, 560, 646, 695), the
special form Unpack, and the types the interpreter builds in."""

from _typing import (Generic, NoDefault, ParamSpec, ParamSpecArgs, ParamSpecKwargs, TypeAliasType,
                     TypeVar, TypeVarTuple, Union, _idfunc)

__all__ = ["Generic", "NoDefault", "ParamSpec", "ParamSpecArgs", "ParamSpecKwargs", "TypeAliasType",
           "TypeVar", "TypeVarTuple", "Union", "Unpack"]

_FunctionType = type(_idfunc)
_BuiltinFunctionType = type(len)
_GenericAliasType = type(list[int])


def _type_repr(obj):
    # annotationlib.type_repr; a tuple (a ParamSpec's arguments) as a list
    if isinstance(obj, tuple):
        return "[" + ", ".join(_type_repr(t) for t in obj) + "]"
    if isinstance(obj, (type, _FunctionType, _BuiltinFunctionType)):
        module = obj.__module__
        if module is None:
            owner = getattr(obj, "__self__", None)
            if owner is not None:
                module = (owner if isinstance(owner, type) else type(owner)).__module__
        if module is None or module == "builtins":
            return obj.__qualname__
        return module + "." + obj.__qualname__
    if obj is ...:
        return "..."
    return repr(obj)


def _type_convert(arg):
    # None stands for its type
    if arg is None:
        return type(None)
    return arg


def _type_check(arg, msg, is_argument=True):
    # a type argument as the special forms allow it
    arg = _type_convert(arg)
    if isinstance(arg, _GenericAlias) and arg.__origin__ is Generic:
        raise TypeError(f"{arg} is not valid as type argument")
    if isinstance(arg, _SpecialForm) or arg is Generic:
        raise TypeError(f"Plain {arg} is not valid as type argument")
    if type(arg) is tuple:
        raise TypeError(f"{msg} Got {arg!r:.100}.")
    return arg


def _is_param_expr(arg):
    return arg is ... or isinstance(arg, (tuple, list, ParamSpec))


def _is_unpacked_typevartuple(x):
    return not isinstance(x, type) and getattr(x, "__typing_is_unpacked_typevartuple__", False) is True


def _is_typevar_like(x):
    return isinstance(x, (TypeVar, ParamSpec)) or _is_unpacked_typevartuple(x)


def _collect_type_parameters(args, *, enforce_default_ordering=True, validate_all=False):
    # the type parameters among ARGS in the order they first appear; an
    # explicit Generic[...] gives its own order
    default_encountered = False
    type_var_tuple_encountered = False
    parameters = []
    for t in args:
        if isinstance(t, type):
            pass
        elif isinstance(t, tuple):
            for x in t:
                for collected in _collect_type_parameters([x]):
                    if collected not in parameters:
                        parameters.append(collected)
        elif hasattr(t, "__typing_subst__"):
            if t not in parameters:
                if enforce_default_ordering:
                    if type_var_tuple_encountered and t.has_default():
                        raise TypeError("Type parameter with a default follows TypeVarTuple")
                    if t.has_default():
                        default_encountered = True
                    elif default_encountered:
                        raise TypeError(f"Type parameter {t!r} without a default"
                                        " follows type parameter with a default")
                parameters.append(t)
        elif not validate_all and isinstance(t, _GenericAlias) and t.__origin__ is Generic:
            parameters = t.__parameters__
        else:
            if _is_unpacked_typevartuple(t):
                type_var_tuple_encountered = True
            for x in getattr(t, "__parameters__", ()):
                if x not in parameters:
                    parameters.append(x)
    return tuple(parameters)


def _check_generic_specialization(cls, arguments):
    # the count of a generic class's arguments, defaults filling the rest
    expected_len = len(cls.__parameters__)
    if not expected_len:
        raise TypeError(f"{cls} is not a generic class")
    actual_len = len(arguments)
    if actual_len != expected_len:
        if actual_len < expected_len:
            if cls.__parameters__[actual_len].has_default():
                return
            expected_len -= sum(p.has_default() for p in cls.__parameters__)
            expect_val = f"at least {expected_len}"
        else:
            expect_val = expected_len
        raise TypeError(f"Too {'many' if actual_len > expected_len else 'few'} arguments"
                        f" for {cls}; actual {actual_len}, expected {expect_val}")


def _unpack_args(*args):
    # an unpacked tuple type's arguments stand in its place
    newargs = []
    for arg in args:
        subargs = getattr(arg, "__typing_unpacked_tuple_args__", None)
        if subargs is not None and not (subargs and subargs[-1] is ...):
            newargs.extend(subargs)
        else:
            newargs.append(arg)
    return newargs


class _LRUCache:
    # functools.lru_cache's keeping: at most MAXSIZE results, the least
    # recently used dropped first
    def __init__(self, maxsize):
        self.maxsize = maxsize
        self.entries = {}

    def lookup(self, key):
        entries = self.entries
        if key in entries:
            value = entries.pop(key)
            entries[key] = value
            return True, value
        return False, None

    def store(self, key, value):
        entries = self.entries
        if len(entries) >= self.maxsize:
            del entries[next(iter(entries))]
        entries[key] = value


def _tp_cache(func):
    # the results of a generic's subscription kept (the same alias each
    # time); arguments that cannot be hashed are not kept
    cache = _LRUCache(128)

    def inner(*args):
        try:
            found, value = cache.lookup(args)
            if found:
                return value
            value = func(*args)
            cache.store(args, value)
            return value
        except TypeError:
            pass
        return func(*args)
    inner.__name__ = func.__name__
    inner.__qualname__ = func.__qualname__
    inner.__doc__ = func.__doc__
    inner.__wrapped__ = func
    return inner


class _Final:
    """Mixin to prohibit subclassing."""

    __slots__ = ("__weakref__",)

    def __init_subclass__(cls, /, *args, **kwds):
        if "_root" not in kwds:
            raise TypeError("Cannot subclass special typing classes")


class _NotIterable:
    """Mixin to prevent iteration, without being compatible with Iterable."""

    __slots__ = ()
    __iter__ = None


class _SpecialForm(_Final, _NotIterable, _root=True):
    # a special typing construct, subscripted by the function it wraps
    __slots__ = ("_name", "__doc__", "_getitem")

    def __init__(self, getitem):
        self._getitem = getitem
        self._name = getitem.__name__
        self.__doc__ = getitem.__doc__

    def __getattr__(self, item):
        if item in {"__name__", "__qualname__"}:
            return self._name
        raise AttributeError(item)

    def __mro_entries__(self, bases):
        raise TypeError(f"Cannot subclass {self!r}")

    def __repr__(self):
        return "typing." + self._name

    def __reduce__(self):
        return self._name

    def __call__(self, *args, **kwds):
        raise TypeError(f"Cannot instantiate {self!r}")

    def __or__(self, other):
        return Union[self, other]

    def __ror__(self, other):
        return Union[other, self]

    def __instancecheck__(self, obj):
        raise TypeError(f"{self} cannot be used with isinstance()")

    def __subclasscheck__(self, cls):
        raise TypeError(f"{self} cannot be used with issubclass()")

    @_tp_cache
    def __getitem__(self, parameters):
        return self._getitem(self, parameters)


def _is_dunder(attr):
    return attr.startswith("__") and attr.endswith("__")


class _BaseGenericAlias(_Final, _root=True):
    # a generic version of the class ORIGIN; one made with inst=False may
    # not be called, and one with a NAME is shown as typing.NAME

    def __init__(self, origin, *, inst=True, name=None):
        self._inst = inst
        self._name = name
        self.__origin__ = origin
        self.__slots__ = None

    def __call__(self, *args, **kwargs):
        if not self._inst:
            raise TypeError(f"Type {self._name} cannot be instantiated; "
                            f"use {self.__origin__.__name__}() instead")
        result = self.__origin__(*args, **kwargs)
        try:
            result.__orig_class__ = self
        except Exception:
            pass
        return result

    def __mro_entries__(self, bases):
        # the origin, and Generic unless a later base brings it
        res = []
        if self.__origin__ not in bases:
            res.append(self.__origin__)
        i = bases.index(self)
        for b in bases[i + 1:]:
            if isinstance(b, _BaseGenericAlias):
                break
            if not isinstance(b, type):
                meth = getattr(b, "__mro_entries__", None)
                new_bases = meth(bases) if meth else None
                if isinstance(new_bases, tuple) and any(
                        isinstance(b2, type) and issubclass(b2, Generic) for b2 in new_bases):
                    break
            elif issubclass(b, Generic):
                break
        else:
            res.append(Generic)
        return tuple(res)

    def __getattr__(self, attr):
        if attr in {"__name__", "__qualname__"}:
            return self._name or self.__origin__.__name__
        if "__origin__" in self.__dict__ and not _is_dunder(attr):
            return getattr(self.__origin__, attr)
        raise AttributeError(attr)

    def __setattr__(self, attr, val):
        if _is_dunder(attr) or attr in {"_name", "_inst", "_nparams", "_defaults"}:
            super().__setattr__(attr, val)
        else:
            setattr(self.__origin__, attr, val)

    def __instancecheck__(self, obj):
        return self.__subclasscheck__(type(obj))

    def __subclasscheck__(self, cls):
        raise TypeError("Subscripted generics cannot be used with class and instance checks")

    def __dir__(self):
        return list(set(super().__dir__() + [attr for attr in dir(self.__origin__) if not _is_dunder(attr)]))


class _GenericAlias(_BaseGenericAlias, _root=True):
    # a generic class with its type arguments, Box[int]

    def __init__(self, origin, args, *, inst=True, name=None):
        super().__init__(origin, inst=inst, name=name)
        if not isinstance(args, tuple):
            args = (args,)
        self.__args__ = tuple(args)
        self.__parameters__ = _collect_type_parameters(args, enforce_default_ordering=origin is Generic)
        if not name:
            self.__module__ = origin.__module__

    def __eq__(self, other):
        if not isinstance(other, _GenericAlias):
            return NotImplemented
        return self.__origin__ == other.__origin__ and self.__args__ == other.__args__

    def __hash__(self):
        return hash((self.__origin__, self.__args__))

    def __or__(self, right):
        return Union[self, right]

    def __ror__(self, left):
        return Union[left, self]

    @_tp_cache
    def __getitem__(self, args):
        # an alias subscripted again: its parameters replaced
        if self.__origin__ is Generic:
            raise TypeError(f"Cannot subscript already-subscripted {self}")
        if not self.__parameters__:
            raise TypeError(f"{self} is not a generic class")
        if not isinstance(args, tuple):
            args = (args,)
        args = _unpack_args(*(_type_convert(p) for p in args))
        new_args = self._determine_new_args(args)
        return self.copy_with(new_args)

    def _determine_new_args(self, args):
        params = self.__parameters__
        for param in params:
            prepare = getattr(param, "__typing_prepare_subst__", None)
            if prepare is not None:
                args = prepare(self, args)
        alen = len(args)
        plen = len(params)
        if alen != plen:
            raise TypeError(f"Too {'many' if alen > plen else 'few'} arguments for {self};"
                            f" actual {alen}, expected {plen}")
        new_arg_by_param = dict(zip(params, args))
        return tuple(self._make_substitution(self.__args__, new_arg_by_param))

    def _make_substitution(self, args, new_arg_by_param):
        new_args = []
        for old_arg in args:
            if isinstance(old_arg, type):
                new_args.append(old_arg)
                continue
            substfunc = getattr(old_arg, "__typing_subst__", None)
            if substfunc:
                new_arg = substfunc(new_arg_by_param[old_arg])
            else:
                subparams = getattr(old_arg, "__parameters__", ())
                if not subparams:
                    new_arg = old_arg
                else:
                    subargs = []
                    for x in subparams:
                        if isinstance(x, TypeVarTuple):
                            subargs.extend(new_arg_by_param[x])
                        else:
                            subargs.append(new_arg_by_param[x])
                    new_arg = old_arg[tuple(subargs)]
            if _is_unpacked_typevartuple(old_arg):
                new_args.extend(new_arg)
            elif isinstance(old_arg, tuple):
                new_args.append(tuple(self._make_substitution(old_arg, new_arg_by_param)))
            else:
                new_args.append(new_arg)
        return new_args

    def copy_with(self, args):
        return self.__class__(self.__origin__, args, name=self._name, inst=self._inst)

    def __repr__(self):
        name = "typing." + self._name if self._name else _type_repr(self.__origin__)
        args = ", ".join([_type_repr(a) for a in self.__args__]) if self.__args__ else "()"
        return f"{name}[{args}]"

    def __reduce__(self):
        origin = globals()[self._name] if self._name else self.__origin__
        args = tuple(self.__args__)
        if len(args) == 1 and not isinstance(args[0], tuple):
            args, = args
        return _operator_getitem, (origin, args)

    def __mro_entries__(self, bases):
        # the origin; Generic[...] gives nothing when a later base is a
        # generic alias, which brings Generic itself
        if isinstance(self.__origin__, _SpecialForm):
            raise TypeError(f"Cannot subclass {self!r}")
        if self._name:
            return super().__mro_entries__(bases)
        if self.__origin__ is Generic:
            i = bases.index(self)
            for b in bases[i + 1:]:
                if isinstance(b, _BaseGenericAlias) and b is not self:
                    return ()
        return (self.__origin__,)

    def __iter__(self):
        yield Unpack[self]


def _operator_getitem(a, b):
    return a[b]


@_SpecialForm
def Unpack(self, parameters):
    """Type unpack operator: the types a tuple type or a TypeVarTuple holds,
    pulled out in place (PEP 646, 692)."""
    item = _type_check(parameters, f"{self} accepts only single type.")
    return _UnpackGenericAlias(origin=self, args=(item,))


class _UnpackGenericAlias(_GenericAlias, _root=True):
    def __repr__(self):
        return f"typing.Unpack[{_type_repr(self.__args__[0])}]"

    def __getitem__(self, args):
        if self.__typing_is_unpacked_typevartuple__:
            return args
        return super().__getitem__(args)

    @property
    def __typing_unpacked_tuple_args__(self):
        arg, = self.__args__
        if isinstance(arg, (_GenericAlias, _GenericAliasType)):
            if arg.__origin__ is not tuple:
                raise TypeError("Unpack[...] must be used with a tuple type")
            return arg.__args__
        return None

    @property
    def __typing_is_unpacked_typevartuple__(self):
        return isinstance(self.__args__[0], TypeVarTuple)


# ---------------------------------------------------------------- what the interpreter calls

def _typevar_subst(self, arg):
    arg = _type_check(arg, "Parameters to generic types must be types.", is_argument=True)
    if ((isinstance(arg, _GenericAlias) and arg.__origin__ is Unpack)
            or (isinstance(arg, _GenericAliasType) and getattr(arg, "__unpacked__", False))):
        raise TypeError(f"{arg} is not valid as type argument")
    return arg


def _typevartuple_prepare_subst(self, alias, args):
    params = alias.__parameters__
    typevartuple_index = params.index(self)
    for param in params[typevartuple_index + 1:]:
        if isinstance(param, TypeVarTuple):
            raise TypeError(f"More than one TypeVarTuple parameter in {alias}")
    alen = len(args)
    plen = len(params)
    left = typevartuple_index
    right = plen - typevartuple_index - 1
    var_tuple_index = None
    fillarg = None
    for k, arg in enumerate(args):
        if not isinstance(arg, type):
            subargs = getattr(arg, "__typing_unpacked_tuple_args__", None)
            if subargs and len(subargs) == 2 and subargs[-1] is ...:
                if var_tuple_index is not None:
                    raise TypeError("More than one unpacked arbitrary-length tuple argument")
                var_tuple_index = k
                fillarg = subargs[0]
    if var_tuple_index is not None:
        left = min(left, var_tuple_index)
        right = min(right, alen - var_tuple_index - 1)
    elif left + right > alen:
        raise TypeError(f"Too few arguments for {alias}; actual {alen}, expected at least {plen - 1}")
    if left == alen - right and self.has_default():
        replacement = _unpack_args(self.__default__)
    else:
        replacement = args[left: alen - right]
    return (*args[:left], *([fillarg] * (typevartuple_index - left)), replacement,
            *([fillarg] * (plen - right - left - typevartuple_index - 1)), *args[alen - right:])


def _paramspec_subst(self, arg):
    if isinstance(arg, (list, tuple)):
        arg = tuple(_type_check(a, "Expected a type.") for a in arg)
    elif not _is_param_expr(arg):
        raise TypeError(f"Expected a list of types, an ellipsis, ParamSpec, or Concatenate. Got {arg}")
    return arg


def _paramspec_prepare_subst(self, alias, args):
    params = alias.__parameters__
    i = params.index(self)
    if i == len(args) and self.has_default():
        args = (*args, self.__default__)
    if i >= len(args):
        raise TypeError(f"Too few arguments for {alias}")
    if len(params) == 1 and not _is_param_expr(args[0]):
        args = (args,)
    elif isinstance(args[i], list):
        args = (*args[:i], tuple(args[i]), *args[i + 1:])
    return args


@_tp_cache
def _generic_class_getitem(cls, args):
    # Generic[...] (unique type variables only) or a generic class
    # subscripted: its parameters prepared and counted
    if not isinstance(args, tuple):
        args = (args,)
    args = tuple(_type_convert(p) for p in args)
    if cls is Generic:
        if not args:
            raise TypeError(f"Parameter list to {cls.__qualname__}[...] cannot be empty")
        if not all(_is_typevar_like(p) for p in args):
            raise TypeError(f"Parameters to {cls.__name__}[...] must all be type variables "
                            f"or parameter specification variables.")
        if len(set(args)) != len(args):
            raise TypeError(f"Parameters to {cls.__name__}[...] must all be unique")
    else:
        try:
            parameters = cls.__parameters__
        except AttributeError as e:
            init_subclass = getattr(cls, "__init_subclass__", None)
            if init_subclass not in {None, Generic.__init_subclass__}:
                e.add_note(f"Note: this exception may have been caused by {init_subclass.__qualname__!r} "
                           f"(or the '__init_subclass__' method on a superclass) not calling "
                           f"'super().__init_subclass__()'")
            raise
        for param in parameters:
            prepare = getattr(param, "__typing_prepare_subst__", None)
            if prepare is not None:
                args = prepare(cls, args)
        _check_generic_specialization(cls, args)
        new_args = []
        for param, new_arg in zip(parameters, args):
            if isinstance(param, TypeVarTuple):
                new_args.extend(new_arg)
            else:
                new_args.append(new_arg)
        args = tuple(new_args)
    return _GenericAlias(cls, args)


def _generic_init_subclass(cls, *args, **kwargs):
    # a subclass of Generic: its __parameters__, the type variables of its
    # original bases, Generic[...]'s when it names them
    super(Generic, cls).__init_subclass__(*args, **kwargs)
    tvars = []
    if "__orig_bases__" in cls.__dict__:
        error = Generic in cls.__orig_bases__
    else:
        error = Generic in cls.__bases__ and cls.__name__ != "Protocol"
    if error:
        raise TypeError("Cannot inherit from plain Generic")
    if "__orig_bases__" in cls.__dict__:
        tvars = _collect_type_parameters(cls.__orig_bases__, validate_all=True)
        gvars = None
        basename = None
        for base in cls.__orig_bases__:
            if isinstance(base, _GenericAlias) and base.__origin__ is Generic:
                if gvars is not None:
                    raise TypeError("Cannot inherit from Generic[...] multiple times.")
                gvars = base.__parameters__
                basename = base.__origin__.__name__
        if gvars is not None:
            tvarset = set(tvars)
            gvarset = set(gvars)
            if not tvarset <= gvarset:
                s_vars = ", ".join(str(t) for t in tvars if t not in gvarset)
                s_args = ", ".join(str(g) for g in gvars)
                raise TypeError(f"Some type variables ({s_vars}) are not listed in {basename}[{s_args}]")
            tvars = gvars
    cls.__parameters__ = tuple(tvars)
