from hyperon import OperationAtom, ValueAtom
from hyperon.ext import register_atoms


@register_atoms(pass_metta=True)
def operations(metta):
    def read_resource():
        rows = metta.run(
            '!(import! &self runner_resource)\n'
            '!(match &self (runner-resource-value $x) $x)')
        assert [[str(atom) for atom in row] for row in rows] == [['()'], ['11']], rows
        return [ValueAtom(True)]

    return {'runner-read-resource': OperationAtom('runner-read-resource', read_resource,
                                                  unwrap=False)}
