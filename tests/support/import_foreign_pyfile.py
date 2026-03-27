from hyperon.ext import register_atoms
from hyperon.atoms import OperationAtom, ValueAtom


@register_atoms
def expose_basic():
    return {
        "file-loaded": ValueAtom("python-file-loaded"),
        "file-double": OperationAtom("file-double", lambda n: [ValueAtom(n * 2)]),
    }


@register_atoms(pass_metta=True)
def expose_bridge(metta):
    def count_foo():
        rows = metta.run("!(match &self (foo) (foo))")
        return [ValueAtom(len(rows[0]) if rows else 0)]

    return {
        "count-foo": OperationAtom("count-foo", lambda: count_foo(), unwrap=False),
    }
