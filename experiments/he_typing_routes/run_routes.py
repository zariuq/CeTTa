#!/usr/bin/env python3
"""Run HE's typing judgments through every route and compare them with HE's own answers.

Routes:
  he-upstream   HE's reference REPL (metta-repl), the ground truth;
  cetta-he      CeTTa's HE dialect, `--lang he --profile he`;
  cetta-ext     CeTTa's HE dialect, `--lang he --profile extended`;
  prime-hol     Prime's HOL contract route, `--lang prime`: get-type of HE-style declarations
                answered through the hosted HOL Light checker;
  fo-schemes    the first-order prenex-scheme theory (types as first-order terms, flat
                arrows, syntactic unification), computed by `fo_schemes.py`;
  lf            the same theory encoded in LF and checked by the MeTTa LF kernel guest;
  hotg          the theory in the HOTG set profile with its closure law (`hotg_closure.py`):
                positives proved from the rules, refusals from the completion of each fibre;
  hol-flat      the first-order derivations replayed in the hosted HOL Light checker under the
                flat reading (`hol_flat.py`): arrows are opaque n-ary type operators applied by
                `he-apply:n`, every declaration its own constant; a refusal is earned when the
                checker refuses every failed way of typing the judgment.

Usage: run_routes.py --work DIR [--upstream-root DIR] [--routes a,b,...] [--json OUT]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CETTA = ROOT / "cetta"
sys.path.insert(0, str(HERE))

import corpus  # noqa: E402
from sexpr import read_forms, show  # noqa: E402

TIMEOUT = 300
MEMORY_KB = 25165824


def run(command: list[str], cwd: Path) -> str:
    limited = ["bash", "-c", f"ulimit -v {MEMORY_KB}; exec \"$@\"", "run", *command]
    result = subprocess.run(limited, cwd=cwd, text=True, capture_output=True, timeout=TIMEOUT)
    return result.stdout


JUDGMENT_LINE = re.compile(r"^\[\(Judgment ")


def parse_answers(output: str) -> dict[str, str]:
    """Map judgment ids to the printed type list of their query."""
    answers: dict[str, str] = {}
    for line in output.splitlines():
        if not JUDGMENT_LINE.match(line):
            continue
        for form in read_forms(line[1:-1]):
            atom = form.atom
            if isinstance(atom, tuple) and len(atom) == 3 and str(atom[0]) == "Judgment":
                answers[str(atom[1])] = show(atom[2])
    return answers


def he_upstream(upstream_root: Path, path: Path) -> dict[str, str]:
    repl = upstream_root / "target" / "release" / "metta-repl"
    return parse_answers(run([str(repl), str(path)], path.parent))


def cetta(arguments: list[str], path: Path) -> dict[str, str]:
    return parse_answers(run([str(CETTA), *arguments, str(path)], ROOT))


def fo_route(path: Path, binders: bool = False) -> dict[str, str]:
    """The first-order prenex-scheme theory, judged in programme order."""
    import fo_schemes
    atoms = []
    answers: dict[str, str] = {}
    for form in read_forms(path.read_text()):
        atom = form.atom
        if not form.bang:
            atoms.append(atom)
            continue
        if (isinstance(atom, tuple) and len(atom) == 4 and str(atom[0]) == "let"
                and isinstance(atom[3], tuple) and str(atom[3][0]) == "Judgment"):
            expression = atom[2][1][1]
            found, _ = fo_schemes.get_type(fo_schemes.Space(atoms, binders), expression)
            answers[str(atom[3][1])] = fo_schemes.answer_text(found)
    return answers


def fo_judgments(path: Path, binders: bool = False):
    """Each judgment of a programme with its context and first-order derivations."""
    import fo_schemes
    atoms = []
    for form in read_forms(path.read_text()):
        atom = form.atom
        if not form.bang:
            atoms.append(atom)
            continue
        if (isinstance(atom, tuple) and len(atom) == 4 and str(atom[0]) == "let"
                and isinstance(atom[3], tuple) and str(atom[3][0]) == "Judgment"):
            space = fo_schemes.Space(list(atoms), binders)
            expression = atom[2][1][1]
            typing = fo_schemes.types(space, expression)
            yield str(atom[3][1]), space, expression, typing


def guest_route(args, path: Path, guest: str, binders: bool = False) -> dict[str, str]:
    """Replay every derivation in a guest checker; the answer lists the types whose
    derivations the guest accepted.  `()` marks a judgment with no derivation and a
    failed application (refuted by exhaustive search); `%Undefined%` one with neither."""
    import encoders
    import fo_schemes
    lines = []
    plan = []
    for ident, space, expression, typing in fo_judgments(path, binders):
        if not typing.derivations:
            # LF's canonical forms are exhaustive: a judgment with no derivation is refuted.
            # Membership is extrinsic: nothing refutes it without a closure law.
            refuted = typing.errors and guest == "lf"
            plan.append((ident, "()" if refuted else "(%Undefined%)", []))
            continue
        checks = []
        for k, derivation in enumerate(typing.derivations):
            reason = encoders.unsupported(derivation)
            if reason or (guest == "hotg" and encoders.gradual(derivation)):
                checks.append((None, reason or "gradual"))
                continue
            tag = f"{ident}.{k}"
            if guest == "lf":
                try:
                    signature, term, typ = (encoders.lf_dependent_judgment if binders
                                            else encoders.lf_judgment)(space, derivation)
                except ValueError:
                    checks.append((None, "unsupported"))
                    continue
                lines.append(f"!(let $r (kernel-check-batch-judgments {signature} "
                             f"(Cons (Accept {term} {typ}) Nil)) (Guest {tag} $r))")
            else:
                try:
                    hotg, proof, statement = (encoders.hotg_dependent_judgment if binders
                                              else encoders.hotg_judgment)(space, derivation)
                except ValueError:
                    proof = None
                if proof is None:
                    checks.append((None, "unsupported"))
                    continue
                constants = " ".join(f"(: {n} {t})" for n, t in hotg.constants.items())
                lines.append(f"!(bind! &t{len(plan)}x{k} (let $s (new-space) (let $_ (add-atom $s "
                             f"(set:profile megalodon-hotg)) $s)))")
                space_name = f"&t{len(plan)}x{k}"
                lines.append(f"!(pf:theory {space_name} ({constants}))")
                for name, statement_text in hotg.axioms.items():
                    lines.append(f"!(let $u (pf:axiom {space_name} {name} {statement_text}) ())")
                lines.append(f"!(let $r (pf:proves {space_name} {proof} {statement}) (Guest {tag} $r))")
            checks.append((tag, fo_schemes.evaluate_ground(derivation.type)))
        plan.append((ident, None, checks))
    program = HERE / ".." / ".." / "runtime"
    work = args.work / f"{path.stem}.{guest}{'-dep' if binders else ''}.metta"
    if guest == "lf":
        kernel = args.kernel_root / "kernel_signature_lf_v0.metta"
        header = f"!(import! &self {kernel})\n"
    else:
        header = "(set:profile megalodon-hotg)\n!(import! &self pf)\n"
    work.write_text(header + "\n".join(lines) + "\n")
    output = run([str(CETTA), "--lang", "prime", str(work)], ROOT) if lines else ""
    verdicts = {}
    for line in output.splitlines():
        if line.startswith("[(Guest "):
            for form in read_forms(line[1:-1]):
                verdicts[str(form.atom[1])] = show(form.atom[2])
    answers = {}
    for ident, fixed, checks in plan:
        if fixed is not None:
            answers[ident] = fixed
            continue
        accepted = [t for tag, t in checks if tag is not None and verdicts.get(tag) == "True"]
        open_checks = [t for tag, t in checks if tag is None or verdicts.get(tag) != "True"]
        if open_checks and not accepted:
            answers[ident] = "(%Undefined%)"
        elif open_checks:
            answers[ident] = fo_schemes.answer_text(accepted) + "+"
        else:
            answers[ident] = fo_schemes.answer_text(accepted)
    return answers


def hol_flat_route(args, path: Path) -> dict[str, str]:
    """The flat reading in the hosted HOL Light checker."""
    import fo_schemes
    import hol_flat
    plan, lines = [], []
    for ident, space, expression, _ in fo_judgments(path):
        status, checks, check_lines = hol_flat.judgment_checks(space, expression)
        for k, text in check_lines:
            lines.append(f"!(let $r {text} (Guest {ident}.{k} $r))")
        plan.append((ident, status, checks))
    library = ROOT / "lib" / "prime" / "hol_light"
    work = args.work / f"{path.stem}.hol-flat.metta"
    work.write_text(hol_flat.HEADER.format(lib=library) + "\n".join(lines) + "\n")
    output = run([str(CETTA), "--lang", "prime", str(work)], ROOT) if lines else ""
    verdicts = {}
    for line in output.splitlines():
        if line.startswith("[(Guest "):
            for form in read_forms(line[1:-1]):
                verdicts[str(form.atom[1])] = show(form.atom[2])
    args.flat_verdicts = getattr(args, "flat_verdicts", {})
    answers = {}
    for ident, status, checks in plan:
        args.flat_verdicts[ident] = (status, [(kind, k, why, verdicts.get(f"{ident}.{k}"))
                                              for kind, k, why, _ in checks])
        if status == "typed":
            # Accepted at its type, and the mutation control refused at another.
            good = "(Checked Accepted WrongType)"
            accepted = [t for kind, k, _, t in checks
                        if kind == "type" and verdicts.get(f"{ident}.{k}") == good]
            open_checks = [t for kind, k, _, t in checks
                           if kind == "open" or verdicts.get(f"{ident}.{k}") != good]
            if open_checks and not accepted:
                answers[ident] = "(%Undefined%)"
            elif open_checks:
                answers[ident] = fo_schemes.answer_text(accepted) + "+"
            else:
                answers[ident] = fo_schemes.answer_text(accepted)
        elif status == "refuted":
            # Each failed way is earned when the checker reads its prefixes and refuses
            # the step that fails.
            earned = bool(checks) and all(
                kind == "refute" and earned_refusal(verdicts.get(f"{ident}.{k}"))
                for kind, k, _, _ in checks)
            answers[ident] = "()" if earned else "(%Undefined%)"
        else:
            answers[ident] = "(%Undefined%)"
    return answers


def hotg_route(args, path: Path) -> dict[str, str]:
    """The first-order scheme theory in the HOTG set profile, with its closure law: positives
    proved from the rules (each with a mutation control), refusals proved from the closure."""
    import fo_schemes
    import hotg_closure
    plan, lines = [], []
    for ident, space, expression, _ in fo_judgments(path):
        status, checks = hotg_closure.judgment_checks(space, expression)
        entries = []
        if status == "typed":
            for k, th, statement, proof, typ, mutant in checks:
                if th is None:
                    entries.append((None, typ, statement))
                    continue
                name = f"&h{len(plan)}x{k}"
                lines.extend(th.text(name))
                lines.append(f"!(let $r (pf:proves {name} {proof} {statement}) "
                             f"(let $m (pf:proves {name} {proof} {mutant}) "
                             f"(Guest {ident}.{k} (Checked $r $m))))")
                entries.append((f"{ident}.{k}", typ, None))
        elif status == "refuted":
            th, statement, proof = checks[0]
            name = f"&h{len(plan)}r"
            lines.extend(th.text(name))
            lines.append(f"!(let $r (pf:proves {name} {proof} {statement}) (Guest {ident}.r $r))")
        plan.append((ident, status, entries, checks))
    work = args.work / f"{path.stem}.hotg.metta"
    work.write_text("(set:profile megalodon-hotg)\n!(import! &self pf)\n" + "\n".join(lines) + "\n")
    output = run([str(CETTA), "--lang", "prime", str(work)], ROOT) if lines else ""
    verdicts = {}
    for line in output.splitlines():
        if line.startswith("[(Guest "):
            for form in read_forms(line[1:-1]):
                verdicts[str(form.atom[1])] = show(form.atom[2])
    args.hotg_verdicts = getattr(args, "hotg_verdicts", {})
    answers = {}
    for ident, status, entries, checks in plan:
        if status == "typed":
            accepted = [t for tag, t, _ in entries
                        if tag is not None and verdicts.get(tag) == "(Checked True False)"]
            missing = [(tag, t, why) for tag, t, why in entries
                       if tag is None or verdicts.get(tag) != "(Checked True False)"]
            args.hotg_verdicts[ident] = [(tag, verdicts.get(tag), why) for tag, _, why in entries]
            if missing and not accepted:
                answers[ident] = "(%Undefined%)"
            elif missing:
                answers[ident] = fo_schemes.answer_text(accepted) + "+"
            else:
                answers[ident] = fo_schemes.answer_text(accepted)
        elif status == "refuted":
            args.hotg_verdicts[ident] = verdicts.get(f"{ident}.r")
            answers[ident] = "()" if verdicts.get(f"{ident}.r") == "True" else "(%Undefined%)"
        else:
            args.hotg_verdicts[ident] = (status, checks)
            answers[ident] = "(%Undefined%)"
    return answers


MM0_RS = Path("/home/aimama/aihub/repos/mm0/mm0-rs/target/release/mm0-rs")
MM0_C = Path("/home/aimama/aihub/tmp/nik-vibe-mm0-20261002/C/upstream-baseline-v1/mm0-c")


def mm0_flat_route(args, path: Path) -> dict[str, str]:
    """The theory as an MM0 theory (`mm0_flat.py`): every positive derivation a theorem,
    compiled by mm0-rs and verified by mm0-c, with one theorem statement mutated as a
    control.  Refusals are not MM0 theorems here."""
    import fo_schemes
    import mm0_flat
    theory, planned = mm0_flat.programme_theory(list(fo_judgments(path)))
    text = theory.text()
    stem = args.work / f"{path.stem}.mm0flat"
    stem.with_suffix(".mm1").write_text(text)
    spec = re.sub(r"pub (theorem [^=]*?\$[^$]*\$)\s*=\s*'[^;]*;", r"\1;", text)
    stem.with_suffix(".mm0").write_text(spec)
    subprocess.run([str(MM0_RS), "compile", str(stem.with_suffix(".mm1")),
                    str(stem.with_suffix(".mmb"))], capture_output=True, text=True)
    with open(stem.with_suffix(".mm0")) as spec_file:
        verified = subprocess.run([str(MM0_C), str(stem.with_suffix(".mmb"))], stdin=spec_file,
                                  capture_output=True, text=True).returncode == 0
    # The mutation control: the first theorem claims a type it does not have.
    mutated = re.sub(r"(pub theorem [^:]*: \$ ty \S+ )(\S+)( \$;)", r"\1h_mutant\3", spec, count=1)
    control = True
    if mutated != spec:
        mutated = mutated.replace("sort atom;", "sort atom;\nterm h_mutant: atom;", 1)
        stem.with_suffix(".mutant.mm0").write_text(mutated)
        with open(stem.with_suffix(".mutant.mm0")) as spec_file:
            control = subprocess.run([str(MM0_C), str(stem.with_suffix(".mmb"))], stdin=spec_file,
                                     capture_output=True, text=True).returncode != 0
    args.mm0_verdicts = getattr(args, "mm0_verdicts", {})
    answers: dict[str, str] = {}
    by_ident: dict[str, list] = {}
    for ident, k, name, typ, why in planned:
        by_ident.setdefault(ident, []).append((name, typ))
    for ident, space, expression, typing in fo_judgments(path):
        entries = by_ident.get(ident, [])
        args.mm0_verdicts[ident] = (verified, control, entries)
        accepted = [t for name, t in entries if name and verified and control]
        if entries and len(accepted) == len(entries):
            answers[ident] = fo_schemes.answer_text(accepted)
        elif accepted:
            answers[ident] = fo_schemes.answer_text(accepted) + "+"
        else:
            answers[ident] = "(%Undefined%)"
    return answers


def earned_refusal(verdict) -> bool:
    """`(Attempt (Read ...) Refused)`: every prefix read, the failing step refused."""
    if not verdict:
        return False
    try:
        atom = read_forms(verdict)[0].atom
    except Exception:
        return False
    return (isinstance(atom, tuple) and len(atom) == 3 and str(atom[0]) == "Attempt"
            and isinstance(atom[1], tuple) and len(atom[1]) > 0
            and all(str(item) == "Read" for item in atom[1]) and str(atom[2]) == "Refused")


def same_answer(left: str, right: str) -> bool:
    """Equal type lists up to order and renaming of variables in each type."""
    import fo_schemes
    if left == right:
        return True
    try:
        a = fo_schemes.read_forms(left)[0].atom
        b = fo_schemes.read_forms(right)[0].atom
    except Exception:
        return False
    if not (isinstance(a, tuple) and isinstance(b, tuple)):
        return False
    key = lambda t: show(fo_schemes.canonical(t))
    return sorted(map(key, a)) == sorted(map(key, b))


ROUTES = {
    "fo-dep": lambda args, path: fo_route(path, True),
    "lf-dep": lambda args, path: guest_route(args, path, "lf", True),
    "hotg-dep": lambda args, path: guest_route(args, path, "hotg", True),
    "fo-schemes": lambda args, path: fo_route(path),
    "lf": lambda args, path: guest_route(args, path, "lf"),
    "hotg": hotg_route,
    "he-upstream": lambda args, path: he_upstream(args.upstream_root, path),
    "cetta-he": lambda args, path: cetta(["--lang", "he", "--profile", "he"], path),
    "cetta-ext": lambda args, path: cetta(["--lang", "he", "--profile", "extended"], path),
    "prime-hol": lambda args, path: cetta(["--lang", "prime"], path),
    "hol-flat": hol_flat_route,
    "mm0-flat": mm0_flat_route,
}


PROBE = ("profile_he_prime_dependent_binders", "profile_he_prime_type_formation",
         "profile_he_prime_dtt_typed_corpus", "profile_he_prime_dtt_tutorial_ladder")


def programmes(args) -> list[corpus.Programme]:
    if args.probe:
        return [corpus.file_programme(name, ROOT / "tests" / f"{name}.metta", f"p{i}.")
                for i, name in enumerate(PROBE)]
    found = corpus.upstream_programmes(args.upstream_root)
    found.append(corpus.file_programme("rust_type_tests", HERE / "rust_type_tests.metta", "r."))
    found.append(corpus.file_programme("separating", HERE / "separating.metta", "s."))
    return found


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--upstream-root", type=Path, default=ROOT.parent / "hyperon-experimental")
    parser.add_argument("--routes", default=",".join(ROUTES))
    parser.add_argument("--kernel-root", type=Path,
                        default=ROOT.parents[1] / "Mettapedia" / "MettaKernel" / "kernel")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta",
                        help="the CeTTa binary of the cetta-he, cetta-ext and prime-hol routes")
    parser.add_argument("--probe", action="store_true",
                        help="the he-prime dependent probe instead of HE's corpus")
    args = parser.parse_args()
    global CETTA
    CETTA = args.cetta
    args.work.mkdir(parents=True, exist_ok=True)
    selected = [name for name in args.routes.split(",") if name]
    table = []
    for programme in programmes(args):
        path = args.work / f"{programme.name}.metta"
        path.write_text(programme.text())
        answers = {route: ROUTES[route](args, path) for route in selected if route in ROUTES}
        import fo_schemes
        fragments = {ident: ",".join(fo_schemes.features(space, expression))
                     for ident, space, expression, _ in fo_judgments(path)}
        for judgment in programme.judgments:
            row = {"id": judgment.ident, "source": judgment.source,
                   "expression": judgment.expression, "kind": judgment.kind,
                   "asserted": judgment.expected}
            for route in selected:
                row[route] = answers.get(route, {}).get(judgment.ident, "-")
            row["fragment"] = fragments.get(judgment.ident, "")
            truth = row.get("he-upstream")
            if truth is not None:
                for route in selected:
                    if route != "he-upstream":
                        row[route + "?"] = same_answer(row[route], truth)
            table.append(row)
    if args.json:
        args.json.write_text(json.dumps(table, indent=1))
    width = max(len(row["expression"]) for row in table)
    header = ["id".ljust(7), "judgment".ljust(min(width, 48))] + [r.ljust(22) for r in selected]
    print("  ".join(header))
    for row in table:
        cells = [row["id"].ljust(7), row["expression"][:48].ljust(min(width, 48))]
        for route in selected:
            mark = "" if route == "he-upstream" or (route + "?") not in row else (
                " =" if row[route + "?"] else " ≠")
            cells.append((row[route][:20] + mark).ljust(22))
        print("  ".join(cells))
    for route in selected:
        if route == "he-upstream" or not table or (route + "?") not in table[0]:
            continue
        agree = sum(1 for row in table if row[route + "?"])
        print(f"{route}: {agree}/{len(table)} agree with he-upstream")


if __name__ == "__main__":
    main()
