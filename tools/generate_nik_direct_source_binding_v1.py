#!/usr/bin/env python3
"""Generate static source-binding metadata for a direct NIK authority.

The generated descriptor is admission/qualification data.  It does not embed
or execute the authored presentation and it does not assert that a fragment
describes the authority completely.
"""

from __future__ import annotations

from hashlib import sha256
from pathlib import Path
from typing import Sequence
import argparse
import json
import re

import gslt2parse_schema_v1 as schema


_C_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def require_identifier(value: str, label: str) -> str:
    if not _C_IDENTIFIER.fullmatch(value):
        raise ValueError(f"{label} must be a C identifier")
    return value


def header_guard(prefix: str) -> str:
    return "CETTA_GENERATED_" + prefix.upper() + "_SOURCE_BINDING_V1_H"


def render_header(prefix: str) -> str:
    guard = header_guard(prefix)
    return (
        f"#ifndef {guard}\n"
        f"#define {guard}\n\n"
        '#include "nik_direct_authority.h"\n\n'
        f"extern const CettaNikDirectSourceBindingV1\n"
        f"    {prefix}_source_binding_v1;\n\n"
        f"#endif /* {guard} */\n"
    )


def render_source(
    *,
    prefix: str,
    generated_header: str,
    authority_header: str,
    authority_symbol: str,
    presentation_id: str,
    semantic_scope: str,
    source_digest: str,
    package_digest: str,
    coverage: str,
) -> str:
    coverage_symbol = {
        "fragment": "CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT",
        "complete": "CETTA_NIK_DIRECT_SOURCE_COMPLETE_PRESENTATION",
    }[coverage]
    return (
        f'#include "{generated_header}"\n'
        f'#include "{authority_header}"\n\n'
        f"const CettaNikDirectSourceBindingV1\n"
        f"    {prefix}_source_binding_v1 = {{\n"
        f"        .authority = &{authority_symbol},\n"
        f"        .schema_id = \"finite-horn-gslt-v1\",\n"
        f"        .presentation_id = {c_string(presentation_id)},\n"
        f"        .semantic_scope = {c_string(semantic_scope)},\n"
        f"        .source_sha256 = {c_string(source_digest)},\n"
        f"        .package_sha256 = {c_string(package_digest)},\n"
        f"        .coverage = {coverage_symbol},\n"
        f"    }};\n"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--presentation", required=True, type=Path)
    parser.add_argument("--symbol-prefix", required=True)
    parser.add_argument("--authority-symbol", required=True)
    parser.add_argument("--authority-header", required=True)
    parser.add_argument("--semantic-scope", required=True)
    parser.add_argument("--coverage", choices=("fragment", "complete"), required=True)
    parser.add_argument("--header-output", required=True, type=Path)
    parser.add_argument("--source-output", required=True, type=Path)
    args = parser.parse_args(argv)

    try:
        prefix = require_identifier(args.symbol_prefix, "symbol prefix")
        authority_symbol = require_identifier(
            args.authority_symbol, "authority symbol"
        )
        presentations = schema.admit([args.presentation])
        if len(presentations) != 1:
            raise ValueError("exactly one finite-Horn presentation is required")
        presentation = presentations[0]
        source_digest = sha256(args.presentation.read_bytes()).hexdigest()
        package_digest = schema.package_digest(presentations)
        generated_header = args.header_output.name
        header = render_header(prefix)
        source = render_source(
            prefix=prefix,
            generated_header=f"generated/{generated_header}",
            authority_header=args.authority_header,
            authority_symbol=authority_symbol,
            presentation_id=presentation.name,
            semantic_scope=args.semantic_scope,
            source_digest=source_digest,
            package_digest=package_digest,
            coverage=args.coverage,
        )
    except (OSError, ValueError, schema.SchemaError) as error:
        parser.exit(1, f"direct source binding rejected: {error}\n")

    args.header_output.write_text(header, encoding="utf-8")
    args.source_output.write_text(source, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
