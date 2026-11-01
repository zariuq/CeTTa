#!/usr/bin/env python3
"""Bounded CAV1 collection transfer through the native MeTTa value service.

CSX1 archives contain length-framed header, rows and completion footer.
They are logical copies, not identity-preserving physical backups. Import
checks the entire file before staging, and the owner rechecks its digest
before atomic publication. No command or stored program is evaluated here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import uuid

from space_service_client import E, S, encode, decode

FRAME_MAX = 131072
REPLY_MAX = 262144


def tagged(value, tag, size):
    return isinstance(value, tuple) and len(value) == size and value[0] == S(tag)


class TransferUncertain(RuntimeError):
    """A reply was lost; inspect the durable receipt before retrying a write."""


class Client:
    def __init__(self, path, timeout=30):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        self.socket.connect(str(path))

    def close(self):
        self.socket.close()

    def _read(self, count):
        data = bytearray()
        while len(data) < count:
            part = self.socket.recv(count - len(data))
            if not part:
                raise EOFError("service disconnected")
            data.extend(part)
        return bytes(data)

    def call(self, body):
        identifier = uuid.uuid4().hex
        packet = encode(E("request", identifier, body))
        if len(packet) > REPLY_MAX:
            raise ValueError("service request frame limit")
        try:
            self.socket.sendall(b"CSP1" + struct.pack(">I", len(packet)) + packet)
            head = self._read(8)
            size = struct.unpack(">I", head[4:])[0]
            if head[:4] != b"CSP1" or not 4 <= size <= REPLY_MAX:
                raise ValueError("invalid service frame")
            reply = decode(self._read(size))
            if not tagged(reply, "reply", 4) or reply[1] != identifier:
                raise ValueError("invalid service reply")
        except (OSError, EOFError, ValueError) as error:
            self.close()
            raise TransferUncertain(f"request {identifier}: {error}") from error
        if reply[2] != S("ok"):
            raise RuntimeError(f"service refused: {reply[3]!r}")
        return reply[3]

    def route(self, operation, arguments=(), token=None):
        body = E("CallV1", S(operation), arguments) if token is None else E("UseV1", token, S(operation), arguments)
        return self.call(body)

    def use(self, token, operation, arguments=()):
        return self.call(E("UseValueV1", token, S(operation), arguments))

    def drop(self, token):
        return self.call(E("DropV1", token))


def frame_write(output, value):
    packet = encode(value)
    if len(packet) > FRAME_MAX:
        raise ValueError("archive frame limit")
    output.write(struct.pack(">I", len(packet)))
    output.write(packet)


def frame_read(source):
    head = source.read(4)
    if len(head) != 4:
        raise ValueError("missing archive completion frame")
    size = struct.unpack(">I", head)[0]
    if not 4 <= size <= FRAME_MAX:
        raise ValueError("archive frame limit")
    packet = source.read(size)
    if len(packet) != size:
        raise ValueError("truncated archive frame")
    value = decode(packet)
    if encode(value) != packet:
        raise ValueError("noncanonical archive frame")
    return value


def archive_scan(source):
    """Yield checked frames; retaining a frame is the caller's choice."""
    if source.read(4) != b"CSX1":
        raise ValueError("unsupported collection archive")
    header = frame_read(source)
    if not tagged(header, "SpaceExportHeaderV1", 6) or type(header[4]) is not int or header[4] < 0:
        raise ValueError("invalid archive header")
    digest = hashlib.sha256(encode(header)).hexdigest()
    yield header
    previous = None
    for sequence in range(header[4]):
        row = frame_read(source)
        if not tagged(row, "SpaceExportRowV1", 4) or type(row[1]) is not int or row[1] != sequence:
            raise ValueError("invalid archive row sequence")
        origin = row[2]
        if not tagged(origin, "StoreOccurrenceV1", 3) or origin[1] != header[1] or not isinstance(origin[2], (str, bytes)):
            raise ValueError("invalid archive origin")
        key = origin[2].encode("utf-8") if isinstance(origin[2], str) else origin[2]
        prefix = b"space/rows/v1/" + encode(header[3]) + b"/"
        if not key.startswith(prefix) or (previous is not None and key <= previous):
            raise ValueError("unordered or foreign archive origin")
        if not isinstance(row[3], (str, bytes)):
            raise ValueError("invalid archive atom")
        payload = row[3].encode("utf-8") if isinstance(row[3], str) else row[3]
        if encode(decode(payload)) != payload:
            raise ValueError("noncanonical archive atom")
        digest = hashlib.sha256(encode(E("SpaceExportLinkV1", digest, row))).hexdigest()
        previous = key
        yield row
    footer = frame_read(source)
    if footer != E("SpaceExportEndV1", header[4], digest) or source.read(1):
        raise ValueError("incomplete, corrupt or trailing archive data")
    yield footer


def archive_validate(source):
    frames = archive_scan(source)
    header = next(frames)
    footer = None
    for footer in frames:
        pass
    return header, footer


def export_collection(client, collection, output):
    resource = None
    view = client.route("Snapshot")
    if not tagged(view, "ServiceResourceV1", 3):
        raise RuntimeError(f"snapshot failed: {view!r}")
    try:
        opened = client.route("ExportOpen", E("ExportOpenV1", collection, FRAME_MAX), view[1])
        if not tagged(opened, "ServiceResourceV1", 3):
            raise RuntimeError(f"export failed: {opened!r}")
        resource, header = opened[1:]
    finally:
        client.drop(view[1])
    try:
        output.write(b"CSX1")
        frame_write(output, header)
        digest = hashlib.sha256(encode(header)).hexdigest()
        count = 0
        while True:
            answer = client.use(resource, "ExportNext")
            resource = None
            if tagged(answer, "ServiceResourceV1", 3):
                resource, row = answer[1:]
                if not tagged(row, "SpaceExportRowV1", 4) or row[1] != count:
                    raise ValueError("invalid export row")
                digest = hashlib.sha256(encode(E("SpaceExportLinkV1", digest, row))).hexdigest()
                frame_write(output, row)
                count += 1
            elif answer == E("SpaceExportCompleteV1", E("SpaceExportEndV1", count, digest)) and count == header[4]:
                frame_write(output, answer[1])
                return header, answer[1]
            else:
                raise RuntimeError(f"export unfinished: {answer!r}")
    finally:
        if resource is not None:
            client.use(resource, "ExportCancel")


def import_collection(client, collection, request, source, maximum=64 * 1024 * 1024):
    header, footer = archive_validate(source)
    source.seek(0)
    frames = archive_scan(source)
    if next(frames) != header:
        raise ValueError("archive changed between passes")
    view = client.route("Snapshot")
    if not tagged(view, "ServiceResourceV1", 3):
        raise RuntimeError(f"snapshot failed: {view!r}")
    try:
        answer = client.route("ImportOpen", E("ImportOpenV1", collection, request, header, footer[2], maximum), view[1])
    finally:
        client.drop(view[1])
    if tagged(answer, "KnowledgeReplayedV1", 2):
        return answer
    if not tagged(answer, "ServiceResourceV1", 3):
        raise RuntimeError(f"import open failed: {answer!r}")
    resource = answer[1]
    try:
        for frame in frames:
            if tagged(frame, "SpaceExportEndV1", 3):
                if frame != footer:
                    raise ValueError("archive changed between passes")
                answer = client.use(resource, "ImportFinish", footer)
                resource = None
                if not tagged(answer, "KnowledgeCommittedV1", 4):
                    raise RuntimeError(f"import not committed: {answer!r}")
                return answer
            answer = client.use(resource, "ImportRow", frame)
            resource = None
            if not tagged(answer, "ServiceResourceV1", 3):
                raise RuntimeError(f"import row refused: {answer!r}")
            resource = answer[1]
        raise ValueError("missing archive completion")
    finally:
        if resource is not None:
            client.use(resource, "ImportCancel")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("socket", type=Path)
    sub = parser.add_subparsers(dest="operation", required=True)
    export = sub.add_parser("export")
    export.add_argument("collection", help="collection symbol")
    export.add_argument("archive", type=Path)
    load = sub.add_parser("import")
    load.add_argument("collection", help="new destination collection symbol")
    load.add_argument("request", help="durable retry id (text)")
    load.add_argument("archive", type=Path)
    load.add_argument("--spool-bytes", type=int, default=64 * 1024 * 1024)
    args = parser.parse_args()
    client = Client(args.socket)
    try:
        if args.operation == "export":
            with args.archive.open("xb") as output:
                header, footer = export_collection(client, S(args.collection), output)
                output.flush()
                os.fsync(output.fileno())
            descriptor = os.open(args.archive.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
            print(json.dumps({"occurrences": header[4], "digest": footer[2]}))
        else:
            with args.archive.open("rb") as source:
                answer = import_collection(client, S(args.collection), args.request, source, args.spool_bytes)
            details = {"status": answer[0].name}
            if tagged(answer, "KnowledgeCommittedV1", 4):
                details.update(epoch=answer[2], revision=answer[3])
            print(json.dumps(details))
    finally:
        client.close()


if __name__ == "__main__":
    main()
