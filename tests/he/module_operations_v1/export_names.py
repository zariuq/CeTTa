from hyperon import ValueAtom
from hyperon.ext import grounded, register_atoms, register_tokens


# A key is a regular expression. One that matches exactly one text names it.
@register_atoms
def atoms():
    return {r"export\.dotted": ValueAtom(3), r"export-plus\+": ValueAtom(4),
            "export-plain": ValueAtom(5)}


@register_tokens
def tokens():
    return {r"export-token\.x": lambda token: ValueAtom(len(token))}


# A grounded function is named by its own name.
@grounded
def export_tripled(x):
    return x * 3
