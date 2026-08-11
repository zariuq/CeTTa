#!/usr/bin/env python3
"""Generate a support-transform GSLT descriptor from its authored profile."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import struct

import gslt2parse_schema_v1 as sx


class ProfileError(RuntimeError):
    pass


@dataclass(frozen=True)
class OperatorDecl:
    surface_symbol: str
    argument_count: int
    operator_id: str


@dataclass(frozen=True)
class Profile:
    language_name: str
    profile_name: str
    work_symbol: str
    work_arity: int
    location_position: int
    input_position: int
    output_position: int
    compat_input_symbol: str
    compat_input_operator_id: str
    explicit_input_symbol: str
    sources: tuple[OperatorDecl, ...]
    compat_output_symbol: str
    compat_output_operator_id: str
    explicit_output_symbol: str
    sinks: tuple[OperatorDecl, ...]


def symbol(value: sx.SExpr, context: str) -> str:
    if isinstance(value, (sx.Symbol, sx.StringLiteral)) and value.text:
        return value.text
    raise ProfileError(f"{context}: expected text")


def field(value: sx.SExpr, context: str) -> tuple[sx.SExpr, ...]:
    if not isinstance(value, tuple) or not value:
        raise ProfileError(f"{context}: expected a nonempty field")
    return value


def exact_field(
    fields: dict[str, tuple[sx.SExpr, ...]], tag: str, expected: tuple[str, ...]
) -> None:
    value = fields[tag]
    actual = tuple(symbol(item, tag) for item in value[1:])
    if actual != expected:
        raise ProfileError(
            f"{tag}: expected {' '.join(expected)}, got {' '.join(actual)}"
        )


def operator_entries(
    value: tuple[sx.SExpr, ...], context: str, expected_tag: str
) -> tuple[OperatorDecl, ...]:
    entries: list[OperatorDecl] = []
    surfaces: set[str] = set()
    for offset, raw in enumerate(value, start=1):
        item = field(raw, f"{context} item {offset}")
        if len(item) != 4:
            raise ProfileError(f"{context} item {offset}: expected four fields")
        if symbol(item[0], f"{context} item {offset} tag") != expected_tag:
            raise ProfileError(
                f"{context} item {offset}: expected {expected_tag}"
            )
        surface = symbol(item[1], f"{context} item {offset} surface")
        if surface in surfaces:
            raise ProfileError(f"{context}: duplicate surface {surface}")
        argument_count = item[2]
        if not isinstance(argument_count, int) or not 0 <= argument_count <= 63:
            raise ProfileError(
                f"{context} item {offset}: argument count must be 0..63"
            )
        operator_id = symbol(item[3], f"{context} item {offset} operator")
        surfaces.add(surface)
        entries.append(OperatorDecl(surface, argument_count, operator_id))
    if not entries:
        raise ProfileError(f"{context}: expected at least one declaration")
    return tuple(entries)


def parse_profile(path: Path) -> Profile:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1 or not isinstance(forms[0], tuple):
        raise ProfileError(f"{path}: expected one profile form")
    root = forms[0]
    if not root or symbol(root[0], f"{path}: root") != (
        "gslt-support-transform-language-v1"
    ):
        raise ProfileError(f"{path}: expected gslt-support-transform-language-v1")

    known = {
        "name",
        "profile",
        "carrier",
        "work-shell",
        "scheduler",
        "unsupported",
        "transition",
        "input-compat",
        "input-explicit",
        "output-compat",
        "output-explicit",
        "fuel",
        "completion",
        "observation",
    }
    fields: dict[str, tuple[sx.SExpr, ...]] = {}
    for offset, raw in enumerate(root[1:], start=1):
        item = field(raw, f"{path}: field {offset}")
        tag = symbol(item[0], f"{path}: field {offset} tag")
        if tag not in known:
            raise ProfileError(f"{path}: unknown field {tag}")
        if tag in fields:
            raise ProfileError(f"{path}: duplicate field {tag}")
        fields[tag] = item
    missing = known - fields.keys()
    if missing:
        raise ProfileError(f"{path}: missing fields {', '.join(sorted(missing))}")

    exact_field(fields, "carrier", ("support",))
    exact_field(
        fields,
        "scheduler",
        ("least-mork-compact-expression-key-v1",),
    )
    exact_field(fields, "unsupported", ("leave-inert",))
    exact_field(
        fields,
        "transition",
        (
            "consume-selected",
            "snapshot-includes-selected",
            "relational-product-may-reuse-support",
            "stage-all-matches-per-sink",
            "finalize-sinks-left-to-right",
        ),
    )
    exact_field(fields, "fuel", ("exact-upper-bound",))
    exact_field(fields, "completion", ("no-supported-work",))
    exact_field(fields, "observation", ("support",))

    work = fields["work-shell"]
    if len(work) != 6 or not all(isinstance(item, int) for item in work[2:]):
        raise ProfileError("work-shell: expected symbol, arity, and three positions")
    work_arity, location_position, input_position, output_position = work[2:]
    if work_arity != 3:
        raise ProfileError("work-shell: V1 requires exactly three arguments")
    positions = (location_position, input_position, output_position)
    if sorted(positions) != list(range(work_arity)):
        raise ProfileError("work-shell: positions must be a permutation of 0, 1, 2")

    input_explicit = fields["input-explicit"]
    output_explicit = fields["output-explicit"]
    if len(input_explicit) < 3 or len(output_explicit) < 3:
        raise ProfileError("explicit input/output fields require a head symbol")
    sources = operator_entries(input_explicit[2:], "input-explicit", "source")
    sinks = operator_entries(output_explicit[2:], "output-explicit", "sink")

    def singleton(tag: str) -> str:
        value = fields[tag]
        if len(value) != 2:
            raise ProfileError(f"{tag}: expected one value")
        return symbol(value[1], tag)

    def compat(tag: str) -> tuple[str, str]:
        value = fields[tag]
        if len(value) != 3:
            raise ProfileError(f"{tag}: expected surface and operator")
        return symbol(value[1], tag), symbol(value[2], tag)

    compat_input_symbol, compat_input_operator = compat("input-compat")
    compat_output_symbol, compat_output_operator = compat("output-compat")

    return Profile(
        language_name=singleton("name"),
        profile_name=singleton("profile"),
        work_symbol=symbol(work[1], "work-shell symbol"),
        work_arity=work_arity,
        location_position=location_position,
        input_position=input_position,
        output_position=output_position,
        compat_input_symbol=compat_input_symbol,
        compat_input_operator_id=compat_input_operator,
        explicit_input_symbol=symbol(input_explicit[1], "input-explicit symbol"),
        sources=sources,
        compat_output_symbol=compat_output_symbol,
        compat_output_operator_id=compat_output_operator,
        explicit_output_symbol=symbol(output_explicit[1], "output-explicit symbol"),
        sinks=sinks,
    )


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def physical_profile_packet(
    profile: Profile, manifest_digest: str, compiler_digest: str
) -> bytes:
    packet = bytearray(b"CSTP")
    packet.extend(struct.pack(">HH", 1, 0))

    def add_text(value: str) -> None:
        encoded = value.encode("utf-8")
        if not encoded or len(encoded) > 0xFFFF:
            raise ProfileError("physical profile strings must be 1..65535 bytes")
        packet.extend(struct.pack(">H", len(encoded)))
        packet.extend(encoded)

    for value in (
        profile.language_name,
        profile.profile_name,
        manifest_digest,
        compiler_digest,
        profile.work_symbol,
        profile.compat_input_symbol,
        profile.compat_input_operator_id,
        profile.explicit_input_symbol,
        profile.compat_output_symbol,
        profile.compat_output_operator_id,
        profile.explicit_output_symbol,
    ):
        add_text(value)
    packet.extend(
        bytes(
            (
                profile.work_arity,
                profile.location_position,
                profile.input_position,
                profile.output_position,
                1,
                1,
            )
        )
    )

    def add_declarations(declarations: tuple[OperatorDecl, ...]) -> None:
        if len(declarations) > 0xFFFF:
            raise ProfileError("physical profile has too many declarations")
        packet.extend(struct.pack(">H", len(declarations)))
        for declaration in declarations:
            add_text(declaration.surface_symbol)
            packet.append(declaration.argument_count)
            add_text(declaration.operator_id)

    add_declarations(profile.sources)
    add_declarations(profile.sinks)
    return bytes(packet)


def c_byte_array(data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 12):
        rows.append(
            "    " + ", ".join(f"0x{value:02x}" for value in data[offset:offset + 12])
        )
    return ",\n".join(rows)


def c_declarations(
    symbol_name: str, suffix: str, declarations: tuple[OperatorDecl, ...]
) -> str:
    entries = "\n".join(
        "    {"
        f".surface_symbol = {c_string(declaration.surface_symbol)}, "
        f".argument_count = {declaration.argument_count}u, "
        f".operator_id = {c_string(declaration.operator_id)}"
        "},"
        for declaration in declarations
    )
    return (
        f"static const CettaGsltSupportOperatorDeclV1 "
        f"{symbol_name}_{suffix}[] = {{\n{entries}\n}};"
    )


def emit(
    manifest_path: Path,
    header_path: Path,
    source_path: Path,
    symbol_name: str,
    header_include: str,
) -> None:
    profile = parse_profile(manifest_path)
    digest = sha256(manifest_path.read_bytes()).hexdigest()
    schema_path = Path(sx.__file__).resolve()
    compiler_identity = sha256()
    compiler_identity.update(b"CeTTaGsltSupportTransformCompilerV1\0")
    compiler_identity.update(Path(__file__).resolve().read_bytes())
    compiler_identity.update(b"\0schema\0")
    compiler_identity.update(schema_path.read_bytes())
    compiler_identity.update(b"\0native-abi\0gslt-support-transform-v1")
    compiler_digest = compiler_identity.hexdigest()
    packet = physical_profile_packet(profile, digest, compiler_digest)
    guard = "CETTA_GENERATED_" + "".join(
        char.upper() if char.isalnum() else "_" for char in symbol_name
    ) + "_H"
    header = f"""#ifndef {guard}
#define {guard}

#include \"gslt_support_transform_runtime.h\"

extern const CettaGsltSupportTransformProfileV1 {symbol_name};

#endif /* {guard} */
"""
    values = {
        "language_name": profile.language_name,
        "profile_name": profile.profile_name,
        "work_symbol": profile.work_symbol,
        "compat_input_symbol": profile.compat_input_symbol,
        "compat_input_operator_id": profile.compat_input_operator_id,
        "explicit_input_symbol": profile.explicit_input_symbol,
        "compat_output_symbol": profile.compat_output_symbol,
        "compat_output_operator_id": profile.compat_output_operator_id,
        "explicit_output_symbol": profile.explicit_output_symbol,
    }
    assignments = "\n".join(
        f"    .{name} = {c_string(value)}," for name, value in values.items()
    )
    source_declarations = c_declarations(symbol_name, "sources", profile.sources)
    sink_declarations = c_declarations(symbol_name, "sinks", profile.sinks)
    packet_name = f"{symbol_name}_physical_profile_packet"
    source = f"""#include \"{header_include}\"

{source_declarations}

{sink_declarations}

static const uint8_t {packet_name}[] = {{
{c_byte_array(packet)}
}};

const CettaGsltSupportTransformProfileV1 {symbol_name} = {{
    .abi_version = 1u,
{assignments}
    .manifest_sha256 = {c_string(digest)},
    .compiler_sha256 = {c_string(compiler_digest)},
    .work_arity = {profile.work_arity}u,
    .location_position = {profile.location_position}u,
    .input_position = {profile.input_position}u,
    .output_position = {profile.output_position}u,
    .scheduler = CETTA_GSLT_SUPPORT_SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION_KEY_V1,
    .unsupported_policy = CETTA_GSLT_SUPPORT_UNSUPPORTED_LEAVE_INERT,
    .source_declarations = {symbol_name}_sources,
    .source_declaration_count = {len(profile.sources)}u,
    .sink_declarations = {symbol_name}_sinks,
    .sink_declaration_count = {len(profile.sinks)}u,
    .physical_profile_packet = {packet_name},
    .physical_profile_packet_size = sizeof({packet_name}),
}};
"""
    header_path.write_text(header, encoding="utf-8")
    source_path.write_text(source, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    args = parser.parse_args()
    try:
        emit(
            args.manifest,
            args.header,
            args.source,
            args.symbol,
            args.header_include,
        )
    except (OSError, ProfileError, sx.ParseError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
