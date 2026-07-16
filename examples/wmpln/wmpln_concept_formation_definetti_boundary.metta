!(import! &self ../../lib/lib_wmpln)

; Runnable witness for the finite-gate de-Finetti concept-formation boundary.
;
; This is the hand-derived arithmetic shadow of the Lean theorems
; conceptFormationWidthComplementITV_deFinettiMixtureBoundary_dichotomy and
; conceptFormationTypedWidthComplementITV_deFinettiMixtureBoundary_dichotomy.
;
; It is deliberately a finite-gate adapter witness, not a claim that formed
; concepts have been given a full de-Finetti process semantics.

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.000001))

; Two active gates.  A concept is robust/lower-formed iff both gates accept it,
; permissive/upper-formed iff at least one gate accepts it.
(= (gate-member ROBUST-CONCEPT GATE-A) True)
(= (gate-member ROBUST-CONCEPT GATE-B) True)

(= (gate-member ABSENT-CONCEPT GATE-A) False)
(= (gate-member ABSENT-CONCEPT GATE-B) False)

(= (gate-member GAP-CONCEPT GATE-A) True)
(= (gate-member GAP-CONCEPT GATE-B) False)

(= (lower-formed $concept)
   (and (gate-member $concept GATE-A)
        (gate-member $concept GATE-B)))

(= (upper-formed $concept)
   (or (gate-member $concept GATE-A)
       (gate-member $concept GATE-B)))

(= (concept-gap $concept)
   (and (upper-formed $concept)
        (not (lower-formed $concept))))

(= (no-gap $concept)
   (not (concept-gap $concept)))

(= (bool-value True) 1.0)
(= (bool-value False) 0.0)

(= (concept-lower $concept)
   (bool-value (lower-formed $concept)))

(= (concept-upper $concept)
   (bool-value (upper-formed $concept)))

(= (interval-width $lo $hi)
   (- $hi $lo))

(= (width-complement $lo $hi)
   (- 1.0 (interval-width $lo $hi)))

(= (interval-midpoint $lo $hi)
   (/ (+ $lo $hi) 2.0))

(= (concept-formation-itv $concept)
   (itv
     (concept-lower $concept)
     (concept-upper $concept)
     (width-complement (concept-lower $concept) (concept-upper $concept))))

(= (itv-lower (itv $lo $hi $credibility))
   $lo)

(= (itv-upper (itv $lo $hi $credibility))
   $hi)

(= (itv-width (itv $lo $hi $credibility))
   (interval-width $lo $hi))

(= (itv-credibility (itv $lo $hi $credibility))
   $credibility)

(= (itv-strength (itv $lo $hi $credibility))
   (interval-midpoint $lo $hi))

; A representing de-Finetti mixture completion is glued to one active finite
; gate, hence it reads the concept-formation gamble as that gate's Boolean
; membership.  In the gap case there are endpoint completions at both values.
(= (mixture-readout $concept $active-gate)
   (bool-value (gate-member $concept $active-gate)))

(= (exact-mixture-readout $concept $active-gate)
   (and (near (itv-lower (concept-formation-itv $concept))
              (mixture-readout $concept $active-gate))
        (and (near (itv-upper (concept-formation-itv $concept))
                   (mixture-readout $concept $active-gate))
             (and (near (itv-width (concept-formation-itv $concept)) 0.0)
                  (and (near (itv-credibility (concept-formation-itv $concept)) 1.0)
                       (near (itv-strength (concept-formation-itv $concept))
                             (mixture-readout $concept $active-gate)))))))

; The exact/no-gap side: robust acceptance collapses the interval to 1.
!(assertEqualToResult (no-gap ROBUST-CONCEPT) (True))
!(assertEqualToResult (near (itv-lower (concept-formation-itv ROBUST-CONCEPT)) 1.0) (True))
!(assertEqualToResult (near (itv-upper (concept-formation-itv ROBUST-CONCEPT)) 1.0) (True))
!(assertEqualToResult (near (itv-width (concept-formation-itv ROBUST-CONCEPT)) 0.0) (True))
!(assertEqualToResult (near (itv-credibility (concept-formation-itv ROBUST-CONCEPT)) 1.0) (True))
!(assertEqualToResult (near (itv-strength (concept-formation-itv ROBUST-CONCEPT)) 1.0) (True))
!(assertEqualToResult (exact-mixture-readout ROBUST-CONCEPT GATE-A) (True))
!(assertEqualToResult (exact-mixture-readout ROBUST-CONCEPT GATE-B) (True))

; The other exact/no-gap side: absence from every gate collapses the interval to 0.
!(assertEqualToResult (no-gap ABSENT-CONCEPT) (True))
!(assertEqualToResult (near (itv-lower (concept-formation-itv ABSENT-CONCEPT)) 0.0) (True))
!(assertEqualToResult (near (itv-upper (concept-formation-itv ABSENT-CONCEPT)) 0.0) (True))
!(assertEqualToResult (near (itv-width (concept-formation-itv ABSENT-CONCEPT)) 0.0) (True))
!(assertEqualToResult (near (itv-credibility (concept-formation-itv ABSENT-CONCEPT)) 1.0) (True))
!(assertEqualToResult (near (itv-strength (concept-formation-itv ABSENT-CONCEPT)) 0.0) (True))
!(assertEqualToResult (exact-mixture-readout ABSENT-CONCEPT GATE-A) (True))
!(assertEqualToResult (exact-mixture-readout ABSENT-CONCEPT GATE-B) (True))

; The negative/gap side: one admissible gate accepts and one rejects.  The
; displayed ITV is [0,1] with credibility 0 and midpoint 1/2; neither endpoint
; mixture readout is allowed to masquerade as the whole truth.
!(assertEqualToResult (concept-gap GAP-CONCEPT) (True))
!(assertEqualToResult (no-gap GAP-CONCEPT) (False))
!(assertEqualToResult (near (mixture-readout GAP-CONCEPT GATE-A) 1.0) (True))
!(assertEqualToResult (near (mixture-readout GAP-CONCEPT GATE-B) 0.0) (True))
!(assertEqualToResult (near (itv-lower (concept-formation-itv GAP-CONCEPT)) 0.0) (True))
!(assertEqualToResult (near (itv-upper (concept-formation-itv GAP-CONCEPT)) 1.0) (True))
!(assertEqualToResult (near (itv-width (concept-formation-itv GAP-CONCEPT)) 1.0) (True))
!(assertEqualToResult (near (itv-credibility (concept-formation-itv GAP-CONCEPT)) 0.0) (True))
!(assertEqualToResult (near (itv-strength (concept-formation-itv GAP-CONCEPT)) 0.5) (True))
!(assertEqualToResult (exact-mixture-readout GAP-CONCEPT GATE-A) (False))
!(assertEqualToResult (exact-mixture-readout GAP-CONCEPT GATE-B) (False))

; Width-complement coherence for all three branches.
!(assertEqualToResult
   (near (+ (itv-width (concept-formation-itv ROBUST-CONCEPT))
            (itv-credibility (concept-formation-itv ROBUST-CONCEPT)))
         1.0)
   (True))
!(assertEqualToResult
   (near (+ (itv-width (concept-formation-itv ABSENT-CONCEPT))
            (itv-credibility (concept-formation-itv ABSENT-CONCEPT)))
         1.0)
   (True))
!(assertEqualToResult
   (near (+ (itv-width (concept-formation-itv GAP-CONCEPT))
            (itv-credibility (concept-formation-itv GAP-CONCEPT)))
         1.0)
   (True))
