; REPRO (2026-08-05, Fable closure sweep): type-directed demand REGRESSION
; in the petta lane, introduced by the overnight Need/tail-reentry tranche.
; Atom-typed parameters must remain SYNTAX (capability ledger row
; "type-directed demand", status closed; obligation: declared parameter
; types determine which arguments are evaluated).
; Attribution ladder (this exact witness + tests/petta/unsupported/
; type_directed_demand.metta):
;   pre-tranche  (base 41bdfa, Aug 4 binary): (pair 6 (+ 4 2))   CORRECT
;   tranche      (Aug 5 binaries, with and without
;                 CETTA_PETTA_SEARCH_MACHINE): (pair 6 6)        BROKEN
; The leak is NOT limited to generated total heads: a USER call in an
; Atom position also evaluates ((uf 3) -> 30 below), so the break is
; upstream of the machine's intact typed-call scheduler - most plausibly the
; new determinate positional-slot argument filling running before the
; declared-type consult.  Gate: tests/petta/test_capability_ledger.py
; (currently 1 failing contract).  Not in the test_* glob deliberately:
; promote after the seam owner repairs it.
(: tp2 (-> Number Atom Expression))
(= (tp2 $s $x) (pair $s $x))
(= (uf $a) (* $a 10))
!(tp2 (+ 2 4) (uf 3))
; expected: [(pair 6 (uf 3))]
!(tp2 (+ 2 4) (- 9 1))
; expected: [(pair 6 (- 9 1))]
