import builtins

from hyperon import ValueAtom
from hyperon.ext import register_atoms


@register_atoms(pass_metta=True)
def a_prepare(metta):
    builtins._cetta_export_reentrant_calls = getattr(
        builtins, "_cetta_export_reentrant_calls", 0) + 1
    count = builtins._cetta_export_reentrant_calls
    print(f"reentrant-export-{count}", flush=True)
    if count == 2:
        rows = metta.run("!(import! &self export_reentrant)")
        assert [[str(atom) for atom in row] for row in rows] == [["()"]], rows
    return {"reentrant-only-value": ValueAtom(93)}


@register_atoms
def z_refuse():
    if builtins._cetta_export_reentrant_calls == 2:
        raise RuntimeError("reentrant export failed after prepared registration")
    return {}
