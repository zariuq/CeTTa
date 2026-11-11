"""Qualify captured ACT Spaces across processes, edits and corrupt indexes."""
import argparse
import ctypes as C
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from check_act_trie import Bytes, Item

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("cetta", type=Path)
    args = parser.parse_args()
    fixture, cetta = args.fixture.resolve(), args.cetta.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith("CETTA_")}
    checks = 0
    with tempfile.TemporaryDirectory(prefix="act-space-", dir=ROOT / "runtime") as td:
        work = Path(td)
        file = work / "facts.cact"

        def run(mode, path=file):
            nonlocal checks
            result = subprocess.run([str(fixture), mode, str(path)], cwd=ROOT,
                                    env=env, capture_output=True, text=True,
                                    timeout=90)
            assert result.returncode == 0, (mode, result.stdout, result.stderr)
            assert result.stdout.strip() == "ACT_SPACE_OK", result.stdout
            checks += 1

        run("write")
        run("read")
        alias = work / "alias.cact"
        alias.symlink_to(file.name)
        run("read", alias)
        alias.unlink()
        original = file.read_bytes()
        sidecar = file.with_name(file.name + '.copies')
        sidecar.write_bytes(b'legacy copies')
        run('reject')
        run('write')
        assert not sidecar.exists()
        run('read')
        file.write_bytes(original)
        sidecar.write_bytes(b'')
        run('read')
        sidecar.unlink()
        os.mkfifo(sidecar)
        run('reject')
        sidecar.unlink()
        pipe = work / 'not-a-regular-file'
        os.mkfifo(pipe)
        run('reject', pipe)
        pipe.unlink()
        held = subprocess.Popen([str(fixture), "hold", str(file)], cwd=ROOT,
                                env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True)
        try:
            assert held.stdout.readline() == "CAPTURED\n"
            replacement = work / "replacement.cact"
            run("write", replacement)
            replacement.replace(file)
            file.unlink()
            stdout, stderr = held.communicate("!", timeout=90)
            assert held.returncode == 0, (stdout, stderr)
            assert stdout.strip() == "ACT_SPACE_OK", stdout
            checks += 1
        finally:
            if held.poll() is None:
                held.kill()
                held.wait()
        file.write_bytes(original)
        # Every one of these rejects before exposing a source or altering it.
        for at in (0, 8, 24, 56, len(original) - 1):
            changed = bytearray(original)
            changed[at] ^= 1
            file.write_bytes(changed)
            run("reject")
        for end in (0, 24, 64, len(original) - 1):
            file.write_bytes(original[:end])
            run("reject")
        # A valid checksum alone does not establish completeness of indexes.
        # Remove an actual scalar posting, rebuild the trie and checksum, and
        # demand rejection rather than a false negative from indexed matching.
        shared = work / "act.so"
        subprocess.run(["cc", "-std=c11", "-O2", "-fPIC", "-shared", "-Isrc",
                        "src/act_trie.c", "-o", str(shared)], cwd=ROOT, check=True)
        lib = C.CDLL(str(shared))
        lib.cetta_act_from_bytes.argtypes = [Bytes, C.POINTER(C.c_void_p)]
        lib.cetta_act_cursor_open.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p,
                                             C.POINTER(C.c_void_p)]
        lib.cetta_act_cursor_next.argtypes = [C.c_void_p, C.POINTER(Bytes),
                                             C.POINTER(C.c_uint64)]
        lib.cetta_act_cursor_close.argtypes = [C.c_void_p]
        lib.cetta_act_release.argtypes = [C.c_void_p]
        lib.cetta_act_build.argtypes = [C.POINTER(Item), C.c_size_t,
                                        C.POINTER(C.c_void_p), C.POINTER(C.c_size_t)]
        tree_at, tree_size, body_at, body_size = struct.unpack_from("<4Q", original, 16)
        image = C.create_string_buffer(original[tree_at:tree_at + tree_size])
        tree, cursor = C.c_void_p(), C.c_void_p()
        assert lib.cetta_act_from_bytes(Bytes(C.cast(image, C.c_void_p), tree_size),
                                        C.byref(tree)) == 0
        assert lib.cetta_act_cursor_open(tree, None, None, C.byref(cursor)) == 0
        entries = []
        key, value = Bytes(), C.c_uint64()
        while True:
            status = lib.cetta_act_cursor_next(cursor, C.byref(key), C.byref(value))
            if status == 1:
                break
            assert status == 0
            entries.append((C.string_at(key.data, key.size), value.value))
        lib.cetta_act_cursor_close(cursor)
        lib.cetta_act_release(tree)
        dropped = next(k for k, _ in entries if k[0] == 3)
        entries = [(k, v) for k, v in entries if k != dropped]
        buffers = [C.create_string_buffer(k) for k, _ in entries]
        items = (Item * len(entries))(*[
            Item(Bytes(C.cast(b, C.c_void_p), len(k)), v)
            for b, (k, v) in zip(buffers, entries)])
        out, size = C.c_void_p(), C.c_size_t()
        assert lib.cetta_act_build(items, len(items), C.byref(out), C.byref(size)) == 0
        header = bytearray(original[:64])
        struct.pack_into("<Q", header, 24, size.value)
        struct.pack_into("<Q", header, 32, 64 + size.value)
        changed = bytes(header) + C.string_at(out, size.value) + original[body_at:body_at + body_size]
        libc = C.CDLL(None)
        libc.free.argtypes = [C.c_void_p]
        libc.free(out)
        file.write_bytes(changed + hashlib.sha256(changed).digest())
        run("reject")
        file.write_bytes(original)
        for dialect in ("petta", "he", "prime"):
            path = work / f"{dialect}.cact"
            source = ROOT / "tests/support/act_space_fixture.metta"
            if dialect != "petta":
                source = work / f"{dialect}.metta"
                text = (ROOT / "tests/support/act_space_fixture.metta").read_text()
                text = text.replace('!(import! &self (library lib_he))\n', '')
                for library in ("act", "str", "system", "fs"):
                    text = text.replace(f"(library {library})", library)
                source.write_text(text)
            result = subprocess.run([
                str(cetta), "--lang", dialect, "-e",
                f'!(import! &self "{source}")', "-e",
                f'!(println! (act-test:run "{path}"))'], cwd=ROOT, env=env,
                capture_output=True, text=True, timeout=90)
            assert result.returncode == 0 and "ACT_SPACE_OK" in result.stdout, (
                dialect, result.returncode, result.stdout, result.stderr)
            assert "SHOULD_NOT_EXECUTE" not in result.stdout
            checks += 1
            for snapshot, expected in [(path, '(b b)'),
                                        (Path(str(path) + '.act'), '(b)')]:
                imported = subprocess.run([
                    str(cetta), '--lang', dialect, '-e',
                    f'!(import! &self "{snapshot}")', '-e',
                    '!(println! (collapse (match &self (edge a $who) $who)))'],
                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
                assert imported.returncode == 0 and expected in imported.stdout.splitlines(), (
                    dialect, snapshot.name, imported.returncode,
                    imported.stdout, imported.stderr)
                assert 'SHOULD_NOT_EXECUTE' not in imported.stdout
                checks += 1
    print(f"ACT Space: {checks} checks of reopen, fidelity, indexing, lifetime and corruption")


if __name__ == "__main__":
    main()
