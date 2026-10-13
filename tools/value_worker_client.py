"""Bounded binary RPC to an operator-selected local CeTTa program."""
import os
import select
import struct
import subprocess
import threading
import time
import uuid

from space_service_client import E, S, encode, decode, MAX_BYTES


class Worker:
    def __init__(self, binary, module, entry, timeout=30, profile=False):
        self.timeout = timeout
        self.profile = profile
        self.last_metrics = None
        self.lock = threading.Lock()
        self.process = subprocess.Popen([str(binary), str(module), entry],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        os.set_blocking(self.process.stdin.fileno(), False)
        os.set_blocking(self.process.stdout.fileno(), False)

    def close(self):
        self.process.stdin.close()
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill(); self.process.wait()
        self.process.stdout.close()
        if self.process.returncode > 0:
            raise RuntimeError("CeTTa worker exited with status " + str(self.process.returncode))
        return self.process.returncode

    def call(self, value):
        with self.lock:
            started = time.perf_counter_ns() if self.profile else 0
            request_id = uuid.uuid4().hex
            data = encode(E("work-profile" if self.profile else "work", request_id, value))
            packet = b"CSP1" + struct.pack(">I", len(data)) + data
            encoded_at = time.perf_counter_ns() if self.profile else 0
            deadline = time.monotonic() + self.timeout

            def transfer(data=None, size=0):
                writing = data is not None
                fd = (self.process.stdin if writing else self.process.stdout).fileno()
                output, offset = bytearray(), 0
                size = len(data) if writing else size
                while offset < size:
                    left = deadline - time.monotonic()
                    if left <= 0:
                        raise TimeoutError("CeTTa worker deadline")
                    ready = select.select([] if writing else [fd], [fd] if writing else [], [], left)
                    if not (ready[0] or ready[1]):
                        raise TimeoutError("CeTTa worker deadline")
                    try:
                        chunk = os.write(fd, data[offset:]) if writing else os.read(fd, size-offset)
                    except BlockingIOError:
                        continue
                    if not chunk:
                        raise EOFError("CeTTa worker stopped")
                    offset += chunk if writing else len(chunk)
                    if not writing:
                        output.extend(chunk)
                return bytes(output)

            try:
                transfer(packet)
                header = transfer(size=8)
                length = struct.unpack(">I", header[4:])[0]
                if header[:4] != b"CSP1" or not 4 <= length <= MAX_BYTES:
                    raise ValueError("invalid worker frame")
                raw = transfer(size=length)
                received_at = time.perf_counter_ns() if self.profile else 0
                response = decode(raw)
                decoded_at = time.perf_counter_ns() if self.profile else 0
                if not isinstance(response, tuple) or len(response) != (5 if self.profile else 4) or response[:2] != (S("reply"), request_id):
                    raise ValueError("uncorrelated worker reply")
                if self.profile:
                    fields = response[4]
                    if not isinstance(fields, tuple) or len(fields) != 7 or fields[0] != S("WorkerMetrics") or not all(isinstance(x, int) and x >= 0 for x in fields[1:]):
                        raise ValueError("invalid worker metrics")
                    self.last_metrics = dict(zip(("native_decode_ns", "native_eval_ns", "permanent_bytes",
                        "scratch_bytes", "data_symbols", "symbol_bytes"), fields[1:]))
                    self.last_metrics.update(client_encode_ns=encoded_at-started,
                        transfer_and_worker_ns=received_at-encoded_at, client_decode_ns=decoded_at-received_at,
                        request_bytes=len(packet), reply_bytes=length+8)
                if response[2] != S("ok"):
                    raise RuntimeError("worker refused: " + repr(response[3]))
                return response[3]
            except BaseException:
                # A timed-out request must never contaminate a later call.
                self.close()
                raise
