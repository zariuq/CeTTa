"""Ordinary Space queue contracts, optionally against the actual gateway Store."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import sys
import tempfile
import uuid
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S, FloatBits, MAX_BYTES
from check_native_kb_service import Server, Client
from check_space_query import use


class QueueConflict(ValueError): pass


def scalar(value):
    return struct.unpack("<d", struct.pack("<Q", value.bits))[0] if isinstance(value, FloatBits) else value


def option(value, *, encoded=True):
    if value == S("NoValueV1"): return None
    assert value[0] == S("SomeValueV1"), value
    return json.loads(value[1]) if encoded else value[1]


def some(value, *, encoded=True):
    return S("NoValueV1") if value is None else E("SomeValueV1", json.dumps(value) if encoded else value)


class NativeStore:
    """Qualification client only: a replayed claim grants no dispatch right."""
    def __init__(self, binary, root, agent, *, service, maximum_bytes=MAX_BYTES):
        self.path = root / "store"; self.path.mkdir(mode=0o700)
        self.agent, self.now, self.serial = agent, 0.0, 0
        self.worker = Worker(binary, ROOT / "examples/kb/space_job_queue.metta", "jobs:run", language="petta", maximum_bytes=maximum_bytes)
        initial = self.worker.call(E("Initialize", str(self.path), agent))
        assert initial[0] == S("KnowledgeCommittedV1"), initial
        self.server, self.client = None, None
        if service:
            self.server = Server(binary, root, module=ROOT / "examples/kb/space_job_queue.metta", entry="jobs:configuration", maximum_bytes=maximum_bytes)
            self.server.environment["CETTA_JOB_AGENT"] = agent
            self.server.start(); self.client = Client(self.server.path, maximum_reply_bytes=maximum_bytes, maximum_request_bytes=maximum_bytes)

    def close(self):
        if self.client: self.client.close()
        if self.server: self.server.stop()
        self.worker.close()

    def apply(self, command, identifier=None):
        if identifier is None:
            self.serial += 1; identifier = "queue-operation-" + uuid.uuid4().hex
        if self.client:
            view, _ = self.client.snapshot()
            try: answer = self.client.route("Apply", E("GatewayApplyV1", identifier, command), view)
            finally: self.client.drop(view)
        else:
            answer = self.worker.call(E("Apply", str(self.path), self.agent, identifier, command))
        if answer[0] == S("GatewayRefusedV1"): raise QueueConflict(answer[1].name)
        assert answer[0] in (S("GatewayCommittedV1"), S("GatewayReplayedV1")), answer
        self.last_envelope = answer
        return answer[1]

    def inspect(self, identifier):
        if not self.client:
            answer = self.worker.call(E("Inspect", str(self.path), identifier))
            assert answer[0] == S("GatewayInspectionV1"), answer
            found, events = answer[1:]
            assert events[0] == S("GatewayEventsV1"), events
            return found, events[1]
        view, _ = self.client.snapshot()
        try:
            found = self.client.route("Find", identifier, view)
            opened = self.client.route("EventsOpen", identifier, view)
            assert opened[0] == S("ServiceResourceV1"), opened
            token, collected = opened[1], []
            for _ in range(10000):
                answer = use(self.client, token, "RelPage", E("RelPageV1", 1, 8))
                if answer[0] == S("ServiceResourceV1"): token, page = answer[1:]
                else: page = answer
                assert page[0] == S("RelPageV1"), page
                collected.extend(page[1])
                if isinstance(page[2], tuple) and page[2][0] == S("Complete"): return found, collected
                assert page[2] in (S("More"), S("Paused")), page
            raise AssertionError("finite job history did not complete")
        finally: self.client.drop(view)

    def get(self, identifier):
        found, receipts = self.inspect(identifier)
        if found == S("SpaceProbeAbsentV1"): return None
        assert found[0] == S("SpaceProbeFoundV1"), found
        job = found[1]; fields = job[7]; events = []
        for wrapped in receipts:
            row = wrapped[1]; detail = row[5]; values = {}
            if detail[0] == S("GatewayDispatchedV1"): values = {"dispatched": False}
            elif detail[0] == S("GatewayAttemptV1"): values = {"dispatch_attempt": detail[1]}
            elif detail[0] == S("GatewayErrorDetailV1"): values = {"error": detail[1]}
            else: assert detail == E("GatewayDetailV1"), detail
            events.append(dict(sequence=row[2], state=row[3].name, at=scalar(row[4]), detail=values))
        return dict(agent=self.agent, id=identifier, state=job[2].name,
                    result=option(fields[5]), error=option(fields[6], encoded=False),
                    outputs=option(fields[7]), receipts=events)

    def submit(self, request, fingerprint, effect):
        self.apply(E("GatewaySubmitV1", request["id"], fingerprint, json.dumps(request), S(effect), request["deadline"], self.now))
        return self.get(request["id"])

    def claim(self):
        answer = self.apply(E("GatewayClaimV1", self.now))
        if answer == S("GatewayEmptyV1"): return None
        assert answer[0] == S("GatewayClaimedV1"), answer
        found, _ = self.inspect(answer[1]); job = found[1]; fields = job[7]
        return dict(id=job[1], fingerprint=fields[1], request=json.loads(fields[2]), effect=fields[3].name,
                    state="queued", cancel=0, result=None, error=None, sequence=job[3], outputs=None)

    def cancellation_requested(self, identifier):
        found, _ = self.inspect(identifier)
        return found != S("SpaceProbeAbsentV1") and found[1][7][4] in (True, S("true"), S("True"))

    def cancel(self, identifier):
        self.apply(E("GatewayCancelV1", identifier, self.now)); return self.get(identifier)

    def finish(self, identifier, state, *, result=None, error=None, outputs=None):
        self.apply(E("GatewayFinishV1", identifier, S(state), some(result), some(error, encoded=False), some(outputs), self.now))

    def recover(self): self.apply(E("GatewayRecoverV1", self.now))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("worker", type=Path)
    p.add_argument("--reference-store", type=Path, help="execution_gateway/store.py to inspect and load as the SQLite reference")
    p.add_argument("--steps", type=int, default=100)
    p.add_argument("--output", type=Path)
    args = p.parse_args(); assert args.steps > 0
    reference_module = None
    if args.reference_store:
        spec = importlib.util.spec_from_file_location("gateway_store_reference", args.reference_store)
        reference_module = importlib.util.module_from_spec(spec); spec.loader.exec_module(reference_module)
    checks, generated = 0, 0
    with tempfile.TemporaryDirectory(prefix="job-queue-contract-") as directory:
        for mode in ("embedded", "service"):
            root = Path(directory) / mode; root.mkdir(mode=0o700)
            native = NativeStore(args.worker.resolve(), root, "example-agent", service=mode == "service")
            reference = reference_module.Store(root / "reference.sqlite3", "example-agent") if reference_module else None
            known = set(); serial = 0
            def compare(method, *arguments, **keywords):
                nonlocal checks, serial
                serial += 1; native.now = float(serial)
                expected_error, expected = None, None
                if reference:
                    with patch.object(reference_module.time, "time", return_value=native.now):
                        try: expected = getattr(reference, method)(*arguments, **keywords)
                        except reference_module.Conflict as error: expected_error = str(error)
                try: actual, actual_error = getattr(native, method)(*arguments, **keywords), None
                except QueueConflict as error: actual, actual_error = None, str(error)
                if reference:
                    assert bool(actual_error) == bool(expected_error), (method, arguments, actual_error, expected_error)
                    if not expected_error:
                        assert json.dumps(actual, sort_keys=True) == json.dumps(expected, sort_keys=True), (mode, method, arguments, actual, expected)
                    for identifier in known:
                        got, wanted = native.get(identifier), reference.get(identifier)
                        assert json.dumps(got, sort_keys=True) == json.dumps(wanted, sort_keys=True), (mode, method, identifier, got, wanted)
                else: assert actual_error is None, (method, actual_error)
                checks += 1; return actual
            def submit(identifier, *, effect="read", deadline=10000.0, payload=None):
                request = dict(id=identifier, tool="synthetic", arguments=[], input=payload, deadline=deadline)
                fingerprint = hashlib.sha256(json.dumps({k: request[k] for k in ("id", "tool", "arguments", "input")}, sort_keys=True).encode()).hexdigest()
                known.add(identifier)
                return compare("submit", request, fingerprint, effect)
            try:
                submit("a"); submit("a"); submit("b", effect="write")
                assert compare("claim")["id"] == "a"
                compare("cancel", "a"); compare("cancel", "a")
                assert compare("cancellation_requested", "a") is True
                compare("finish", "a", "succeeded", result=0, outputs={"artifact": "synthetic"})
                assert compare("claim")["id"] == "b"
                compare("recover")
                assert native.get("b")["state"] == "uncertain"
                assert compare("claim") is None
                assert compare("cancel", "absent") is None
                submit("expired", deadline=0.0); submit("next")
                assert compare("claim") is None
                assert compare("claim")["id"] == "next"
                compare("finish", "next", "failed", error="")
                submit("cancelled"); compare("cancel", "cancelled")
                assert native.get("cancelled")["state"] == "cancelled"
                submit("read-recovery"); compare("claim"); compare("recover")
                assert native.get("read-recovery")["state"] == "failed"
                if reference:
                    rng = random.Random(311029)
                    for index in range(args.steps):
                        choice = rng.randrange(5)
                        if choice == 0:
                            submit(f"generated-{rng.randrange(12)}", effect="write" if rng.randrange(2) else "read")
                        elif choice == 1: compare("claim")
                        elif choice == 2: compare("cancel", rng.choice(sorted(known)))
                        elif choice == 3:
                            running = [key for key in sorted(known) if reference.get(key)["state"] == "running"]
                            if running: compare("finish", running[0], "succeeded", result={"step": index})
                        else: compare("recover")
                        generated += 1
            finally: native.close()
    report = dict(scripted_and_generated_comparisons=checks, generated_steps=generated, modes=["embedded", "service"],
                  actual_sqlite_reference=bool(reference_module),
                  reference_sha256=hashlib.sha256(args.reference_store.read_bytes()).hexdigest() if reference_module else None,
                  scope="Gateway-admitted terminal finishes and finite timestamps; local storage only, no tool dispatch or live migration")
    if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__": main()
