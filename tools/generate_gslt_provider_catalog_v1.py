#!/usr/bin/env python3
"""Compile an authored semantic-provider catalog into a C descriptor."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import re

import gslt2parse_schema_v1 as sx
from generate_gslt_language_v1 import parse_manifest


class CompileError(RuntimeError):
    pass


@dataclass(frozen=True)
class Requirement:
    relation: str
    arity: int
    semantic_id: str


@dataclass(frozen=True)
class Catalog:
    name: str
    language: str
    profile: str | None
    requirements: tuple[Requirement, ...]


def symbol(value: sx.SExpr, context: str) -> str:
    if not isinstance(value, sx.Symbol) or not value.text:
        raise CompileError(f"{context}: expected a symbol")
    return value.text


def text(value: sx.SExpr, context: str) -> str:
    if isinstance(value, (sx.Symbol, sx.StringLiteral)) and value.text:
        return value.text
    raise CompileError(f"{context}: expected text")


def parse_catalog(path: Path) -> Catalog:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise CompileError(f"{path}: expected one catalog form")
    root = forms[0]
    if not root or symbol(root[0], f"{path}: root") != "gslt-provider-catalog-v1":
        raise CompileError(f"{path}: expected gslt-provider-catalog-v1")

    name: str | None = None
    language: str | None = None
    profile: str | None = None
    requirements: list[Requirement] = []
    keys: set[tuple[str, int]] = set()
    identities: set[str] = set()
    for index, raw in enumerate(root[1:], start=1):
        if not isinstance(raw, tuple) or not raw:
            raise CompileError(f"{path}: field {index} is malformed")
        tag = symbol(raw[0], f"{path}: field {index} tag")
        if tag in {"name", "language", "profile"}:
            if len(raw) != 2:
                raise CompileError(f"{path}: malformed {tag} field")
            value = text(raw[1], f"{path}: {tag}")
            if tag == "name":
                if name is not None:
                    raise CompileError(f"{path}: duplicate name")
                name = value
            elif tag == "language":
                if language is not None:
                    raise CompileError(f"{path}: duplicate language")
                language = value
            else:
                if profile is not None:
                    raise CompileError(f"{path}: duplicate profile")
                profile = value
            continue
        if tag != "provider" or len(raw) != 4:
            raise CompileError(f"{path}: unknown or malformed field {tag}")
        relation = text(raw[1], f"{path}: provider relation")
        arity = raw[2]
        if not isinstance(raw[3], sx.StringLiteral) or not raw[3].text:
            raise CompileError(
                f"{path}: provider semantic identity must be a string"
            )
        semantic_id = raw[3].text
        if not isinstance(arity, int) or arity < 0 or arity >= 2**32:
            raise CompileError(f"{path}: provider arity is out of range")
        key = (relation, arity)
        if key in keys:
            raise CompileError(f"{path}: duplicate provider {relation}/{arity}")
        if semantic_id in identities:
            raise CompileError(
                f"{path}: duplicate provider semantic identity {semantic_id}"
            )
        keys.add(key)
        identities.add(semantic_id)
        requirements.append(Requirement(relation, arity, semantic_id))
    if name is None or language is None or not requirements:
        raise CompileError(f"{path}: catalog requires name, language, and provider")
    return Catalog(name, language, profile, tuple(requirements))


def validate_against_language(catalog: Catalog, manifest_path: Path) -> None:
    manifest = parse_manifest(manifest_path, catalog.profile)
    if manifest.name != catalog.language:
        raise CompileError(
            f"catalog language {catalog.language} differs from {manifest.name}"
        )
    presentations = sx.admit(
        tuple(
            (manifest_path.parent / relative).resolve()
            for relative in manifest.semantic_sources
        )
    )
    operators = {
        (operator.name, operator.arity)
        for presentation in presentations
        for operator in presentation.operators
    }
    authored_heads = {
        (symbol(rule.head[0], f"{presentation.source}: rule head"),
         len(rule.head) - 1)
        for presentation in presentations
        for rule in presentation.rules
        if isinstance(rule.head, tuple) and rule.head
    }
    for requirement in catalog.requirements:
        key = (requirement.relation, requirement.arity)
        if key not in operators:
            raise CompileError(
                f"provider {requirement.relation}/{requirement.arity} "
                "is absent from the admitted language signature"
            )
        if key in authored_heads:
            raise CompileError(
                f"provider {requirement.relation}/{requirement.arity} "
                "would override an authored rule"
            )


def c_string(value: str | None) -> str:
    return "NULL" if value is None else json.dumps(value, ensure_ascii=True)


def c_bytes(name: str, payload: bytes) -> str:
    rows = []
    for start in range(0, len(payload), 16):
        chunk = payload[start : start + 16]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return f"static const uint8_t {name}[] = {{\n" + "\n".join(rows) + "\n};\n"


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--language-manifest", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", arguments.symbol):
        raise CompileError("descriptor symbol is not a C identifier")

    catalog_path = arguments.catalog.resolve()
    manifest_path = arguments.language_manifest.resolve()
    catalog = parse_catalog(catalog_path)
    validate_against_language(catalog, manifest_path)
    source_root = (
        arguments.source_root.resolve()
        if arguments.source_root else catalog_path.parent.resolve()
    )
    try:
        source_name = str(catalog_path.relative_to(source_root))
    except ValueError as error:
        raise CompileError("catalog escapes the declared source root") from error

    source_payload = catalog_path.read_bytes()
    manifest_payload = manifest_path.read_bytes()
    generator_hash = sha256()
    generator_hash.update(b"CettaGsltProviderCatalogCompilerV1\0")
    for dependency in (
        Path(__file__).resolve(),
        Path(sx.__file__).resolve(),
    ):
        payload = dependency.read_bytes()
        generator_hash.update(len(payload).to_bytes(8, "big"))
        generator_hash.update(payload)

    guard = f"CETTA_GENERATED_{arguments.symbol.upper()}_H"
    header = (
        f"#ifndef {guard}\n#define {guard}\n\n"
        '#include "gslt_provider_runtime.h"\n\n'
        f"extern const CettaGsltProviderCatalogV1 {arguments.symbol};\n\n"
        f"#endif /* {guard} */\n"
    )
    source_array = f"{arguments.symbol}_source_v1"
    requirement_array = f"{arguments.symbol}_requirements_v1"
    requirement_rows = "\n".join(
        "    {"
        f".relation = {c_string(requirement.relation)}, "
        f".arity = {requirement.arity}u, "
        f".semantic_id = {c_string(requirement.semantic_id)}"
        "},"
        for requirement in catalog.requirements
    )
    source = (
        f'#include "{arguments.header_include}"\n\n'
        + c_bytes(source_array, source_payload)
        + "\n"
        + f"static const CettaGsltProviderRequirementV1 {requirement_array}[] = {{\n"
        + requirement_rows
        + "\n};\n\n"
        + f"const CettaGsltProviderCatalogV1 {arguments.symbol} = {{\n"
        + f"    .name = {c_string(catalog.name)},\n"
        + f"    .language_name = {c_string(catalog.language)},\n"
        + f"    .profile_name = {c_string(catalog.profile)},\n"
        + f"    .language_manifest_sha256 = {c_string(sha256(manifest_payload).hexdigest())},\n"
        + f"    .source_bytes = {source_array},\n"
        + f"    .source_length = sizeof({source_array}),\n"
        + f"    .source_name = {c_string(source_name)},\n"
        + f"    .source_sha256 = {c_string(sha256(source_payload).hexdigest())},\n"
        + f"    .requirements = {requirement_array},\n"
        + f"    .requirement_count = {len(catalog.requirements)}u,\n"
        + f"    .generator_sha256 = {c_string(generator_hash.hexdigest())},\n"
        + "};\n"
    )
    write_if_changed(arguments.header, header)
    write_if_changed(arguments.source, source)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CompileError, sx.SchemaError, OSError) as error:
        print(f"error: {error}")
        raise SystemExit(1)
