#!/usr/bin/env python3
"""Apply the native elaborator's scope migration plan without reformatting source.

The span reader locates syntax only. It makes no binding or ownership decisions.
Native analysis checks that the input stays unchanged; edits validate node shapes.
Literal-code ownership diagnostics are separate from executable-source edits.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


@dataclass
class Node:
    start: int
    end: int
    word: str = ""
    children: tuple[Node, ...] = ()
    synthetic: bool = False

    @property
    def head(self):
        return self.children[0].word if self.children else ""


class SpanReader:
    """Source spans with the compiled reader's prefix/list/meta projection."""
    def __init__(self, text):
        self.text, self.pos = text, 0

    def skip(self):
        while self.pos < len(self.text):
            if self.text[self.pos].isspace():
                self.pos += 1
            elif self.text[self.pos] == ";":
                p = self.text.find("\n", self.pos)
                self.pos = len(self.text) if p < 0 else p + 1
            else:
                break

    def tag(self, word, at):
        return Node(at, at, word, synthetic=True)

    def atom(self, *, suffix=True, in_list=False):
        self.skip()
        start = self.pos
        if start >= len(self.text):
            raise ValueError("unexpected end of source")
        c = self.text[self.pos]
        prefix = None
        if self.text.startswith("$@", start):
            prefix = "syn:var"
            self.pos += 2
        elif self.text.startswith("&@", start):
            prefix = "syn:ref"
            self.pos += 2
        elif c == "@":
            prefix = "quote"
            self.pos += 1
        elif c == "*" and start + 1 < len(self.text) and self.text[start + 1] in "(@$&*":
            prefix = "unquote"
            self.pos += 1
        if prefix:
            payload = self.atom(suffix=False, in_list=in_list)
            if prefix in ("syn:var", "syn:ref"):
                payload = Node(start, payload.end, children=(self.tag("quote", start), payload), synthetic=True)
            node = Node(start, self.pos, children=(self.tag(prefix, start), payload))
        elif c in "([{":
            self.pos += 1
            close = {"(": ")", "[": "]", "{": "}"}[c]
            parts = []
            rest = None
            while True:
                self.skip()
                if self.pos >= len(self.text):
                    raise ValueError(f"unclosed {c} at {start}")
                if self.text[self.pos] == close:
                    self.pos += 1
                    break
                if c == "[" and self.text[self.pos] == "|" and (
                    self.pos + 1 == len(self.text) or self.text[self.pos + 1].isspace()
                    or self.text[self.pos + 1] in '()[]{};"'
                ):
                    self.pos += 1
                    rest = self.atom(in_list=True)
                    self.skip()
                    if self.pos >= len(self.text) or self.text[self.pos] != close:
                        raise ValueError("list rest must be last")
                    self.pos += 1
                    break
                parts.append(self.atom(in_list=c == "["))
            if c == "[":
                if rest and rest.head in ("#list", "#list-rest"):
                    parts.extend(rest.children[1:])
                    tag = rest.head
                else:
                    tag = "#list-rest" if rest else "#list"
                    if rest:
                        parts.append(rest)
                parts.insert(0, self.tag(tag, start))
            elif c == "{":
                parts.insert(0, self.tag("#braces", start))
            node = Node(start, self.pos, children=tuple(parts))
        elif c == '"':
            self.pos += 1
            while self.pos < len(self.text) and self.text[self.pos] != '"':
                self.pos += 2 if self.text[self.pos] == "\\" else 1
            if self.pos >= len(self.text):
                raise ValueError("unclosed string")
            self.pos += 1
            node = Node(start, self.pos, self.text[start:self.pos])
        elif c in ")]}":
            raise ValueError(f"unexpected {c} at {start}")
        else:
            while self.pos < len(self.text) and not self.text[self.pos].isspace() and self.text[self.pos] not in '()[]{};"':
                self.pos += 1
            node = Node(start, self.pos, self.text[start:self.pos])
        while suffix and self.pos < len(self.text) and self.text[self.pos] in "[{":
            argument = self.atom(suffix=False)
            node = Node(start, self.pos, children=(self.tag("meta", start), node, argument))
        return node

    def document(self):
        result = []
        self.skip()
        while self.pos < len(self.text):
            result.append(self.atom())
            self.skip()
        return result


def native_plan(binary: Path, source: Path, timeout=120):
    source_digest = hashlib.sha256(source.read_bytes()).digest()
    env = dict(os.environ, CETTA_PRIME_SCOPE_CENSUS="plan",
               CETTA_PRIME_SCOPE_DEFAULT="mercury-implicit/per-call/reference")
    run = subprocess.run([str(binary), "--lang", "prime", str(source)],
                         capture_output=True, text=True, env=env, timeout=timeout)
    if run.returncode:
        raise ValueError(f"native analysis failed ({run.returncode}): {run.stderr.strip()}")
    if hashlib.sha256(source.read_bytes()).digest() != source_digest:
        raise ValueError("source changed during native analysis")
    records = [json.loads(line) for line in run.stdout.splitlines()]
    if not records and not SpanReader(source.read_text()).document():
        records = [{"kind": "complete", "forms": 0}]
    if not records or records[-1].get("kind") != "complete":
        raise ValueError("native analysis did not complete")
    return records


def translate(text: str, records):
    if not any(r.get("kind") in ("cross", "new") for r in records):
        return text, [], records[:-1]
    forms = SpanReader(text).document()
    if records[-1] != {"kind": "complete", "forms": len(forms)}:
        raise ValueError("source span reader disagrees with native form count")
    inserts: dict[int, list[tuple[int, str]]] = {}
    diagnostics = []
    edits = []
    for record in records[:-1]:
        node = forms[record["form"]]
        for index in record["path"]:
            node = node.children[index]
        if len(node.children) != record["arity"] or node.head != record["head"]:
            raise ValueError(f"source span disagrees with native shape: {record}")
        if record["kind"] not in ("cross", "new"):
            diagnostics.append(record)
            continue
        if node.synthetic:
            raise ValueError("cannot edit a synthetic node")
        names = " ".join(record["names"])
        if not names or any(not n.startswith("$") for n in record["names"]):
            raise ValueError("native plan contains an invalid variable spelling")
        depth = len(record["path"])
        if record["kind"] == "cross":
            inserts.setdefault(node.end, []).append((-depth * 2, "{" + names + "}"))
        else:
            inserts.setdefault(node.start, []).append((depth * 2, "(new (" + names + ") "))
            inserts.setdefault(node.end, []).append((-depth * 2 + 1, ")"))
        edits.append(dict(record, start=node.start, end=node.end))
    out = text
    for position in sorted(inserts, reverse=True):
        addition = "".join(s for _, s in sorted(inserts[position], key=lambda x: x[0]))
        out = out[:position] + addition + out[position:]
    return out, edits, diagnostics


def library_spellings(binary: Path, root: Path, *, check: bool):
    """Generate the lexical spelling, leaving other dialect sources intact."""
    manifest_path = root / "prime" / "scope_migration.json"
    managed = json.loads(manifest_path.read_text()) if manifest_path.exists() else {"libraries": []}
    owned = {r["output"] for r in managed["libraries"]}
    libraries = []
    delegated = []
    pending = []
    for source in sorted(root.rglob("*.metta")):
        relative = source.relative_to(root)
        if relative.parts[0] in ("petta", "prime"):
            continue
        output = root / "prime" / relative
        if output.exists() and str(relative) not in owned and output.read_text().startswith(
                "; Generated by tools/library_spelling.py from the shared source;"):
            # This generator changes the native handoff protocol, before
            # scope migration. Its own regeneration gate owns the output.
            from library_spelling import spelling
            expected = spelling(source.read_text(), "prime")
            if output.read_text() != expected:
                raise ValueError(f"independent library spelling is stale: {output}")
            delegated.append({"source": str(relative), "generator": "library_spelling.py",
                              "output_sha256": hashlib.sha256(expected.encode()).hexdigest()})
            continue
        text = source.read_text()
        records = native_plan(binary, source)
        translated, edits, diagnostics = translate(text, records)
        if not edits and str(relative) not in owned:
            continue
        output = root / "prime" / relative
        name = str(relative)
        header = ("; Generated by tools/prime_scope_migration.py from lib/" + name + "; do not edit.\n"
                  "(scope:profile lexical-fresh per-call reference)\n\n")
        generated = header + translated
        if output.exists() and name not in owned and output.read_text() != generated:
            raise ValueError(f"refusing to overwrite independently authored library: {output}")
        if check:
            if not output.exists() or output.read_text() != generated:
                raise ValueError(f"generated scope spelling is stale: {output}")
        else:
            pending.append((output, generated))
        libraries.append({"source": name, "output": name,
                          "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
                          "output_sha256": hashlib.sha256(generated.encode()).hexdigest(),
                          "edits": len(edits), "code_diagnostics": len(diagnostics)})
    manifest = {"schema": "prime-scope-library-spellings-v1", "libraries": libraries,
                "delegated": delegated}
    if check:
        if managed != manifest:
            raise ValueError("scope library manifest does not match the complete source census")
    else:
        missing = owned - {r["output"] for r in libraries}
        if missing:
            raise ValueError(f"previous generated libraries need review: {sorted(missing)}")
        for output, generated in pending:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(generated)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, nargs="?")
    ap.add_argument("--libraries", type=Path)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--receipt", type=Path)
    args = ap.parse_args()
    if args.libraries:
        manifest = library_spellings(args.binary.resolve(), args.libraries.resolve(), check=args.check)
        print(f"Scope library spellings: {len(manifest['libraries'])} checked" if args.check else f"Scope library spellings: {len(manifest['libraries'])} generated")
        return
    if not args.source:
        ap.error("source or --libraries is required")
    original = args.source.read_text()
    records = native_plan(args.binary.resolve(), args.source.resolve())
    translated, edits, diagnostics = translate(original, records)
    receipt = {"source_sha256": hashlib.sha256(original.encode()).hexdigest(),
               "translated_sha256": hashlib.sha256(translated.encode()).hexdigest(),
               "binary_sha256": hashlib.file_digest(args.binary.open("rb"), "sha256").hexdigest(),
               "edits": edits, "diagnostics": diagnostics}
    if args.output:
        args.output.write_text(translated)
    else:
        sys.stdout.write(translated)
    if args.receipt:
        args.receipt.write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
