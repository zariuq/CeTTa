#!/usr/bin/env python3
"""Named-state authority composes with transaction views and worker scopes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


SPACES = """!(bind! &left (new-state (new-space)))
!(add-atom (get-state &left) (key 7))
!(bind! &right (new-state (new-space)))
!(add-atom (get-state &right) (key 9))
"""

CASES = (
    ("scalar and direct Space reads",
     SPACES + """!(bind! counter (new-state 17))
!(transaction (get-state counter))
!(transaction (let $s (get-state &left) (collapse (match $s (key $x) $x))))
""", ["true"] * 5 + ["17", "(7)"]),
    ("nested algebra reads",
     SPACES + """!(transaction (let $s
  (space-union (get-state &left) (get-state &right))
  (collapse (match $s (key $x) $x))))
!(transaction (let $s
  (space-intersection (get-state &left) (get-state &left))
  (collapse (match $s (key $x) $x))))
""", ["true"] * 4 + ["(7 9)", "(7)"]),
    ("nested writes execute once and bind outside rollback",
     SPACES + """!(bind! counter (new-state 0))
!(collapse (transaction (let $s
  (space-union
    (let $_ (change-state! counter (+ (get-state counter) 1)) (get-state &left))
    (let $_ (bind! created (new-state 41)) (get-state &right)))
  (empty))))
!(get-state counter)
!(get-state created)
""", ["true"] * 5 + ["()", "1", "41"]),
    ("existing Space exported to outside state",
     SPACES + """!(bind! &saved (new-state 0))
!(transaction (change-state! &saved (get-state &left)))
!(collapse (match (get-state &saved) (key $x) $x))
!(add-atom (get-state &left) (key 8))
!(collapse (match (get-state &saved) (key $x) $x))
""", ["true"] * 6 + ["(7)", "true", "(7 8)"]),
    ("read your writes and retain computed snapshot after rollback",
     SPACES + """!(bind! &saved (new-state 0))
!(collapse (transaction
  (let $l (get-state &left)
    (let $_ (add-atom $l (key 8))
      (let $s (space-union (get-state &left) (get-state &right))
        (let $_ (change-state! &saved $s) (empty)))))))
!(collapse (match (get-state &left) (key $x) $x))
!(collapse (match (get-state &saved) (key $x) $x))
""", ["true"] * 5 + ["()", "(7)", "(7 8 9)"]),
    ("fresh Space and fresh state survive successful publication",
     """!(bind! &saved (new-state 0))
!(transaction (let $s (new-space)
  (let $_ (add-atom $s (key 11)) (change-state! &saved $s))))
!(collapse (match (get-state &saved) (key $x) $x))
!(transaction (bind! fresh (new-state (pair state 41))))
!(get-state fresh)
""", ["true", "true", "(11)", "true", "(pair state 41)"]),
    ("fresh outside owners survive failed transaction without its row writes",
     """!(bind! &saved (new-state 0))
!(collapse (transaction (let $s (new-space)
  (let $_ (add-atom $s (key 11))
    (let $_ (change-state! &saved $s)
      (let $_ (bind! fresh (new-state (pair survived 42))) (empty)))))))
!(get-state fresh)
!(add-atom (get-state &saved) (key 12))
!(collapse (match (get-state &saved) (key $x) $x))
""", ["true", "()", "(pair survived 42)", "true", "(12)"]),
    ("nested transaction views",
     SPACES + """!(bind! &saved (new-state 0))
!(transaction (let $l (get-state &left)
  (let $_ (add-atom $l (key 8))
    (transaction (let $s
      (space-union (get-state &left) (get-state &right))
      (change-state! &saved $s))))))
!(collapse (match (get-state &left) (key $x) $x))
!(collapse (match (get-state &saved) (key $x) $x))
""", ["true"] * 6 + ["(7 8)", "(7 8 9)"]),
    ("child commit remains inside outer rollback",
     SPACES + """!(collapse (transaction (let $l (get-state &left)
  (let $_ (add-atom $l (key 8))
    (let $_ (transaction (let $t (get-state &left) (add-atom $t (key 10))))
      (empty))))))
!(collapse (match (get-state &left) (key $x) $x))
""", ["true"] * 4 + ["()", "(7)"]),
    ("named Space publication refreshes aliases without replay",
     """!(bind! &alias (new-space))
!(add-atom &alias (key 5))
!(bind! &old (new-state &alias))
!(transaction (let $_ (collapse (bind! &alias (new-space)))
  (let $_ (add-atom &alias (key 6))
    (let $s (space-union &alias &alias)
      (collapse (match $s (key $x) $x))))))
!(collapse (match &alias (key $x) $x))
!(collapse (match (get-state &old) (key $x) $x))
!(collapse (transaction (let $_ (collapse (bind! &alias (new-space)))
  (let $_ (add-atom &alias (key 99)) (empty)))))
!(collapse (match &alias (key $x) $x))
""", ["true", "true", "(6 6)", "(6)", "(5)", "()", "()"]),
    ("new named Space preserves intervening local binding delta",
     """!(transaction (let $_ (add-atom &gap (gap 3))
  (let $_ (collapse (bind! &fresh_space (new-space)))
    (add-atom &fresh_space (key 4)))))
!(collapse (match &gap (gap $x) $x))
!(collapse (match &fresh_space (key $x) $x))
""", ["true", "(3)", "(4)"]),
    ("extended constructor binding keeps authored name inert",
     """!(bind! constructor_count (new-state 0))
!(transaction (collapse (bind!
  (let $_ (change-state! constructor_count (+ (get-state constructor_count) 1))
    (atom_concat &effect_ slot))
  (new-space))))
!(get-state constructor_count)
!(add-atom &effect_slot (key 4))
!(collapse (match &effect_slot (key $x) $x))
""", ["true", "()", "0", "true", "(4)"]),
    ("ready Space producer is evaluated once before named-state publication",
     """!(bind! constructor_count (new-state 0))
!(transaction (bind! &ready (new-state (let $s (new-space)
  (let $_ (change-state! constructor_count (+ (get-state constructor_count) 1)) $s)))))
!(get-state constructor_count)
!(add-atom (get-state &ready) (key 4))
!(collapse (match (get-state &ready) (key $x) $x))
""", ["true", "true", "1", "true", "(4)"]),
    ("PeTTa descriptor names remain unbound without evaluating their data",
     """!(bind! name_effect (new-state 0))
!(transaction (collapse (bind!
  (resolve-name (quote (descriptor (change-state! name_effect 1))))
  (new-space))))
!(get-state name_effect)
!(collapse (match
  (resolve-name (quote (descriptor (change-state! name_effect 1)))) (key $x) $x))
""", ["true", "()", "0", "()"]),
    ("repeated transaction retirement leaves following plans live",
     SPACES + """(= (retire $n) (if (== $n 0) done
  (let $_ (transaction (let $s (space-union (get-state &left) (get-state &right))
    (size-atom (collapse (match $s (key $x) $x)))))
    (retire (- $n 1)))))
!(retire 40)
!(transaction (collapse (hyperpose ((car-atom (catch (get-state missing_worker)))))))
!(+ 20 22)
!(collapse (match (get-state &left) (key $x) $x))
""", ["true"] * 4 + ["done", "(Error)", "42", "(7)"]),
    ("captured operands and isolated worker globals",
     SPACES + """!(bind! counter (new-state 19))
!(let $l (get-state &left) (let $r (get-state &right)
  (transaction (size-atom (collapse (let $s
    (hyperpose ((space-union $l $r) (space-intersection $l $l)))
    (match $s (key $x) $x)))))))
!(transaction (collapse (hyperpose
  ((car-atom (catch (get-state counter)))))))
!(transaction (collapse (hyperpose
  ((let $_ (change-state! counter 99) (get-state counter))))))
!(get-state counter)
""", ["true"] * 5 + ["3", "(Error)", "(99)", "19"]),
    ("nested inline worker does not inherit transaction authority",
     """!(change-state! parent 10)
!(collapse (hyperpose
  ((let $_ (change-state! outer_worker 17)
    (transaction (let $inner (collapse (hyperpose
      ((car-atom (catch (get-state outer_worker))))))
      ($inner (get-state outer_worker))))))))
!(car-atom (catch (get-state outer_worker)))
!(get-state parent)
""", ["true", "(((Error) 17))", "Error", "10"]),
)


def run_source(binary, source, env, language="petta"):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(source)
        path = f.name
    try:
        return subprocess.run(
            [binary, "--profile", "extended", "--lang", language, path],
            env=env, capture_output=True, text=True, timeout=120)
    finally:
        Path(path).unlink()


def main():
    binary = os.path.abspath(sys.argv[1])
    for reference in ("0", "1"):
        env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1",
                   CETTA_OPEN_EQUATIONS_REFERENCE=reference)
        for name, source, expected in CASES:
            result = run_source(binary, source, env)
            actual = result.stdout.splitlines()
            if result.returncode or result.stderr or actual != expected:
                raise SystemExit(
                    f"FAIL: {name}, reference={reference}, exit={result.returncode}; "
                    f"actual={actual!r}, expected={expected!r}; {result.stderr}")
            print(f"PASS: {name}, reference={reference}")
    structural = """!(bind! (resolve-name (quote (tag one))) (new-space))
!(add-atom (resolve-name (quote (tag one))) (key 5))
!(collapse (match (resolve-name (quote (tag one))) (key $x) $x))
"""
    result = run_source(binary, structural, env, "he")
    if result.returncode or result.stderr or result.stdout.splitlines() != ["[()]", "[()]", "[(5)]"]:
        raise SystemExit(f"FAIL: HE inert structural binding changed: {result.returncode}, "
                         f"{result.stdout!r}, {result.stderr!r}")
    print("PASS: generic HE structural binding retains its inert authored-name policy")


if __name__ == "__main__":
    main()
