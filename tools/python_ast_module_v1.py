"""Write langdef/python/lib/_ast.py: the _ast module the runtime holds, its
node classes read from the running CPython's own _ast (run it with the
CPython 3.15 oracle), its generic behaviour written after CPython's
Python/Python-ast.c.

usage: python3.15 -I tools/python_ast_module_v1.py langdef/python/lib/_ast.py [--check]
"""
import sys
import types
import _ast

HEADER = '''# The _ast module, as CPython 3.15's Python/Python-ast.c defines it: the AST
# node classes (generated from CPython's own classes by
# tools/python_ast_module_v1.py; do not edit by hand) and their base AST,
# whose construction, pickling, replacement and repr follow ast_type_init,
# ast_type_reduce, ast_type_replace and ast_repr.  The runtime's ast stage
# gives a program's syntax as data; _from_encoding makes nodes of it and
# _to_encoding gives it back for compile().

PyCF_ONLY_AST = %(PyCF_ONLY_AST)d
PyCF_TYPE_COMMENTS = %(PyCF_TYPE_COMMENTS)d
PyCF_ALLOW_TOP_LEVEL_AWAIT = %(PyCF_ALLOW_TOP_LEVEL_AWAIT)d
PyCF_OPTIMIZED_AST = %(PyCF_OPTIMIZED_AST)d

_fqn = _cetta_prelude("_fqn")
_NO = object()
_UnionType = type(int | None)
_GenericAlias = type(list[int])


def _format_missing(missing, fields):
    # format_missing: the missing names in field order, each repr'd
    names = [repr(f) for f in fields if f in missing]
    if len(names) == 1:
        return names[0]
    if len(names) == 2:
        return names[0] + " and " + names[1]
    return ", ".join(names[:-1]) + ", and " + names[-1]


def _repr_list(seq, depth):
    # ast_repr_list: the first item and the last, "..." between when more
    if not seq:
        return repr(seq)
    items = [seq[0]] if len(seq) == 1 else [seq[0], seq[-1]]
    parts = []
    for i, item in enumerate(items):
        parts.append(_repr_depth(item, depth - 1) if isinstance(item, AST) else repr(item))
        if i == 0 and len(seq) > 2:
            parts[-1] += ", ..."
    text = ", ".join(parts)
    return "[" + text + "]" if isinstance(seq, list) else "(" + text + ")"


def _repr_depth(node, depth):
    # ast_repr_max_depth
    name = type(node).__name__
    if depth <= 0 or _cetta_repr_enter(node):
        return name + "(...)"
    try:
        fields = type(node)._fields
        if not fields:
            return name + "()"
        parts = []
        for f in fields:
            value = getattr(node, f)
            if isinstance(value, (list, tuple)):
                text = _repr_list(value, depth)
            elif isinstance(value, AST):
                text = _repr_depth(value, depth - 1)
            else:
                text = repr(value)
            parts.append(f + "=" + text)
        return name + "(" + ", ".join(parts) + ")"
    finally:
        _cetta_repr_leave(node)


class AST:
    __module__ = "ast"
    _fields = ()
    _attributes = ()
    __match_args__ = ()

    def __init__(self, *args, **kwargs):
        # ast_type_init
        cls = type(self)
        if cls in _ABSTRACT:
            import warnings
            warnings.warn("Instantiating abstract AST node class " + _fqn(cls)
                          + " is deprecated. This will become an error in Python 3.20",
                          DeprecationWarning, stacklevel=2)
        fields = cls._fields
        numfields = len(fields)
        remaining = set(fields)
        if numfields < len(args):
            raise TypeError(cls.__name__ + " constructor takes at most " + str(numfields)
                            + " positional argument" + ("" if numfields == 1 else "s"))
        for name, value in zip(fields, args):
            setattr(self, name, value)
            remaining.discard(name)
        attributes = None
        for key, value in kwargs.items():
            if key in fields:
                if key not in remaining:
                    raise TypeError(_fqn(cls) + " got multiple values for argument " + repr(key))
                remaining.discard(key)
            else:
                if attributes is None:
                    attributes = cls._attributes
                if key not in attributes:
                    raise TypeError(_fqn(cls) + ".__init__ got an unexpected keyword argument " + repr(key))
            setattr(self, key, value)
        if remaining:
            field_types = getattr(cls, "_field_types", _NO)
            if field_types is _NO:
                return
            missing = set()
            for name in list(remaining):
                t = field_types.get(name, _NO)
                if t is _NO:
                    raise TypeError("Field " + repr(name) + " is missing from " + _fqn(cls) + "._field_types")
                if type(t) is _UnionType:
                    pass
                elif type(t) is _GenericAlias:
                    setattr(self, name, [])
                elif t is expr_context:
                    setattr(self, name, _SINGLETON["Load"])
                else:
                    missing.add(name)
            if missing:
                raise TypeError(_fqn(cls) + ".__init__ missing " + str(len(missing)) + " required positional argument"
                                + ("" if len(missing) == 1 else "s") + ": " + _format_missing(missing, fields))

    def __reduce__(self):
        # ast_type_reduce: None for each field up to the first one the
        # instance lacks, the fields themselves from its __dict__
        d = getattr(self, "__dict__", _NO)
        if d is _NO:
            return (type(self), ())
        fields = getattr(type(self), "_fields", _NO)
        if fields is _NO:
            return (type(self), (), d)
        positional = []
        for name in fields:
            if name not in d:
                break
            positional.append(None)
        return (type(self), tuple(positional), d)

    def __replace__(self, *args, **kwargs):
        # ast_type_replace: the type called with the instance's fields and
        # attributes, then the keywords
        if args:
            raise TypeError("__replace__() takes no positional arguments")
        cls = type(self)
        d = getattr(self, "__dict__", _NO)
        payload = {}
        if d is not _NO:
            for key in tuple(getattr(cls, "_fields", ())) + tuple(getattr(cls, "_attributes", ())):
                if key in d:
                    payload[key] = d[key]
        payload.update(kwargs)
        return cls(**payload)

    def __repr__(self):
        return _repr_depth(self, 3)

'''

FOOTER = '''

# the parser's node for each class without fields or attributes (Load,
# Add, ...) is one object, as CPython's ast2obj gives its singletons
_SINGLETON = {}
for _c in _CLASSES.values():
    if not _c._fields and not _c._attributes and _c not in _ABSTRACT:
        _SINGLETON[_c.__name__] = _c()
del _c

# per class, how each field and attribute is read from the ast stage's
# data: a node, a list of nodes, or a value
_KINDS = {}


def _kind_of(t):
    if type(t) is _UnionType:
        args = [a for a in t.__args__ if a is not type(None)]
        return _kind_of(args[0]) if len(args) == 1 else "value"
    if type(t) is _GenericAlias:
        return "nodes" if isinstance(t.__args__[0], type) and issubclass(t.__args__[0], AST) else "values"
    if isinstance(t, type) and issubclass(t, AST):
        return "node"
    return "value"


def _kinds(cls):
    k = _KINDS.get(cls)
    if k is None:
        ft = cls.__dict__.get("_field_types", {})
        k = tuple((f, _kind_of(ft[f]) if f in ft else "value") for f in cls._fields) \\
            + tuple((a, "value") for a in cls._attributes)
        _KINDS[cls] = k
    return k


def _from_encoding(enc):
    # a node from the ast stage's data: (NAME, field..., attribute...)
    name = enc[0]
    single = _SINGLETON.get(name)
    if single is not None:
        return single
    cls = _CLASSES[name]
    node = cls.__new__(cls)
    for (f, kind), value in zip(_kinds(cls), enc[1:]):
        if kind == "node":
            if value is not None:
                value = _from_encoding(value)
        elif kind == "nodes":
            value = [_from_encoding(v) for v in value]
        elif kind == "values":
            value = list(value)
        setattr(node, f, value)
    return node


def _to_encoding(node):
    # the data the runtime compiles from a node: a missing field or
    # attribute is refused as compile() refuses it (obj2ast)
    if not isinstance(node, AST):
        raise TypeError("expected some sort of AST, but got " + repr(node))
    cls = type(node)
    if cls in _ABSTRACT or cls.__name__ not in _CLASSES:
        raise TypeError("expected some sort of " + cls.__mro__[1].__name__ + ", but got " + repr(node))
    out = [cls.__name__]
    for f, kind in _kinds(cls):
        value = getattr(node, f, _NO)
        if value is _NO:
            if f in ("end_lineno", "end_col_offset"):
                value = None
            elif kind in ("nodes", "values"):
                value = []
            elif type(cls.__dict__.get("_field_types", {}).get(f)) is _UnionType:
                value = None
            else:
                raise TypeError("required field " + repr(f).replace("'", '"') + " missing from "
                                + _category(cls).__name__)
        if kind == "value" and type(value) is tuple:
            # a tuple held as a value (a constant): kept apart from a node's data
            value = ("$v", value)
        elif kind == "node" and value is not None:
            value = _to_encoding(value)
        elif kind == "nodes":
            value = [_to_encoding(v) for v in value]
        elif kind == "values":
            value = list(value)
        out.append(value)
    return tuple(out)


def _category(cls):
    for c in cls.__mro__[1:]:
        if c in _ABSTRACT and c is not AST:
            return c
    return cls
'''


def render_type(t):
    if isinstance(t, types.UnionType):
        return " | ".join(render_type(a) for a in t.__args__)
    if isinstance(t, types.GenericAlias):
        return t.__origin__.__name__ + "[" + ", ".join(render_type(a) for a in t.__args__) + "]"
    if t is type(None):
        return "None"
    return t.__name__


def generate():
    out = [HEADER % {n: getattr(_ast, n) for n in ("PyCF_ONLY_AST", "PyCF_TYPE_COMMENTS",
                                                    "PyCF_ALLOW_TOP_LEVEL_AWAIT", "PyCF_OPTIMIZED_AST")}]
    classes = [v for v in vars(_ast).values() if isinstance(v, type) and issubclass(v, _ast.AST) and v is not _ast.AST]
    abstract = [c for c in classes if any(d.__base__ is c for d in classes)]
    names = []
    for c in classes:
        names.append(c.__name__)
        own = vars(c)
        lines = ["class %s(%s):" % (c.__name__, c.__base__.__name__), '    __module__ = "ast"']
        if own.get("__doc__") is not None:
            lines.append("    __doc__ = %r" % own["__doc__"])
        for key in ("_fields", "_attributes", "__match_args__"):
            if key in own:
                lines.append("    %s = %r" % (key, tuple(own[key])))
        for key, value in own.items():
            if value is None and not key.startswith("__"):
                lines.append("    %s = None" % key)
        out.append("\n".join(lines) + "\n\n")
    out.append("\n# _field_types, once every class exists\n")
    for c in classes:
        ft = vars(c).get("_field_types")
        if ft is not None:
            out.append("%s._field_types = {%s}\n" % (c.__name__, ", ".join(
                "%r: %s" % (k, render_type(v)) for k, v in ft.items())))
    out.append("\n_ABSTRACT = {AST, %s}\n" % ", ".join(c.__name__ for c in abstract))
    out.append("_CLASSES = {%s}\n" % ", ".join("%r: %s" % (n, n) for n in names))
    out.append(FOOTER)
    return "".join(out)


def main():
    path = sys.argv[1]
    text = generate()
    if "--check" in sys.argv[2:]:
        current = open(path).read()
        if current != text:
            print(path + " differs from a fresh generation")
            sys.exit(1)
        print(path + " is up to date")
        return
    with open(path, "w") as f:
        f.write(text)
    print("wrote " + path)


if __name__ == "__main__":
    main()
