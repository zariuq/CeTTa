; A practical Prime workload: a PLN deduction constructor computes its
; dependent truth-value result type and rejects an ill-shaped premise chain.

!(import! &self ../../../showcase/nil_pln_core.metta)

(: P Atom) (: Q Atom) (: R Atom)
(: pP (≞ P (STV 0.8 0.9)))
(: pQ (≞ Q (STV 0.7 0.8)))
(: pR (≞ R (STV 0.6 0.7)))
(: pPQ (≞ (→ P Q) (STV 0.75 0.8)))
(: pQR (≞ (→ Q R) (STV 0.8 0.75)))

!(assertEqual
   (get-type (Deduction pP pQ pR pPQ pQR))
   (≞ (→ P R) (STV 0.6333333333333334 0.7)))

; Reusing P→Q where Q→R is required must not synthesize a result type.
!(assertEqualToResult
   (get-type (Deduction pP pQ pR pPQ pPQ))
   ())
