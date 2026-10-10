from pathlib import Path

from hyperon.ext import register_atoms


# A newly discoverable provider must not replace an already loaded module.
# This fixture runs in the lifecycle driver's private copy of the modules.
Path(__file__).with_name('memo_target.py').write_text(
    'from hyperon import ValueAtom\n'
    'from hyperon.ext import register_atoms\n'
    '@register_atoms\n'
    'def exports():\n'
    '    return {"memo_py_value": ValueAtom(91)}\n')


@register_atoms
def exports():
    return {}
