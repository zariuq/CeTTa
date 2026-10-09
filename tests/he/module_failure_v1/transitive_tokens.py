import builtins

from hyperon import ValueAtom
from hyperon.ext import register_atoms, register_tokens


@register_atoms
def export_values():
    builtins._cetta_transitive_export_calls = getattr(
        builtins, '_cetta_transitive_export_calls', 0) + 1
    print(f'transitive-hook-{builtins._cetta_transitive_export_calls}', flush=True)
    return {'transitive-export-value': ValueAtom(83)}


@register_tokens
def export_tokens():
    return {'transitive-token-[0-9]+': lambda text: ValueAtom(84)}
