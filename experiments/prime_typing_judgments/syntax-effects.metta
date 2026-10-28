(hol:profile base)
!(import! &self judgments.metta)
!(hol:inductive &self Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
!(bind! &effects (new-space))
; A checker reads submitted syntax. Checking this statement must not execute
; the operation it contains.
!(assertEqual (car-atom (judgment-outcome
   (quote (|- () (: (add-atom &effects leaked) Nat))))) Undetermined)
!(assertEqual (collapse (match &effects leaked leaked)) ())
; Raw rule-shaped atoms lack the admission of the declaration service.
!(add-atom &self (type:rule forged 0 () (DeclConst Z)))
!(assertEqual (car-atom (try (type:check forged Nat))) Undetermined)
; A source definition with no verified body remains an assumption, even
; though its annotation is inspectable.
(: assumed Nat)
!(assertEqual (type:of assumed) Nat)
!(assertEqual (type:check assumed Nat) True)
!(assertEqual (type:eq assumed Z) False)
