from hyperon import ValueAtom
from hyperon.ext import register_atoms

print("registered-provider-initialized", flush=True)


@register_atoms
def values():
    return {"registered-provider-value": ValueAtom(17)}
