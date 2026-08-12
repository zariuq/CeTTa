Definition True : prop := forall p:prop, p -> p.
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

Theorem test_let : forall A B:prop, A -> B -> A /\ B.
let A B.
assume HA: A.
assume HB: B.
exact (fun p H => H HA HB).
Qed.

Theorem test_assume : forall A:prop, A -> A.
let A.
assume H: A.
exact H.
Qed.

Theorem andI : forall A B:prop, A -> B -> A /\ B.
exact (fun A B a b p H => H a b).
Qed.

Theorem test_apply : forall A B:prop, A -> B -> A /\ B.
let A B.
assume HA HB.
apply andI.
- exact HA.
- exact HB.
Qed.

Theorem test_prove : forall A:prop, A -> A.
let A.
assume H: A.
prove A.
exact H.
Qed.

Theorem test_claim : forall A:prop, A -> A /\ A.
let A.
assume H: A.
claim L: A.
{ exact H. }
apply andI.
- exact L.
- exact L.
Qed.

Theorem test_rewrite : forall x y:prop, x = y -> (x -> y).
let x y.
assume Heq: x = y.
assume Hx: x.
rewrite <- Heq.
exact Hx.
Qed.

Section Ex.
Variable A:SType.
Definition ex : (A->prop)->prop := fun Q:A->prop => forall P:prop, (forall x:A, Q x -> P) -> P.
End Ex.
Binder+ exists , := ex.

Theorem test_witness : forall P:prop->prop, P True -> exists x:prop, P x.
let P.
assume H: P True.
witness True.
exact H.
Qed.

Theorem test_reflexivity : forall x:prop, x = x.
let x.
reflexivity.
Qed.

Theorem test_symmetry : forall x y:prop, x = y -> y = x.
let x y.
assume H: x = y.
symmetry.
exact H.
Qed.
