#!/usr/bin/env python3
"""The PeTTa example corpus on CeTTa: an unmodified base, overlays, our own oracles.

The base is a PeTTa checkout or snapshot.  The manifest records it in one field,
``<name>@<commit>``, where the name says which line it follows (``upstream`` by
default, or ``stable-fixed``, the queue of fixes pending upstream).  The base is
never edited: every run works on a copy of it.

A correction to a base file is an overlay in ``tests/petta/overlay/``:
``<stem>.patch`` is a unified diff against the base (paths ``a/...`` and
``b/...`` relative to the base root) and ``<stem>.why`` is one line giving the
reason, with the upstream issue or pull request when there is one.  The overlay
named after an example is applied to a copy of the base whenever that example
runs.

Every example is judged by its own law, never by another engine's bytes:

- ``self-check``: an example that makes assertions (``test`` or the ``assert``
  family) passes iff every assertion it makes passes and every top-level
  ``test`` prints its verdict.  A self-check that CeTTa currently fails is kept
  with ``failing`` set to its first failing verdict; it must keep failing until
  ``accept`` records that it passes.
- ``golden``: any other example must reproduce ``tests/petta/golden/<stem>.stdout``,
  CeTTa's normalized stdout, refreshed by ``accept``.
- ``negative``: an example meant to fail must exit as recorded and print the
  recorded failure line.

``sync`` moves the corpus to the base it is given: it records the commit,
retires every overlay the base already contains, names every overlay that no
longer applies, and refreshes the classification and goldens, printing the
diff.  ``accept`` does the same refresh on the recorded base.  SWI-PeTTa is
never an oracle: ``swi-report`` only lists divergences for a person to classify.
"""

from __future__ import annotations

import argparse
from collections import Counter
from contextlib import contextmanager
import difflib
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
from typing import Any, Iterator, NamedTuple


REPO_ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "cetta-petta-corpus-v3"
DEFAULT_MANIFEST = REPO_ROOT / "tests" / "petta" / "corpus" / "manifest.json"
DEFAULT_OVERLAY_DIR = REPO_ROOT / "tests" / "petta" / "overlay"
DEFAULT_GOLDEN_DIR = REPO_ROOT / "tests" / "petta" / "golden"
KNOWN_BASES = ("upstream", "stable-fixed")
DEFAULT_BASE = "upstream"
BASE_NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
COMMIT_RE = re.compile(r"[0-9a-f]{40}")
DEFAULT_TIMEOUT_SECONDS = 60
MODE_SELF_CHECK = "self-check"
MODE_GOLDEN = "golden"
MODE_NEGATIVE = "negative"
MODES = (MODE_SELF_CHECK, MODE_GOLDEN, MODE_NEGATIVE)
ENTRY_KEYS = (
    "name", "mode", "overlay", "timeout", "fixture", "requires",
    "failing", "exit", "failure", "observation",
)

STDOUT_EXACT_STREAM = "exact-stream"
STDOUT_OCCURRENCE_BAG = "occurrence-bag"
STDOUT_OBSERVATION_CONTRACTS = frozenset({
    STDOUT_EXACT_STREAM,
    STDOUT_OCCURRENCE_BAG,
})
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
MORK_READY_BANNER = "MORK init: done\n"
PYTHON_RUNTIME_WARNING = (
    "Could not find platform dependent libraries <exec_prefix>\n"
)
OPTIONAL_MORK_INIT_FAILURE_RE = re.compile(
    r"\AERROR: <PETTA_ROOT>/mork_ffi/morkspaces\.pl:[0-9]+:\n"
    r"ERROR:    library/3: Unknown procedure: library_path/1\n"
    r"Warning: <PETTA_ROOT>/mork_ffi/morkspaces\.pl:[0-9]+:\n"
    r"Warning:    Goal \(directive\) failed: user:\(library\(mork_ffi,"
    r"'morklib\.so',(?P<temporary>_[0-9]+)\),"
    r"use_foreign_library\((?P=temporary)\),"
    r"current_predicate\(mork/3\)->writeln\(\"MORK init: done\"\);"
    r"writeln\(\"MORK init: failed\"\)\)\n"
)
OPTIONAL_JANUS_INIT_FAILURE_RE = re.compile(
    r"\AERROR: [^\n]*/swipy/janus\.pl:[0-9]+:\n"
    r"ERROR:    [^\n]*/swipy/janus\.pl:[0-9]+: Initialization goal "
    r"raised exception:\n"
    r"ERROR:    open_shared_object/3: libpython[^ \n]*: cannot open "
    r"shared object file: No such file or directory\n"
)
VARIABLE_TERMINATORS = frozenset(" \t\r\n()[]{}\",")
TEST_DIAGNOSTIC_SUFFIXES = (". ✅ ", ". ❌ ")
PASSED_SUFFIX, FAILED_SUFFIX = TEST_DIAGNOSTIC_SUFFIXES
ASSERTION_FAILED_PREFIX = "Assertion failed: "
SPECIALIZER_DIAGNOSTIC_RE = re.compile(
    r"Not specialized [^\r\n]+/[0-9]+"
)
MAX_CAPTURE_BYTES = 16 * 1024 * 1024

# Heads of the assertion forms an example uses to check itself.  `test` prints
# a verdict line either way; the `assert` family prints only on failure.
TEST_HEAD = "test"
ASSERT_HEADS = frozenset({
    "assert",
    "assertEqual",
    "assertAlphaEqual",
    "assertEqualToResult",
    "assertAlphaEqualToResult",
})
TOKEN_TERMINATORS = frozenset(" \t\r\n()\";")

# Copying the base: version-control state, caches and imported packages are
# not part of it; the native MORK tree is large and only SWI-PeTTa loads it,
# so a copy links it instead.
BASE_COPY_SKIP = frozenset({".git", "__pycache__", "build", "repos"})
BASE_COPY_LINK = frozenset({"mork_ffi"})

KNOWN_OPTIONAL_CAPABILITIES = frozenset({"git", "lib-prolog"})
CASE_CAPABILITY_REQUIREMENTS: dict[str, tuple[str, ...]] = {
    # Its fixture has CeTTa clone a local repository, so it needs git.
    "git_import2.metta": ("git", "lib-prolog"),
    "prologimport.metta": ("lib-prolog",),
    "test_datetime.metta": ("lib-prolog",),
}

# Environment fixtures: what an example needs around it (input, a stand-in
# Python module, a local package, a local repository), never a change to the
# example or the library it uses.  Changes to base files are overlays.
CONTROLLED_CASES: dict[str, dict[str, Any]] = {
    "git_import.metta": {
        "class": "external",
        "name": "local-package-fixture",
        "mode": "complete",
        "kind": "local-package",
        "stdin": "",
        "packages": ["repos/test_metta_lib"],
    },
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
            "(library lib_faiss)":
                "(library faiss_ffi lib_faiss)",
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
        # The REPL never exits on end of input; its golden is the prefix it
        # must print while it is still running.
        "class": "interactive",
        "name": "scripted-prefix-session",
        "mode": "prefix",
        "stdin": "(+ 1 2)\n",
    },
    "torch.metta": {
        "class": "external",
        "name": "deterministic-python-torch-fixture",
        "mode": "complete",
        "stdin": "",
        "python_files": ["torch.py"],
    },
}
FIXTURE_CASES = CONTROLLED_CASES


# ---------------------------------------------------------------------------
# Normalization


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def normalize_stream(
    text: str, petta_dir: Path, extra_roots: tuple[Path, ...] = ()
) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = ANSI_RE.sub("", text)
    # Longest first, so a root inside another root is replaced whole.
    roots = sorted(
        {str(root) for root in (petta_dir, *extra_roots)},
        key=len, reverse=True,
    )
    for root in roots:
        text = text.replace(root, "<PETTA_ROOT>")
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
    """Normalize SWI-PeTTa stdout (it may print a MORK banner first)."""
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


def semantic_stdout(text: str) -> str:
    """Remove the optional engine diagnostic from stdout comparison.

    PeTTa historically printed unsuccessful specialization attempts on
    stdout.  They are neither program answers nor test verdicts, and CeTTa
    exposes the same information only through its opt-in trace.
    """
    kept: list[str] = []
    for line_with_ending in text.splitlines(keepends=True):
        line = line_with_ending.rstrip("\r\n")
        if SPECIALIZER_DIAGNOSTIC_RE.fullmatch(line):
            continue
        kept.append(line_with_ending)
    return "".join(kept)


def stdout_observation(
    text: str, contract: str
) -> str | Counter[str]:
    """Observe output under an explicitly selected answer algebra."""
    semantic = semantic_stdout(text)
    if contract == STDOUT_EXACT_STREAM:
        return semantic
    if contract == STDOUT_OCCURRENCE_BAG:
        # Line terminators are transport framing, not part of an answer.
        # Multiplicity and every character within each answer remain exact.
        return Counter(semantic.splitlines())
    raise ValueError(f"unknown stdout observation contract: {contract!r}")


def stdout_observation_equal(
    left: str, right: str, contract: str
) -> bool:
    return stdout_observation(left, contract) == stdout_observation(
        right, contract
    )


def normalize_oracle_stderr(
    text: str, petta_dir: Path, extra_roots: tuple[Path, ...] = ()
) -> str:
    normalized = normalize_stream(text, petta_dir, extra_roots)
    while True:
        previous = normalized
        if normalized.startswith(PYTHON_RUNTIME_WARNING):
            normalized = normalized[len(PYTHON_RUNTIME_WARNING):]
        normalized = OPTIONAL_MORK_INIT_FAILURE_RE.sub("", normalized, count=1)
        normalized = OPTIONAL_JANUS_INIT_FAILURE_RE.sub("", normalized, count=1)
        if normalized == previous:
            break
    return normalized


def split_lines(text: str) -> list[str]:
    """Split on newline only, keeping it; other separators stay in lines."""
    parts = text.split("\n")
    lines = [part + "\n" for part in parts[:-1]]
    if parts[-1]:
        lines.append(parts[-1])
    return lines


# ---------------------------------------------------------------------------
# Processes


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
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
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

    if process.returncode is None:
        process.wait()
    exit_code = process.returncode if process.returncode is not None else 1
    if (timed_out or output_limited) and exit_code == 0:
        # The resource contract dominates a child that happened to exit just
        # before supervision could terminate it.
        exit_code = 1

    return (
        exit_code,
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
            "supervised process did not produce its expected prefix; "
            f"stdout={normalized_stdout[:500]!r}, "
            f"stderr={normalized_stderr[:500]!r}"
        )
    if not prefix_seen_while_live:
        raise RuntimeError(
            "supervised process exited instead of remaining live after prefix"
        )
    return "supervised-prefix", expected_prefix, normalized_stderr


# ---------------------------------------------------------------------------
# Overlays: unified diffs applied with exact context (offsets allowed, no fuzz)


class PatchError(RuntimeError):
    pass


HUNK_HEADER_RE = re.compile(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


class Hunk(NamedTuple):
    old_start: int
    old_lines: list[str]
    new_start: int
    new_lines: list[str]


class FilePatch(NamedTuple):
    path: str
    hunks: list[Hunk]
    creates: bool = False
    deletes: bool = False


def patch_path(raw: str) -> str | None:
    path = raw.split("\t", 1)[0].strip()
    if path == "/dev/null":
        return None
    if path.startswith(("a/", "b/")):
        path = path[2:]
    parts = Path(path).parts
    if not path or Path(path).is_absolute() or ".." in parts:
        raise PatchError(f"patch path must stay inside the base: {raw!r}")
    return path


def parse_unified_diff(text: str) -> list[FilePatch]:
    lines = split_lines(text)
    patches: list[FilePatch] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        if not (
            line.startswith("--- ")
            and index + 1 < len(lines)
            and lines[index + 1].startswith("+++ ")
        ):
            index += 1
            continue
        old = patch_path(line[4:].rstrip("\n"))
        new = patch_path(lines[index + 1][4:].rstrip("\n"))
        if old is None and new is None:
            raise PatchError("a file patch needs at least one real path")
        file_patch = FilePatch(
            path=new if new is not None else old,
            hunks=[],
            creates=old is None,
            deletes=new is None,
        )
        index += 2
        while index < len(lines) and lines[index].startswith("@@"):
            match = HUNK_HEADER_RE.match(lines[index])
            if match is None:
                raise PatchError(f"malformed hunk header: {lines[index]!r}")
            old_count = int(match[2]) if match[2] is not None else 1
            new_count = int(match[4]) if match[4] is not None else 1
            hunk = Hunk(int(match[1]), [], int(match[3]), [])
            index += 1
            last_targets: tuple[list[str], ...] = ()
            while (
                old_count > 0 or new_count > 0
                or (index < len(lines) and lines[index].startswith("\\"))
            ):
                if index >= len(lines):
                    raise PatchError(f"{file_patch.path}: truncated hunk")
                body = lines[index]
                index += 1
                if body.startswith("\\"):
                    # The previous line has no newline at end of file.
                    for target in last_targets:
                        if target[-1].endswith("\n"):
                            target[-1] = target[-1][:-1]
                    continue
                tag, content = (" ", "\n") if body == "\n" else (
                    body[0], body[1:]
                )
                if tag == " ":
                    hunk.old_lines.append(content)
                    hunk.new_lines.append(content)
                    old_count -= 1
                    new_count -= 1
                    last_targets = (hunk.old_lines, hunk.new_lines)
                elif tag == "-":
                    hunk.old_lines.append(content)
                    old_count -= 1
                    last_targets = (hunk.old_lines,)
                elif tag == "+":
                    hunk.new_lines.append(content)
                    new_count -= 1
                    last_targets = (hunk.new_lines,)
                else:
                    raise PatchError(
                        f"{file_patch.path}: unexpected hunk line {body!r}"
                    )
                if old_count < 0 or new_count < 0:
                    raise PatchError(
                        f"{file_patch.path}: hunk longer than its header"
                    )
            following = lines[index] if index < len(lines) else ""
            if following[:1] in {" ", "-", "+"} and following != "-- \n" and not (
                following.startswith("--- ")
                and index + 1 < len(lines)
                and lines[index + 1].startswith("+++ ")
            ):
                raise PatchError(
                    f"{file_patch.path}: hunk longer than its header"
                )
            file_patch.hunks.append(hunk)
        if not file_patch.hunks:
            raise PatchError(f"{file_patch.path}: file patch without hunks")
        patches.append(file_patch)
    if not patches:
        raise PatchError("no file patch found")
    paths = [patch.path for patch in patches]
    if len(set(paths)) != len(paths):
        raise PatchError("one diff per file: a path appears twice")
    return patches


def find_block(
    lines: list[str], block: list[str], expected: int, minimum: int
) -> int | None:
    """The position of block nearest to expected, at or after minimum."""
    last = len(lines) - len(block)
    if last < minimum:
        return None
    expected = min(max(expected, minimum), last)
    if not block:
        return expected
    for distance in range(0, max(expected - minimum, last - expected) + 1):
        for position in (expected - distance, expected + distance):
            if minimum <= position <= last and (
                lines[position:position + len(block)] == block
            ):
                return position
    return None


def apply_hunks(
    lines: list[str], hunks: list[Hunk], *, reverse: bool = False
) -> list[str]:
    result = list(lines)
    offset = 0
    minimum = 0
    for number, hunk in enumerate(hunks, start=1):
        if reverse:
            before, after, start = hunk.new_lines, hunk.old_lines, hunk.new_start
        else:
            before, after, start = hunk.old_lines, hunk.new_lines, hunk.old_start
        # A range of length zero names the line after which text is inserted.
        anchor = start if not before else max(start - 1, 0)
        position = find_block(result, before, anchor + offset, minimum)
        if position is None:
            raise PatchError(
                f"hunk {number} (line {start}) does not match the base"
            )
        result[position:position + len(before)] = after
        offset = position - anchor + len(after) - len(before)
        minimum = position + len(after)
    return result


def apply_file_patch(
    root: Path, file_patch: FilePatch, *, reverse: bool = False
) -> str | None:
    """The patched text of one file, or None when the patch deletes it."""
    target = root / file_patch.path
    creates, deletes = (
        (file_patch.deletes, file_patch.creates)
        if reverse else (file_patch.creates, file_patch.deletes)
    )
    if creates:
        if target.exists():
            raise PatchError(f"{file_patch.path}: exists, but the patch creates it")
        lines: list[str] = []
    else:
        if not target.is_file():
            raise PatchError(f"{file_patch.path}: missing from the base")
        try:
            lines = split_lines(target.read_text(encoding="utf-8"))
        except UnicodeDecodeError as error:
            raise PatchError(f"{file_patch.path}: not UTF-8 ({error})") from error
    try:
        patched = apply_hunks(lines, file_patch.hunks, reverse=reverse)
    except PatchError as error:
        raise PatchError(f"{file_patch.path}: {error}") from error
    if deletes:
        if patched:
            raise PatchError(
                f"{file_patch.path}: the patch deletes it, but text remains"
            )
        return None
    return "".join(patched)


def patched_texts(
    root: Path, patches: list[FilePatch], *, reverse: bool = False
) -> dict[str, str | None]:
    return {
        file_patch.path: apply_file_patch(root, file_patch, reverse=reverse)
        for file_patch in patches
    }


OVERLAY_APPLIES = "applies"
OVERLAY_IN_BASE = "in-base"
OVERLAY_STALE = "stale"


def overlay_state(root: Path, patches: list[FilePatch]) -> tuple[str, str]:
    """applies, in-base (the base already contains the change) or stale."""
    try:
        patched_texts(root, patches)
        return OVERLAY_APPLIES, ""
    except PatchError as forward:
        try:
            patched_texts(root, patches, reverse=True)
            return OVERLAY_IN_BASE, ""
        except PatchError:
            return OVERLAY_STALE, str(forward)


def write_patched(workspace: Path, patches: list[FilePatch]) -> None:
    texts = patched_texts(workspace, patches)
    for relative, text in texts.items():
        target = workspace / relative
        probe = workspace
        for part in Path(relative).parts[:-1]:
            probe = probe / part
            if probe.is_symlink():
                raise PatchError(
                    f"{relative}: an overlay may not write through a linked "
                    "directory of the base"
                )
        if target.is_symlink() or text is None:
            target.unlink()
        if text is not None:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text, encoding="utf-8")


class Overlay(NamedTuple):
    stem: str
    patch_file: Path
    why_file: Path
    reason: str
    patches: list[FilePatch]

    @property
    def example(self) -> str:
        return f"{self.stem}.metta"

    @property
    def file_name(self) -> str:
        return self.patch_file.name


def overlay_reason(why_file: Path) -> str:
    text = why_file.read_text(encoding="utf-8")
    lines = text.splitlines()
    if len(lines) != 1 or not lines[0].strip():
        raise PatchError(
            f"{why_file.name}: must be exactly one non-empty line"
        )
    return lines[0].strip()


def load_overlays(
    overlay_dir: Path,
) -> tuple[dict[str, Overlay], list[str]]:
    """Overlays by example name, and the problems found reading them."""
    overlays: dict[str, Overlay] = {}
    problems: list[str] = []
    if not overlay_dir.is_dir():
        return overlays, problems
    for path in sorted(overlay_dir.iterdir()):
        if path.suffix == ".why":
            if not path.with_suffix(".patch").is_file():
                problems.append(f"overlay {path.name} has no .patch beside it")
            continue
        if path.suffix != ".patch":
            problems.append(f"unexpected file in the overlay directory: {path.name}")
            continue
        why = path.with_suffix(".why")
        if not why.is_file():
            problems.append(f"overlay {path.name} has no .why beside it")
            continue
        try:
            reason = overlay_reason(why)
            patches = parse_unified_diff(path.read_text(encoding="utf-8"))
        except (PatchError, UnicodeDecodeError) as error:
            problems.append(f"overlay {path.name}: {error}")
            continue
        overlay = Overlay(path.stem, path, why, reason, patches)
        overlays[overlay.example] = overlay
    return overlays, problems


def format_file_patches(patches: list[FilePatch]) -> str:
    """Canonical unified diff text of parsed file patches."""

    def body_line(tag: str, line: str) -> list[str]:
        if line.endswith("\n"):
            return [tag + line]
        return [tag + line + "\n", "\\ No newline at end of file\n"]

    output: list[str] = []
    for file_patch in patches:
        output.append(
            "--- " + ("/dev/null" if file_patch.creates else f"a/{file_patch.path}")
            + "\n"
        )
        output.append(
            "+++ " + ("/dev/null" if file_patch.deletes else f"b/{file_patch.path}")
            + "\n"
        )
        for hunk in file_patch.hunks:
            output.append(
                f"@@ -{hunk.old_start},{len(hunk.old_lines)} "
                f"+{hunk.new_start},{len(hunk.new_lines)} @@\n"
            )
            matcher = difflib.SequenceMatcher(
                a=hunk.old_lines, b=hunk.new_lines, autojunk=False
            )
            for tag, old_from, old_to, new_from, new_to in matcher.get_opcodes():
                if tag == "equal":
                    for line in hunk.old_lines[old_from:old_to]:
                        output += body_line(" ", line)
                    continue
                for line in hunk.old_lines[old_from:old_to]:
                    output += body_line("-", line)
                for line in hunk.new_lines[new_from:new_to]:
                    output += body_line("+", line)
    return "".join(output)


def export_overlay(overlay: Overlay, destination: Path) -> Path:
    """Write the overlay as a patch for the stable-fixed queue.

    The file is the overlay's diff, in canonical form, behind its reason;
    `git apply` and `patch -p1` read it from the PeTTa root, and the reason
    is the commit message.
    """
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / overlay.patch_file.name
    touched = ", ".join(patch.path for patch in overlay.patches)
    output.write_text(
        f"{overlay.reason}\n\n"
        f"Changes {touched}.\n"
        "---\n"
        + format_file_patches(overlay.patches),
        encoding="utf-8",
    )
    return output


# ---------------------------------------------------------------------------
# The base


def git_head(root: Path) -> tuple[str | None, str | None]:
    """(branch, commit) of a checkout, read from its git files without git.

    A snapshot without git metadata gives (None, None); a commit that cannot
    be resolved (for example a reftable store) gives (branch, None).
    """
    dot_git = root / ".git"
    if dot_git.is_dir():
        git_dir = dot_git
    elif dot_git.is_file():
        text = dot_git.read_text(encoding="utf-8").strip()
        if not text.startswith("gitdir:"):
            return None, None
        git_dir = Path(text.split(":", 1)[1].strip())
        if not git_dir.is_absolute():
            git_dir = (root / git_dir).resolve()
    else:
        return None, None
    common_dir = git_dir
    common_file = git_dir / "commondir"
    if common_file.is_file():
        common = Path(common_file.read_text(encoding="utf-8").strip())
        common_dir = common if common.is_absolute() else (git_dir / common).resolve()
    head_file = git_dir / "HEAD"
    if not head_file.is_file():
        return None, None
    head = head_file.read_text(encoding="utf-8").strip()
    if not head.startswith("ref:"):
        return None, head if COMMIT_RE.fullmatch(head) else None
    reference = head[4:].strip()
    branch = reference.removeprefix("refs/heads/")
    for directory in (git_dir, common_dir):
        loose = directory / reference
        if loose.is_file():
            value = loose.read_text(encoding="utf-8").strip()
            return branch, value if COMMIT_RE.fullmatch(value) else None
    packed = common_dir / "packed-refs"
    if packed.is_file():
        for line in packed.read_text(encoding="utf-8").splitlines():
            fields = line.split(" ", 1)
            if len(fields) == 2 and fields[1] == reference and (
                COMMIT_RE.fullmatch(fields[0])
            ):
                return branch, fields[0]
    return branch, None


def parse_base(label: Any) -> tuple[str, str]:
    if not isinstance(label, str) or label.count("@") != 1:
        raise RuntimeError(f"base must be '<name>@<commit>', got {label!r}")
    name, commit = label.split("@")
    if not BASE_NAME_RE.fullmatch(name) or not COMMIT_RE.fullmatch(commit):
        raise RuntimeError(f"base must be '<name>@<commit>', got {label!r}")
    return name, commit


def base_examples(petta_dir: Path) -> list[str]:
    examples_dir = petta_dir / "examples"
    if not examples_dir.is_dir():
        raise RuntimeError(f"not a PeTTa base (no examples/): {petta_dir}")
    return sorted(path.name for path in examples_dir.glob("*.metta"))


def copy_base(root: Path, destination: Path) -> None:
    """Copy the base so no run can write into it."""
    if destination.is_symlink():
        destination.unlink()
    elif destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    for child in sorted(root.iterdir(), key=lambda path: path.name):
        if child.name in BASE_COPY_SKIP:
            continue
        target = destination / child.name
        if child.name in BASE_COPY_LINK or child.is_symlink():
            os.symlink(
                child.resolve(), target, target_is_directory=child.is_dir()
            )
        elif child.is_dir():
            shutil.copytree(
                child, target, symlinks=True,
                ignore=shutil.ignore_patterns("__pycache__"),
                copy_function=shutil.copy,
            )
        else:
            shutil.copy(child, target)


# ---------------------------------------------------------------------------
# Running one example


class Run(NamedTuple):
    exit: int | str
    stdout: str
    stderr: str
    seconds: float
    source_text: str


ABNORMAL_TIMEOUT = "timeout"
ABNORMAL_OUTPUT_LIMIT = "output-limit"
ABNORMAL_FIXTURE = "fixture-failure"
PREFIX_SEEN = "supervised-prefix"


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
    *, directory: Path | None = None,
):
    with tempfile.TemporaryDirectory(
        prefix="cetta-petta-git-fixture-", dir=directory,
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


@contextmanager
def example_workspace(
    root: Path,
    name: str,
    overlay: Overlay | None,
    fixture: dict[str, Any] | None,
    directory: Path | None,
) -> Iterator[Path]:
    """A private copy of the base for one example, with its overlay applied."""
    if directory is not None:
        directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=f"petta-corpus-{Path(name).stem}-", dir=directory,
    ) as temporary:
        workspace = Path(temporary) / "base"
        copy_base(root, workspace)
        if overlay is not None:
            write_patched(workspace, overlay.patches)
        for package in (fixture or {}).get("packages", ()):
            shutil.copytree(
                REPO_ROOT / package, workspace / package,
                copy_function=shutil.copy,
            )
        yield workspace


def golden_file(golden_dir: Path, name: str) -> Path:
    return golden_dir / f"{Path(name).stem}.stdout"


def run_example(
    engine: str,
    executable: Path | None,
    root: Path,
    name: str,
    timeout_seconds: float,
    *,
    fixture: dict[str, Any] | None,
    overlay: Overlay | None,
    scratch: Path | None,
    clone_root: Path | None = None,
    expected_prefix: str | None = None,
    environment: dict[str, str] | None = None,
    arguments: tuple[str, ...] = (),
    process_metrics: dict[str, int] | None = None,
) -> Run:
    """Run one example of the base on CeTTa ("cetta") or SWI-PeTTa ("swi").

    Examples without an overlay or a workspace fixture run in ``root``; the
    others run in a private copy of it.  stdout and stderr come back
    normalized.
    """
    if environment is None:
        environment = fixture_environment(REPO_ROOT, fixture)
    environment = dict(environment)
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    if scratch is not None:
        temporary = scratch / "tmp"
        temporary.mkdir(parents=True, exist_ok=True)
        environment["TMPDIR"] = str(temporary)
    kind = (fixture or {}).get("kind")
    stdin_text = (fixture or {}).get("stdin") if fixture else None
    started = time.monotonic()

    def command_for(workspace: Path, program: str) -> list[str]:
        if engine == "cetta":
            assert executable is not None
            return [str(executable), *arguments, "--lang", "petta", program]
        if engine == "swi":
            run_sh = workspace / "run.sh"
            if not run_sh.is_file():
                run_sh = (clone_root or root) / "run.sh"
            return ["sh", str(run_sh), program, "--silent"]
        raise ValueError(f"unknown engine {engine!r}")

    def finish(
        workspace: Path, exit_code: int | str, stdout: str, stderr: str,
        source_text: str,
    ) -> Run:
        roots = tuple(
            path for path in (
                workspace, clone_root, executable.parent if executable else None,
            ) if path is not None
        )
        normalize = (
            normalize_cetta_stdout if engine == "cetta"
            else normalize_oracle_stdout
        )
        return Run(
            exit_code,
            normalize(stdout, root, roots),
            normalize_oracle_stderr(stderr, root, roots),
            time.monotonic() - started,
            source_text,
        )

    def execute(workspace: Path, program: str, source: Path) -> Run:
        source_text = source.read_text(encoding="utf-8", errors="replace")
        command = command_for(workspace, program)
        if fixture and fixture.get("mode") == "prefix":
            if expected_prefix is None:
                raise RuntimeError(f"{name}: no expected prefix to observe")
            roots = tuple(
                path for path in (workspace, clone_root,
                                  executable.parent if executable else None)
                if path is not None
            )
            normalize = (
                normalize_cetta_stdout if engine == "cetta"
                else normalize_oracle_stdout
            )
            try:
                exit_code, stdout, stderr = run_supervised_prefix(
                    command, workspace, environment, stdin_text or "",
                    expected_prefix, timeout_seconds,
                    lambda text: normalize(text, root, roots),
                    lambda text: normalize_oracle_stderr(text, root, roots),
                )
            except RuntimeError as error:
                return Run(
                    ABNORMAL_FIXTURE, "", f"{error}\n",
                    time.monotonic() - started, source_text,
                )
            return Run(
                exit_code, stdout, stderr, time.monotonic() - started,
                source_text,
            )
        process_started = time.monotonic_ns()
        try:
            exit_code, stdout, stderr, timed_out, limited = run_bounded_process(
                command, workspace, environment, stdin_text, timeout_seconds,
            )
        finally:
            if process_metrics is not None:
                process_metrics["process_elapsed_ns"] = time.monotonic_ns() - process_started
        if timed_out:
            exit_code = ABNORMAL_TIMEOUT
        elif limited:
            exit_code = ABNORMAL_OUTPUT_LIMIT
        return finish(workspace, exit_code, stdout, stderr, source_text)

    try:
        if kind == "local-git":
            with example_workspace(
                root, name, overlay, None, scratch
            ) as workspace, local_git_fixture_workspace(
                REPO_ROOT, clone_root or root,
                workspace / "examples" / name, fixture, directory=scratch,
            ) as (git_workspace, program):
                return execute(git_workspace, str(program), program)
        if overlay is not None or kind == "local-package":
            with example_workspace(
                root, name, overlay, fixture, scratch
            ) as workspace:
                return execute(
                    workspace, f"examples/{name}",
                    workspace / "examples" / name,
                )
        return execute(root, f"examples/{name}", root / "examples" / name)
    except (RuntimeError, OSError) as error:
        return Run(
            ABNORMAL_FIXTURE, "", f"{error}\n", time.monotonic() - started,
            "",
        )


def active_overlay(root: Path, name: str) -> Overlay | None:
    """The overlay to apply for this example against this base, if any."""
    overlays, _ = load_overlays(DEFAULT_OVERLAY_DIR)
    overlay = overlays.get(name)
    if overlay is None:
        return None
    state, detail = overlay_state(root, overlay.patches)
    if state == OVERLAY_APPLIES:
        return overlay
    if state == OVERLAY_IN_BASE:
        return None
    raise RuntimeError(f"overlay {overlay.file_name} does not apply: {detail}")


def legacy_prefix(fixture: dict[str, Any] | None, name: str) -> str | None:
    if fixture and fixture.get("mode") == "prefix":
        golden = golden_file(DEFAULT_GOLDEN_DIR, name)
        return golden.read_text(encoding="utf-8") if golden.is_file() else None
    return None


def run_cetta(
    cetta: Path,
    petta_dir: Path,
    source: Path,
    timeout_seconds: float,
    fixture: dict[str, Any] | None = None,
    search_controller: str | None = None,
    machine_stats: bool = False,
) -> tuple[int | str, str, str]:
    """One example on CeTTa as the corpus runs it: (exit, stdout, stderr).

    A timeout is exit 124; "output-limit" and "controlled-failure" name the
    other abnormal ends; a live prefix session ends "supervised-prefix".
    """
    environment = fixture_environment(REPO_ROOT, fixture)
    if search_controller is not None:
        environment["CETTA_SEARCH_CONTROLLER"] = search_controller
    if machine_stats:
        environment["CETTA_PETTA_MACHINE_STATS"] = "1"
    try:
        overlay = active_overlay(petta_dir, source.name)
    except RuntimeError as error:
        return "controlled-failure", "", f"{error}\n"
    run = run_example(
        "cetta", cetta, petta_dir, source.name, timeout_seconds,
        fixture=fixture, overlay=overlay, scratch=None,
        expected_prefix=legacy_prefix(fixture, source.name),
        environment=environment,
    )
    legacy_exit = {
        ABNORMAL_TIMEOUT: 124,
        ABNORMAL_FIXTURE: "controlled-failure",
    }.get(run.exit, run.exit) if isinstance(run.exit, str) else run.exit
    return legacy_exit, run.stdout, run.stderr


def run_oracle(
    repo_root: Path,
    petta_dir: Path,
    source: Path,
    timeout_seconds: float,
    fixture: dict[str, Any] | None = None,
) -> tuple[int | str, str, str]:
    """One example on SWI-PeTTa, for reports and benchmarks, never an oracle."""
    del repo_root
    overlay = active_overlay(petta_dir, source.name)
    run = run_example(
        "swi", None, petta_dir, source.name, timeout_seconds,
        fixture=fixture, overlay=overlay, scratch=None,
        expected_prefix=legacy_prefix(fixture, source.name),
    )
    if run.exit in (ABNORMAL_TIMEOUT, ABNORMAL_OUTPUT_LIMIT, ABNORMAL_FIXTURE):
        raise RuntimeError(
            f"SWI-PeTTa run of {source.name} ended {run.exit}: "
            f"{run.stderr[:1000]}"
        )
    return run.exit, run.stdout, run.stderr


# ---------------------------------------------------------------------------
# Oracles


class AssertionSites(NamedTuple):
    top_level_tests: int
    tests: int
    asserts: int

    @property
    def self_checking(self) -> bool:
        return self.tests + self.asserts > 0

    @property
    def required_verdicts(self) -> int:
        """Verdict lines a passing run must print: one per top-level test."""
        if self.top_level_tests:
            return self.top_level_tests
        return 1 if self.tests else 0


def assertion_sites(text: str) -> AssertionSites:
    """Count assertion forms in MeTTa source, outside comments and strings."""
    top_level_tests = tests = asserts = 0
    depth = 0
    index = 0
    length = len(text)
    while index < length:
        character = text[index]
        if character == ";":
            newline = text.find("\n", index)
            index = length if newline < 0 else newline + 1
            continue
        if character == '"':
            index += 1
            while index < length:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == '"':
                    index += 1
                    break
                index += 1
            continue
        if character == "(":
            directive = depth == 0 and index > 0 and text[index - 1] == "!"
            depth += 1
            end = index + 1
            while end < length and text[end] not in TOKEN_TERMINATORS:
                end += 1
            head = text[index + 1:end]
            if head == TEST_HEAD:
                tests += 1
                if directive:
                    top_level_tests += 1
            elif head in ASSERT_HEADS:
                asserts += 1
        elif character == ")":
            depth = max(depth - 1, 0)
        index += 1
    return AssertionSites(top_level_tests, tests, asserts)


def assertion_verdicts(stdout: str) -> tuple[int, list[str]]:
    """Passing verdict lines, and failing verdict or assertion lines."""
    passed = 0
    failures: list[str] = []
    for line in stdout.split("\n"):
        if line.startswith("is ") and line.endswith(PASSED_SUFFIX):
            passed += 1
        elif line.startswith("is ") and line.endswith(FAILED_SUFFIX):
            failures.append(line)
        elif line.startswith(ASSERTION_FAILED_PREFIX):
            failures.append(line)
    return passed, failures


def first_line(text: str) -> str:
    for line in text.split("\n"):
        if line.strip():
            return line.strip()
    return ""


def abnormal_detail(run: Run, timeout_seconds: float) -> str:
    if run.exit == ABNORMAL_TIMEOUT:
        return f"timed out after {timeout_seconds:g}s"
    if run.exit == ABNORMAL_OUTPUT_LIMIT:
        return f"exceeded the {MAX_CAPTURE_BYTES}-byte output limit"
    if run.exit == ABNORMAL_FIXTURE:
        return f"fixture failed: {first_line(run.stderr)}"
    detail = f"exit {run.exit}"
    stderr = first_line(run.stderr)
    return f"{detail}: {stderr}" if stderr else detail


def self_check_outcome(
    run: Run, timeout_seconds: float
) -> tuple[bool, str]:
    """Whether every assertion the example makes passed, and why not."""
    sites = assertion_sites(run.source_text)
    passed, failures = assertion_verdicts(run.stdout)
    if failures:
        return False, failures[0]
    if not isinstance(run.exit, int) or run.exit != 0:
        return False, abnormal_detail(run, timeout_seconds)
    if passed < sites.required_verdicts:
        return False, (
            f"{sites.required_verdicts} top-level test(s) expected a verdict, "
            f"{passed} printed"
        )
    return True, f"{passed} verdict(s) passed"


def first_difference(expected: str, actual: str) -> str:
    expected_lines = expected.split("\n")
    actual_lines = actual.split("\n")
    for number, (left, right) in enumerate(
        zip(expected_lines, actual_lines), start=1
    ):
        if left != right:
            return f"line {number}: expected {left[:120]!r}, got {right[:120]!r}"
    number = min(len(expected_lines), len(actual_lines)) + 1
    return (
        f"line {number}: expected {len(expected_lines)} line(s), "
        f"got {len(actual_lines)}"
    )


class Verdict(NamedTuple):
    status: str  # PASS, XFAIL (a recorded failing self-check), or FAIL
    detail: str

    @property
    def matches(self) -> bool:
        return self.status in {"PASS", "XFAIL"}


def judge(
    entry: dict[str, Any], run: Run, golden: str | None, timeout_seconds: float
) -> Verdict:
    mode = entry["mode"]
    if mode == MODE_SELF_CHECK:
        passed, detail = self_check_outcome(run, timeout_seconds)
        recorded = entry.get("failing")
        if recorded is None:
            return Verdict("PASS", detail) if passed else Verdict("FAIL", detail)
        if passed:
            return Verdict(
                "FAIL",
                "now passes its own assertions (recorded failing: "
                f"{recorded}); run make petta-corpus-accept",
            )
        if not isinstance(run.exit, int) or run.exit < 0:
            return Verdict("FAIL", abnormal_detail(run, timeout_seconds))
        if detail != recorded:
            return Verdict("FAIL", f"recorded failure changed: {detail!r}; expected {recorded!r}")
        _, assertion_failures = assertion_verdicts(run.stdout)
        if assertion_failures and run.exit != 1:
            return Verdict("FAIL", f"assertion failure terminated with exit {run.exit}, expected 1")
        return Verdict("XFAIL", detail)
    if mode == MODE_NEGATIVE:
        expected_exit = entry["exit"]
        if run.exit != expected_exit:
            return Verdict(
                "FAIL",
                f"expected exit {expected_exit}, got "
                + abnormal_detail(run, timeout_seconds),
            )
        if entry["failure"] not in run.stdout.split("\n"):
            return Verdict(
                "FAIL", f"did not print its failure line {entry['failure']!r}"
            )
        return Verdict("PASS", f"failed as recorded: {entry['failure']}")
    if golden is None:
        return Verdict("FAIL", "golden file missing; run make petta-corpus-accept")
    if run.exit == PREFIX_SEEN:
        return Verdict("PASS", "printed its golden prefix while live")
    expected_exit = entry.get("exit", 0)
    if run.exit != expected_exit:
        return Verdict(
            "FAIL",
            f"expected exit {expected_exit}, got "
            + abnormal_detail(run, timeout_seconds),
        )
    contract = entry.get("observation", STDOUT_EXACT_STREAM)
    actual = semantic_stdout(run.stdout)
    if not stdout_observation_equal(actual, golden, contract):
        return Verdict("FAIL", "stdout differs from golden, " + first_difference(
            semantic_stdout(golden), actual
        ))
    return Verdict("PASS", "matches golden")


# ---------------------------------------------------------------------------
# The manifest


def load_manifest(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("PeTTa corpus manifest root must be an object")
    return value


def ordered_entry(entry: dict[str, Any]) -> dict[str, Any]:
    return {key: entry[key] for key in ENTRY_KEYS if key in entry}


def dump_manifest(manifest: dict[str, Any]) -> str:
    """One entry per line, so a change reads as a one-line diff."""
    entries = manifest["entries"]
    lines = [
        "{",
        f'  "schema": {json.dumps(manifest["schema"])},',
        f'  "base": {json.dumps(manifest["base"])},',
        '  "entries": [',
    ]
    for index, entry in enumerate(entries):
        comma = "," if index + 1 < len(entries) else ""
        lines.append(
            "    " + json.dumps(ordered_entry(entry), ensure_ascii=False) + comma
        )
    lines += ["  ]", "}"]
    return "\n".join(lines) + "\n"


def expected_fixture_name(name: str) -> str | None:
    fixture = CONTROLLED_CASES.get(name)
    return fixture["name"] if fixture else None


def expected_requirements(name: str) -> list[str] | None:
    requirements = CASE_CAPABILITY_REQUIREMENTS.get(name)
    return sorted(requirements) if requirements else None


def entry_shape_problems(entry: Any) -> list[str]:
    if not isinstance(entry, dict):
        return ["entry must be an object"]
    name = entry.get("name")
    label = name if isinstance(name, str) else repr(name)
    problems = []
    unknown = sorted(set(entry) - set(ENTRY_KEYS))
    if unknown:
        problems.append(f"{label}: unknown field(s) {unknown}")
    if not isinstance(name, str) or not name.endswith(".metta") or "/" in name:
        problems.append(f"{label}: name must be an example file name")
        return problems
    mode = entry.get("mode")
    if mode not in MODES:
        problems.append(f"{name}: mode must be one of {list(MODES)}")
    timeout = entry.get("timeout")
    if (
        isinstance(timeout, bool)
        or not isinstance(timeout, (int, float))
        or not math.isfinite(timeout)
        or timeout <= 0
    ):
        problems.append(f"{name}: timeout must be a positive number of seconds")
    overlay = entry.get("overlay")
    if overlay is not None and overlay != f"{Path(name).stem}.patch":
        problems.append(
            f"{name}: overlay must be {Path(name).stem + '.patch'!r}"
        )
    if entry.get("fixture") != expected_fixture_name(name):
        problems.append(
            f"{name}: fixture must be {expected_fixture_name(name)!r} "
            f"(the fixtures defined for the corpus)"
        )
    if entry.get("requires") != expected_requirements(name):
        problems.append(
            f"{name}: requires must be {expected_requirements(name)!r}"
        )
    failing = entry.get("failing")
    if failing is not None and (
        mode != MODE_SELF_CHECK or not isinstance(failing, str) or not failing
    ):
        problems.append(f"{name}: failing is a non-empty line of a self-check")
    if mode == MODE_NEGATIVE:
        if (
            isinstance(entry.get("exit"), bool)
            or not isinstance(entry.get("exit"), int)
            or entry.get("exit") == 0
        ):
            problems.append(f"{name}: a negative case records its nonzero exit")
        if not isinstance(entry.get("failure"), str) or not entry.get("failure"):
            problems.append(f"{name}: a negative case records its failure line")
    else:
        if "failure" in entry:
            problems.append(f"{name}: failure belongs to a negative case")
        if "exit" in entry and (
            mode != MODE_GOLDEN
            or isinstance(entry["exit"], bool)
            or not isinstance(entry["exit"], int)
            or entry["exit"] == 0
        ):
            problems.append(
                f"{name}: exit is recorded only when a golden exits nonzero"
            )
    observation = entry.get("observation")
    if observation is not None and (
        mode != MODE_GOLDEN
        or observation not in STDOUT_OBSERVATION_CONTRACTS
        or observation == STDOUT_EXACT_STREAM
    ):
        problems.append(
            f"{name}: observation may only select {STDOUT_OCCURRENCE_BAG!r} "
            "for a golden"
        )
    return problems


class CorpusCheck(NamedTuple):
    manifest: dict[str, Any]
    overlays: dict[str, Overlay]
    problems: list[str]            # corpus-wide: no example can explain them
    entry_problems: dict[str, str]  # per example: it cannot be judged
    notices: list[str]


def check_corpus(
    petta_dir: Path,
    manifest_path: Path,
    overlay_dir: Path,
    golden_dir: Path,
    require_complete: bool,
) -> CorpusCheck:
    """Everything that can be checked without running an example."""
    manifest = load_manifest(manifest_path)
    if manifest.get("schema") != SCHEMA:
        raise RuntimeError(
            f"unsupported PeTTa corpus schema {manifest.get('schema')!r}"
        )
    unknown = sorted(set(manifest) - {"schema", "base", "entries"})
    if unknown:
        raise RuntimeError(f"unknown manifest field(s) {unknown}")
    base_name, base_commit = parse_base(manifest.get("base"))
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        raise RuntimeError("PeTTa corpus manifest entries must be a list")

    problems: list[str] = []
    entry_problems: dict[str, str] = {}
    notices: list[str] = []
    for entry in entries:
        problems.extend(entry_shape_problems(entry))
    if problems:
        raise RuntimeError("malformed manifest:\n  " + "\n  ".join(problems))
    names = [entry["name"] for entry in entries]
    if names != sorted(names):
        raise RuntimeError("PeTTa corpus manifest entries are not name-sorted")
    if len(set(names)) != len(names):
        raise RuntimeError("PeTTa corpus manifest contains duplicate names")

    branch, commit = git_head(petta_dir)
    if commit is not None and commit != base_commit:
        notices.append(
            f"the base checkout is at {commit[:12]}"
            + (f" ({branch})" if branch else "")
            + f"; the corpus was synced at {base_name}@{base_commit[:12]}"
            " (make petta-corpus-sync moves it)"
        )
    disk = set(base_examples(petta_dir))
    listed = set(names)
    for name in sorted(disk - listed):
        problems.append(
            f"{name}: in the base but not in the corpus "
            "(make petta-corpus-sync classifies it)"
        )
    for name in sorted(listed - disk):
        entry_problems[name] = (
            "in the corpus but not in the base "
            "(make petta-corpus-sync drops it)"
        )

    overlays, overlay_problems = load_overlays(overlay_dir)
    problems.extend(overlay_problems)
    referenced = {
        entry["name"] for entry in entries if entry.get("overlay")
    }
    for example in sorted(set(overlays) - referenced):
        problems.append(
            f"overlay {overlays[example].file_name} is not named by an entry"
        )
    for entry in entries:
        name = entry["name"]
        if not entry.get("overlay"):
            continue
        overlay = overlays.get(name)
        if overlay is None:
            entry_problems[name] = f"overlay {entry['overlay']} is missing"
            continue
        if name not in disk:
            continue
        state, detail = overlay_state(petta_dir, overlay.patches)
        if state == OVERLAY_IN_BASE:
            entry_problems[name] = (
                f"overlay {overlay.file_name} is already in the base "
                "(make petta-corpus-sync retires it)"
            )
        elif state == OVERLAY_STALE:
            entry_problems[name] = (
                f"overlay {overlay.file_name} no longer applies: {detail}"
            )

    golden_names = {
        entry["name"] for entry in entries if entry["mode"] == MODE_GOLDEN
    }
    if golden_dir.is_dir():
        for path in sorted(golden_dir.iterdir()):
            if path.suffix != ".stdout" or f"{path.stem}.metta" not in golden_names:
                problems.append(
                    f"golden {path.name} belongs to no golden entry"
                )
    for name in sorted(golden_names):
        if not golden_file(golden_dir, name).is_file():
            message = "golden file missing (make petta-corpus-accept)"
            if require_complete:
                entry_problems.setdefault(name, message)
            else:
                notices.append(f"{name}: {message}")
    return CorpusCheck(manifest, overlays, problems, entry_problems, notices)


def verify_manifest(
    petta_dir: Path,
    manifest_path: Path,
    require_complete: bool,
    overlay_dir: Path = DEFAULT_OVERLAY_DIR,
    golden_dir: Path = DEFAULT_GOLDEN_DIR,
) -> CorpusCheck:
    """Static check of the corpus against the base; raises on any problem."""
    check = check_corpus(
        petta_dir, manifest_path, overlay_dir, golden_dir, require_complete
    )
    for notice in check.notices:
        print(f"NOTE: {notice}")
    failures = check.problems + [
        f"{name}: {problem}" for name, problem in sorted(
            check.entry_problems.items()
        )
    ]
    if failures:
        raise RuntimeError(
            "PeTTa corpus is inconsistent:\n  " + "\n  ".join(failures)
        )
    entries = check.manifest["entries"]
    counts = Counter(entry["mode"] for entry in entries)
    print(
        f"PASS: PeTTa corpus manifest is consistent with its base "
        f"({len(entries)} examples: "
        + ", ".join(f"{counts[mode]} {mode}" for mode in MODES)
        + f"; {len(check.overlays)} overlay(s))"
    )
    return check


def select_entries(
    entries: list[dict[str, Any]],
    only: set[str],
    excluded_capabilities: set[str],
    limit: int | None,
) -> tuple[list[dict[str, Any]], list[str]]:
    unknown_examples = sorted(
        only - {entry["name"] for entry in entries}
    )
    if unknown_examples:
        raise RuntimeError(
            "requested examples are absent from the manifest: "
            + ", ".join(unknown_examples)
        )
    unknown_capabilities = sorted(
        excluded_capabilities - KNOWN_OPTIONAL_CAPABILITIES
    )
    if unknown_capabilities:
        raise RuntimeError(
            "unknown optional capabilities: "
            + ", ".join(unknown_capabilities)
        )
    selected = [
        entry for entry in entries if not only or entry["name"] in only
    ]
    excluded_names = [
        entry["name"]
        for entry in selected
        if excluded_capabilities.intersection(entry.get("requires", ()))
    ]
    excluded = set(excluded_names)
    selected = [entry for entry in selected if entry["name"] not in excluded]
    if limit is not None:
        selected = selected[:limit]
    return selected, excluded_names


def binary_record(cetta: Path) -> dict[str, Any]:
    status = cetta.stat()
    return {
        "path": str(cetta),
        "sha256": sha256_file(cetta),
        "mtime": time.strftime(
            "%Y-%m-%dT%H:%M:%S%z", time.localtime(status.st_mtime)
        ),
    }


def write_run_outputs(out_dir: Path, name: str, run: Run) -> None:
    actual = out_dir / "actual"
    actual.mkdir(parents=True, exist_ok=True)
    (actual / f"{name}.stdout").write_text(run.stdout, encoding="utf-8")
    (actual / f"{name}.stderr").write_text(run.stderr, encoding="utf-8")


def compare_manifest(
    petta_dir: Path,
    manifest_path: Path,
    cetta: Path,
    out_dir: Path,
    only: set[str],
    excluded_capabilities: set[str],
    limit: int | None,
    timeout_override: float | None,
    require_complete: bool,
    require_match: bool,
    overlay_dir: Path = DEFAULT_OVERLAY_DIR,
    golden_dir: Path = DEFAULT_GOLDEN_DIR,
) -> dict[str, Any]:
    """Run the corpus on CeTTa and judge every example by its own oracle."""
    if not cetta.is_file():
        raise RuntimeError(f"CeTTa binary does not exist: {cetta}")
    check = check_corpus(
        petta_dir, manifest_path, overlay_dir, golden_dir, require_complete
    )
    for notice in check.notices:
        print(f"NOTE: {notice}")
    manifest = check.manifest
    entries, excluded_names = select_entries(
        manifest["entries"], only, excluded_capabilities, limit
    )
    binary = binary_record(cetta)
    out_dir.mkdir(parents=True, exist_ok=True)
    scratch = out_dir / "work"
    base_copy = scratch / "base"
    copy_base(petta_dir, base_copy)
    rows = ["example\tmode\tstatus\texit\tseconds\tdetail"]
    counts: Counter[str] = Counter()
    failures: list[str] = []
    for problem in check.problems:
        counts["FAIL"] += 1
        failures.append(f"corpus: {problem}")
        rows.append(f"(corpus)\t-\tFAIL\t-\t-\t{problem}")
        print(f"FAIL (corpus): {problem}")
    for index, entry in enumerate(entries, start=1):
        name = entry["name"]
        timeout = (
            timeout_override if timeout_override is not None
            else float(entry["timeout"])
        )
        problem = check.entry_problems.get(name)
        if problem is not None:
            verdict, run = Verdict("FAIL", problem), None
        else:
            golden_path = golden_file(golden_dir, name)
            golden = (
                golden_path.read_text(encoding="utf-8")
                if golden_path.is_file() else None
            )
            fixture = CONTROLLED_CASES.get(name)
            run = run_example(
                "cetta", cetta, base_copy, name, timeout,
                fixture=fixture,
                overlay=check.overlays.get(name) if entry.get("overlay") else None,
                scratch=scratch, clone_root=petta_dir,
                expected_prefix=golden,
            )
            write_run_outputs(out_dir, name, run)
            verdict = judge(entry, run, golden, timeout)
        counts[verdict.status] += 1
        if not verdict.matches or (require_match and verdict.status == "XFAIL"):
            failures.append(f"{name}: {verdict.detail}")
        exit_text = "-" if run is None else str(run.exit)
        seconds = "-" if run is None else f"{run.seconds:.3f}"
        rows.append(
            f"{name}\t{entry['mode']}\t{verdict.status}\t{exit_text}\t"
            f"{seconds}\t{verdict.detail}"
        )
        shown = "" if verdict.status == "PASS" else f" — {verdict.detail}"
        print(
            f"[{index:03d}/{len(entries):03d}] {name}: {verdict.status}"
            f"{shown}",
            flush=True,
        )
    (out_dir / "results.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    summary = {
        "base": manifest["base"],
        "base_checkout": dict(zip(("branch", "commit"), git_head(petta_dir))),
        "cetta": binary,
        "counts": dict(sorted(counts.items())),
        "failures": failures,
        "excluded_capabilities": sorted(excluded_capabilities),
        "excluded_examples": excluded_names,
        "selected": len(entries),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    for name in excluded_names:
        print(f"SKIP: {name} (requires {', '.join(CASE_CAPABILITY_REQUIREMENTS[name])})")
    print(json.dumps(summary["counts"], sort_keys=True))
    if failures:
        print(f"{len(failures)} example(s) do not meet their oracle:")
        for failure in failures:
            print(f"  {failure}")
        if require_match:
            raise RuntimeError(
                f"PeTTa corpus: {len(failures)} example(s) do not meet their "
                "oracle"
            )
    else:
        print(
            f"PASS: {len(entries)} PeTTa corpus example(s) meet their "
            f"oracles on CeTTa ({counts['PASS']} pass, "
            f"{counts['XFAIL']} recorded failing self-check(s))"
        )
    return summary


# ---------------------------------------------------------------------------
# accept and sync: refresh the classification and goldens, show the diff


def unified_text_diff(old: str, new: str, label: str) -> str:
    return "".join(difflib.unified_diff(
        old.splitlines(keepends=True), new.splitlines(keepends=True),
        f"a/{label}", f"b/{label}",
    ))


def refresh_corpus(
    *,
    petta_dir: Path,
    manifest_path: Path,
    cetta: Path,
    out_dir: Path,
    timeout_override: float | None,
    excluded_capabilities: set[str],
    sync: bool,
    base_name: str = DEFAULT_BASE,
    base_commit: str | None = None,
    overlay_dir: Path = DEFAULT_OVERLAY_DIR,
    golden_dir: Path = DEFAULT_GOLDEN_DIR,
) -> int:
    """Run every example on CeTTa and rewrite the manifest and goldens.

    accept (sync=False) keeps the recorded base and refuses a moved one; sync
    records the base it is given, retires overlays the base already contains
    and names those that no longer apply.  Returns the exit status.
    """
    if not cetta.is_file():
        raise RuntimeError(f"CeTTa binary does not exist: {cetta}")
    unknown_capabilities = sorted(
        excluded_capabilities - KNOWN_OPTIONAL_CAPABILITIES
    )
    if unknown_capabilities:
        raise RuntimeError(
            "unknown optional capabilities: " + ", ".join(unknown_capabilities)
        )
    old_text = manifest_path.read_text(encoding="utf-8") if (
        manifest_path.is_file()
    ) else ""
    old_manifest = json.loads(old_text) if old_text else None
    old_entries: dict[str, dict[str, Any]] = {}
    if old_manifest is not None and old_manifest.get("schema") == SCHEMA:
        old_entries = {
            entry["name"]: entry for entry in old_manifest.get("entries", [])
        }

    branch, checkout_commit = git_head(petta_dir)
    commit = base_commit or checkout_commit
    if sync:
        if not BASE_NAME_RE.fullmatch(base_name):
            raise RuntimeError(f"base name must be a plain word: {base_name!r}")
        if commit is None:
            raise RuntimeError(
                "cannot read the base commit from the checkout; pass it "
                "(PETTA_CORPUS_BASE_COMMIT=<sha>)"
            )
        if not COMMIT_RE.fullmatch(commit):
            raise RuntimeError(f"base commit must be a full sha: {commit!r}")
        if (
            branch in KNOWN_BASES and branch != base_name
        ):
            raise RuntimeError(
                f"the base checkout is on branch {branch!r} but the selected "
                f"base is {base_name!r}; select PETTA_CORPUS_BASE={branch} or "
                f"point PETTA_CORPUS_ROOT at a checkout of {base_name}"
            )
        base_label = f"{base_name}@{commit}"
    else:
        if old_manifest is None or old_manifest.get("schema") != SCHEMA:
            raise RuntimeError(
                "no corpus manifest to accept into; run make petta-corpus-sync"
            )
        recorded_name, recorded_commit = parse_base(old_manifest.get("base"))
        if commit is not None and commit != recorded_commit:
            raise RuntimeError(
                f"the base checkout is at {commit[:12]}, the corpus at "
                f"{recorded_name}@{recorded_commit[:12]}: run "
                "make petta-corpus-sync to move the corpus"
            )
        base_label = old_manifest["base"]

    examples = base_examples(petta_dir)
    overlays, overlay_problems = load_overlays(overlay_dir)
    if overlay_problems:
        raise RuntimeError(
            "overlay directory is inconsistent:\n  "
            + "\n  ".join(overlay_problems)
        )
    report: list[str] = []
    stale: list[str] = []
    active: dict[str, Overlay] = {}
    for example, overlay in sorted(overlays.items()):
        if example not in examples:
            state, detail = OVERLAY_STALE, "its example is not in the base"
        else:
            state, detail = overlay_state(petta_dir, overlay.patches)
        if state == OVERLAY_APPLIES:
            active[example] = overlay
        elif state == OVERLAY_IN_BASE and sync:
            overlay.patch_file.unlink()
            overlay.why_file.unlink()
            report.append(f"retired: {example} (now in base)")
        elif state == OVERLAY_IN_BASE:
            stale.append(
                f"{overlay.file_name}: already in the base "
                "(make petta-corpus-sync retires it)"
            )
        else:
            stale.append(f"{overlay.file_name}: no longer applies: {detail}")
    if stale and not sync:
        raise RuntimeError(
            "overlays do not match the base:\n  " + "\n  ".join(stale)
        )

    out_dir.mkdir(parents=True, exist_ok=True)
    scratch = out_dir / "work"
    base_copy = scratch / "base"
    copy_base(petta_dir, base_copy)
    golden_dir.mkdir(parents=True, exist_ok=True)

    entries: list[dict[str, Any]] = []
    goldens: dict[str, str] = {}
    unresolved: list[str] = []
    changes: list[str] = []
    for index, name in enumerate(examples, start=1):
        old = old_entries.get(name, {})
        entry: dict[str, Any] = {"name": name}
        timeout = old.get("timeout", DEFAULT_TIMEOUT_SECONDS)
        fixture = CONTROLLED_CASES.get(name)
        requires = expected_requirements(name)
        overlay = active.get(name)
        golden_path = golden_file(golden_dir, name)
        prior_golden = (
            golden_path.read_text(encoding="utf-8")
            if golden_path.is_file() else None
        )
        run_timeout = (
            timeout_override if timeout_override is not None else float(timeout)
        )
        skipped_for = sorted(excluded_capabilities.intersection(requires or ()))
        stale_overlay = name in overlays and overlay is None and any(
            item.startswith(overlays[name].file_name) for item in stale
        )
        run = None
        if not skipped_for and not stale_overlay:
            run = run_example(
                "cetta", cetta, base_copy, name, run_timeout,
                fixture=fixture, overlay=overlay, scratch=scratch,
                clone_root=petta_dir,
                expected_prefix=prior_golden,
            )
            write_run_outputs(out_dir, name, run)

        if old.get("mode") == MODE_NEGATIVE:
            entry.update({
                key: old[key] for key in ("mode", "exit", "failure") if key in old
            })
            if run is not None:
                verdict = judge(entry | {"timeout": timeout}, run, None, run_timeout)
                if not verdict.matches:
                    unresolved.append(f"{name}: negative case: {verdict.detail}")
        else:
            if run is not None:
                source_text = run.source_text
            else:
                source = (
                    petta_dir / "examples" / name
                )
                source_text = source.read_text(encoding="utf-8", errors="replace")
            sites = assertion_sites(source_text)
            if sites.self_checking:
                entry["mode"] = MODE_SELF_CHECK
                if run is None:
                    if old.get("mode") == MODE_SELF_CHECK and "failing" in old:
                        entry["failing"] = old["failing"]
                else:
                    passed, detail = self_check_outcome(run, run_timeout)
                    if not passed:
                        entry["failing"] = detail
            else:
                entry["mode"] = MODE_GOLDEN
                if old.get("observation"):
                    entry["observation"] = old["observation"]
                if fixture and fixture.get("mode") == "prefix":
                    # The expected prefix is authored, not observed.
                    if prior_golden is None:
                        unresolved.append(
                            f"{name}: write the prefix it must print to "
                            f"{golden_path.name}"
                        )
                    else:
                        goldens[name] = prior_golden
                        if run is not None and run.exit != PREFIX_SEEN:
                            unresolved.append(
                                f"{name}: did not print its golden prefix "
                                f"while live: {first_line(run.stderr)}"
                            )
                elif run is None:
                    if prior_golden is not None:
                        goldens[name] = prior_golden
                        if "exit" in old:
                            entry["exit"] = old["exit"]
                elif isinstance(run.exit, int):
                    goldens[name] = semantic_stdout(run.stdout)
                    if run.exit != 0:
                        entry["exit"] = run.exit
                else:
                    unresolved.append(
                        f"{name}: cannot record a golden: "
                        + abnormal_detail(run, run_timeout)
                    )
                    if prior_golden is not None:
                        goldens[name] = prior_golden
                        if "exit" in old:
                            entry["exit"] = old["exit"]
        if name in overlays and (overlay is not None or stale_overlay):
            entry["overlay"] = overlays[name].file_name
        entry["timeout"] = timeout
        if fixture:
            entry["fixture"] = fixture["name"]
        if requires:
            entry["requires"] = requires
        entries.append(entry)

        status = "not run (requires " + ", ".join(skipped_for) + ")" if (
            skipped_for
        ) else ("not run (stale overlay)" if stale_overlay else entry["mode"])
        if entry.get("failing"):
            status += f" FAILING: {entry['failing']}"
        print(f"[{index:03d}/{len(examples):03d}] {name}: {status}", flush=True)
        if not old:
            changes.append(f"new: {name} ({entry['mode']})")
        else:
            before = ordered_entry(old)
            after = ordered_entry(entry)
            if before != after:
                changes.append(
                    f"changed: {name}: {json.dumps(before, ensure_ascii=False)}"
                    f" -> {json.dumps(after, ensure_ascii=False)}"
                )
    for name in sorted(set(old_entries) - set(examples)):
        changes.append(f"removed: {name} (no longer in the base)")

    manifest = {"schema": SCHEMA, "base": base_label, "entries": entries}
    new_text = dump_manifest(manifest)
    diffs: list[str] = []
    for name, text in sorted(goldens.items()):
        path = golden_file(golden_dir, name)
        previous = path.read_text(encoding="utf-8") if path.is_file() else ""
        if previous != text or not path.is_file():
            diffs.append(unified_text_diff(
                previous, text, f"tests/petta/golden/{path.name}"
            ) or f"new empty golden tests/petta/golden/{path.name}\n")
            path.write_text(text, encoding="utf-8")
    keep = {golden_file(golden_dir, name).name for name in goldens}
    for path in sorted(golden_dir.iterdir()):
        if path.name not in keep:
            diffs.append(
                f"removed golden tests/petta/golden/{path.name} "
                "(its example is no longer a golden)\n"
            )
            path.unlink()
    manifest_diff = unified_text_diff(
        old_text, new_text, "tests/petta/corpus/manifest.json"
    )
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(new_text, encoding="utf-8")

    print()
    print("== diff for review ==")
    sys.stdout.write(manifest_diff or "(manifest unchanged)\n")
    for diff in diffs:
        sys.stdout.write(diff)
    print("== summary ==")
    counts = Counter(entry["mode"] for entry in entries)
    failing = [entry["name"] for entry in entries if entry.get("failing")]
    print(
        f"base {base_label}; {len(entries)} examples: "
        + ", ".join(f"{counts[mode]} {mode}" for mode in MODES)
        + f"; {len(active)} active overlay(s); "
        f"{len(failing)} failing self-check(s)"
    )
    print(f"CeTTa {binary_record(cetta)['sha256'][:16]}")
    for line in report + changes:
        print(line)
    for name in failing:
        print(f"failing self-check: {name}: {next(e for e in entries if e['name'] == name)['failing']}")
    for problem in stale:
        print(f"STALE OVERLAY: {problem}")
    for problem in unresolved:
        print(f"UNRESOLVED: {problem}")
    return 1 if stale or unresolved else 0


# ---------------------------------------------------------------------------
# The optional SWI-PeTTa report (never a gate)


def swi_report(
    petta_dir: Path,
    manifest_path: Path,
    cetta: Path,
    out_dir: Path,
    only: set[str],
    limit: int | None,
    timeout_override: float | None,
    overlay_dir: Path = DEFAULT_OVERLAY_DIR,
    golden_dir: Path = DEFAULT_GOLDEN_DIR,
) -> None:
    """List where SWI-PeTTa's stdout differs from CeTTa's, for a person.

    Each divergence is an upstream bug, a CeTTa bug or an intended
    difference; this report only finds them.
    """
    check = check_corpus(
        petta_dir, manifest_path, overlay_dir, golden_dir, False
    )
    entries, _ = select_entries(check.manifest["entries"], only, set(), limit)
    out_dir.mkdir(parents=True, exist_ok=True)
    scratch = out_dir / "work"
    base_copy = scratch / "base"
    copy_base(petta_dir, base_copy)
    (out_dir / "diffs").mkdir(exist_ok=True)
    rows = ["example\tmode\tswi_exit\tcetta_exit\tstdout_equal\tfirst_difference"]
    divergent = 0
    for index, entry in enumerate(entries, start=1):
        name = entry["name"]
        timeout = (
            timeout_override if timeout_override is not None
            else float(entry["timeout"])
        )
        golden_path = golden_file(golden_dir, name)
        prefix = (
            golden_path.read_text(encoding="utf-8")
            if golden_path.is_file() else None
        )
        overlay = check.overlays.get(name) if entry.get("overlay") else None
        if name in check.entry_problems:
            rows.append(f"{name}\t{entry['mode']}\t-\t-\t-\t{check.entry_problems[name]}")
            continue
        runs = {
            engine: run_example(
                engine, cetta if engine == "cetta" else None, base_copy, name,
                timeout, fixture=CONTROLLED_CASES.get(name), overlay=overlay,
                scratch=scratch, clone_root=petta_dir, expected_prefix=prefix,
            )
            for engine in ("swi", "cetta")
        }
        swi_stdout = semantic_stdout(runs["swi"].stdout)
        cetta_stdout = semantic_stdout(runs["cetta"].stdout)
        equal = swi_stdout == cetta_stdout and runs["swi"].exit == runs["cetta"].exit
        difference = "" if equal else (
            first_difference(swi_stdout, cetta_stdout)
            if swi_stdout != cetta_stdout
            else f"exit {runs['swi'].exit} vs {runs['cetta'].exit}"
        )
        if not equal:
            divergent += 1
            (out_dir / "diffs" / f"{Path(name).stem}.diff").write_text(
                unified_text_diff(swi_stdout, cetta_stdout, name),
                encoding="utf-8",
            )
        rows.append(
            f"{name}\t{entry['mode']}\t{runs['swi'].exit}\t{runs['cetta'].exit}"
            f"\t{int(equal)}\t{difference}"
        )
        print(
            f"[{index:03d}/{len(entries):03d}] {name}: "
            + ("same" if equal else f"DIVERGES — {difference}"),
            flush=True,
        )
    (out_dir / "report.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(
        f"{divergent} of {len(entries)} example(s) diverge between SWI-PeTTa "
        f"and CeTTa; classify each (upstream bug, CeTTa bug, intended "
        f"difference). Report: {out_dir / 'report.tsv'}"
    )


# ---------------------------------------------------------------------------
# Command line


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    subparsers = parser.add_subparsers(dest="command", required=True)

    def corpus_paths(sub: argparse.ArgumentParser) -> None:
        sub.add_argument("--petta-dir", type=Path, required=True)
        sub.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
        sub.add_argument("--overlay-dir", type=Path, default=DEFAULT_OVERLAY_DIR)
        sub.add_argument("--golden-dir", type=Path, default=DEFAULT_GOLDEN_DIR)

    verify = subparsers.add_parser(
        "verify", help="check the corpus against the base without running it"
    )
    corpus_paths(verify)
    verify.add_argument("--require-complete", action="store_true")

    for command, text in (
        ("compare", "run the corpus on CeTTa and judge every example"),
        ("accept", "refresh classification and goldens on the recorded base"),
        ("sync", "move the corpus to the given base, then refresh"),
        ("swi-report", "optional: list SWI-PeTTa/CeTTa divergences"),
    ):
        sub = subparsers.add_parser(command, help=text)
        corpus_paths(sub)
        sub.add_argument("--cetta", type=Path, required=True)
        sub.add_argument("--out", type=Path, required=True)
        sub.add_argument("--timeout", type=float)
        if command in {"compare", "swi-report"}:
            sub.add_argument("--only", action="append", default=[])
            sub.add_argument("--limit", type=int)
        if command in {"compare", "accept", "sync"}:
            sub.add_argument(
                "--exclude-capability", action="append", default=[]
            )
        if command == "compare":
            sub.add_argument("--require-complete", action="store_true")
            sub.add_argument("--require-match", action="store_true",
                             help="require every selected example to pass")
        if command == "sync":
            sub.add_argument("--base", default=DEFAULT_BASE)
            sub.add_argument("--base-commit")

    export = subparsers.add_parser(
        "export-overlay",
        help="write an overlay as a patch for the stable-fixed queue",
    )
    export.add_argument("--name", required=True)
    export.add_argument("--out", type=Path, required=True)
    export.add_argument("--overlay-dir", type=Path, default=DEFAULT_OVERLAY_DIR)
    export.add_argument("--petta-dir", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "export-overlay":
            overlays, problems = load_overlays(args.overlay_dir.resolve())
            name = args.name if args.name.endswith(".metta") else (
                f"{Path(args.name).stem}.metta"
            )
            overlay = overlays.get(name)
            if overlay is None:
                raise RuntimeError(
                    f"no overlay for {name}"
                    + (f" ({'; '.join(problems)})" if problems else "")
                )
            if args.petta_dir is not None:
                state, detail = overlay_state(
                    args.petta_dir.resolve(), overlay.patches
                )
                if state != OVERLAY_APPLIES:
                    raise RuntimeError(
                        f"{overlay.file_name} does not apply to that "
                        f"checkout ({state}{': ' + detail if detail else ''})"
                    )
            output = export_overlay(overlay, args.out.resolve())
            print(f"wrote {output}")
            return 0
        petta_dir = args.petta_dir.resolve()
        manifest_path = args.manifest.resolve()
        overlay_dir = args.overlay_dir.resolve()
        golden_dir = args.golden_dir.resolve()
        if args.command == "verify":
            verify_manifest(
                petta_dir, manifest_path, args.require_complete,
                overlay_dir, golden_dir,
            )
            return 0
        cetta = args.cetta.resolve()
        out_dir = args.out.resolve()
        if args.command == "compare":
            compare_manifest(
                petta_dir, manifest_path, cetta, out_dir,
                set(args.only), set(args.exclude_capability), args.limit,
                args.timeout, args.require_complete, args.require_match,
                overlay_dir, golden_dir,
            )
            return 0
        if args.command in {"accept", "sync"}:
            return refresh_corpus(
                petta_dir=petta_dir, manifest_path=manifest_path,
                cetta=cetta, out_dir=out_dir, timeout_override=args.timeout,
                excluded_capabilities=set(args.exclude_capability),
                sync=args.command == "sync",
                base_name=getattr(args, "base", DEFAULT_BASE),
                base_commit=getattr(args, "base_commit", None),
                overlay_dir=overlay_dir, golden_dir=golden_dir,
            )
        swi_report(
            petta_dir, manifest_path, cetta, out_dir, set(args.only),
            args.limit, args.timeout, overlay_dir, golden_dir,
        )
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
