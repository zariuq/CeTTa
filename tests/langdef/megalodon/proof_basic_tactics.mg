(** let, assume, apply, exact, claim, prove, set, witness. *)
Infix -> 800 right := imp.
Theorem t1 : forall p q:prop, p -> (p -> q) -> q.
let p q.
assume Hp: p.
assume Hpq.
claim L: p.
{ exact Hp. }
prove q.
apply Hpq.
exact L.
Qed.
Theorem t2 : forall x:set, P x.
let x:set.
set y := f x.
set z : set := g y.
witness y.
exact H y z.
Qed.
