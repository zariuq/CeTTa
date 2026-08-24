; May and Must describe a nondeterministic answer bag without flattening its
; completion evidence into the answers themselves.

(: Nat Type)
(: Bool Type)
(: Z Nat)
(: Truth Bool)

(= (choose) Z)
(= (choose) Truth)
(= (only) Z)

!(type:may (choose) Nat)
!(type:must (choose) Nat)
!(type:must (only) Nat)
!(type:must (superpose ()) Nat)
