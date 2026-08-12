(* Parameter p "0000000000000000000000000000000000000000000000000000000000000011" "1000000000000000000000000000000000000000000000000000000000000011" *)
Parameter p : set -> prop.

Theorem forall_identity : (forall x:set, p x) -> forall x:set, p x.
exact (fun hypothesis => fun x:set => hypothesis x).
Qed.
