; ============================================================================
; ADVECTION TRACE --- Roman 4-node through PLN.Derive
; ============================================================================
;
; Companion to advection_demo.metta. The earlier file evaluates the
; deduction + revision formulas as *snapshots*; this one drives the
; same network through the actual PLN derivation engine so we see
; *dynamics*:
;
;   - the priority queue selects tasks step-by-step,
;   - each step's selected sentence is traced via lib_wmpln.metta's
;     own (trace! (SELECTED ...)) at the derivation loop,
;   - StampDisjoint gates which pairs are allowed to combine,
;   - the engine reaches the target (Inheritance A D) along TWO
;     trajectories (A->B->D and A->C->D), revises them, and produces
;     the merged (stv 0.5 ~0.9474) with the combined stamp tuple.
;
; This is the evidence-flow / discrete-control analogy applied to a
; running derivation engine: tasks move through the priority queue
; along inference rules, with stamps gating which trajectories may
; merge additively.  Calling this "advection" is a useful framing
; for the algebraic floor (CommSemiring sum-product) and for the
; discrete-Bellman parallel; it is NOT a claim that we have
; established a continuous-time advection limit for PLN derivation
; --- that would require its own theorem.
;
; KB mirrors hyperon/cetta-roman-mork-ffi/examples/bc_roman_4node.mm2:
;   A -> B (stv 0.25 0.9)   stamp (1)
;   B -> D (stv 0.5  0.9)   stamp (2)
;   A -> C (stv 0.25 0.9)   stamp (3)
;   C -> D (stv 0.5  0.9)   stamp (4)
;
; Node priors: A(0.5, 0.9), B(0.25, 0.9), C(0.25, 0.9), D(0.5, 0.9)
;
; Analytic expected result:
;   Each path Truth_Deduction = (stv 0.5 0.9)
;   Revision of two paths = (stv 0.5 18/19) = (stv 0.5 0.9473684210526316)
;   Combined stamp = (1 2 3 4)  (insertion-sorted union)
;
; Run:  ./cetta examples/advection_trace_roman.metta
; Expect: TRACE lines from the engine showing each SELECTED step,
;         then a final PLN.Query result matching the analytic expectation.
; ============================================================================

!(import! &self ../lib/lib_wmpln)

; Node priors that the deduction rule consults via (STV $atom)
(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

; Roman 4-node KB: four facts, each with its own NARS-style provenance stamp.
; The stamps are pairwise disjoint, so additive revision at A -> D is sound.
(= (kb)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

!(println! "=== Roman 4-node through PLN.Derive ===")
!(println! "Trace output below shows the engine's step-by-step task selection.")
!(println! "Look for SELECTED lines from lib_wmpln.metta line 451.")
!(println! "")

; Drive the engine and query for (Inheritance A D).
; PLN.Query returns ($TV $stamp) for the highest-confidence belief matching
; the query term.  We cap maxsteps at 25 because:
;   - by step 18 the engine derives (Inheritance A D) via C with stv (0.5 0.9),
;   - by step 19 it revises against the via-B derivation, producing
;     (stv 0.5 0.9473684210526316) with the merged stamp (1 2 3 4) ---
;     exactly the analytic prediction,
;   - extra steps add churn (further revisions whose confidence only stays
;     equal or drops) without changing the highest-confidence answer.
;
; The result should be:
;   ((stv 0.5 0.9473684210526316) (1 2 3 4))
;
; where 0.9473... = w2c(c2w(0.9) + c2w(0.9)) = 18/19, and (1 2 3 4) is the
; insertion-sorted union of the four input stamps.
!(println! "Final PLN.Query result for (Inheritance A D), maxsteps=25:")
!(println! (PLN.Query (kb) (Inheritance A D) 25))

!(println! "")
!(println! "=== Trace demo complete ===")
