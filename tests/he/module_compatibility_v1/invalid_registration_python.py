from hyperon import ValueAtom
from hyperon.ext import register_atoms


@register_atoms
def atoms():
    # One valid and one malformed token pattern. The reference aborts the whole
    # process on the malformed one; here nothing from this module is published.
    return {"good-token": ValueAtom(7), "(unclosed": ValueAtom(1)}
