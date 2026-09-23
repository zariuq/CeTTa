Definition imported_identity : set -> set := fun x => x.
Definition imported_keep : set -> set -> set := fun x => fun y => x.
Definition imported_apply : (set -> set) -> set -> set := fun f => fun x => f x.
Definition imported_keep_alias : set -> set -> set := imported_keep.

Theorem imported_keep_computes :
  forall x y:set, forall P:set -> prop, P x -> P (imported_keep_alias x y).
let x y P.
assume h.
exact h.
Qed.

Theorem imported_apply_computes :
  forall x:set, forall P:set -> prop, P x -> P (imported_apply imported_identity x).
let x P.
assume h.
exact h.
Qed.
