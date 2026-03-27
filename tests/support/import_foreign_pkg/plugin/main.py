from hyperon.ext import register_atoms
from hyperon.atoms import OperationAtom, ValueAtom


@register_atoms
def expose_pkg():
    return {
        "plugin-tag": ValueAtom("pkg"),
        "plugin-inc": OperationAtom("plugin-inc", lambda n: [ValueAtom(n + 1)]),
    }
