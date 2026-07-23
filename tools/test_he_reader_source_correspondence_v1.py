#!/usr/bin/env python3
"""Structural specification/implementation inventory for the HE reader."""

from __future__ import annotations

import argparse
import csv
from hashlib import sha256
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FOUNDATION = ROOT / "experiments" / "gslt2parse_foundation"
PRESENTATIONS = FOUNDATION / "presentations"
CLASSES = PRESENTATIONS / "shared" / "he_reader_scalar_classes_v1.metta"
READER = PRESENTATIONS / "languages" / "he_reader_v1.metta"
MANIFEST = FOUNDATION / "he_reader_source_correspondence_v1.tsv"
CONTRACT = FOUNDATION / "he_reader_contract_conformance_v1.tsv"
AUTHORITIES = FOUNDATION / "parser_authorities_v1.tsv"
BUILDER = ROOT / "scripts" / "build_he_reader_gslt_v1.py"

EXPECTED_REVISION = "3f76dc460da6961f57f69f6c3e550c59c74ada83"
EXPECTED_SPEC_DIGEST = (
    "ad0210aff8cddf1eacbbe848d5e842aa6c421a32c400f9625f87893d14d81509"
)
EXPECTED_SOURCE_DIGEST = (
    "c4bda32a09da1d091a5d725fb541e239dcea1339c8f312ad0e98763dad426a28"
)
EXPECTED_CETTA_SOURCE_DIGEST = (
    "fb01bd8578fbb413b6282bf88b23832d1ac5348e3ac57d80a6155b6ebc585446"
)
EXPECTED_LEATTA_SPEC_DIGEST = (
    "3b6cdc8e6939622cb9705bc43442aa6f86e9f0989eae1b05e31e0cc6817e610b"
)
EXPECTED_LEATTA_PARSER_DIGEST = (
    "73eb8d692526964a7c754c490702329e552dfce861ea32b584933ec8880bd3f0"
)
EXPECTED_OBLIGATIONS = {
    "decoded-unicode-scalar-input",
    "top-comment-dispatch",
    "top-whitespace-dispatch",
    "top-variable-dispatch",
    "top-expression-dispatch",
    "unexpected-right-paren",
    "token-string-dispatch",
    "token-word-dispatch",
    "comment-stops-only-lf",
    "expression-layout-recursion",
    "expression-close-required",
    "string-close",
    "string-simple-escapes",
    "string-byte-escape",
    "string-unicode-escape",
    "string-invalid-escape",
    "string-unclosed",
    "word-boundaries",
    "word-interior-quote-dollar-hash",
    "variable-boundaries",
    "variable-empty-name",
    "variable-semicolon-quote",
    "variable-hash-reject",
    "atom-variable",
    "atom-string-word-empty-tokenizer",
    "atom-expression-trivia",
    "document-stream",
    "runtime-tokenizer",
    "program-bang",
}
MANIFEST_FIELDS = (
    "obligation_id",
    "implementation_function",
    "implementation_behavior",
    "gslt_witness",
    "disposition",
    "limit",
)
DISPOSITIONS = {
    "modeled-valid",
    "rejection-modeled",
    "implementation-divergence",
    "separate-boundary",
}
BOUNDARIES = {
    "empty-tokenizer",
    "invalid-source",
    "program-loader",
    "runtime-tokenizer",
    "utf8-decoder",
}
EXPECTED_CONTRACT_CLAUSES = {
    "unicode-whitespace",
    "word-interior-quote",
    "variable-interior-quote",
    "variable-hash-reserved",
    "bare-dollar",
    "variable-semicolon",
    "comment-eol",
    "bang-program-form",
    "bang-leading-symbol",
    "string-escape-boundary",
    "string-simple-escape",
    "string-byte-escape",
    "string-unicode-escape",
    "string-invalid-escape",
    "runtime-tokenizer",
    "error-recovery",
}
CONTRACT_FIELDS = (
    "clause_id",
    "contract_basis",
    "written_reference",
    "finding",
    "rust_reference",
    "rust_relation",
    "cetta_reference",
    "cetta_relation",
    "leatta_reference",
    "leatta_relation",
    "gslt_witness",
    "gslt_relation",
    "disposition",
    "limit",
)
CONTRACT_BASES = {
    "written-direct",
    "written-delegation",
    "written-ambiguous",
    "written-weak-requirement",
    "written-incomplete",
    "written-silent",
    "ratified-clarification",
    "ratified-extension",
}
CONTRACT_RELATIONS = {
    "conforms",
    "diverges",
    "candidate-clarification",
    "separate-boundary",
}
CONTRACT_DISPOSITIONS = {
    "settled",
    "ratification-required",
    "separate-layer",
}
FUNCTION_PATTERNS = {
    "SExprParser::next": r"\bfn\s+next\s*\(",
    "SExprParser::parse": r"\bpub\s+fn\s+parse\s*\(",
    "SExprParser::parse_to_syntax_tree": (
        r"\bpub\s+fn\s+parse_to_syntax_tree\s*\("
    ),
    "SExprParser::parse_comment": r"\bfn\s+parse_comment\s*\(",
    "SExprParser::parse_expr": r"\bfn\s+parse_expr\s*\(",
    "SExprParser::parse_token": r"\bfn\s+parse_token\s*\(",
    "SExprParser::parse_string": r"\bfn\s+parse_string\s*\(",
    "SExprParser::parse_unicode_sequence": (
        r"\bfn\s+parse_unicode_sequence\s*\("
    ),
    "SExprParser::parse_word": r"\bfn\s+parse_word\s*\(",
    "SExprParser::parse_variable": r"\bfn\s+parse_variable\s*\(",
    "SyntaxNode::as_atom": r"\bpub\s+fn\s+as_atom\s*\(",
}
SOURCE_MARKERS = (
    r"';'\s*=>\s*\{\s*return self\.parse_comment\(\)",
    r"_ if c\.is_whitespace\(\)\s*=>",
    r"'\$'\s*=>\s*\{\s*return self\.parse_variable\(\)",
    r"'\('\s*=>\s*\{\s*return self\.parse_expr\(\)",
    r"Some\(\(_idx, '\"'\)\)\s*=>",
    r"'\\n'\s*=>\s*break",
    r"if c == '\\\\'",
    r"'\\''\s*\|\s*'\\\"'\s*\|\s*'\\\\'\s*=>\s*c",
    r"'x'\s*=>",
    r"'u'\s*=>",
    r"if hex <= 0x7F",
    r"if next_char == '\{'\s*\{\s*continue",
    r"if cur_idx > 8",
    r"char::from_u32\(result_u32\)",
    r"c == '\(' \|\| c == '\)' \|\| c == ';'",
    r"if c == '#'",
)
SPEC_MARKERS = (
    r"METTA\s*::=",
    r"WORD\s*::=",
    r"VARIABLE\s*::=",
    r"STRING\s*::=",
    r"COMMENT\s*::=",
    r"EOL\s*::=",
    r"char\.is_whitespace\(\)",
    r"Tokenizer is a collection",
)
CETTA_MARKERS = (
    r"while \(text\[\*pos\] && isspace",
    r"text\[\*pos\] != '\\n'",
    r"c != '\"'",
    r"default:\s*return c;",
    r"if \(tok\[0\] == '\$'\) \{\s*if \(len > 1\)",
    r"strcmp\(tok, \"True\"\)",
    r"strtoll\(tok",
)
LEATTA_SPEC_MARKERS = (
    r"structure LexerSpec where",
    r"reserveHashInVariable\s*:\s*Bool",
    r"trimAsciiWhitespace\s*:\s*Bool",
    r"structure EvalPrefixPolicy where",
    r"bangPrefixedWordIsSymbol\s*:\s*Bool",
    r"def he\s*:\s*SyntaxSpec",
)
LEATTA_PARSER_MARKERS = (
    r"private def tokenizeAux",
    r"else if c\.isWhitespace then",
    r"if tok\.startsWith \"\$\" then",
    r"if name\.isEmpty then \.fvar tok",
    r"private def shouldTreatBangPrefixedWordAsSymbol",
    r"if st\.inString then",
    r"def parseProgramWith",
)
LEATTA_PARSER_FORBIDDEN_MARKERS = (
    r"reserveHashInVariable",
    r"allowHashInSymbol",
)


class GateFailure(RuntimeError):
    pass


def run_checked(command: list[str], *, cwd: Path) -> bytes:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=300,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            + completed.stderr.decode("utf-8", errors="replace")
        )
    return completed.stdout


def authority_rows() -> dict[str, dict[str, str]]:
    with AUTHORITIES.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    indexed: dict[str, dict[str, str]] = {}
    for row in rows:
        authority_id = row.get("authority_id", "")
        if authority_id in indexed:
            raise GateFailure(f"duplicated parser authority {authority_id}")
        indexed[authority_id] = row
    expected = {
        "he-written-syntax-v1": (EXPECTED_REVISION, EXPECTED_SPEC_DIGEST),
        "he-sexpr-parser-v1": (EXPECTED_REVISION, EXPECTED_SOURCE_DIGEST),
        "cetta-he-compat-parser-v1": (
            "8dae2cc61fe7072ec9946cb31ffa41151dbe09d5",
            EXPECTED_CETTA_SOURCE_DIGEST,
        ),
        "leatta-he-syntax-v1": (
            "7842a1202f4fa968dd167d7de0933b1173fb7c91",
            EXPECTED_LEATTA_SPEC_DIGEST,
        ),
        "leatta-he-parser-v1": (
            "7842a1202f4fa968dd167d7de0933b1173fb7c91",
            EXPECTED_LEATTA_PARSER_DIGEST,
        ),
    }
    for authority_id, (revision, digest) in expected.items():
        row = indexed.get(authority_id)
        if row is None:
            raise GateFailure(f"parser authority is absent: {authority_id}")
        if row.get("revision") != revision:
            raise GateFailure(f"parser authority revision changed: {authority_id}")
        if row.get("source_sha256") != digest:
            raise GateFailure(f"parser authority digest changed: {authority_id}")
    return indexed


def authority_sources(
    he_root: Path, cetta_root: Path, mettapedia_root: Path
) -> tuple[str, str, str, str, str]:
    revision = run_checked(["git", "rev-parse", "HEAD"], cwd=he_root)
    if revision.decode("ascii").strip() != EXPECTED_REVISION:
        raise GateFailure("pinned HE checkout revision changed")
    rows = authority_rows()

    def checked_source(
        root: Path, authority_id: str, expected_digest: str
    ) -> str:
        path = root / rows[authority_id]["source_path"]
        if not path.is_file():
            raise GateFailure(f"pinned source is absent: {authority_id}")
        raw = path.read_bytes()
        if sha256(raw).hexdigest() != expected_digest:
            raise GateFailure(f"pinned source digest changed: {authority_id}")
        return raw.decode("utf-8")

    specification = checked_source(
        he_root, "he-written-syntax-v1", EXPECTED_SPEC_DIGEST
    )
    rust_source = checked_source(
        he_root, "he-sexpr-parser-v1", EXPECTED_SOURCE_DIGEST
    )
    cetta_source = checked_source(
        cetta_root,
        "cetta-he-compat-parser-v1",
        EXPECTED_CETTA_SOURCE_DIGEST,
    )
    leatta_spec = checked_source(
        mettapedia_root,
        "leatta-he-syntax-v1",
        EXPECTED_LEATTA_SPEC_DIGEST,
    )
    leatta_parser = checked_source(
        mettapedia_root,
        "leatta-he-parser-v1",
        EXPECTED_LEATTA_PARSER_DIGEST,
    )
    return specification, rust_source, cetta_source, leatta_spec, leatta_parser


def presentation_rules() -> set[str]:
    text = CLASSES.read_text(encoding="utf-8") + READER.read_text(encoding="utf-8")
    return set(re.findall(r"\(rule\s+([^\s()]+)", text))


def check_manifest(source: str, rules: set[str]) -> tuple[int, int, int, int]:
    with MANIFEST.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if tuple(reader.fieldnames or ()) != MANIFEST_FIELDS:
            raise GateFailure("HE source-correspondence schema changed")
        rows = list(reader)
    ids = [row["obligation_id"] for row in rows]
    if len(ids) != len(set(ids)) or set(ids) != EXPECTED_OBLIGATIONS:
        raise GateFailure("HE source-correspondence obligation inventory changed")

    modeled = 0
    rejected = 0
    divergences = 0
    separate = 0
    for row in rows:
        if (
            not row["implementation_behavior"]
            or not row["gslt_witness"]
            or not row["limit"]
            or row["disposition"] not in DISPOSITIONS
        ):
            raise GateFailure(
                f"incomplete HE source obligation {row['obligation_id']}"
            )
        implementation_function = row["implementation_function"]
        if implementation_function != "HE program loader":
            pattern = FUNCTION_PATTERNS.get(implementation_function)
            if pattern is None or re.search(pattern, source) is None:
                raise GateFailure(
                    f"authority function is absent for {row['obligation_id']}"
                )
        for witness in row["gslt_witness"].split(","):
            kind, separator, value = witness.partition(":")
            if not separator or not value:
                raise GateFailure(
                    f"malformed GSLT witness for {row['obligation_id']}"
                )
            if kind == "rule" and value not in rules:
                raise GateFailure(
                    f"missing GSLT rule {value} for {row['obligation_id']}"
                )
            if kind == "prefix" and not any(
                rule.startswith(value) for rule in rules
            ):
                raise GateFailure(
                    f"missing GSLT rule family {value} for {row['obligation_id']}"
                )
            if kind == "boundary" and value not in BOUNDARIES:
                raise GateFailure(
                    f"unknown boundary {value} for {row['obligation_id']}"
                )
            if kind not in {"rule", "prefix", "boundary"}:
                raise GateFailure(
                    f"unknown witness kind {kind} for {row['obligation_id']}"
                )
        if row["disposition"] == "modeled-valid":
            modeled += 1
        elif row["disposition"] == "rejection-modeled":
            rejected += 1
        elif row["disposition"] == "implementation-divergence":
            divergences += 1
        else:
            separate += 1
    return modeled, rejected, divergences, separate


def check_contract(rules: set[str]) -> tuple[int, int, int]:
    with CONTRACT.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if tuple(reader.fieldnames or ()) != CONTRACT_FIELDS:
            raise GateFailure("HE contract-conformance schema changed")
        rows = list(reader)
    ids = [row["clause_id"] for row in rows]
    if len(ids) != len(set(ids)) or set(ids) != EXPECTED_CONTRACT_CLAUSES:
        raise GateFailure("HE contract-conformance clause inventory changed")

    counts = {name: 0 for name in CONTRACT_DISPOSITIONS}
    for row in rows:
        if any(not row[field] for field in CONTRACT_FIELDS):
            raise GateFailure(
                f"incomplete HE contract clause {row['clause_id']}"
            )
        if row["contract_basis"] not in CONTRACT_BASES:
            raise GateFailure(
                f"unknown contract basis for {row['clause_id']}"
            )
        if not row["written_reference"].startswith("docs/metta.md:"):
            raise GateFailure(
                f"unanchored written clause {row['clause_id']}"
            )
        for field in (
            "rust_relation",
            "cetta_relation",
            "leatta_relation",
            "gslt_relation",
        ):
            if row[field] not in CONTRACT_RELATIONS:
                raise GateFailure(
                    f"unknown {field} for {row['clause_id']}"
                )
        disposition = row["disposition"]
        if disposition not in CONTRACT_DISPOSITIONS:
            raise GateFailure(
                f"unknown disposition for {row['clause_id']}"
            )
        if disposition == "settled" and row["contract_basis"] not in {
            "written-direct",
            "written-delegation",
            "ratified-clarification",
            "ratified-extension",
        }:
            raise GateFailure(
                f"settled clause lacks a direct basis: {row['clause_id']}"
            )
        if disposition == "ratification-required" and (
            "candidate-clarification"
            not in {
                row["rust_relation"],
                row["cetta_relation"],
                row["leatta_relation"],
                row["gslt_relation"],
            }
        ):
            raise GateFailure(
                f"ratification clause has no candidate: {row['clause_id']}"
            )
        for witness in row["gslt_witness"].split(","):
            kind, separator, value = witness.partition(":")
            if not separator or not value:
                raise GateFailure(
                    f"malformed contract witness for {row['clause_id']}"
                )
            if kind == "rule" and value not in rules:
                raise GateFailure(
                    f"missing contract rule {value} for {row['clause_id']}"
                )
            if kind == "prefix" and not any(
                rule.startswith(value) for rule in rules
            ):
                raise GateFailure(
                    f"missing contract rule family {value} for {row['clause_id']}"
                )
            if kind == "boundary" and value not in BOUNDARIES:
                raise GateFailure(
                    f"unknown contract boundary {value} for {row['clause_id']}"
                )
            if kind not in {"rule", "prefix", "boundary"}:
                raise GateFailure(
                    f"unknown contract witness kind for {row['clause_id']}"
                )
        counts[disposition] += 1
    return (
        counts["settled"],
        counts["ratification-required"],
        counts["separate-layer"],
    )


def check_markers(label: str, source: str, markers: tuple[str, ...]) -> None:
    for marker in markers:
        if re.search(marker, source) is None:
            raise GateFailure(f"pinned {label} marker disappeared: {marker}")


def check_forbidden_markers(
    label: str, source: str, markers: tuple[str, ...]
) -> None:
    for marker in markers:
        if re.search(marker, source) is not None:
            raise GateFailure(f"pinned {label} unexpectedly contains: {marker}")


def check_generated_presentations() -> None:
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-he-source-correspondence-"
    ) as raw:
        directory = Path(raw)
        generated_classes = directory / "classes.metta"
        generated_reader = directory / "reader.metta"
        run_checked(
            [
                sys.executable,
                str(BUILDER),
                "--classes",
                str(generated_classes),
                "--reader",
                str(generated_reader),
            ],
            cwd=ROOT,
        )
        if generated_classes.read_bytes() != CLASSES.read_bytes():
            raise GateFailure("tracked HE scalar classes differ from the generator")
        if generated_reader.read_bytes() != READER.read_bytes():
            raise GateFailure("tracked HE reader differs from the generator")


def rust_whitespace_scalars() -> set[int]:
    source = """
fn main() {
    for value in 0u32..=0x10ffff {
        if let Some(scalar) = char::from_u32(value) {
            if scalar.is_whitespace() {
                println!("{}", value);
            }
        }
    }
}
"""
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-he-whitespace-"
    ) as raw:
        directory = Path(raw)
        probe = directory / "probe.rs"
        binary = directory / "probe"
        probe.write_text(source, encoding="utf-8")
        run_checked(
            ["rustc", "-O", "-o", str(binary), str(probe)],
            cwd=directory,
        )
        output = run_checked([str(binary)], cwd=directory)
    return {int(line) for line in output.decode("ascii").splitlines()}


def gslt_whitespace_scalars() -> set[int]:
    text = CLASSES.read_text(encoding="utf-8")
    matches = re.findall(
        r"\(rule member-he-whitespace-scalar-(\d+) "
        r"\(head \(member he-whitespace-scalar \(cp (\d+)\)\)\) "
        r"\(body\)\)",
        text,
    )
    if not matches or any(left != right for left, right in matches):
        raise GateFailure("HE whitespace facts are malformed")
    return {int(left) for left, _right in matches}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--he-root", type=Path, required=True)
    parser.add_argument("--cetta-root", type=Path, required=True)
    parser.add_argument("--mettapedia-root", type=Path, required=True)
    arguments = parser.parse_args()
    he_root = arguments.he_root.resolve()
    cetta_root = arguments.cetta_root.resolve()
    mettapedia_root = arguments.mettapedia_root.resolve()
    if not he_root.is_dir():
        raise GateFailure("pinned HE checkout is absent")
    if not cetta_root.is_dir():
        raise GateFailure("pinned CeTTa checkout is absent")
    if not mettapedia_root.is_dir():
        raise GateFailure("pinned MeTTapedia checkout is absent")

    specification, source, cetta_source, leatta_spec, leatta_parser = (
        authority_sources(he_root, cetta_root, mettapedia_root)
    )
    check_markers("HE written specification", specification, SPEC_MARKERS)
    check_markers("Rust HE parser", source, SOURCE_MARKERS)
    check_markers("CeTTa HE parser", cetta_source, CETTA_MARKERS)
    check_markers("LeaTTa HE syntax", leatta_spec, LEATTA_SPEC_MARKERS)
    check_markers("LeaTTa parser", leatta_parser, LEATTA_PARSER_MARKERS)
    check_forbidden_markers(
        "LeaTTa parser", leatta_parser, LEATTA_PARSER_FORBIDDEN_MARKERS
    )
    rules = presentation_rules()
    modeled, rejected, divergences, separate = check_manifest(source, rules)
    settled, ratification, separate_layers = check_contract(rules)
    check_generated_presentations()
    rust_whitespace = rust_whitespace_scalars()
    gslt_whitespace = gslt_whitespace_scalars()
    if rust_whitespace != gslt_whitespace:
        raise GateFailure(
            "HE whitespace relation differs from exhaustive Rust char behavior"
        )

    print(
        "(HEReaderSourceCorrespondenceV1Summary "
        f"{len(EXPECTED_OBLIGATIONS)} {modeled} {rejected} {divergences} "
        f"{separate} "
        f"{len(rust_whitespace)} {len(SOURCE_MARKERS)} 0)"
    )
    print(
        "(HEReaderContractConformanceV1Summary "
        f"{len(EXPECTED_CONTRACT_CLAUSES)} {settled} {ratification} "
        f"{separate_layers} {len(SPEC_MARKERS)} {len(CETTA_MARKERS)} "
        f"{len(LEATTA_SPEC_MARKERS)} {len(LEATTA_PARSER_MARKERS)} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
