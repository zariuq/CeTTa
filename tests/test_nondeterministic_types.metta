; Nondeterministic types — atom with multiple type annotations
; Ref: metta.md interpret_expression, multiple types tried

; Ten has both Nat and Int type — no error because one path succeeds
(: Nat Type)
(: Int Type)
(: Z Nat)
(: Ten Nat)
(: Ten Int)
(: Add (-> Nat Nat Nat))
(= (Add $x Z) $x)

; Ten matches Nat for Add — should succeed
!(assertEqual (Add Z Ten) Ten)
