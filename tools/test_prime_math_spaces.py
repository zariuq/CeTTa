#!/usr/bin/env python3
"""Qualify native mathematical projects, portable replay and checkpoint races."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import selectors
import signal
import subprocess
import time
import uuid


ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "lib/prime/math_space_files.metta"
FIXTURES = ("native_workflow", "authority_boundaries", "snapshots", "agent_views",
            "programming_workflow", "schema_boundaries", "gslt_routes", "hol_workflow")
BUILD_PROJECT = '''
!(bind! &project (math:new "checkpoint"))
!(math:declare &project (quote (hol:inductive Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))))
!(math:declare &project (quote (hol:define add (-> Nat Nat Nat)
   (= (add $n Z) $n) (= (add $n (S $m)) (S (add $n $m))))))
!(math:goal &project "right-zero" type
   (quote (-> (n : Nat) (id Nat (add n Z) n))) "Right identity.")
!(math:attempt &project "proof" "right-zero" "reflexivity" (quote (lam n (refl n))) 100000)
!(math:publish &project "proof" right-zero 100000)
'''
JSON_HELPERS = '''
(= (qual:emit $value)
   (let $text (json:stringify $value) (system:write stdout (str:concat $text "\\n"))))
(= (qual:save-result $actor $result)
   (case $result
     (((Math:Saved $path $revision)
        (qual:emit (JsonObject ((JsonPair "actor" $actor) (JsonPair "state" "saved")
                               (JsonPair "revision" $revision)))))
      ((Math:StoreConflict $path $expected $actual)
        (qual:emit (JsonObject ((JsonPair "actor" $actor) (JsonPair "state" "conflict")))))
      ($other
        (let $detail (repr $other)
          (qual:emit (JsonObject ((JsonPair "actor" $actor) (JsonPair "state" "fault")
                                 (JsonPair "detail" $detail)))))))))
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Qualification:
    def __init__(self, binary: Path, output: Path, timeout: float):
        self.binary, self.output, self.timeout = binary, output, timeout
        self.output.mkdir(parents=True, exist_ok=True)
        self.observations: list[dict] = []

    def source(self, name: str, body: str, library: Path = LIBRARY) -> Path:
        path = self.output / f"{name}.metta"
        path.write_text(f'!(import! &self {json.dumps(str(library))})\n'
                        '!(import! &self system)\n' + JSON_HELPERS + body)
        return path

    def run(self, name: str, source: Path, *, no_results: bool = True):
        command = [str(self.binary), "--lang", "prime"]
        if no_results:
            command.append("--no-results")
        start = time.monotonic()
        result = subprocess.run(command + [str(source)], cwd=ROOT, text=True,
                                capture_output=True, timeout=self.timeout)
        (self.output / f"{name}.stdout").write_text(result.stdout)
        (self.output / f"{name}.stderr").write_text(result.stderr)
        row = {"name": name, "exit": result.returncode, "seconds": time.monotonic() - start,
               "source_sha256": digest(source), "stdout_sha256": hashlib.sha256(result.stdout.encode()).hexdigest()}
        self.observations.append(row)
        assert result.returncode == 0 and not result.stderr, (name, result.returncode, result.stdout, result.stderr)
        return result

    def seed(self, path: Path, name: str):
        source = self.source(name, BUILD_PROJECT +
            f'!(qual:save-result "seed" (math:save &project {json.dumps(str(path))} 100000))\n')
        result = json.loads(self.run(name, source).stdout)
        assert result["state"] == "saved", result
        return result

    def load(self, path: Path, name: str):
        body = f'''!(case (math:load {json.dumps(str(path))} 100000)
 (((Math:Loaded $project $revision)
    (let* (($status (math:goal-status $project "right-zero" 100000))
           ($notes (math:notes $project)) ($text (repr $status)) ($note-text (repr $notes)))
      (qual:emit (JsonObject ((JsonPair "state" "loaded") (JsonPair "revision" $revision)
        (JsonPair "goal" $text) (JsonPair "notes" $note-text))))))
  ($other (let $text (repr $other)
    (qual:emit (JsonObject ((JsonPair "state" "fault") (JsonPair "detail" $text))))))))\n'''
        result = json.loads(self.run(name, self.source(name, body)).stdout)
        assert result["state"] == "loaded", result
        assert result["goal"] == '(Math:GoalStatus "right-zero" checked (published right-zero))', result
        return result

    def actor_source(self, name: str, path: Path, *, wait: bool, library: Path = LIBRARY):
        actor, target = json.dumps(name), json.dumps(str(path))
        action = f'(qual:save-result {actor} (math:save $project {target} 100000))'
        if wait:
            action = f'''(chain
 (qual:emit (JsonObject ((JsonPair "stage" "ready") (JsonPair "base" $revision)))) $sent
 (chain (system:read-line) $go {action}))'''
        body = f'''!(case (math:load {target} 100000)
 (((Math:Loaded $project $revision)
    (chain (math:note $project {actor} {actor} ()) $noted {action}))))\n'''
        return self.source(name, body, library)

    def start(self, source: Path):
        return subprocess.Popen([str(self.binary), "--lang", "prime", "--no-results", str(source)],
                                cwd=ROOT, text=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)

    def line(self, process):
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            assert selector.select(self.timeout), "worker did not reach its barrier"
            line = process.stdout.readline()
            assert line, (process.poll(), process.stderr.read() if process.poll() is not None else "")
            return line

    @staticmethod
    def stop(process):
        if process.poll() is None:
            try:
                signal_group = process.pid
                import os
                os.killpg(signal_group, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=5)

    def race(self, name: str, *, alias: bool):
        target = self.output / f"{name}.json"
        seed = self.seed(target, name + "-seed")
        second = target
        if alias:
            second = self.output / f"{name}-alias.json"
            second.symlink_to(target)
        processes = [self.start(self.actor_source(name + "-left", target, wait=True)),
                     self.start(self.actor_source(name + "-right", second, wait=True))]
        try:
            barriers = [json.loads(self.line(p)) for p in processes]
            assert all(b == {"stage": "ready", "base": seed["revision"]} for b in barriers), barriers
            for p in processes:
                p.stdin.write("commit\n")
                p.stdin.flush()
                p.stdin.close()
                p.stdin = None
            results = []
            for p in processes:
                stdout, stderr = p.communicate(timeout=self.timeout)
                assert p.returncode == 0 and not stderr, (p.returncode, stdout, stderr)
                results.append(json.loads(stdout))
            assert sorted(r["state"] for r in results) == ["conflict", "saved"], results
            winner = next(r["actor"] for r in results if r["state"] == "saved")
            loser = next(r["actor"] for r in results if r["state"] == "conflict")
            loaded = self.load(target, name + "-reload")
            assert winner in loaded["notes"] and loser not in loaded["notes"], loaded
            self.observations.append({"name": name, "passed": True, "results": results,
                                      "base": seed["revision"], "revision": loaded["revision"]})
        finally:
            for p in processes:
                self.stop(p)

    def crash(self, name: str, *, after_rename: bool):
        target = self.output / f"{name}.json"
        self.seed(target, name + "-seed")
        before = target.read_bytes()
        code = LIBRARY.read_text().replace('"./math_space_store.metta"',
                                          json.dumps(str(ROOT / "lib/prime/math_space_store.metta")))
        if after_rename:
            old = "(fs:open-read $directory)"
            new = "(qual:pause-directory $directory)"
            hook = '''(= (qual:pause-directory $directory)
 (chain (system:write stdout "after-rename\\n") $sent
   (chain (system:read-line) $go (fs:open-read $directory))))\n'''
        else:
            old = "(fs:rename $temporary $target)"
            new = "(qual:pause-rename $temporary $target)"
            hook = '''(= (qual:pause-rename $temporary $target)
 (chain (system:write stdout "before-rename\\n") $sent
   (chain (system:read-line) $go (fs:rename $temporary $target))))\n'''
        assert code.count(old) == 1, old
        mutant = self.output / f"{name}-paused-library.metta"
        mutant.write_text(code.replace(old, new) + "\n" + hook)
        process = self.start(self.actor_source(name, target, wait=False, library=mutant))
        try:
            assert self.line(process).strip() == ("after-rename" if after_rename else "before-rename")
            self.stop(process)
            after = target.read_bytes()
            assert (after != before) == after_rename, name
            json.loads(after)
            loaded = self.load(target, name + "-recover")
            assert (name in loaded["notes"]) == after_rename, loaded
            self.observations.append({"name": name, "passed": True, "after_rename": after_rename,
                                      "checkpoint_sha256": hashlib.sha256(after).hexdigest()})
        finally:
            self.stop(process)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/math-space-qualification")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--pure-only", action="store_true", help="qualify native and codec fixtures without filesystem writers")
    args = parser.parse_args()
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = args.output.resolve() / run_id
    q = Qualification(args.binary.resolve(), output, args.timeout)
    binary_sha = digest(q.binary)
    sources = {str(p.relative_to(ROOT)): digest(p) for p in (ROOT / "lib/prime").glob("math_space*.metta")}
    report = {"schema": "prime.math-space.qualification.v1", "binary_sha256": binary_sha,
              "sources": sources, "pure_only": args.pure_only, "passed": False}
    try:
        for name in FIXTURES:
            source = ROOT / f"tests/prime/math_spaces/{name}.metta"
            text = source.read_text()
            for lib in ("math_space", "math_space_store", "math_space_agent"):
                text = text.replace(f"!(import! &self {lib})",
                                    f'!(import! &self {json.dumps(str(ROOT / "lib/prime" / (lib + ".metta")))})')
            local = output / f"{name}.metta"
            local.write_text(text)
            result = q.run(name, local, no_results=False)
            expected = (ROOT / f"tests/prime/math_spaces/{name}.expected").read_text()
            assert result.stdout == expected, (name, result.stdout)
            print("PASS", name, flush=True)
        if not args.pure_only:
            for name, alias in (("two-writers", False), ("aliased-writers", True)):
                q.race(name, alias=alias)
                print("PASS", name, flush=True)
            for name, after in (("crash-before-rename", False), ("crash-after-rename", True)):
                q.crash(name, after_rename=after)
                print("PASS", name, flush=True)
        current = binary_sha == digest(q.binary) and all(digest(ROOT / p) == sha for p, sha in sources.items())
        assert current, "binary or libraries changed during qualification"
        report["passed"] = True
        print("Math spaces: all selected controls passed")
    finally:
        report["observations"] = q.observations
        (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Evidence:", output)


if __name__ == "__main__":
    main()
