#!/usr/bin/env python3
"""Qualify self-contained parser restoration through public profiles."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess

from test_composable_syntax_v1 import form, lets, operand, origin, parts, rule, text
from test_lbnf_values_v1 import CFLEX, headed, library_import, lower_read, output_values

ROOT = Path(__file__).resolve().parents[1]


def run(binary, language, output, name, program):
    source = output / f"{language}-{name}.metta"
    source.write_text(program, encoding="utf-8")
    command = [str(binary), "--lang", language]
    if language != "prime":
        command += ["--profile", "extended"]
    result = subprocess.run(command + [str(source)], cwd=ROOT,
                            capture_output=True, text=True, timeout=180)
    (output / f"{language}-{name}.stdout").write_text(result.stdout)
    (output / f"{language}-{name}.stderr").write_text(result.stderr)
    if result.returncode:
        raise AssertionError((language, name, result.returncode, result.stderr))
    return output_values(result.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime/parser-artifacts-public")
    args = parser.parse_args()
    binary, output = args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scannerless, scalar_profile, _, _ = parts("StoredScalar", [rule("Same", "a")])
    cases = [
        ("scannerless", form("langdef:load-authored-parser-values", scannerless, scalar_profile), "a"),
        ("tokenized", lets([("$stored-syntax", lower_read(
            'First. S ::= "a"; Second. S ::= "a"; comment "/*" "*/";',
            name="StoredToken", start=text("S")))],
            form("langdef:load-syntax-gslt-parser", "$stored-syntax")), " /* comment */ a "),
        ("latin1", lets([("$stored-syntax", lower_read('Name. S ::= Ident;',
            name="StoredLatin1", start=text("S")))],
            form("langdef:load-syntax-gslt-parser", "$stored-syntax")), " ÿz "),
        ("c-flex", lets([("$stored-syntax", lower_read('Quoted. S ::= String;',
            name="StoredCFlex", start=text("S"), policy=CFLEX))],
            form("langdef:load-syntax-gslt-parser", "$stored-syntax")), '"line\\n"'),
    ]
    count = 0
    for language in ("prime", "he", "petta"):
        for name, load, sample in cases:
            path = output / f"{language}-{name}.tpp"
            source = library_import() + "!" + lets([
                ("$original", load),
                ("$original-value", form("langdef:parser-parse-text", "$original", text(sample))),
                ("$original-evidence", form("langdef:parse-authored-text-evidence", "$original", text(sample), "10000")),
                ("$frozen", form("langdef:freeze-parser", "$original", text(str(path)))),
                ("$close-original", form("langdef:close-parser", "$original")),
                ("$trusted", form("langdef:load-parser-artifact", text(str(path)))),
                ("$checked", form("langdef:load-parser-artifact-checked", text(str(path)))),
                ("$trusted-value", form("langdef:parser-parse-text", "$trusted", text(sample))),
                ("$checked-value", form("langdef:parser-parse-text", "$checked", text(sample))),
                ("$restored-evidence", form("langdef:parse-authored-text-evidence", "$trusted", text(sample), "10000")),
                ("$info", form("langdef:parser-info", "$trusted")),
                ("$close-trusted", form("langdef:close-parser", "$trusted")),
                ("$surviving-value", form("langdef:parser-parse-text", "$checked", text(sample))),
                ("$close-checked", form("langdef:close-parser", "$checked")),
            ], "(function (return (ParserArtifactControlV1 $frozen $original-value $trusted-value "
               "$checked-value $surviving-value $original-evidence $restored-evidence $info "
               "$close-original $close-trusted $close-checked)))") + "\n"
            values = run(binary, language, output, name, source)
            records = [value for value in values if headed(value, "ParserArtifactControlV1", 11)]
            if len(records) != 1:
                raise AssertionError((language, name, values))
            record = records[0]
            if not headed(record[1], "ParserArtifactV1", 2):
                raise AssertionError((language, name, "freeze", record[1]))
            if not headed(record[2], "ParserAcceptedV1", 2) or any(record[i] != record[2] for i in (3, 4, 5)):
                raise AssertionError((language, name, "restored values/spans/multiplicity", record[2:6]))
            if record[6] != record[7]:
                raise AssertionError((language, name, "packed evidence changed", record[6:8]))
            if not headed(record[8], "ParserInfoV1", 6) or record[8][4] != 2:
                raise AssertionError((language, name, "independent shared handles", record[8]))
            original = path.read_bytes()
            # The content identity may be wrong while the format is intact.
            # Trusted loading must not perform the integrity audit implicitly.
            corrupted = bytearray(original)
            corrupted[72] = ord("1") if corrupted[72] != ord("1") else ord("2")
            corrupt_path = output / f"{language}-{name}-wrong-digest.tpp"
            corrupt_path.write_bytes(corrupted)
            checks = "!(import! &self langdef)\n!" + lets([
                ("$trusted", form("langdef:load-parser-artifact", text(str(corrupt_path)))),
                ("$value", form("langdef:parser-parse-text", "$trusted", text(sample))),
                ("$checked", form("langdef:load-parser-artifact-checked", text(str(corrupt_path)))),
                ("$closed", form("langdef:close-parser", "$trusted")),
            ], "(function (return (ParserArtifactNegativeV1 $value $checked $closed)))") + "\n"
            negative = run(binary, language, output, name + "-corruption", checks)
            negative = [value for value in negative if headed(value, "ParserArtifactNegativeV1", 3)]
            if len(negative) != 1 or not headed(negative[0][1], "ParserAcceptedV1", 2) or not headed(negative[0][2], "ParserFailureV1", 1):
                raise AssertionError((language, name, "trusted/checked separation", negative))
            if list(output.glob(path.name + ".partial.*")):
                raise AssertionError((language, name, "unfinished publication"))
            count += 8
            if name == "tokenized":
                # Re-sign a shape-compatible metadata change. Trusted loading
                # decodes it; the explicit audit must compare lexical priority
                # with the stored profile rather than accepting the digest.
                match = re.search(rb'( TokenTextV1 [0-9]+ )([0-9]+)( \(ParserLexerSuffixV1)', original)
                if not match:
                    raise AssertionError("stored lexer priority control has an unexpected shape")
                corrupted = bytearray(original)
                at = match.end(2) - 1
                corrupted[at] = ord("0") if corrupted[at] != ord("0") else ord("1")
                corrupted[72:136] = b"0" * 64
                corrupted[72:136] = hashlib.sha256(
                    b"cetta-general-parser-artifact-v1" + corrupted).hexdigest().encode()
                changed_path = output / f"{language}-{name}-wrong-priority.tpp"
                changed_path.write_bytes(corrupted)
                checks = "!(import! &self langdef)\n!" + lets([
                    ("$trusted", form("langdef:load-parser-artifact", text(str(changed_path)))),
                    ("$info", form("langdef:parser-info", "$trusted")),
                    ("$checked", form("langdef:load-parser-artifact-checked", text(str(changed_path)))),
                    ("$closed", form("langdef:close-parser", "$trusted")),
                ], "(function (return (LexerAuditControlV1 $info $checked $closed)))") + "\n"
                checked = [value for value in run(binary, language, output, "lexer-priority-audit", checks)
                           if headed(value, "LexerAuditControlV1", 3)]
                if len(checked) != 1 or not headed(checked[0][1], "ParserInfoV1", 6) or \
                        not headed(checked[0][2], "ParserFailureV1", 1) or \
                        "lexer metadata" not in checked[0][2][1].text:
                    raise AssertionError((language, "shape-compatible lexer priority must fail explicit audit", checked))
                count += 3
        original_syntax = operand("OriginOwner", [rule("Same", "a")], sites=[origin("first", 0)])
        other_syntax = operand("OriginOwner", [rule("Same", "a")], sites=[origin("second", 0)])
        provenance = "!(import! &self langdef)\n!" + lets([
            ("$syntax", original_syntax), ("$other-syntax", other_syntax),
            ("$first", form("langdef:load-syntax-gslt-parser", "$syntax")),
            ("$again", form("langdef:load-syntax-gslt-parser", "$syntax")),
            ("$other", form("langdef:load-syntax-gslt-parser", "$other-syntax")),
            ("$first-info", form("langdef:parser-info", "$first")),
            ("$other-info", form("langdef:parser-info", "$other")),
            ("$closed-first", form("langdef:close-parser", "$first")),
            ("$closed-again", form("langdef:close-parser", "$again")),
            ("$closed-other", form("langdef:close-parser", "$other")),
        ], "(function (return (ParserOriginsControlV1 $first-info $other-info)))") + "\n"
        values = [value for value in run(binary, language, output, "origin-sharing", provenance)
                  if headed(value, "ParserOriginsControlV1", 2)]
        if len(values) != 1 or any(not headed(info, "ParserInfoV1", 6) for info in values[0][1:]) or \
                values[0][1][4] != 2 or values[0][2][4] != 1:
            raise AssertionError((language, "origin-sensitive preparation sharing", values))
        count += 2
        # The digest can be valid while a stored, shape-compatible decision
        # is false. Certification must check the decision, not only its hash.
        path = output / f"{language}-wrong-decision.tpp"
        syntax = operand("AuditDecisions", [rule("A", "a"), rule("B", "b")])
        publication = "!(import! &self langdef)\n!" + lets([
            ("$syntax", syntax),
            ("$parser", form("langdef:compile-parser", "$syntax", "auto")),
            ("$frozen", form("langdef:freeze-parser", "$parser", text(str(path)))),
            ("$closed", form("langdef:close-parser", "$parser")),
        ], "(function (return $frozen))") + "\n"
        published = run(binary, language, output, "publish-decision-control", publication)
        if not any(headed(value, "ParserArtifactV1", 2) for value in published):
            raise AssertionError((language, "decision control publication", published))
        content = path.read_bytes()
        original_path = output / f"{language}-original-decisions.tpp"
        original_path.write_bytes(content)
        pattern = rb'(PredictiveProgramStoredV1(?: [0-9]+){8} \()([^()]*)\)'
        match = re.search(pattern, content)
        decision = re.search(rb'(?<![0-9])[01](?![0-9])', match[2]) if match else None
        if not match or not decision:
            raise AssertionError("stored predictive control has an unexpected shape")
        mutated = bytearray(content)
        mutated[match.start(2) + decision.start()] = ord("1") if decision[0] == b"0" else ord("0")
        mutated[72:136] = b"0" * 64
        mutated[72:136] = hashlib.sha256(b"cetta-general-parser-artifact-v1" + mutated).hexdigest().encode()
        path.write_bytes(mutated)
        checks = "!(import! &self langdef)\n!" + lets([
            ("$trusted", form("langdef:load-parser-artifact", text(str(path)))),
            ("$info", form("langdef:parser-info", "$trusted")),
            ("$checked", form("langdef:load-parser-artifact-checked", text(str(path)))),
            ("$closed", form("langdef:close-parser", "$trusted")),
        ], "(function (return (DecisionAuditControlV1 $info $checked $closed)))") + "\n"
        checks = [value for value in run(binary, language, output, "decision-audit", checks)
                  if headed(value, "DecisionAuditControlV1", 3)]
        if len(checks) != 1 or not headed(checks[0][1], "ParserInfoV1", 6) or \
                not headed(checks[0][2], "ParserFailureV1", 1) or \
                "predictive decisions" not in checks[0][2][1].text:
            raise AssertionError((language, "shape-compatible decision corruption must fail explicit audit", checks))
        count += 3
        warm = "!(import! &self langdef)\n!" + lets([
            ("$original", form("langdef:load-parser-artifact-checked", text(str(original_path)))),
            ("$different", form("langdef:load-parser-artifact", text(str(path)))),
            ("$same-different", form("langdef:load-parser-artifact", text(str(path)))),
            ("$original-info", form("langdef:parser-info", "$original")),
            ("$different-info", form("langdef:parser-info", "$different")),
            ("$same-info", form("langdef:parser-info", "$same-different")),
            ("$close-original", form("langdef:close-parser", "$original")),
            ("$close-different", form("langdef:close-parser", "$different")),
            ("$close-same", form("langdef:close-parser", "$same-different")),
        ], "(function (return (ArtifactSharingIdentityV1 $original-info $different-info $same-info)))") + "\n"
        identities = [value for value in run(binary, language, output, "warm-artifact-identity", warm)
                      if headed(value, "ArtifactSharingIdentityV1", 3)]
        if len(identities) != 1 or any(not headed(info, "ParserInfoV1", 6) for info in identities[0][1:]) or \
                [info[4] for info in identities[0][1:]] != [1, 2, 2]:
            raise AssertionError((language, "different saved decisions must not alias by source identity", identities))
        count += 3
        print(f"PASS: {language} restored parser values, multiplicity, evidence, handles, origins and integrity")
    print(f"(ParserArtifactsPublicV1Summary {count} 0)")


if __name__ == "__main__":
    main()
