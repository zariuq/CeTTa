(* Parameter a "0000000000000000000000000000000000000000000000000000000000000021" "1000000000000000000000000000000000000000000000000000000000000021" *)
Parameter a : prop.

Theorem known_identity : a -> a.
exact (fun hypothesis => hypothesis).
Qed.

Theorem known_identity_reuse : a -> a.
exact (fun hypothesis => known_identity hypothesis).
Qed.
