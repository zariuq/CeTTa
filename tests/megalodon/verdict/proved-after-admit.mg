Theorem L1 : forall P:prop, P -> P.
admit.
Qed.
Theorem L2 : forall P:prop, P -> P.
let P. assume HP. exact HP.
Qed.
Theorem U : forall Q R:prop, Q -> Q.
let Q R. exact L1 Q.
Qed.
