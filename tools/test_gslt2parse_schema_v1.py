#!/usr/bin/env python3
"""Positive and destructive canaries for the finite Horn GSLT v1 schema."""

from __future__ import annotations

import csv
from hashlib import sha256
from pathlib import Path
import re
import tempfile

import gslt2parse_schema_v1 as schema
import migrate_gslt_presentation_v0 as migration


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
CORE = PRESENTATIONS / "core" / "syntax_core_v1.metta"
TOY = PRESENTATIONS / "canaries" / "two_char_v1.metta"
WRONG_ARITY = PRESENTATIONS / "mutations" / "two_char_wrong_arity_v1.metta"
WITHOUT_DEFINITION = (
    PRESENTATIONS / "mutations" / "two_char_without_definition_v1.metta"
)
CHAR_CORE = PRESENTATIONS / "shared" / "char_core_v1.metta"
METAMATH = ROOT / "langdef" / "metamath" / "syntax_v1.metta"
TPTP = ROOT / "langdef" / "tptp" / "syntax_fof_cnf_v1.metta"
TPTP_UNICODE_SCALARS = ROOT / "langdef" / "tptp" / "unicode_scalar_classes_v1.metta"
LOOKAHEAD = PRESENTATIONS / "shared" / "lookahead_core_v1.metta"
MEGALODON = PRESENTATIONS / "languages" / "megalodon_dynamic_v1.metta"
GROUND_RELATIONS = PRESENTATIONS / "shared" / "ground_relations_v1.metta"
HE_READER_SCALARS = PRESENTATIONS / "shared" / "he_reader_scalar_classes_v1.metta"
CETTA_PRIME_SCALARS = (
    PRESENTATIONS / "shared" / "cetta_prime_scalar_classes_v1.metta"
)
CETTA_PRIME_READER = PRESENTATIONS / "languages" / "cetta_prime_reader_v1.metta"
CETTA_RHO_SCALARS = PRESENTATIONS / "shared" / "cetta_rho_scalar_classes_v1.metta"
CETTA_RHO_LEXICAL = PRESENTATIONS / "shared" / "cetta_rho_lexical_v1.metta"
CETTA_RHO_ABSTRACT = PRESENTATIONS / "shared" / "rho_abstract_syntax_v1.metta"
CETTA_RHO_RUNTIME_ABI = (
    PRESENTATIONS / "shared" / "rho_runtime_term_abi_v1.metta"
)
CETTA_RHO_READER = PRESENTATIONS / "languages" / "cetta_rho_reader_v1.metta"
CETTA_RHO_MRHO_READER = (
    PRESENTATIONS / "languages" / "cetta_rho_mrho_reader_v1.metta"
)
CETTA_COST_RHO_READER = (
    PRESENTATIONS / "languages" / "cetta_cost_rho_reader_v1.metta"
)
COMPILER = PRESENTATIONS / "compiler" / "syntax_compiler_v1.metta"
PARSER_PACK_CORE = PRESENTATIONS / "parserpack" / "parser_pack_core_v1.metta"
RELATIONAL_CFG_LOWERING = (
    PRESENTATIONS / "compiler" / "relational_cfg_lowering_v1.metta"
)
RELATIONAL_CFG_LINK = PRESENTATIONS / "compiler" / "relational_cfg_link_v1.metta"
PARSER_PACK_COMPILER = (
    PRESENTATIONS / "compiler" / "parser_pack_compiler_v1.metta"
)
REFLECTION = PRESENTATIONS / "reflection" / "finite_horn_reflection_v1.metta"
PROVENANCE = PRESENTATIONS.parent / "migrated_sources.tsv"
MIGRATION_FIXTURE = PRESENTATIONS.parent / "migration_fixtures" / "typed_toy_v0.metta"
EXPECTED_PACKAGE_DIGEST = (
    "bd763693062eeaea7e95fe73f036af4e1707fe1f338c25345a622de25d48e7a4"
)
EXPECTED_METAMATH_DIGEST = (
    "8c1b50b54686eed8b6e11ff61160cf17db79d814b933896aca78f76638043b82"
)
EXPECTED_TPTP_DIGEST = (
    "9fb1551ab2bba0ccce30839d82ebba58ae0421deeec49cd4a5f3e5a91ffbd00e"
)
EXPECTED_MEGALODON_DIGEST = (
    "2eba88d8bb4681540cb84bf6dd15e183cf83ad2821ae682cb94a754b4f2eb36a"
)
EXPECTED_CETTA_PRIME_READER_DIGEST = (
    "993ef4d50f23ed8738a4a35cac9b09855b4843a753e75f6e80290451298b0438"
)
EXPECTED_CETTA_RHO_READER_DIGEST = (
    "722739d757a25e5ff6c982682f9fb8e024db8148707fcaf8868f4035d2b6b4b8"
)
EXPECTED_CETTA_COST_RHO_READER_DIGEST = (
    "518102c33fbcbff3bce0e6cf6f08abe441449121dd3cee3bd6a8b7d893d289ea"
)
EXPECTED_CETTA_RHO_ABSTRACT_DIGEST = (
    "2055fabd3cbf7915384030a7b126d2604e98e42f4eb7efeb71943093f3cbadbd"
)
EXPECTED_CETTA_RHO_MRHO_READER_DIGEST = (
    "1609516d5b30e8e25f75ab35ef448d056c6266bec13ded390a9e78fb0be46b84"
)
EXPECTED_COMPILER_DIGEST = (
    "0186885115acae44b9e982bc6fc19c236f35fda523e3c94abc1a96c0d4e509a9"
)
EXPECTED_PARSER_PACK_COMPILER_DIGEST = (
    "493efbe57646f3e566e02e5e112d4243728a389f0b1ad1eb6bb1bd5e0cdfcdb5"
)
PROVENANCE_FIELDS = (
    "artifact_id",
    "origin_id",
    "origin_sha256",
    "target_path",
    "target_sha256",
    "role",
    "status",
    "limit",
)


class GateFailure(RuntimeError):
    pass


def expect_rejected(paths: list[Path], needle: str) -> None:
    try:
        schema.admit(paths)
    except schema.SchemaError as error:
        if needle not in str(error):
            raise GateFailure(f"wrong rejection: expected {needle!r}, got {error}")
        return
    raise GateFailure(f"presentation unexpectedly admitted; expected {needle!r}")


def file_digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def check_provenance() -> None:
    with PROVENANCE.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if tuple(reader.fieldnames or ()) != PROVENANCE_FIELDS:
            raise GateFailure("migration provenance schema changed")
        rows = list(reader)
    if len(rows) != 19:
        raise GateFailure(f"migration provenance inventory changed: {len(rows)}")
    ids: set[str] = set()
    targets: set[str] = set()
    for row in rows:
        artifact = row["artifact_id"]
        if not artifact or artifact in ids:
            raise GateFailure(f"empty or duplicate provenance id {artifact!r}")
        relative = Path(row["target_path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise GateFailure(f"non-local provenance target {relative}")
        if row["target_path"] in targets:
            raise GateFailure(f"duplicate provenance target {relative}")
        for field in ("origin_sha256", "target_sha256"):
            if not re.fullmatch(r"[0-9a-f]{64}", row[field]):
                raise GateFailure(f"invalid {field} for {artifact}")
        target = ROOT / relative
        if not target.is_file() or file_digest(target) != row["target_sha256"]:
            raise GateFailure(f"target digest mismatch for {artifact}")
        if not row["role"] or not row["status"] or not row["limit"]:
            raise GateFailure(f"incomplete provenance claim for {artifact}")
        ids.add(artifact)
        targets.add(row["target_path"])


def main() -> int:
    passed = 0

    admitted = schema.admit([CORE, TOY])
    digest = schema.package_digest(admitted)
    if digest != EXPECTED_PACKAGE_DIGEST:
        raise GateFailure(f"canonical package digest changed: {digest}")
    passed += 1

    if schema.package_digest(schema.admit([TOY, CORE])) != digest:
        raise GateFailure("component order changed the package digest")
    passed += 1

    with tempfile.TemporaryDirectory(prefix="gslt2parse-schema-v1-") as raw_temp:
        temp = Path(raw_temp)
        roundtrip_paths: list[Path] = []
        for presentation in admitted:
            path = temp / f"{presentation.name}.metta"
            path.write_text(schema.canonical_text(presentation), encoding="utf-8")
            roundtrip_paths.append(path)
        roundtrip = schema.admit(roundtrip_paths)
        if schema.package_digest(roundtrip) != digest:
            raise GateFailure("parse-canonicalize-parse changed the package digest")
    passed += 1

    with tempfile.TemporaryDirectory(prefix="gslt2parse-schema-v1-") as raw_temp:
        temp = Path(raw_temp)
        noisy = temp / "noisy-core.metta"
        noisy.write_text(
            "; comments and layout do not enter the digest\n\n"
            + CORE.read_text(encoding="utf-8").replace("  (signature", "\n (signature"),
            encoding="utf-8",
        )
        if schema.package_digest(schema.admit([noisy, TOY])) != digest:
            raise GateFailure("comments or whitespace changed the package digest")
    passed += 1

    expect_rejected([CORE, WRONG_ARITY], "undeclared operator seq/1")
    passed += 1

    without_definition = schema.admit([CORE, WITHOUT_DEFINITION])
    if schema.package_digest(without_definition) == digest:
        raise GateFailure("rule deletion did not change the package digest")
    passed += 1

    with tempfile.TemporaryDirectory(prefix="gslt2parse-nullary-v1-") as raw_temp:
        nullary = Path(raw_temp) / "nullary.metta"
        nullary.write_text(
            "(gslt-presentation-v1 NullaryV1 "
            "(signature (operator z 0)) (equations) "
            "(rewrites (rule z-rule (head (z)) (body))))",
            encoding="utf-8",
        )
        canonical_nullary = schema.canonical_text(schema.admit([nullary])[0])
        if "(operator z 0)" not in canonical_nullary or "(head (z))" not in canonical_nullary:
            raise GateFailure("nullary operator was not preserved canonically")
    passed += 1

    mutations = {
        "unknown-field": (
            "(gslt-presentation-v1 Bad "
            "(signature) (equations) (rewrites) (parser-options))",
            "unknown presentation field",
        ),
        "authored-equation": (
            "(gslt-presentation-v1 Bad "
            "(signature) (equations (equation e a b)) (rewrites))",
            "identity equations only",
        ),
        "duplicate-operator": (
            "(gslt-presentation-v1 Bad "
            "(signature (operator p 1) (operator p 1)) "
            "(equations) (rewrites))",
            "duplicate operator p/1",
        ),
        "negative-operator-arity": (
            "(gslt-presentation-v1 Bad "
            "(signature (operator p -1)) (equations) (rewrites))",
            "NONNEGATIVE-ARITY",
        ),
        "duplicate-rule": (
            "(gslt-presentation-v1 Bad "
            "(signature (operator p 1)) (equations) "
            "(rewrites "
            "(rule r (head (p a)) (body)) "
            "(rule r (head (p b)) (body))))",
            "duplicate rule name r",
        ),
        "anonymous-variable": (
            "(gslt-presentation-v1 Bad "
            "(signature (operator p 1)) (equations) "
            "(rewrites (rule r (head (p ?_)) (body))))",
            "anonymous variable",
        ),
        "unicode-surrogate": (
            "(gslt-presentation-v1 Bad "
            "(signature (operator p 1)) (equations) "
            "(rewrites (rule r (head (p \"\\ud800\")) (body))))",
            "non-scalar Unicode surrogate",
        ),
        "quoted-symbol-fragment": (
            "(gslt-presentation-v1 Bad "
            "(signature (operator p 1)) (equations) "
            '(rewrites (rule r (head (p abc"def")) (body))))',
            "undeclared operator p/2",
        ),
    }
    with tempfile.TemporaryDirectory(prefix="gslt2parse-schema-v1-") as raw_temp:
        temp = Path(raw_temp)
        for name, (text, needle) in mutations.items():
            path = temp / f"{name}.metta"
            path.write_text(text, encoding="utf-8")
            expect_rejected([path], needle)
            passed += 1

        unicode_path = temp / "unicode.metta"
        unicode_path.write_text(
            "(gslt-presentation-v1 UnicodeV1 "
            "(signature (operator probe 1)) "
            "(equations) "
            "(rewrites (rule unicode-scalar-string "
            "(head (probe \"λ→∀\")) (body))))",
            encoding="utf-8",
        )
        unicode_presentation = schema.admit([unicode_path])[0]
        canonical = schema.canonical_text(unicode_presentation)
        if "λ→∀" not in canonical:
            raise GateFailure("Unicode string was escaped or lost")
        unicode_roundtrip = temp / "unicode-roundtrip.metta"
        unicode_roundtrip.write_text(canonical, encoding="utf-8")
        if schema.canonical_text(schema.admit([unicode_roundtrip])[0]) != canonical:
            raise GateFailure("Unicode canonical round-trip changed the presentation")
        passed += 1

        escaped_unicode_path = temp / "unicode-escaped.metta"
        escaped_unicode_path.write_text(
            "(gslt-presentation-v1 UnicodeEscapeV1 "
            "(signature (operator probe 1)) "
            "(equations) "
            "(rewrites (rule unicode-scalar-string "
            '(head (probe "\\u03bb\\ud83d\\ude00")) (body))))',
            encoding="utf-8",
        )
        raw_unicode_path = temp / "unicode-raw.metta"
        raw_unicode_path.write_text(
            "(gslt-presentation-v1 UnicodeEscapeV1 "
            "(signature (operator probe 1)) "
            "(equations) "
            "(rewrites (rule unicode-scalar-string "
            '(head (probe "λ😀")) (body))))',
            encoding="utf-8",
        )
        escaped_digest = schema.package_digest(schema.admit([escaped_unicode_path]))
        raw_digest = schema.package_digest(schema.admit([raw_unicode_path]))
        if escaped_digest != raw_digest:
            raise GateFailure("escaped Unicode scalars changed canonical bytes")
        passed += 1

    migrated = migration.migrate(MIGRATION_FIXTURE, name="MigratedTypedToyV1")
    if {(item.name, item.arity) for item in migrated.operators} != {
        ("pair", 2),
        ("rel", 1),
    }:
        raise GateFailure("v0 migration did not derive exact positive arities")
    if tuple(rule.name for rule in migrated.rules) != ("rel-z",):
        raise GateFailure("v0 migration did not preserve the Horn rule")
    with tempfile.TemporaryDirectory(prefix="gslt2parse-migration-v0-") as raw_temp:
        migrated_path = Path(raw_temp) / "migrated.metta"
        migrated_path.write_text(migration.pretty_text(migrated), encoding="utf-8")
        admitted_migration = schema.admit([migrated_path])
        if schema.canonical_form(admitted_migration[0]) != schema.canonical_form(
            migrated
        ):
            raise GateFailure("v0 migration failed the admitted v1 round-trip")
    passed += 1

    expected_migration_source_digest = (
        "4f7532349a4f379d44cacf936b281fdfa29677a240a36dc9213603369a3e6200"
    )
    migration.verify_source_digest(MIGRATION_FIXTURE, expected_migration_source_digest)
    passed += 1

    try:
        migration.verify_source_digest(MIGRATION_FIXTURE, "0" * 64)
    except schema.SchemaError as error:
        if "source digest mismatch" not in str(error):
            raise GateFailure(f"wrong migration-digest rejection: {error}") from error
    else:
        raise GateFailure("migration accepted a wrong source digest")
    passed += 1

    with tempfile.TemporaryDirectory(prefix="gslt2parse-migration-v0-") as raw_temp:
        temp = Path(raw_temp)
        source_text = MIGRATION_FIXTURE.read_text(encoding="utf-8")
        authored_equation = temp / "authored-equation.metta"
        authored_equation.write_text(
            source_text.replace(
                "  (equations)", "  (equations (equation collapse z z))"
            ),
            encoding="utf-8",
        )
        try:
            migration.migrate(authored_equation, name="MustReject")
        except schema.SchemaError as error:
            if "authored equations cannot migrate" not in str(error):
                raise GateFailure(f"wrong authored-equation rejection: {error}") from error
        else:
            raise GateFailure("migration accepted authored v0 equations")
        passed += 1

        malformed_constructor = temp / "malformed-constructor.metta"
        malformed_constructor.write_text(
            source_text.replace("(constructor pair A B B)", "(constructor pair)"),
            encoding="utf-8",
        )
        try:
            migration.migrate(malformed_constructor, name="MustReject")
        except schema.SchemaError as error:
            if "malformed v0 constructor" not in str(error):
                raise GateFailure(f"wrong constructor rejection: {error}") from error
        else:
            raise GateFailure("migration accepted a malformed v0 constructor")
        passed += 1

    metamath = schema.admit([CORE, METAMATH])
    if schema.package_digest(metamath) != EXPECTED_METAMATH_DIGEST:
        raise GateFailure("Metamath v1 package digest changed")
    passed += 1

    tptp = schema.admit(
        [CORE, LOOKAHEAD, CHAR_CORE, GROUND_RELATIONS, TPTP_UNICODE_SCALARS, TPTP]
    )
    if schema.package_digest(tptp) != EXPECTED_TPTP_DIGEST:
        raise GateFailure("TPTP v1 package digest changed")
    passed += 1

    megalodon = schema.admit([CORE, CHAR_CORE, LOOKAHEAD, MEGALODON])
    if schema.package_digest(megalodon) != EXPECTED_MEGALODON_DIGEST:
        raise GateFailure("Megalodon dynamic v1 package digest changed")
    passed += 1

    cetta_prime_reader = schema.admit(
        [
            CORE,
            LOOKAHEAD,
            GROUND_RELATIONS,
            CETTA_PRIME_SCALARS,
            CETTA_PRIME_READER,
        ]
    )
    if (
        schema.package_digest(cetta_prime_reader)
        != EXPECTED_CETTA_PRIME_READER_DIGEST
    ):
        raise GateFailure("CeTTa Prime reader v1 package digest changed")
    passed += 1

    cetta_rho_reader = schema.admit(
        [
            CORE,
            LOOKAHEAD,
            GROUND_RELATIONS,
            CHAR_CORE,
            CETTA_RHO_SCALARS,
            CETTA_RHO_LEXICAL,
            CETTA_RHO_READER,
        ]
    )
    if schema.package_digest(cetta_rho_reader) != EXPECTED_CETTA_RHO_READER_DIGEST:
        raise GateFailure("CeTTa rho reader v1 package digest changed")
    passed += 1

    cetta_cost_rho_reader = schema.admit(
        [
            CORE,
            LOOKAHEAD,
            GROUND_RELATIONS,
            CHAR_CORE,
            CETTA_RHO_SCALARS,
            CETTA_RHO_LEXICAL,
            CETTA_COST_RHO_READER,
        ]
    )
    if (
        schema.package_digest(cetta_cost_rho_reader)
        != EXPECTED_CETTA_COST_RHO_READER_DIGEST
    ):
        raise GateFailure("CeTTa cost-rho reader v1 package digest changed")
    passed += 1

    cetta_rho_abstract = schema.admit(
        [CETTA_RHO_ABSTRACT, CETTA_RHO_RUNTIME_ABI]
    )
    if (
        schema.package_digest(cetta_rho_abstract)
        != EXPECTED_CETTA_RHO_ABSTRACT_DIGEST
    ):
        raise GateFailure("CeTTa rho abstract syntax/runtime ABI changed")
    passed += 1

    cetta_rho_mrho_reader = schema.admit(
        [
            CORE,
            LOOKAHEAD,
            GROUND_RELATIONS,
            HE_READER_SCALARS,
            CETTA_RHO_ABSTRACT,
            CETTA_RHO_MRHO_READER,
        ]
    )
    if (
        schema.package_digest(cetta_rho_mrho_reader)
        != EXPECTED_CETTA_RHO_MRHO_READER_DIGEST
    ):
        raise GateFailure("CeTTa rho .mrho reader v1 package digest changed")
    passed += 1

    compiler = schema.admit([CORE, LOOKAHEAD, COMPILER])
    if schema.package_digest(compiler) != EXPECTED_COMPILER_DIGEST:
        raise GateFailure("syntax compiler v1 package digest changed")
    passed += 1

    parser_pack_compiler = schema.admit(
        [
            CORE,
            LOOKAHEAD,
            PARSER_PACK_CORE,
            REFLECTION,
            COMPILER,
            RELATIONAL_CFG_LOWERING,
            RELATIONAL_CFG_LINK,
            PARSER_PACK_COMPILER,
        ]
    )
    if (
        schema.package_digest(parser_pack_compiler)
        != EXPECTED_PARSER_PACK_COMPILER_DIGEST
    ):
        raise GateFailure("ParserPack compiler package digest changed")
    passed += 1

    expect_rejected(
        [CORE, LOOKAHEAD, CETTA_PRIME_SCALARS, CETTA_PRIME_READER],
        "undeclared operator different/2",
    )
    passed += 1

    scalar_text = CETTA_PRIME_SCALARS.read_text(encoding="utf-8")
    reader_text = CETTA_PRIME_READER.read_text(encoding="utf-8")
    if "-byte" in scalar_text or "-byte" in reader_text:
        raise GateFailure("byte-oriented class name survived scalarization")
    if "(different ?codepoint (cp 34))" not in scalar_text:
        raise GateFailure("scalarized string delimiter guard is absent")
    passed += 1

    check_provenance()
    passed += 1

    for path in PRESENTATIONS.rglob("*.metta"):
        text = path.read_text(encoding="utf-8")
        if re.search(r"\((?:types|terms|constructor)\b", text):
            raise GateFailure(f"decorative v0 typing survived in {path.relative_to(ROOT)}")
    passed += 1

    generic_sources = (
        ROOT / "tools" / "gslt2parse_schema_v1.py",
        ROOT / "tools" / "migrate_gslt_presentation_v0.py",
    )
    guest_name = re.compile(r"metamath|megalodon|tptp", re.IGNORECASE)
    for path in generic_sources:
        if guest_name.search(path.read_text(encoding="utf-8")):
            raise GateFailure(f"guest-language name in generic tool {path.name}")
    passed += 1

    total = 37
    if passed != total:
        raise GateFailure(f"gate accounting mismatch: {passed}/{total}")
    print(f"(FiniteHornGSLTV1CanarySummary {total} {passed} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
