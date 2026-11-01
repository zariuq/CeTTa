#!/usr/bin/env python3
"""Inventory every route in the observer and coordinate-existence gates.

The executable hash selects a reviewed probe profile. Source anchors describe
the working-source obligation map and are not asserted to be the binary's
build inputs. Named-symbol, inlined-checkpoint and operation-trace activation
remain distinct evidence kinds.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

from check_native_observers import ROOT, save_json, sha256
from native_observer_cases import FAMILIES, SOURCE_ANCHORS
from native_observer_probes import CHECKPOINTS, COLLECT, FUNCTIONS, REVIEWED_SHA256S

EXTERNAL = {
    "match-existence-partition": {
        "source": "src/space_match_backend.c",
        "anchor": "space_match_exists_flat_linear_view64",
        "language": "he",
        "driver": "check_existence_domains.py",
        "symbols": ["space_match_exists_flat_linear_view64",
                    "space_match_exists_ground_exact_expression_coordinates"],
    },
}


def route_table():
    routes = {}
    for family in FAMILIES:
        source, anchor = SOURCE_ANCHORS[family]
        if family in FUNCTIONS:
            evidence = {"kind": "named-return", "symbols": list(FUNCTIONS[family])}
        elif family in CHECKPOINTS:
            evidence = {"kind": "inlined-checkpoint", "owner": COLLECT,
                        "checkpoints": [dict(item) for item in CHECKPOINTS[family]]}
        elif family == "row-takeover":
            evidence = {"kind": "operation-trace", "symbols": ["space_dense_promote"]}
        else:
            raise AssertionError("route lacks an evidence contract: " + family)
        routes[family] = {"source": source, "anchor": anchor, "language": "petta",
                          "driver": "check_native_observers.py", "evidence": evidence}
    for name, item in EXTERNAL.items():
        routes[name] = {"source": item["source"], "anchor": item["anchor"],
                        "language": item["language"], "driver": item["driver"],
                        "evidence": {"kind": "partitioned-caller-chain", "symbols": item["symbols"]}}
    return routes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    identity = sha256(binary)
    if identity not in REVIEWED_SHA256S:
        raise SystemExit("no reviewed route profile for this executable")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    result = subprocess.run(["nm", "-n", str(binary)], capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        raise SystemExit("nm could not read the reviewed executable")
    symbols = {line.split()[-1] for line in result.stdout.splitlines() if len(line.split()) >= 3}
    notes = subprocess.run(["readelf", "-n", str(binary)], capture_output=True, text=True, timeout=30)
    version = subprocess.run([str(binary), "--version"], capture_output=True, text=True, timeout=30)
    build_id = re.search(r"Build ID: ([0-9a-f]+)", notes.stdout)
    if notes.returncode != 0 or version.returncode != 0 or not build_id:
        raise SystemExit("could not read executable build identity")
    routes = route_table()
    for name, route in routes.items():
        path = ROOT / route["source"]
        route["source_sha256"] = sha256(path)
        route["anchor_present"] = route["anchor"] in path.read_text()
        required = route["evidence"].get("symbols", [])
        if route["evidence"]["kind"] == "inlined-checkpoint":
            required = [route["evidence"]["owner"]]
        route["binary_symbols_present"] = {symbol: symbol in symbols for symbol in required}
        route["qualified_inventory_entry"] = route["anchor_present"] and all(route["binary_symbols_present"].values())
    summary = {"binary_sha256": identity, "elf_build_id": build_id.group(1),
               "version": version.stdout.strip(), "binary_unchanged": sha256(binary) == identity,
               "route_count": len(routes), "routes": routes,
               "boundary": "Working-source anchors plus hash-pinned binary symbols; behavior is in the named drivers."}
    save_json(out / "inventory.json", summary)
    print(json.dumps(summary, indent=2))
    return 0 if summary["binary_unchanged"] and all(r["qualified_inventory_entry"] for r in routes.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
