"""Inventory authored GSLT inputs and the obligations behind production gates.

Discovery is structural, not an admission or correctness oracle. A recipe is a
way to run a check; its presence does not certify a language or close a valid
construct. Unknown families and unparsed sources remain visible in the output.
"""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "tests/langdef/gslt_coverage_v1.json"
sys.path.insert(0, str(ROOT / "tests/support"))
from check_transform_swi_differential import read_forms  # noqa: E402


def digest(value: bytes) -> str:
    return sha256(value).hexdigest()


def make_fragments(root: Path = ROOT) -> list[Path]:
    return [root / "Makefile", *sorted((root / "mk").glob("*.mk")),
            *sorted((root / "native").rglob("*.mk")), *sorted((root / "src").rglob("*.mk"))]


def presentations(forms):
    """Retain occurrences, including distinct occurrences of the same name."""
    result, todo = [], list(reversed(forms))
    while todo:
        node = todo.pop()
        if not isinstance(node, list):
            continue
        if node and node[0] == "gslt-presentation-v1":
            sections = [field for field in node[2:]
                        if isinstance(field, list) and field and isinstance(field[0], str)]
            counts = {tag: sum(len(field) - 1 for field in sections if field[0] == tag)
                      for tag in ("signature", "equations", "rewrites")}
            result.append(dict(occurrence=len(result),
                               name=node[1] if len(node) > 1 and isinstance(node[1], str) else None,
                               fields=sorted(field[0] for field in sections),
                               operators=counts["signature"], equations=counts["equations"],
                               rewrites=counts["rewrites"]))
        todo.extend(reversed(node))
    return result


def inventory(root: Path = ROOT, registry: Path = REGISTRY) -> dict:
    specification = json.loads(registry.read_text())
    if specification.get("schema") != "cetta-gslt-coverage-v1":
        raise ValueError("unknown GSLT coverage registry schema")
    ids = [row["id"] for row in specification["constructs"]]
    if len(ids) != len(set(ids)) or not ids:
        raise ValueError("construct obligations must have distinct nonempty identities")
    if any(row.get("status") != "open" for row in specification["constructs"]):
        raise ValueError("inventory recipes cannot establish construct qualification")
    files, families = {}, {}
    for source_root in (*specification["source_roots"], *specification.get("presentation_scans", [])):
        directory = root / source_root
        if not directory.is_dir():
            raise ValueError(f"missing authored source root: {source_root}")
        for path in sorted(directory.rglob("*")):
            if not path.is_file():
                continue
            if source_root in specification.get("presentation_scans", []):
                if path.suffix not in (".metta", ".module"):
                    continue
                # The structural reader distinguishes forms from strings/comments.
                if b"gslt-presentation-v1" not in path.read_bytes():
                    continue
            relative = path.relative_to(root).as_posix()
            if relative in files:
                raise ValueError(f"overlapping authored source roots: {relative}")
            family_part = path.relative_to(directory).parts[0]
            family = source_root + "/" + family_part
            row = dict(path=relative, sha256=digest(path.read_bytes()), family=family)
            if path.suffix in (".metta", ".module"):
                try:
                    text = path.read_text()
                    if path.suffix == ".module" and not text.lstrip().startswith("("):
                        row["representation"] = "module-text"
                    else:
                        row["representation"] = "sexpr"
                        forms = read_forms(text)
                        row["presentations"] = presentations(forms)
                        row["top_heads"] = sorted({form[0] for form in forms
                            if isinstance(form, list) and form and isinstance(form[0], str)})
                except (UnicodeError, ValueError) as error:
                    row["indexing_error"] = str(error)
            files[relative] = row
            families.setdefault(family, []).append(relative)
    recipes = specification["production_recipes"]
    unknown = sorted(set(recipes) - set(families))
    if unknown:
        raise ValueError(f"recipes name absent source families: {unknown}")
    family_rows = [dict(family=name, files=paths,
                        production_gates=recipes.get(name, []), qualification="pending")
                   for name, paths in sorted(families.items())]
    producer_rows = []
    for rule in specification["producer_inputs"]:
        directory = root / rule["directory"]
        if not directory.is_dir():
            raise ValueError(f"missing producer input root: {rule['directory']}")
        for path in sorted(directory.rglob("*")):
            if path.is_file() and ("*" in rule["suffixes"] or path.suffix in rule["suffixes"]):
                producer_rows.append((path.relative_to(root).as_posix(), digest(path.read_bytes())))
    identity = digest(json.dumps([specification,
        [(name, row["sha256"]) for name, row in sorted(files.items())],
        producer_rows], sort_keys=True, ensure_ascii=False).encode())
    return dict(schema="cetta-gslt-source-inventory-v1", identity=identity,
                constructs=specification["constructs"], families=family_rows,
                files=[files[name] for name in sorted(files)], producers=producer_rows)


def summary(value: dict) -> dict:
    occurrences = [presentation for row in value["files"]
                   for presentation in row.get("presentations", [])]
    return dict(files=len(value["files"]), families=len(value["families"]),
                presentation_occurrences=len(occurrences),
                authored_equations=sum(p["equations"] for p in occurrences),
                families_with_recipes=sum(bool(f["production_gates"]) for f in value["families"]),
                indexing_errors=[row["path"] for row in value["files"] if "indexing_error" in row],
                open_constructs=len(value["constructs"]), identity=value["identity"])


def check_recipes(value: dict, makefiles: list[Path]) -> None:
    declared = set()
    for path in makefiles:
        for line in path.read_text().splitlines():
            if line.startswith("\t") or line.lstrip().startswith("#") or ":" not in line:
                continue
            left, right = line.split(":", 1)
            if right.startswith("="):
                continue
            declared.update(left.split())
    missing = sorted({gate for family in value["families"]
                      for gate in family["production_gates"] if gate not in declared})
    if missing:
        raise ValueError(f"coverage recipes name undeclared production gates: {missing}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=REGISTRY)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    value = inventory(registry=args.registry)
    check_recipes(value, make_fragments())
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps(summary(value), ensure_ascii=False))


if __name__ == "__main__":
    main()
