"""Logical copies preserve encoded bags and origin mappings in one commit."""
import argparse
from collections import Counter
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, FloatBits, BigInt, Rational, encode, decode
from value_worker_client import Worker
from cetta_space_transfer import archive_validate, archive_scan, frame_write, export_collection, import_collection, Client as TransferClient
from check_native_kb_service import Server, Client, await_resources
from check_space_query import open_query, drain


def raw(value):
    return value.encode("utf-8") if isinstance(value, str) else value


def use(client, token, route, argument=(), **kwargs):
    return client.call(E("UseValueV1", token, S(route), argument), **kwargs)


def archive(header, rows):
    digest = hashlib.sha256(encode(header)).hexdigest()
    result = io.BytesIO(b"CSX1")
    result.seek(4)
    frame_write(result, header)
    for row in rows:
        digest = hashlib.sha256(encode(E("SpaceExportLinkV1", digest, row))).hexdigest()
        frame_write(result, row)
    footer = E("SpaceExportEndV1", len(rows), digest)
    frame_write(result, footer)
    return result.getvalue(), footer


def opened(client, collection, request, header, footer, maximum=64 * 1024 * 1024):
    view, _ = client.snapshot()
    try:
        return client.route("ImportOpen", E("ImportOpenV1", collection, request, header, footer[2], maximum), view)
    finally:
        client.drop(view)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="space-transfer-") as directory:
        root = Path(directory)
        source = root / "source"; source.mkdir(mode=0o700)
        destination = root / "destination"; destination.mkdir(mode=0o700)
        for folder in (source, destination):
            (folder / "store").mkdir(mode=0o700)
        binary = args.worker.resolve()
        workers = []
        def worker(module, entry):
            item = Worker(binary, ROOT / "tests/support" / module, entry, language="petta")
            workers.append(item)
            return item
        db = worker("indexed_space_fixture.metta", "indexed-fixture:run")
        transfer = worker("space_transfer_fixture.metta", "transfer-test:run")
        storage = worker("store_fixture.metta", "store-fixture:run")
        first = Server(binary, source, module=ROOT / "tests/support/space_transfer_server.metta", entry="transfer-server:configuration")
        second = Server(binary, destination, module=ROOT / "tests/support/space_transfer_server.metta", entry="transfer-server:configuration")
        clients = []
        def client(server):
            value = Client(server.path); clients.append(value); return value
        def call(folder, command):
            return db.call(E("At", str(folder / "store"), command))
        def embedded(folder, command):
            return transfer.call(E("At", str(folder / "store"), command))
        try:
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceRangeIndexV1", 3, 2))
            assert db.call(E("Create", str(source / "store"), specs))[0] == S("KnowledgeCommittedV1")
            assert storage.call(E("Create", str(destination / "store"))) == S("Created")
            values = [E("edge", S("a"), 1), E("edge", S("a"), 1),
                      E("shared", Var(0), Var(0)), E("independent", Var(0), Var(1)), Var(0),
                      E("boolean", True, S("true")), E("boolean", S("false"), False),
                      E("float", FloatBits(0x8000000000000000), FloatBits(0x7ff8000000001234)),
                      E("exact", BigInt(str(1 << 90)), Rational("7/13")),
                      E("text", "a\x00ž", b"\xff\x00"), E("danger"),
                      E("=", E("stored"), E("println!", S("ACCIDENTAL_EXECUTION")))]
            inserted = call(source, E("Commit", "seed", tuple(E("SpaceInsertV1", i, v) for i, v in enumerate(values))))
            assert inserted[0] == S("KnowledgeCommittedV1"), inserted
            reference = inserted[1][1][0]
            values.append(E("provenance", reference, S("original")))
            assert call(source, E("Commit", "lineage", (E("SpaceInsertV1", 0, values[-1]),)))[0] == S("KnowledgeCommittedV1")
            small = embedded(source, E("Export", S("sample"), 131072))
            assert small[0] == S("TransferArchiveV1"), small
            header, rows, footer = small[1:]
            packet, oracle_footer = archive(header, rows)
            assert footer == oracle_footer
            assert archive_validate(io.BytesIO(packet)) == (header, footer)
            assert Counter(raw(row[3]) for row in rows) == Counter(encode(v) for v in values)
            copied = embedded(destination, E("Import", S("embedded-copy"), "embedded", header, footer[2], rows, footer, 16 * 1024 * 1024))
            assert copied[1][0] == S("KnowledgeCommittedV1"), copied
            assert len(copied[2]) == len(rows)
            embedded_copy = embedded(destination, E("Export", S("embedded-copy"), 131072))
            assert Counter(raw(row[3]) for row in embedded_copy[2]) == Counter(raw(row[3]) for row in rows)
            for mapping in copied[2]:
                assert mapping[1] != mapping[2]
                assert embedded(destination, E("Origin", S("embedded-copy"), "embedded", mapping[1])) == E("StoreDecoded", mapping)
            replay = embedded(destination, E("Import", S("embedded-copy"), "embedded", header, footer[2], (), footer, 16 * 1024 * 1024))
            assert replay[0] == S("KnowledgeReplayedV1"), replay
            assert embedded(destination, E("Import", S("embedded-copy"), "embedded", header, "0" * 64, (), footer, 16 * 1024 * 1024)) == E("KnowledgeRefusedV1", S("receipt-collision"))
            # A stale continuation must not overwrite a spool and then let its
            # newer sibling publish under the original body's hash.
            one_header = E("SpaceExportHeaderV1", header[1], header[2], header[3], 1, header[5])
            _, one_footer = archive(one_header, rows[:1])
            replacement = E("SpaceExportRowV1", 0, rows[0][2], encode(E("tampered")))
            alias = embedded(destination, E("Alias", S("aliased"), "aliased", one_header, one_footer[2], rows[0], replacement, one_footer))
            assert alias[1] == E("SpaceRefusedV1", S("import-row")), alias
            assert alias[2][0] == S("SpaceRefusedV1"), alias

            first.start(); second.start()
            a, b, other = client(first), client(second), client(second)
            before = call(source, E("Info"))
            # Export holds its captured root across publication/collection.
            view, _ = a.snapshot()
            state = a.route("ExportOpen", E("ExportOpenV1", S("sample"), 131072), view)
            a.drop(view)
            assert state[2] == header
            assert call(source, E("Commit", "after-capture", (E("SpaceInsertV1", 0, E("later")),)))[0] == S("KnowledgeCommittedV1")
            assert a.route("Compact")[0] == S("StoreCompacted")
            streamed = []
            while state[0] == S("ServiceResourceV1"):
                token = state[1]
                identifier = f"export-{len(streamed)}"
                state = use(a, token, "ExportNext", identifier=identifier)
                assert use(a, token, "ExportNext", identifier=identifier) == state
                if state[0] == S("ServiceResourceV1"):
                    streamed.append(state[2])
            assert streamed == list(rows) and state == E("SpaceExportCompleteV1", footer)
            await_resources(a, 0)
            assert a.route("Create", E("CreateV1", S("empty"), "empty-create"))[0] == S("KnowledgeCommittedV1")
            empty = embedded(source, E("Export", S("empty"), 131072))
            assert empty[2] == () and empty[3][1] == 0, empty
            state = opened(b, S("empty-copy"), "empty-copy", empty[1], empty[3])
            assert use(b, state[1], "ImportFinish", empty[3])[0] == S("KnowledgeCommittedV1")
            assert embedded(destination, E("Export", S("empty-copy"), 131072))[2] == ()

            # Malformed framing is rejected independently of the runtime.
            bad_files = [packet[:-1], packet + b"trailer", b"CSX0" + packet[4:], b"CSX1" + struct.pack(">I", 131073)]
            flipped = bytearray(packet); flipped[-8] ^= 1; bad_files.append(bytes(flipped))
            for bad in bad_files:
                try:
                    archive_validate(io.BytesIO(bad))
                except (ValueError, UnicodeError):
                    pass
                else:
                    raise AssertionError("accepted corrupt archive")
            revision = b.route("Snapshot"); b.drop(revision[1])
            # All failed/cancelled staging remains invisible, including maps.
            for bad in [(), Var(0), E("SpaceExportHeaderV1", header[1], header[2], header[3], -1, header[5]),
                        E("SpaceExportHeaderV1", header[1], header[2], header[3], header[4], (E("SpaceRangeIndexV1", 0, 0),))]:
                assert opened(b, S("bad-header"), "bad-header", bad, footer) == E("SpaceRefusedV1", S("import-header"))
            view, _ = a.snapshot()
            assert a.route("ExportOpen", E("ExportOpenV1", S("sample"), 64), view) == E("SpaceRefusedV1", S("transfer-frame-limit"))
            a.drop(view)
            cases = [
                ("sequence", E("SpaceExportRowV1", 1, rows[0][2], rows[0][3])),
                ("epoch", E("SpaceExportRowV1", 0, E("StoreOccurrenceV1", "f" * 32, rows[0][2][2]), rows[0][3])),
                ("domain", E("SpaceExportRowV1", 0, E("StoreOccurrenceV1", header[1], "foreign"), rows[0][3])),
                ("decode", E("SpaceExportRowV1", 0, rows[0][2], b"invalid")),
                ("variable-sequence", E("SpaceExportRowV1", Var(0), rows[0][2], rows[0][3])),
                ("variable-epoch", E("SpaceExportRowV1", 0, E("StoreOccurrenceV1", Var(0), rows[0][2][2]), rows[0][3])),
                ("oversized", E("SpaceExportRowV1", 0, rows[0][2], encode("x" * 140000))),
            ]
            for name, badrow in cases:
                state = opened(b, S("bad-" + name), name, header, footer)
                result = use(b, state[1], "ImportRow", badrow)
                assert result[0] != S("ServiceResourceV1"), (name, result)
            state = opened(b, S("premature"), "premature", header, footer)
            assert use(b, state[1], "ImportFinish", footer) == E("SpaceRefusedV1", S("import-incomplete-or-digest"))
            state = opened(b, S("limit"), "limit", header, footer, 64)
            if state[0] == S("ServiceResourceV1"):
                state = use(b, state[1], "ImportRow", rows[0])
            assert state[0] == S("StoreRefused"), state
            state = opened(b, S("out-of-order"), "out-of-order", header, footer)
            state = use(b, state[1], "ImportRow", rows[0])
            assert use(b, state[1], "ImportRow", E("SpaceExportRowV1", 1, rows[0][2], rows[0][3])) == E("SpaceRefusedV1", S("import-row"))
            state = opened(b, S("digest"), "digest", header, footer)
            for row in rows:
                changed = replacement if row[1] == 0 else row
                state = use(b, state[1], "ImportRow", changed)
                assert state[0] == S("ServiceResourceV1"), state
            assert use(b, state[1], "ImportFinish", footer) == E("SpaceRefusedV1", S("import-incomplete-or-digest"))
            await_resources(b, 0)
            now = b.route("Snapshot"); assert now[2] == revision[2]; b.drop(now[1])

            state = opened(b, S("foreign-token"), "foreign-token", header, footer)
            assert use(other, state[1], "ImportCancel", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            assert b.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
            assert use(b, state[1], "ImportRow", rows[0], ok=False) == E("ServiceRefusedV1", S("read-only"))
            assert use(b, state[1], "ImportCancel") == S("SpaceImportClosedV1")
            b.close(); b = client(second)
            state = opened(b, S("conflict"), "conflict", header, footer)
            for row in rows:state = use(b, state[1], "ImportRow", row)
            assert other.route("Create", E("CreateV1", S("competing"), "competing"))[0] == S("KnowledgeCommittedV1")
            assert use(b, state[1], "ImportFinish", footer) == E("StoreRefused", S("conflict"))
            for mode in ("disconnect", "kill"):
                abandoned = client(second)
                state = opened(abandoned, S("abandoned-" + mode), mode, header, footer)
                state = use(abandoned, state[1], "ImportRow", rows[0])
                abandoned.close()
                if mode == "kill":
                    second.stop(crash=True); second.start(); b.close(); other.close(); b, other = client(second), client(second)
                await_resources(b, 0)
                command = E("SpaceImportV1", S("abandoned-" + mode), header, footer[2])
                assert b.route("Resolve", E("ResolveV1", mode, command))[0] == S("KnowledgeUnrecordedV1")

            # Commit reply loss settles from the same durable receipt after a
            # process kill, without staging again or advancing the revision.
            state = opened(b, S("served-copy"), "served", header, footer)
            mappings = []
            for row in rows:
                state = use(b, state[1], "ImportRow", row)
                mappings.append(state[2])
                probe, _ = other.snapshot()
                assert other.route("ExportOpen", E("ExportOpenV1", S("served-copy"), 131072), probe) == E("SpaceRefusedV1", S("collection-absent"))
                other.drop(probe)
            old_view, _ = other.snapshot()
            identifier = b.send(E("UseValueV1", state[1], S("ImportFinish"), footer))
            b.close()
            command = E("SpaceImportV1", S("served-copy"), header, footer[2])
            for _ in range(100):
                settled = other.route("Resolve", E("ResolveV1", "served", command))
                if settled[0] == S("KnowledgeResolvedV1"):break
            assert settled[0] == S("KnowledgeResolvedV1"), settled
            assert other.route("ExportOpen", E("ExportOpenV1", S("served-copy"), 131072), old_view) == E("SpaceRefusedV1", S("collection-absent"))
            other.drop(old_view)
            second.stop(crash=True); second.start(); other.close(); b, other = client(second), client(second)
            replay = opened(b, S("served-copy"), "served", header, footer)
            assert replay == E("KnowledgeReplayedV1", settled[1]), replay
            for origin, mapping in zip(rows, mappings):
                view, _ = b.snapshot()
                assert b.route("ImportOrigin", E("ImportOriginV1", S("served-copy"), "served", origin[2]), view) == E("StoreDecoded", mapping)
                b.drop(view)
            assert b.route("Compact")[0] == S("StoreCompacted")
            def query(connection, collection, pattern, template):
                view, _ = connection.snapshot()
                cursor = connection.route("Query", E("QueryV1", collection, pattern, template), view)
                assert cursor[0] == S("ServiceResourceV1"), cursor
                try:
                    return connection.drain(cursor[1])
                finally:
                    connection.drop(cursor[1]); connection.drop(view)
            translated = {encode(item[1]): item[2] for item in mappings}
            for pattern, template in [(E("edge", S("a"), Var(0)), Var(0)),
                                      (E("shared", Var(0), Var(1)), E("pair", Var(0), Var(1))),
                                      (E("independent", Var(0), Var(1)), E("pair", Var(0), Var(1))),
                                      (E("boolean", True, Var(0)), Var(0))]:
                original = query(a, S("sample"), pattern, template)
                restored = query(b, S("served-copy"), pattern, template)
                expected = Counter(encode((value, tuple(translated[encode(ref)] for ref in refs))) for value, refs in original)
                assert Counter(map(encode, restored)) == expected, (original, restored)
            view, _ = b.snapshot()
            plan = E("KqRange", S("served-copy"), 3, 2, E("Inclusive", 1), E("Inclusive", 1), E("edge", S("a"), Var(0)), Var(0))
            range_rows, _ = drain(b, open_query(b, view, plan), 1, 2)
            assert len(range_rows) == 2, range_rows
            b.drop(view)

            # Real file tooling streams a corpus larger than a CAV1 packet.
            bulk = [E("property", S(f"n{i}"), str(i) + "x" * 8192) for i in range(132)]
            for start in range(0, len(bulk), 8):
                result = call(source, E("Commit", f"bulk-{start}", tuple(E("SpaceInsertV1", i, v) for i, v in enumerate(bulk[start:start + 8]))))
                assert result[0] == S("KnowledgeCommittedV1"), result
            archive_path = root / "collection.csx"
            result = subprocess.run([sys.executable, str(ROOT / "tools/cetta_space_transfer.py"), str(first.path), "export", "sample", str(archive_path)], capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, result.stderr
            assert archive_path.stat().st_size > 1048576
            denied = subprocess.run([sys.executable, str(ROOT / "tools/cetta_space_transfer.py"), str(first.path), "export", "sample", str(archive_path)], capture_output=True, timeout=20)
            assert denied.returncode != 0
            cli = [sys.executable, str(ROOT / "tools/cetta_space_transfer.py"), str(second.path), "import", "file-copy", "file-copy", str(archive_path)]
            loaded = subprocess.run(cli, capture_output=True, text=True, timeout=60)
            assert loaded.returncode == 0, loaded.stderr
            replayed = subprocess.run(cli, capture_output=True, text=True, timeout=60)
            assert replayed.returncode == 0 and json.loads(replayed.stdout)["status"] == "KnowledgeReplayedV1", replayed.stderr
            with archive_path.open("rb") as stream:
                full_header, full_footer = archive_validate(stream)
            transport = TransferClient(second.path)
            output = io.BytesIO()
            try:
                copied_header, copied_footer = export_collection(transport, S("file-copy"), output)
            finally:
                transport.close()
            assert copied_header[4] == full_header[4] == len(values) + 1 + len(bulk)
            with archive_path.open("rb") as original:
                expected = Counter(raw(frame[3]) for frame in archive_scan(original) if isinstance(frame, tuple) and frame[0] == S("SpaceExportRowV1"))
            actual = Counter(raw(frame[3]) for frame in archive_scan(io.BytesIO(output.getvalue())) if isinstance(frame, tuple) and frame[0] == S("SpaceExportRowV1"))
            assert actual == expected

            # Fixed KB repeated cancellation/disconnect must retire resources.
            samples = []
            for i in range(args.rounds + 24):
                transient = client(second)
                state = opened(transient, S("cancelled"), "cancelled", header, footer)
                state = use(transient, state[1], "ImportRow", rows[0])
                view, _ = transient.snapshot()
                exported = transient.route("ExportOpen", E("ExportOpenV1", S("served-copy"), 131072), view)
                transient.drop(view)
                exported = use(transient, exported[1], "ExportNext")
                assert exported[0] == S("ServiceResourceV1"), exported
                if i % 2 == 0:
                    assert use(transient, state[1], "ImportCancel") == S("SpaceImportClosedV1")
                    assert use(transient, exported[1], "ExportCancel") == S("SpaceExportClosedV1")
                transient.close()
                if i in (23, args.rounds + 23):
                    await_resources(b, 0); samples.append(second.sample(b, i - 23))
            assert samples[0]["descriptors"] == samples[1]["descriptors"], samples
            assert samples[0]["stats"] == samples[1]["stats"], samples
            report = {"small_occurrences": len(rows), "large_occurrences": full_header[4], "archive_bytes": archive_path.stat().st_size,
                      "malformed_files": len(bad_files), "malformed_rows": len(cases), "rounds": args.rounds,
                      "samples": samples, "embedded_service_file_restart": True, "stale_spool_alias_refused": True}
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, indent=2) + "\n")
            print("PASS: bounded transfer, exact payload bags/origin maps, atomic indexes, malformed streams, stale state, conflicts, lost replies/restart and cleanup")
        finally:
            for connection in clients:connection.close()
            first.stop(); second.stop()
            for instance in workers:instance.close()


if __name__ == "__main__":
    main()
