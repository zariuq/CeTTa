; DTT-chainer showcase — verified capabilities, self-checking, --profile he-prime.
; Imports the verified nil_pln_core (Nil Geisweiller's PLN in dependent types).
; The FULL chainer (backward-search inhabitation) is the runnable demo at
; ../showcase/nil_pln_dtt_exact_demo.metta; this suite test pins the
; deterministic core so `make test-profiles` catches a regression.
!(import! &self ../showcase/nil_pln_core.metta)
(: P Atom) (: Q Atom) (: R Atom)
(: pP (≞ P (STV 0.8 0.9))) (: pQ (≞ Q (STV 0.7 0.8))) (: pR (≞ R (STV 0.6 0.7)))
(: pPQ (≞ (→ P Q) (STV 0.75 0.8))) (: pQR (≞ (→ Q R) (STV 0.8 0.75)))
(: A Atom) (: B Atom) (: aA (≞ A (STV 0.9 0.8))) (: aAB (≞ (→ A B) (STV 0.95 0.7)))

; (1) THE headline (Nil's decade-disabled request): a user function runs inside
;     the dependent result type — Deduction's truth value is COMPUTED in the type.
!(assertEqual (type-of &self (quote (Deduction pP pQ pR pPQ pQR)))
              (type-set (≞ (→ P R) (STV 0.6333333333333334 0.7))))
; (2) Modus Ponens, same mechanism.
!(assertEqual (type-of &self (quote (ModusPonens aA aAB)))
              (type-set (≞ B (STV 0.875 0.7))))
; (3) Nat-refinement (the -1 gap native HE never validated): valid accepts, invalid rejects.
!(assertEqual (validate-type-properties &self (VecN String 2) 10000) (he-accept (VecN String 2)))
!(assertEqual (validate-type-properties &self (VecN String -1) 10000) (he-reject (type-property-failed -1)))
; (4) success-typing discipline: dynamic edge, laundering-block, Atom-asymmetry.
!(assertEqual (is-consistent Nat %Undefined%) (he-accept dynamic))
!(assertEqual (is-consistent Nat Bool) (he-reject (inconsistent (Nat Bool))))
!(assertEqual (is-consistent Atom Nat) (he-reject (inconsistent (Atom Nat))))
!(assertEqual (is-consistent Nat Atom) (he-accept top))
