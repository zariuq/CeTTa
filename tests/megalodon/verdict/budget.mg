Theorem Small : forall P:prop, P -> P.
let P. assume HP. exact HP.
Qed.
Theorem Large : forall P:prop, P -> P -> P.
let P. assume HP HP2.
exact (fun H1:P => (fun H2:P => (fun H3:P => (fun H4:P => (fun H5:P => H5) H4) H3) H2) H1) HP.
Qed.
Theorem UsesLarge : forall Q R:prop, Q -> Q.
let Q R. assume HQ. exact Large Q HQ HQ.
Qed.
Theorem Independent : forall Q R:prop, Q -> R -> Q.
let Q R. assume HQ HR. exact HQ.
Qed.
