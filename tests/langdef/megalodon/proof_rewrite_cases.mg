(** rewrite (both directions, at positions) and cases. *)
Theorem t4 : x = y -> P x -> P y.
assume H1 H2.
rewrite <- H1.
rewrite H1 at 2.
rewrite <- H1 at 1 3.
cases H2: a: A, b: B | c: C.
exact a.
exact c.
Qed.
