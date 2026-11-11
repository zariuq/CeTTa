(** Proof terms with automation strings and fun proofs. *)
Theorem t9 : forall p:prop, p -> p.
exact fun p H => H.
Qed.
Theorem t10 : q.
exact "0a1b".
Qed.
Theorem t11 : r.
apply foo "".
Qed.
