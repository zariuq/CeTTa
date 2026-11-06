Axiom Assumption : forall P:prop, P.
Theorem FromAssumption : forall P:prop, P -> forall Q:prop, Q.
let P. assume HP. exact Assumption.
Qed.
