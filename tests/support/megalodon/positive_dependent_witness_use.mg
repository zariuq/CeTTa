Definition and : prop -> prop -> prop := fun A B:prop =>
  forall r:prop, (A -> B -> r) -> r.

Section Witness.
Variable A:SType.
Definition ex : (A -> prop) -> prop := fun P:A->prop =>
  forall r:prop, (forall x:A, P x -> r) -> r.

Theorem split_witness : forall P Q:A->prop,
  ex (fun x:A => and (P x) (Q x)) -> and (ex P) (ex Q).
exact (fun P Q H r k => H r (fun x h =>
  k (fun s f => f x (h (P x) (fun px qx => px)))
    (fun s f => f x (h (Q x) (fun px qx => qx))))).
Qed.
End Witness.
