#!/usr/bin/env python3
"""Compile a first-class GSLT language manifest into an embedded C descriptor."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import re

import gslt2parse_schema_v1 as sx


class CompileError(RuntimeError):
    pass


@dataclass(frozen=True)
class Manifest:
    name: str
    syntax_backend: str
    term_abi: str
    semantic_sources: tuple[str, ...]
    program_nil: str
    program_cons: str
    entry_relation: str
    entry_arity: int
    program_position: int
    result_position: int
    observation: str


def symbol(value: sx.SExpr, context: str) -> str:
    if not isinstance(value, sx.Symbol) or not value.text:
        raise CompileError(f"{context}: expected a symbol")
    return value.text


def text(value: sx.SExpr, context: str) -> str:
    if isinstance(value, (sx.Symbol, sx.StringLiteral)) and value.text:
        return value.text
    raise CompileError(f"{context}: expected text")


def field(value: sx.SExpr, context: str) -> tuple[sx.SExpr, ...]:
    if not isinstance(value, tuple) or not value:
        raise CompileError(f"{context}: expected a nonempty field")
    return value


def parse_manifest(path: Path) -> Manifest:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise CompileError(f"{path}: expected one manifest form")
    root = forms[0]
    if not root or symbol(root[0], f"{path}: root") != "gslt-language-v1":
        raise CompileError(f"{path}: expected gslt-language-v1")
    singleton: dict[str, tuple[sx.SExpr, ...]] = {}
    sources: list[str] = []
    known = {
        "name",
        "syntax-backend",
        "term-abi",
        "semantic-source",
        "program-carrier",
        "entry",
        "observation",
    }
    for offset, raw in enumerate(root[1:], start=1):
        item = field(raw, f"{path}: field {offset}")
        tag = symbol(item[0], f"{path}: field {offset} tag")
        if tag not in known:
            raise CompileError(f"{path}: unknown field {tag}")
        if tag == "semantic-source":
            if len(item) != 2:
                raise CompileError(f"{path}: malformed semantic-source")
            sources.append(text(item[1], f"{path}: semantic-source"))
            continue
        if tag in singleton:
            raise CompileError(f"{path}: duplicate field {tag}")
        singleton[tag] = item
    required = known - {"semantic-source"}
    missing = sorted(required - singleton.keys())
    if missing or not sources:
        raise CompileError(
            f"{path}: missing fields: {', '.join(missing or ['semantic-source'])}"
        )
    if any(len(singleton[name]) != 2 for name in (
        "name", "syntax-backend", "term-abi", "observation"
    )):
        raise CompileError(f"{path}: malformed scalar field")
    carrier = singleton["program-carrier"]
    entry = singleton["entry"]
    if len(carrier) != 3 or len(entry) != 5:
        raise CompileError(f"{path}: malformed carrier or entry")
    if not all(isinstance(entry[index], int) for index in (2, 3, 4)):
        raise CompileError(f"{path}: entry indices must be integers")
    arity = entry[2]
    program_position = entry[3]
    result_position = entry[4]
    if (
        arity <= 0
        or arity >= 2**32 - 1
        or program_position < 0
        or result_position < 0
        or program_position >= arity
        or result_position >= arity
        or program_position == result_position
    ):
        raise CompileError(f"{path}: invalid entry positions")
    manifest = Manifest(
        name=text(singleton["name"][1], f"{path}: name"),
        syntax_backend=text(
            singleton["syntax-backend"][1], f"{path}: syntax-backend"
        ),
        term_abi=text(singleton["term-abi"][1], f"{path}: term-abi"),
        semantic_sources=tuple(sources),
        program_nil=text(carrier[1], f"{path}: program nil"),
        program_cons=text(carrier[2], f"{path}: program cons"),
        entry_relation=text(entry[1], f"{path}: entry relation"),
        entry_arity=arity,
        program_position=program_position,
        result_position=result_position,
        observation=text(
            singleton["observation"][1], f"{path}: observation"
        ),
    )
    if manifest.term_abi != "finite-horn-quote-v1":
        raise CompileError(f"{path}: unsupported term ABI")
    if manifest.observation != "bag":
        raise CompileError(f"{path}: unsupported observation")
    return manifest


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def c_bytes(name: str, payload: bytes) -> str:
    rows = []
    for start in range(0, len(payload), 16):
        chunk = payload[start : start + 16]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    body = "\n".join(rows)
    return f"static const uint8_t {name}[] = {{\n{body}\n}};\n"


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", arguments.symbol):
        raise CompileError("descriptor symbol is not a C identifier")

    manifest_path = arguments.manifest.resolve()
    manifest = parse_manifest(manifest_path)
    semantic_payloads: list[tuple[str, bytes]] = []
    for relative in manifest.semantic_sources:
        source = (manifest_path.parent / relative).resolve()
        try:
            source.relative_to(manifest_path.parent.resolve())
        except ValueError as error:
            raise CompileError(
                f"semantic source escapes the language directory: {relative}"
            ) from error
        sx.parse_presentation(source)
        semantic_payloads.append((relative, source.read_bytes()))

    compiler_digest = sha256(Path(__file__).read_bytes()).hexdigest()
    manifest_digest = sha256(manifest_path.read_bytes()).hexdigest()
    guard = f"CETTA_GENERATED_{arguments.symbol.upper()}_H"
    header = (
        f"#ifndef {guard}\n#define {guard}\n\n"
        '#include "gslt_language_runtime.h"\n\n'
        f"extern const CettaGsltEmbeddedLanguageV1 {arguments.symbol};\n\n"
        f"#endif /* {guard} */\n"
    )

    arrays = []
    source_rows = []
    for index, (relative, payload) in enumerate(semantic_payloads):
        array_name = f"{arguments.symbol}_semantic_{index}"
        arrays.append(c_bytes(array_name, payload))
        source_rows.append(
            "    {\n"
            "        .input = {\n"
            f"            .bytes = {array_name},\n"
            f"            .length = sizeof({array_name}),\n"
            f"            .source = {c_string(relative)},\n"
            "        },\n"
            f"        .sha256 = {c_string(sha256(payload).hexdigest())},\n"
            "    },"
        )
    source = (
        f'#include "{arguments.header_include}"\n\n'
        + "\n".join(arrays)
        + "\n"
        + f"static const CettaGsltEmbeddedSemanticSourceV1 "
          f"{arguments.symbol}_sources[] = {{\n"
        + "\n".join(source_rows)
        + "\n};\n\n"
        + f"const CettaGsltEmbeddedLanguageV1 {arguments.symbol} = {{\n"
        + f"    .name = {c_string(manifest.name)},\n"
        + f"    .syntax_backend = {c_string(manifest.syntax_backend)},\n"
        + f"    .term_abi = {c_string(manifest.term_abi)},\n"
        + f"    .semantic_sources = {arguments.symbol}_sources,\n"
        + f"    .semantic_source_count = {len(semantic_payloads)}u,\n"
        + f"    .program_nil = {c_string(manifest.program_nil)},\n"
        + f"    .program_cons = {c_string(manifest.program_cons)},\n"
        + f"    .entry_relation = {c_string(manifest.entry_relation)},\n"
        + f"    .entry_arity = {manifest.entry_arity}u,\n"
        + f"    .program_position = {manifest.program_position}u,\n"
        + f"    .result_position = {manifest.result_position}u,\n"
        + f"    .observation = {c_string(manifest.observation)},\n"
        + f"    .manifest_sha256 = {c_string(manifest_digest)},\n"
        + f"    .compiler_sha256 = {c_string(compiler_digest)},\n"
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
