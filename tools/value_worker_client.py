"""Bounded binary RPC to an operator-selected local CeTTa program."""
import os
import select
import struct
import subprocess
import threading
import time
import uuid

if __package__:
    from .space_service_client import E, S, encode, decode, MAX_BYTES
else:
    from space_service_client import E, S, encode, decode, MAX_BYTES


class WorkerUnavailable(RuntimeError):
    """A disposable evaluator failed; its outstanding reply was discarded."""


class Worker:
    def __init__(self, binary, module, entry, timeout=30, profile=False,
                 *, language=None, num_threads=None, language_profile=None, import_mode=None):
        if language is not None and language not in ("petta", "prime"):
            raise ValueError("invalid worker language")
        if num_threads is not None and (type(num_threads) is not int or not 0 <= num_threads <= 1024):
            raise ValueError("invalid worker thread count")
        if import_mode is not None and import_mode not in ("upstream", "relative", "ancestor-walk"):
            raise ValueError("invalid worker import mode")
        self.timeout = timeout
        self.profile = profile
        self.last_metrics = None
        self.lock = threading.Lock()
        self.command = [str(binary)]
        if language is not None:
            self.command += ["--lang", language]
        if language_profile is not None:
            self.command += ["--profile", language_profile]
        if import_mode is not None:
            self.command += ["--import-mode", import_mode]
        if num_threads is not None:
            self.command += ["--num-threads", str(num_threads)]
        self.command += [str(module), entry]
        self.closed = False
        self.process = None
        self._start()

    def _start(self):
        if self.closed:
            raise WorkerUnavailable("CeTTa worker is closed")
        self.process = subprocess.Popen(self.command,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        os.set_blocking(self.process.stdin.fileno(), False)
        os.set_blocking(self.process.stdout.fileno(), False)

    def _discard(self):
        process, self.process = self.process, None
        if process is None:
            return None
        process.stdin.close()
        if process.poll() is None:
            try:
                process.wait(timeout=.2)
            except subprocess.TimeoutExpired:
                process.terminate()
                try: process.wait(timeout=.2)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
        process.stdout.close()
        return process.returncode

    def close(self):
        with self.lock:
            self.closed = True
            return self._discard()

    def call(self, value):
        with self.lock:
            if self.closed:
                raise WorkerUnavailable("CeTTa worker is closed")
            if self.process is None or self.process.poll() is not None:
                self._discard()
                self._start()
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
            except (OSError, EOFError, ValueError) as error:
                # A timed-out request must never contaminate a later call.
                self._discard()
                raise WorkerUnavailable("CeTTa worker transport failed: " + type(error).__name__) from error
            except BaseException:
                self._discard()
                raise
