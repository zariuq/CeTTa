"""Warnings: the filters that decide what becomes of a warning, and how a
warning is shown.  warn() and warn_explicit() are the runtime's own (the
_warnings module, as CPython's are written in C); the rest is here."""

import sys

__all__ = [
    "warn",
    "warn_explicit",
    "showwarning",
    "formatwarning",
    "filterwarnings",
    "simplefilter",
    "resetwarnings",
    "catch_warnings",
    "deprecated",
]

from _warnings import (
    _acquire_lock,
    _defaultaction as defaultaction,
    _filters_mutated_lock_held,
    _onceregistry as onceregistry,
    _release_lock,
    _warnings_context,
    filters,
    warn,
    warn_explicit,
)

_use_context = False


class _Lock:
    def __enter__(self):
        _acquire_lock()
        return self

    def __exit__(self, *args):
        _release_lock()


_lock = _Lock()


def _get_filters():
    """The filters now in force."""
    return sys.modules["warnings"].filters


def showwarning(message, category, filename, lineno, file=None, line=None):
    """Write a warning to a file (sys.stderr by default); may be replaced."""
    msg = WarningMessage(message, category, filename, lineno, file, line)
    _showwarnmsg_impl(msg)


def formatwarning(message, category, filename, lineno, line=None):
    """A warning as text, the standard way."""
    msg = WarningMessage(message, category, filename, lineno, None, line)
    return _formatwarnmsg_impl(msg)


def _showwarnmsg_impl(msg):
    file = msg.file
    if file is None:
        file = sys.stderr
        if file is None:
            return
    text = _formatwarnmsg(msg)
    try:
        file.write(text)
    except OSError:
        pass


def _formatwarnmsg_impl(msg):
    category = msg.category.__name__
    s = f"{msg.filename}:{msg.lineno}: {category}: {msg.message}\n"
    if msg.line is None:
        try:
            import linecache
            line = linecache.getline(msg.filename, msg.lineno)
        except Exception:
            line = None
    else:
        line = msg.line
    if line:
        line = line.strip()
        s += "  %s\n" % line
    return s


_showwarning_orig = showwarning


def _showwarnmsg(msg):
    """Show a warning through showwarning() when it was replaced, else the
    standard way."""
    try:
        sw = sys.modules["warnings"].showwarning
    except AttributeError:
        pass
    else:
        if sw is not _showwarning_orig:
            if not callable(sw):
                raise TypeError("warnings.showwarning() must be set to a "
                                "function or method")
            sw(msg.message, msg.category, msg.filename, msg.lineno,
               msg.file, msg.line)
            return
    sys.modules["warnings"]._showwarnmsg_impl(msg)


_formatwarning_orig = formatwarning


def _formatwarnmsg(msg):
    """A warning as text through formatwarning() when it was replaced, else
    the standard way."""
    try:
        fw = sys.modules["warnings"].formatwarning
    except AttributeError:
        pass
    else:
        if fw is not _formatwarning_orig:
            return fw(msg.message, msg.category,
                      msg.filename, msg.lineno, msg.line)
    return sys.modules["warnings"]._formatwarnmsg_impl(msg)


def filterwarnings(action, message="", category=Warning, module="", lineno=0,
                   append=False):
    """Put a filter first among the filters (last with append): ACTION for the
    warnings of CATEGORY whose text matches the regular expression MESSAGE,
    raised in a module matching MODULE, at line LINENO (0: any)."""
    if action not in {"error", "ignore", "always", "all", "default", "module", "once"}:
        raise ValueError(f"invalid action: {action!r}")
    if not isinstance(message, str):
        raise TypeError("message must be a string")
    if not isinstance(category, type) or not issubclass(category, Warning):
        raise TypeError("category must be a Warning subclass")
    if not isinstance(module, str):
        raise TypeError("module must be a string")
    if not isinstance(lineno, int):
        raise TypeError("lineno must be an int")
    if lineno < 0:
        raise ValueError("lineno must be an int >= 0")
    if message or module:
        import re
    if message:
        message = re.compile(message, re.I)
    else:
        message = None
    if module:
        module = re.compile(module)
    else:
        module = None
    _add_filter(action, message, category, module, lineno, append=append)


def simplefilter(action, category=Warning, lineno=0, append=False):
    """Put a filter for every message and module first among the filters
    (last with append)."""
    if action not in {"error", "ignore", "always", "all", "default", "module", "once"}:
        raise ValueError(f"invalid action: {action!r}")
    if not isinstance(lineno, int):
        raise TypeError("lineno must be an int")
    if lineno < 0:
        raise ValueError("lineno must be an int >= 0")
    _add_filter(action, None, category, None, lineno, append=append)


def _filters_mutated():
    with _lock:
        _filters_mutated_lock_held()


def _add_filter(*item, append):
    with _lock:
        filters = _get_filters()
        if not append:
            try:
                filters.remove(item)
            except ValueError:
                pass
            filters.insert(0, item)
        else:
            if item not in filters:
                filters.append(item)
        _filters_mutated_lock_held()


def resetwarnings():
    """Remove every filter."""
    with _lock:
        del _get_filters()[:]
        _filters_mutated_lock_held()


class WarningMessage(object):

    _WARNING_DETAILS = ("message", "category", "filename", "lineno", "file",
                        "line", "source", "module")

    def __init__(self, message, category, filename, lineno, file=None,
                 line=None, source=None, module=None):
        self.message = message
        self.category = category
        self.filename = filename
        self.lineno = lineno
        self.file = file
        self.line = line
        self.source = source
        self.module = module
        self._category_name = category.__name__ if category else None

    def __str__(self):
        return ("{message : %r, category : %r, module : %r, "
                "filename : %r, lineno : %s, line : %r}" % (
                    self.message, self._category_name, self.module,
                    self.filename, self.lineno, self.line))

    def __repr__(self):
        return f"<{type(self).__qualname__} {self}>"


class catch_warnings(object):
    """A context manager that saves the filters and showwarning() when it is
    entered and puts them back when it is left; with record=True, the
    warnings shown meanwhile are kept, as WarningMessage objects, in the list
    it gives; with action, simplefilter(action, category, lineno, append) is
    called as it is entered."""

    def __init__(self, *, record=False, module=None,
                 action=None, category=Warning, lineno=0, append=False):
        self._record = record
        self._module = sys.modules["warnings"] if module is None else module
        self._entered = False
        if action is None:
            self._filter = None
        else:
            self._filter = (action, category, lineno, append)

    def __repr__(self):
        args = []
        if self._record:
            args.append("record=True")
        if self._module is not sys.modules["warnings"]:
            args.append("module=%r" % self._module)
        name = type(self).__name__
        return "%s(%s)" % (name, ", ".join(args))

    def __enter__(self):
        if self._entered:
            raise RuntimeError("Cannot enter %r twice" % self)
        self._entered = True
        with _lock:
            self._filters = self._module.filters
            self._module.filters = self._filters[:]
            self._showwarnmsg_impl = self._module._showwarnmsg_impl
            self._showwarning = self._module.showwarning
            self._module._filters_mutated_lock_held()
            if self._record:
                log = []
                self._module._showwarnmsg_impl = log.append
                self._module.showwarning = self._module._showwarning_orig
            else:
                log = None
        if self._filter is not None:
            self._module.simplefilter(*self._filter)
        return log

    def __exit__(self, *exc_info):
        if not self._entered:
            raise RuntimeError("Cannot exit %r without entering first" % self)
        with _lock:
            self._module.filters = self._filters
            self._module._showwarnmsg_impl = self._showwarnmsg_impl
            self._module.showwarning = self._showwarning
            self._module._filters_mutated_lock_held()


class deprecated:
    """Mark a class or function deprecated (PEP 702): its use warns with
    CATEGORY (DeprecationWarning; None: no warning) at STACKLEVEL, and its
    __deprecated__ attribute holds the message."""

    def __init__(self, message, /, *, category=DeprecationWarning, stacklevel=1):
        if not isinstance(message, str):
            raise TypeError(
                f"Expected an object of type str for 'message', not {type(message).__name__!r}"
            )
        self.message = message
        self.category = category
        self.stacklevel = stacklevel

    def __call__(self, arg, /):
        msg = self.message
        category = self.category
        stacklevel = self.stacklevel
        if category is None:
            arg.__deprecated__ = msg
            return arg
        elif isinstance(arg, type):
            import functools
            from types import MethodType

            original_new = arg.__new__

            @functools.wraps(original_new)
            def __new__(cls, /, *args, **kwargs):
                if cls is arg:
                    warn(msg, category=category, stacklevel=stacklevel + 1)
                if original_new is not object.__new__:
                    return original_new(cls, *args, **kwargs)
                elif cls.__init__ is object.__init__ and (args or kwargs):
                    raise TypeError(f"{cls.__name__}() takes no arguments")
                else:
                    return original_new(cls)

            arg.__new__ = staticmethod(__new__)
            if "__init_subclass__" in arg.__dict__:
                original_init_subclass = arg.__init_subclass__
                if isinstance(original_init_subclass, MethodType):
                    original_init_subclass = original_init_subclass.__func__

                @functools.wraps(original_init_subclass)
                def __init_subclass__(*args, **kwargs):
                    warn(msg, category=category, stacklevel=stacklevel + 1)
                    return original_init_subclass(*args, **kwargs)
            else:
                def __init_subclass__(cls, *args, **kwargs):
                    warn(msg, category=category, stacklevel=stacklevel + 1)
                    return super(arg, cls).__init_subclass__(*args, **kwargs)

            arg.__init_subclass__ = classmethod(__init_subclass__)
            arg.__deprecated__ = __new__.__deprecated__ = msg
            __init_subclass__.__deprecated__ = msg
            return arg
        elif callable(arg):
            import functools
            import inspect

            @functools.wraps(arg)
            def wrapper(*args, **kwargs):
                warn(msg, category=category, stacklevel=stacklevel + 1)
                return arg(*args, **kwargs)

            if inspect.iscoroutinefunction(arg):
                wrapper = inspect.markcoroutinefunction(wrapper)
            arg.__deprecated__ = wrapper.__deprecated__ = msg
            return wrapper
        else:
            raise TypeError(
                "@deprecated decorator with non-None category must be applied to "
                f"a class or callable, not {arg!r}"
            )
