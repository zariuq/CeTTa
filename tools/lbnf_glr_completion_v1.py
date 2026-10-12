#!/usr/bin/env python3
"""Complete a frozen BNFC C reference with Bison's GLR algorithm.

The deterministic C reference resolves each table conflict by Bison's
default choice, so it can reject an input that the generated grammar
derives.  The completion keeps the frozen generated grammar, lexer and
abstract syntax and adds one declaration, %glr-parser, so every conflict is
explored instead of resolved.  Choosing the GLR skeleton alone is not
enough: without the declaration Bison still emits deterministic tables.

The completion decides membership in the grammar the reference was
generated from.  It is consulted only where the deterministic reference
rejects.  This is an untrusted differential-test tool, not a parser
implementation or a proof of correspondence.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

FROZEN = ("Absyn.h", "Absyn.c", "Absyn.o", "Buffer.h", "Buffer.o",
          "Lexer.o", "Parser.h", "Test.c", "Bison.h")
DECLARATION = "%glr-parser"


def classify_glr_completion(result):
    """Separate parser verdicts from a failed reference process.

    The typed visitor exits with 4 when the parser supplies no tree. Its
    diagnostic must identify a syntax rejection or unresolved ambiguity;
    that exit code can also signal an invalid AST constructor. Neither a
    crash nor resource exhaustion is evidence that an input is rejected.
    """
    stderr = result.stderr
    if result.returncode == 0:
        if stderr:
            raise ValueError("GLR reference succeeded with unexpected diagnostics")
        typed = json.loads(result.stdout)
        if not isinstance(typed, dict) or typed.get("format") != "BnfcTypedAstV1":
            raise ValueError("GLR reference returned an invalid typed AST envelope")
        return {"algorithm": "bison-glr", "status": 0,
                "ambiguous": False, "typed": typed}
    if result.returncode != 4:
        raise ValueError("GLR reference process failed: " + str(result.returncode))
    # BNFC prefixes yyerror's message with its position and appends the
    # offending token. Token text must not supply or alter the verdict.
    diagnostic = stderr.splitlines()[0] if stderr else b""
    diagnostic = re.sub(rb"^error:\s*\d+,\d+:\s*", b"", diagnostic)
    message = diagnostic.split(b" at ", 1)[0]
    ambiguous = message == b"syntax is ambiguous"
    if not ambiguous and message != b"syntax error":
        raise ValueError("GLR reference supplied no checked parse verdict")
    return {"algorithm": "bison-glr", "status": 4,
            "ambiguous": ambiguous, "typed": None}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def token_numbering(header):
    return re.findall(r"^\s+(\w+) = (\d+),?", header, re.MULTILINE)


def build_glr_completion(reference, output, bison, compiler="gcc"):
    reference, output = Path(reference), Path(output)
    grammars = sorted(reference.glob("*.y"))
    if len(grammars) != 1:
        raise ValueError("a frozen reference has exactly one generated grammar")
    grammar = grammars[0]
    options = re.search(r"^BISON_OPTS = (.*)$",
                        (reference / "Makefile").read_text(), re.MULTILINE)
    if not options or not re.fullmatch(r"-t -p\w+", options[1].strip()):
        raise ValueError("unknown generated Bison options")
    lines = grammar.read_text().splitlines(keepends=True)
    if any(line.strip() == DECLARATION for line in lines):
        raise ValueError("the frozen grammar already declares a GLR parser")
    anchors = [index for index, line in enumerate(lines)
               if line.strip() == "%pure_parser"]
    if len(anchors) != 1:
        raise ValueError("the generated grammar has no unique parser declaration")
    lines.insert(anchors[0] + 1, DECLARATION + "\n")
    output.mkdir(parents=True, exist_ok=True)
    for name in FROZEN:
        shutil.copyfile(reference / name, output / ("reference-" + name
                        if name == "Bison.h" else name))
    completed = output / grammar.name
    completed.write_text("".join(lines))
    receipt = {"format": "BnfcGlrCompletionV1", "declaration": DECLARATION,
               "reference_grammar_sha256": sha256(grammar),
               "completed_grammar_sha256": sha256(completed),
               "bison": {"path": str(bison), "sha256": sha256(bison)},
               "frozen_sha256": {name: sha256(reference / name) for name in FROZEN}}
    commands = [[str(bison), *options[1].split(), completed.name, "-o", "Parser.c"],
                [compiler, "-c", "Parser.c", "-o", "Parser.o"]]
    for index, command in enumerate(commands):
        result = subprocess.run(command, cwd=output, capture_output=True, timeout=300)
        (output / ("step-" + str(index) + ".stdout")).write_bytes(result.stdout)
        (output / ("step-" + str(index) + ".stderr")).write_bytes(result.stderr)
        if result.returncode:
            raise ValueError("GLR completion step failed: " + " ".join(command))
    numbering = token_numbering((output / "Bison.h").read_text())
    if not numbering or numbering != token_numbering(
            (output / "reference-Bison.h").read_text()):
        raise ValueError("the completed parser renumbers the frozen lexer's tokens")
    receipt["commands"] = commands
    receipt["parser_object_sha256"] = sha256(output / "Parser.o")
    (output / "glr-completion.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bison", type=Path, required=True)
    args = parser.parse_args()
    print(build_glr_completion(args.reference, args.output, args.bison))


if __name__ == "__main__":
    main()
