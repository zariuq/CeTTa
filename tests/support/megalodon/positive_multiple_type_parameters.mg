Section TwoTypes.
Variable A B : SType.

Definition keepFirst : A -> B -> A := fun x:A => fun y:B => x.

Theorem keepFirst_preserves :
 forall x:A, forall y:B, forall P:A->prop,
 P x -> P (keepFirst x y).
let x y P.
assume h.
exact h.
Qed.

End TwoTypes.

Section ThreeTypes.
Variable A B C : SType.

Definition composeThree : (B -> C) -> (A -> B) -> A -> C :=
 fun f:B->C => fun g:A->B => fun x:A => f (g x).

Theorem composeThree_preserves :
 forall f:B->C, forall g:A->B, forall x:A, forall P:C->prop,
 P (f (g x)) -> P (composeThree f g x).
let f g x P.
assume h.
exact h.
Qed.

End ThreeTypes.
