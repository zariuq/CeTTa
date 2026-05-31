; ============================================================================
; ROMAN BUDGET SWEEP --- schedule sensitivity of derivation
; ============================================================================
;
; Companion to advection_trace_roman.metta.  The trace file uses maxsteps=25
; based on the observed fact that
;   - step 18 first derives (Inheritance A D) (stv 0.5 0.9) via one path,
;   - step 19 revises to (stv 0.5 0.9473...) with the merged stamp.
;
; That magic number is brittle.  This sweep turns it into a measurement:
; run PLN.Query at maxsteps in {5, 10, 15, 20, 25, 30, 40} on the same KB
; and observe how the answer evolves.
;
; What we'd expect a priori:
;   - tiny budgets (5, 10):    target NOT YET derived; query returns ()
;                              or an empty-result variant
;   - medium budget (15-20):    target derived along ONE path, single-stamp
;                              confidence 0.9
;   - sufficient budget (25+): target revised across BOTH paths, merged
;                              confidence ~0.9474
;
; What this shows: how quickly the engine reaches "complete" vs "merged"
; answers.  Useful for setting realistic budgets in production inference.
; Also surfaces whether the engine is monotonic (does growing budget only
; add information?) or whether it can OVERSHOOT (revising the revised
; answer with already-counted evidence and degrading confidence?).
;
; Run:  ./cetta examples/roman_budget_sweep.metta
; Expect: a table of (budget -> result) showing the transition from
;         "not-derived" through "single-path" to "merged-paths".
; ============================================================================

!(import! &self ../lib/lib_wmpln)

(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

(= (kb)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

!(println! "=============================================================")
!(println! " ROMAN BUDGET SWEEP --- (Inheritance A D) vs maxsteps")
!(println! "=============================================================")
!(println! "")

!(println! "budget= 5 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 5)))
!(println! "")

!(println! "budget=10 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 10)))
!(println! "")

!(println! "budget=15 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 15)))
!(println! "")

!(println! "budget=20 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 20)))
!(println! "")

!(println! "budget=25 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 25)))
!(println! "")

!(println! "budget=30 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 30)))
!(println! "")

!(println! "budget=40 ->")
!(println! ("    " (PLN.Query (kb) (Inheritance A D) 40)))
!(println! "")

!(println! "=============================================================")
!(println! " READING THE TABLE:")
!(println! "=============================================================")
!(println! "")
!(println! "  Smallest budget where (Inheritance A D) gets derived at all")
!(println! "  --- the *coverage* threshold.  Anything below this returns ()")
!(println! "  or an empty result.")
!(println! "")
!(println! "  Smallest budget where the answer climbs to confidence ~0.9474")
!(println! "  --- the *revision* threshold.  This is the budget you need to")
!(println! "  see the additive merge of both trajectories.")
!(println! "")
!(println! "  Any budget where confidence DROPS BELOW 0.9474 after first")
!(println! "  reaching it --- the *overshoot* warning.  Indicates the engine")
!(println! "  is performing further revisions that DEGRADE the answer (e.g.,")
!(println! "  revising already-revised sentences against single-path ones).")
