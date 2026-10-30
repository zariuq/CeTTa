#!/usr/bin/env python3
"""Independent whole-tool controls for the native MeTTa vocabulary checker."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def link(path, target, *, directory=False):
    if path.is_symlink():
        assert os.readlink(path) == target, (path, target)
    else:
        path.symlink_to(target, target_is_directory=directory)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/vocabulary-native-v1")
    parser.add_argument("--language", action="append", choices=("he", "petta", "prime"))
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    outside = output / "outside"
    outside.mkdir(parents=True, exist_ok=True)
    cases = {
        "empty": {},
        "clean": {"src/example.metta": b"(ordinary value)\n"},
        "content": {"src/example.metta": ("x" + "sur" + "face" + "y\n").encode()},
        "filename": {"src/" + "sur" + "face" + ".c": b"ordinary\n"},
        "unicode_lines": {"src/example.metta": ("\r\n\n\x85\u2028" + "ſur" + "face" + "\r\n").encode()},
        "ignored_bytes": {"src/example.metta": b"su\xffr" + b"face\n"},
        "excluded": {"src/target/input.txt": b"sur" + b"face\n", "src/a.o": b"sur" + b"face\n"},
        "chunk_boundary": {"src/example.metta": b"x" * 65534 + ("ſur" + "face\n").encode()},
        "long_clean": {"src/example.metta": b"x" * 131073 + b"\n\nlast\n"},
        "many_lines": {"src/example.metta": b"ordinary line\n" * 10000},
        "suffix_directory": {"src/ordinary.o/input.metta": b"sur" + b"face\n"},
        "dot_filename": {"src/.o": b"sur" + b"face\n"},
        "symlink_file": {"src/target.metta": b"sur" + b"face\n"},
        "symlink_directory": {"outside-input/target.metta": b"sur" + b"face\n"},
        "broken_link": {},
        "self_alias": {},
        "opaque_filename": {},
        "unreadable_file": {"src/private.metta": b"ordinary\n"},
        "unreadable_directory": {"src/private/input.metta": b"ordinary\n"},
        "pending_match": {"src/example.metta": b"sur" + b"face" + b"x" * 131073 + b"\n"},
        "earlier_completed_match": {"src/example.metta": b"sur" + b"face\n" + b"x" * 131073 + b"\n"},
    }
    cases["many_clean_files"] = {f"src/input-{i:03d}.metta": b"ordinary\n" for i in range(97)}
    cases["many_chunks"] = {"src/input.metta": b"ordinary line\n" * 500000}
    cases["long_pending_chunks"] = {"src/input.metta": b"x" * (1024 * 1024)}
    cases["repeated_batch_violations"] = {
        f"src/input-{i:03d}.metta": b"sur" + b"face\n" for i in range(65)
    }
    cases["many_excluded_files"] = {f"src/input-{i:03d}.o": b"sur" + b"face\n" for i in range(97)}
    pinned_name = 'examples/import_error_' + 'sur' + 'face.metta'
    reference_field = ('  "' + pinned_name + '": "' + 'a' * 64 + '",\n').encode()
    observation_field = ('  "source": "' + pinned_name + '",\n').encode()
    cases.update({
        "pinned_reference": {"langdef/petta/reference.json": reference_field},
        "pinned_observation": {"tests/petta/corrected_stable_observations.json": observation_field},
        "pinned_wrong_path": {"src/reference.json": reference_field},
        "pinned_wrong_field": {"langdef/petta/reference.json": observation_field},
        "pinned_wrong_digest": {"langdef/petta/reference.json": reference_field.replace(b'a' * 64, b'a' * 63)},
        "pinned_extra_text": {"tests/petta/corrected_stable_observations.json": observation_field.rstrip() + b' extra\n'},
        "pinned_nearby_content": {"langdef/petta/reference.json": reference_field + b'"note": "sur' + b'face"\n'},
        "pinned_unicode_space": {"langdef/petta/reference.json": '\u2003'.encode() + reference_field},
        "pinned_invalid_bytes": {"tests/petta/corrected_stable_observations.json": observation_field.rstrip() + b'\xff\n'},
    })
    for cut in range(1, 8):
        cases[f"literal_seam_{cut}"] = {"src/example.metta": b"x" * (65536 - cut)
            + ("ſur" + "face\n").encode()}
    checked = 0
    for language in args.language or ("he", "petta"):
        for name, marker in (("fold_scoped_positive", b"FoldScoped 3"),
                             ("fold_deep_callback_positive", b"FoldDeepCallback 7"),
                             ("fold_data_initial_positive", b"FoldDataInitial (FoldPayload 7 9)"),
                             ("fold_data_computed_positive", b"FoldDataComputed (FoldPayload 7 9)"),
                             ("fold_data_constructor_negative", b"FoldDataInert (FoldPayload 7 9)"),
                             ("fold_effects_positive", b"FoldEffects 7"),
                             ("fold_multiple_negative", b"step produced multiple results")):
            command = [str(binary), "--lang", language]
            if language != "prime": command += ["--profile", "extended"]
            result = subprocess.run(command + [str(ROOT / "tests/filesystem_v1" / (name + ".metta"))],
                                    capture_output=True, cwd=outside, timeout=30)
            checked += 1
            assert marker in result.stdout, (language, name, result.stdout, result.stderr)
            assert b"forbidden-fold-payload" not in result.stdout, (language, name, result.stdout)
            if name == "fold_effects_positive":
                assert result.stdout.count(b"ab") == 1, (language, name, result.stdout)
    for name, inputs in cases.items():
        root = output / name
        scripts = root / "scripts"
        scripts.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "scripts/check_precise_vocabulary.py", scripts / "check_precise_vocabulary.py")
        shutil.copyfile(ROOT / "scripts/check_precise_vocabulary.metta", scripts / "check_precise_vocabulary.metta")
        for relative, data in inputs.items():
            file = root / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
        if name.startswith("symlink_"):
            (root / "src").mkdir(exist_ok=True)
            if name == "symlink_file": link(root / "src/alias.metta", "target.metta")
            else: link(root / "src/alias", "../outside-input", directory=True)
        if name == "broken_link":
            (root / "src").mkdir(exist_ok=True)
            link(root / "src/missing", "vanished")
        if name == "self_alias":
            (root / "src").mkdir(exist_ok=True)
            link(root / "src/alias.py", "../scripts/check_precise_vocabulary.py")
        if name == "opaque_filename":
            (root / "src").mkdir(exist_ok=True)
            file = os.fsencode(root / "src") + b"/su\xffr" + b"face.metta"
            with open(file, "wb") as stream: stream.write(b"ordinary\n")
        protected = None
        if name.startswith("unreadable_"):
            assert os.geteuid() != 0, "permission controls require an ordinary user"
            protected = root / ("src/private" if name == "unreadable_directory" else "src/private.metta")
            restore_mode = protected.stat().st_mode & 0o7777
            protected.chmod(0)
        try:
            reference = subprocess.run([sys.executable, str(scripts / "check_precise_vocabulary.py")],
                                       cwd=outside, capture_output=True, timeout=30)
            for language in args.language or ("he", "petta"):
                command = [str(binary), "--no-results", "--lang", language]
                if language != "prime": command += ["--profile", "extended"]
                observed = subprocess.run(command + [str(scripts / "check_precise_vocabulary.metta")],
                                          cwd=outside, capture_output=True, timeout=60)
                (root / f"{language}.stdout").write_bytes(observed.stdout)
                (root / f"{language}.stderr").write_bytes(observed.stderr)
                checked += 1
                expected_code = 1 if protected is not None else reference.returncode
                assert observed.returncode == expected_code, (language, name, expected_code,
                    observed.returncode, observed.stdout, observed.stderr)
                if protected is None:
                    # Traversal order is unspecified between files. Keep exact
                    # diagnostics, repeated occurrences and row order per path.
                    def grouped(stderr):
                        lines = stderr.split(b"\n")
                        rows = {}
                        for line in lines[1:]:
                            if line: rows.setdefault(line.split(b":", 1)[0], []).append(line)
                        return lines[0], rows
                    assert grouped(observed.stderr) == grouped(reference.stderr), (language, name,
                        reference.stderr, observed.stderr)
                    assert reference.stdout == observed.stdout, (language, name, reference.stdout, observed.stdout)
                else:
                    assert b"Permission denied" in observed.stderr and b"PASS" not in observed.stdout, observed
                    assert observed.stderr.startswith(b"Cannot complete vocabulary check:"), observed
        finally:
            if protected is not None: protected.chmod(restore_mode)
    original = (ROOT / "scripts/check_precise_vocabulary.metta").read_text()
    preparation = '(str:concat "(?i)" (vocabulary:needle))'
    invocation = '      (vocabulary:prepare-pinned)))'
    assert original.count(preparation) == original.count(invocation) == 1
    faults = {
        "regex_preparation_failure": original.replace(preparation,
            '(str:concat "(?unknown)" (vocabulary:needle))'),
        "regex_execution_failure": original.replace(invocation,
            '      (let $closed (re:close $pattern) (vocabulary:prepare-pinned))))'),
    }
    for name, program in faults.items():
        root = output / name
        (root / "scripts").mkdir(parents=True, exist_ok=True)
        (root / "src").mkdir(exist_ok=True)
        entry = root / "scripts/check_precise_vocabulary.metta"
        entry.write_text(program)
        (root / "src/ordinary.metta").write_text("ordinary\n")
        for language in args.language or ("he", "petta"):
            command = [str(binary), "--no-results", "--lang", language]
            if language != "prime": command += ["--profile", "extended"]
            observed = subprocess.run(command + [str(entry)], cwd=outside,
                                      capture_output=True, timeout=60)
            assert observed.returncode == 2, (language, name, observed)
            assert b"PASS" not in observed.stdout, (language, name, observed)
            assert observed.stderr.startswith(b"Cannot complete vocabulary check: regex"), observed
            checked += 1
    print(f"(VocabularyNativeV1Summary {checked} 0)")


if __name__ == "__main__":
    main()
