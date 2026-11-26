"""The lines of source files, read once and kept: tracebacks and warnings
show a program's lines from here."""

__all__ = ["getline", "clearcache", "checkcache", "lazycache"]

# filename -> (size, mtime, lines, fullname); a lazily loaded entry is a
# 1-tuple holding a function that gives the source
cache = {}


def clearcache():
    """Forget every file's lines."""
    cache.clear()


def getline(filename, lineno, module_globals=None):
    """A file's line LINENO with its newline, or '' when there is none."""
    lines = getlines(filename, module_globals)
    if 1 <= lineno <= len(lines):
        return lines[lineno - 1]
    return ""


def getlines(filename, module_globals=None):
    """A file's lines, read now unless kept."""
    if filename in cache:
        entry = cache[filename]
        if len(entry) != 1:
            return cache[filename][2]
    try:
        return updatecache(filename, module_globals)
    except MemoryError:
        clearcache()
        return []


def checkcache(filename=None):
    """Forget the lines of files changed since they were read (here, a file
    no longer readable)."""
    if filename is None:
        filenames = list(cache.keys())
    elif filename in cache:
        filenames = [filename]
    else:
        return
    for name in filenames:
        entry = cache[name]
        if len(entry) == 1:
            continue
        try:
            with open(entry[3], encoding="utf-8") as fp:
                fp.read(0)
        except (OSError, ValueError):
            cache.pop(name, None)


def updatecache(filename, module_globals=None):
    """Read a file's lines again and keep them; [] when it cannot be read."""
    if filename in cache:
        if len(cache[filename]) != 1:
            cache.pop(filename, None)
    if not filename or (filename.startswith("<") and filename.endswith(">")):
        return []
    if lazycache(filename, module_globals):
        try:
            data = cache[filename][0]()
        except (ImportError, OSError):
            pass
        else:
            if data is None:
                return []
            lines = [line + "\n" for line in data.splitlines()]
            cache[filename] = (len(data), None, lines, filename)
            return lines
    try:
        with open(filename, encoding="utf-8") as fp:
            lines = fp.readlines()
    except (OSError, UnicodeDecodeError, SyntaxError, ValueError):
        return []
    if not lines:
        lines = ["\n"]
    elif not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    size = sum(len(line) for line in lines)
    cache[filename] = (size, None, lines, filename)
    return lines


def lazycache(filename, module_globals):
    """Keep a way to get a module's source from its loader, for a file that
    has no lines on disk; true when the source can be had that way."""
    if filename in cache:
        if len(cache[filename]) == 1:
            return True
        return False
    if not filename or (filename.startswith("<") and filename.endswith(">")):
        return False
    if module_globals and "__name__" in module_globals:
        spec = module_globals.get("__spec__")
        name = getattr(spec, "name", None) or module_globals["__name__"]
        loader = getattr(spec, "loader", None)
        if loader is None:
            loader = module_globals.get("__loader__")
        get_source = getattr(loader, "get_source", None)
        if name and get_source:
            def get_lines(name=name, *args, **kwargs):
                return get_source(name, *args, **kwargs)
            cache[filename] = (get_lines,)
            return True
    return False
