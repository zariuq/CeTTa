from hyperon import ValueAtom
from hyperon.ext import register_atoms


@register_atoms(pass_metta=True)
def exported_values(metta):
    # A run returns at the first Error value and never reads the later text.
    rows = metta.run('!(import! &self no_such_runner_module)\n(unbalanced')
    assert len(rows) == 1 and str(rows[0][0]).startswith('(Error'), rows
    # A run that reaches a syntax fault raises.
    try:
        metta.run('(runner-stop-fact 1)\n(unbalanced')
        reached = 0
    except RuntimeError:
        reached = 1
    return {'runner-stop-token': ValueAtom(reached)}
