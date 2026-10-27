#!/usr/bin/env python3
"""Independent standard-library CSP1 client. No evaluator or native extension.

Expressions are tuples, symbols are Symbol, and text/bytes are string values.
Variables are scoped to a packet. Floating-point bits are preserved explicitly.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import socket
import select
import struct
import time
import uuid

MAX_BYTES = 1024 * 1024
MAX_NODES = 65536
MAX_DEPTH = 64
MAX_VARS = 4096


@dataclass(frozen=True)
class Symbol:
    name: str | bytes


@dataclass(frozen=True)
class Var:
    key: int


@dataclass(frozen=True)
class FloatBits:
    bits: int

    @classmethod
    def of(cls, value: float) -> FloatBits:
        return cls(struct.unpack("<Q", struct.pack("<d", value))[0])


@dataclass(frozen=True)
class BigInt:
    decimal: str


@dataclass(frozen=True)
class Rational:
    spelling: str


@dataclass(frozen=True)
class Tensor:
    """Owned CTN1 bytes. Construction copies buffers and never creates scalar atoms.

    The portable payload is little endian. Buffer import requires an explicit
    supported dtype, C-contiguous layout, and a matching buffer format/shape.
    """
    encoded: bytes

    def __post_init__(self):
        data = bytes(self.encoded)
        if len(data) < 9 or data[:4] != b"CTN1" or data[4] not in (1, 2, 3, 4):
            raise ValueError("invalid tensor header")
        rank = struct.unpack_from("<I", data, 5)[0]
        if rank > (len(data) - 9) // 8:
            raise ValueError("invalid tensor shape")
        shape = struct.unpack_from("<" + "Q" * rank, data, 9)
        import math
        size = math.prod(shape) * ({3: 1, 4: 4}.get(data[4], 8))
        if size != len(data) - 9 - 8 * rank:
            raise ValueError("tensor payload size mismatch")
        object.__setattr__(self, "encoded", data)

    @property
    def shape(self):
        rank = struct.unpack_from("<I", self.encoded, 5)[0]
        return struct.unpack_from("<" + "Q" * rank, self.encoded, 9)

    @classmethod
    def from_buffer(cls, value):
        import sys
        view = memoryview(value)
        try:
            if not view.c_contiguous:
                raise ValueError("contiguous buffer required; copy explicitly first")
            formats = {"q": (1, 8), "d": (2, 8), "B": (3, 1), "f": (4, 4)}
            fmt = view.format
            if fmt.startswith(("@", "=")):
                fmt = fmt[1:]
            if fmt not in formats:
                raise ValueError("buffer dtype must be i64, f64, f32 or u8")
            dtype, width = formats[fmt]
            if width != view.itemsize or (width > 1 and sys.byteorder != "little"):
                raise ValueError("buffer representation requires explicit conversion")
            shape = tuple(view.shape)
            return cls(b"CTN1" + bytes([dtype]) + struct.pack("<I", len(shape)) +
                       struct.pack("<" + "Q" * len(shape), *shape) + view.tobytes())
        finally:
            view.release()

    @classmethod
    def vector(cls, values):
        # Python lists can be copied directly into a packed buffer; the wire
        # and evaluator see a single tensor value, not one Atom per component.
        from array import array
        return cls.from_buffer(array("d", values))


def S(name: str) -> Symbol:
    return Symbol(name)


def E(head: str, *args) -> tuple:
    return (S(head), *args)


def encode(value) -> bytes:
    output = bytearray(b"CAV1")
    variables = {}
    nodes = 0

    def append(data):
        if len(output) + len(data) > MAX_BYTES:
            raise ValueError("atom byte limit")
        output.extend(data)

    def text(tag, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        append(tag + struct.pack("<I", len(data)) + data)

    def visit(v, depth):
        nonlocal nodes
        nodes += 1
        if nodes > MAX_NODES or depth > MAX_DEPTH:
            raise ValueError("atom structure limit")
        if isinstance(v, Symbol):
            text(b"S", v.name)
        elif isinstance(v, Var):
            index = variables.setdefault(v.key, len(variables))
            if len(variables) > MAX_VARS:
                raise ValueError("variable limit")
            append(b"V" + struct.pack("<I", index))
        elif isinstance(v, bool):
            append(b"Y" if v else b"N")
        elif isinstance(v, int):
            append(b"I" + struct.pack("<q", v))
        elif isinstance(v, float):
            append(b"F" + struct.pack("<d", v))
        elif isinstance(v, FloatBits):
            append(b"F" + struct.pack("<Q", v.bits))
        elif isinstance(v, (str, bytes)):
            text(b"T", v)
        elif isinstance(v, BigInt):
            text(b"J", v.decimal)
        elif isinstance(v, Rational):
            text(b"Q", v.spelling)
        elif isinstance(v, Tensor):
            text(b"A", v.encoded)
        elif isinstance(v, tuple):
            append(b"E" + struct.pack("<I", len(v)))
            for child in v:
                visit(child, depth + 1)
        else:
            raise TypeError(f"not portable syntax: {type(v).__name__}")

    visit(value, 0)
    return bytes(output)


def decode(data: bytes):
    if len(data) > MAX_BYTES or data[:4] != b"CAV1":
        raise ValueError("invalid CAV1 packet")
    pos, nodes, variables = 4, 0, 0

    def take(n):
        nonlocal pos
        if n > len(data) - pos:
            raise ValueError("truncated atom")
        result = data[pos:pos+n]
        pos += n
        return result

    def visit(depth):
        nonlocal nodes, variables
        nodes += 1
        if nodes > MAX_NODES or depth > MAX_DEPTH:
            raise ValueError("atom structure limit")
        tag = take(1)
        if tag in (b"I", b"F"):
            value = take(8)
            return struct.unpack("<q", value)[0] if tag == b"I" else FloatBits(struct.unpack("<Q", value)[0])
        if tag in (b"Y", b"N"):
            return tag == b"Y"
        if tag == b"V":
            index = struct.unpack("<I", take(4))[0]
            if index > variables or index >= MAX_VARS:
                raise ValueError("invalid variable scope")
            if index == variables:
                variables += 1
            return Var(index)
        if tag == b"E":
            count = struct.unpack("<I", take(4))[0]
            if count > MAX_NODES - nodes or count > len(data) - pos:
                raise ValueError("expression limit")
            return tuple(visit(depth + 1) for _ in range(count))
        if tag == b"A":
            return Tensor(take(struct.unpack("<I", take(4))[0]))
        if tag in (b"S", b"T", b"J", b"Q"):
            value = take(struct.unpack("<I", take(4))[0])
            try:
                value = value.decode("utf-8")
            except UnicodeDecodeError:
                if tag in (b"J", b"Q"):
                    raise ValueError("invalid exact number") from None
            if tag == b"S":
                return Symbol(value)
            if tag == b"J":
                return BigInt(value)
            if tag == b"Q":
                return Rational(value)
            return value
        raise ValueError("unknown atom tag")

    result = visit(0)
    if pos != len(data):
        raise ValueError("trailing atom bytes")
    return result


def frame(value) -> bytes:
    packet = encode(value)
    return b"CSP1" + struct.pack(">I", len(packet)) + packet


class ServiceError(RuntimeError):
    def __init__(self, response):
        super().__init__(repr(response))
        self.response = response
        self.reason = response[3]


class Uncertain(RuntimeError):
    def __init__(self, request):
        super().__init__("reply unavailable after sending; inspect receipt, do not blindly resubmit")
        self.request = request


class Client:
    def __init__(self, path: str, timeout: float = 10):
        self.path, self.timeout, self.connection = path, timeout, None

    def close(self):
        if self.connection:
            self.connection.close()
            self.connection = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _read(self, n):
        output = bytearray()
        while len(output) < n:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("reply deadline")
            self.connection.settimeout(remaining)
            block = self.connection.recv(n - len(output))
            if not block:
                raise EOFError("closed socket")
            output.extend(block)
        return bytes(output)

    def call(self, operation: str, *args, request_id: str | None = None):
        request_id = request_id or uuid.uuid4().hex
        request = E("request", request_id, S(operation), *args)
        packet = frame(request)
        started = False
        self.deadline = time.monotonic() + self.timeout
        try:
            # An idle owner can close a previously successful connection.
            # Detect EOF before sending; never retry a packet already sent.
            if self.connection is not None:
                try:
                    readable = select.select([self.connection], [], [], 0)[0]
                    if readable and not self.connection.recv(1, socket.MSG_PEEK | socket.MSG_DONTWAIT):
                        self.close()
                except (OSError, ValueError):
                    self.close()
            if self.connection is None:
                connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                connection.settimeout(self.timeout)
                try:
                    connection.connect(self.path)
                except BaseException:
                    connection.close()
                    raise
                self.connection = connection
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("request deadline")
            self.connection.settimeout(remaining)
            started = True
            self.connection.sendall(packet)
            header = self._read(8)
            if header[:4] != b"CSP1":
                raise ValueError("invalid reply framing")
            size = struct.unpack(">I", header[4:])[0]
            if size < 4 or size > MAX_BYTES:
                raise ValueError("reply byte limit")
            response = decode(self._read(size))
            if not isinstance(response, tuple) or len(response) != 4 or response[:2] != (S("reply"), request_id):
                raise ValueError("uncorrelated reply")
            if response[2] == S("error"):
                raise ServiceError(response)
            if response[2] != S("ok"):
                raise ValueError("invalid reply status")
            return response[3]
        except ServiceError:
            raise
        except (OSError, EOFError, ValueError) as failure:
            self.close()
            if started:
                raise Uncertain(request) from failure
            raise

    def hello(self):
        return self.call("hello")

    def principal_ref(self):
        hello = self.hello()
        return self.call("principal-ref", hello[2])

    def rotate_principal(self, account):
        hello = self.hello()
        return self.call("rotate-principal", hello[2], account)

    def open(self, name: str, backend: str = "native") -> Space:
        hello = self.hello()
        return Space(self, self.call("open", hello[2], name, S(backend), hello[4]))

    def list_spaces(self):
        hello=self.hello()
        catalog=self.call("list",hello[2])
        return list(catalog[3][1:])


class Space:
    def __init__(self, client: Client, descriptor):
        self.client = client
        self.uuid, self.id, self.name, self.backend, self.multiplicity, self.revision, self.count = descriptor[1:]
        self.backend=self.backend.name
        self.multiplicity=self.multiplicity.name

    @staticmethod
    def _authority(grant):
        return () if grant is None else (E("authority", grant),)

    def describe(self, grant=None):
        value = self.client.call("describe", self.uuid, self.id, *self._authority(grant))
        self.revision, self.count = value[6:]
        return value

    def watch(self, revision: int, wait_ms: int = 30000, grant=None):
        """Wait for a source revision change; this never consumes a value.

        Use a dedicated client while waiting and a timeout longer than the
        requested lease. An owner restart invalidates the connection: reopen
        and inspect durable state. The captured revision closes the race
        between a read and registration, even if a write already happened.
        """
        return self.client.call("watch", self.uuid, self.id, revision, wait_ms,
                                *self._authority(grant))

    def batch(self, *operations, request_id: str | None = None):
        value = self.client.call("batch", self.uuid, self.id, self.revision,
                                 E("ops", *operations), request_id=request_id)
        self.revision = value[3]
        return value

    def add(self, value, **kwargs):
        return self.batch(E("add", value), **kwargs)

    def remove(self, value, **kwargs):
        return self.batch(E("remove", value), **kwargs)

    def query(self, pattern, template, limit: int = 4096, lease_ms: int = 30000, grant=None):
        return self.client.call("query", self.uuid, self.id, pattern, template, limit, lease_ms,
                                *self._authority(grant))

    def query_first(self, pattern, template, lease_ms: int = 30000, grant=None):
        """Select the first matching occurrence; this is not an exhaustive view."""
        return self.client.call("query-first", self.uuid, self.id, pattern, template, 1, lease_ms,
                                *self._authority(grant))

    def profile_query(self, pattern, template, limit: int = 4096, lease_ms: int = 30000,
                      first=False, grant=None):
        """Return (profile, view, query-work) with candidates actually examined.

        The remaining counters describe all retained path indexes, not a
        claim that a particular index alone answered this query.
        """
        return self.client.call("query-first-profile" if first else "query-profile",
                                self.uuid,self.id,pattern,template,1 if first else limit,lease_ms,
                                *self._authority(grant))

    def fetch(self, snapshot, offset: int = 0, count: int = 128):
        return self.client.call("fetch", snapshot[1], snapshot[2], offset, count)

    def close_query(self, snapshot):
        return self.client.call("close", snapshot[1], snapshot[2])

    def observe(self, reads=(), writes=(), limit: int = 4096, lease_ms: int = 30000):
        """Capture complete record scopes atomically. The handle is one-use."""
        return self.client.call("observe", self.uuid, self.id,
                                E("reads", *reads), E("writes", *writes), limit, lease_ms)

    def observe_fetch(self, observation, scope: int, offset: int = 0, count: int = 128):
        return self.client.call("observe-fetch", observation[1], observation[2], scope, offset, count)

    def observe_close(self, observation):
        return self.client.call("observe-close", observation[1], observation[2])

    def observe_watch(self, observation, wait_ms: int = 30000):
        """Transfer the observation to a wait for changes in its scopes.

        Use a dedicated connection and a client timeout longer than wait_ms.
        Closing that connection cancels the wait and releases its captured data.
        """
        return self.client.call("observe-watch", self.uuid, self.id,
                                observation[1], observation[2], wait_ms)

    def batch_observed(self, observation, *operations, request_id=None):
        value = self.client.call("batch-observed", self.uuid, self.id,
                                 observation[1], observation[2], E("ops", *operations), request_id=request_id)
        self.revision = value[3]
        return value

    def match(self, pattern, template, limit: int = 4096, grant=None):
        snapshot = self.query(pattern, template, limit, grant=grant)
        rows, offset = [], 0
        try:
            while True:
                page = self.fetch(snapshot, offset)
                rows.extend(page[3][1:])
                if page[5]:
                    break
                offset = page[4]
        except BaseException:
            try:
                self.close_query(snapshot)
            except (ServiceError, Uncertain, OSError):
                pass
            raise
        else:
            self.close_query(snapshot)
        return rows

    def match_count(self, pattern, grant=None):
        return self.client.call("count", self.uuid, self.id, pattern, *self._authority(grant))

    def verify(self, grant=None):
        return self.client.call("verify",self.uuid,self.id,*self._authority(grant))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True)
    parser.add_argument("operation", choices=("hello", "list", "demo"), default="hello", nargs="?")
    parser.add_argument("--backend", choices=("native", "native-ordered", "pathmap", "mork"), default="native")
    args = parser.parse_args()
    with Client(args.socket) as client:
        if args.operation == "hello":
            print(client.hello())
        elif args.operation == "list":
            for space in client.list_spaces():
                print(space)
        else:
            space = client.open("example-" + args.backend, args.backend)
            space.batch(E("add", E("color", S("sky"), S("blue"))),
                        E("add", E("color", S("sky"), S("blue"))))
            print(space.describe())
            print(space.match(E("color", S("sky"), Var(0)), Var(0)))


if __name__ == "__main__":
    main()
