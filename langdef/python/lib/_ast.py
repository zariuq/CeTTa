# The _ast module, as CPython 3.15's Python/Python-ast.c defines it: the AST
# node classes (generated from CPython's own classes by
# tools/python_ast_module_v1.py; do not edit by hand) and their base AST,
# whose construction, pickling, replacement and repr follow ast_type_init,
# ast_type_reduce, ast_type_replace and ast_repr.  The runtime's ast stage
# gives a program's syntax as data; _from_encoding makes nodes of it and
# _to_encoding gives it back for compile().

PyCF_ONLY_AST = 1024
PyCF_TYPE_COMMENTS = 4096
PyCF_ALLOW_TOP_LEVEL_AWAIT = 8192
PyCF_OPTIMIZED_AST = 33792

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

class mod(AST):
    __module__ = "ast"
    __doc__ = 'mod = Module(stmt* body, type_ignore* type_ignores)\n    | Interactive(stmt* body)\n    | Expression(expr body)\n    | FunctionType(expr* argtypes, expr returns)'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class Module(mod):
    __module__ = "ast"
    __doc__ = 'Module(stmt* body, type_ignore* type_ignores)'
    _fields = ('body', 'type_ignores')
    __match_args__ = ('body', 'type_ignores')

class Interactive(mod):
    __module__ = "ast"
    __doc__ = 'Interactive(stmt* body)'
    _fields = ('body',)
    __match_args__ = ('body',)

class Expression(mod):
    __module__ = "ast"
    __doc__ = 'Expression(expr body)'
    _fields = ('body',)
    __match_args__ = ('body',)

class FunctionType(mod):
    __module__ = "ast"
    __doc__ = 'FunctionType(expr* argtypes, expr returns)'
    _fields = ('argtypes', 'returns')
    __match_args__ = ('argtypes', 'returns')

class stmt(AST):
    __module__ = "ast"
    __doc__ = 'stmt = FunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)\n     | AsyncFunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)\n     | ClassDef(identifier name, expr* bases, keyword* keywords, stmt* body, expr* decorator_list, type_param* type_params)\n     | Return(expr? value)\n     | Delete(expr* targets)\n     | Assign(expr* targets, expr value, string? type_comment)\n     | TypeAlias(expr name, type_param* type_params, expr value)\n     | AugAssign(expr target, operator op, expr value)\n     | AnnAssign(expr target, expr annotation, expr? value, int simple)\n     | For(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)\n     | AsyncFor(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)\n     | While(expr test, stmt* body, stmt* orelse)\n     | If(expr test, stmt* body, stmt* orelse)\n     | With(withitem* items, stmt* body, string? type_comment)\n     | AsyncWith(withitem* items, stmt* body, string? type_comment)\n     | Match(expr subject, match_case* cases)\n     | Raise(expr? exc, expr? cause)\n     | Try(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)\n     | TryStar(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)\n     | Assert(expr test, expr? msg)\n     | Import(alias* names, int? is_lazy)\n     | ImportFrom(identifier? module, alias* names, int? level, int? is_lazy)\n     | Global(identifier* names)\n     | Nonlocal(identifier* names)\n     | Expr(expr value)\n     | Pass\n     | Break\n     | Continue'
    _fields = ()
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ()
    end_lineno = None
    end_col_offset = None

class FunctionDef(stmt):
    __module__ = "ast"
    __doc__ = 'FunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)'
    _fields = ('name', 'args', 'body', 'decorator_list', 'returns', 'type_comment', 'type_params')
    __match_args__ = ('name', 'args', 'body', 'decorator_list', 'returns', 'type_comment', 'type_params')
    returns = None
    type_comment = None

class AsyncFunctionDef(stmt):
    __module__ = "ast"
    __doc__ = 'AsyncFunctionDef(identifier name, arguments args, stmt* body, expr* decorator_list, expr? returns, string? type_comment, type_param* type_params)'
    _fields = ('name', 'args', 'body', 'decorator_list', 'returns', 'type_comment', 'type_params')
    __match_args__ = ('name', 'args', 'body', 'decorator_list', 'returns', 'type_comment', 'type_params')
    returns = None
    type_comment = None

class ClassDef(stmt):
    __module__ = "ast"
    __doc__ = 'ClassDef(identifier name, expr* bases, keyword* keywords, stmt* body, expr* decorator_list, type_param* type_params)'
    _fields = ('name', 'bases', 'keywords', 'body', 'decorator_list', 'type_params')
    __match_args__ = ('name', 'bases', 'keywords', 'body', 'decorator_list', 'type_params')

class Return(stmt):
    __module__ = "ast"
    __doc__ = 'Return(expr? value)'
    _fields = ('value',)
    __match_args__ = ('value',)
    value = None

class Delete(stmt):
    __module__ = "ast"
    __doc__ = 'Delete(expr* targets)'
    _fields = ('targets',)
    __match_args__ = ('targets',)

class Assign(stmt):
    __module__ = "ast"
    __doc__ = 'Assign(expr* targets, expr value, string? type_comment)'
    _fields = ('targets', 'value', 'type_comment')
    __match_args__ = ('targets', 'value', 'type_comment')
    type_comment = None

class TypeAlias(stmt):
    __module__ = "ast"
    __doc__ = 'TypeAlias(expr name, type_param* type_params, expr value)'
    _fields = ('name', 'type_params', 'value')
    __match_args__ = ('name', 'type_params', 'value')

class AugAssign(stmt):
    __module__ = "ast"
    __doc__ = 'AugAssign(expr target, operator op, expr value)'
    _fields = ('target', 'op', 'value')
    __match_args__ = ('target', 'op', 'value')

class AnnAssign(stmt):
    __module__ = "ast"
    __doc__ = 'AnnAssign(expr target, expr annotation, expr? value, int simple)'
    _fields = ('target', 'annotation', 'value', 'simple')
    __match_args__ = ('target', 'annotation', 'value', 'simple')
    value = None

class For(stmt):
    __module__ = "ast"
    __doc__ = 'For(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)'
    _fields = ('target', 'iter', 'body', 'orelse', 'type_comment')
    __match_args__ = ('target', 'iter', 'body', 'orelse', 'type_comment')
    type_comment = None

class AsyncFor(stmt):
    __module__ = "ast"
    __doc__ = 'AsyncFor(expr target, expr iter, stmt* body, stmt* orelse, string? type_comment)'
    _fields = ('target', 'iter', 'body', 'orelse', 'type_comment')
    __match_args__ = ('target', 'iter', 'body', 'orelse', 'type_comment')
    type_comment = None

class While(stmt):
    __module__ = "ast"
    __doc__ = 'While(expr test, stmt* body, stmt* orelse)'
    _fields = ('test', 'body', 'orelse')
    __match_args__ = ('test', 'body', 'orelse')

class If(stmt):
    __module__ = "ast"
    __doc__ = 'If(expr test, stmt* body, stmt* orelse)'
    _fields = ('test', 'body', 'orelse')
    __match_args__ = ('test', 'body', 'orelse')

class With(stmt):
    __module__ = "ast"
    __doc__ = 'With(withitem* items, stmt* body, string? type_comment)'
    _fields = ('items', 'body', 'type_comment')
    __match_args__ = ('items', 'body', 'type_comment')
    type_comment = None

class AsyncWith(stmt):
    __module__ = "ast"
    __doc__ = 'AsyncWith(withitem* items, stmt* body, string? type_comment)'
    _fields = ('items', 'body', 'type_comment')
    __match_args__ = ('items', 'body', 'type_comment')
    type_comment = None

class Match(stmt):
    __module__ = "ast"
    __doc__ = 'Match(expr subject, match_case* cases)'
    _fields = ('subject', 'cases')
    __match_args__ = ('subject', 'cases')

class Raise(stmt):
    __module__ = "ast"
    __doc__ = 'Raise(expr? exc, expr? cause)'
    _fields = ('exc', 'cause')
    __match_args__ = ('exc', 'cause')
    exc = None
    cause = None

class Try(stmt):
    __module__ = "ast"
    __doc__ = 'Try(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)'
    _fields = ('body', 'handlers', 'orelse', 'finalbody')
    __match_args__ = ('body', 'handlers', 'orelse', 'finalbody')

class TryStar(stmt):
    __module__ = "ast"
    __doc__ = 'TryStar(stmt* body, excepthandler* handlers, stmt* orelse, stmt* finalbody)'
    _fields = ('body', 'handlers', 'orelse', 'finalbody')
    __match_args__ = ('body', 'handlers', 'orelse', 'finalbody')

class Assert(stmt):
    __module__ = "ast"
    __doc__ = 'Assert(expr test, expr? msg)'
    _fields = ('test', 'msg')
    __match_args__ = ('test', 'msg')
    msg = None

class Import(stmt):
    __module__ = "ast"
    __doc__ = 'Import(alias* names, int? is_lazy)'
    _fields = ('names', 'is_lazy')
    __match_args__ = ('names', 'is_lazy')
    is_lazy = None

class ImportFrom(stmt):
    __module__ = "ast"
    __doc__ = 'ImportFrom(identifier? module, alias* names, int? level, int? is_lazy)'
    _fields = ('module', 'names', 'level', 'is_lazy')
    __match_args__ = ('module', 'names', 'level', 'is_lazy')
    module = None
    level = None
    is_lazy = None

class Global(stmt):
    __module__ = "ast"
    __doc__ = 'Global(identifier* names)'
    _fields = ('names',)
    __match_args__ = ('names',)

class Nonlocal(stmt):
    __module__ = "ast"
    __doc__ = 'Nonlocal(identifier* names)'
    _fields = ('names',)
    __match_args__ = ('names',)

class Expr(stmt):
    __module__ = "ast"
    __doc__ = 'Expr(expr value)'
    _fields = ('value',)
    __match_args__ = ('value',)

class Pass(stmt):
    __module__ = "ast"
    __doc__ = 'Pass'
    _fields = ()
    __match_args__ = ()

class Break(stmt):
    __module__ = "ast"
    __doc__ = 'Break'
    _fields = ()
    __match_args__ = ()

class Continue(stmt):
    __module__ = "ast"
    __doc__ = 'Continue'
    _fields = ()
    __match_args__ = ()

class expr(AST):
    __module__ = "ast"
    __doc__ = 'expr = BoolOp(boolop op, expr* values)\n     | NamedExpr(expr target, expr value)\n     | BinOp(expr left, operator op, expr right)\n     | UnaryOp(unaryop op, expr operand)\n     | Lambda(arguments args, expr body)\n     | IfExp(expr test, expr body, expr orelse)\n     | Dict(expr?* keys, expr* values)\n     | Set(expr* elts)\n     | ListComp(expr elt, comprehension* generators)\n     | SetComp(expr elt, comprehension* generators)\n     | DictComp(expr key, expr? value, comprehension* generators)\n     | GeneratorExp(expr elt, comprehension* generators)\n     | Await(expr value)\n     | Yield(expr? value)\n     | YieldFrom(expr value)\n     | Compare(expr left, cmpop* ops, expr* comparators)\n     | Call(expr func, expr* args, keyword* keywords)\n     | FormattedValue(expr value, int conversion, expr? format_spec)\n     | Interpolation(expr value, constant str, int conversion, expr? format_spec)\n     | JoinedStr(expr* values)\n     | TemplateStr(expr* values)\n     | Constant(constant value, string? kind)\n     | Attribute(expr value, identifier attr, expr_context ctx)\n     | Subscript(expr value, expr slice, expr_context ctx)\n     | Starred(expr value, expr_context ctx)\n     | Name(identifier id, expr_context ctx)\n     | List(expr* elts, expr_context ctx)\n     | Tuple(expr* elts, expr_context ctx)\n     | Slice(expr? lower, expr? upper, expr? step)'
    _fields = ()
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ()
    end_lineno = None
    end_col_offset = None

class BoolOp(expr):
    __module__ = "ast"
    __doc__ = 'BoolOp(boolop op, expr* values)'
    _fields = ('op', 'values')
    __match_args__ = ('op', 'values')

class NamedExpr(expr):
    __module__ = "ast"
    __doc__ = 'NamedExpr(expr target, expr value)'
    _fields = ('target', 'value')
    __match_args__ = ('target', 'value')

class BinOp(expr):
    __module__ = "ast"
    __doc__ = 'BinOp(expr left, operator op, expr right)'
    _fields = ('left', 'op', 'right')
    __match_args__ = ('left', 'op', 'right')

class UnaryOp(expr):
    __module__ = "ast"
    __doc__ = 'UnaryOp(unaryop op, expr operand)'
    _fields = ('op', 'operand')
    __match_args__ = ('op', 'operand')

class Lambda(expr):
    __module__ = "ast"
    __doc__ = 'Lambda(arguments args, expr body)'
    _fields = ('args', 'body')
    __match_args__ = ('args', 'body')

class IfExp(expr):
    __module__ = "ast"
    __doc__ = 'IfExp(expr test, expr body, expr orelse)'
    _fields = ('test', 'body', 'orelse')
    __match_args__ = ('test', 'body', 'orelse')

class Dict(expr):
    __module__ = "ast"
    __doc__ = 'Dict(expr?* keys, expr* values)'
    _fields = ('keys', 'values')
    __match_args__ = ('keys', 'values')

class Set(expr):
    __module__ = "ast"
    __doc__ = 'Set(expr* elts)'
    _fields = ('elts',)
    __match_args__ = ('elts',)

class ListComp(expr):
    __module__ = "ast"
    __doc__ = 'ListComp(expr elt, comprehension* generators)'
    _fields = ('elt', 'generators')
    __match_args__ = ('elt', 'generators')

class SetComp(expr):
    __module__ = "ast"
    __doc__ = 'SetComp(expr elt, comprehension* generators)'
    _fields = ('elt', 'generators')
    __match_args__ = ('elt', 'generators')

class DictComp(expr):
    __module__ = "ast"
    __doc__ = 'DictComp(expr key, expr? value, comprehension* generators)'
    _fields = ('key', 'value', 'generators')
    __match_args__ = ('key', 'value', 'generators')
    value = None

class GeneratorExp(expr):
    __module__ = "ast"
    __doc__ = 'GeneratorExp(expr elt, comprehension* generators)'
    _fields = ('elt', 'generators')
    __match_args__ = ('elt', 'generators')

class Await(expr):
    __module__ = "ast"
    __doc__ = 'Await(expr value)'
    _fields = ('value',)
    __match_args__ = ('value',)

class Yield(expr):
    __module__ = "ast"
    __doc__ = 'Yield(expr? value)'
    _fields = ('value',)
    __match_args__ = ('value',)
    value = None

class YieldFrom(expr):
    __module__ = "ast"
    __doc__ = 'YieldFrom(expr value)'
    _fields = ('value',)
    __match_args__ = ('value',)

class Compare(expr):
    __module__ = "ast"
    __doc__ = 'Compare(expr left, cmpop* ops, expr* comparators)'
    _fields = ('left', 'ops', 'comparators')
    __match_args__ = ('left', 'ops', 'comparators')

class Call(expr):
    __module__ = "ast"
    __doc__ = 'Call(expr func, expr* args, keyword* keywords)'
    _fields = ('func', 'args', 'keywords')
    __match_args__ = ('func', 'args', 'keywords')

class FormattedValue(expr):
    __module__ = "ast"
    __doc__ = 'FormattedValue(expr value, int conversion, expr? format_spec)'
    _fields = ('value', 'conversion', 'format_spec')
    __match_args__ = ('value', 'conversion', 'format_spec')
    format_spec = None

class Interpolation(expr):
    __module__ = "ast"
    __doc__ = 'Interpolation(expr value, constant str, int conversion, expr? format_spec)'
    _fields = ('value', 'str', 'conversion', 'format_spec')
    __match_args__ = ('value', 'str', 'conversion', 'format_spec')
    format_spec = None

class JoinedStr(expr):
    __module__ = "ast"
    __doc__ = 'JoinedStr(expr* values)'
    _fields = ('values',)
    __match_args__ = ('values',)

class TemplateStr(expr):
    __module__ = "ast"
    __doc__ = 'TemplateStr(expr* values)'
    _fields = ('values',)
    __match_args__ = ('values',)

class Constant(expr):
    __module__ = "ast"
    __doc__ = 'Constant(constant value, string? kind)'
    _fields = ('value', 'kind')
    __match_args__ = ('value', 'kind')
    kind = None

class Attribute(expr):
    __module__ = "ast"
    __doc__ = 'Attribute(expr value, identifier attr, expr_context ctx)'
    _fields = ('value', 'attr', 'ctx')
    __match_args__ = ('value', 'attr', 'ctx')

class Subscript(expr):
    __module__ = "ast"
    __doc__ = 'Subscript(expr value, expr slice, expr_context ctx)'
    _fields = ('value', 'slice', 'ctx')
    __match_args__ = ('value', 'slice', 'ctx')

class Starred(expr):
    __module__ = "ast"
    __doc__ = 'Starred(expr value, expr_context ctx)'
    _fields = ('value', 'ctx')
    __match_args__ = ('value', 'ctx')

class Name(expr):
    __module__ = "ast"
    __doc__ = 'Name(identifier id, expr_context ctx)'
    _fields = ('id', 'ctx')
    __match_args__ = ('id', 'ctx')

class List(expr):
    __module__ = "ast"
    __doc__ = 'List(expr* elts, expr_context ctx)'
    _fields = ('elts', 'ctx')
    __match_args__ = ('elts', 'ctx')

class Tuple(expr):
    __module__ = "ast"
    __doc__ = 'Tuple(expr* elts, expr_context ctx)'
    _fields = ('elts', 'ctx')
    __match_args__ = ('elts', 'ctx')

class Slice(expr):
    __module__ = "ast"
    __doc__ = 'Slice(expr? lower, expr? upper, expr? step)'
    _fields = ('lower', 'upper', 'step')
    __match_args__ = ('lower', 'upper', 'step')
    lower = None
    upper = None
    step = None

class expr_context(AST):
    __module__ = "ast"
    __doc__ = 'expr_context = Load | Store | Del'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class Load(expr_context):
    __module__ = "ast"
    __doc__ = 'Load'
    _fields = ()
    __match_args__ = ()

class Store(expr_context):
    __module__ = "ast"
    __doc__ = 'Store'
    _fields = ()
    __match_args__ = ()

class Del(expr_context):
    __module__ = "ast"
    __doc__ = 'Del'
    _fields = ()
    __match_args__ = ()

class boolop(AST):
    __module__ = "ast"
    __doc__ = 'boolop = And | Or'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class And(boolop):
    __module__ = "ast"
    __doc__ = 'And'
    _fields = ()
    __match_args__ = ()

class Or(boolop):
    __module__ = "ast"
    __doc__ = 'Or'
    _fields = ()
    __match_args__ = ()

class operator(AST):
    __module__ = "ast"
    __doc__ = 'operator = Add | Sub | Mult | MatMult | Div | Mod | Pow | LShift | RShift | BitOr | BitXor | BitAnd | FloorDiv'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class Add(operator):
    __module__ = "ast"
    __doc__ = 'Add'
    _fields = ()
    __match_args__ = ()

class Sub(operator):
    __module__ = "ast"
    __doc__ = 'Sub'
    _fields = ()
    __match_args__ = ()

class Mult(operator):
    __module__ = "ast"
    __doc__ = 'Mult'
    _fields = ()
    __match_args__ = ()

class MatMult(operator):
    __module__ = "ast"
    __doc__ = 'MatMult'
    _fields = ()
    __match_args__ = ()

class Div(operator):
    __module__ = "ast"
    __doc__ = 'Div'
    _fields = ()
    __match_args__ = ()

class Mod(operator):
    __module__ = "ast"
    __doc__ = 'Mod'
    _fields = ()
    __match_args__ = ()

class Pow(operator):
    __module__ = "ast"
    __doc__ = 'Pow'
    _fields = ()
    __match_args__ = ()

class LShift(operator):
    __module__ = "ast"
    __doc__ = 'LShift'
    _fields = ()
    __match_args__ = ()

class RShift(operator):
    __module__ = "ast"
    __doc__ = 'RShift'
    _fields = ()
    __match_args__ = ()

class BitOr(operator):
    __module__ = "ast"
    __doc__ = 'BitOr'
    _fields = ()
    __match_args__ = ()

class BitXor(operator):
    __module__ = "ast"
    __doc__ = 'BitXor'
    _fields = ()
    __match_args__ = ()

class BitAnd(operator):
    __module__ = "ast"
    __doc__ = 'BitAnd'
    _fields = ()
    __match_args__ = ()

class FloorDiv(operator):
    __module__ = "ast"
    __doc__ = 'FloorDiv'
    _fields = ()
    __match_args__ = ()

class unaryop(AST):
    __module__ = "ast"
    __doc__ = 'unaryop = Invert | Not | UAdd | USub'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class Invert(unaryop):
    __module__ = "ast"
    __doc__ = 'Invert'
    _fields = ()
    __match_args__ = ()

class Not(unaryop):
    __module__ = "ast"
    __doc__ = 'Not'
    _fields = ()
    __match_args__ = ()

class UAdd(unaryop):
    __module__ = "ast"
    __doc__ = 'UAdd'
    _fields = ()
    __match_args__ = ()

class USub(unaryop):
    __module__ = "ast"
    __doc__ = 'USub'
    _fields = ()
    __match_args__ = ()

class cmpop(AST):
    __module__ = "ast"
    __doc__ = 'cmpop = Eq | NotEq | Lt | LtE | Gt | GtE | Is | IsNot | In | NotIn'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class Eq(cmpop):
    __module__ = "ast"
    __doc__ = 'Eq'
    _fields = ()
    __match_args__ = ()

class NotEq(cmpop):
    __module__ = "ast"
    __doc__ = 'NotEq'
    _fields = ()
    __match_args__ = ()

class Lt(cmpop):
    __module__ = "ast"
    __doc__ = 'Lt'
    _fields = ()
    __match_args__ = ()

class LtE(cmpop):
    __module__ = "ast"
    __doc__ = 'LtE'
    _fields = ()
    __match_args__ = ()

class Gt(cmpop):
    __module__ = "ast"
    __doc__ = 'Gt'
    _fields = ()
    __match_args__ = ()

class GtE(cmpop):
    __module__ = "ast"
    __doc__ = 'GtE'
    _fields = ()
    __match_args__ = ()

class Is(cmpop):
    __module__ = "ast"
    __doc__ = 'Is'
    _fields = ()
    __match_args__ = ()

class IsNot(cmpop):
    __module__ = "ast"
    __doc__ = 'IsNot'
    _fields = ()
    __match_args__ = ()

class In(cmpop):
    __module__ = "ast"
    __doc__ = 'In'
    _fields = ()
    __match_args__ = ()

class NotIn(cmpop):
    __module__ = "ast"
    __doc__ = 'NotIn'
    _fields = ()
    __match_args__ = ()

class comprehension(AST):
    __module__ = "ast"
    __doc__ = 'comprehension(expr target, expr iter, expr* ifs, int is_async)'
    _fields = ('target', 'iter', 'ifs', 'is_async')
    _attributes = ()
    __match_args__ = ('target', 'iter', 'ifs', 'is_async')

class excepthandler(AST):
    __module__ = "ast"
    __doc__ = 'excepthandler = ExceptHandler(expr? type, identifier? name, stmt* body)'
    _fields = ()
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ()
    end_lineno = None
    end_col_offset = None

class ExceptHandler(excepthandler):
    __module__ = "ast"
    __doc__ = 'ExceptHandler(expr? type, identifier? name, stmt* body)'
    _fields = ('type', 'name', 'body')
    __match_args__ = ('type', 'name', 'body')
    type = None
    name = None

class arguments(AST):
    __module__ = "ast"
    __doc__ = 'arguments(arg* posonlyargs, arg* args, arg? vararg, arg* kwonlyargs, expr?* kw_defaults, arg? kwarg, expr* defaults)'
    _fields = ('posonlyargs', 'args', 'vararg', 'kwonlyargs', 'kw_defaults', 'kwarg', 'defaults')
    _attributes = ()
    __match_args__ = ('posonlyargs', 'args', 'vararg', 'kwonlyargs', 'kw_defaults', 'kwarg', 'defaults')
    vararg = None
    kwarg = None

class arg(AST):
    __module__ = "ast"
    __doc__ = 'arg(identifier arg, expr? annotation, string? type_comment)'
    _fields = ('arg', 'annotation', 'type_comment')
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ('arg', 'annotation', 'type_comment')
    annotation = None
    type_comment = None
    end_lineno = None
    end_col_offset = None

class keyword(AST):
    __module__ = "ast"
    __doc__ = 'keyword(identifier? arg, expr value)'
    _fields = ('arg', 'value')
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ('arg', 'value')
    arg = None
    end_lineno = None
    end_col_offset = None

class alias(AST):
    __module__ = "ast"
    __doc__ = 'alias(identifier name, identifier? asname)'
    _fields = ('name', 'asname')
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ('name', 'asname')
    asname = None
    end_lineno = None
    end_col_offset = None

class withitem(AST):
    __module__ = "ast"
    __doc__ = 'withitem(expr context_expr, expr? optional_vars)'
    _fields = ('context_expr', 'optional_vars')
    _attributes = ()
    __match_args__ = ('context_expr', 'optional_vars')
    optional_vars = None

class match_case(AST):
    __module__ = "ast"
    __doc__ = 'match_case(pattern pattern, expr? guard, stmt* body)'
    _fields = ('pattern', 'guard', 'body')
    _attributes = ()
    __match_args__ = ('pattern', 'guard', 'body')
    guard = None

class pattern(AST):
    __module__ = "ast"
    __doc__ = 'pattern = MatchValue(expr value)\n        | MatchSingleton(constant value)\n        | MatchSequence(pattern* patterns)\n        | MatchMapping(expr* keys, pattern* patterns, identifier? rest)\n        | MatchClass(expr cls, pattern* patterns, identifier* kwd_attrs, pattern* kwd_patterns)\n        | MatchStar(identifier? name)\n        | MatchAs(pattern? pattern, identifier? name)\n        | MatchOr(pattern* patterns)'
    _fields = ()
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ()

class MatchValue(pattern):
    __module__ = "ast"
    __doc__ = 'MatchValue(expr value)'
    _fields = ('value',)
    __match_args__ = ('value',)

class MatchSingleton(pattern):
    __module__ = "ast"
    __doc__ = 'MatchSingleton(constant value)'
    _fields = ('value',)
    __match_args__ = ('value',)

class MatchSequence(pattern):
    __module__ = "ast"
    __doc__ = 'MatchSequence(pattern* patterns)'
    _fields = ('patterns',)
    __match_args__ = ('patterns',)

class MatchMapping(pattern):
    __module__ = "ast"
    __doc__ = 'MatchMapping(expr* keys, pattern* patterns, identifier? rest)'
    _fields = ('keys', 'patterns', 'rest')
    __match_args__ = ('keys', 'patterns', 'rest')
    rest = None

class MatchClass(pattern):
    __module__ = "ast"
    __doc__ = 'MatchClass(expr cls, pattern* patterns, identifier* kwd_attrs, pattern* kwd_patterns)'
    _fields = ('cls', 'patterns', 'kwd_attrs', 'kwd_patterns')
    __match_args__ = ('cls', 'patterns', 'kwd_attrs', 'kwd_patterns')

class MatchStar(pattern):
    __module__ = "ast"
    __doc__ = 'MatchStar(identifier? name)'
    _fields = ('name',)
    __match_args__ = ('name',)
    name = None

class MatchAs(pattern):
    __module__ = "ast"
    __doc__ = 'MatchAs(pattern? pattern, identifier? name)'
    _fields = ('pattern', 'name')
    __match_args__ = ('pattern', 'name')
    pattern = None
    name = None

class MatchOr(pattern):
    __module__ = "ast"
    __doc__ = 'MatchOr(pattern* patterns)'
    _fields = ('patterns',)
    __match_args__ = ('patterns',)

class type_ignore(AST):
    __module__ = "ast"
    __doc__ = 'type_ignore = TypeIgnore(int lineno, string tag)'
    _fields = ()
    _attributes = ()
    __match_args__ = ()

class TypeIgnore(type_ignore):
    __module__ = "ast"
    __doc__ = 'TypeIgnore(int lineno, string tag)'
    _fields = ('lineno', 'tag')
    __match_args__ = ('lineno', 'tag')

class type_param(AST):
    __module__ = "ast"
    __doc__ = 'type_param = TypeVar(identifier name, expr? bound, expr? default_value)\n           | ParamSpec(identifier name, expr? default_value)\n           | TypeVarTuple(identifier name, expr? default_value)'
    _fields = ()
    _attributes = ('lineno', 'col_offset', 'end_lineno', 'end_col_offset')
    __match_args__ = ()

class TypeVar(type_param):
    __module__ = "ast"
    __doc__ = 'TypeVar(identifier name, expr? bound, expr? default_value)'
    _fields = ('name', 'bound', 'default_value')
    __match_args__ = ('name', 'bound', 'default_value')
    bound = None
    default_value = None

class ParamSpec(type_param):
    __module__ = "ast"
    __doc__ = 'ParamSpec(identifier name, expr? default_value)'
    _fields = ('name', 'default_value')
    __match_args__ = ('name', 'default_value')
    default_value = None

class TypeVarTuple(type_param):
    __module__ = "ast"
    __doc__ = 'TypeVarTuple(identifier name, expr? default_value)'
    _fields = ('name', 'default_value')
    __match_args__ = ('name', 'default_value')
    default_value = None


# _field_types, once every class exists
Module._field_types = {'body': list[stmt], 'type_ignores': list[type_ignore]}
Interactive._field_types = {'body': list[stmt]}
Expression._field_types = {'body': expr}
FunctionType._field_types = {'argtypes': list[expr], 'returns': expr}
FunctionDef._field_types = {'name': str, 'args': arguments, 'body': list[stmt], 'decorator_list': list[expr], 'returns': expr | None, 'type_comment': str | None, 'type_params': list[type_param]}
AsyncFunctionDef._field_types = {'name': str, 'args': arguments, 'body': list[stmt], 'decorator_list': list[expr], 'returns': expr | None, 'type_comment': str | None, 'type_params': list[type_param]}
ClassDef._field_types = {'name': str, 'bases': list[expr], 'keywords': list[keyword], 'body': list[stmt], 'decorator_list': list[expr], 'type_params': list[type_param]}
Return._field_types = {'value': expr | None}
Delete._field_types = {'targets': list[expr]}
Assign._field_types = {'targets': list[expr], 'value': expr, 'type_comment': str | None}
TypeAlias._field_types = {'name': expr, 'type_params': list[type_param], 'value': expr}
AugAssign._field_types = {'target': expr, 'op': operator, 'value': expr}
AnnAssign._field_types = {'target': expr, 'annotation': expr, 'value': expr | None, 'simple': int}
For._field_types = {'target': expr, 'iter': expr, 'body': list[stmt], 'orelse': list[stmt], 'type_comment': str | None}
AsyncFor._field_types = {'target': expr, 'iter': expr, 'body': list[stmt], 'orelse': list[stmt], 'type_comment': str | None}
While._field_types = {'test': expr, 'body': list[stmt], 'orelse': list[stmt]}
If._field_types = {'test': expr, 'body': list[stmt], 'orelse': list[stmt]}
With._field_types = {'items': list[withitem], 'body': list[stmt], 'type_comment': str | None}
AsyncWith._field_types = {'items': list[withitem], 'body': list[stmt], 'type_comment': str | None}
Match._field_types = {'subject': expr, 'cases': list[match_case]}
Raise._field_types = {'exc': expr | None, 'cause': expr | None}
Try._field_types = {'body': list[stmt], 'handlers': list[excepthandler], 'orelse': list[stmt], 'finalbody': list[stmt]}
TryStar._field_types = {'body': list[stmt], 'handlers': list[excepthandler], 'orelse': list[stmt], 'finalbody': list[stmt]}
Assert._field_types = {'test': expr, 'msg': expr | None}
Import._field_types = {'names': list[alias], 'is_lazy': int | None}
ImportFrom._field_types = {'module': str | None, 'names': list[alias], 'level': int | None, 'is_lazy': int | None}
Global._field_types = {'names': list[str]}
Nonlocal._field_types = {'names': list[str]}
Expr._field_types = {'value': expr}
Pass._field_types = {}
Break._field_types = {}
Continue._field_types = {}
BoolOp._field_types = {'op': boolop, 'values': list[expr]}
NamedExpr._field_types = {'target': expr, 'value': expr}
BinOp._field_types = {'left': expr, 'op': operator, 'right': expr}
UnaryOp._field_types = {'op': unaryop, 'operand': expr}
Lambda._field_types = {'args': arguments, 'body': expr}
IfExp._field_types = {'test': expr, 'body': expr, 'orelse': expr}
Dict._field_types = {'keys': list[expr], 'values': list[expr]}
Set._field_types = {'elts': list[expr]}
ListComp._field_types = {'elt': expr, 'generators': list[comprehension]}
SetComp._field_types = {'elt': expr, 'generators': list[comprehension]}
DictComp._field_types = {'key': expr, 'value': expr | None, 'generators': list[comprehension]}
GeneratorExp._field_types = {'elt': expr, 'generators': list[comprehension]}
Await._field_types = {'value': expr}
Yield._field_types = {'value': expr | None}
YieldFrom._field_types = {'value': expr}
Compare._field_types = {'left': expr, 'ops': list[cmpop], 'comparators': list[expr]}
Call._field_types = {'func': expr, 'args': list[expr], 'keywords': list[keyword]}
FormattedValue._field_types = {'value': expr, 'conversion': int, 'format_spec': expr | None}
Interpolation._field_types = {'value': expr, 'str': object, 'conversion': int, 'format_spec': expr | None}
JoinedStr._field_types = {'values': list[expr]}
TemplateStr._field_types = {'values': list[expr]}
Constant._field_types = {'value': object, 'kind': str | None}
Attribute._field_types = {'value': expr, 'attr': str, 'ctx': expr_context}
Subscript._field_types = {'value': expr, 'slice': expr, 'ctx': expr_context}
Starred._field_types = {'value': expr, 'ctx': expr_context}
Name._field_types = {'id': str, 'ctx': expr_context}
List._field_types = {'elts': list[expr], 'ctx': expr_context}
Tuple._field_types = {'elts': list[expr], 'ctx': expr_context}
Slice._field_types = {'lower': expr | None, 'upper': expr | None, 'step': expr | None}
Load._field_types = {}
Store._field_types = {}
Del._field_types = {}
And._field_types = {}
Or._field_types = {}
Add._field_types = {}
Sub._field_types = {}
Mult._field_types = {}
MatMult._field_types = {}
Div._field_types = {}
Mod._field_types = {}
Pow._field_types = {}
LShift._field_types = {}
RShift._field_types = {}
BitOr._field_types = {}
BitXor._field_types = {}
BitAnd._field_types = {}
FloorDiv._field_types = {}
Invert._field_types = {}
Not._field_types = {}
UAdd._field_types = {}
USub._field_types = {}
Eq._field_types = {}
NotEq._field_types = {}
Lt._field_types = {}
LtE._field_types = {}
Gt._field_types = {}
GtE._field_types = {}
Is._field_types = {}
IsNot._field_types = {}
In._field_types = {}
NotIn._field_types = {}
comprehension._field_types = {'target': expr, 'iter': expr, 'ifs': list[expr], 'is_async': int}
ExceptHandler._field_types = {'type': expr | None, 'name': str | None, 'body': list[stmt]}
arguments._field_types = {'posonlyargs': list[arg], 'args': list[arg], 'vararg': arg | None, 'kwonlyargs': list[arg], 'kw_defaults': list[expr], 'kwarg': arg | None, 'defaults': list[expr]}
arg._field_types = {'arg': str, 'annotation': expr | None, 'type_comment': str | None}
keyword._field_types = {'arg': str | None, 'value': expr}
alias._field_types = {'name': str, 'asname': str | None}
withitem._field_types = {'context_expr': expr, 'optional_vars': expr | None}
match_case._field_types = {'pattern': pattern, 'guard': expr | None, 'body': list[stmt]}
MatchValue._field_types = {'value': expr}
MatchSingleton._field_types = {'value': object}
MatchSequence._field_types = {'patterns': list[pattern]}
MatchMapping._field_types = {'keys': list[expr], 'patterns': list[pattern], 'rest': str | None}
MatchClass._field_types = {'cls': expr, 'patterns': list[pattern], 'kwd_attrs': list[str], 'kwd_patterns': list[pattern]}
MatchStar._field_types = {'name': str | None}
MatchAs._field_types = {'pattern': pattern | None, 'name': str | None}
MatchOr._field_types = {'patterns': list[pattern]}
TypeIgnore._field_types = {'lineno': int, 'tag': str}
TypeVar._field_types = {'name': str, 'bound': expr | None, 'default_value': expr | None}
ParamSpec._field_types = {'name': str, 'default_value': expr | None}
TypeVarTuple._field_types = {'name': str, 'default_value': expr | None}

_ABSTRACT = {AST, mod, stmt, expr, expr_context, boolop, operator, unaryop, cmpop, excepthandler, pattern, type_ignore, type_param}
_CLASSES = {'mod': mod, 'Module': Module, 'Interactive': Interactive, 'Expression': Expression, 'FunctionType': FunctionType, 'stmt': stmt, 'FunctionDef': FunctionDef, 'AsyncFunctionDef': AsyncFunctionDef, 'ClassDef': ClassDef, 'Return': Return, 'Delete': Delete, 'Assign': Assign, 'TypeAlias': TypeAlias, 'AugAssign': AugAssign, 'AnnAssign': AnnAssign, 'For': For, 'AsyncFor': AsyncFor, 'While': While, 'If': If, 'With': With, 'AsyncWith': AsyncWith, 'Match': Match, 'Raise': Raise, 'Try': Try, 'TryStar': TryStar, 'Assert': Assert, 'Import': Import, 'ImportFrom': ImportFrom, 'Global': Global, 'Nonlocal': Nonlocal, 'Expr': Expr, 'Pass': Pass, 'Break': Break, 'Continue': Continue, 'expr': expr, 'BoolOp': BoolOp, 'NamedExpr': NamedExpr, 'BinOp': BinOp, 'UnaryOp': UnaryOp, 'Lambda': Lambda, 'IfExp': IfExp, 'Dict': Dict, 'Set': Set, 'ListComp': ListComp, 'SetComp': SetComp, 'DictComp': DictComp, 'GeneratorExp': GeneratorExp, 'Await': Await, 'Yield': Yield, 'YieldFrom': YieldFrom, 'Compare': Compare, 'Call': Call, 'FormattedValue': FormattedValue, 'Interpolation': Interpolation, 'JoinedStr': JoinedStr, 'TemplateStr': TemplateStr, 'Constant': Constant, 'Attribute': Attribute, 'Subscript': Subscript, 'Starred': Starred, 'Name': Name, 'List': List, 'Tuple': Tuple, 'Slice': Slice, 'expr_context': expr_context, 'Load': Load, 'Store': Store, 'Del': Del, 'boolop': boolop, 'And': And, 'Or': Or, 'operator': operator, 'Add': Add, 'Sub': Sub, 'Mult': Mult, 'MatMult': MatMult, 'Div': Div, 'Mod': Mod, 'Pow': Pow, 'LShift': LShift, 'RShift': RShift, 'BitOr': BitOr, 'BitXor': BitXor, 'BitAnd': BitAnd, 'FloorDiv': FloorDiv, 'unaryop': unaryop, 'Invert': Invert, 'Not': Not, 'UAdd': UAdd, 'USub': USub, 'cmpop': cmpop, 'Eq': Eq, 'NotEq': NotEq, 'Lt': Lt, 'LtE': LtE, 'Gt': Gt, 'GtE': GtE, 'Is': Is, 'IsNot': IsNot, 'In': In, 'NotIn': NotIn, 'comprehension': comprehension, 'excepthandler': excepthandler, 'ExceptHandler': ExceptHandler, 'arguments': arguments, 'arg': arg, 'keyword': keyword, 'alias': alias, 'withitem': withitem, 'match_case': match_case, 'pattern': pattern, 'MatchValue': MatchValue, 'MatchSingleton': MatchSingleton, 'MatchSequence': MatchSequence, 'MatchMapping': MatchMapping, 'MatchClass': MatchClass, 'MatchStar': MatchStar, 'MatchAs': MatchAs, 'MatchOr': MatchOr, 'type_ignore': type_ignore, 'TypeIgnore': TypeIgnore, 'type_param': type_param, 'TypeVar': TypeVar, 'ParamSpec': ParamSpec, 'TypeVarTuple': TypeVarTuple}


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
        k = tuple((f, _kind_of(ft[f]) if f in ft else "value") for f in cls._fields) \
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
