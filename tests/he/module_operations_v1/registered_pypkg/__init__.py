from hyperon import ValueAtom
from hyperon.ext import register_atoms

print("registered-package-initialized", flush=True)


@register_atoms
def values():
    return {"registered-package-value": ValueAtom(19)}
