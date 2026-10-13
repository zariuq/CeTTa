from hyperon import OperationAtom, ValueAtom
from hyperon.ext import register_atoms


@register_atoms(pass_metta=True)
def operations(metta):
    def define():
        rows = metta.run('(bridge-owned 53)')
        assert rows == [], rows
        return [ValueAtom(True)]

    return {'bridge-define': OperationAtom('bridge-define', define, unwrap=False)}
