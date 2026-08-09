#!/usr/bin/env python3
"""Compile a first-class GSLT language manifest into an embedded C descriptor."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import re
import struct

import gslt2parse_schema_v1 as sx


class CompileError(RuntimeError):
    pass


@dataclass(frozen=True)
class Manifest:
    name: str
    profile_name: str | None
    syntax_backend: str
    term_abi: str
    semantic_sources: tuple[str, ...]
    program_nil: str
    program_cons: str
    entry_relation: str | None
    entry_arity: int
    program_position: int
    result_position: int
    classify_relation: str | None
    produce_relation: str | None
    observe_relation: str | None
    produced_nil: str | None
    produced_cons: str | None
    observation: str


PLAN_SYMBOL = 1
PLAN_VARIABLE = 2
PLAN_STRING = 3
PLAN_INTEGER = 4
PLAN_APPLICATION = 5


@dataclass(frozen=True)
class PlanNode:
    kind: int
    child_offset: int = 0
    child_count: int = 0
    integer: int = 0
    variable: int = 0
    text: str = ""


@dataclass(frozen=True)
class PlanRule:
    name: str
    head: int
    body_offset: int
    body_count: int
    variable_count: int


class PlanBuilder:
    def __init__(self) -> None:
        self.nodes: list[PlanNode] = []
        self.children: list[int] = []
        self.rules: list[PlanRule] = []
        self.bodies: list[int] = []

    def term(self, value: sx.SExpr, variables: dict[str, int]) -> int:
        if isinstance(value, sx.Symbol):
            node = PlanNode(PLAN_SYMBOL, text=value.text)
        elif isinstance(value, sx.Variable):
            slot = variables.setdefault(value.name, len(variables))
            node = PlanNode(PLAN_VARIABLE, variable=slot)
        elif isinstance(value, sx.StringLiteral):
            node = PlanNode(PLAN_STRING, text=value.text)
        elif isinstance(value, int):
            if value < -(2**63) or value >= 2**63:
                raise CompileError(
                    "compiled GSLT plan supports signed 64-bit integers"
                )
            node = PlanNode(PLAN_INTEGER, integer=value)
        elif isinstance(value, tuple) and value:
            head = symbol(value[0], "compiled GSLT application head")
            child_ids = [self.term(child, variables) for child in value[1:]]
            child_offset = len(self.children)
            self.children.extend(child_ids)
            node = PlanNode(
                PLAN_APPLICATION,
                child_offset=child_offset,
                child_count=len(child_ids),
                text=head,
            )
        else:
            raise CompileError("compiled GSLT plan cannot encode an empty term")
        index = len(self.nodes)
        self.nodes.append(node)
        return index

    def presentation(self, presentation: sx.Presentation) -> None:
        for rule in presentation.rules:
            variables: dict[str, int] = {}
            head = self.term(rule.head, variables)
            body_offset = len(self.bodies)
            self.bodies.extend(
                self.term(goal, variables) for goal in rule.body
            )
            self.rules.append(
                PlanRule(
                    name=rule.name,
                    head=head,
                    body_offset=body_offset,
                    body_count=len(rule.body),
                    variable_count=len(variables),
                )
            )


def _plan_text(value: str) -> bytes:
    payload = value.encode("utf-8")
    return struct.pack("<I", len(payload)) + payload


def compile_plan(presentations: tuple[sx.Presentation, ...]) -> bytes:
    builder = PlanBuilder()
    for presentation in presentations:
        builder.presentation(presentation)
    payload = bytearray(b"CGP1")
    payload.extend(
        struct.pack(
            "<IIII",
            len(builder.nodes),
            len(builder.children),
            len(builder.rules),
            len(builder.bodies),
        )
    )
    for node in builder.nodes:
        payload.extend(
            struct.pack(
                "<BIIqI",
                node.kind,
                node.child_offset,
                node.child_count,
                node.integer,
                node.variable,
            )
        )
        payload.extend(_plan_text(node.text))
    for child in builder.children:
        payload.extend(struct.pack("<I", child))
    for rule in builder.rules:
        payload.extend(
            struct.pack(
                "<IIII",
                rule.head,
                rule.body_offset,
                rule.body_count,
                rule.variable_count,
            )
        )
        payload.extend(_plan_text(rule.name))
    for body in builder.bodies:
        payload.extend(struct.pack("<I", body))
    return bytes(payload)


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


def parse_manifest(path: Path, selected_profile: str | None = None) -> Manifest:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise CompileError(f"{path}: expected one manifest form")
    root = forms[0]
    if not root or symbol(root[0], f"{path}: root") != "gslt-language-v1":
        raise CompileError(f"{path}: expected gslt-language-v1")
    singleton: dict[str, tuple[sx.SExpr, ...]] = {}
    sources: list[str] = []
    profiles: dict[str, tuple[str, ...]] = {}
    known = {
        "name",
        "syntax-backend",
        "term-abi",
        "semantic-source",
        "program-carrier",
        "entry",
        "request-pipeline",
        "observation",
        "profile",
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
        if tag == "profile":
            if len(item) < 3:
                raise CompileError(f"{path}: malformed profile")
            profile_name = symbol(item[1], f"{path}: profile name")
            if profile_name in profiles:
                raise CompileError(
                    f"{path}: duplicate profile {profile_name}"
                )
            profile_sources: list[str] = []
            for layer_offset, raw_layer in enumerate(item[2:], start=1):
                layer = field(
                    raw_layer,
                    f"{path}: profile {profile_name} field {layer_offset}",
                )
                layer_tag = symbol(
                    layer[0],
                    f"{path}: profile {profile_name} field "
                    f"{layer_offset} tag",
                )
                if layer_tag != "semantic-source" or len(layer) != 2:
                    raise CompileError(
                        f"{path}: profile {profile_name} supports only "
                        "semantic-source extensions"
                    )
                profile_sources.append(
                    text(
                        layer[1],
                        f"{path}: profile {profile_name} semantic-source",
                    )
                )
            profiles[profile_name] = tuple(profile_sources)
            continue
        if tag in singleton:
            raise CompileError(f"{path}: duplicate field {tag}")
        singleton[tag] = item
    required = known - {
        "semantic-source", "entry", "request-pipeline", "profile"
    }
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
    if len(carrier) != 3:
        raise CompileError(f"{path}: malformed program carrier")
    has_entry = "entry" in singleton
    has_pipeline = "request-pipeline" in singleton
    if has_entry == has_pipeline:
        raise CompileError(
            f"{path}: expected exactly one entry or request-pipeline"
        )
    entry_relation: str | None = None
    arity = 0
    program_position = 0
    result_position = 0
    classify_relation: str | None = None
    produce_relation: str | None = None
    observe_relation: str | None = None
    produced_nil: str | None = None
    produced_cons: str | None = None
    if has_entry:
        entry = singleton["entry"]
        if len(entry) != 5:
            raise CompileError(f"{path}: malformed entry")
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
        entry_relation = text(entry[1], f"{path}: entry relation")
    else:
        pipeline = singleton["request-pipeline"]
        if len(pipeline) != 6:
            raise CompileError(f"{path}: malformed request-pipeline")
        classify_relation = text(pipeline[1], f"{path}: classify relation")
        produce_relation = text(pipeline[2], f"{path}: produce relation")
        observe_relation = text(pipeline[3], f"{path}: observe relation")
        produced_nil = text(pipeline[4], f"{path}: produced nil")
        produced_cons = text(pipeline[5], f"{path}: produced cons")
    if selected_profile is not None:
        extension_sources = profiles.get(selected_profile)
        if extension_sources is None:
            raise CompileError(
                f"{path}: unknown profile {selected_profile}"
            )
        sources.extend(extension_sources)
    manifest = Manifest(
        name=text(singleton["name"][1], f"{path}: name"),
        profile_name=selected_profile,
        syntax_backend=text(
            singleton["syntax-backend"][1], f"{path}: syntax-backend"
        ),
        term_abi=text(singleton["term-abi"][1], f"{path}: term-abi"),
        semantic_sources=tuple(sources),
        program_nil=text(carrier[1], f"{path}: program nil"),
        program_cons=text(carrier[2], f"{path}: program cons"),
        entry_relation=entry_relation,
        entry_arity=arity,
        program_position=program_position,
        result_position=result_position,
        classify_relation=classify_relation,
        produce_relation=produce_relation,
        observe_relation=observe_relation,
        produced_nil=produced_nil,
        produced_cons=produced_cons,
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
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--profile")
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", arguments.symbol):
        raise CompileError("descriptor symbol is not a C identifier")

    manifest_path = arguments.manifest.resolve()
    manifest = parse_manifest(manifest_path, arguments.profile)
    source_root = (
        arguments.source_root.resolve()
        if arguments.source_root
        else manifest_path.parent.resolve()
    )
    try:
        manifest_path.relative_to(source_root)
    except ValueError as error:
        raise CompileError("manifest escapes the declared source root") from error
    semantic_payloads: list[tuple[str, bytes]] = []
    semantic_paths: list[Path] = []
    for relative in manifest.semantic_sources:
        source = (manifest_path.parent / relative).resolve()
        try:
            source.relative_to(source_root)
        except ValueError as error:
            raise CompileError(
                f"semantic source escapes the declared source root: {relative}"
            ) from error
        sx.parse_presentation(source)
        semantic_paths.append(source)
        semantic_payloads.append((relative, source.read_bytes()))

    admitted = sx.admit(tuple(semantic_paths))
    compiled_plan = compile_plan(admitted)

    compiler_hash = sha256()
    compiler_hash.update(b"CettaGsltLanguageCompilerV1\0")
    for compiler_source in (Path(__file__).resolve(), Path(sx.__file__).resolve()):
        payload = compiler_source.read_bytes()
        compiler_hash.update(len(payload).to_bytes(8, "big"))
        compiler_hash.update(payload)
    compiler_digest = compiler_hash.hexdigest()
    manifest_payload = manifest_path.read_bytes()
    manifest_digest = sha256(manifest_payload).hexdigest()
    manifest_relative = str(manifest_path.relative_to(source_root))
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
    plan_array = f"{arguments.symbol}_compiled_plan_v1"
    manifest_array = f"{arguments.symbol}_manifest_v1"
    source = (
        f'#include "{arguments.header_include}"\n\n'
        + c_bytes(manifest_array, manifest_payload)
        + "\n"
        + "\n".join(arrays)
        + "\n"
        + c_bytes(plan_array, compiled_plan)
        + "\n"
        + f"static const CettaGsltEmbeddedSourceV1 "
          f"{arguments.symbol}_sources[] = {{\n"
        + "\n".join(source_rows)
        + "\n};\n\n"
        + f"const CettaGsltEmbeddedLanguageV1 {arguments.symbol} = {{\n"
        + f"    .name = {c_string(manifest.name)},\n"
        + f"    .profile_name = {c_string(manifest.profile_name) if manifest.profile_name else 'NULL'},\n"
        + f"    .syntax_backend = {c_string(manifest.syntax_backend)},\n"
        + f"    .term_abi = {c_string(manifest.term_abi)},\n"
        + "    .manifest = {\n"
        + "        .input = {\n"
        + f"            .bytes = {manifest_array},\n"
        + f"            .length = sizeof({manifest_array}),\n"
        + f"            .source = {c_string(manifest_relative)},\n"
        + "        },\n"
        + f"        .sha256 = {c_string(manifest_digest)},\n"
        + "    },\n"
        + f"    .semantic_sources = {arguments.symbol}_sources,\n"
        + f"    .semantic_source_count = {len(semantic_payloads)}u,\n"
        + "    .compiled_plan = {\n"
        + f"        .bytes = {plan_array},\n"
        + f"        .length = sizeof({plan_array}),\n"
        + f"        .sha256 = {c_string(sha256(compiled_plan).hexdigest())},\n"
        + "    },\n"
        + f"    .program_nil = {c_string(manifest.program_nil)},\n"
        + f"    .program_cons = {c_string(manifest.program_cons)},\n"
        + f"    .entry_relation = {c_string(manifest.entry_relation) if manifest.entry_relation else 'NULL'},\n"
        + f"    .entry_arity = {manifest.entry_arity}u,\n"
        + f"    .program_position = {manifest.program_position}u,\n"
        + f"    .result_position = {manifest.result_position}u,\n"
        + (
            "    .request_pipeline = &(const CettaGsltRequestPipelineV1){\n"
            f"        .classify_relation = {c_string(manifest.classify_relation)},\n"
            f"        .produce_relation = {c_string(manifest.produce_relation)},\n"
            f"        .observe_relation = {c_string(manifest.observe_relation)},\n"
            f"        .produced_nil = {c_string(manifest.produced_nil)},\n"
            f"        .produced_cons = {c_string(manifest.produced_cons)},\n"
            "    },\n"
            if manifest.classify_relation else
            "    .request_pipeline = NULL,\n"
        )
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
