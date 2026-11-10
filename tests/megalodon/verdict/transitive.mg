Axiom A : forall P:prop, P.
Theorem T1 : forall P Q:prop, P.
let P Q. exact A P.
Qed.
Theorem T2 : forall Q:prop, Q -> forall P:prop, P.
let Q. assume HQ. let P. exact T1 P Q.
Qed.
Theorem T3 : forall P:prop, P -> P.
let P. assume HP. exact HP.
Qed.
