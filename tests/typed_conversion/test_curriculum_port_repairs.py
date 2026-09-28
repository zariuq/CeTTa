#!/usr/bin/env python3
"""Run full source ports and reject nearby changes to their mathematical claims."""

import argparse
import json
import os
import subprocess
from pathlib import Path

import curriculum_port_repairs as ports
import build_curriculum_ledger as ledger
import test_cic_natmax
import test_lf_guest_boundaries
import test_curriculum_closeout_metadata
from curriculum_trace import INVALID, final_output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ports.ROOT / "runtime/curriculum-port-controls")
    parser.add_argument("--timeout", type=float)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipts = {}
    for name in ports.EXPECTED:
        ok, receipt = ports.execute(binary, ports.PORTS / (name + ".metta"), output, args.timeout)
        assert ok, (name, receipt)
        receipts[name] = receipt
        print("PASS curriculum faithful " + name)
    mutations = {
        "negbot3": ("(id bool (eval (lam n true) Bot) true)", "(id bool (eval (lam n true) Bot) false)"),
        "negbot": ("(id bool (eval (lam n true) Bot) true)", "(id bool (eval (lam n true) Bot) false)"),
        "eqset": ("(imp (Q a a) (Q a a))", "(imp (Q a a) (all prop (lam p p)))"),
        "update": ("(update G x T x) (some T)", "(update G x T x) none"),
        "badrfl": ("(id nat zero (succ zero))", "(id nat zero zero)"),
        "badwitness": ("(id nat (add n n) (succ (succ (succ (succ (succ zero))))))", "(id nat (add n n) (succ (succ (succ (succ zero)))))"),
        "decide": ("(id bool (decide_valid (Var zero)) true)", "(id bool (decide_valid (Var zero)) false)"),
        "pair": ("(= (pair-proj $A $B $a $b) (refl $a))", "(= (pair-proj $A $B $a $b) (refl $b))"),
        "signlit": ("(eq form (signLit (setv v n true) n) (FVar n))", "(eq form (signLit (setv v n true) n) (Neg (FVar n)))"),
        "dne": ("(valid (Imp (Neg (Neg f)) f))", "(valid f)"),
        "hotg": ("(pf:imp-elim (pf:hyp 0) (pf:hyp 1))", "(pf:hyp 1)"),
        "setv": ("$b ($v $m)", "$b $b"),
        "value": ("(= (nvalue (tsucc $t)) (nvalue $t))", "(= (nvalue (tsucc $t)) truth)"),
        "shift": ("(shiftAbove (succ $cut) $inc $b)", "(shiftAbove $cut $inc $b)"),
    }
    for name, (old, new) in mutations.items():
        source = (ports.PORTS / (name + ".metta")).read_text()
        assert source.count(old) == 1, (name, "mutation site changed")
        mutant = output / (name + "-mutant.metta")
        mutant.write_text(source.replace(old, new))
        result = subprocess.run([str(binary), "--lang", "prime", str(mutant)],
                                cwd=ports.ROOT, capture_output=True, text=True, timeout=args.timeout)
        (output / (name + "-mutant.stdout")).write_text(result.stdout)
        (output / (name + "-mutant.stderr")).write_text(result.stderr)
        assert result.returncode == 0 and not result.stderr.strip(), (name, result.stderr)
        if name in {"badrfl", "badwitness", "decide", "negbot", "negbot3"}:
            assert "[False]" not in result.stdout.splitlines() and result.stdout.splitlines()[-1] == "[True]", (name, result.stdout)
            print("PASS curriculum accepts corrected " + name)
        else:
            assert "[False]" in result.stdout.splitlines(), (name, result.stdout)
            print("PASS curriculum rejects changed " + name)
    atoms = [0, 1, None]
    shallow = atoms + [(a, b) for a in atoms for b in atoms]
    formulas = atoms + [(a, b) for a in shallow for b in shallow]

    def render(form):
        if form is None:
            return 'Bot'
        if isinstance(form, int):
            return '(Var ' + ('zero' if form == 0 else '(succ zero)') + ')'
        return '(Imp ' + render(form[0]) + ' ' + render(form[1]) + ')'

    def truth(form, valuation):
        if form is None:
            return False
        if isinstance(form, int):
            return valuation[form]
        return not truth(form[0], valuation) or truth(form[1], valuation)

    setup = (ports.PORTS / 'decide.metta').read_text().split('!(set:eq')[0]
    queries = ['!(set:eq &thy (decide_valid ' + render(form) + ') ' +
               ('true' if all(truth(form, (a, b)) for a in (False, True) for b in (False, True)) else 'false') + ')'
               for form in formulas]
    oracle = output / 'decide-truth-table.metta'
    oracle.write_text(setup + '\n'.join(queries) + '\n')
    result = subprocess.run([str(binary), '--lang', 'prime', str(oracle)], cwd=ports.ROOT,
                            capture_output=True, text=True, timeout=args.timeout)
    (output / 'decide-truth-table.stdout').write_text(result.stdout)
    (output / 'decide-truth-table.stderr').write_text(result.stderr)
    assert result.returncode == 0 and not result.stderr, result.stderr
    assert result.stdout.splitlines() == ['[' + item + ']' for item in ports.EXPECTED['decide'][:24]] + ['[True]'] * len(formulas)
    print(f'PASS curriculum decision procedure: {len(formulas)} independent truth-table checks')
    assert len(ports.REPAIRED) == 19 and len(ports.UNFINISHED) == 9
    assert not ports.REPAIRED.keys() & ports.UNFINISHED.keys()
    assert ledger._unencoded("theorem no_port_available : arbitrary_claim") == ""
    for name in ports.UNFINISHED_NAMES:
        assert ports.program(name) == "", name
    print("PASS curriculum seven proof and two frontend obligations stay unclaimed")
    invalid_runs = [
        (0, ['[(Error setup bad)]', '[accepted]'], ''),
        (0, ['[False]', '[accepted]'], ''),
        (1, ['[accepted]'], ''),
        (0, ['[accepted]'], 'runtime diagnostic'),
        (0, ['[(Error setup bad)]', '[False]'], ''),
    ]
    for code, lines, err in invalid_runs:
        verdict = final_output(code, lines, err)
        assert verdict.startswith(INVALID), (code, lines, err, verdict)
        row = {"source_statement": "Fail Definition rejected := missing.", "id": "control"}
        ledger._store_row_program(row, '!(set:check missing T)\n', verdict, output)
        ledger.apply_addendum4([row])
        assert row['capability'] == 'missing', row
    assert final_output(0, ['[setup]', '[False]'], '') == '[False]'
    row = {"source_statement": "Fail Definition rejected := missing.", "id": "control"}
    ledger._store_row_program(row, '!(set:check missing T)\n', '[False]', output)
    assert row['capability'] == 'implemented', row
    ledger._store_row_program(row, '!(set:check missing T)\n', '[True]', output)
    assert row['capability'] == 'defect', row
    for unresolved in ['[]', '[Undetermined]', '[(Incomplete fuel-exhausted)]', '[(missing T)]']:
        ledger._store_row_program(row, '!(set:check missing T)\n', unresolved, output)
        assert row['capability'] == 'missing', row
    for failed_target in ['[(Error target bad)]', '[(Error (assertEqual a b) mismatch)]']:
        ledger._store_row_program(row, '!(set:check missing T)\n', failed_target, output)
        ledger.apply_addendum4([row])
        assert row['capability'] != 'implemented', row
    assert not ledger.call_reduced('source-term', INVALID + 'setup failed')
    assert not ledger.call_reduced('source-term', 'error: reader failure')
    assert not ledger._admitted('[False]') and not ledger._admitted(INVALID + 'control')
    print("PASS curriculum rejects invalid traces before accepting negative controls")
    quoted = '(= (held) (quote (helper)))\n'
    assert ledger.prime_port(quoted, {'helper'}) == quoted
    for rel in ['Notation/10_hol_light_concrete_syntax.metta',
                'Notation/11_hol_side_by_side_showcase.metta']:
        # This gate uses its selected binary, even when the caller has no TC_CETTA.
        ledger.BIN = binary
        os.environ['TC_SCRATCH'] = str(output / 'guest-adapter')
        source = ledger.CURRICULUM / rel
        port = ledger.materialize_guest(source)
        code, lines, err = ledger.cetta_lines(port)
        assert code == 0 and not err and lines and all(line == '[()]' for line in lines), (rel, code, lines, err)
        assert len(lines) == len(ledger.top_level_bangs(source.read_text())), rel
        print('PASS curriculum notation assertions ' + rel)
    test_cic_natmax.check(binary, output / "cic-natmax")
    test_lf_guest_boundaries.check(binary, output / "lf-boundaries")
    test_curriculum_closeout_metadata.check(binary, output / "metadata")
    (output / "positive-receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")


if __name__ == "__main__":
    main()
