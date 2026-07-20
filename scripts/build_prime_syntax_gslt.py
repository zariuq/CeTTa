#!/usr/bin/env python3
"""Emit Prime's scannerless reader as ordinary finite Horn/GSLT rules.

The emitted presentation is the authority.  This generator is only an authoring
convenience: the generic engine consumes the resulting .metta file without a
MeTTa-specific branch.
"""
from __future__ import annotations
from pathlib import Path
import argparse

ASCII_WS = {9, 10, 11, 12, 13, 32}
TOKEN_STOP = ASCII_WS | {34, 40, 41, 59}  # quote, (, ), ;
TOKEN_CHARS = set(range(1, 256)) - TOKEN_STOP
STRING_NORMAL = set(range(1, 256)) - {34, 92}
COMMENT_CHARS = set(range(1, 256)) - {10}
UNKNOWN_ESC = set(range(1, 256)) - {34, 92, 110, 114, 116}
DIGITS = set(range(48, 58))
NONZERO_DIGITS = set(range(49, 58))

# Disjoint DFA fallback classes.  All are subsets of TOKEN_CHARS.
CLASSES = {
    "whitespace": ASCII_WS,
    "skip-stop": set(range(1, 256)) - (ASCII_WS | {59}),
    "token-char": TOKEN_CHARS,
    "token-stop": TOKEN_STOP,
    "string-normal": STRING_NORMAL,
    "comment-char": COMMENT_CHARS,
    "unknown-escape": UNKNOWN_ESC,
    "digit": DIGITS,
    "nonzero-digit": NONZERO_DIGITS,
    # These four atom-initial bytes have Prime-specific structural roles.
    # They remain ordinary token bytes after a non-special first byte.
    "numeric-start-other": TOKEN_CHARS - ({36, 38, 42, 43, 45, 46, 64} | DIGITS),
    "dollar-tail-start": TOKEN_CHARS - {64},
    "amp-tail-start": TOKEN_CHARS - {64},
    "star-payload-start": set(range(1, 256)) - (ASCII_WS | {41, 59}),
    "sign-other": TOKEN_CHARS - ({46} | DIGITS),
    "int-other": TOKEN_CHARS - ({46, 47} | DIGITS),
    "float-other": TOKEN_CHARS - ({69, 101} | DIGITS),
    "leading-dot-other": TOKEN_CHARS - DIGITS,
    "exp-start-other": TOKEN_CHARS - ({43, 45} | DIGITS),
    "exp-sign-other": TOKEN_CHARS - DIGITS,
    "rat-start-other": TOKEN_CHARS - ({43, 45} | DIGITS),
    "rat-digits-other": TOKEN_CHARS - DIGITS,
}


def fact(rule_id: str, head: str) -> str:
    return f"""    (rule {rule_id}\n      (head {head})\n      (body))\n"""


def rule(rule_id: str, head: str, *body: str) -> str:
    b = "\n".join(f"        {g}" for g in body)
    return f"""    (rule {rule_id}\n      (head {head})\n      (body{(chr(10) + b) if body else ''}))\n"""


def code_units(text: str) -> str:
    """Render UTF-8 code units as the presentation's explicit cons/cp data."""
    out = "nil"
    for byte in reversed(text.encode("utf-8")):
        out = f"(cons (cp {byte}) {out})"
    return out


def symbol(text: str) -> str:
    return f"(symbol {code_units(text)})"


def unary_form(head: str, value: str) -> str:
    return f"(expression (cons {symbol(head)} (cons {value} nil)))"


def emit(path: Path, *, include_universal_names: bool = True) -> None:
    out: list[str] = []
    out.append("""; Prime concrete reader as an ordinary scannerless GSLT/Horn presentation.
; The input alphabet is UTF-8 code units.  No lexical callback, token table,
; regex engine, parser root field, or guest-language branch is required by the
; evaluator.  Skipping, strings, token boundaries, numeric recognition, the
; universal-name surface, expressions, and document structure are rules below.

(presentation PrimeSyntaxGSLTV0
  (types
    (type MeTTaDocument)
    (type MeTTaAtom)
    (type MeTTaAtoms)
    (type MeTTaStringBytes)
    (type NumericKind))
  (terms
    (constructor metta-document Grammar)
    (constructor metta-atoms Grammar)
    (constructor metta-atom Grammar)
    (constructor metta-expression Grammar)
    (constructor metta-string Grammar)
    (constructor metta-string-body Grammar)
    (constructor metta-token Grammar)
    (constructor metta-token-tail Grammar)
    (constructor metta-skip Grammar)
    (constructor metta-whitespace Grammar)
    (constructor metta-comment Grammar)
    (constructor metta-comment-body Grammar)
    (constructor document MeTTaAtoms MeTTaDocument)
    (constructor expression MeTTaAtoms MeTTaAtom)
    (constructor symbol Codepoints MeTTaAtom)
    (constructor variable Codepoints MeTTaAtom)
    (constructor structural-variable MeTTaAtom MeTTaAtom)
    (constructor string Codepoints MeTTaAtom)
    (constructor number NumericKind Codepoints MeTTaAtom)
    (constructor integer NumericKind)
    (constructor float NumericKind)
    (constructor rational NumericKind)
    (constructor skip Value)
    (constructor whitespace Value)
    (constructor comment Value)
    (constructor closed-atom MeTTaAtom Goal)
    (constructor closed-atoms MeTTaAtoms Goal)
    (constructor classify-token Codepoints MeTTaAtom Goal)
    (constructor classify-start Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-sign Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-int Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-leading-dot Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-float-dot Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-frac Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-exp-start Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-exp-sign Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-exp-digits Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-rat-start Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-rat-sign Codepoints Codepoints MeTTaAtom Goal)
    (constructor classify-rat-digits Name Codepoints Codepoints MeTTaAtom Goal)
    (constructor decode-escape Codepoint Codepoint Goal))
  (equations)
  (rewrites
""")

    for cname in sorted(CLASSES):
        for cp in sorted(CLASSES[cname]):
            out.append(fact(f"member-{cname}-{cp}", f"(member {cname} (cp {cp}))"))

    # String escape semantics matches parser.c: named escapes, otherwise identity.
    for cp, dec, name in [(110, 10, "n"), (114, 13, "r"), (116, 9, "t"),
                          (34, 34, "quote"), (92, 92, "backslash")]:
        out.append(fact(f"decode-escape-{name}", f"(decode-escape (cp {cp}) (cp {dec}))"))
    out.append(rule("decode-escape-identity", "(decode-escape ?cp ?cp)",
                    "(member unknown-escape ?cp)"))

    # Deterministic maximal skipping.  The stop rules make skip consume the
    # complete whitespace/comment prefix rather than every possible prefix.
    out += [
        fact("parse-metta-skip-eof", "(parse metta-skip nil skip nil)"),
        rule("parse-metta-skip-stop",
             "(parse metta-skip (cons ?cp ?tail) skip (cons ?cp ?tail))",
             "(member skip-stop ?cp)"),
        rule("parse-metta-skip-whitespace", "(parse metta-skip ?input skip ?rest)",
             "(parse metta-whitespace ?input whitespace ?middle)",
             "(parse metta-skip ?middle skip ?rest)"),
        rule("parse-metta-skip-comment", "(parse metta-skip ?input skip ?rest)",
             "(parse metta-comment ?input comment ?middle)",
             "(parse metta-skip ?middle skip ?rest)"),
        rule("parse-metta-whitespace",
             "(parse metta-whitespace (cons ?cp ?rest) whitespace ?rest)",
             "(member whitespace ?cp)"),
        rule("parse-metta-comment",
             "(parse metta-comment (cons (cp 59) ?input) comment ?rest)",
             "(parse metta-comment-body ?input comment ?rest)"),
        fact("parse-metta-comment-eof", "(parse metta-comment-body nil comment nil)"),
        fact("parse-metta-comment-newline",
             "(parse metta-comment-body (cons (cp 10) ?rest) comment ?rest)"),
        rule("parse-metta-comment-char",
             "(parse metta-comment-body (cons ?cp ?tail) comment ?rest)",
             "(member comment-char ?cp)",
             "(parse metta-comment-body ?tail comment ?rest)"),
    ]

    # Strings decode escapes during parsing, exactly as the native reader does.
    out += [
        rule("parse-metta-string",
             "(parse metta-string (cons (cp 34) ?input) (string ?bytes) ?rest)",
             "(parse metta-string-body ?input ?bytes ?rest)"),
        fact("parse-metta-string-end",
             "(parse metta-string-body (cons (cp 34) ?rest) nil ?rest)"),
        rule("parse-metta-string-escape",
             "(parse metta-string-body (cons (cp 92) (cons ?esc ?tail)) (cons ?decoded ?more) ?rest)",
             "(decode-escape ?esc ?decoded)",
             "(parse metta-string-body ?tail ?more ?rest)"),
        rule("parse-metta-string-normal",
             "(parse metta-string-body (cons ?cp ?tail) (cons ?cp ?more) ?rest)",
             "(member string-normal ?cp)",
             "(parse metta-string-body ?tail ?more ?rest)"),
    ]

    # Deterministic whole-token capture.  It accepts adjacent atoms because a
    # structural delimiter is a positive token boundary; no whitespace is needed.
    out += [
        fact("parse-metta-token-tail-eof", "(parse metta-token-tail nil nil nil)"),
        rule("parse-metta-token-tail-stop",
             "(parse metta-token-tail (cons ?cp ?tail) nil (cons ?cp ?tail))",
             "(member token-stop ?cp)"),
        rule("parse-metta-token-tail-char",
             "(parse metta-token-tail (cons ?cp ?tail) (cons ?cp ?more) ?rest)",
             "(member token-char ?cp)",
             "(parse metta-token-tail ?tail ?more ?rest)"),
        rule("parse-metta-token",
             "(parse metta-token (cons ?cp ?tail) ?atom ?rest)",
             "(member token-char ?cp)",
             "(parse metta-token-tail ?tail ?more ?rest)",
             "(classify-token (cons ?cp ?more) ?atom)"),
    ]

    # A disjoint regular classifier for variables and ordinary decimal numeric
    # spellings.  Everything else remains an uninterpreted symbol.
    out.append(rule("classify-token-start", "(classify-token ?raw ?atom)",
                    "(classify-start ?raw ?raw ?atom)"))
    out += [
        fact("classify-dollar-alone",
             "(classify-start (cons (cp 36) nil) ?original (symbol ?original))"),
        rule("classify-dollar-variable",
             "(classify-start (cons (cp 36) (cons ?cp ?tail)) ?original (variable (cons ?cp ?tail)))",
             "(member dollar-tail-start ?cp)"),
        fact("classify-amp-alone",
             "(classify-start (cons (cp 38) nil) ?original (symbol ?original))"),
        rule("classify-amp-symbol",
             "(classify-start (cons (cp 38) (cons ?cp ?tail)) ?original (symbol ?original))",
             "(member amp-tail-start ?cp)"),
        fact("classify-star-alone",
             "(classify-start (cons (cp 42) nil) ?original (symbol ?original))"),
        rule("classify-start-digit",
             "(classify-start (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-int ?tail ?original ?atom)"),
        rule("classify-start-plus",
             "(classify-start (cons (cp 43) ?tail) ?original ?atom)",
             "(classify-sign ?tail ?original ?atom)"),
        rule("classify-start-minus",
             "(classify-start (cons (cp 45) ?tail) ?original ?atom)",
             "(classify-sign ?tail ?original ?atom)"),
        rule("classify-start-dot",
             "(classify-start (cons (cp 46) ?tail) ?original ?atom)",
             "(classify-leading-dot ?tail ?original ?atom)"),
        rule("classify-start-symbol",
             "(classify-start (cons ?cp ?tail) ?original (symbol ?original))",
             "(member numeric-start-other ?cp)"),
    ]

    out += [
        fact("classify-sign-end", "(classify-sign nil ?original (symbol ?original))"),
        rule("classify-sign-digit", "(classify-sign (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-int ?tail ?original ?atom)"),
        rule("classify-sign-dot", "(classify-sign (cons (cp 46) ?tail) ?original ?atom)",
             "(classify-leading-dot ?tail ?original ?atom)"),
        rule("classify-sign-symbol", "(classify-sign (cons ?cp ?tail) ?original (symbol ?original))",
             "(member sign-other ?cp)"),

        fact("classify-int-end", "(classify-int nil ?original (number integer ?original))"),
        rule("classify-int-digit", "(classify-int (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-int ?tail ?original ?atom)"),
        rule("classify-int-dot", "(classify-int (cons (cp 46) ?tail) ?original ?atom)",
             "(classify-float-dot ?tail ?original ?atom)"),
        rule("classify-int-slash", "(classify-int (cons (cp 47) ?tail) ?original ?atom)",
             "(classify-rat-start ?tail ?original ?atom)"),
        rule("classify-int-symbol", "(classify-int (cons ?cp ?tail) ?original (symbol ?original))",
             "(member int-other ?cp)"),

        fact("classify-leading-dot-end", "(classify-leading-dot nil ?original (symbol ?original))"),
        rule("classify-leading-dot-digit", "(classify-leading-dot (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-frac ?tail ?original ?atom)"),
        rule("classify-leading-dot-symbol", "(classify-leading-dot (cons ?cp ?tail) ?original (symbol ?original))",
             "(member leading-dot-other ?cp)"),

        fact("classify-float-dot-end", "(classify-float-dot nil ?original (number float ?original))"),
        rule("classify-float-dot-digit", "(classify-float-dot (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-frac ?tail ?original ?atom)"),
        rule("classify-float-dot-e", "(classify-float-dot (cons (cp 101) ?tail) ?original ?atom)",
             "(classify-exp-start ?tail ?original ?atom)"),
        rule("classify-float-dot-E", "(classify-float-dot (cons (cp 69) ?tail) ?original ?atom)",
             "(classify-exp-start ?tail ?original ?atom)"),
        rule("classify-float-dot-symbol", "(classify-float-dot (cons ?cp ?tail) ?original (symbol ?original))",
             "(member float-other ?cp)"),

        fact("classify-frac-end", "(classify-frac nil ?original (number float ?original))"),
        rule("classify-frac-digit", "(classify-frac (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-frac ?tail ?original ?atom)"),
        rule("classify-frac-e", "(classify-frac (cons (cp 101) ?tail) ?original ?atom)",
             "(classify-exp-start ?tail ?original ?atom)"),
        rule("classify-frac-E", "(classify-frac (cons (cp 69) ?tail) ?original ?atom)",
             "(classify-exp-start ?tail ?original ?atom)"),
        rule("classify-frac-symbol", "(classify-frac (cons ?cp ?tail) ?original (symbol ?original))",
             "(member float-other ?cp)"),

        fact("classify-exp-start-end", "(classify-exp-start nil ?original (symbol ?original))"),
        rule("classify-exp-start-plus", "(classify-exp-start (cons (cp 43) ?tail) ?original ?atom)",
             "(classify-exp-sign ?tail ?original ?atom)"),
        rule("classify-exp-start-minus", "(classify-exp-start (cons (cp 45) ?tail) ?original ?atom)",
             "(classify-exp-sign ?tail ?original ?atom)"),
        rule("classify-exp-start-digit", "(classify-exp-start (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-exp-digits ?tail ?original ?atom)"),
        rule("classify-exp-start-symbol", "(classify-exp-start (cons ?cp ?tail) ?original (symbol ?original))",
             "(member exp-start-other ?cp)"),

        fact("classify-exp-sign-end", "(classify-exp-sign nil ?original (symbol ?original))"),
        rule("classify-exp-sign-digit", "(classify-exp-sign (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-exp-digits ?tail ?original ?atom)"),
        rule("classify-exp-sign-symbol", "(classify-exp-sign (cons ?cp ?tail) ?original (symbol ?original))",
             "(member exp-sign-other ?cp)"),

        fact("classify-exp-digits-end", "(classify-exp-digits nil ?original (number float ?original))"),
        rule("classify-exp-digits-more", "(classify-exp-digits (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-exp-digits ?tail ?original ?atom)"),
        rule("classify-exp-digits-symbol", "(classify-exp-digits (cons ?cp ?tail) ?original (symbol ?original))",
             "(member exp-sign-other ?cp)"),

        fact("classify-rat-start-end", "(classify-rat-start nil ?original (symbol ?original))"),
        rule("classify-rat-start-plus", "(classify-rat-start (cons (cp 43) ?tail) ?original ?atom)",
             "(classify-rat-sign ?tail ?original ?atom)"),
        rule("classify-rat-start-minus", "(classify-rat-start (cons (cp 45) ?tail) ?original ?atom)",
             "(classify-rat-sign ?tail ?original ?atom)"),
        rule("classify-rat-start-zero", "(classify-rat-start (cons (cp 48) ?tail) ?original ?atom)",
             "(classify-rat-digits zero ?tail ?original ?atom)"),
        rule("classify-rat-start-nonzero", "(classify-rat-start (cons ?cp ?tail) ?original ?atom)",
             "(member nonzero-digit ?cp)", "(classify-rat-digits nonzero ?tail ?original ?atom)"),
        rule("classify-rat-start-symbol", "(classify-rat-start (cons ?cp ?tail) ?original (symbol ?original))",
             "(member rat-start-other ?cp)"),

        fact("classify-rat-sign-end", "(classify-rat-sign nil ?original (symbol ?original))"),
        rule("classify-rat-sign-zero", "(classify-rat-sign (cons (cp 48) ?tail) ?original ?atom)",
             "(classify-rat-digits zero ?tail ?original ?atom)"),
        rule("classify-rat-sign-nonzero", "(classify-rat-sign (cons ?cp ?tail) ?original ?atom)",
             "(member nonzero-digit ?cp)", "(classify-rat-digits nonzero ?tail ?original ?atom)"),
        rule("classify-rat-sign-symbol", "(classify-rat-sign (cons ?cp ?tail) ?original (symbol ?original))",
             "(member rat-digits-other ?cp)"),

        fact("classify-rat-zero-end", "(classify-rat-digits zero nil ?original (symbol ?original))"),
        fact("classify-rat-nonzero-end", "(classify-rat-digits nonzero nil ?original (number rational ?original))"),
        rule("classify-rat-zero-more-zero", "(classify-rat-digits zero (cons (cp 48) ?tail) ?original ?atom)",
             "(classify-rat-digits zero ?tail ?original ?atom)"),
        rule("classify-rat-zero-more-nonzero", "(classify-rat-digits zero (cons ?cp ?tail) ?original ?atom)",
             "(member nonzero-digit ?cp)", "(classify-rat-digits nonzero ?tail ?original ?atom)"),
        rule("classify-rat-nonzero-more", "(classify-rat-digits nonzero (cons ?cp ?tail) ?original ?atom)",
             "(member digit ?cp)", "(classify-rat-digits nonzero ?tail ?original ?atom)"),
        rule("classify-rat-zero-symbol", "(classify-rat-digits zero (cons ?cp ?tail) ?original (symbol ?original))",
             "(member rat-digits-other ?cp)"),
        rule("classify-rat-nonzero-symbol", "(classify-rat-digits nonzero (cons ?cp ?tail) ?original (symbol ?original))",
             "(member rat-digits-other ?cp)"),
    ]

    # Closedness is a relation in the presentation, not a host callback.  It is
    # required for persistent descriptors and structural matcher identities;
    # ordinary quotation itself remains available for open syntax.
    out += [
        fact("closed-atom-symbol", "(closed-atom (symbol ?raw))"),
        fact("closed-atom-string", "(closed-atom (string ?raw))"),
        fact("closed-atom-number", "(closed-atom (number ?kind ?raw))"),
        rule("closed-atom-expression", "(closed-atom (expression ?atoms))",
             "(closed-atoms ?atoms)"),
        fact("closed-atoms-empty", "(closed-atoms nil)"),
        rule("closed-atoms-cons", "(closed-atoms (cons ?atom ?atoms))",
             "(closed-atom ?atom)", "(closed-atoms ?atoms)"),
    ]

    # Universal-name surface.  The rules directly construct the same canonical
    # quote/unquote/resolve-name expressions as the native reader.  Structural
    # variables retain their structural key as a distinct reader node.
    if include_universal_names:
        quote_value = unary_form("quote", "?payload")
        unquote_value = unary_form("unquote", "?payload")
        resolve_value = unary_form("resolve-name", unary_form("quote", "?payload"))
        out += [
            rule("parse-prime-quote",
                 f"(parse metta-atom (cons (cp 64) ?after-prefix) {quote_value} ?rest)",
                 "(parse metta-skip ?after-prefix skip ?payload-input)",
                 "(parse metta-atom ?payload-input ?payload ?rest)"),
            rule("parse-prime-structural-variable",
                 "(parse metta-atom (cons (cp 36) (cons (cp 64) ?after-prefix)) (structural-variable ?payload) ?rest)",
                 "(parse metta-skip ?after-prefix skip ?payload-input)",
                 "(parse metta-atom ?payload-input ?payload ?rest)",
                 "(closed-atom ?payload)"),
            rule("parse-prime-resolve-name",
                 f"(parse metta-atom (cons (cp 38) (cons (cp 64) ?after-prefix)) {resolve_value} ?rest)",
                 "(parse metta-skip ?after-prefix skip ?payload-input)",
                 "(parse metta-atom ?payload-input ?payload ?rest)",
                 "(closed-atom ?payload)"),
            rule("parse-prime-unquote",
                 f"(parse metta-atom (cons (cp 42) (cons ?next ?tail)) {unquote_value} ?rest)",
                 "(member star-payload-start ?next)",
                 "(parse metta-atom (cons ?next ?tail) ?payload ?rest)"),
        ]

    # Atom, expression, sequence, and whole-document grammar.  Every list is
    # explicit data.  Empty expressions and empty documents are admitted.
    out += [
        rule("parse-metta-atom-expression", "(parse metta-atom ?input ?atom ?rest)",
             "(parse metta-expression ?input ?atom ?rest)"),
        rule("parse-metta-atom-string", "(parse metta-atom ?input ?atom ?rest)",
             "(parse metta-string ?input ?atom ?rest)"),
        rule("parse-metta-atom-token", "(parse metta-atom ?input ?atom ?rest)",
             "(parse metta-token ?input ?atom ?rest)"),
        fact("parse-metta-atoms-empty", "(parse metta-atoms ?input nil ?input)"),
        rule("parse-metta-atoms-cons",
             "(parse metta-atoms ?input (cons ?atom ?atoms) ?rest)",
             "(parse metta-atom ?input ?atom ?middle)",
             "(parse metta-skip ?middle skip ?after-skip)",
             "(parse metta-atoms ?after-skip ?atoms ?rest)"),
        rule("parse-metta-expression",
             "(parse metta-expression (cons (cp 40) ?after-open) (expression ?atoms) ?rest)",
             "(parse metta-skip ?after-open skip ?content)",
             "(parse metta-atoms ?content ?atoms (cons (cp 41) ?rest))"),
        rule("parse-metta-document",
             "(parse metta-document ?input (document ?atoms) nil)",
             "(parse metta-skip ?input skip ?content)",
             "(parse metta-atoms ?content ?atoms nil)"),
        fact("def-metta-document", "(definition metta-document metta-document)"),
    ]

    out.append("  ))\n")
    path.write_text("".join(out), encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("output", type=Path)
    ap.add_argument("--without-universal-names", action="store_true")
    args = ap.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    emit(args.output, include_universal_names=not args.without_universal_names)

if __name__ == "__main__":
    main()
