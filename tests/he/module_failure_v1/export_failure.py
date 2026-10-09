import builtins

from hyperon import ValueAtom
from hyperon.ext import register_atoms, register_tokens

@register_atoms
def a_prepare():
    builtins._cetta_export_failure_calls = getattr(
        builtins, "_cetta_export_failure_calls", 0) + 1
    print(f"export-hook-{builtins._cetta_export_failure_calls}", flush=True)
    return {"export-only-value": ValueAtom(91)}


@register_tokens
def b_prepare():
    return {"export-only-token": lambda text: ValueAtom(92)}


@register_atoms
def z_refuse():
    if builtins._cetta_export_failure_calls == 2:
        raise RuntimeError("export failed after prepared registrations")
    return {}
