from hyperon import ValueAtom
from hyperon.ext import register_atoms


@register_atoms(pass_metta=True)
def exported_values(metta):
    rows = metta.run('!(import! &self runner_exports_child)')
    assert [[str(atom) for atom in row] for row in rows] == [['()']], rows
    rows = metta.run('!(match &self (export-hook-leaf $x) $x)')
    assert [[str(atom) for atom in row] for row in rows] == [['78']], rows
    return {'exports-ready': ValueAtom(79)}
