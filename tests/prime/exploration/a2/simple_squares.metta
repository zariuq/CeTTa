; Erasure from the simple fragment into the candidate, read by two observers.
; On erasures of one simple type the candidate's conversion is exactly
; beta-conversion, and its typed equality is exactly beta-eta-conversion, so
; the typed equality decides such comparisons in both directions.
;
; The constants s and z have no computation; they stand for the variables of
; the context s : Base -> Base, z : Base.

(: Base (u 0))
(: s (-> Base Base))
(: z Base)

; 1. Two distinct terms with one erasure: the identity at Base, after
;    discarding an identity at Base, and after discarding an identity at
;    Base -> Base.  Terms of one type with one erasure are beta-convertible
;    (betaConv_of_erase_eq; discarded_identities_convert), so they are equal.
!(type:eq (lam (x : Base) ((lam (y : (-> Base Base)) x) (lam (w : Base) w)))
          (lam (x : Base) ((lam (y : (-> (-> Base Base) (-> Base Base))) x)
                           (lam (w : (-> Base Base)) w))))

; 2. Eta: equal in the typed equality (equal_eta, equal_iff_betaEtaConv),
;    closed under a binder and open at s.  The open pair is the eta pair of
;    eta_not_betaConv: equal here, and not beta-convertible.
!(type:eq (lam (f : (-> Base Base)) (lam (x : Base) (f x))) (lam (f : (-> Base Base)) f))
!(type:eq (lam (x : Base) (s x)) s)

; 3. Control: s z and z denote differently, so they are not
;    beta-eta-convertible, and the typed equality refutes them
;    (successorOfZero_not_equal).
!(type:eq (s z) z)
