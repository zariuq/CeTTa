from hyperon import ValueAtom
from hyperon.ext import register_atoms

calls = 0


@register_atoms
def a_prepare():
    global calls
    calls += 1
    return {"export-storage-value": ValueAtom(calls)}


@register_atoms
def z_refuse():
    if calls == 2:
        raise RuntimeError("retained export storage failure")
    return {}
