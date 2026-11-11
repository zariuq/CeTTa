Parameter wn : prop -> prop.
Parameter wa : prop -> prop -> prop.
Parameter wtru : prop.
Parameter cv : set -> set -> prop.
Parameter wceq : (set -> prop) -> (set -> prop) -> prop.
Definition wor : prop -> prop -> prop := fun p q => wi (wn p) q.
Theorem T1 : wi (wa wtru wtru) wtru.
admit.
Qed.
Theorem T2 : wal (fun x => wi (cv x x) (wor (wn (cv x x)) wtru)).
admit.
Qed.
Theorem T3 : forall x, wal (fun y => wceq (cv x) (cv y)).
admit.
Qed.
