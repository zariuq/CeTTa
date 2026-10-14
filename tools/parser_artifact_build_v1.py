#!/usr/bin/env python3
"""Build portable parsers and an embeddable catalog through CeTTa's compiler."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from gslt2parse_schema_v1 import Symbol, parse_sexprs, render
from parser_artifact_publication_v1 import publish_file, publish_generation

ROOT = Path(__file__).resolve().parents[1]


def form(head, *args):
    return "(" + " ".join((head, *args)) + ")"


def text(value):
    return json.dumps(str(value), ensure_ascii=False)


def lets(bindings, body):
    for name, value in reversed(bindings):
        body = form("let", name, value, body)
    return body


def headed(value, name, arity):
    return isinstance(value, tuple) and len(value) == arity + 1 and value[0] == Symbol(name)


def load_manifest(path):
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("format") != "ParserBuildManifestV1" or not isinstance(document.get("parsers"), list):
        raise ValueError("expected ParserBuildManifestV1 with a parser list")
    rows = document["parsers"]
    names = [row.get("name") for row in rows]
    if not rows or any(not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.:-]*", name)
                       for name in names) or len(set(names)) != len(names):
        raise ValueError("parser names must be nonempty, unique catalog identifiers")
    for row in rows:
        if row.get("strategy", "auto") not in ("general", "auto", "predictive"):
            raise ValueError("unknown parser execution strategy")
        if row.get("format") not in ("authored", "syntax", "lbnf", "bnf", "ebnf", "join", "reader", "module"):
            raise ValueError("unknown parser input format")
        if row.get("format") == "module":
            if any(not isinstance(row.get(key), str) or not row[key]
                   for key in ("reader", "source", "theory", "start")) or row["reader"] not in names:
                raise ValueError("module input requires a manifest reader, source, closed theory and start category")
            catalog = row.get("catalog", {})
            if not isinstance(catalog, dict) or any(not isinstance(k, str) or not isinstance(v, str)
                                                    for k, v in catalog.items()):
                raise ValueError("module catalog maps explicit import locations to source files")
    return rows


def source_path(manifest, name):
    path = Path(name)
    return path.resolve() if path.is_absolute() else (manifest.parent / path).resolve()


def dependencies(manifest, rows):
    result = {manifest}
    for row in rows:
        for key in ("language", "profile", "source", "authority"):
            if key in row:
                result.add(source_path(manifest, row[key]))
        if row["format"] == "module":
            result.update(source_path(manifest, path) for path in row.get("catalog", {}).values())
    # Reader construction and lowering use the existing authored libraries.
    result.update((ROOT / "lib").glob("*bnf.metta"))
    result.update((ROOT / "lib/petta").glob("*bnf.metta"))
    result.update((ROOT / "langdef/bnf").glob("*.metta"))
    result.update((ROOT / "langdef/bnf").glob("*.bnf"))
    for lane in ("he", "petta"):
        result.update((ROOT / f"langdef/{lane}/generated").glob("plain_bnf*.metta"))
    result.update((ROOT / "lib/langdef.metta", ROOT / "lib/petta/langdef.metta",
                   ROOT / "lib/lib_module.metta", ROOT / "lib/petta/lib_module.metta",
                   ROOT / "tools/gslt2parse_schema_v1.py",
                   ROOT / "tools/parser_artifact_publication_v1.py", Path(__file__).resolve()))
    if any(not path.is_file() for path in result):
        raise ValueError("a parser manifest input does not exist")
    return sorted(result)


def one_form(path):
    values = parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(values) != 1:
        raise ValueError("a structured parser input must contain one data form")
    return values[0]


class Inputs:
    def __init__(self, manifest, rows):
        self.manifest = manifest
        self.rows = {row["name"]: row for row in rows}
        self.active = set()
        self.scope = 0

    def syntax(self, name):
        if name in self.active:
            raise ValueError("cyclic build-manifest composition")
        self.active.add(name)
        scope = self.scope
        self.scope += 1
        prefix = f"$build{scope}_"
        try:
            row = self.rows[name]
            kind = row["format"]
            if kind == "syntax":
                value = one_form(source_path(self.manifest, row["source"]))
                if headed(value, "SyntaxGSLTV1", 4):
                    return form("langdef:syntax-gslt", *(render(field) for field in value[1:]))
                if headed(value, "SyntaxGSLTAdmittedV1", 1):
                    return render(value)
                raise ValueError("syntax input must be SyntaxGSLTV1 or its admitted envelope")
            if kind == "lbnf":
                policy = row.get("policy", "bnfc-documented")
                if policy not in ("bnfc-documented", "bnfc-2.9.6.3-c-flex-2.6.4"):
                    raise ValueError("unknown LBNF lexical policy")
                start = text(row["start"]) if "start" in row else "LbnfDefaultStartV1"
                read, binding, source, document, failure = (prefix + role for role in
                    ("read", "binding", "source", "document", "failure"))
                return lets([(read, form("lbnf:read-file", text(source_path(self.manifest, row["source"]))))],
                    form("case", read, form(
                        form(form("LbnfReadValueV1", binding, source, document),
                             form("lbnf:lower-document", document, text(name), start,
                                  form("SyntaxLexicalPolicyV1", policy))),
                        form(failure, failure))))
            if kind == "module":
                policy = row.get("policy", "bnfc-documented")
                if policy not in ("bnfc-documented", "bnfc-2.9.6.3-c-flex-2.6.4"):
                    raise ValueError("unknown module lexical policy")
                reader = prefix + "reader"
                document, presentation, language = (prefix + role for role in
                                                   ("document", "presentation", "language"))
                # Compile the explicitly named reader through this same pipeline;
                # do not require a baked parser in the bootstrap executable.
                bindings = [(reader, self.parser(row["reader"]))]
                catalog = []
                for index, (location, path) in enumerate(row.get("catalog", {}).items()):
                    variable = prefix + f"catalog{index}"
                    bindings.append((variable, form("module:read-file", reader,
                        text("build-import:" + location), text(source_path(self.manifest, path)))))
                    catalog.append(form("ModuleCatalogEntryV1", text(location), variable))
                catalog_list = "LNil"
                for item in reversed(catalog):
                    catalog_list = form("LCons", item, catalog_list)
                bindings += [
                    (document, form("module:read-file", reader, text("build-module:" + name),
                                    text(source_path(self.manifest, row["source"])))),
                    (presentation, form("module:assemble", document, text(row["theory"]),
                                        "LNil", catalog_list, "OriginViewV1", text("build-instance:" + name))),
                    (language, form("module:language", presentation, text(name), text(row["start"]),
                                    form("SyntaxLexicalPolicyV1", policy))),
                    (prefix + "closed", form("langdef:close-parser", reader)),
                ]
                return lets(bindings, form("module:language-syntax", language))
            if kind == "join":
                operands = row.get("operands", [])
                if not isinstance(operands, list) or len(operands) < 2 or not row.get("start"):
                    raise ValueError("join requires two or more syntax operands and a start category")
                bindings = [(prefix + f"operand{i}", self.syntax(operand)) for i, operand in enumerate(operands)]
                value = prefix + "operand0"
                for i in range(1, len(operands)):
                    key = prefix + f"joined{i}"
                    bindings.append((key, form("langdef:join-syntax-gslts", text(name), text(row["start"]),
                                               value, prefix + f"operand{i}")))
                    value = key
                return lets(bindings, value)
            raise ValueError("join operands must be syntax, LBNF, module, or another join")
        finally:
            self.active.remove(name)

    def parser(self, name):
        prefix = f"$build{self.scope}_parser_"
        self.scope += 1
        syntax, raw_name, selected, failure, chosen, closed = (prefix + part for part in
            ("syntax", "raw", "selected", "failure", "chosen", "closed"))
        strategy = self.rows[name].get("strategy", "auto")
        if self.rows[name]["format"] in ("syntax", "lbnf", "module", "join"):
            return lets([(syntax, self.syntax(name))],
                        form("langdef:compile-parser", syntax, strategy))
        raw = self.parser_source(name)
        selection = form("case", form("__cetta_lib_parser_select_strategy", raw_name, strategy),
                         form(form(form("return", selected), selected), form(failure, failure)))
        return lets([(raw_name, raw), (chosen, selection),
                     (closed, form("langdef:close-parser", raw_name))], chosen)

    def parser_source(self, name):
        row = self.rows[name]
        kind = row["format"]
        if kind in ("syntax", "lbnf", "module", "join"):
            syntax = f"$build{self.scope}_syntax"
            self.scope += 1
            return lets([(syntax, self.syntax(name))],
                        form("langdef:load-syntax-gslt-parser", syntax))
        if kind == "authored":
            return form("langdef:load-authored-parser", text(source_path(self.manifest, row["language"])),
                        text(source_path(self.manifest, row["profile"])))
        if kind in ("bnf", "ebnf"):
            operation = "bnf:parser-from-file" if kind == "bnf" else "bnf:parser-from-ebnf-file"
            source = text(source_path(self.manifest, row["source"]))
            if "authority" in row:
                parser = form(operation + "-with-authority", source,
                              render(one_form(source_path(self.manifest, row["authority"]))))
            else:
                parser = form(operation, source, text(row["start"]))
            if kind == "ebnf":
                # The authored EBNF frontend exposes a source/projection
                # receipt around its underlying prepared-parser handle.
                prefix = f"$build{self.scope}_ebnf_"
                self.scope += 1
                handle, binding, source, projection, failure = (prefix + role for role in
                    ("handle", "binding", "source", "projection", "failure"))
                return form("case", parser, form(
                    form(form("EBNF:SourceParser", handle, binding, source, projection), handle),
                    form(form("EBNF:Parser", handle, projection), handle),
                    form(failure, failure)))
            return parser
        if kind == "reader":
            reader = row["reader"]
            if reader == "lbnf":
                return "(lbnf:open-reader)"
            if reader == "ebnf":
                return "(bnf:open-ebnf-reader)"
        raise ValueError("unsupported parser construction")


def run_compiler(binary, directory, name, program, timeout):
    source = directory / f"{name}.metta"
    source.write_text(program, encoding="utf-8")
    result = subprocess.run([str(binary), "--lang", "petta@1.0.6", "--profile", "extended", str(source)],
                            cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    (directory / f"{name}.stdout").write_text(result.stdout, encoding="utf-8")
    (directory / f"{name}.stderr").write_text(result.stderr, encoding="utf-8")
    if result.returncode:
        raise ValueError(f"parser compiler failed for {name}: exit {result.returncode}")
    values = []
    for line in result.stdout.splitlines():
        if line.strip() and line != "true":
            values.extend(parse_sexprs(line))
    return values


def compile_catalog(binary, manifest, rows, output, header, timeout):
    output.mkdir(parents=True, exist_ok=True)
    inputs = Inputs(manifest, rows)
    imports = ('!(import! &self "lib/lib_lbnf.metta")\n!(import! &self "lib/lib_bnf.metta")\n'
               '!(import! &self "lib/lib_module.metta")\n')
    with tempfile.TemporaryDirectory(prefix=".partial-", dir=output) as temporary:
        directory = Path(temporary)
        artifacts = []
        for row in rows:
            name = row["name"]
            path = directory / f"{name}.tpp"
            program = imports + "!" + lets([
                ("$parser", inputs.parser(name)),
                ("$frozen", form("langdef:freeze-parser", "$parser", text(path))),
                ("$closed", form("langdef:close-parser", "$parser")),
                ("$restored", form("langdef:load-parser-artifact-checked", text(path))),
                ("$info", form("langdef:parser-info", "$restored")),
                ("$closed-restored", form("langdef:close-parser", "$restored")),
            ], "(function (return (ParserBuildResultV1 $frozen $info $closed $closed-restored)))") + "\n"
            values = run_compiler(binary, directory, name, program, timeout)
            records = [value for value in values if headed(value, "ParserBuildResultV1", 4)]
            if len(records) != 1 or not headed(records[0][1], "ParserArtifactV1", 2) or \
                    not headed(records[0][2], "ParserInfoV1", 6) or \
                    records[0][3:] != (Symbol("true"), Symbol("true")):
                raise ValueError(f"parser construction or checked restoration refused {name}: {values!r}")
            artifacts.append((name, path.read_bytes()))
        digest = hashlib.sha256()
        emitted = ["/* Generated portable parser catalog. */"]
        for index, (name, content) in enumerate(artifacts):
            digest.update(name.encode("utf-8") + b"\0")
            digest.update(len(content).to_bytes(8, "little") + content)
            emitted.append(f"static const uint8_t cetta_baked_parser_bytes_v1_{index}[] = {{")
            for offset in range(0, len(content), 24):
                emitted.append("    " + ",".join(str(byte) for byte in content[offset:offset + 24]) + ",")
            emitted.append("};")
        emitted.append("static const CettaBakedParserV1 cetta_baked_parsers_v1[] = {")
        for index, (name, _) in enumerate(artifacts):
            emitted.append(f"    {{{json.dumps(name)}, cetta_baked_parser_bytes_v1_{index}, "
                           f"sizeof(cetta_baked_parser_bytes_v1_{index})}},")
        emitted += ["};", f"#define CETTA_BAKED_PARSER_COUNT_V1 {len(artifacts)}u", ""]
        content = "\n".join(emitted).encode("ascii")
        generation = output / digest.hexdigest()
        # The generation is published only after every parser was restored
        # by the checked loader. Interrupted construction cannot expose it.
        publish_generation(directory, generation,
                           {name + ".tpp": artifact for name, artifact in artifacts})
        publish_file(header, content)
    print(f"(ParserBuildCatalogV1 {len(artifacts)} {digest.hexdigest()})")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--dependencies", action="store_true")
    parser.add_argument("--manifest-key", action="store_true")
    parser.add_argument("--cetta", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    manifest = args.manifest.resolve()
    rows = load_manifest(manifest)
    if args.manifest_key:
        print(hashlib.sha256(str(manifest).encode("utf-8") + b"\0" + manifest.read_bytes()).hexdigest()[:16])
        return
    if args.dependencies:
        def make_path(path):
            return os.path.relpath(path, ROOT).replace("\\", "\\\\").replace(" ", "\\ ").replace("#", "\\#").replace("$", "$$")
        print(" ".join(make_path(path) for path in dependencies(manifest, rows)))
        return
    if not args.cetta or not args.output or not args.header:
        parser.error("catalog construction needs --cetta, --output and --header")
    compile_catalog(args.cetta.resolve(), manifest, rows, args.output.resolve(), args.header.resolve(), args.timeout)


if __name__ == "__main__":
    main()
