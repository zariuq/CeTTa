#!/usr/bin/env python3
"""Emit a deterministic certificate for one generated GSLT language pack.

The producer is intentionally untrusted.  The independent native checker
recomputes every identity below, reparses and admits the embedded sources, and
checks the residual plan against every admitted rule occurrence.
"""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
from typing import Iterable

import generate_gslt_language_v1 as language_compiler
import gslt2parse_schema_v1 as sx


class CertificateError(RuntimeError):
    pass


COMPILER_DOMAIN = b"CettaGsltLanguageCompilerV1\0"
SELECTED_SOURCE_DOMAIN = b"CettaGsltSelectedSourceV1\0"
ADMISSION_DOMAIN = b"CettaGsltAdmissionV1\0"
ARTIFACT_DOMAIN = b"CettaGsltEmbeddedArtifactV1\0"


def _update_u64(digest: object, value: int) -> None:
    if value < 0 or value >= 2**64:
        raise CertificateError("certificate length exceeds the V1 ABI")
    digest.update(value.to_bytes(8, "big"))


def _update_blob(digest: object, payload: bytes) -> None:
    _update_u64(digest, len(payload))
    digest.update(payload)


def _digest_blobs(domain: bytes, payloads: Iterable[bytes]) -> str:
    digest = sha256()
    digest.update(domain)
    for payload in payloads:
        _update_blob(digest, payload)
    return digest.hexdigest()


def compiler_digest(generator: Path, schema: Path) -> str:
    digest = sha256()
    digest.update(COMPILER_DOMAIN)
    for path in (generator, schema):
        _update_blob(digest, path.read_bytes())
    return digest.hexdigest()


def selected_source_digest(
    profile: str,
    manifest_name: str,
    manifest_payload: bytes,
    semantic_payloads: list[tuple[str, bytes]],
) -> str:
    payloads = [
        profile.encode("utf-8"),
        manifest_name.encode("utf-8"),
        manifest_payload,
        len(semantic_payloads).to_bytes(8, "big"),
    ]
    for name, payload in semantic_payloads:
        payloads.extend((name.encode("utf-8"), payload))
    return _digest_blobs(SELECTED_SOURCE_DOMAIN, payloads)


def admission_digest(
    selected_digest: str, presentation_count: int, rule_count: int
) -> str:
    return _digest_blobs(
        ADMISSION_DOMAIN,
        (
            selected_digest.encode("ascii"),
            presentation_count.to_bytes(8, "big"),
            rule_count.to_bytes(8, "big"),
        ),
    )


def artifact_digest(header: bytes, source: bytes) -> str:
    return _digest_blobs(ARTIFACT_DOMAIN, (header, source))


def _quoted(value: str) -> str:
    return json.dumps(value, ensure_ascii=True, separators=(",", ":"))


def _embedded_compiler_digest(source: str) -> str:
    match = re.search(
        r'\.compiler_sha256\s*=\s*"([0-9a-f]{64})"', source
    )
    if not match:
        raise CertificateError("generated source omits compiler identity")
    return match.group(1)


def render_certificate(
    *,
    manifest_path: Path,
    source_root: Path,
    profile_name: str | None,
    descriptor_symbol: str,
    header_path: Path,
    source_path: Path,
    generator_path: Path,
    schema_path: Path,
) -> str:
    manifest = language_compiler.parse_manifest(manifest_path, profile_name)
    try:
        manifest_name = str(manifest_path.relative_to(source_root))
    except ValueError as error:
        raise CertificateError(
            "manifest escapes the declared source root"
        ) from error

    semantic_paths: list[Path] = []
    semantic_payloads: list[tuple[str, bytes]] = []
    for relative in manifest.semantic_sources:
        path = (manifest_path.parent / relative).resolve()
        try:
            path.relative_to(source_root)
        except ValueError as error:
            raise CertificateError(
                f"semantic source escapes the source root: {relative}"
            ) from error
        semantic_paths.append(path)
        semantic_payloads.append((relative, path.read_bytes()))

    admitted = sx.admit(tuple(semantic_paths))
    plan = language_compiler.compile_plan(admitted)
    manifest_payload = manifest_path.read_bytes()
    header_payload = header_path.read_bytes()
    source_payload = source_path.read_bytes()
    source_text = source_payload.decode("utf-8")
    if descriptor_symbol not in source_text or descriptor_symbol not in (
        header_payload.decode("utf-8")
    ):
        raise CertificateError("generated artifact omits its descriptor symbol")

    compiler_sha = compiler_digest(generator_path, schema_path)
    if _embedded_compiler_digest(source_text) != compiler_sha:
        raise CertificateError(
            "generated artifact was produced by a different compiler identity"
        )
    profile = profile_name if profile_name is not None else "base"
    selected_sha = selected_source_digest(
        profile,
        manifest_name,
        manifest_payload,
        semantic_payloads,
    )
    presentation_count = len(admitted)
    rule_count = sum(len(presentation.rules) for presentation in admitted)
    admission_sha = admission_digest(
        selected_sha, presentation_count, rule_count
    )
    plan_sha = sha256(plan).hexdigest()
    artifact_sha = artifact_digest(header_payload, source_payload)

    rows = [
        "(gslt-compilation-certificate-v1",
        f"  (compiler {_quoted('CettaGsltLanguageCompilerV1')} {_quoted(compiler_sha)})",
        f"  (descriptor {_quoted(descriptor_symbol)})",
        f"  (language {_quoted(manifest.name)})",
        f"  (profile {_quoted(profile)})",
        f"  (observation {_quoted(manifest.observation)})",
        "  (manifest "
        f"{_quoted(manifest_name)} {len(manifest_payload)} "
        f"{_quoted(sha256(manifest_payload).hexdigest())})",
    ]
    for index, (relative, payload) in enumerate(semantic_payloads):
        rows.append(
            "  (semantic-source "
            f"{index} {_quoted(relative)} {len(payload)} "
            f"{_quoted(sha256(payload).hexdigest())})"
        )
    rows.extend(
        [
            "  (admission "
            f"{presentation_count} {rule_count} {_quoted(admission_sha)})",
            "  (compiled-plan "
            f"{_quoted('CGP1')} {len(plan)} {_quoted(plan_sha)})",
            "  (artifact "
            f"{len(header_payload)} {_quoted(sha256(header_payload).hexdigest())} "
            f"{len(source_payload)} {_quoted(sha256(source_payload).hexdigest())} "
            f"{_quoted(artifact_sha)})",
            "  (stage 0 "
            f"{_quoted('source-selection')} {_quoted('authored-gslt-v1')} "
            f"{_quoted(selected_sha)})",
            "  (stage 1 "
            f"{_quoted('source-admission')} {_quoted(selected_sha)} "
            f"{_quoted(admission_sha)})",
            "  (stage 2 "
            f"{_quoted('plan-compilation')} {_quoted(admission_sha)} "
            f"{_quoted(plan_sha)})",
            "  (stage 3 "
            f"{_quoted('artifact-serialization')} {_quoted(plan_sha)} "
            f"{_quoted(artifact_sha)})",
            ")",
            "",
        ]
    )
    return "\n".join(rows)


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--profile")
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--certificate", type=Path, required=True)
    arguments = parser.parse_args()

    manifest_path = arguments.manifest.resolve()
    source_root = (
        arguments.source_root.resolve()
        if arguments.source_root
        else manifest_path.parent.resolve()
    )
    content = render_certificate(
        manifest_path=manifest_path,
        source_root=source_root,
        profile_name=arguments.profile,
        descriptor_symbol=arguments.symbol,
        header_path=arguments.header.resolve(),
        source_path=arguments.source.resolve(),
        generator_path=arguments.generator.resolve(),
        schema_path=arguments.schema.resolve(),
    )
    write_if_changed(arguments.certificate, content)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        CertificateError,
        language_compiler.CompileError,
        sx.SchemaError,
        OSError,
        UnicodeError,
    ) as error:
        print(f"error: {error}")
        raise SystemExit(1)
