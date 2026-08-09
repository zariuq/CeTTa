; Prime let patterns share one monotone relational context.  Reusing a name in
; a later premise refines the established value; no auxiliary unify call is
; needed merely to preserve a cross-premise constraint.

(: imp (-> Type Type Type))

(= (infer ax1)
   (: (ax1-explicit $ph $ps)
      (imp $ph (imp $ps $ph))))

(= (infer (mp $implication $antecedent))
   (let (: $implication-proof (imp $antecedent-type $result-type))
        (infer $implication)
     (let (: $antecedent-proof $antecedent-type)
          (infer $antecedent)
       (: (mp $implication-proof $antecedent-proof) $result-type))))

; Positive: mp ax1 ax1 has the schematic shape X -> (Y -> (Z -> Y)).
!(assertEqualToResult
   (let (: $proof (imp $x (imp $y (imp $z $y))))
        (infer (mp ax1 ax1))
     shared-refinement-preserved)
   (shared-refinement-preserved))

; Negative: no instantiation of that proof has an atomic result type.
!(assertEqualToResult
   (let (: $proof atomic-result)
        (infer (mp ax1 ax1))
     impossible-refinement)
   ())

; A generic equation argument remains lazy until `match` consumes its pattern
; structure.  Crossing that equation boundary must neither hide the pattern
; behind a private suspension nor lose refinements produced by the match.
(: need-match-g (-> String Bool))

(= (need-match-through-equation $query)
   (match &self $query $query))

!(assertEqualToResult
   (need-match-through-equation (: need-match-g (-> $input Bool)))
   ((: need-match-g (-> String Bool))))

; Demanding the pattern does not manufacture a match for absent data.
!(assertEqualToResult
   (need-match-through-equation (: need-match-missing (-> $input Bool)))
   ())

!(println! (PrimeSequentialUnificationSummary 4 4 0))
