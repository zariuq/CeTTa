#!/usr/bin/env python3
"""Refresh HE runtime parity matrices from source.

This script keeps the CeTTa parity bookkeeping honest by:
1. extracting the HE grounded/user-callable runtime heads from the current
   hyperon-experimental Rust stdlib sources;
2. recomputing CeTTa layer detection from eval.c / grounded.c / stdlib.metta;
3. folding in the focused runtime_surface_audit coverage statuses when present;
4. rewriting both specs/he_runtime_impl_matrix.json and
   specs/he_runtime_3layer_matrix.json.

The grounded head inventory comes directly from HE source. The kernel and
stdlib inventories reuse the existing 3-layer matrix entry set so this first
checked refresh path fixes drift in status/layer claims without re-auditing the
 broader doc taxonomy.
"""

from __future__ import annotations

import json
import re
from collections import Counter
from datetime import date
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
HE_ROOT = WORKSPACE / "hyperon" / "hyperon-experimental"
HE_STDLIB = HE_ROOT / "lib" / "src" / "metta" / "runner" / "stdlib"
SPECS = ROOT / "specs"

IMPL_MATRIX_PATH = SPECS / "he_runtime_impl_matrix.json"
THREELAYER_MATRIX_PATH = SPECS / "he_runtime_3layer_matrix.json"
AUDIT_PATH = SPECS / "runtime_surface_audit.json"

EVAL_PATH = ROOT / "src" / "eval.c"
GROUNDED_PATH = ROOT / "src" / "grounded.c"
PARSER_PATH = ROOT / "src" / "parser.c"
COMPILE_PATH = ROOT / "src" / "compile.c"
STDLIB_PATH = ROOT / "lib" / "stdlib.metta"

HE_IMPL_FILES = [
    HE_STDLIB / "arithmetics.rs",
    HE_STDLIB / "atom.rs",
    HE_STDLIB / "core.rs",
    HE_STDLIB / "debug.rs",
    HE_STDLIB / "math.rs",
    HE_STDLIB / "module.rs",
    HE_STDLIB / "package.rs",
    HE_STDLIB / "space.rs",
    HE_STDLIB / "string.rs",
]

KERNEL_FALLBACK = [
    "eval", "evalc", "metta", "chain", "case", "switch", "collapse",
    "superpose", "match", "unify", "function", "return", "let", "let*",
    "new-space", "bind!", "add-atom", "remove-atom", "get-atoms",
    "new-state", "get-state", "change-state!", "context-space", "import!",
    "register-module!",
]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


SOURCE_TEXT = {
    "eval.c": read_text(EVAL_PATH),
    "grounded.c": read_text(GROUNDED_PATH),
    "parser.c": read_text(PARSER_PATH),
    "compile.c": read_text(COMPILE_PATH),
    "stdlib.metta": read_text(STDLIB_PATH),
}


def split_grouped_heads(label: str) -> list[str]:
    return [part.strip() for part in label.split("/") if part.strip()]


def load_audit_map() -> dict[str, dict]:
    data = json.loads(read_text(AUDIT_PATH))
    audit = {}
    for entry in data["entries"]:
        for head in split_grouped_heads(entry["head"]):
            audit[head] = entry
    return audit


def decode_regex_head(head: str) -> str:
    mapping = {
        r"\+": "+",
        r"\-": "-",
        r"\*": "*",
    }
    return mapping.get(head, head)


def extract_he_grounded_heads() -> list[tuple[str, str]]:
    seen = set()
    entries: list[tuple[str, str]] = []
    for path in HE_IMPL_FILES:
        text = read_text(path)
        module = path.name
        for match in re.finditer(r'grounded_op!\([^,]+,\s*"([^"]+)"\)', text):
            head = match.group(1)
            if (module, head) not in seen:
                seen.add((module, head))
                entries.append((module, head))
        for match in re.finditer(r'register_token\(regex\(r"([^"]+)"\)', text):
            head = decode_regex_head(match.group(1))
            if head == "True|False":
                continue
            if (module, head) not in seen:
                seen.add((module, head))
                entries.append((module, head))
    return entries


def contains_head(text: str, head: str) -> bool:
    return f'"{head}"' in text or f"(: {head} " in text or f"(@doc {head}" in text or f"(= ({head} " in text


def detect_impl_layers(head: str) -> list[str]:
    layers = []
    if contains_head(SOURCE_TEXT["eval.c"], head):
        layers.append("eval.c")
    if contains_head(SOURCE_TEXT["grounded.c"], head):
        layers.append("grounded.c")
    if contains_head(SOURCE_TEXT["parser.c"], head):
        layers.append("parser.c")
    if contains_head(SOURCE_TEXT["compile.c"], head):
        layers.append("compile.c")
    if contains_head(SOURCE_TEXT["stdlib.metta"], head):
        layers.append("stdlib.metta")
    return layers


def normalize_layers(layers: list[str]) -> list[str]:
    out = []
    for layer in layers:
        if layer == "eval.c":
            out.append("kernel_eval")
        elif layer == "grounded.c":
            out.append("grounded")
        elif layer == "parser.c":
            out.append("token_parser")
        elif layer == "compile.c":
            out.append("kernel_eval")
        elif layer == "stdlib.metta":
            out.append("stdlib")
    return out


def sanitize_inventory_head(head: str) -> str:
    if head.startswith("(@"):
        return head[1:]
    return head


def impl_status_for(head: str, layers: list[str], audit_map: dict[str, dict]) -> tuple[str, str]:
    audit = audit_map.get(head)
    if audit:
        status = audit["status"]
        if status == "covered":
            return "covered", audit["notes"]
        if status == "partial":
            return "partial", audit["notes"]
    if layers:
        return "implemented", "Detected in CeTTa source; dedicated audit entry not yet added."
    return "missing", "No current CeTTa implementation token detected."


def tier_status_for(head: str, layers: list[str], audit_map: dict[str, dict]) -> str:
    audit = audit_map.get(head)
    if audit:
        status = audit["status"]
        if status == "covered":
            return "implemented"
        if status == "partial":
            return "partial"
    return "implemented" if layers else "missing"


def load_existing_3layer_inventory() -> tuple[list[str], list[str]]:
    if THREELAYER_MATRIX_PATH.exists():
        data = json.loads(read_text(THREELAYER_MATRIX_PATH))
        kernel = []
        stdlib = []
        seen_kernel = set()
        seen_stdlib = set()
        for entry in data["entries"]:
            head = sanitize_inventory_head(entry["head"])
            if entry["tier"] == "kernel":
                if head not in seen_kernel:
                    seen_kernel.add(head)
                    kernel.append(head)
            elif entry["tier"] == "stdlib":
                if head not in seen_stdlib:
                    seen_stdlib.add(head)
                    stdlib.append(head)
        return kernel, stdlib
    return KERNEL_FALLBACK, []


def build_impl_matrix(audit_map: dict[str, dict]) -> dict:
    entries = []
    counts = Counter()
    for module, head in extract_he_grounded_heads():
        layers = detect_impl_layers(head)
        status, notes = impl_status_for(head, layers, audit_map)
        counts[status] += 1
        entries.append({
            "head": head,
            "he_module": module,
            "tier": "grounded",
            "cetta_status": status,
            "cetta_notes": notes,
            "cetta_layers": layers,
        })
    return {
        "generated_on": date.today().isoformat(),
        "scope": "HE Rust stdlib grounded/user-callable implementation matrix",
        "sources": [str(path) for path in HE_IMPL_FILES] + [
            str(EVAL_PATH), str(GROUNDED_PATH), str(PARSER_PATH), str(COMPILE_PATH), str(STDLIB_PATH), str(AUDIT_PATH)
        ],
        "summary": {
            "total_heads": len(entries),
            "covered": counts["covered"],
            "implemented": counts["implemented"],
            "missing": counts["missing"],
            "partial": counts["partial"],
        },
        "entries": entries,
    }


def build_3layer_matrix(audit_map: dict[str, dict], impl_matrix: dict) -> dict:
    kernel_heads, stdlib_heads = load_existing_3layer_inventory()
    entries = []
    summary: dict[str, Counter] = {
        "kernel": Counter(),
        "grounded": Counter(),
        "stdlib": Counter(),
    }

    def add_entry(head: str, tier: str, layers: list[str]) -> None:
        status = tier_status_for(head, layers, audit_map)
        summary[tier][status] += 1
        entries.append({
            "head": head,
            "tier": tier,
            "cetta_status": status,
            "cetta_layers": normalize_layers(layers),
        })

    for head in kernel_heads:
        add_entry(head, "kernel", detect_impl_layers(head))
    for entry in impl_matrix["entries"]:
        add_entry(entry["head"], "grounded", entry["cetta_layers"])
    for head in stdlib_heads:
        add_entry(head, "stdlib", detect_impl_layers(head))

    return {
        "generated_on": date.today().isoformat(),
        "scope": "HE 3-layer runtime overview (kernel / grounded / stdlib docs) cross-referenced against CeTTa",
        "sources": [
            str(THREELAYER_MATRIX_PATH if THREELAYER_MATRIX_PATH.exists() else STDLIB_PATH),
            str(IMPL_MATRIX_PATH),
            str(EVAL_PATH),
            str(GROUNDED_PATH),
            str(PARSER_PATH),
            str(COMPILE_PATH),
            str(STDLIB_PATH),
        ],
        "summary": {
            tier: {
                "total": len([e for e in entries if e["tier"] == tier]),
                "implemented": summary[tier]["implemented"],
                "missing": summary[tier]["missing"],
                "partial": summary[tier]["partial"],
            }
            for tier in ("kernel", "grounded", "stdlib")
        },
        "entries": entries,
    }


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def main() -> None:
    audit_map = load_audit_map()
    impl_matrix = build_impl_matrix(audit_map)
    write_json(IMPL_MATRIX_PATH, impl_matrix)
    three = build_3layer_matrix(audit_map, impl_matrix)
    write_json(THREELAYER_MATRIX_PATH, three)


if __name__ == "__main__":
    main()
