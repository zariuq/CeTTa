(* Parameter p "0000000000000000000000000000000000000000000000000000000000000031" "1000000000000000000000000000000000000000000000000000000000000031" *)
Parameter p : prop.

Definition idp : prop -> prop := fun value : prop => value.

Theorem definition_identity : idp p -> p.
exact (fun hypothesis => hypothesis).
Qed.
