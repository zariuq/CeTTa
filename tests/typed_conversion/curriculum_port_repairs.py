"""Source-specific curriculum ports and explicit unfinished proof obligations."""

from __future__ import annotations

import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORTS = Path(__file__).with_name("faithful_ports")

# Each theorem port retains the full quantifiers and hypotheses of its source.
REPAIRED = {
    "item-35d546b822e5": ("negbot", "False"),
    "item-eb5ba3093a19": ("negbot3", "False"),
    "item-eb1d89b315c1": ("eqset", "eq-refl-set"),
    "item-453067faeb83": ("update", "update-eq"),
    "item-3634c0843b2b": ("dne", "dne-valid"),
    "item-2043438ed05d": ("setv", "True"),
    "item-c63bcbf72e97": ("setv", "True"),
    "item-86a2a7190e1b": ("value", "True"),
    "item-1a3bb644a5e1": ("hotg", "hotg-mem"),
    "item-f99170686afc": ("hotg", "hotg-sym-from-axioms"),
    "item-31d22e614e5f": ("shift", "True"),
    "item-8636f150a507": ("shift", "True"),
    "item-63bbe1181304": ("signlit", "signLit-setv-true"),
    "item-19980eb3f0aa": ("signlit", "signLit-setv-false"),
    "item-59c3340fe2be": ("pair", "True"),
    "item-b94f94e9068c": ("pair", "True"),
    "item-dd020b95956b": ("badwitness", "False"),
    "item-8ac928d1e29c": ("decide", "False"),
    "item-0b0cbcc77026": ("badrfl", "False"),
}

UNFINISHED = {
    "item-bd3d721868b5": "no_instance needs Coq typeclass resolution; native source elaboration is not implemented",
    "item-80604b3bef30": "bad_forward_ref needs closed source-signature name resolution; the native unknown-type query remains unresolved",
    "item-21632733014a": "hoare_seq requires native ceval indexed induction and its sequencing inversion proof",
    "item-ec20f45de6b9": "map_signLit_setv requires the full list-induction proof for functional valuation update",
    "item-f032936a26af": "remove_atoms requires the nd induction/case-split proof under NoDup",
    "item-7800cb25bc55": "allAssign_complete requires the full finite-valuation enumeration completeness proof",
    "item-62a293d83d95": "decide_valid_correct requires eval extensionality and allAssign completeness",
    "item-fa8d4700eeb8": "Quot native quotient type is not implemented",
    "item-37e8e4d422f7": "Quot.lift native quotient eliminator is not implemented",
}

EXPECTED = {
    "negbot": ["()", "()", "nat", "bool", "(refl@bool subst@bool)", "form", "impb", "andb", "orb", "Neg", "eval", "False"],
    "eqset": ["()", "eq-refl-set"],
    "update": ["()", "()", "nat", "bool", "andb", "is-zero", "predn", "if-zero", "subk", "nat-eqb", "ty", "option", "if-option", "update", "(refl@nat subst@nat)", "(refl@bool subst@bool)", "(refl@option subst@option)", "sub-diag", "eqb-diag", "update-eq"],
    "negbot3": ["()", "()", "nat", "bool", "andb", "is-zero", "predn", "if-zero", "subk", "nat-eqb", "if-bool", "setv", "form", "nlist", "vlist", "impb", "eval", "nappend", "vappend", "vars", "map-setv", "allAssign", "forall-eval", "decide_valid", "False"],
    "badrfl": ["()", "nat", "False"],
    "badwitness": ["()", "nat", "add", "False"],
    "decide": ["()", "()", "nat", "bool", "andb", "is-zero", "predn", "if-zero", "subk",
               "nat-eqb", "if-bool", "setv", "form", "nlist", "vlist", "impb", "eval", "nappend",
               "vappend", "vars", "map-setv", "allAssign", "forall-eval", "decide_valid"] + ["True"] * 4 + ["False"],
    "pair": ["()", "mk-pair", "pair-proj", "True", "True"],
    "signlit": ["()", "()", "nat", "bool", "andb", "is-zero", "predn", "if-zero",
                "subk", "nat-eqb", "if-bool", "setv", "(refl@nat subst@nat)",
                "(refl@bool subst@bool)", "form", "(refl@form subst@form)", "Neg",
                "if-form", "signLit", "sub-diag", "eqb-diag", "signLit-setv-true", "signLit-setv-false"],
    "shift": ["()", "nat", "bool", "Tm", "add", "is-zero", "predn", "if-zero", "subk", "lt", "if-Tm", "shiftAbove", "shift"] + ["True"] * 7,
    "dne": ["()", "()", "nat", "bool", "(refl@bool subst@bool)", "form",
            "impb", "andb", "orb", "Neg", "eval", "valid", "dne-bool", "dne-valid"],
    "hotg": ["()", "hotg-mem", "hotg-sym-from-axioms"],
    "setv": ["()", "()", "nat", "bool", "andb", "is-zero", "predn", "if-zero",
             "subk", "nat-eqb", "if-bool", "setv"] + ["True"] * 5,
    "value": ["()", "()", "tm", "truth", "falsum", "or", "bvalue", "nvalue", "value"] + ["True"] * 5,
}

PORT_NAMES = {
    "neg_bot_true": "negbot", "eq_refl_set": "eqset",
    "bad_witness": "badwitness", "bad_rfl": "badrfl", "neg_var_valid": "decide",
    "decide_valid": "decide", "allAssign": "decide", "dec_taut": "decide",
    "dec_nontaut": "decide", "dec_peirce": "decide",
    "pair_proj": "pair", "signLit_setv_true": "signlit", "signLit_setv_false": "signlit",
    "shift": "shift", "shiftAbove": "shift",
    "shift_keeps_bound_var_under_lam": "shift", "shift_moves_free_var_under_lam": "shift",
    "dne_valid": "dne", "setv": "setv", "value": "value",
    "hotg_mem": "hotg", "hotg_sym_from_axioms": "hotg",
}
UNFINISHED_NAMES = {
    "no_instance", "bad_forward_ref", "hoare_seq", "map_signLit_setv", "remove_atoms", "allAssign_complete",
    "decide_valid_correct", "QP", "par",
}


def program(name: str) -> str | None:
    """None means this module does not own the source declaration; empty means unported."""
    if name in UNFINISHED_NAMES:
        return ""
    key = PORT_NAMES.get(name)
    if key is None:
        return None
    source = (PORTS / (key + ".metta")).read_text()
    if key == "decide" and name != "neg_var_valid":
        source = source[:source.index("!(type:check")]
    if key == "hotg":
        theorem = "hotg-mem" if name == "hotg_mem" else "hotg-sym-from-axioms"
        source = "\n".join(line for line in source.splitlines()
                           if not line.startswith("!(set:theorem") or
                           line.startswith("!(set:theorem " + theorem + " ")) + "\n"
    return source


def execute(binary: Path, source: Path, evidence: Path, timeout: float | None = None) -> tuple[bool, dict]:
    """Validate all outputs, process status and stderr, not merely the last print."""
    result = subprocess.run([str(binary), "--lang", "prime", str(source)],
                            cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / (source.stem + ".stdout")).write_text(result.stdout)
    (evidence / (source.stem + ".stderr")).write_text(result.stderr)
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    wanted = ["[" + item + "]" for item in EXPECTED[source.stem]]
    ok = result.returncode == 0 and not result.stderr.strip() and lines == wanted
    receipt = {"source": str(source.relative_to(ROOT)),
               "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "exit": result.returncode, "outputs": lines, "accepted": ok}
    return ok, receipt


def apply(rows: list[dict], binary: Path, scratch: Path) -> dict:
    """Repair the reviewed rows; absent proofs remain unclaimed, never runtime defects."""
    evidence = scratch / "faithful-port-evidence"
    results = {}
    for name in sorted({key for key, _ in REPAIRED.values()}):
        ok, receipt = execute(binary, PORTS / (name + ".metta"), evidence)
        results[name] = receipt
        if not ok:
            raise RuntimeError(f"faithful curriculum port {name} failed; see its complete run evidence")
    found = set()
    for row in rows:
        rid = row["id"]
        if rid in REPAIRED:
            name, verdict = REPAIRED[rid]
            row.update(capability="implemented", dependency="none", expected="[" + verdict + "]",
                       prime_statement=" ".join((PORTS / (name + ".metta")).read_text().split()),
                       artifact=str((PORTS / (name + ".metta")).relative_to(ROOT)),
                       query_kind="source-port", rule_package="native set: judgments",
                       justification="source-specific full program; all admissions and controls checked")
            if name == "eqset":
                row["assumptions"] = "source set carrier generalized to A:u0; source equality expanded as forall Q:A→A→prop, Q x y→Q y x; no guest set axioms used"
            elif name == "hotg":
                row["assumptions"] = "source set constants generalized; axMem and axSym discharged as the corresponding implication premises"
            elif name == "dne":
                row["assumptions"] = "source Boolean semantics; bool induction and the native HOL equality presentation"
            found.add(rid)
        elif rid in UNFINISHED:
            row.update(capability="missing", dependency=UNFINISHED[rid], expected="not claimed",
                       prime_statement="not claimed", query_kind="unported-source-proof",
                       rule_package="none", justification="the prior program did not state this source item; its proof remains unimplemented")
            found.add(rid)
    missing = (REPAIRED.keys() | UNFINISHED.keys()) - found
    if missing:
        raise RuntimeError(f"reviewed curriculum rows missing or changed: {sorted(missing)}")
    receipt = {"binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
               "ports": results, "repaired_rows": sorted(REPAIRED),
               "unfinished_rows": UNFINISHED}
    (evidence / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt
