(** Bullets and braces. *)
Theorem t3 : p /\ q.
apply andI.
- exact Hp.
- apply foo.
  + exact Ha.
  + exact Hb.
    * exact Hc.
    * { exact Hd. }
Qed.
