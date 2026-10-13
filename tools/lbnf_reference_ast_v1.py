#!/usr/bin/env python3
"""Read typed ASTs from the pinned BNFC C backend without its lossy viewer.

The visitor is derived from generated constructor implementations, not from
CeTTa's grammar lowering. Argument order comes from the makers, since the C
struct fields are grouped by type. The reference lexer and parser objects are
linked unchanged. This is an untrusted differential-test tool, not a parser
implementation or a proof of correspondence.
"""

from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from test_lbnf_values_v1 import headed, llist, scalar_text


@dataclass(frozen=True)
class Constructor:
    category: str
    label: str
    parameters: tuple[tuple[str, str], ...]
    fields: tuple[str, ...]
    tagged: bool


@dataclass(frozen=True)
class ReferenceLayout:
    tokens: tuple[str, ...]
    constructors: tuple[Constructor, ...]
    start: str
    entry: str

    def signature(self, category, label):
        candidates = [row for row in self.constructors
                      if row.category == category and row.label == label]
        if len(candidates) != 1:
            raise ValueError("unknown or repeated C AST signature: " + category + ":" + label)
        return candidates[0]

    def list_element(self, category):
        maker = self.signature(category, category)
        if maker.tagged or len(maker.parameters) != 2 or maker.parameters[1][0] != category:
            raise ValueError("not a generated list category: " + category)
        return maker.parameters[0][0]


def reference_layout(directory):
    header = (directory / "Absyn.h").read_text()
    source = (directory / "Absyn.c").read_text()
    test = (directory / "Test.c").read_text()
    if "bnfc 2.9.6.3" not in header:
        raise ValueError("the reference AST visitor requires BNFC 2.9.6.3")
    tokens = tuple(name for base, name in re.findall(
        r"^typedef (.+?) (\w+);$", header, re.MULTILINE)
        if base.replace(" ", "") == "char*")
    matches = re.findall(r"^(\w+) make_(\w+)\(([^)]*)\)\n\{(.*?)\n\}",
                         source, re.MULTILINE | re.DOTALL)
    constructors = []
    for category, label, parameters, body in matches:
        if not re.fullmatch(r"[A-Za-z_]\w*", category + label):
            raise ValueError("unsupported generated C identifier")
        parameters = parameters.strip()
        parsed = []
        if parameters and parameters != "void":
            for parameter in parameters.split(","):
                match = re.fullmatch(r"\s*(\w+)\s+(p\d+)\s*", parameter)
                if not match:
                    raise ValueError("unknown C maker parameter: " + parameter)
                parsed.append(match.groups())
        assignments = re.findall(r"tmp->([\w.]+)\s*=\s*(p\d+);", body)
        if (len(assignments) != len(parsed) or
                {parameter for _, parameter in assignments} != {name for _, name in parsed}):
            raise ValueError("C maker argument assignments are not bijective: " + label)
        fields = dict((parameter, field) for field, parameter in assignments)
        tag = re.findall(r"tmp->kind\s*=\s*is_(\w+);", body)
        if tag and tag != [label]:
            raise ValueError("unexpected C constructor tag: " + label)
        if not tag and (label != category or len(parsed) != 2 or parsed[1][0] != category):
            raise ValueError("unknown untagged C AST carrier: " + label)
        constructors.append(Constructor(category, label, tuple(parsed),
                                        tuple(fields[name] for _, name in parsed), bool(tag)))
    if not constructors or len(constructors) != len(re.findall(r"^\w+ make_", source, re.MULTILINE)):
        raise ValueError("generated C AST constructors were not fully accounted for")
    start = re.search(r"\b(\w+)\s+parse_tree;", test)
    entry = re.search(r"parse_tree\s*=\s*(\w+)\(input\);", test)
    if not start or not entry:
        raise ValueError("unknown C test parser entrypoint")
    layout = ReferenceLayout(tokens, tuple(constructors), start[1], entry[1])
    known = set(tokens) | {"Integer", "Char", "Double"} | {row.category for row in constructors}
    if layout.start not in known or any(type_ not in known for row in constructors
                                       for type_, _ in row.parameters):
        raise ValueError("a generated C AST field has an unaccounted type")
    return layout


def visitor_source(layout):
    categories = sorted({row.category for row in layout.constructors})
    pieces = [r'''#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include "Absyn.h"
#include "Parser.h"

void ast_bytes(const char *type, const unsigned char *bytes, size_t length) {
    printf("[\"token\",\"%s\",\"", type);
    for (size_t i = 0; i < length; ++i) printf("%02x", bytes[i]);
    fputs("\"]", stdout);
}
void ast_Integer(Integer x) { printf("[\"token\",\"Integer\",\"%d\"]", x); }
void ast_Char(Char x) { unsigned char byte = (unsigned char)x; ast_bytes("Char", &byte, 1); }
void ast_Double(Double x) {
    uint64_t bits;
    if (sizeof(x) != sizeof(bits)) exit(4);
    memcpy(&bits, &x, sizeof(bits));
    printf("[\"token\",\"Double\",\"%016" PRIx64 "\"]", bits);
}
''']
    for token in layout.tokens:
        pieces.append(f'void ast_{token}({token} x) {{ if (!x) exit(4); '
                      f'ast_bytes("{token}", (const unsigned char *)x, strlen(x)); }}\n')
    for category in categories:
        pieces.append(f"void ast_{category}({category} x);\n")
    for category in categories:
        rows = [row for row in layout.constructors if row.category == category]
        pieces.append(f"void ast_{category}({category} x) {{\n")
        if not rows[0].tagged:
            if len(rows) != 1:
                raise ValueError("multiple untagged makers for one C category")
            row = rows[0]
            pieces.append('fputs("[\\\"list\\\",[", stdout); int comma = 0;\n')
            pieces.append(f"while (x) {{ if (comma++) putchar(','); ast_{row.parameters[0][0]}"
                          f"(x->{row.fields[0]}); x = x->{row.fields[1]}; }}\n")
            pieces.append('fputs("]]", stdout);\n}\n')
            continue
        if not all(row.tagged for row in rows):
            raise ValueError("mixed tagged and untagged C AST carrier")
        pieces.append("if (!x) { exit(4); }\nswitch (x->kind) {\n")
        for row in rows:
            pieces.append(f'case is_{row.label}: fputs("[\\\"node\\\",\\\"{category}\\\",'
                          f'\\\"{row.label}\\\",[", stdout);\n')
            for index, ((type_, _), field) in enumerate(zip(row.parameters, row.fields)):
                if index:
                    pieces.append("putchar(',');\n")
                pieces.append(f"ast_{type_}(x->{field});\n")
            pieces.append('fputs("]]", stdout); break;\n')
        pieces.append("default: exit(4); }\n}\n")
    pieces.append(f'''int main(int argc, char **argv) {{
    if (argc != 2) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 2;
    {layout.start} value = {layout.entry}(input);
    fclose(input);
    printf("{{\\\"format\\\":\\\"BnfcTypedAstV1\\\",\\\"int_bits\\\":%zu,"
           "\\\"double_bytes\\\":%zu,\\\"ast\\\":", sizeof(Integer) * CHAR_BIT, sizeof(Double));
    ast_{layout.start}(value);
    puts("}}");
    free_{layout.start}(value);
    return 0;
}}
''')
    return "".join(pieces)


def build_visitor(reference, output, compiler="cc"):
    layout = reference_layout(reference)
    output.mkdir(parents=True, exist_ok=True)
    source = output / "typed-ast.c"
    executable = output / "typed-ast"
    source.write_text(visitor_source(layout))
    objects = [reference / (name + ".o") for name in ("Absyn", "Parser", "Lexer", "Buffer")]
    if not all(path.is_file() for path in objects):
        raise ValueError("frozen C reference objects are missing")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(reference), str(source), *map(str, objects), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, timeout=120)
    (output / "build.stdout").write_bytes(result.stdout)
    (output / "build.stderr").write_bytes(result.stderr)
    receipt = {"command": command, "status": result.returncode,
               "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "reference_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                    for path in [reference / "Absyn.h", reference / "Absyn.c", *objects]}}
    if result.returncode:
        raise ValueError("typed C reference visitor build failed: " + result.stderr.decode()[:1000])
    receipt["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
    (output / "build.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return executable, layout


class InputDomainError(ValueError):
    pass


def c_category(category):
    if category.startswith("[") and category.endswith("]"):
        return "List" + c_category(category[1:-1])
    return re.sub(r"[0-9]+$", "", category)


def normalize_ast(value, int_bits=32):
    """Erase source sites and production identity only in the AST projection.

    Trees and field types stay explicit; strings are exact UTF-8 bytes, doubles
    are exact binary64 bits, and integers must fit the reference C int domain.
    The original packed forest is retained independently by the corpus tool.
    """
    if headed(value, "LbnfAstNodeV1", 4):
        return ["node", c_category(value[1].text), value[2].text,
                [normalize_ast(child, int_bits) for child in llist(value[4])]]
    if headed(value, "LbnfAstListV1", 3):
        return ["list", [normalize_ast(child, int_bits) for child in llist(value[3])]]
    if headed(value, "LbnfAstTokenV1", 6):
        category, payload = value[1].text, scalar_text(value[3])
        if category == "Integer":
            integer = int(payload)
            if not -(1 << (int_bits - 1)) <= integer < (1 << (int_bits - 1)):
                raise InputDomainError("integer exceeds the pinned C int domain: " + payload)
            normalized = str(integer)
        elif category == "Double":
            number = float(payload)
            if not math.isfinite(number):
                raise InputDomainError("double is not finite in the pinned C domain: " + payload)
            normalized = struct.pack(">d", number).hex()
        else:
            normalized = payload.encode("utf-8").hex()
            if category == "Char" and len(bytes.fromhex(normalized)) != 1:
                raise InputDomainError("character is not one byte in the pinned C domain")
        return ["token", category, normalized]
    raise ValueError("not a generic labelled LBNF AST")


def golden_tokens(source):
    """Decode the Haskell/C AST viewer notation used by stored BNFC goldens."""
    tokens, offset = [], 0
    escapes = {"a": "\a", "b": "\b", "f": "\f", "n": "\n", "r": "\r",
               "t": "\t", "v": "\v", "\\": "\\", '"': '"', "'": "'"}
    while offset < len(source):
        char = source[offset]
        if char.isspace():
            offset += 1
            continue
        if char in "(),[]":
            tokens.append(char)
            offset += 1
            continue
        if char in "\"'":
            quote, pieces = char, []
            offset += 1
            while offset < len(source) and source[offset] != quote:
                char = source[offset]
                offset += 1
                if char != "\\":
                    pieces.append(char)
                    continue
                if offset >= len(source):
                    raise ValueError("unterminated golden escape")
                escape = source[offset]
                offset += 1
                if escape == "&":
                    continue
                if escape in escapes:
                    pieces.append(escapes[escape])
                    continue
                if escape.isascii() and escape.isdigit():
                    end = offset
                    while end < len(source) and source[end].isascii() and source[end].isdigit():
                        end += 1
                    pieces.append(chr(int(escape + source[offset:end], 10)))
                    offset = end
                    continue
                if escape in "xo":
                    alphabet, base = ("0123456789abcdefABCDEF", 16) if escape == "x" else ("01234567", 8)
                    end = offset
                    while end < len(source) and source[end] in alphabet:
                        end += 1
                    if end == offset:
                        raise ValueError("empty numeric golden escape")
                    pieces.append(chr(int(source[offset:end], base)))
                    offset = end
                    continue
                raise ValueError("unknown golden escape: " + escape)
            if offset >= len(source):
                raise ValueError("unterminated golden quoted value")
            offset += 1
            tokens.append(("string" if quote == '"' else "char", "".join(pieces)))
            continue
        end = offset
        while end < len(source) and not source[end].isspace() and source[end] not in "(),[]\"'":
            end += 1
        if end == offset:
            raise ValueError("invalid golden token")
        tokens.append(source[offset:end])
        offset = end
    return tokens


class GoldenReader:
    def __init__(self, source, layout):
        self.tokens, self.offset, self.layout = golden_tokens(source), 0, layout

    def peek(self):
        return self.tokens[self.offset] if self.offset < len(self.tokens) else None

    def take(self, expected=None):
        token = self.peek()
        if token is None or (expected is not None and token != expected):
            raise ValueError("golden AST expected " + repr(expected) + ", found " + repr(token))
        self.offset += 1
        return token

    def read(self, category):
        parenthesized = self.peek() == "("
        if parenthesized:
            self.take("(")
        primitive = category in ("Integer", "Double", "Char") or category in self.layout.tokens
        if primitive:
            # Haskell newtype token wrappers carry their type; C's viewer does
            # not. A different wrapper is an error, not incidental identity.
            if self.peek() == category:
                self.take(category)
            value = self.take()
            if category == "Integer":
                if not isinstance(value, str) or not re.fullmatch(r"-?[0-9]+", value):
                    raise ValueError("golden Integer is not an integer")
                result = ["token", category, str(int(value))]
            elif category == "Double":
                if not isinstance(value, str):
                    raise ValueError("golden Double is not numeric")
                number = float(value)
                if not math.isfinite(number):
                    raise ValueError("golden Double is not finite")
                result = ["token", category, struct.pack(">d", number).hex()]
            else:
                quote = "char" if category == "Char" else "string"
                if not isinstance(value, tuple) or value[0] != quote:
                    raise ValueError("golden token wrapper or quoted payload disagrees with " + category)
                result = ["token", category, value[1].encode().hex()]
        elif any(row.category == category and not row.tagged for row in self.layout.constructors):
            element = self.layout.list_element(category)
            if element == "Char" and isinstance(self.peek(), tuple) and self.peek()[0] == "string":
                result = ["list", [["token", "Char", char.encode().hex()] for char in self.take()[1]]]
            else:
                self.take("[")
                children = []
                if self.peek() != "]":
                    while True:
                        children.append(self.read(element))
                        if self.peek() != ",":
                            break
                        self.take(",")
                self.take("]")
                result = ["list", children]
        else:
            label = self.take()
            if not isinstance(label, str):
                raise ValueError("golden constructor label is not a name")
            signature = self.layout.signature(category, label)
            result = ["node", category, label, [self.read(type_) for type_, _ in signature.parameters]]
        if parenthesized:
            self.take(")")
        return result

    def document(self):
        result = self.read(self.layout.start)
        if self.peek() is not None:
            raise ValueError("trailing golden AST token: " + repr(self.peek()))
        return result


def golden_comparable(tree):
    """Stored viewers print doubles at 15 digits; the live C gate stays bit-exact."""
    if tree[0] == "node":
        return tree[:3] + [[golden_comparable(child) for child in tree[3]]]
    if tree[0] == "list":
        return ["list", [golden_comparable(child) for child in tree[1]]]
    if tree[:2] == ["token", "Double"]:
        return ["token", "Double", format(struct.unpack(">d", bytes.fromhex(tree[2]))[0], ".15g")]
    return tree
