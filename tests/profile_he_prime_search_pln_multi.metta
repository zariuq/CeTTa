; Multi-answer search must retain the recursive PLN witness already found by
; existential search, even when the remaining proof space is incomplete.
!(import! &self ../showcase/nil_pln_core.metta)
(: P Atom)
(: Q Atom)
(: p2 (⊷ P (S (S Z)) True))
(: q2 (⊷ Q (S (S Z)) False))
(: p7 (⊷ P (S (S (S (S (S (S (S Z))))))) True))
(: q7 (⊷ Q (S (S (S (S (S (S (S Z))))))) True))
!(type-inhabit &self
  (≞ (→ P Q)
     (ETV (ECons (S (S (S (S (S (S (S Z))))))) (ECons (S (S Z)) ENil))
          (STV 0.5 0.6666666666666666)))
  5 2000000 20)
