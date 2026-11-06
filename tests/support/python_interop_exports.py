"""A compatibility export module that can participate in host ownership cycles."""
from cetta_bridge import ValueAtom, register_atoms


@register_atoms()
def exported_values():
    return {'hosted-export': ValueAtom(7)}
