#!/usr/bin/env python3
"""Offline SQLite oracle for the native disk byte index; no runtime SQL route."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import random
import sqlite3
import tempfile
import time


class Bytes(c.Structure):
    _fields_ = [("data", c.POINTER(c.c_ubyte)), ("size", c.c_size_t)]


class Buffer(c.Structure):
    _fields_ = [("data", c.POINTER(c.c_ubyte)), ("size", c.c_size_t)]


class Root(c.Structure):
    _fields_ = [("fd", c.c_int), ("root", c.c_uint64), ("end", c.c_uint64),
                ("key_limit", c.c_size_t), ("value_limit", c.c_size_t)]


class Operation(c.Structure):
    _fields_ = [("remove", c.c_bool), ("key", Bytes), ("value", Bytes)]


class Blob(c.Structure):
    _fields_ = [("reference", c.c_uint64), ("size", c.c_uint64)]


class Index:
    def __init__(self, library):
        self.lib = c.CDLL(str(Path(library).resolve()))
        signatures = {
            "cetta_disk_build": ([c.POINTER(Root), c.POINTER(Operation), c.c_size_t, c.POINTER(Root)], c.c_int),
            "cetta_disk_lookup": ([c.POINTER(Root), Bytes, c.POINTER(Blob)], c.c_int),
            "cetta_disk_blob_read": ([c.POINTER(Root), Blob, c.c_uint64, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)], c.c_int),
            "cetta_disk_cursor_open": ([c.POINTER(Root), c.POINTER(c.c_void_p)], c.c_int),
            "cetta_disk_cursor_open_range": ([c.POINTER(Root), c.POINTER(Bytes), c.c_bool,
                                               c.POINTER(Bytes), c.POINTER(c.c_void_p)], c.c_int),
            "cetta_disk_cursor_next": ([c.c_void_p, c.POINTER(Buffer), c.POINTER(Blob)], c.c_int),
            "cetta_disk_cursor_root": ([c.c_void_p], c.POINTER(Root)),
            "cetta_disk_cursor_close": ([c.c_void_p], None),
            "cetta_disk_buffer_free": ([c.POINTER(Buffer)], None),
            "cetta_disk_compact": ([c.POINTER(Root), c.c_int, c.POINTER(Root)], c.c_int),
            "cetta_disk_sync": ([c.POINTER(Root)], c.c_int),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.lib, name)
            function.argtypes, function.restype = arguments, result

    @staticmethod
    def borrowed(data, keep):
        storage = c.create_string_buffer(data or b"\0")
        keep.append(storage)
        return Bytes(c.cast(storage, c.POINTER(c.c_ubyte)), len(data))

    def build(self, root, operations):
        keep = []
        values = (Operation * len(operations))(*[
            Operation(remove, self.borrowed(key, keep), self.borrowed(value, keep))
            for remove, key, value in operations])
        result = Root()
        status = self.lib.cetta_disk_build(c.byref(root), values, len(values), c.byref(result))
        if status:
            raise AssertionError(("native build", status))
        return result

    def body(self, root, value):
        storage = c.create_string_buffer(max(1, value.size))
        count = c.c_size_t()
        status = self.lib.cetta_disk_blob_read(root, value, 0, storage, value.size, c.byref(count))
        if status or count.value != value.size:
            raise AssertionError(("native body", status, count.value, value.size))
        return storage.raw[:count.value]

    def lookup(self, root, key):
        keep, value = [], Blob()
        status = self.lib.cetta_disk_lookup(c.byref(root), self.borrowed(key, keep), c.byref(value))
        if status == 2:
            return None
        if status:
            raise AssertionError(("native lookup", status))
        return self.body(c.byref(root), value)

    def rows(self, root, lower=None, exclusive=False, upper=None):
        cursor = c.c_void_p()
        keep = []
        low = None if lower is None else self.borrowed(lower, keep)
        high = None if upper is None else self.borrowed(upper, keep)
        status = self.lib.cetta_disk_cursor_open_range(c.byref(root),
            None if low is None else c.byref(low), exclusive,
            None if high is None else c.byref(high), c.byref(cursor))
        if status:
            raise AssertionError(("native cursor", status))
        result = []
        try:
            while True:
                key, value = Buffer(), Blob()
                status = self.lib.cetta_disk_cursor_next(cursor, c.byref(key), c.byref(value))
                if status == 1:
                    return result
                if status:
                    raise AssertionError(("native next", status))
                try:
                    result.append((c.string_at(key.data, key.size),
                                   self.body(self.lib.cetta_disk_cursor_root(cursor), value)))
                finally:
                    self.lib.cetta_disk_buffer_free(c.byref(key))
        finally:
            self.lib.cetta_disk_cursor_close(cursor)


def check(index, seeds, steps):
    checked = lookups = transitions = ranges = 0
    trace = hashlib.sha256()
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="native-disk-oracle-") as directory:
        for seed in seeds:
            rng = random.Random(seed)
            database = sqlite3.connect(":memory:")
            database.execute("CREATE TABLE records(key BLOB PRIMARY KEY NOT NULL, value BLOB NOT NULL)")
            fd = os.open(Path(directory) / str(seed), os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
            root = Root(fd, 0, 0, 4096, 4 * 1024 * 1024)
            old = []
            keys = [b"", b"\0", b"\0\0", b"\0\xff", b"\xff", b"\xff\0"]
            keys += [rng.randbytes(rng.randrange(1, 20)) for _ in range(70)]
            try:
                for step in range(steps):
                    operations = [(rng.randrange(4) == 0, rng.choice(keys),
                                   rng.randbytes(rng.randrange(160))) for _ in range(rng.randrange(1, 7))]
                    before = root
                    root = index.build(root, operations)
                    with database:
                        for remove, key, value in operations:
                            if remove:
                                database.execute("DELETE FROM records WHERE key=?", (key,))
                            else:
                                database.execute("INSERT OR REPLACE INTO records VALUES(?,?)", (key, value))
                    transitions += len(operations)
                    trace.update(json.dumps([(r, k.hex(), v.hex()) for r, k, v in operations],
                                            separators=(",", ":")).encode())
                    if step % 29 == 0:
                        expected = database.execute("SELECT key,value FROM records ORDER BY key").fetchall()
                        actual = index.rows(root)
                        if actual != expected:
                            raise AssertionError(("rows", seed, step, actual, expected))
                        checked += len(expected)
                        old.append((root, expected))
                    key = rng.choice(keys + [rng.randbytes(11)])
                    expected = database.execute("SELECT value FROM records WHERE key=?", (key,)).fetchone()
                    if index.lookup(root, key) != (None if expected is None else expected[0]):
                        raise AssertionError(("lookup", seed, step))
                    lookups += 1
                    lower = rng.choice(keys + [rng.randbytes(11), None])
                    upper = rng.choice(keys + [rng.randbytes(11), None])
                    exclusive = bool(rng.randrange(2))
                    constraints, arguments = [], []
                    if lower is not None:
                        constraints.append("key>?" if exclusive else "key>=?")
                        arguments.append(lower)
                    if upper is not None:
                        constraints.append("key<?")
                        arguments.append(upper)
                    query = "SELECT key,value FROM records"
                    if constraints:
                        query += " WHERE " + " AND ".join(constraints)
                    wanted = database.execute(query + " ORDER BY key", arguments).fetchall()
                    if index.rows(root, lower, exclusive, upper) != wanted:
                        raise AssertionError(("range", seed, step, lower, exclusive, upper))
                    checked += len(wanted)
                    ranges += 1
                    if step and step % 101 == 0:
                        if index.rows(old[0][0]) != old[0][1]:
                            raise AssertionError(("captured old root", seed, step))
                        # Rejected candidate validation must not modify input.
                        bad = Operation(False, Bytes(None, 1), Bytes(None, 0))
                        candidate = Root()
                        status = index.lib.cetta_disk_build(c.byref(before), c.byref(bad), 1, c.byref(candidate))
                        if status != 3 or candidate.fd != -1:
                            raise AssertionError(("invalid candidate", seed, step, status))
                expected = database.execute("SELECT key,value FROM records ORDER BY key").fetchall()
                compact_fd = os.open(Path(directory) / (str(seed) + "-compact"),
                                     os.O_RDWR | os.O_CREAT | os.O_EXCL, 0o600)
                try:
                    compacted = Root()
                    status = index.lib.cetta_disk_compact(c.byref(root), compact_fd, c.byref(compacted))
                    if status or index.rows(compacted) != expected:
                        raise AssertionError(("compaction", seed, status))
                    if index.lib.cetta_disk_sync(c.byref(compacted)):
                        raise AssertionError(("sync", seed))
                    checked += len(expected)
                    for captured, wanted in old:
                        if index.rows(captured) != wanted:
                            raise AssertionError(("snapshot", seed))
                        checked += len(wanted)
                finally:
                    os.close(compact_fd)
            finally:
                os.close(fd)
                database.close()
    return {"seeds": list(seeds), "steps_per_seed": steps, "mutations": transitions,
            "exact_rows": checked, "point_lookups": lookups, "ranges": ranges,
            "trace_sha256": trace.hexdigest(),
            "sqlite_version": sqlite3.sqlite_version, "seconds": time.monotonic() - start}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--seeds", type=int, default=12)
    parser.add_argument("--steps", type=int, default=300)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.seeds<1 or arguments.steps<1:
        parser.error("seeds and steps must be positive")
    result = check(Index(arguments.library), range(arguments.seeds), arguments.steps)
    output = json.dumps(result, indent=2) + "\n"
    if arguments.output:
        arguments.output.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
