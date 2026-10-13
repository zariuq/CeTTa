from hyperon import OperationAtom, ValueAtom
from hyperon.ext import register_atoms


@register_atoms(pass_metta=True)
def operations(metta):
    def run_document():
        rows = metta.run(
            '(bridge-doc-value 53)\n'
            '!(match &self (bridge-doc-value $x) $x)\n'
            '!(collapse (match &self (bridge-doc-missing $x) $x))\n'
            '!(superpose (17 17 19))')
        assert [[str(atom) for atom in row] for row in rows] == [
            ['53'], ['()'], ['17', '17', '19']], rows
        return [ValueAtom(True)]

    def error_document():
        rows = metta.run(
            '(bridge-before-error 23)\n'
            '!(Error bridge RunnerError)\n'
            '(bridge-after-error 29)')
        assert [[str(atom) for atom in row] for row in rows] == [
            ['(Error bridge RunnerError)']], rows
        return [ValueAtom(True)]

    def parse_failure_document():
        try:
            metta.run('(bridge-before-parse 31)\n(unclosed')
        except RuntimeError:
            return [ValueAtom(True)]
        raise AssertionError('an incomplete document was accepted')

    return {name: OperationAtom(name, function, unwrap=False) for name, function in (
        ('bridge-document', run_document),
        ('bridge-error-document', error_document),
        ('bridge-parse-document', parse_failure_document))}
