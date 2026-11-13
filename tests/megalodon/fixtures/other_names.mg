(** A definition and a theorem, each stated a second time under another name. **)
Parameter Power : set->set.
Definition twice : set -> set := fun x => Power (Power x).
Definition again : set -> set := fun x => twice x.
Theorem pass : forall p:prop, p -> p.
let p. assume H: p. exact H.
Qed.
Theorem pass_again : forall q:prop, q -> q.
let q. assume H: q. exact H.
Qed.
