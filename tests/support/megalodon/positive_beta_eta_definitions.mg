Definition eta_outer : (set -> set) -> set -> set := fun f => fun x => f x.
Definition eta_inner : (set -> set -> set) -> set -> set -> set := fun f => fun x => fun y => f x y.
Definition beta_keep : set -> set -> set := fun x => fun y => (fun z:set => x) y.
Definition twice_apply : (set -> set) -> set -> set := fun f => fun x => f (f x).

Theorem beta_keep_computes :
  forall x y:set, forall P:set -> prop, P x -> P (beta_keep x y).
let x y P.
assume h.
exact h.
Qed.
