from hyperon import E, S, ValueAtom, OperationAtom
from hyperon.ext import register_atoms


@register_atoms
def operations():
    return {
        "make-data-println": OperationAtom("make-data-println", lambda: [
            E(S("println!"), ValueAtom("constructed-effect"))], unwrap=False),
        "make-data-plus": OperationAtom("make-data-plus", lambda: [
            E(S("+"), ValueAtom(2), ValueAtom(3))], unwrap=False),
        "make-plus-symbol": OperationAtom("make-plus-symbol", lambda: [S("+")],
                                          unwrap=False),
        "make-equation-call": OperationAtom("make-equation-call", lambda: [
            E(S("ordinary-function"), ValueAtom(4))], unwrap=False),
        "direct-python-operation": OperationAtom("direct-python-operation",
                                                  lambda: 17),
        "make-native-call": OperationAtom("make-native-call", lambda: [
            E(S("__cetta_lib_system_monotonic_ns"), E())], unwrap=False),
    }
