#!/usr/bin/env python3
"""Qualify the proposal HTTP boundary with a local protocol peer, not ML inference."""
from __future__ import annotations

import argparse
import ast
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
from threading import Thread
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def payload_models(source):
    """Load the actual request classes without importing the model-serving stack."""
    import pydantic
    import typing
    names = {"_strip_or_raise", "ProveRequest", "PredictTacticRequest", "EvaluateRequest"}
    tree = ast.parse(source.read_text())
    definitions = [node for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names]
    assert {node.name for node in definitions} == names
    namespace = {**vars(typing), "BaseModel": pydantic.BaseModel, "Field": pydantic.Field,
                 "field_validator": pydantic.field_validator}
    exec(compile(ast.Module(body=definitions, type_ignores=[]), str(source), "exec"), namespace)
    return {"/prove": namespace["ProveRequest"], "/gnn/predict_tactic": namespace["PredictTacticRequest"],
            "/pln/evaluate": namespace["EvaluateRequest"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/math-provider-qualification")
    parser.add_argument("--schema-source", type=Path, help="also validate requests with the provider's actual Pydantic source")
    args = parser.parse_args()
    models = payload_models(args.schema_source) if args.schema_source else {}
    observations = []
    source_hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in (ROOT / "lib/prime").glob("math_space*.metta")}

    class Peer(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            suffix = next((path for path in ("/prove", "/gnn/predict_tactic", "/pln/evaluate") if self.path.endswith(path)), "")
            if models:
                models[suffix].model_validate(body)
            observations.append({"path": self.path, "request": body, "validated_against_source": bool(models)})
            if self.path.startswith("/slow"):
                time.sleep(0.3)
            status = 503 if self.path.startswith("/unready") else 200
            if suffix == "/gnn/predict_tactic":
                reply = {"goal": body["goal"], "candidates": []}
            elif suffix == "/pln/evaluate":
                reply = {"expression": body["expression"], "stv": {"strength": 0.9, "confidence": 0.1},
                         "score": 0.09, "status": "fixture", "is_fallback": True}
            else:
                reply = {"solved": True, "exhausted": False, "termination_reason": "solved",
                         "proof_trace": {"goal": body["goal"], "tactic": "exact", "arguments": ["True.intro"], "subgoals": []},
                         "summary": {"fixture": "protocol-only"}}
            if self.path.startswith("/large"):
                reply["padding"] = "x" * 4096
            raw = json.dumps(reply).encode()
            try:
                self.send_response(status)
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Peer)
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f"http://127.0.0.1:{server.server_port}"
    run = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = args.output.resolve() / run
    output.mkdir(parents=True)
    source = output / "providers.metta"
    source.write_text('''!(import! &self math_space_providers)
!(bind! &base ''' + json.dumps(url) + ''')
!(assertEqual (math:peniel-tactics "True" 21) (Math:Refused provider-request))
!(assertEqual (math:peniel-prove "True" () 0 10) (Math:Refused provider-request))
!(let* (($request (math:peniel-tactics "True" 3))
        ($reply (math:peniel-call curl &base tactics $request 2000 8192)))
   (assertEqual (case $reply (((Math:ForeignProposal "lean4" tactics $r $a) Proposed) ($other Wrong))) Proposed))
!(let* (($request (math:peniel-score "True" ("h : True")))
        ($reply (math:peniel-call curl &base score $request 2000 8192)))
   (assertEqual (case $reply (((Math:ForeignProposal "lean4" score $r $a) Proposed) ($other Wrong))) Proposed))
!(bind! &project (math:new "foreign-proposals"))
!(math:declare &project (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
!(math:goal &project "false" type (quote (id Nat Z (S Z))) "A native claim unrelated to provider success.")
!(let* (($request (math:peniel-prove "True" () 3 10))
        ($reply (math:peniel-call curl &base prove $request 2000 8192)))
   (chain (math:remember-provider &project "provider" "peniel-protocol-peer" $reply) $saved
     (assertEqual (case $reply (((Math:ForeignProposal "lean4" prove $r $a) Proposed) ($other Wrong))) Proposed)))
!(assertEqual (math:goal-status &project "false" 10000)
   (Math:GoalStatus "false" open no-current-publication))
!(let $text (math:serialize &project)
   (assertEqual (case (math:restore $text 10000)
     (((Math:Restored $restored) (math:goal-status $restored "false" 10000)) ($other Wrong)))
     (Math:GoalStatus "false" open no-current-publication)))
!(let* (($request (math:peniel-tactics "True" 3)) ($endpoint (str:concat &base "/slow")))
   (assertEqual (math:peniel-call curl $endpoint tactics $request 30 8192)
     (Math:ProviderUnfinished deadline)))
!(let* (($request (math:peniel-tactics "True" 3)) ($endpoint (str:concat &base "/large")))
   (assertEqual (math:peniel-call curl $endpoint tactics $request 2000 64)
     (Math:ProviderUnfinished incomplete-capture)))
!(let* (($request (math:peniel-tactics "True" 3)) ($endpoint (str:concat &base "/unready")))
   (assertEqual (case (math:peniel-call curl $endpoint tactics $request 2000 8192)
     (((Math:ProviderUnavailable process $observation) Unavailable) ($other Wrong))) Unavailable))
''')
    try:
        result = subprocess.run([str(args.binary.resolve()), "--lang", "prime", str(source)],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        (output / "stdout").write_text(result.stdout)
        (output / "stderr").write_text(result.stderr)
        assert result.returncode == 0 and not result.stderr, result
        assert result.stdout and "(Error" not in result.stdout and "[]" not in result.stdout, result.stdout
        assert len(result.stdout.splitlines()) == 15, result.stdout
        assert len(observations) == 6, observations
        assert all(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == sha
                   for name, sha in source_hashes.items())
        report = {"schema": "prime.math-provider.qualification.v1", "passed": True,
                  "scope": "local HTTP protocol peer; no model inference or independent Lean acceptance",
                  "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                  "sources": source_hashes,
                  "schema_source_sha256": hashlib.sha256(args.schema_source.read_bytes()).hexdigest() if models else None,
                  "requests": observations}
        (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Math providers: request schemas, HTTP, limits, failure and non-authority controls passed")
        print("Evidence:", output)
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


if __name__ == "__main__":
    main()
