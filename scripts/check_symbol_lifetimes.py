#!/usr/bin/env python3
"""Check typed SymbolId holders against an authored lifetime presentation.

Clang's AST supplies macro-expanded fields, typedef aliases and static locals.
This is a declaration check for the selected build, not proof of complete GC
roots: erased integer handles, Atom pointers and foreign borrows need separate
audits. `unresolved` is an explicit blocker for destructive collection.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

from gen_semiring_fixtures import parse_forms

ROOT = Path(__file__).resolve().parents[1]
CLASSES = {"owned-vocabulary", "program-structure", "traced-atom",
           "compare-only-cache", "transient", "weak-index", "unresolved"}


def descendants(node):
    yield node
    for child in node.get("inner", []):
        yield from descendants(child)


def ast_holders(ast, root: Path):
    nodes = list(descendants(ast))
    function_aliases = {node["name"] for node in nodes if node.get("kind") == "TypedefDecl"
                        and any(t.get("kind") in ("FunctionProtoType", "FunctionNoProtoType")
                                for t in descendants(node))}
    aliases = {"SymbolId"}
    while True:
        before = len(aliases)
        for node in nodes:
            if node.get("kind") == "TypedefDecl" and node.get("name") not in function_aliases:
                spelling = node.get("type", {}).get("qualType", "")
                if set(re.findall(r"\w+", spelling)) & aliases:
                    aliases.add(node["name"])
        if len(aliases) == before:
            break

    record_names = {}
    for node in nodes:
        if node.get("kind") == "TypedefDecl":
            for typ in descendants(node):
                record = typ.get("ownedTagDecl", {})
                if record.get("kind") == "RecordDecl":
                    record_names[record["id"]] = node["name"]

    def offset(location):
        return location.get("expansionLoc", location).get("offset")

    def name_nested_records(parent, owner=None):
        if parent.get("kind") == "RecordDecl":
            owner = parent.get("name") or record_names.get(parent.get("id")) or owner
        elif parent.get("kind") == "FunctionDecl":
            owner = "function:" + parent.get("name", "?")
        children = parent.get("inner", [])
        for child in children:
            if child.get("kind") != "RecordDecl" or child.get("name") or child.get("id") in record_names:
                continue
            start = offset(child.get("range", {}).get("begin", {}))
            members = [sibling["name"] for sibling in children
                       if sibling.get("kind") in ("FieldDecl", "VarDecl") and sibling.get("name")
                       and start is not None
                       and offset(sibling.get("range", {}).get("begin", {})) == start]
            if members:
                # Distinguish same-shaped anonymous records by their actual
                # owning members, not source line numbers or field names alone.
                record_names[child["id"]] = (owner + "." if owner else "anonymous:") + ",".join(members)
        for child in children:
            name_nested_records(child, owner)

    name_nested_records(ast)

    # Clang omits repeated file names. Its top-level declarations still switch
    # that state; children inherit the owning declaration's file. Macro fields
    # use expansion locations, so a header-generating macro stays with its type.
    current_file = None
    rows = {}

    def walk(node, parent_file=None, record=None, function=None):
        nonlocal current_file
        loc = node.get("loc", {})
        loc = loc.get("expansionLoc", loc)
        explicit = loc.get("file")
        if explicit:
            current_file = explicit
        filename = explicit or parent_file or current_file
        kind = node.get("kind")
        if kind == "FunctionDecl":
            function = node.get("name", "?")
        if kind == "RecordDecl":
            record = node.get("name") or record_names.get(node.get("id"))
            if not record:
                fields = [n.get("name", "_") for n in node.get("inner", [])
                          if n.get("kind") == "FieldDecl"]
                record = "anonymous[" + ",".join(fields) + "]"
        spelling = node.get("type", {}).get("qualType", "")
        typed = bool(set(re.findall(r"\w+", spelling)) & aliases)
        # A callback mentioning SymbolId in its signature stores a function
        # pointer, not an ID. Atomic-qualified value IDs remain actual holders.
        non_atomic = re.sub(r"_Atomic\(([^()]*)\)", r"\1", spelling)
        # Pointer-to-array IDs, e.g. SymbolId (*)[8], are holders too. A
        # function pointer instead has an argument list after its declarator.
        typed = typed and not re.search(r"\)\s*\(", non_atomic)
        is_field = kind == "FieldDecl" and record is not None
        is_static = kind == "VarDecl" and (
            function is None or node.get("storageClass") == "static"
            or node.get("tlsKind"))
        if typed and (is_field or is_static) and filename:
            path = Path(filename)
            if not path.is_absolute():
                path = root / path
            try:
                relative = str(path.resolve().relative_to(root.resolve()))
            except ValueError:
                relative = None
            if relative and relative.startswith(("src/", "native/", "experiments/", "runtime/generated/")):
                owner = record if is_field else "static:" + (function + ":" if function else "")
                member = owner + "." + node.get("name", "?")
                key = relative + "::" + member
                position = loc.get("offset")
                previous = rows.get(key)
                if previous is not None and previous.get("offset") != position:
                    raise ValueError("ambiguous holder identity; name its owning type: " + key)
                rows[key] = {"file": relative, "member": member, "type": spelling,
                             "offset": position}
        for child in node.get("inner", []):
            walk(child, filename, record, function)

    # Each root sibling must be allowed to change the inherited source file.
    for node in ast.get("inner", []):
        walk(node)
    return rows


def declarations(path: Path):
    forms = parse_forms(path.read_text())
    matching = [form for form in forms if isinstance(form, list) and
                form[:2] == ["gslt", "symbol-holder-lifetimes"]]
    if len(matching) != 1:
        raise ValueError("expected exactly one symbol-holder-lifetimes presentation")
    rules = {}
    for form in matching[0][2:]:
        if not isinstance(form, list) or not form or form[0] != "holder":
            continue
        if len(form) != 4 or form[3] not in CLASSES:
            raise ValueError(f"invalid holder declaration: {form}")
        key = form[1] + "::" + form[2]
        if key in rules:
            raise ValueError("duplicate holder declaration: " + key)
        rules[key] = form[3]
    return rules


def classify(rows, rules, require_resolved=False):
    missing, blocked = [], []
    for key, row in sorted(rows.items()):
        lifetime = rules.get(key)
        if lifetime is None:
            # Only an explicit whole-type declaration may cover generated
            # vocabulary fields. There is no file-wide default.
            owner_key = key.rsplit(".", 1)[0] + ".*"
            lifetime = rules.get(owner_key)
        if lifetime is None:
            missing.append(key)
        else:
            row["lifetime"] = lifetime
            if lifetime == "unresolved":
                blocked.append(key)
    if missing:
        raise ValueError("unclassified SymbolId holders:\n" + "\n".join(missing))
    if require_resolved and blocked:
        raise ValueError("unresolved SymbolId holders prohibit sweeping:\n" + "\n".join(blocked))
    return blocked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--cppflags", default="-Isrc -I.")
    parser.add_argument("--sources", nargs="+", required=True)
    parser.add_argument("--spec", type=Path, default=ROOT / "lib/symbol_lifetimes.metta")
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/symbol-holder-inventory.json")
    parser.add_argument("--scan-only", action="store_true")
    parser.add_argument("--require-resolved", action="store_true")
    args = parser.parse_args()
    cache = ROOT / "runtime/symbol-holder-ast-cache"
    cache.mkdir(parents=True, exist_ok=True)
    # Headers and included C fragments can change any translation unit,
    # including macro expansions and declarations moved out of a .c file.
    headers = hashlib.sha256()
    for base in ("src", "native", "experiments/gslt2parse_foundation/native", "runtime/bootstrap"):
        for path in sorted(path for path in (ROOT / base).rglob("*")
                           if path.suffix in {".h", ".inc"}):
            headers.update(str(path.relative_to(ROOT)).encode())
            headers.update(path.read_bytes())
    compiler = subprocess.check_output([args.clang, "--version"], text=True).splitlines()[0]
    config = "census-v5" + headers.hexdigest() + compiler + args.cppflags
    rows = {}
    for source in args.sources:
        path = ROOT / source
        digest = hashlib.sha256(config.encode() + source.encode() + path.read_bytes()).hexdigest()
        cached = cache / (digest + ".json")
        if cached.exists():
            found = json.loads(cached.read_text())
        else:
            command = [args.clang, *shlex.split(args.cppflags), "-std=c11", "-pthread", "-w",
                       "-fsyntax-only", "-Xclang", "-ast-dump=json", source]
            with tempfile.TemporaryFile(mode="w+", dir=cache) as output:
                result = subprocess.run(command, cwd=ROOT, stdout=output,
                                        stderr=subprocess.PIPE, text=True)
                if result.returncode:
                    raise ValueError("AST compilation failed for " + source + ":\n" + result.stderr)
                output.seek(0)
                found = ast_holders(json.load(output), ROOT)
            cached.write_text(json.dumps(found, sort_keys=True))
        rows.update(found)
    blocked = [] if args.scan_only else classify(rows, declarations(args.spec), args.require_resolved)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"compiler": compiler, "holders": list(rows.values()),
                                      "classification_checked": not args.scan_only,
                                      "unresolved_count": None if args.scan_only else len(blocked)},
                                     indent=2) + "\n")
    state = "classification pending" if args.scan_only else f"{len(blocked)} unresolved"
    print(f"Symbol holder census: {len(rows)} typed declarations; {state}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
