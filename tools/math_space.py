#!/usr/bin/env python3
"""Drive a persistent Prime mathematical workspace with JSON Lines commands."""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
import math
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time
from threading import Lock, Thread

ROOT = Path(__file__).resolve().parents[1]


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON field: {key}")
        result[key] = value
    return result


def decode(text):
    def nonfinite(value):
        raise ValueError(f"non-finite JSON number: {value}")
    return json.loads(text, object_pairs_hook=unique_object, parse_constant=nonfinite)


class WorkspaceError(RuntimeError):
    def __init__(self, state, reason, request_id="", detail=""):
        super().__init__(reason)
        self.reply = {"schema": "prime.math-space.reply.v1", "id": request_id,
                      "state": state, "reason": reason}
        if detail:
            self.reply["detail"] = detail


class MathSpaceSession:
    """One live C process; restarting requires an explicit checkpoint load.

    The filesystem root is a controller policy, not an OS sandbox. The controller
    must isolate the runtime separately when processing hostile native code.
    """
    def __init__(self, binary: Path, workspace_root: Path, timeout: float = 30,
                 lean_root: Path | None = None, reply_characters: int = 16384):
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be positive")
        if type(reply_characters) is not int or reply_characters <= 0:
            raise ValueError("reply characters must be a positive integer")
        self.workspace_root = workspace_root.resolve(strict=True)
        if not self.workspace_root.is_dir():
            raise ValueError("workspace root must be a directory")
        self.timeout = timeout
        self.roundtrip = Lock()
        self.stderr = deque(maxlen=128)
        env = dict(os.environ)
        env.pop("CETTA_MATH_LEAN_ROOT", None)
        env.pop("CETTA_MATH_LEAN_PIN", None)
        env["CETTA_MATH_WORKSPACE_ROOT"] = str(self.workspace_root)
        env["CETTA_MATH_REPLY_CHARACTERS"] = str(reply_characters)
        self.backend_files = {}
        self.lean_root = None
        if lean_root is not None:
            lean_root = lean_root.resolve(strict=True)
            self.lean_root = lean_root
            for suffix in ("itp-guard", "sysroot.txt", "lib/MegLib.olean", "lib/Bridge.olean", "lib/CettaProofTactics.olean"):
                path = lean_root / ".cetta/lean-native" / suffix
                self.backend_files[path] = hashlib.sha256(path.read_bytes()).hexdigest()
            env["CETTA_MATH_LEAN_ROOT"] = str(lean_root)
            env["CETTA_MATH_LEAN_PIN"] = json.dumps(
                {str(path.relative_to(lean_root)): sha for path, sha in self.backend_files.items()})
        self.process = subprocess.Popen(
            [str(binary.resolve(strict=True)), "--lang", "prime", "--no-results",
             str(ROOT / "examples/math_space_agent.metta")],
            cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, bufsize=0, start_new_session=True, env=env)
        os.set_blocking(self.process.stdin.fileno(), False)
        os.set_blocking(self.process.stdout.fileno(), False)
        self.pending = bytearray()
        self.stderr_thread = Thread(target=self._drain_stderr, daemon=True)
        self.stderr_thread.start()

    def _drain_stderr(self):
        for chunk in iter(lambda: self.process.stderr.read(2048), b""):
            self.stderr.append(chunk.decode("utf-8", errors="replace"))

    def close(self):
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.process.wait(timeout=5)
        for stream in (self.process.stdin, self.process.stdout):
            if stream:
                stream.close()
        self.stderr_thread.join(timeout=1)
        if self.process.stderr:
            self.process.stderr.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _path_policy(self, request):
        result = dict(request)
        field = "certificate" if result.get("command") == "lean-replay" else "path"
        if result.get("command") in ("save", "load", "lean-replay") and isinstance(result.get(field), str):
            path = Path(result[field])
            if not path.is_absolute():
                path = self.workspace_root / path
            target = path.resolve()
            if not target.is_relative_to(self.workspace_root):
                raise WorkspaceError("refused", "path-outside-workspace", result.get("id", ""))
            result[field] = str(target)
        return result

    def request(self, request):
        with self.roundtrip:
            return self._request(request)

    def _request(self, request):
        if not isinstance(request, dict) or not isinstance(request.get("id"), str):
            raise WorkspaceError("refused", "request-needs-text-id")
        request = self._path_policy(request)
        rid = request["id"]
        if request.get("command") == "lean-replay":
            self._check_backend(rid)
        if self.process.poll() is not None:
            raise WorkspaceError("unavailable", "session-closed", rid, "".join(self.stderr))
        wire = (json.dumps(request, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n").encode()
        deadline, written = time.monotonic() + self.timeout, 0
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            selector.register(self.process.stdin, selectors.EVENT_WRITE)
            while b"\n" not in self.pending:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self.close()
                    state = "uncertain" if request.get("command") == "save" else "unfinished"
                    raise WorkspaceError(state, "operation-timeout-session-closed", rid)
                for key, _ in selector.select(remaining):
                    if key.fileobj is self.process.stdin:
                        try:
                            written += os.write(key.fd, wire[written:])
                        except BlockingIOError:
                            continue
                        except BrokenPipeError:
                            self.close()
                            raise WorkspaceError("unavailable", "runtime-closed-input", rid) from None
                        if written == len(wire):
                            selector.unregister(self.process.stdin)
                    else:
                        try:
                            chunk = os.read(key.fd, 65536)
                        except BlockingIOError:
                            continue
                        if not chunk:
                            self.close()
                            raise WorkspaceError("unavailable", "runtime-closed-output", rid, "".join(self.stderr))
                        self.pending.extend(chunk)
        line, _, rest = self.pending.partition(b"\n")
        self.pending = bytearray(rest)
        try:
            reply = decode(line.decode("utf-8"))
            if (reply.get("schema") != "prime.math-space.reply.v1" or reply.get("id") != rid
                    or not isinstance(reply.get("state"), str)):
                raise ValueError("unexpected reply contract")
        except (ValueError, AttributeError) as error:
            self.close()
            raise WorkspaceError("fault", "runtime-protocol", rid, str(error)) from error
        if request.get("command") == "lean-replay":
            self._check_backend(rid)
            reply["backend_files"] = {str(path.relative_to(self.lean_root)): sha
                                      for path, sha in self.backend_files.items()}
        return reply

    def _check_backend(self, rid):
        try:
            current = all(hashlib.sha256(path.read_bytes()).hexdigest() == sha
                          for path, sha in self.backend_files.items())
        except OSError:
            current = False
        if not current:
            self.close()
            raise WorkspaceError("unavailable", "selected-lean-backend-changed", rid)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "cetta")
    parser.add_argument("--workspace-root", type=Path, default=Path.cwd())
    parser.add_argument("--timeout", type=float, default=30, help="wall-clock seconds per operation")
    parser.add_argument("--lean-root", type=Path, help="explicit ai-mathematician checkout providing the receipt guard")
    parser.add_argument("--reply-characters", type=int, default=16384, help="maximum characters of result text per reply")
    args = parser.parse_args()
    with MathSpaceSession(args.binary, args.workspace_root, args.timeout, args.lean_root,
                          args.reply_characters) as session:
        for line in sys.stdin:
            request = None
            try:
                request = decode(line)
                reply = session.request(request)
            except (ValueError, TypeError) as error:
                rid = request.get("id", "") if isinstance(request, dict) else ""
                reply = WorkspaceError("refused", "invalid-json-request", rid, str(error)).reply
            except WorkspaceError as error:
                reply = error.reply
            print(json.dumps(reply, ensure_ascii=False, allow_nan=False), flush=True)


if __name__ == "__main__":
    main()
