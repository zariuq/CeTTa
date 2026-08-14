#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import time


SCHEMA = "cetta.petta-chainer-compat.v1"
ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")
HEX_COMMIT = re.compile(r"[0-9a-f]{40}")
HEX_SHA256 = re.compile(r"[0-9a-f]{64}")
PRINTED_VARIABLE = re.compile(r"\$(?:V[0-9]+|_[0-9]+)")


class CompatFailure(RuntimeError):
    pass


def fail(message: str) -> None:
    raise CompatFailure(message)


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def checked_relative_path(value: str, field: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        fail(f"{field} is not a safe relative path: {value!r}")
    return path


def load_manifest(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read compatibility manifest {path}: {error}")
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        fail(f"unsupported compatibility manifest schema in {path}")
    for source_name in ("chainer", "petta"):
        source = data.get(source_name)
        if not isinstance(source, dict):
            fail(f"manifest {source_name} source is absent")
        revision = source.get("revision")
        if not isinstance(revision, str) or not HEX_COMMIT.fullmatch(revision):
            fail(f"manifest {source_name} revision is not a full Git commit")
    subtree = data["chainer"].get("subtree")
    checked_relative_path(subtree, "chainer subtree")
    overlay_prefix = data["petta"].get("overlay_prefix")
    checked_relative_path(overlay_prefix, "PeTTa overlay prefix")
    examples = data.get("examples")
    if not isinstance(examples, list) or not examples:
        fail("manifest examples must be a non-empty list")
    names = []
    for example in examples:
        if not isinstance(example, dict):
            fail("manifest example is not an object")
        name = example.get("name")
        if not isinstance(name, str) or not name:
            fail("manifest example has no name")
        names.append(name)
        checked_relative_path(example.get("path", ""), f"{name} path")
        for field in ("source_sha256", "normalized_sha256"):
            value = example.get(field)
            if not isinstance(value, str) or not HEX_SHA256.fullmatch(value):
                fail(f"{name} {field} is not a SHA-256 digest")
        for field in ("checks", "truths", "timeout_seconds"):
            value = example.get(field)
            if not isinstance(value, int) or value < 0:
                fail(f"{name} {field} is not a non-negative integer")
    if names != sorted(names) or len(set(names)) != len(names):
        fail("manifest example names must be unique and sorted")
    forbidden = data.get("forbidden_stdout")
    if not isinstance(forbidden, list) or not all(
        isinstance(value, str) and value for value in forbidden
    ):
        fail("manifest forbidden_stdout must be a non-empty string list")
    return data


def run_git(repo: Path, *arguments: str, text: bool = False):
    command = ["git", "-C", str(repo), *arguments]
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=text,
        check=False,
    )
    if completed.returncode != 0:
        stderr = completed.stderr if text else completed.stderr.decode(errors="replace")
        fail(f"Git command failed in {repo}: {' '.join(arguments)}\n{stderr}")
    return completed.stdout


def require_commit(repo: Path, revision: str, source_name: str) -> None:
    if not repo.is_dir():
        fail(f"{source_name} repository does not exist: {repo}")
    run_git(repo, "cat-file", "-e", f"{revision}^{{commit}}")


def git_paths(repo: Path, revision: str, prefix: str) -> list[str]:
    output = run_git(
        repo, "ls-tree", "-r", "--name-only", revision, "--", prefix, text=True
    )
    return [line for line in output.splitlines() if line]


def git_file(repo: Path, revision: str, path: str) -> bytes:
    return run_git(repo, "show", f"{revision}:{path}")


def write_materialized_file(root: Path, relative: PurePosixPath, payload: bytes) -> None:
    destination = root.joinpath(*relative.parts)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)


def materialize_sources(
    root: Path,
    manifest: dict,
    chainer_repo: Path,
    petta_repo: Path,
    chainer_working_tree: bool = False,
) -> None:
    chainer_revision = manifest["chainer"]["revision"]
    subtree = manifest["chainer"]["subtree"].rstrip("/")
    paths = git_paths(chainer_repo, chainer_revision, subtree)
    prefix = subtree + "/"
    if not paths or any(not path.startswith(prefix) for path in paths):
        fail(f"pinned Chainer subtree is empty or malformed: {subtree}")
    for path in paths:
        relative = checked_relative_path(path[len(prefix) :], "Chainer member")
        payload = (
            (chainer_repo / path).read_bytes()
            if chainer_working_tree
            else git_file(chainer_repo, chainer_revision, path)
        )
        write_materialized_file(
            root, relative, payload
        )

    petta_revision = manifest["petta"]["revision"]
    overlay_prefix = manifest["petta"]["overlay_prefix"].rstrip("/")
    overlay_paths = [
        path
        for path in git_paths(petta_repo, petta_revision, overlay_prefix)
        if path.startswith(overlay_prefix + "/lib_") and path.endswith(".metta")
    ]
    if not overlay_paths:
        fail("pinned PeTTa library overlay is empty")
    for path in overlay_paths:
        relative = checked_relative_path(path, "PeTTa overlay member")
        write_materialized_file(
            root, relative, git_file(petta_repo, petta_revision, path)
        )


def alpha_normalize_printed_variables(text: str) -> str:
    names: dict[str, str] = {}

    def replace(match: re.Match[str]) -> str:
        source = match.group(0)
        if source not in names:
            names[source] = f"$V{len(names)}"
        return names[source]

    return PRINTED_VARIABLE.sub(replace, text)


def normalize_verdict_line(line: str) -> str:
    separator = ", should "
    if separator not in line:
        return alpha_normalize_printed_variables(line)
    actual, expected = line.split(separator, 1)
    return (
        alpha_normalize_printed_variables(actual)
        + separator
        + alpha_normalize_printed_variables(expected)
    )


def normalize_test_output(stdout: str) -> str:
    plain = ANSI_ESCAPE.sub("", stdout)
    selected = []
    for raw_line in plain.splitlines():
        line = raw_line.rstrip()
        if line == "true":
            selected.append(line)
        elif line.startswith("is ") and line.endswith("✅"):
            selected.append(normalize_verdict_line(line))
    return "".join(f"{line}\n" for line in selected)


def validate_output(
    example: dict,
    stdout: str,
    stderr: str,
    returncode: int,
    forbidden: list[str],
    implementation: str,
) -> str:
    name = example["name"]
    if returncode != 0:
        fail(
            f"{implementation} {name} exited {returncode}\n"
            f"stdout:\n{stdout[-4000:]}\nstderr:\n{stderr[-4000:]}"
        )
    if implementation == "CeTTa" and stderr:
        fail(f"CeTTa {name} wrote unexpected stderr:\n{stderr[-4000:]}")
    for marker in forbidden:
        if marker in stdout:
            fail(f"{implementation} {name} stdout contains forbidden marker {marker!r}")
    normalized = normalize_test_output(stdout)
    digest = sha256_bytes(normalized.encode("utf-8"))
    if digest != example["normalized_sha256"]:
        fail(
            f"{implementation} {name} normalized result differs: "
            f"expected {example['normalized_sha256']}, got {digest}\n{normalized}"
        )
    lines = normalized.splitlines()
    checks = sum(line.startswith("is ") for line in lines)
    truths = sum(line == "true" for line in lines)
    if checks != example["checks"] or truths != example["truths"]:
        fail(
            f"{implementation} {name} result cardinality differs: "
            f"checks={checks}, truths={truths}"
        )
    return digest


def run_example(
    command: list[str], cwd: Path, timeout: int, environment: dict[str, str]
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        fail(f"command timed out after {timeout}s: {' '.join(command)}\n{error}")
    except OSError as error:
        fail(f"cannot run {' '.join(command)}: {error}")


def require_reference_checkout(petta_repo: Path, revision: str) -> None:
    head = run_git(petta_repo, "rev-parse", "HEAD", text=True).strip()
    if head != revision:
        fail(f"reference checkout HEAD is {head}, expected {revision}")
    tracked = run_git(
        petta_repo, "status", "--porcelain", "--untracked-files=no", text=True
    )
    if tracked:
        fail("reference checkout has tracked modifications")
    if not (petta_repo / "run.sh").is_file():
        fail("reference checkout has no run.sh")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", required=True)
    parser.add_argument("--chainer-repo", required=True)
    parser.add_argument("--petta-root", required=True)
    parser.add_argument(
        "--manifest", default="tests/petta/chainer_compat/manifest.json"
    )
    parser.add_argument("--out", default="runtime/petta-chainer-compat")
    parser.add_argument(
        "--profile",
        choices=("extended", "typecheck-v2", "typecheck-v3"),
        default="extended",
    )
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--chainer-working-tree", action="store_true")
    arguments = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    manifest_path = Path(arguments.manifest)
    if not manifest_path.is_absolute():
        manifest_path = repo_root / manifest_path
    manifest = load_manifest(manifest_path)

    cetta = Path(arguments.cetta).resolve()
    chainer_repo = Path(arguments.chainer_repo).resolve()
    petta_repo = Path(arguments.petta_root).resolve()
    output_root = Path(arguments.out)
    if not output_root.is_absolute():
        output_root = repo_root / output_root
    output_root.mkdir(parents=True, exist_ok=True)
    if not cetta.is_file():
        fail(f"CeTTa binary does not exist: {cetta}")

    require_commit(
        chainer_repo, manifest["chainer"]["revision"], "PeTTaChainer"
    )
    chainer_head = run_git(
        chainer_repo, "rev-parse", "HEAD", text=True
    ).strip()
    if arguments.chainer_working_tree and (
        chainer_head != manifest["chainer"]["revision"]
    ):
        fail(
            "PeTTaChainer working tree HEAD is "
            f"{chainer_head}, expected {manifest['chainer']['revision']}"
        )
    require_commit(petta_repo, manifest["petta"]["revision"], "PeTTa")
    if arguments.reference:
        require_reference_checkout(petta_repo, manifest["petta"]["revision"])

    environment = os.environ.copy()
    environment["CETTA_PETTA_SEARCH_MACHINE"] = "1"
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    results = []
    with tempfile.TemporaryDirectory(
        prefix=".pettachainer-image-", dir=output_root
    ) as temporary:
        image = Path(temporary)
        materialize_sources(
            image,
            manifest,
            chainer_repo,
            petta_repo,
            chainer_working_tree=arguments.chainer_working_tree,
        )
        for example in manifest["examples"]:
            source = image.joinpath(
                *checked_relative_path(example["path"], "example path").parts
            )
            if not source.is_file():
                fail(f"materialized example is absent: {example['path']}")
            source_digest = sha256_bytes(source.read_bytes())
            if source_digest != example["source_sha256"]:
                fail(
                    f"{example['name']} source differs: expected "
                    f"{example['source_sha256']}, got {source_digest}"
                )

            started = time.monotonic()
            completed = run_example(
                [
                    str(cetta),
                    "--lang",
                    "petta",
                    "--profile",
                    arguments.profile,
                    str(source),
                ],
                image,
                example["timeout_seconds"],
                environment,
            )
            cetta_seconds = time.monotonic() - started
            (output_root / f"{example['name']}.stdout").write_text(
                completed.stdout, encoding="utf-8"
            )
            (output_root / f"{example['name']}.stderr").write_text(
                completed.stderr, encoding="utf-8"
            )
            digest = validate_output(
                example,
                completed.stdout,
                completed.stderr,
                completed.returncode,
                manifest["forbidden_stdout"],
                "CeTTa",
            )
            result = {
                "name": example["name"],
                "cetta": "MATCH",
                "cetta_seconds": round(cetta_seconds, 6),
                "normalized_sha256": digest,
            }

            if arguments.reference:
                started = time.monotonic()
                reference = run_example(
                    ["bash", str(petta_repo / "run.sh"), str(source), "--silent"],
                    image,
                    example["timeout_seconds"],
                    environment,
                )
                reference_seconds = time.monotonic() - started
                (output_root / f"{example['name']}.reference.stdout").write_text(
                    reference.stdout, encoding="utf-8"
                )
                (output_root / f"{example['name']}.reference.stderr").write_text(
                    reference.stderr, encoding="utf-8"
                )
                reference_digest = validate_output(
                    example,
                    reference.stdout,
                    reference.stderr,
                    reference.returncode,
                    ["❌"],
                    "PeTTa",
                )
                result["reference"] = "MATCH"
                result["reference_seconds"] = round(
                    reference_seconds, 6
                )
                result["reference_normalized_sha256"] = reference_digest
            results.append(result)

    summary = {
        "schema": SCHEMA,
        "chainer_revision": manifest["chainer"]["revision"],
        "petta_revision": manifest["petta"]["revision"],
        "profile": arguments.profile,
        "chainer_source": (
            "working-tree" if arguments.chainer_working_tree else "commit"
        ),
        "results": results,
    }
    if arguments.chainer_working_tree:
        patch = run_git(
            chainer_repo,
            "diff",
            "--binary",
            "HEAD",
            "--",
            manifest["chainer"]["subtree"],
        )
        summary["chainer_working_tree_diff_sha256"] = sha256_bytes(patch)
    (output_root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        f"PASS: PeTTaChainer compatibility ({len(results)}/{len(results)} examples)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CompatFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
