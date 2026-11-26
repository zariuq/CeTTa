"""Template string literals (t-strings): their two types, and helpers."""

t = t"{0}"
Template = type(t)
Interpolation = type(t.interpolations[0])
del t


def convert(obj, /, conversion):
    """obj converted as a replacement field's conversion converts it."""
    if conversion is None:
        return obj
    if conversion == 'r':
        return repr(obj)
    if conversion == 's':
        return str(obj)
    if conversion == 'a':
        return ascii(obj)
    raise ValueError(f'invalid conversion specifier: {conversion}')


def _template_unpickle(*args):
    # a template made again from its strings and its interpolations, taken
    # in turn (the longer's last ones alone)
    if len(args) != 2:
        raise ValueError('Template expects tuple of length 2 to unpickle')
    strings, interpolations = args
    strings = list(strings)
    interpolations = list(interpolations)
    parts = []
    for i in range(max(len(strings), len(interpolations))):
        string = strings[i] if i < len(strings) else None
        interpolation = interpolations[i] if i < len(interpolations) else None
        if string is not None:
            parts.append(string)
        if interpolation is not None:
            parts.append(interpolation)
    return Template(*parts)
