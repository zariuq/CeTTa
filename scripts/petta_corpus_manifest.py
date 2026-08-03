#!/usr/bin/env python3
"""Freeze and verify the complete PeTTa example corpus and its SWI oracle."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import re
import selectors
import signal
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any


SCHEMA = "cetta-petta-corpus-v1"
EXPECTED_TOTAL = 176
EXPECTED_CONTROLLED = 5
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
MORK_READY_BANNER = "MORK init: done\n"
PYTHON_RUNTIME_WARNING = (
    "Could not find platform dependent libraries <exec_prefix>\n"
)
VARIABLE_TERMINATORS = frozenset(" \t\r\n()[]{}\",")
TEST_DIAGNOSTIC_SUFFIXES = (". ✅ ", ". ❌ ")
MAX_CAPTURE_BYTES = 16 * 1024 * 1024

CONTROLLED_CASES: dict[str, dict[str, Any]] = {
    "git_import2.metta": {
        "class": "external",
        "name": "local-git-and-build-fixture",
        "mode": "complete",
        "kind": "local-git",
        "stdin": "",
        "files": [
            "tests/petta/fixtures/faiss/build.sh",
            "tests/petta/fixtures/faiss/faiss.pl",
            "tests/petta/fixtures/faiss/embed.pl",
            "tests/petta/fixtures/faiss/lib_faiss.metta",
        ],
        "source_replacements": {
            "https://github.com/patham9/faiss_ffi": "./faiss_ffi",
            "build.sh": "../../fixture/build.sh",
        },
        "source_prefix": "!(import! &self (library lib_import))\n\n",
    },
    "greedy_chess.metta": {
        "class": "interactive",
        "name": "scripted-quit-session",
        "mode": "complete",
        "stdin": "q\n",
    },
    "llm_cities.metta": {
        "class": "external",
        "name": "deterministic-llm-fixture",
        "mode": "complete",
        "stdin": "",
        "python_files": ["openai.py"],
    },
    "repl.metta": {
        "class": "interactive",
        "name": "scripted-prefix-session",
        "mode": "prefix",
        "stdin": "(+ 1 2)\n",
        "expected_prefix": "3\n",
    },
    "torch.metta": {
        "class": "external",
        "name": "deterministic-python-torch-fixture",
        "mode": "complete",
        "stdin": "",
        "python_files": ["torch.py"],
    },
}

CORRECTED_CASES: dict[str, dict[str, Any]] = {
    "space_let_probe.metta": {
        "class": "hermetic-corrected",
        "name": "space-let-root-allocation-correction",
        "mode": "complete",
        "kind": "source-file",
        "source_file": "tests/petta/fixtures/space_let_corrected.metta",
    },
}

FIXTURE_CASES = CONTROLLED_CASES | CORRECTED_CASES

NEGATIVE_CASES = {
    "assert_probe.metta": "intentional-failed-assertion",
}

# Hermetic examples may still depend on authored files outside examples/.
# These files are part of the frozen oracle contract rather than ambient
# checkout state.  In particular, git_import.metta exercises mounting an
# already-present local package; git_import2.metta separately exercises a
# controlled clone/build fixture.
HERMETIC_REQUIRED_FILES: dict[str, tuple[str, ...]] = {
    "git_import.metta": ("repos/test_metta_lib/test.metta",),
    "metamo_tea_break.metta": ("lib/lib_metamo.metta",),
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def normalize_stream(
    text: str, petta_dir: Path, extra_roots: tuple[Path, ...] = ()
) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = ANSI_RE.sub("", text)
    for root in (petta_dir, *extra_roots):
        text = text.replace(str(root), "<PETTA_ROOT>")
    return text


def alpha_canonicalize_atom_text(text: str) -> str:
    mapping: dict[str, str] = {}
    output: list[str] = []
    index = 0
    next_variable = 0
    in_string = False
    escaped = False
    while index < len(text):
        character = text[index]
        if in_string:
            output.append(character)
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            index += 1
            continue
        if character == '"':
            in_string = True
            output.append(character)
            index += 1
            continue
        if character != "$":
            output.append(character)
            index += 1
            continue

        end = index + 1
        while (
            end < len(text)
            and text[end] not in VARIABLE_TERMINATORS
        ):
            end += 1
        token = text[index:end]
        if token == "$":
            key = f"$<anonymous:{next_variable}>"
        else:
            key = token
        replacement = mapping.get(key)
        if replacement is None:
            replacement = f"$V{next_variable}"
            mapping[key] = replacement
            next_variable += 1
        output.append(replacement)
        index = end
    return "".join(output)


def test_diagnostic_parts(line: str) -> tuple[str, str, str] | None:
    if not line.startswith("is "):
        return None
    suffix = next(
        (candidate for candidate in TEST_DIAGNOSTIC_SUFFIXES
         if line.endswith(candidate)),
        None,
    )
    if suffix is None:
        return None

    body = line[3:-len(suffix)]
    depth = 0
    in_string = False
    escaped = False
    delimiter = ", should "
    index = 0
    while index < len(body):
        character = body[index]
        if in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
            index += 1
            continue
        if character == '"':
            in_string = True
        elif character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
        elif depth == 0 and body.startswith(delimiter, index):
            return body[:index], body[index + len(delimiter):], suffix
        index += 1
    return None


def alpha_canonicalize_output(text: str) -> str:
    normalized_lines: list[str] = []
    for line_with_ending in text.splitlines(keepends=True):
        if line_with_ending.endswith("\n"):
            line = line_with_ending[:-1]
            ending = "\n"
        else:
            line = line_with_ending
            ending = ""
        diagnostic = test_diagnostic_parts(line)
        if diagnostic is None:
            normalized = alpha_canonicalize_atom_text(line)
        else:
            actual, expected, suffix = diagnostic
            normalized = (
                "is "
                + alpha_canonicalize_atom_text(actual)
                + ", should "
                + alpha_canonicalize_atom_text(expected)
                + suffix
            )
        normalized_lines.append(normalized + ending)
    return "".join(normalized_lines)


def normalize_oracle_stdout(
    text: str, petta_dir: Path, extra_roots: tuple[Path, ...] = ()
) -> str:
    normalized = normalize_stream(text, petta_dir, extra_roots)
    if normalized.startswith(MORK_READY_BANNER):
        normalized = normalized[len(MORK_READY_BANNER):]
    return alpha_canonicalize_output(normalized)


def normalize_cetta_stdout(
    text: str, petta_dir: Path, extra_roots: tuple[Path, ...] = ()
) -> str:
    return alpha_canonicalize_output(
        normalize_stream(text, petta_dir, extra_roots)
    )


def normalize_oracle_stderr(
    text: str, petta_dir: Path, extra_roots: tuple[Path, ...] = ()
) -> str:
    normalized = normalize_stream(text, petta_dir, extra_roots)
    if normalized.startswith(PYTHON_RUNTIME_WARNING):
        return normalized[len(PYTHON_RUNTIME_WARNING):]
    return normalized


def normalization_contract() -> dict[str, Any]:
    return {
        "line_endings": "LF",
        "strip_ansi_csi": True,
        "replace_petta_root": "<PETTA_ROOT>",
        "alpha_canonicalize_printed_variables": True,
        "drop_exact_leading_oracle_stdout": [MORK_READY_BANNER],
        "drop_exact_leading_python_stderr": [
            PYTHON_RUNTIME_WARNING
        ],
        "max_captured_output_bytes": MAX_CAPTURE_BYTES,
    }


def fixture_environment(
    repo_root: Path, fixture: dict[str, Any] | None
) -> dict[str, str]:
    environment = os.environ.copy()
    if fixture and fixture.get("python_files"):
        python_dir = repo_root / "tests" / "petta" / "fixtures" / "python"
        prior = environment.get("PYTHONPATH")
        environment["PYTHONPATH"] = (
            f"{python_dir}{os.pathsep}{prior}" if prior else str(python_dir)
        )
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
    return environment


@contextmanager
def local_git_fixture_workspace(
    repo_root: Path,
    petta_dir: Path,
    source: Path,
    fixture: dict[str, Any],
):
    with tempfile.TemporaryDirectory(
        prefix="cetta-petta-git-fixture-"
    ) as temporary:
        workspace = Path(temporary)
        os.symlink(
            petta_dir, workspace / "faiss_ffi", target_is_directory=True
        )
        shutil.copytree(
            repo_root / "tests" / "petta" / "fixtures" / "faiss",
            workspace / "fixture",
        )
        program_dir = workspace / "program"
        program_dir.mkdir()
        transformed = source.read_text(encoding="utf-8")
        transformed = fixture.get("source_prefix", "") + transformed
        for original, replacement in fixture["source_replacements"].items():
            occurrences = transformed.count(original)
            if occurrences != 1:
                raise RuntimeError(
                    f"{source.name}: expected one occurrence of "
                    f"{original!r}, found {occurrences}"
                )
            transformed = transformed.replace(original, replacement)
        transformed_source = program_dir / source.name
        transformed_source.write_text(transformed, encoding="utf-8")
        yield workspace, transformed_source


def terminate_process_group(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=2.0)
    except (ProcessLookupError, subprocess.TimeoutExpired):
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=2.0)


def run_bounded_process(
    command: list[str],
    cwd: Path,
    environment: dict[str, str],
    stdin_text: str | None,
    timeout_seconds: float,
    max_capture_bytes: int = MAX_CAPTURE_BYTES,
) -> tuple[int, str, str, bool, bool]:
    if max_capture_bytes <= 0:
        raise ValueError("max_capture_bytes must be positive")
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=environment,
        stdin=subprocess.PIPE if stdin_text is not None else subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    if process.stdin is not None:
        try:
            process.stdin.write(stdin_text.encode("utf-8"))
        except BrokenPipeError:
            pass
        finally:
            process.stdin.close()
    assert process.stdout is not None
    assert process.stderr is not None

    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    stdout = bytearray()
    stderr = bytearray()
    deadline = time.monotonic() + timeout_seconds
    timed_out = False
    output_limited = False
    try:
        while selector.get_map():
            remaining_time = deadline - time.monotonic()
            if remaining_time <= 0.0:
                timed_out = True
                break
            events = selector.select(timeout=min(0.1, remaining_time))
            if not events and process.poll() is not None:
                # A final nonblocking pass drains bytes written before exit.
                events = selector.select(timeout=0.0)
                if not events:
                    break
            for key, _ in events:
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                captured = len(stdout) + len(stderr)
                room = max_capture_bytes - captured
                target = stdout if key.data == "stdout" else stderr
                if room > 0:
                    target.extend(chunk[:room])
                if len(chunk) > room:
                    output_limited = True
                    break
            if output_limited:
                break
    finally:
        selector.close()
        if timed_out or output_limited or process.poll() is None:
            terminate_process_group(process)
        process.stdout.close()
        process.stderr.close()

    return (
        process.returncode if process.returncode is not None else 1,
        stdout.decode("utf-8", errors="replace"),
        stderr.decode("utf-8", errors="replace"),
        timed_out,
        output_limited,
    )


def run_supervised_prefix(
    command: list[str],
    cwd: Path,
    environment: dict[str, str],
    stdin_text: str,
    expected_prefix: str,
    timeout_seconds: float,
    normalize_stdout: Any,
    normalize_stderr: Any,
) -> tuple[str, str, str]:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=environment,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    assert process.stdin is not None
    assert process.stdout is not None
    assert process.stderr is not None
    process.stdin.write(stdin_text.encode("utf-8"))
    process.stdin.close()

    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    stdout = bytearray()
    stderr = bytearray()
    deadline = time.monotonic() + timeout_seconds
    prefix_seen_while_live = False
    try:
        while time.monotonic() < deadline:
            for key, _ in selector.select(
                timeout=min(0.1, max(0.0, deadline - time.monotonic()))
            ):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                target = stdout if key.data == "stdout" else stderr
                captured = len(stdout) + len(stderr)
                room = MAX_CAPTURE_BYTES - captured
                if room > 0:
                    target.extend(chunk[:room])
                if len(chunk) > room:
                    raise RuntimeError(
                        "supervised process exceeded the output "
                        f"capture limit ({MAX_CAPTURE_BYTES} bytes)"
                    )
            normalized_stdout = normalize_stdout(
                stdout.decode("utf-8", errors="replace")
            )
            if normalized_stdout.startswith(expected_prefix):
                prefix_seen_while_live = process.poll() is None
                break
            if process.poll() is not None and not selector.get_map():
                break
    finally:
        selector.close()
        terminate_process_group(process)
        process.stdout.close()
        process.stderr.close()

    normalized_stdout = normalize_stdout(
        stdout.decode("utf-8", errors="replace")
    )
    normalized_stderr = normalize_stderr(
        stderr.decode("utf-8", errors="replace")
    )
    if not normalized_stdout.startswith(expected_prefix):
        raise RuntimeError(
            "supervised process did not produce its pinned prefix; "
            f"stdout={normalized_stdout[:500]!r}, "
            f"stderr={normalized_stderr[:500]!r}"
        )
    if not prefix_seen_while_live:
        raise RuntimeError(
            "supervised process exited instead of remaining live after prefix"
        )
    return "supervised-prefix", expected_prefix, normalized_stderr


def run_complete_process(
    command: list[str],
    cwd: Path,
    environment: dict[str, str],
    stdin_text: str | None,
    timeout_seconds: float,
    label: str,
) -> tuple[int, str, str]:
    exit_code, stdout, stderr, timed_out, output_limited = (
        run_bounded_process(
            command, cwd, environment, stdin_text,
            timeout_seconds,
        )
    )
    if timed_out:
        raise RuntimeError(
            f"{label} timed out after {timeout_seconds:g}s\n"
            f"stdout:\n{stdout[:1000]}\n"
            f"stderr:\n{stderr[:1000]}"
        )
    if output_limited:
        raise RuntimeError(
            f"{label} exceeded the output capture limit "
            f"({MAX_CAPTURE_BYTES} bytes)\n"
            f"stdout:\n{stdout[:1000]}\n"
            f"stderr:\n{stderr[:1000]}"
        )
    return exit_code, stdout, stderr


def run_oracle(
    repo_root: Path,
    petta_dir: Path,
    source: Path,
    timeout_seconds: float,
    fixture: dict[str, Any] | None = None,
) -> tuple[int | str, str, str]:
    command = ["sh", "run.sh", f"examples/{source.name}", "--silent"]
    environment = fixture_environment(repo_root, fixture)
    if fixture and fixture.get("kind") == "source-file":
        corrected_source = repo_root / fixture["source_file"]
        exit_code, stdout, stderr = run_complete_process(
            [
                "sh",
                str(petta_dir / "run.sh"),
                str(corrected_source),
                "--silent",
            ],
            petta_dir,
            environment,
            fixture.get("stdin", ""),
            timeout_seconds,
            f"SWI corrected oracle for {source.name}",
        )
        return (
            exit_code,
            normalize_oracle_stdout(stdout, petta_dir, (repo_root,)),
            normalize_oracle_stderr(stderr, petta_dir, (repo_root,)),
        )
    if fixture and fixture.get("kind") == "local-git":
        with local_git_fixture_workspace(
            repo_root, petta_dir, source, fixture
        ) as (workspace, transformed_source):
            exit_code, stdout, stderr = run_complete_process(
                [
                    "sh",
                    str(petta_dir / "run.sh"),
                    str(transformed_source),
                    "--silent",
                ],
                workspace,
                environment,
                fixture.get("stdin", ""),
                timeout_seconds,
                f"SWI oracle for {source.name}",
            )
            return (
                exit_code,
                normalize_oracle_stdout(stdout, petta_dir),
                normalize_oracle_stderr(stderr, petta_dir),
            )
    if fixture and fixture.get("mode") == "prefix":
        return run_supervised_prefix(
            command,
            petta_dir,
            environment,
            fixture["stdin"],
            fixture["expected_prefix"],
            timeout_seconds,
            lambda text: normalize_oracle_stdout(text, petta_dir),
            lambda text: normalize_oracle_stderr(text, petta_dir),
        )
    exit_code, stdout, stderr = run_complete_process(
        command,
        petta_dir,
        environment,
        fixture.get("stdin", "") if fixture else None,
        timeout_seconds,
        f"SWI oracle for {source.name}",
    )
    return (
        exit_code,
        normalize_oracle_stdout(stdout, petta_dir),
        normalize_oracle_stderr(stderr, petta_dir),
    )


def run_cetta(
    cetta: Path,
    petta_dir: Path,
    source: Path,
    timeout_seconds: float,
    fixture: dict[str, Any] | None = None,
) -> tuple[int | str, str, str]:
    repo_root = Path(__file__).resolve().parents[1]
    environment = fixture_environment(repo_root, fixture)
    command = [str(cetta), "--lang", "petta", str(source)]
    if fixture and fixture.get("kind") == "source-file":
        corrected_source = repo_root / fixture["source_file"]
        command = [
            str(cetta),
            "--lang",
            "petta",
            str(corrected_source),
        ]
    if fixture and fixture.get("kind") == "local-git":
        try:
            with local_git_fixture_workspace(
                repo_root, petta_dir, source, fixture
            ) as (workspace, transformed_source):
                exit_code, stdout, stderr = run_complete_process(
                    [
                        str(cetta),
                        "--lang",
                        "petta",
                        str(transformed_source),
                    ],
                    workspace,
                    environment,
                    fixture.get("stdin", ""),
                    timeout_seconds,
                    f"CeTTa controlled run for {source.name}",
                )
                return (
                    exit_code,
                    normalize_cetta_stdout(
                        stdout, petta_dir, (cetta.parent, workspace)
                    ),
                    normalize_oracle_stderr(
                        stderr, petta_dir, (cetta.parent, workspace)
                    ),
                )
        except RuntimeError as error:
            return "controlled-failure", "", f"{error}\n"
    if fixture and fixture.get("mode") == "prefix":
        try:
            return run_supervised_prefix(
                command,
                cetta.parent,
                environment,
                fixture["stdin"],
                fixture["expected_prefix"],
                timeout_seconds,
                lambda text: normalize_cetta_stdout(
                    text, petta_dir, (cetta.parent,)
                ),
                lambda text: normalize_oracle_stderr(
                    text, petta_dir, (cetta.parent,)
                ),
            )
        except RuntimeError as error:
            return "controlled-failure", "", f"{error}\n"
    exit_code, stdout, stderr, timed_out, output_limited = (
        run_bounded_process(
            command,
            petta_dir,
            environment,
            fixture.get("stdin", "") if fixture else None,
            timeout_seconds,
        )
    )
    if timed_out:
        exit_code = 124
    elif output_limited:
        exit_code = "output-limit"
    return (
        exit_code,
        normalize_cetta_stdout(stdout, petta_dir, (cetta.parent,)),
        normalize_oracle_stderr(
            stderr, petta_dir, (cetta.parent,)
        ),
    )


def tracked_examples(petta_dir: Path) -> set[str]:
    completed = subprocess.run(
        ["git", "ls-files", "--", "examples/*.metta"],
        cwd=petta_dir,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return {
        Path(line).name
        for line in completed.stdout.splitlines()
        if line.strip()
    }


def git_revision(petta_dir: Path) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=petta_dir,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return completed.stdout.strip()


def oracle_record(
    exit_code: int | str,
    stdout: str,
    stderr: str,
    fixture: str | None = None,
) -> dict[str, Any]:
    record = {
        "status": "pinned",
        "exit": exit_code,
        "stdout": stdout,
        "stdout_sha256": sha256_bytes(stdout.encode("utf-8")),
        "stderr": stderr,
        "stderr_sha256": sha256_bytes(stderr.encode("utf-8")),
    }
    if fixture is not None:
        record["fixture"] = fixture
    return record


def pending_oracle(fixture: str) -> dict[str, Any]:
    return {
        "status": "fixture-pending",
        "fixture": fixture,
        "exit": None,
        "stdout": None,
        "stdout_sha256": None,
        "stderr": None,
        "stderr_sha256": None,
    }


def fixture_record(
    repo_root: Path, fixture: dict[str, Any]
) -> dict[str, Any]:
    record: dict[str, Any] = {
        "name": fixture["name"],
        "mode": fixture["mode"],
    }
    if "kind" in fixture:
        record["kind"] = fixture["kind"]
    if "source_file" in fixture:
        record["source_file"] = fixture["source_file"]
    if "stdin" in fixture:
        record["stdin"] = fixture["stdin"]
        record["stdin_sha256"] = sha256_bytes(
            fixture["stdin"].encode("utf-8")
        )
    if "expected_prefix" in fixture:
        record["expected_prefix"] = fixture["expected_prefix"]
    fixture_paths = list(fixture.get("files", []))
    if "source_file" in fixture:
        fixture_paths.append(fixture["source_file"])
    fixture_paths.extend(
        f"tests/petta/fixtures/python/{name}"
        for name in fixture.get("python_files", [])
    )
    files = []
    for relative in fixture_paths:
        files.append(
            {
                "path": relative,
                "sha256": sha256_file(repo_root / relative),
            }
        )
    if files:
        record["files"] = files
    if "source_replacements" in fixture:
        record["source_replacements"] = fixture["source_replacements"]
        if "source_prefix" in fixture:
            record["source_prefix"] = fixture["source_prefix"]
            record["source_prefix_sha256"] = sha256_bytes(
                fixture["source_prefix"].encode("utf-8")
            )
        record["upstream"] = {
            "kind": "pinned-petta-oracle",
        }
    if fixture.get("python_files"):
        record["environment"] = {
            "PYTHONPATH": "tests/petta/fixtures/python",
            "PYTHONDONTWRITEBYTECODE": "1",
        }
    return record


def hermetic_required_file_records(
    petta_dir: Path, name: str
) -> list[dict[str, str]]:
    records = []
    for relative in HERMETIC_REQUIRED_FILES.get(name, ()):
        path = petta_dir / relative
        if not path.is_file():
            raise RuntimeError(
                f"{name}: required hermetic file is missing: {relative}"
            )
        records.append(
            {
                "path": relative,
                "sha256": sha256_file(path),
            }
        )
    return records


def build_manifest(
    petta_dir: Path, timeout_seconds: float
) -> dict[str, Any]:
    repo_root = Path(__file__).resolve().parents[1]
    examples_dir = petta_dir / "examples"
    examples = sorted(examples_dir.glob("*.metta"), key=lambda path: path.name)
    if len(examples) != EXPECTED_TOTAL:
        raise RuntimeError(
            f"expected {EXPECTED_TOTAL} PeTTa examples, found {len(examples)}"
        )

    names = {path.name for path in examples}
    missing_fixtures = sorted(set(FIXTURE_CASES) - names)
    if missing_fixtures:
        raise RuntimeError(
            "fixture-backed corpus entries are missing: "
            + ", ".join(missing_fixtures)
        )

    tracked = tracked_examples(petta_dir)
    entries: list[dict[str, Any]] = []
    for index, source in enumerate(examples, start=1):
        source_bytes = source.read_bytes()
        entry_class = "hermetic"
        fixture = None
        if source.name in FIXTURE_CASES:
            fixture = FIXTURE_CASES[source.name]
            entry_class = fixture["class"]
        elif source.name in NEGATIVE_CASES:
            entry_class = "hermetic-negative"

        entry: dict[str, Any] = {
            "name": source.name,
            "source": f"examples/{source.name}",
            "source_sha256": sha256_bytes(source_bytes),
            "git_state": "tracked" if source.name in tracked else "untracked",
            "class": entry_class,
            "command": [
                "sh",
                "run.sh",
                f"examples/{source.name}",
                "--silent",
            ],
            "timeout_seconds": timeout_seconds,
        }
        if source.name in NEGATIVE_CASES:
            entry["negative_contract"] = NEGATIVE_CASES[source.name]

        required_files = hermetic_required_file_records(
            petta_dir, source.name
        )
        if required_files:
            entry["required_files"] = required_files

        if fixture is not None:
            entry["fixture"] = fixture_record(repo_root, fixture)

        if fixture is None:
            exit_code, stdout, stderr = run_oracle(
                repo_root, petta_dir, source, timeout_seconds
            )
            entry["oracle"] = oracle_record(exit_code, stdout, stderr)
        elif fixture["mode"] == "pending":
            entry["oracle"] = pending_oracle(fixture["name"])
        else:
            exit_code, stdout, stderr = run_oracle(
                repo_root, petta_dir, source, timeout_seconds, fixture
            )
            entry["oracle"] = oracle_record(
                exit_code, stdout, stderr, fixture["name"]
            )
        entries.append(entry)
        print(
            f"[{index:03d}/{len(examples):03d}] {source.name}: "
            f"{entry['oracle']['status']}",
            flush=True,
        )

    script_path = Path(__file__).resolve()
    return {
        "schema": SCHEMA,
        "petta_revision": git_revision(petta_dir),
        "run_sh_sha256": sha256_file(petta_dir / "run.sh"),
        "generator_sha256": sha256_file(script_path),
        "normalization": normalization_contract(),
        "counts": {
            "total": len(entries),
            "hermetic": sum(
                entry["class"].startswith("hermetic") for entry in entries
            ),
            "controlled": sum(
                entry["class"] in {"external", "interactive"}
                for entry in entries
            ),
        },
        "entries": entries,
    }


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(
        manifest, ensure_ascii=False, indent=2, sort_keys=False
    )
    path.write_text(payload + "\n", encoding="utf-8")


def load_manifest(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("PeTTa corpus manifest root must be an object")
    return value


def verify_oracle(
    name: str,
    oracle: dict[str, Any],
    require_complete: bool,
    fixture_mode: str | None = None,
) -> None:
    status = oracle.get("status")
    if status == "fixture-pending":
        if require_complete:
            raise RuntimeError(f"{name}: controlled fixture is still pending")
        return
    if status != "pinned":
        raise RuntimeError(f"{name}: unknown oracle status {status!r}")
    for stream_name in ("stdout", "stderr"):
        stream = oracle.get(stream_name)
        digest = oracle.get(f"{stream_name}_sha256")
        if not isinstance(stream, str) or not isinstance(digest, str):
            raise RuntimeError(f"{name}: incomplete {stream_name} contract")
        actual = sha256_bytes(stream.encode("utf-8"))
        if actual != digest:
            raise RuntimeError(
                f"{name}: {stream_name} digest is {actual}, expected {digest}"
            )
    exit_contract = oracle.get("exit")
    if fixture_mode == "prefix":
        if exit_contract != "supervised-prefix":
            raise RuntimeError(
                f"{name}: supervised prefix exit contract changed"
            )
    elif not isinstance(exit_contract, int):
        raise RuntimeError(f"{name}: pinned oracle lacks an integer exit code")


def verify_manifest(
    petta_dir: Path, manifest_path: Path, require_complete: bool
) -> None:
    manifest = load_manifest(manifest_path)
    if manifest.get("schema") != SCHEMA:
        raise RuntimeError(
            f"unsupported PeTTa corpus schema {manifest.get('schema')!r}"
        )
    if manifest.get("normalization") != normalization_contract():
        raise RuntimeError("PeTTa corpus normalization contract changed")
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        raise RuntimeError("PeTTa corpus manifest entries must be a list")
    if len(entries) != EXPECTED_TOTAL:
        raise RuntimeError(
            f"manifest has {len(entries)} entries, expected {EXPECTED_TOTAL}"
        )
    names_in_order = [entry.get("name") for entry in entries]
    if not all(isinstance(name, str) for name in names_in_order):
        raise RuntimeError("manifest entry names must be strings")
    if names_in_order != sorted(names_in_order):
        raise RuntimeError("PeTTa corpus manifest entries are not name-sorted")

    disk_paths = sorted((petta_dir / "examples").glob("*.metta"))
    disk_names = {path.name for path in disk_paths}
    manifest_names = set(names_in_order)
    if len(manifest_names) != len(entries):
        raise RuntimeError("PeTTa corpus manifest contains duplicate names")
    if manifest_names != disk_names:
        missing = sorted(disk_names - manifest_names)
        extra = sorted(manifest_names - disk_names)
        raise RuntimeError(
            f"manifest/disk name mismatch: missing={missing}, extra={extra}"
        )

    repo_root = Path(__file__).resolve().parents[1]
    tracked = tracked_examples(petta_dir)
    controlled = 0
    hermetic = 0
    for entry in entries:
        name = entry["name"]
        expected_source = f"examples/{name}"
        if entry.get("source") != expected_source:
            raise RuntimeError(
                f"{name}: source must be exactly {expected_source!r}"
            )
        source = petta_dir / entry["source"]
        digest = sha256_file(source)
        if digest != entry.get("source_sha256"):
            raise RuntimeError(
                f"{name}: source digest is {digest}, "
                f"expected {entry.get('source_sha256')}"
            )

        expected_git_state = "tracked" if name in tracked else "untracked"
        if entry.get("git_state") != expected_git_state:
            raise RuntimeError(
                f"{name}: git state is {expected_git_state}, "
                f"expected {entry.get('git_state')}"
            )

        expected_command = [
            "sh",
            "run.sh",
            expected_source,
            "--silent",
        ]
        if entry.get("command") != expected_command:
            raise RuntimeError(f"{name}: oracle command changed")
        timeout_seconds = entry.get("timeout_seconds")
        if (
            not isinstance(timeout_seconds, (int, float))
            or not math.isfinite(timeout_seconds)
            or timeout_seconds <= 0
        ):
            raise RuntimeError(f"{name}: timeout must be finite and positive")

        expected_class = "hermetic"
        if name in FIXTURE_CASES:
            expected_class = FIXTURE_CASES[name]["class"]
        elif name in NEGATIVE_CASES:
            expected_class = "hermetic-negative"
        if entry.get("class") != expected_class:
            raise RuntimeError(
                f"{name}: class is {entry.get('class')!r}, "
                f"expected {expected_class!r}"
            )
        if name in NEGATIVE_CASES:
            if entry.get("negative_contract") != NEGATIVE_CASES[name]:
                raise RuntimeError(f"{name}: negative contract changed")
        elif "negative_contract" in entry:
            raise RuntimeError(
                f"{name}: unexpected negative contract"
            )

        if expected_class in {"external", "interactive"}:
            controlled += 1
        else:
            hermetic += 1
        oracle = entry.get("oracle")
        if not isinstance(oracle, dict):
            raise RuntimeError(f"{name}: oracle contract must be an object")
        if name in FIXTURE_CASES:
            fixture = FIXTURE_CASES[name]
            expected_fixture = fixture_record(repo_root, fixture)
            if entry.get("fixture") != expected_fixture:
                raise RuntimeError(
                    f"{name}: fixture record changed"
                )
            if oracle.get("fixture") != fixture["name"]:
                raise RuntimeError(
                    f"{name}: fixture is {oracle.get('fixture')!r}, "
                    f"expected {fixture['name']!r}"
                )
            fixture_mode = fixture["mode"]
        else:
            if "fixture" in entry or "fixture" in oracle:
                raise RuntimeError(f"{name}: unexpected controlled fixture")
            fixture_mode = None

        expected_required_files = hermetic_required_file_records(
            petta_dir, name
        )
        if expected_required_files:
            if entry.get("required_files") != expected_required_files:
                raise RuntimeError(
                    f"{name}: required hermetic files changed"
                )
        elif "required_files" in entry:
            raise RuntimeError(
                f"{name}: unexpected required hermetic files"
            )
        verify_oracle(
            name, oracle, require_complete, fixture_mode
        )

    if controlled != EXPECTED_CONTROLLED:
        raise RuntimeError(
            f"manifest has {controlled} controlled cases, "
            f"expected {EXPECTED_CONTROLLED}"
        )
    expected_counts = {
        "total": len(entries),
        "hermetic": hermetic,
        "controlled": controlled,
    }
    if manifest.get("counts") != expected_counts:
        raise RuntimeError(
            f"manifest counts are {manifest.get('counts')!r}, "
            f"expected {expected_counts!r}"
        )
    if manifest.get("petta_revision") != git_revision(petta_dir):
        raise RuntimeError(
            "PeTTa revision changed after the oracle was frozen"
        )
    if manifest.get("run_sh_sha256") != sha256_file(petta_dir / "run.sh"):
        raise RuntimeError("PeTTa run.sh changed after the oracle was frozen")
    if manifest.get("generator_sha256") != sha256_file(Path(__file__).resolve()):
        raise RuntimeError(
            "PeTTa corpus manifest generator changed after the oracle was frozen"
        )
    print(
        f"PASS: PeTTa corpus manifest covers {len(entries)} examples "
        f"({len(entries) - controlled} hermetic, {controlled} controlled)"
    )


def verify_exact_match_counts(counts: dict[str, int], selected: int) -> None:
    if counts != {"MATCH": selected}:
        raise RuntimeError(
            "PeTTa corpus differential is not exact: "
            + json.dumps(dict(sorted(counts.items())), sort_keys=True)
        )


def compare_manifest(
    petta_dir: Path,
    manifest_path: Path,
    cetta: Path,
    out_dir: Path,
    only: set[str],
    limit: int | None,
    timeout_override: float | None,
    require_complete: bool,
    require_match: bool,
) -> None:
    verify_manifest(petta_dir, manifest_path, require_complete)
    manifest = load_manifest(manifest_path)
    entries = manifest["entries"]
    if only:
        unknown = sorted(only - {entry["name"] for entry in entries})
        if unknown:
            raise RuntimeError(
                "requested examples are absent from the manifest: "
                + ", ".join(unknown)
            )
        entries = [entry for entry in entries if entry["name"] in only]
    if limit is not None:
        entries = entries[:limit]

    if not cetta.is_file():
        raise RuntimeError(f"CeTTa binary does not exist: {cetta}")
    out_dir.mkdir(parents=True, exist_ok=True)
    actual_dir = out_dir / "actual"
    actual_dir.mkdir(parents=True, exist_ok=True)
    rows = [
        "example\tclass\tstatus\toracle_exit\tcetta_exit\t"
        "stdout_equal\tstderr_equal\tcetta_seconds"
    ]
    counts: dict[str, int] = {}

    for index, entry in enumerate(entries, start=1):
        name = entry["name"]
        oracle = entry["oracle"]
        if oracle["status"] == "fixture-pending":
            status = "FIXTURE_PENDING"
            rows.append(
                f"{name}\t{entry['class']}\t{status}\t-\t-\t-\t-\t-"
            )
            counts[status] = counts.get(status, 0) + 1
            print(f"[{index:03d}/{len(entries):03d}] {name}: {status}")
            continue

        source = petta_dir / entry["source"]
        timeout_seconds = (
            timeout_override
            if timeout_override is not None
            else float(entry["timeout_seconds"])
        )
        started = time.monotonic()
        cetta_exit, cetta_stdout, cetta_stderr = run_cetta(
            cetta,
            petta_dir,
            source,
            timeout_seconds,
            FIXTURE_CASES.get(name),
        )
        elapsed = time.monotonic() - started
        stdout_equal = cetta_stdout == oracle["stdout"]
        stderr_equal = cetta_stderr == oracle["stderr"]

        (actual_dir / f"{name}.stdout").write_text(
            cetta_stdout, encoding="utf-8"
        )
        (actual_dir / f"{name}.stderr").write_text(
            cetta_stderr, encoding="utf-8"
        )
        if cetta_exit == 124:
            status = "CETTA_TIMEOUT"
        elif cetta_exit == "output-limit":
            status = "CETTA_OUTPUT_LIMIT"
        elif cetta_exit == "controlled-failure":
            status = "CETTA_CONTROL_FAILURE"
        elif cetta_exit != oracle["exit"]:
            status = "EXIT_MISMATCH"
        elif not stdout_equal or not stderr_equal:
            status = "OUTPUT_MISMATCH"
        else:
            status = "MATCH"
        counts[status] = counts.get(status, 0) + 1
        rows.append(
            f"{name}\t{entry['class']}\t{status}\t"
            f"{oracle['exit']}\t{cetta_exit}\t"
            f"{int(stdout_equal)}\t{int(stderr_equal)}\t{elapsed:.3f}"
        )
        print(
            f"[{index:03d}/{len(entries):03d}] {name}: {status} "
            f"({elapsed:.2f}s)",
            flush=True,
        )

    (out_dir / "results.tsv").write_text(
        "\n".join(rows) + "\n", encoding="utf-8"
    )
    summary = {
        "manifest_sha256": sha256_file(manifest_path),
        "cetta": str(cetta),
        "cetta_sha256": sha256_file(cetta),
        "counts": dict(sorted(counts.items())),
        "selected": len(entries),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary["counts"], sort_keys=True))
    if require_match:
        verify_exact_match_counts(counts, len(entries))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("freeze", "verify", "compare"):
        sub = subparsers.add_parser(command)
        sub.add_argument("--petta-dir", type=Path, required=True)
        sub.add_argument("--manifest", type=Path, required=True)
        if command == "freeze":
            sub.add_argument("--timeout", type=float, default=30.0)
        elif command == "verify":
            sub.add_argument("--require-complete", action="store_true")
        else:
            sub.add_argument("--cetta", type=Path, required=True)
            sub.add_argument("--out", type=Path, required=True)
            sub.add_argument("--only", action="append", default=[])
            sub.add_argument("--limit", type=int)
            sub.add_argument("--timeout", type=float)
            sub.add_argument("--require-complete", action="store_true")
            sub.add_argument("--require-match", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    petta_dir = args.petta_dir.resolve()
    manifest_path = args.manifest.resolve()
    try:
        if args.command == "freeze":
            manifest = build_manifest(petta_dir, args.timeout)
            write_manifest(manifest_path, manifest)
            verify_manifest(petta_dir, manifest_path, False)
        elif args.command == "verify":
            verify_manifest(
                petta_dir, manifest_path, args.require_complete
            )
        else:
            compare_manifest(
                petta_dir,
                manifest_path,
                args.cetta.resolve(),
                args.out.resolve(),
                set(args.only),
                args.limit,
                args.timeout,
                args.require_complete,
                args.require_match,
            )
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
