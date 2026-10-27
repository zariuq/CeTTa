"""Independent CDR1/CSL1 oracle for native durable-service tests.

Read one journal inode so a concurrent checkpoint cannot splice generations.
A partial final append is uncommitted; complete corrupt frames are errors.
This module never evaluates a stored value or obtains service authority.
"""
import hashlib
import struct
from pathlib import Path

BASE = struct.Struct('>4sB32sQQ')
ROW = struct.Struct('>BIIIQQ')


def frames(path):
    with open(path, 'rb') as stream:
        data = stream.read()
    offset = 0
    while offset < len(data):
        if len(data) - offset < 72:
            break
        magic, length = struct.unpack_from('>4sI', data, offset)
        assert magic == b'CSL1', (path, offset, magic)
        end = offset + 72 + length
        if end > len(data):
            break
        payload = data[offset + 72:end]
        assert hashlib.sha256(payload).hexdigest().encode() == data[offset + 8:offset + 72], (path, offset)
        yield payload
        offset = end


def read(path):
    records, epoch, revision, remaining = {}, None, None, 0
    for frame in frames(path):
        magic, kind, current, rev, count = BASE.unpack_from(frame)
        assert magic == b'CDR1'
        if epoch is None:
            assert kind == 0 and len(frame) == BASE.size
            epoch, revision, remaining = current, rev, count
            continue
        assert current == epoch
        assert (kind == 2 and remaining and rev == revision and count == 1) or (
            kind == 1 and not remaining and rev == revision + 1 and count)
        offset = BASE.size
        for position in range(count):
            op, ns, nk, nv, record_rev, record_pos = ROW.unpack_from(frame, offset)
            offset += ROW.size
            name = frame[offset:offset + ns].decode(); offset += ns
            key = frame[offset:offset + nk].decode(); offset += nk
            value = frame[offset:offset + nv]; offset += nv
            identity = name, key
            assert len(value) == nv and 0 < record_rev <= rev
            assert kind == 2 or (record_rev == rev and record_pos == position)
            assert (op == 1) == (identity not in records)
            if op == 3:
                assert not nv
                del records[identity]
            else:
                assert op in (1, 2)
                records[identity] = value
        assert offset == len(frame)
        if kind == 2:
            remaining -= 1
        else:
            revision = rev
    assert not remaining
    return records


def records(path, space):
    try:
        return {key: value for (name, key), value in read(path).items() if name == space}
    except FileNotFoundError:
        return {}


def damage_value(path, key):
    """Forge an application value while preserving framing and store identities."""
    data = []
    changed = 0
    for raw in frames(path):
        frame = bytearray(raw)
        _, kind, _, _, count = BASE.unpack_from(frame)
        offset = BASE.size
        if kind:
            for _ in range(count):
                _, ns, nk, nv, _, _ = ROW.unpack_from(frame, offset)
                offset += ROW.size + ns
                current = frame[offset:offset + nk].decode(); offset += nk
                if current == key and nv:
                    frame[offset] = ord('X'); changed += 1
                offset += nv
        data.append(b'CSL1' + struct.pack('>I', len(frame)) +
                    hashlib.sha256(frame).hexdigest().encode() + frame)
    assert changed
    Path(path).write_bytes(b''.join(data))
