#!/usr/bin/env python3
"""Measure the authored examples and retained data, without timing claims."""
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent


def expression_at(text, start):
    """Extract a balanced expression, retaining its actual whitespace."""
    start = text.index("(", start)
    depth, quoted, escaped = 0, False, False
    for index in range(start, len(text)):
        char = text[index]
        if quoted:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise ValueError("Unclosed measured expression")


def main():
    source = (HERE / "declaration-packages.metta").read_text()
    measurements, position = [], 0
    for family in ("Nat", "Vec"):
        start = source.index("((lam family", position)
        builder = expression_at(source, start)
        start = source.index("(quote (Declarations", start)
        block = expression_at(source, start)
        position = start + len(block)
        for form, code in (("builder call", builder), ("complete block", block)):
            measurements.append({
                "family": family, "authoring_form": form,
                "utf8_bytes_including_whitespace": len(code.encode()),
                "tokens_including_parentheses": len(re.findall(r"\(|\)|[^\s()]+", code)),
                "quote_forms": code.count("(quote "), "type_rows": code.count("(: "),
            })
    print(json.dumps({
        "authored_examples": measurements,
        "shared_builder_implementation_bytes": (HERE / "declaration-builders.metta").stat().st_size,
        "generated_comparison_bytes": {
            name: (HERE / name).stat().st_size for name in
            ("generated-family-packages.metta", "named-readback-generated.metta")
        },
        "scope": "Source size and quotation counts; library work is explicit. No runtime benchmark.",
    }, indent=2))


if __name__ == "__main__":
    main()
