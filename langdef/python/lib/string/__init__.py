"""String constants, word capitalization, $-substitution templates and
str.format's machinery as a class.

whitespace, ascii_lowercase, ascii_uppercase, ascii_letters, digits,
hexdigits, octdigits, punctuation and printable are the ASCII character
classes; capwords, Template and Formatter are the helpers.
"""

__all__ = ["ascii_letters", "ascii_lowercase", "ascii_uppercase", "capwords",
           "digits", "hexdigits", "octdigits", "printable", "punctuation",
           "whitespace", "Formatter", "Template"]

import _string

whitespace = ' \t\n\r\v\f'
ascii_lowercase = 'abcdefghijklmnopqrstuvwxyz'
ascii_uppercase = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
ascii_letters = ascii_lowercase + ascii_uppercase
digits = '0123456789'
hexdigits = digits + 'abcdef' + 'ABCDEF'
octdigits = '01234567'
punctuation = r"""!"#$%&'()*+,-./:;<=>?@[\]^_`{|}~"""
printable = digits + ascii_letters + punctuation + whitespace


def capwords(s, sep=None):
    """capwords(s [,sep]) -> string

    Each word of s capitalized, the words split by sep (by runs of
    whitespace when sep is None, the result then joined by single spaces).
    """
    return (sep or ' ').join(map(str.capitalize, s.split(sep)))


# the mapping substitute() reads when it is given none
_no_mapping = {}


class _UncompiledPattern:
    # Template.pattern until the class compiles it: reading it compiles it
    def __get__(self, instance, cls=None):
        if cls is None:
            return self
        return cls._compile_pattern()


_UncompiledPattern = _UncompiledPattern()


class Template:
    """A string with $-placeholders, substituted from a mapping."""

    delimiter = '$'
    # ASCII identifiers only, whatever the flags ((?a:...)): [a-z] with
    # IGNORECASE alone would also match some non-ASCII letters
    idpattern = r'(?a:[_a-z][_a-z0-9]*)'
    braceidpattern = None
    flags = None

    pattern = _UncompiledPattern

    def __init_subclass__(cls):
        super().__init_subclass__()
        cls._compile_pattern()

    @classmethod
    def _compile_pattern(cls):
        # the class's own pattern: a compiled one as it is, a string compiled,
        # else the one its delimiter and identifier patterns make; compiled
        # with re.VERBOSE and the class's flags (re.IGNORECASE when None),
        # then kept on the class
        import re
        pattern = cls.__dict__.get('pattern', _UncompiledPattern)
        if isinstance(pattern, re.Pattern):
            return pattern
        if pattern is _UncompiledPattern:
            delim = re.escape(cls.delimiter)
            bid = cls.braceidpattern or cls.idpattern
            pattern = (delim + r"(?:"
                       + r"(?P<escaped>" + delim + r")|"
                       + r"(?P<named>" + cls.idpattern + r")|"
                       + r"{(?P<braced>" + bid + r")}|"
                       + r"(?P<invalid>))")
        if cls.flags is None:
            cls.flags = re.IGNORECASE
        compiled = cls.pattern = re.compile(pattern, cls.flags | re.VERBOSE)
        return compiled

    def __init__(self, template):
        self.template = template

    def _invalid(self, mo):
        # the line and column (from 1) of a delimiter nothing valid follows
        i = mo.start('invalid')
        lines = self.template[:i].splitlines(keepends=True)
        if not lines:
            colno = 1
            lineno = 1
        else:
            colno = i - len(''.join(lines[:-1]))
            lineno = len(lines)
        raise ValueError('Invalid placeholder in string: line %d, col %d' %
                         (lineno, colno))

    def substitute(self, mapping=_no_mapping, /, **kws):
        if mapping is _no_mapping:
            mapping = kws
        elif kws:
            from collections import ChainMap
            mapping = ChainMap(kws, mapping)

        def convert(mo):
            named = mo.group('named') or mo.group('braced')
            if named is not None:
                return str(mapping[named])
            if mo.group('escaped') is not None:
                return self.delimiter
            if mo.group('invalid') is not None:
                self._invalid(mo)
            raise ValueError('Unrecognized named group in pattern',
                             self.pattern)
        return self.pattern.sub(convert, self.template)

    def safe_substitute(self, mapping=_no_mapping, /, **kws):
        if mapping is _no_mapping:
            mapping = kws
        elif kws:
            from collections import ChainMap
            mapping = ChainMap(kws, mapping)

        def convert(mo):
            named = mo.group('named') or mo.group('braced')
            if named is not None:
                try:
                    return str(mapping[named])
                except KeyError:
                    return mo.group()
            if mo.group('escaped') is not None:
                return self.delimiter
            if mo.group('invalid') is not None:
                return mo.group()
            raise ValueError('Unrecognized named group in pattern',
                             self.pattern)
        return self.pattern.sub(convert, self.template)

    def is_valid(self):
        for mo in self.pattern.finditer(self.template):
            if mo.group('invalid') is not None:
                return False
            if (mo.group('named') is None
                    and mo.group('braced') is None
                    and mo.group('escaped') is None):
                raise ValueError('Unrecognized named group in pattern',
                                 self.pattern)
        return True

    def get_identifiers(self):
        ids = []
        for mo in self.pattern.finditer(self.template):
            named = mo.group('named') or mo.group('braced')
            if named is not None and named not in ids:
                ids.append(named)
            elif (named is None
                    and mo.group('invalid') is None
                    and mo.group('escaped') is None):
                raise ValueError('Unrecognized named group in pattern',
                                 self.pattern)
        return ids


class Formatter:
    """str.format's formatting as overridable methods (PEP 3101)."""

    def format(self, format_string, /, *args, **kwargs):
        return self.vformat(format_string, args, kwargs)

    def vformat(self, format_string, args, kwargs):
        used_args = set()
        result, _ = self._vformat(format_string, args, kwargs, used_args, 2)
        self.check_unused_args(used_args, args, kwargs)
        return result

    def _vformat(self, format_string, args, kwargs, used_args, recursion_depth,
                 auto_arg_index=0):
        # each part of the format in turn: its literal text, then its field
        # (numbered automatically when its name is empty), converted, its
        # spec formatted the same way one level down, the value formatted
        if recursion_depth < 0:
            raise ValueError('Max string recursion exceeded')
        result = []
        for literal_text, field_name, format_spec, conversion in self.parse(format_string):
            if literal_text:
                result.append(literal_text)
            if field_name is None:
                continue
            field_first, _ = _string.formatter_field_name_split(field_name)
            if field_first == '':
                if auto_arg_index is False:
                    raise ValueError('cannot switch from manual field '
                                     'specification to automatic field '
                                     'numbering')
                field_name = str(auto_arg_index) + field_name
                auto_arg_index += 1
            elif isinstance(field_first, int):
                if auto_arg_index:
                    raise ValueError('cannot switch from automatic field '
                                     'numbering to manual field '
                                     'specification')
                auto_arg_index = False
            obj, arg_used = self.get_field(field_name, args, kwargs)
            used_args.add(arg_used)
            obj = self.convert_field(obj, conversion)
            format_spec, auto_arg_index = self._vformat(
                format_spec, args, kwargs,
                used_args, recursion_depth - 1,
                auto_arg_index=auto_arg_index)
            result.append(self.format_field(obj, format_spec))
        return ''.join(result), auto_arg_index

    def get_value(self, key, args, kwargs):
        if isinstance(key, int):
            return args[key]
        return kwargs[key]

    def check_unused_args(self, used_args, args, kwargs):
        pass

    def format_field(self, value, format_spec):
        return format(value, format_spec)

    def convert_field(self, value, conversion):
        if conversion is None:
            return value
        if conversion == 's':
            return str(value)
        if conversion == 'r':
            return repr(value)
        if conversion == 'a':
            return ascii(value)
        raise ValueError("Unknown conversion specifier {0!s}".format(conversion))

    def parse(self, format_string):
        """The parts of format_string, each a tuple (literal_text,
        field_name, format_spec, conversion); field_name is None for
        literal text alone, else the field to look up and format.
        """
        return _string.formatter_parser(format_string)

    def get_field(self, field_name, args, kwargs):
        """The object a field name such as "0.name" or "lookup[3]" names,
        its first part read by get_value(), and that first part.
        """
        first, rest = _string.formatter_field_name_split(field_name)
        obj = self.get_value(first, args, kwargs)
        for is_attr, i in rest:
            if is_attr:
                obj = getattr(obj, i)
            else:
                obj = obj[i]
        return obj, first
