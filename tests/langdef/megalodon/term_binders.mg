(** fun, forall and exists with typed groups, bounded variables and
    ascriptions. *)
Infix = 502 := eq.
Binder+ exists , := ex; and.
Axiom g1 : forall x y:set, forall P:set -> prop, P x -> P y.
Axiom g2 : forall x :e X, forall y c= Y, x = y.
Axiom g3 : (fun x y => x) = (fun (x:set) (y:set) => x).
Axiom g4 : exists f:set -> set, forall x, f x = x.
Axiom g5 : forall A B:SType, forall f:A -> B, f = f.
Axiom g6 : fun x:set => fun P:set -> prop => P x.
