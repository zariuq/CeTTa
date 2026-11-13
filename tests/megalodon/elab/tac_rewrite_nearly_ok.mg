(** A rewrite that passes a nearly equal term before the one it rewrites.
    f1 .. f6 each apply the one before twice; g is f6 with one more Power at
    the bottom, so g and f6 differ only after every unfolding.  The claim
    holds "f6 b" to the left of "g a": the rewrite first asks whether g and
    f6 are convertible, is told no, and goes on to its occurrence. **)
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
Theorem almost : forall P:set -> set -> prop, forall a b:set, g a = a -> P (f6 b) a -> P (f6 b) (g a).
let P a b. assume H: g a = a. assume H2: P (f6 b) a.
rewrite H.
exact H2.
Qed.
