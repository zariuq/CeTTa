; Source equations used by GSLT.LanguageDef.SurrealCountdown.
; Its ordinal rank and half-unit firing charges are mathematical contracts;
; these queries replay ordinary evaluation without measuring runtime cost.
(= (countdown Z) Done)
(= (countdown (S $n)) (countdown $n))

!(countdown Z)
!(countdown (S (S (S Z))))
!(Pair (countdown (S Z)) (countdown (S (S Z))))
!(countdown mystery)
