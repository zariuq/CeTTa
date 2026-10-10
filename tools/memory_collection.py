"""Chroma-shaped local adapter to the MeTTa memory collection library.

The named space is authoritative. The worker holds a disposable snapshot;
profile checks, matching, filters, scoring and mutation plans run in MeTTa.
This transport adapter does not run an embedding model or require Chroma.
"""
from __future__ import annotations
from array import array
import hashlib
import json
import math
from pathlib import Path
import struct
import threading
import time

from space_service_client import Client, E, S, Var, Tensor, FloatBits, Uncertain, ServiceError, encode
from value_worker_client import Worker


def plain(value):
    if isinstance(value, FloatBits):
        return struct.unpack("<d", struct.pack("<Q", value.bits))[0]
    if isinstance(value, tuple):
        return [plain(x) for x in value]
    return value


def metadata_atom(metadata, allow_delete=False):
    def atom(value):
        if value is None and allow_delete:
            return S("MemoryDeleteField")
        if isinstance(value, list):
            return tuple(atom(x) for x in value)
        if not isinstance(value, (str, bool, int, float)):
            raise TypeError("unsupported metadata value")
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("nonfinite metadata")
        return value
    return tuple((str(k), atom(v)) for k, v in sorted((metadata or {}).items()))


def metadata_python(value):
    return {key: plain(v) for key, v in value}


def where_atom(where):
    if not where:
        return S("All")
    if not isinstance(where, dict) or len(where) != 1:
        raise ValueError("one filter operator required")
    key, value = next(iter(where.items()))
    if key in ("$and", "$or"):
        if not isinstance(value, list) or not value:
            raise ValueError("nonempty filter operands required")
        return E("And" if key == "$and" else "Or", tuple(where_atom(v) for v in value))
    if key.startswith("$"):
        raise ValueError("unsupported filter operator")
    if isinstance(value, dict):
        if list(value) != ["$eq"]:
            raise ValueError("this profile supports equality, and, or filters")
        value = value["$eq"]
    if not isinstance(value, (str, bool, int, float)):
        raise TypeError("scalar equality operand required")
    return E("Eq", key, value)


class NativeCollection:
    def __init__(self, socket, name, profile, binary, module=None):
        self.client = Client(str(socket), timeout=30)
        if S("tensor-ctn1-native") not in self.client.hello()[5][1:]:
            raise RuntimeError("space service does not advertise native tensors")
        self.space = self.client.open(name, "native")
        self.metadata = dict(profile)
        if profile.get("hnsw:space", "cosine") != "cosine":
            raise ValueError("this collection profile requires cosine")
        self.dimensions = int(profile["embedding_output_dim"])
        if self.dimensions < 1:
            raise ValueError("positive embedding dimensions required")
        # Operational index parameters do not change the embedding's meaning.
        semantic = {k: v for k, v in profile.items() if not k.startswith("hnsw:")}
        semantic.update(metric="cosine", representation="f64-copy-of-source")
        profile_json = json.dumps(semantic, sort_keys=True, separators=(",", ":"))
        self.profile = hashlib.sha256(profile_json.encode()).hexdigest()
        contract = E("MemoryProfile", self.profile, profile_json)
        existing = self.space.match(E("MemoryProfile", Var(0), Var(1)),
                                    E("MemoryProfile", Var(0), Var(1)))
        if not existing:
            if self.space.count:
                raise ValueError("nonempty space without collection profile")
            self.space.add(contract)
        elif existing != [contract]:
            raise ValueError("persisted embedding profile mismatch")
        self.binary = str(binary)
        self.module = str(module or Path(__file__).resolve().parents[1] / "lib/petta/lib_memory_collection.metta")
        self.worker = None
        self.revision = None
        self.churn = 0
        self.lock = threading.RLock()

    def close(self):
        if self.worker:
            self.worker.close(); self.worker = None
        self.client.close()

    def _discard_worker(self):
        worker, self.worker = self.worker, None
        if worker:
            try:
                worker.close()
            except Exception as error:
                # Cache cleanup must not undo a durable success or prevent the
                # next read from reconstructing a failed worker.
                self.last_cache_error = type(error).__name__

    def _call(self, request):
        value = self.worker.call(request)
        if isinstance(value, tuple) and value and value[0] == S("MemoryFailure"):
            raise ValueError("MeTTa collection refused: " + repr(value))
        return value

    def _read_only(self, function):
        # A restarted owner drops connections and cursor leases. Retry only a
        # complete read: never resume an old page or resubmit a mutation.
        deadline = time.monotonic() + 5
        for attempt in range(21):
            try:
                return function()
            except ServiceError as error:
                if error.reason not in (S("cursor-identity"), S("expired-or-closed")):
                    raise
                if attempt == 20 or time.monotonic() >= deadline:
                    raise
            except (Uncertain, ConnectionError, FileNotFoundError, EOFError):
                self.client.close()
                if attempt == 20 or time.monotonic() >= deadline:
                    raise
            time.sleep(.25)

    def _refresh(self):
        return self._read_only(self._refresh_once)

    def _refresh_once(self):
        current = self.space.describe()[6]
        if self.worker and self.worker.process.poll() is None and self.revision == current and self.churn < 128:
            return
        self._discard_worker()
        # The server snapshot captures all fields, including embeddings, at one
        # revision. Mutations during paging cannot mix versions into the view.
        row = E("Memory", *(Var(i) for i in range(6)))
        snapshot = self.space.query(row, row, limit=65536)
        rows, offset = [], 0
        try:
            while True:
                page = self.space.fetch(snapshot, offset, 32)
                rows.extend(page[3][1:])
                if page[5]:
                    break
                offset = page[4]
        except BaseException:
            try:
                self.space.close_query(snapshot)
            except (ServiceError, Uncertain, OSError):
                pass
            raise
        else:
            self.space.close_query(snapshot)
        rows.sort(key=lambda r: r[1])
        if len({r[1] for r in rows}) != len(rows):
            raise ValueError("duplicate memory identities in source")
        worker = Worker(self.binary, self.module, "memory:dispatch")
        try:
            if worker.call(E("load", self.profile, self.dimensions, ())) != E("Loaded", 0):
                raise RuntimeError("worker profile initialization failed")
            for start in range(0, len(rows), 32):
                chunk = tuple(rows[start:start+32])
                if worker.call(E("load", self.profile, self.dimensions, chunk)) != E("Loaded", len(chunk)):
                    raise RuntimeError("incomplete worker load")
        except BaseException:
            worker.close(); raise
        self.worker, self.revision = worker, snapshot[4]
        self.churn = 0
        self.space.revision = self.revision

    def _commit(self, operations):
        if not operations:
            return
        import uuid
        request_id = uuid.uuid4().hex
        committed = False
        try:
            self.space.batch(*operations, request_id=request_id)
            committed = True
        except Uncertain:
            # The retained receipt contains the original reply. Absence is
            # uncertainty; do not retry with a refreshed revision.
            receipt = self.client.call("receipt", self.space.uuid, request_id)
            if (not isinstance(receipt, tuple) or len(receipt) != 4 or
                    receipt[:3] != (S("reply"), request_id, S("ok"))):
                raise
            self.space.revision = receipt[3][3]
            committed = True
        finally:
            self.revision = None
        if committed:
            # A failed cache update cannot undo a synchronized successful write.
            # Discard the view and reconstruct it on the next operation.
            try:
                if self._call(E("apply", tuple(operations))) != E("Applied", len(operations)):
                    raise RuntimeError("incomplete cache update")
                self.revision = self.space.revision
                self.churn += 1
            except Exception:
                self._discard_worker()

    def count(self):
        with self.lock:
            return self._read_only(lambda: self.space.match_count(E("Memory", *(Var(i) for i in range(6)))))

    def get(self, ids=None, where=None, include=None, limit=None, offset=0):
        include = ["documents", "metadatas"] if include is None else include
        with self.lock:
            self._refresh()
            selected = list(self._call(E("ids", where_atom(where))))
            if ids is not None:
                wanted = set(ids); selected = [i for i in selected if i in wanted]
            selected = selected[offset:] if limit is None else selected[offset:offset+limit]
            result = {"ids": [], "documents": [], "metadatas": [], "embeddings": []}
            for start in range(0, len(selected), 32):
                command = "records" if "embeddings" in include else "get"
                for row in self._call(E(command, tuple(selected[start:start+32]))):
                    if command == "records":
                        _, item_id, revision, profile, vector, doc, meta = row
                        values = array("d")
                        values.frombytes(vector.encoded[9 + 8 * len(vector.shape):])
                        result["embeddings"].append(values)
                    else:
                        _, item_id, revision, doc, meta = row
                    result["ids"].append(item_id)
                    result["documents"].append(doc)
                    result["metadatas"].append(metadata_python(meta))
            return {key: value if key == "ids" or key in include else None for key, value in result.items()}

    def query(self, query_embeddings, n_results=10, where=None, include=None):
        if not isinstance(n_results, int) or not 0 < n_results <= 1000:
            raise ValueError("n_results must be between 1 and 1000")
        include = ["documents", "metadatas", "distances"] if include is None else include
        with self.lock:
            self._refresh()
            result = {k: [] for k in ("ids", "documents", "metadatas", "distances")}
            for query in query_embeddings:
                tensor = self._vector(query)
                found = self._call(E("query", self.profile, tensor, n_results, where_atom(where)))
                result["ids"].append([r[1][1] for r in found])
                result["documents"].append([r[1][3] for r in found])
                result["metadatas"].append([metadata_python(r[1][4]) for r in found])
                result["distances"].append([1.0 - plain(r[0]) for r in found])
            return {key: value if key == "ids" or key in include else None for key, value in result.items()}

    def _vector(self, value):
        tensor = value if isinstance(value, Tensor) else Tensor.vector(value)
        if tensor.shape != (self.dimensions,):
            raise ValueError("embedding dimension mismatch")
        return tensor

    def upsert(self, ids, embeddings, documents=None, metadatas=None, _insert=False):
        if len(set(ids)) != len(ids) or not all(isinstance(i, str) and i for i in ids):
            raise ValueError("distinct nonempty string IDs required")
        if not (len(ids) == len(embeddings) == len(documents) == len(metadatas)):
            raise ValueError("batch field lengths differ")
        with self.lock:
            self._refresh()
            operations = []
            for item_id, vector, doc, meta in zip(ids, embeddings, documents, metadatas):
                if not isinstance(doc, str):
                    raise TypeError("document must be text")
                record = E("Memory", item_id, self.revision+1, self.profile,
                           self._vector(vector), doc, metadata_atom(meta))
                operations.extend(self._call(E("insert" if _insert else "upsert", self.profile, self.dimensions, record)))
            # Check frame admission before any effect; a batch is never split
            # silently. Migration bulk callers choose their chunk size.
            encode(E("ops", *operations))
            self._commit(operations)

    def add(self, ids, embeddings, documents=None, metadatas=None):
        self.upsert(ids, embeddings, documents, metadatas, _insert=True)

    def delete(self, ids=None, where=None):
        with self.lock:
            self._refresh()
            selected = self.get(ids=ids, where=where, include=[])["ids"]
            self._commit(self._call(E("delete", tuple(selected))))

    def update(self, ids, metadatas):
        if len(ids) != len(metadatas) or len(ids) != len(set(ids)):
            raise ValueError("invalid update batch")
        with self.lock:
            self._refresh()
            operations = []
            for item_id, meta in zip(ids, metadatas):
                operations.extend(self._call(E("patch", self.revision+1, item_id, metadata_atom(meta, allow_delete=True))))
            self._commit(operations)
