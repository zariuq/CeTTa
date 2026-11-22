"""Keep new families, equations, duplicate occurrences and producer drift visible."""

import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from gslt_coverage_registry_v1 import inventory, summary, check_recipes, make_fragments


def main():
    actual = inventory()
    assert "native/gslt_petta_direct_v1.c" in dict(actual["producers"])
    check_recipes(actual, make_fragments())
    with tempfile.TemporaryDirectory(prefix="gslt-registry-", dir=ROOT / "runtime") as raw:
        root = Path(raw)
        for name in ("langdef/first", "tools", "tests", "fixtures"):
            (root / name).mkdir(parents=True)
        source = root / "langdef/first/a.metta"
        presentation = ("(gslt-presentation-v1 Same (signature (operator f 1)) "
                        "(equations (equation (f ?x) ?x)) (rewrites))\n")
        source.write_text(presentation + presentation +
                          '(held "(gslt-presentation-v1 Fake) \\q")\n')
        producer = root / "tools/compile.c"
        producer.write_text("int version = 0;\n")
        fixture = root / "fixtures/query.calls"
        fixture.write_text("Call:1 before\n")
        registry = root / "registry.json"
        spec = dict(schema="cetta-gslt-coverage-v1", source_roots=["langdef"],
            presentation_scans=["tests"], producer_inputs=[dict(directory="tools", suffixes=[".c"]),
                                                         dict(directory="fixtures", suffixes=["*"])],
            constructs=[dict(id="equation.authored", status="open")], production_recipes={})
        registry.write_text(json.dumps(spec))
        before = inventory(root, registry)
        assert summary(before)["presentation_occurrences"] == 2
        assert summary(before)["authored_equations"] == 2
        assert [p["occurrence"] for p in before["files"][0]["presentations"]] == [0, 1]
        assert all(f["qualification"] == "pending" for f in before["families"])

        (root / "langdef/new-family").mkdir()
        (root / "langdef/new-family/syntax.metta").write_text("(new-wire-format lambda)\n")
        grown = inventory(root, registry)
        assert grown["identity"] != before["identity"]
        assert any(f["family"] == "langdef/new-family" and not f["production_gates"]
                   for f in grown["families"])
        producer.write_text("int version = 1;\n")
        changed = inventory(root, registry)
        assert changed["identity"] != grown["identity"]
        fixture.write_text("Call:1 after\n")
        changed_fixture = inventory(root, registry)
        assert changed_fixture["identity"] != changed["identity"]
        (root / "tests/control.metta").write_text(presentation)
        control = inventory(root, registry)
        assert summary(control)["presentation_occurrences"] == 3
        assert control["identity"] != changed_fixture["identity"]

        spec["constructs"][0]["status"] = "refused"
        registry.write_text(json.dumps(spec))
        try:
            inventory(root, registry)
        except ValueError as error:
            assert "cannot establish construct qualification" in str(error)
        else:
            raise AssertionError("a refusal was allowed to close a valid construct")

        bad_recipe = dict(families=[dict(production_gates=["missing-gate"])])
        makefile = root / "Makefile"
        makefile.write_text(" # missing-gate: an old comment\n.PHONY: missing-gate\n")
        try:
            check_recipes(bad_recipe, [makefile])
        except ValueError as error:
            assert "missing-gate" in str(error)
        else:
            raise AssertionError("a stale recipe was accepted as a production gate")
    counts = summary(actual)
    print(f"PASS: source inventory registers {counts['files']} files and "
          f"{counts['presentation_occurrences']} presentation occurrences; "
          "new families, authored equations and producer changes invalidate its identity")
    print(f"OPEN: {counts['open_constructs']} full qualification obligations; "
          "inventory and recipe membership do not close them")


if __name__ == "__main__":
    main()
