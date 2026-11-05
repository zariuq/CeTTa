(** Binders, with comma or arrow, and Binder+ with its second term. *)
Binder \/_ , := famunion.
Binder+ exists , := ex; and.
Binder lam_ => := setlam.
Axiom c1 : \/_ x :e X, F x.
Axiom c2 : exists x y :e X, p x y.
Axiom c3 : lam_ x :e X => f x.
