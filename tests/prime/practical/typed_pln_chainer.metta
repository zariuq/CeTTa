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

; Independent dependent premises retain only the lexical support needed by
; each nested type query, rather than every earlier caller frame.
(: SupportMeasure Type)
(: SupportValue (-> Number SupportMeasure))
(: SupportEvidence (-> Proc SupportMeasure Type))
(: support-premise (SupportEvidence SupportEvent (SupportValue 1)))
(: SupportReturnFirst
   (-> (SupportEvidence $x0 $m0)
       (SupportEvidence $x1 $m1)
       (SupportEvidence $x2 $m2)
       (SupportEvidence $x0 $m0)))
!(assertEqual
   (get-type (SupportReturnFirst support-premise support-premise support-premise))
   (SupportEvidence SupportEvent (SupportValue 1)))
!(assertEqualToResult
   (get-type (SupportReturnFirst support-premise 7 support-premise))
   ())
