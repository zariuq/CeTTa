"""ACTree03 byte/value differential checks against the pinned Rust PathMap oracle."""
import argparse
import ctypes as C
from pathlib import Path
import random
import os
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


class Bytes(C.Structure):
    _fields_ = [("data", C.c_void_p), ("size", C.c_size_t)]


class Item(C.Structure):
    _fields_ = [("key", Bytes), ("value", C.c_uint64)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    oracle = p.add_mutually_exclusive_group(required=True)
    oracle.add_argument("--oracle", type=Path)
    oracle.add_argument("--pathmap", type=Path,
                        help="PathMap checkout used only to build the test oracle")
    a = p.parse_args()
    with tempfile.TemporaryDirectory(prefix="act-differential-", dir=ROOT / "runtime") as td:
        work = Path(td)
        if a.pathmap:
            crate = work / "oracle"
            crate.mkdir()
            manifest = ('[package]\nname="cetta-act-reference"\nversion="0.0.0"\n'
                        'edition="2024"\n[dependencies]\npathmap={path=' +
                        json.dumps(str(a.pathmap.resolve())) +
                        ',features=["arena_compact","nightly"]}\n[[bin]]\n'
                        'name="cetta-act-reference"\npath=' +
                        json.dumps(str(ROOT / "tests/support/act_reference.rs")) + '\n')
            (crate / "Cargo.toml").write_text(manifest)
            env = os.environ.copy()
            env['RUSTFLAGS'] = env.get('RUSTFLAGS', '') + ' -C target-cpu=native'
            subprocess.run(['cargo', '+nightly', 'build', '--quiet',
                            '--manifest-path', str(crate / 'Cargo.toml')],
                           env=env, check=True)
            a.oracle = crate / 'target/debug/cetta-act-reference'
        shared = work / "act.so"
        subprocess.run(["cc", "-std=c11", "-O2", "-fPIC", "-shared", "-Isrc",
                        "src/act_trie.c", "-o", str(shared)], cwd=ROOT, check=True)
        lib = C.CDLL(str(shared))
        lib.cetta_act_build.argtypes = [C.POINTER(Item), C.c_size_t, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t)]
        lib.cetta_act_builder_open.argtypes = [C.c_int, C.c_int, C.c_int, C.c_size_t, C.POINTER(C.c_void_p)]
        lib.cetta_act_builder_put.argtypes = [C.c_void_p, Bytes, C.c_uint64]
        lib.cetta_act_builder_finish.argtypes = [C.c_void_p, C.POINTER(C.c_uint64)]
        lib.cetta_act_builder_close.argtypes = [C.c_void_p]
        lib.cetta_act_open.argtypes = [C.c_char_p, C.POINTER(C.c_void_p)]
        lib.cetta_act_get.argtypes = [C.c_void_p, Bytes, C.POINTER(C.c_uint64)]
        lib.cetta_act_validate.argtypes = [C.c_void_p, C.POINTER(C.c_uint64)]
        lib.cetta_act_release.argtypes = [C.c_void_p]
        libc = C.CDLL(None)
        libc.free.argtypes = [C.c_void_p]
        rng = random.Random(3817)
        cases = [{}, {b"": 0}, {bytes([i]): (1 << 64) - 1 - i for i in range(256)},
                 {b"": 99, b"a": 1, b"ab": 2, b"abc": 3, b"abd": 4, b"abcd": 5},
                 {b"x" * 100_000: 71, b"x" * 100_000 + b"\0": 72}]
        for _ in range(30):
            cases.append({rng.randbytes(rng.randrange(0, 100)): rng.getrandbits(64) for _ in range(300)})
        checked = 0
        for number, entries in enumerate(cases):
            entries = sorted(entries.items())
            buffers = [C.create_string_buffer(key) for key, _ in entries]
            items = (Item * len(entries))(*[Item(Bytes(C.cast(buf, C.c_void_p), len(key)), value)
                                           for buf, (key, value) in zip(buffers, entries)])
            image, size = C.c_void_p(), C.c_size_t()
            assert lib.cetta_act_build(items, len(items), C.byref(image), C.byref(size)) == 0
            c_file = work / f"c-{number}.act"
            c_file.write_bytes(C.string_at(image, size.value))
            libc.free(image)
            expected = "".join(f"{key.hex()} {value}\n" for key, value in entries)
            result = subprocess.check_output([str(a.oracle.resolve()), "read", str(c_file)], text=True)
            assert result == expected, (number, "C writer/Rust reader")
            streamed = work / f"stream-{number}.act"
            scratch = [streamed, work / "frontier.tmp", work / "key.tmp"]
            files = [path.open("w+b") for path in scratch]
            builder = C.c_void_p()
            try:
                assert lib.cetta_act_builder_open(*(f.fileno() for f in files),
                    256 * 1024, C.byref(builder)) == 0
                for buf, (key, value) in zip(buffers, entries):
                    assert lib.cetta_act_builder_put(builder,
                        Bytes(C.cast(buf, C.c_void_p), len(key)), value) == 0
                written = C.c_uint64()
                assert lib.cetta_act_builder_finish(builder, C.byref(written)) == 0
                assert written.value == os.fstat(files[0].fileno()).st_size
            finally:
                lib.cetta_act_builder_close(builder)
                for f in files:
                    f.close()
            result = subprocess.check_output([str(a.oracle.resolve()), "read", str(streamed)], text=True)
            assert result == expected, (number, "streaming C writer/Rust reader")

            source = work / "keys.txt"
            source.write_text(expected)
            rust_file = work / f"rust-{number}.act"
            subprocess.run([str(a.oracle.resolve()), "write", str(source), str(rust_file)], check=True)
            tree = C.c_void_p()
            assert lib.cetta_act_open(str(rust_file).encode(), C.byref(tree)) == 0
            try:
                count = C.c_uint64()
                assert lib.cetta_act_validate(tree, C.byref(count)) == 0 and count.value == len(entries)
                for buf, (key, value) in zip(buffers, entries):
                    got = C.c_uint64()
                    status = lib.cetta_act_get(tree, Bytes(C.cast(buf, C.c_void_p), len(key)), C.byref(got))
                    assert status == 0, (number, key[:40].hex(), len(key), status)
                    assert got.value == value
                    checked += 1
            finally:
                lib.cetta_act_release(tree)
        print(f"ACT differential: {len(cases)} images through buffered/streaming C and Rust writers, {checked} key/value lookups")


if __name__ == "__main__":
    main()
