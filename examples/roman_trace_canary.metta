; ============================================================================
; ROMAN TRACE CANARY --- presence + top-candidate, separated
; ============================================================================
;
; Companion canary to advection_trace_roman.metta.  Same KB, but
; separates two concerns that PLN.Query (lib_wmpln.metta:473) conflates:
;
;   (1) PRESENCE.  Did the engine ever derive ANY (Inheritance A D)
;       belief at all?  A yes/no question about the inference engine's
;       *coverage*, independent of ranking policy.
;
;   (2) TOP.  Among the derived (Inheritance A D) beliefs, which one
;       has the highest confidence?  A question about *ranking + revision
;       policy*, independent of whether the target was reached.
;
; PLN.Query answers (2) directly, by running BestCandidate ConfidenceRank
; over the collapsed matching beliefs.  If the ranking policy were buggy
; or if revision didn't fire, PLN.Query could silently return a
; "good-looking" but wrong top.  Separating the two assertions catches
; this regression class.
;
; This canary runs PLN.Derive directly, collapses all matching beliefs,
; and asserts:
;   (a) at least one matching belief exists  (presence)
;   (b) the best-confidence one matches the analytic prediction
;       (stv 0.5 0.9473684210526316) with stamps (1 2 3 4)  (top)
;
; Run:  ./cetta examples/roman_trace_canary.metta
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

; Collapse all beliefs whose term matches (Inheritance A D).
; Returns a tuple of ($TV $Stamp) pairs --- one for each derived
; A->D belief, including all revised forms.
(= (target-matches)
   (collapse
     (let ($TasksRet $BeliefsRet) (PLN.Derive (kb) (kb) 25)
          (case (superpose $BeliefsRet)
                (((Sentence ((Inheritance A D) $TV) $Ev) ($TV $Ev)))))))

!(println! "=============================================")
!(println! "CANARY 1 (presence) --- did A->D get derived?")
!(println! "=============================================")
!(println! ("  matching beliefs:" (target-matches)))
!(println! ("  matching count  :" (TupleCount (target-matches))))

; Presence: non-empty tuple of matching beliefs
!(assertEqual (> (TupleCount (target-matches)) 0) True)

!(println! "")
!(println! "=============================================")
!(println! "CANARY 2 (top) --- highest-confidence answer")
!(println! "=============================================")
!(println! ("  top candidate:" (BestCandidate ConfidenceRank () (target-matches))))

; Top: best candidate matches the analytic answer
; The two-path revision produces (stv 0.5 18/19) with merged stamps.
!(assertEqual
   (BestCandidate ConfidenceRank () (target-matches))
   ((stv 0.5 0.9473684210526316) (1 2 3 4)))

!(println! "")
!(println! "=============================================")
!(println! "Both canaries passed.")
!(println! "=============================================")
