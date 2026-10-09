from hyperon import ValueAtom
from hyperon.ext import register_atoms


# A Python module has names to export and no MeTTa text to include.
@register_atoms
def atoms():
    return {'include-python-value': ValueAtom(1)}
