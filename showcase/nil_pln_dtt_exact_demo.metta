!(import! &self nil_pln_core.metta)

(: P Atom) (: Q Atom) (: R Atom)
(: pP (≞ P (STV 0.8 0.9)))
(: pQ (≞ Q (STV 0.7 0.8)))
(: pR (≞ R (STV 0.6 0.7)))
(: pPQ (≞ (→ P Q) (STV 0.75 0.8)))
(: pQR (≞ (→ Q R) (STV 0.8 0.75)))

; Nil's exact disabled request: a function executes inside the dependent result.
!(type-of &self (quote (Deduction pP pQ pR pPQ pQR)))
!(check-typing &self
  (quote (Deduction pP pQ pR pPQ pQR))
  (≞ (→ P R) (deduction-formula
      (STV 0.8 0.9) (STV 0.7 0.8) (STV 0.6 0.7)
      (STV 0.75 0.8) (STV 0.8 0.75)))
  100000)
!(type-inhabit-first &self
  (≞ (→ P R) (deduction-formula
      (STV 0.8 0.9) (STV 0.7 0.8) (STV 0.6 0.7)
      (STV 0.75 0.8) (STV 0.8 0.75)))
  2 500000)

(: A Atom) (: B Atom)
(: aA (≞ A (STV 0.9 0.8)))
(: aAB (≞ (→ A B) (STV 0.95 0.7)))
!(type-inhabit-first &self
  (≞ B (modus-ponens-formula (STV 0.9 0.8) (STV 0.95 0.7)))
  2 300000)

(: p2 (⊷ P (S (S Z)) True))
(: q2 (⊷ Q (S (S Z)) False))
!(type-inhabit-first &self
  (≞ (→ P Q) (ETV (ECons (S (S Z)) ENil) (STV 0.0 0.5)))
  2 300000)

(: p7 (⊷ P (S (S (S (S (S (S (S Z))))))) True))
(: q7 (⊷ Q (S (S (S (S (S (S (S Z))))))) True))
!(type-inhabit-first &self
  (≞ (→ P Q)
     (ETV (ECons (S (S (S (S (S (S (S Z))))))) (ECons (S (S Z)) ENil))
          (STV 0.5 0.6666666666666666)))
  5 800000)

!(validate-type-properties &self (VecN String 2) 10000)
!(validate-type-properties &self (VecN String -1) 10000)
!(type-inhabit-first &self (⍃ (S (S Z)) (S (S (S (S (S (S (S Z)))))))) 5 800000)
!(type-inhabit &self
  (≞ (→ P Q)
     (ETV (ECons (S (S (S (S (S (S (S Z))))))) (ECons (S (S Z)) ENil))
          (STV 0.5 0.6666666666666666)))
  5 2000000 20)
