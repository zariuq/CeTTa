import os
from pathlib import Path

from hyperon import E, OperationAtom, ValueAtom
from hyperon.ext import register_atoms


def attempt(name):
    return f"!(case (import! &self {name}) (((Error $a $b) caught) ($ok loaded)))"


def expect_rows(rows, names):
    assert [[str(atom) for atom in row] for row in rows] == [[name] for name in names], rows


@register_atoms(pass_metta=True)
def operations(metta):
    def separate_runs():
        source = attempt("run_failure") + "\n" + attempt("run_failure")
        expect_rows(metta.run(source), ["caught", "caught"])
        expect_rows(metta.run(source), ["caught", "caught"])
        return [ValueAtom(True)]

    def inner_run():
        expect_rows(metta.run(attempt("run_failure") + "\n" + attempt("run_failure")),
                    ["caught", "caught"])
        return [ValueAtom(True)]

    def nested_runs():
        source = attempt("run_failure") + "\n!(inner-run)\n" + attempt("run_failure")
        expect_rows(metta.run(source), ["caught", "True", "caught"])
        return [ValueAtom(True)]

    def nested_retry():
        expect_rows(metta.run(attempt("failed_parent")), ["caught"])
        expect_rows(metta.run(attempt("retained_child")), ["loaded"])
        expect_rows(metta.run("!(match &self (retained-child-value $x) $x)"), ["37"])
        return [ValueAtom(True)]

    def source_change():
        path = Path(__file__).with_name("changing.metta")
        first = '!(println! "source-a")\n(unterminated\n'
        path.write_text(first)
        stamp = path.stat()
        expect_rows(metta.run(attempt("changing")), ["caught"])
        # Rewriting identical bytes is not a new initializer.
        path.write_text(first)
        os.utime(path, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        expect_rows(metta.run(attempt("changing")), ["caught"])
        # Same length and timestamp, different contents: metadata is not a key.
        path.write_text(first.replace("source-a", "source-b"))
        os.utime(path, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        expect_rows(metta.run(attempt("changing")), ["caught"])
        path.write_text('!(println! "source-c")\n(changed-value 37)\n')
        expect_rows(metta.run(attempt("changing")), ["loaded"])
        expect_rows(metta.run("!(match &self (changed-value $x) $x)"), ["37"])
        return [ValueAtom(True)]

    def reuse_slots():
        path = Path(__file__).with_name("changing_parent.metta")
        for index in range(70):
            path.write_text(f"!(import! &self retained_child)\n(unclosed-{index}\n")
            expect_rows(metta.run(attempt("changing_parent")), ["caught"])
        expect_rows(metta.run(attempt("retained_child")), ["loaded"])
        expect_rows(metta.run("!(match &self (retained-child-value $x) $x)"), ["37"])
        return [ValueAtom(True)]

    def export_runs():
        first = metta.run(attempt("export_failure"))
        second = metta.run(attempt("export_failure"))
        return [E(first[0][0], second[0][0])]

    def source_snapshot():
        path = Path(__file__).with_name("changing_snapshot.py")
        refusal = ('from hyperon.ext import register_atoms\n'
                   '@register_atoms\n'
                   'def refusal():\n'
                   '    print("snapshot-refusal", flush=True)\n'
                   '    raise RuntimeError("snapshot initializer refused")\n')
        second = 'print("snapshot-b", flush=True)\n' + refusal
        first = ('from pathlib import Path\n'
                 'print("snapshot-a", flush=True)\n'
                 f'Path(__file__).write_bytes({second.encode()!r})\n' + refusal)
        path.write_bytes(first.encode())
        # All attempts share one run. A loader visit changes the source, but
        # only the next initialization attempt may observe that new version.
        expect_rows(metta.run('\n'.join([attempt("changing_snapshot")] * 3)),
                    ["caught"] * 3)
        return [ValueAtom(True)]

    def python_source_change():
        import py_compile

        path = Path(__file__).with_name("changing_cached.py")
        first = ('print("cached-source-a", flush=True)\n'
                 'from hyperon.ext import register_atoms\n'
                 '@register_atoms\n'
                 'def refusal():\n'
                 '    print("cached-refusal", flush=True)\n'
                 '    raise RuntimeError("cached initializer refused")\n')
        path.write_bytes(first.encode())
        # Populate the cache explicitly even when the test runner disables
        # incidental bytecode writes. Each interpreter builds its own cache.
        py_compile.compile(str(path), doraise=True)
        stamp = path.stat()
        expect_rows(metta.run(attempt("changing_cached")), ["caught"])
        # The second version has the same metadata used by Python's ordinary
        # bytecode cache, but it has different source bytes.
        second = first.replace("cached-source-a", "cached-source-b")
        path.write_bytes(second.encode())
        os.utime(path, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        expect_rows(metta.run(attempt("changing_cached")), ["caught"])
        expect_rows(metta.run(attempt("changing_cached")), ["caught"])
        return [ValueAtom(True)]

    return {name: OperationAtom(name, function, unwrap=False) for name, function in (
        ("separate-runs", separate_runs), ("inner-run", inner_run),
        ("nested-runs", nested_runs), ("nested-retry", nested_retry),
        ("source-change", source_change),
        ("source-snapshot", source_snapshot),
        ("python-source-change", python_source_change),
        ("export-runs", export_runs),
        ("reuse-slots", reuse_slots))}
