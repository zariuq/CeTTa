"""Record compiler-gate invocations without changing file or argument boundaries.

Records retain the original command, working directory, runtime environment,
input bytes and the library/presentation context. Replays reject stale context
instead of interpreting an old qualification against a different program.
"""
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

from check_transform_swi_differential import read_forms, write_form

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "cetta-gslt-invocation-v1"
ENV_PREFIXES = ("CETTA_", "GSLT_", "PETTA_", "METTA_")
ENV_NAMES = ("LD_LIBRARY_PATH", "SWI_HOME_DIR")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def runtime_environment():
    return {k: v for k, v in os.environ.items()
            if k.startswith(ENV_PREFIXES) or k in ENV_NAMES}


def context_identity(root=ROOT):
    """Pin the installed textual libraries and authored/generated presentations."""
    rows = []
    for directory in (root / "lib", root / "langdef"):
        if directory.exists():
            for path in sorted(directory.rglob("*")):
                if path.is_file():
                    rows.append((str(path.relative_to(root)), digest(path.read_bytes())))
    return dict(root=str(root), sha256=digest(json.dumps(rows).encode()), files=rows)


def save_json(path, value):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".pending")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    os.replace(temporary, path)


def literal_imports(path, root):
    """Possible authored file imports; dynamic import operands remain explicit."""
    todo = list(read_forms(path.read_text()))
    found, unresolved = [], []
    while todo:
        form = todo.pop()
        if not isinstance(form, list) or not form:
            continue
        if form[0] == "quote":
            continue
        if form[0] != "import!" or len(form) != 3:
            todo.extend(form)
            continue
        target = form[2]
        if isinstance(target, list):
            if target[:1] == ["native"]:
                continue
            if len(target) == 2 and target[0] == "library" and isinstance(target[1], str):
                target = target[1]
            else:
                unresolved.append(form)
                continue
        if not isinstance(target, str) or target.startswith("$"):
            unresolved.append(form)
            continue
        if target.startswith('"'):
            try:
                target = json.loads(target)
            except ValueError:
                unresolved.append(form)
                continue
        elif target.startswith("r#") and '"' in target:
            opening = target.index('"')
            target = target[opening + 1:-(opening)]
        candidates = [path.parent / target, root / target,
                      root / "lib/petta" / target, root / "lib" / target]
        paths = []
        for candidate in candidates:
            for name in (candidate, Path(str(candidate) + ".metta")):
                if name.is_file():
                    paths.append(name.resolve())
        if not paths:
            unresolved.append(form)
        for imported in paths:
            found.append(imported)
            variant = root / "lib/petta" / imported.name
            if imported.parent == root / "lib" and variant.is_file():
                found.append(variant.resolve())
    return found, unresolved


def make_record(directory, command, arguments, *, root=ROOT):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    blobs = directory / "inputs"
    blobs.mkdir(exist_ok=True)
    sources, dependencies, unresolved, seen = [], [], [], set()

    def capture(path):
        path = path.resolve()
        content = path.read_bytes()
        sha = digest(content)
        blob = blobs / sha
        if not blob.exists():
            temporary = blobs / (sha + "." + uuid.uuid4().hex)
            temporary.write_bytes(content)
            os.replace(temporary, blob)
        return dict(path=str(path), sha256=sha, blob=str(blob.relative_to(directory)))

    for index, argument in enumerate(arguments):
        path = Path(argument)
        if not path.is_file():
            continue
        sources.append(dict(argument=index, **capture(path)))
    todo = [Path(source["path"]) for source in sources]
    while todo:
        path = todo.pop()
        if path in seen:
            continue
        seen.add(path)
        try:
            imported, missing = literal_imports(path, root)
        except (UnicodeError, ValueError) as error:
            unresolved.append(dict(source=str(path), error=str(error)))
            continue
        unresolved.extend(dict(source=str(path), form=write_form(form)) for form in missing)
        for dependency in imported:
            if dependency not in seen:
                dependencies.append(capture(dependency))
                todo.append(dependency)
    environment = runtime_environment()
    artifacts = []
    for argument in [*command, *(environment[k] for k in ("CETTA_WRAPPED_BIN", "CETTA_BIN")
                                 if k in environment)]:
        # Launchers can name the runtime in an environment assignment, as
        # the sanitizer runner does with CETTA_WRAPPED_BIN.
        candidate = argument.partition("=")[2] if "=" in argument else argument
        found = candidate if Path(candidate).is_file() else shutil.which(candidate)
        if found and Path(found).is_file():
            path = Path(found).resolve()
            item = dict(path=str(path), sha256=digest(path.read_bytes()))
            if item not in artifacts:
                artifacts.append(item)
    record = dict(schema=SCHEMA, source_gate=os.environ.get("CETTA_GSLT_SOURCE_GATE"),
                  command=list(command), arguments=list(arguments),
                  cwd=os.getcwd(), environment=environment,
                  sources=sources, dependencies=dependencies,
                  unresolved_imports=unresolved, artifacts=artifacts,
                  import_context=context_identity(root), started_ns=time.time_ns(),
                  process=dict(state="pending"))
    label = Path(sources[-1]["path"]).stem if sources else "invocation"
    path = directory / f"{label}.{uuid.uuid4().hex}.invocation.json"
    save_json(path, record)
    return path, record


def read_record(path):
    record = json.loads(Path(path).read_text())
    if record.get("schema") != SCHEMA:
        raise ValueError("unknown invocation schema")
    return record


@contextmanager
def restored_inputs(path):
    """Restore only missing generated inputs, never replace a live source file."""
    path = Path(path).resolve()
    record = read_record(path)
    context = record["import_context"]
    if record["unresolved_imports"]:
        raise ValueError("invocation has imports without captured resolution")
    current = context_identity(Path(context["root"]))
    if current["sha256"] != context["sha256"]:
        before, after = dict(context.get("files", [])), dict(current["files"])
        changed = sorted(path for path in before.keys() | after.keys()
                         if before.get(path) != after.get(path)) if before else []
        detail = ": " + ", ".join(changed[:5]) if changed else ""
        raise ValueError("recorded library/presentation context is stale" + detail)
    for artifact in record["artifacts"]:
        live = Path(artifact["path"])
        if not live.is_file() or digest(live.read_bytes()) != artifact["sha256"]:
            raise ValueError(f"recorded executable artifact is stale: {live}")
    made, directories = [], []
    try:
        for source in record["sources"] + record["dependencies"]:
            live = Path(source["path"])
            if live.exists():
                if not live.is_file() or digest(live.read_bytes()) != source["sha256"]:
                    raise ValueError(f"recorded input is stale: {live}")
                continue
            generated = Path(context["root"]) / "runtime"
            if not live.resolve().is_relative_to(generated.resolve()):
                raise ValueError(f"missing source outside generated output: {live}")
            data = (path.parent / source["blob"]).read_bytes()
            if digest(data) != source["sha256"]:
                raise ValueError("recorded input bytes fail their digest")
            missing = []
            parent = live.parent
            while not parent.exists():
                missing.append(parent)
                parent = parent.parent
            for parent in reversed(missing):
                parent.mkdir()
                directories.append(parent)
            with live.open("xb") as stream:
                stream.write(data)
            made.append((live, source["sha256"]))
        yield record
    finally:
        for live, sha in reversed(made):
            # A guest may have changed a file. Keep such output for inspection.
            if live.is_file() and digest(live.read_bytes()) == sha:
                live.unlink()
        for directory in reversed(directories):
            if directory.is_dir() and not any(directory.iterdir()):
                directory.rmdir()


def replay_environment(record):
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(ENV_PREFIXES) and k not in ENV_NAMES}
    env.update(record["environment"])
    return env


def main(arguments):
    if arguments[:1] == ["finish-gate"]:
        if len(arguments) != 4:
            raise SystemExit("usage: gslt_invocation.py finish-gate CORPUS GATE STATUS")
        status = int(arguments[3])
        for path in Path(arguments[1]).glob("*.invocation.json"):
            record = read_record(path)
            if record.get("source_gate") == arguments[2]:
                record["source_gate_status"] = status
                save_json(path, record)
        return 0
    if arguments[:1] == ["check-gate"]:
        if len(arguments) != 3:
            raise SystemExit("usage: gslt_invocation.py check-gate CORPUS GATE")
        records = [read_record(p) for p in Path(arguments[1]).glob("*.invocation.json")]
        selected = [r for r in records if r.get("source_gate") == arguments[2]]
        if not selected:
            raise SystemExit("source gate produced no invocation records: " + arguments[2])
        if any(r.get("source_gate_status") != 0 or
               r["process"].get("state") != "exited" or
               r["process"].get("returncode") != 0 for r in selected):
            raise SystemExit("source gate has unfinished or failed invocations: " + arguments[2])
        print(f"(GSLTRecordedGateV1 {arguments[2]} {len(selected)})")
        return 0
    if arguments[:1] != ["record"]:
        raise SystemExit("expected record, finish-gate or check-gate")
    arguments = arguments[1:]
    if len(arguments) < 3:
        raise SystemExit("usage: record_petta_direct_run.sh CORPUS COMMAND_ARGC COMMAND... ARGUMENTS...")
    try:
        count = int(arguments[1])
    except ValueError:
        raise SystemExit("COMMAND_ARGC must count the command's argv elements")
    if count < 1 or count > len(arguments) - 2:
        raise SystemExit("invalid command argument count")
    directory, command, args = arguments[0], arguments[2:2 + count], arguments[2 + count:]
    if not command:
        raise SystemExit("recording requires an executable command")
    path, record = make_record(directory, command, args)
    try:
        run = subprocess.run(command + args)
    except OSError as error:
        record["process"] = dict(state="launch-failed", error=str(error))
        save_json(path, record)
        raise
    record["process"] = dict(state="exited", returncode=run.returncode)
    save_json(path, record)
    if run.returncode < 0:
        import signal
        signal.signal(-run.returncode, signal.SIG_DFL)
        os.kill(os.getpid(), -run.returncode)
    return run.returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
