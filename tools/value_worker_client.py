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
    def __init__(self, binary, module, entry, timeout=30):
        self.timeout = timeout
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
            request_id = uuid.uuid4().hex
            data = encode(E("work", request_id, value))
            packet = b"CSP1" + struct.pack(">I", len(data)) + data
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
                response = decode(transfer(size=length))
                if not isinstance(response, tuple) or len(response) != 4 or response[:2] != (S("reply"), request_id):
                    raise ValueError("uncorrelated worker reply")
                if response[2] != S("ok"):
                    raise RuntimeError("worker refused: " + repr(response[3]))
                return response[3]
            except BaseException:
                # A timed-out request must never contaminate a later call.
                self.close()
                raise
