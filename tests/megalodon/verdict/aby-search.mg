Definition False : prop := forall p:prop, p.
Definition not : prop -> prop := fun A:prop => A -> False.
Prefix ~ 700 := not.
Definition and : prop -> prop -> prop := fun A B:prop => forall p:prop, (A -> B -> p) -> p.
Infix /\ 780 left := and.
Definition or : prop -> prop -> prop := fun A B:prop => forall p:prop, (A -> p) -> (B -> p) -> p.
Infix \/ 785 left := or.
Definition iff : prop -> prop -> prop := fun A B:prop => and (A -> B) (B -> A).
Infix <-> 805 := iff.
Section Eq.
Variable A:SType.
Definition eq : A->A->prop := fun x y:A => forall Q:A->A->prop, Q x y -> Q y x.
End Eq.
Infix = 502 := eq.
Theorem and_comm : forall A B:prop, A /\ B -> B /\ A.
aby.
Qed.
Theorem or_comm : forall A B:prop, A \/ B -> B \/ A.
aby.
Qed.
Theorem iff_trans : forall A B C:prop, (A <-> B) -> (B <-> C) -> (A <-> C).
aby.
Qed.
Theorem eq_trans : forall x y z:set, x = y -> y = z -> x = z.
aby.
Qed.
Theorem uses_hint : forall A B:prop, (A <-> B) -> B -> A.
aby iff_trans.
Qed.
Theorem peirce : forall A B:prop, ((A -> B) -> A) -> A.
aby.
Qed.
