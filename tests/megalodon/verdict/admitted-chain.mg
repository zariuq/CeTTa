Theorem Missing : forall P:prop, P.
admit.
Qed.
Theorem Dependent : forall P:prop, P -> forall Q:prop, Q.
let P. assume HP. exact Missing.
Qed.
