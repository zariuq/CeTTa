(** The negative of tac_rewrite_nearly_ok: here the claim holds "f6 a" where
    that one holds "g a", so the side of the equation occurs nowhere.  g and
    f6 are not convertible, and the rewrite fails. **)
Section Eq.
Variable A:SType.
Definition eq : A->A->prop := fun x y:A => forall Q:A->A->prop, Q x y -> Q y x.
End Eq.
Infix = 502 := eq.
Parameter Power : set->set.
Definition f1 : set -> set := fun x => Power (Power x).
Definition f2 : set -> set := fun x => f1 (f1 x).
Definition f3 : set -> set := fun x => f2 (f2 x).
Definition f4 : set -> set := fun x => f3 (f3 x).
Definition f5 : set -> set := fun x => f4 (f4 x).
Definition f6 : set -> set := fun x => f5 (f5 x).
Definition g : set -> set := fun x => f5 (f5 (Power x)).
Theorem almost : forall P:set -> set -> prop, forall a b:set, g a = a -> P (f6 b) a -> P (f6 b) (f6 a).
let P a b. assume H: g a = a. assume H2: P (f6 b) a.
rewrite H.
exact H2.
Qed.
