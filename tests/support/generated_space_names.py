"""Outputs compared up to a consistent renaming of generated names.

CeTTa generates two kinds of nominal name: a space new-space makes prints as
&space(N), and a callable a PeTTa lambda makes prints as lambda_N.  Which
numeral a name is given is not specified (under the counter name supply, a
clock, or a share's prefix code and a clock, dot-separated, behind a block's
32 hexadecimal digits beyond the root block; under the unforgeable one, 32
hexadecimal digits): two outputs that differ only in the names they were
given agree.
Each side is renamed on its own, and each kind apart, every distinct name in
the order it first appears, so a name keeps one spelling throughout an
output, two names never merge, and a space is never confused with a
callable.  A lambda_N is a name only as a whole token: a symbol that merely
contains one, such as my_lambda_1, lambda_1x or lambda_0.x, is left as it
is.  Renaming is idempotent and commutes with taking a prefix of an output
that ends between tokens.
"""
import re

NUMERAL = r"(?:[0-9a-f]{32}(?:\.[0-9]+)*|[0-9]+(?:\.[0-9]+)*)"
# The characters that end a token in printed answers and messages: blanks,
# parentheses, quotes, and the brackets and commas of a list of answers.
DELIMITER = r"[\s()\"\[\],]"
GENERATED_NAME = re.compile(
    r"&space\((?P<space>" + NUMERAL + r")\)"
    r"|(?:(?<=" + DELIMITER + r")|^)lambda_(?P<callable>" + NUMERAL + r")"
    r"(?=" + DELIMITER + r"|$)",
    re.MULTILINE)


def rename_generated_space_names(text):
    """TEXT with the generated names of each kind, spaces and callables,
    numbered 1, 2, ... in the order each first appears."""
    names = {"space": {}, "callable": {}}

    def rename(match):
        kind = "space" if match.group("space") is not None else "callable"
        numbers = names[kind]
        number = numbers.setdefault(match.group(kind), len(numbers) + 1)
        return ("&space(%d)" if kind == "space" else "lambda_%d") % number

    return GENERATED_NAME.sub(rename, text)
