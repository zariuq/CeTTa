; A rigid open HE annotation is not a native universally quantified proof.
(: Nat (u 0))
(: RigidP (-> (u 0) (u 0)))
(: RigidWitness (RigidP $x))
!(search-first-inhabitant &self (RigidP Nat) 1 10000)
; Native polymorphism states its binder and retains its argument in the proof.
; Isolate the signature so the query's two universe inhabitants are explicit.
!(bind! &s (new-space))
!(add-atom &s (: Nat (u 0)))
!(add-atom &s (: Bool (u 0)))
!(add-atom &s (: SchemeP (-> (u 0) (u 0))))
!(add-atom &s (: SchemeWitness (-> (T : (u 0)) (SchemeP T))))
!(add-atom &s (chaining-rule SchemeWitness))
!(search-first-inhabitant &s (SchemeP Nat) 1 100000)
; The receipt must preserve distinct query and elaboration substitutions.
(= (native-scheme:receipt (typing-search $state $depth $reason $proofs $stats))
   (Search $state $depth $reason $proofs))
!(native-scheme:receipt (search-inhabitants &s (SchemeP $query) 1 100000 3))
!(type:check &s (SchemeWitness Nat) (SchemeP Nat))
!(type:check &s (SchemeWitness Nat) (SchemeP Bool))
; Stored HE schemes cannot add an undeclared native universal binder.
!(bind! &legacy (new-space))
!(add-atom &legacy (: Nat (u 0)))
!(add-atom &legacy (: SchemeP (-> (u 0) (u 0))))
!(add-atom &legacy (: SchemeWitness (SchemeP $x)))
!(add-atom &legacy (type-scheme SchemeWitness))
!(search-first-inhabitant &legacy (SchemeP Nat) 1 100000)
