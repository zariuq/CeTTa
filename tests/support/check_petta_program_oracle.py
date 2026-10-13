#!/usr/bin/env python3
"""Compare ordered PeTTa program observations with an executable Lean model.

The reference driver runs the declared Lean evaluator. Native observations
come from the real executable, using the same authored equations and request;
differences in argument, choice or effect scheduling remain failed comparisons.
Complete Lean configurations qualify model resumption; native CLI output does
not expose an equivalent complete configuration or establish C refinement.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


class AtomReader:
    """Read the printed ground integer, Boolean, string and expression profile."""

    def __init__(self, text):
        self.text = text
        self.position = 0

    def spaces(self):
        while self.position < len(self.text) and self.text[self.position].isspace():
            self.position += 1

    def atom(self):
        self.spaces()
        if self.position == len(self.text):
            raise ValueError("missing printed atom")
        first = self.text[self.position]
        if first == "(":
            self.position += 1
            children = []
            while True:
                self.spaces()
                if self.position == len(self.text):
                    raise ValueError("unterminated printed expression")
                if self.text[self.position] == ")":
                    self.position += 1
                    return ("expression", tuple(children))
                children.append(self.atom())
        if first == '"':
            value, length = json.JSONDecoder().raw_decode(self.text[self.position:])
            if not isinstance(value, str):
                raise ValueError("invalid printed string")
            self.position += length
            return ("string", value)
        if first == ")":
            raise ValueError("unexpected closing expression")
        start = self.position
        while self.position < len(self.text) and not self.text[self.position].isspace() and self.text[self.position] not in "()":
            self.position += 1
        token = self.text[start:self.position]
        if not token:
            raise ValueError("empty printed token")
        if token in ["true", "True", "false", "False"]:
            return ("boolean", token.lower() == "true")
        if re.fullmatch(r"[+-]?[0-9]+", token):
            return ("number", str(int(token)))
        if token.startswith("$"):
            return ("variable", token[1:])
        return ("symbol", token)

    def read(self):
        result = self.atom()
        self.spaces()
        if self.position != len(self.text):
            raise ValueError("multiple atoms in one native observation")
        return result


def structural_atom(atom):
    if not isinstance(atom, dict) or "kind" not in atom:
        raise ValueError("malformed model atom")
    kind = atom["kind"]
    if kind == "expression":
        if set(atom) != {"kind", "items"} or not isinstance(atom["items"], list):
            raise ValueError("malformed expression atom")
        return (kind, tuple(structural_atom(child) for child in atom["items"]))
    if set(atom) != {"kind", "value"}:
        raise ValueError("malformed scalar atom")
    value = atom["value"]
    if kind == "number":
        if isinstance(value, bool) or not re.fullmatch(r"[+-]?[0-9]+", str(value)):
            raise ValueError("model number is outside the integer observation profile")
        return (kind, str(int(value)))
    if kind == "boolean":
        if not isinstance(value, bool):
            raise ValueError("malformed Boolean value")
        return (kind, value)
    if kind not in ["symbol", "variable", "string"] or not isinstance(value, str):
        raise ValueError("model atom is outside the declared observation profile")
    return (kind, value)


def native_observation(stdout):
    return [AtomReader(line).read() for line in stdout.splitlines()]


def expected_observation(model):
    if model.get("status") != "complete" or model.get("split_equal") is not True:
        raise ValueError("model did not complete with exact split agreement")
    if not isinstance(model.get("answers"), list) or not isinstance(model.get("effects"), list):
        raise ValueError("missing ordered model observations")
    return [structural_atom(atom) for atom in model["effects"] + model["answers"]]


def _atom_sequence(values):
    if not isinstance(values, list):
        raise ValueError("invalid structural atom sequence")
    for value in values:
        structural_atom(value)


def _bindings(bindings):
    if not isinstance(bindings, list):
        raise ValueError("incomplete branch-local bindings")
    for binding in bindings:
        if not isinstance(binding, dict) or set(binding) != {"name", "value"} or not isinstance(binding["name"], str):
            raise ValueError("invalid branch-local binding")
        structural_atom(binding["value"])


def _target(target):
    if target == {"kind": "data"}:
        return
    if not isinstance(target, dict) or set(target) != {"kind", "head"} or target["kind"] != "function" or not isinstance(target["head"], str):
        raise ValueError("invalid argument target")


def _fault(reason):
    if reason in [{"kind": "invalidSpace"}, {"kind": "zeroDivisor"}]:
        return
    if isinstance(reason, dict) and reason.get("kind") == "invalidArguments" and set(reason) == {"kind", "head"} and isinstance(reason["head"], str):
        return
    if isinstance(reason, dict) and reason.get("kind") == "missingCell" and set(reason) == {"kind", "name"} and isinstance(reason["name"], str):
        return
    raise ValueError("incomplete fault reason")


def _arguments(record):
    _bindings(record["bindings"])
    _target(record["target"])
    _atom_sequence(record["remaining"])
    _atom_sequence(record["values"])
    if type(record["index"]) is not int or record["index"] < 0:
        raise ValueError("invalid argument index")


def _dfs_control(control):
    if not isinstance(control, dict):
        raise ValueError("missing branch control")
    kind = control.get("kind")
    if kind == "evaluate" and set(control) == {"kind", "bindings", "expression"}:
        _bindings(control["bindings"])
        structural_atom(control["expression"])
    elif kind == "arguments" and set(control) == {"kind", "bindings", "target", "remaining", "values", "index"}:
        _arguments(control)
    elif kind == "returned" and set(control) == {"kind", "value"}:
        structural_atom(control["value"])
    elif kind == "fault" and set(control) == {"kind", "reason"}:
        _fault(control["reason"])
    else:
        raise ValueError("incomplete scalar branch control")


def _dfs_frames(frames):
    if not isinstance(frames, list):
        raise ValueError("missing continuation frames")
    for frame in frames:
        if not isinstance(frame, dict):
            raise ValueError("invalid continuation frame")
        kind = frame.get("kind")
        if kind == "argument" and set(frame) == {"kind", "bindings", "target", "remaining", "values", "index"}:
            _arguments(frame)
        elif kind == "bind" and set(frame) == {"kind", "bindings", "pattern", "body"}:
            _bindings(frame["bindings"])
            structural_atom(frame["pattern"])
            structural_atom(frame["body"])
        elif kind == "select" and set(frame) == {"kind", "bindings", "cases"}:
            _bindings(frame["bindings"])
            if not isinstance(frame["cases"], list):
                raise ValueError("invalid case rows")
            for row in frame["cases"]:
                if not isinstance(row, dict) or set(row) != {"pattern", "body"}:
                    raise ValueError("incomplete case row")
                structural_atom(row["pattern"])
                structural_atom(row["body"])
        elif kind == "branch" and set(frame) == {"kind", "bindings", "yes", "no"}:
            _bindings(frame["bindings"])
            structural_atom(frame["yes"])
            structural_atom(frame["no"])
        else:
            raise ValueError("incomplete continuation frame")


def _dfs_configuration(configuration):
    if not isinstance(configuration, dict) or set(configuration) != {"world", "scopes"}:
        raise ValueError("incomplete depth-first configuration")
    world = configuration["world"]
    if not isinstance(world, dict) or set(world) != {"state", "input", "output"}:
        raise ValueError("incomplete shared world")
    _atom_sequence(world["input"])
    _atom_sequence(world["output"])
    state = world["state"]
    if not isinstance(state, dict) or set(state) != {"core", "next", "spaces", "unallocated_spaces", "cells", "absent_cells"}:
        raise ValueError("incomplete read-only store encoding")
    _atom_sequence(state["core"])
    if type(state["next"]) is not int or state["next"] < 0 or not isinstance(state["spaces"], list) or len(state["spaces"]) != state["next"]:
        raise ValueError("incomplete allocated-space prefix")
    for rows in state["spaces"]:
        _atom_sequence(rows)
    if state["unallocated_spaces"] != [] or state["cells"] != [] or state["absent_cells"] is not None:
        raise ValueError("store is outside the read-only reconstruction profile")
    scopes = configuration["scopes"]
    if not isinstance(scopes, list) or not scopes:
        raise ValueError("root search scope omitted")
    for index, scope in enumerate(scopes):
        if not isinstance(scope, dict) or set(scope) != {"frontier", "collected", "caller"}:
            raise ValueError("incomplete search scope")
        _atom_sequence(scope["collected"])
        if index == len(scopes) - 1:
            if scope["caller"] is not None:
                raise ValueError("root scope acquired a delimiter caller")
        else:
            _dfs_frames(scope["caller"])
        if not isinstance(scope["frontier"], list):
            raise ValueError("ordered search frontier omitted")
        for branch in scope["frontier"]:
            if not isinstance(branch, dict) or set(branch) != {"control", "frames"}:
                raise ValueError("incomplete retained branch")
            _dfs_control(branch["control"])
            _dfs_frames(branch["frames"])
    return world, scopes


def completed_checkpoint_observations(model, request, configuration_policy="batch"):
    """Validate the retained configurations for this read-only source profile."""
    expected = expected_observation(model)
    configuration = model.get("configuration")
    checkpoints = model.get("checkpoints")
    fuels = request.get("fuel_chunks")
    if not isinstance(configuration, dict) or not isinstance(checkpoints, list) or not isinstance(fuels, list):
        raise ValueError("missing complete replay checkpoints")
    if len(checkpoints) != len(fuels) or not checkpoints:
        raise ValueError("checkpoint population changed")
    if model.get("fuel_chunks") != fuels or model.get("fuel_total") != sum(fuels):
        raise ValueError("replay fuel population changed")
    if checkpoints[-1].get("configuration") != configuration:
        raise ValueError("final checkpoint lost the reached configuration")
    if configuration_policy == "eager-dfs":
        if model.get("policy") != "cetta-eager-dfs":
            raise ValueError("reference evaluation policy changed")
        world, scopes = _dfs_configuration(configuration)
        previous_effects, previous_answers = [], []
        for index, checkpoint in enumerate(checkpoints):
            reached_world, reached_scopes = _dfs_configuration(checkpoint.get("configuration"))
            if checkpoint.get("fuel") != fuels[index] or checkpoint.get("status") not in ["complete", "exhausted"]:
                raise ValueError("invalid successful replay checkpoint")
            if reached_world["state"] != world["state"]:
                raise ValueError("read-only replay changed the loaded store")
            effects = reached_world["output"]
            answers = reached_scopes[-1]["collected"]
            if effects[:len(previous_effects)] != previous_effects or answers[:len(previous_answers)] != previous_answers:
                raise ValueError("performed effects or published occurrences lost their prefix")
            if checkpoint["status"] == "complete" and (len(reached_scopes) != 1 or reached_scopes[0]["frontier"]):
                raise ValueError("completion omitted unfinished search scopes")
            previous_effects, previous_answers = effects, answers
        if len(scopes) != 1 or scopes[0] != {"frontier": [], "collected": model["answers"], "caller": None}:
            raise ValueError("completion omitted unfinished search work")
        if world["output"] != model["effects"] or world["input"] != model.get("remaining_input"):
            raise ValueError("configuration observations changed")
        return expected
    if configuration_policy != "batch":
        raise ValueError("unknown reference configuration policy")
    state = configuration.get("state")
    if not isinstance(state, dict) or set(state) != {"core", "next", "spaces", "unallocated_spaces", "cells", "absent_cells"}:
        raise ValueError("incomplete read-only store encoding")
    if state["unallocated_spaces"] != [] or state["cells"] != [] or state["absent_cells"] is not None:
        raise ValueError("store is outside the read-only reconstruction profile")
    for index, checkpoint in enumerate(checkpoints):
        reached = checkpoint.get("configuration")
        if not isinstance(reached, dict) or set(reached) != {"state", "control", "frames", "input", "output"}:
            raise ValueError("incomplete reached configuration")
        if checkpoint.get("fuel") != fuels[index] or checkpoint.get("status") not in ["complete", "exhausted"]:
            raise ValueError("invalid successful replay checkpoint")
        if reached["state"] != state:
            raise ValueError("read-only replay changed the loaded store")
        if not isinstance(reached["frames"], list) or not isinstance(reached["output"], list) or not isinstance(reached["input"], list):
            raise ValueError("invalid configuration sequences")
    control = configuration["control"]
    if control != {"kind": "returned", "answers": model["answers"]} or configuration["frames"]:
        raise ValueError("completion omitted unfinished work")
    if configuration["output"] != model["effects"] or configuration["input"] != model.get("remaining_input"):
        raise ValueError("configuration observations changed")
    return expected


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--requests", type=Path, required=True,
                        help="JSON-lines requests for the reference program driver")
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--native-cwd", type=Path, default=Path.cwd())
    parser.add_argument("--lean-project", type=Path, required=True)
    parser.add_argument("--lean-command", required=True,
                        help="reference driver command; request file is appended as one argument")
    parser.add_argument("--reference-input", type=Path, action="append", default=[],
                        help="reference source, compiled module or build input to hash before and after execution")
    parser.add_argument("--configuration-policy", choices=["batch", "eager-dfs"], default="batch",
                        help="declared reference configuration schema and evaluation policy")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--profiles", nargs="+", choices=["default", "extended"], default=["default", "extended"])
    parser.add_argument("--native-timeout", type=float, default=30.0)
    parser.add_argument("--reference-timeout", type=float, default=600.0)
    args = parser.parse_args()
    if args.native_timeout <= 0 or args.reference_timeout <= 0 or not args.profiles:
        raise ValueError("invalid oracle runner settings")
    requests_path = args.requests.resolve()
    native = args.native.resolve()
    requests = [json.loads(line) for line in requests_path.read_text().splitlines() if line.strip()]
    if not requests:
        raise ValueError("empty oracle population")
    args.out.mkdir(parents=True, exist_ok=False)
    guards = {str(requests_path): sha256(requests_path), str(native): sha256(native),
              str(Path(__file__).resolve()): sha256(Path(__file__).resolve())}
    for reference_input in args.reference_input:
        path = reference_input.resolve()
        guards[str(path)] = sha256(path)
    sources = []
    for index, request in enumerate(requests):
        if not isinstance(request, dict) or not isinstance(request.get("program_path"), str) or not isinstance(request.get("expression"), str):
            raise ValueError("malformed program request")
        if request.get("input", []):
            raise ValueError("native CLI comparison does not consume model input")
        expression = request["expression"]
        if not expression.lstrip().startswith("("):
            raise ValueError("native authored requests require a parenthesized expression")
        program = Path(request["program_path"]).resolve()
        guards[str(program)] = sha256(program)
        source = args.out / (f"{index:04d}.metta")
        source.write_text(program.read_text() + "\n!" + expression + "\n")
        sources.append(source)
    command = shlex.split(args.lean_command) + [str(requests_path)]
    provenance = {"source_identities": guards, "reference_command": command,
                  "configuration_policy": args.configuration_policy,
                  "profiles": args.profiles, "program_requests": len(requests)}
    (args.out / "inputs.json").write_text(json.dumps(provenance, indent=2) + "\n")
    reference = subprocess.run(command, cwd=args.lean_project, capture_output=True,
                               timeout=args.reference_timeout)
    (args.out / "reference.stdout").write_bytes(reference.stdout)
    (args.out / "reference.stderr").write_bytes(reference.stderr)
    if reference.returncode:
        raise AssertionError("reference program driver refused the population; inspect retained stderr")
    models = [json.loads(line) for line in reference.stdout.decode().splitlines() if line.strip()]
    if len(models) != len(requests):
        raise AssertionError("reference population size changed")
    expectations = [completed_checkpoint_observations(model, request, args.configuration_policy)
                    for model, request in zip(models, requests)]
    records = []
    failures = []
    for profile in args.profiles:
        for index, source in enumerate(sources):
            command = [str(native), "--lang", "petta"]
            if profile != "default":
                command += ["--profile", profile]
            command.append(str(source.resolve()))
            stem = args.out / (f"{index:04d}-" + profile)
            timed_out = False
            try:
                result = subprocess.run(command, cwd=args.native_cwd, env=dict(os.environ),
                                        capture_output=True, timeout=args.native_timeout)
                stdout, stderr, returncode = result.stdout, result.stderr, result.returncode
            except subprocess.TimeoutExpired as error:
                stdout, stderr, returncode = error.stdout or b"", error.stderr or b"", None
                timed_out = True
            stem.with_suffix(".stdout").write_bytes(stdout)
            stem.with_suffix(".stderr").write_bytes(stderr)
            parse_error = None
            try:
                observation = native_observation(stdout.decode())
            except (ValueError, UnicodeError) as error:
                observation = None
                parse_error = str(error)
            agrees = returncode == 0 and not stderr and not parse_error and observation == expectations[index]
            record = {"case": index, "profile": profile, "command": command,
                      "exit": returncode, "timed_out": timed_out, "parse_error": parse_error, "agrees": agrees,
                      "stderr_empty": not stderr,
                      "source_sha256": sha256(source),
                      "native_observation": observation, "model_observation": expectations[index]}
            records.append(record)
            if not agrees:
                failures.append({"case": index, "profile": profile})
            (args.out / "records.json").write_text(json.dumps(records, indent=2) + "\n")
        print("Compared program requests:", profile, len(requests), flush=True)
    changed = [name for name, expected in guards.items() if sha256(Path(name)) != expected]
    receipt = {"program_requests": len(requests), "native_observations": len(records),
               "failures": failures, "changed_inputs": changed, "source_identities": guards,
               "configuration_policy": args.configuration_policy,
               "reference_command": shlex.split(args.lean_command) + [str(requests_path)],
               "scope": "Ordered program answers and println effects in the admitted integer/data profile; model split configurations are not a native C refinement."}
    (args.out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if failures or changed:
        raise AssertionError(f"program oracle rejected {len(failures)} comparisons; changed inputs: {len(changed)}")
    print("PASS: ordered program observations agree:", len(records), flush=True)


if __name__ == "__main__":
    main()
