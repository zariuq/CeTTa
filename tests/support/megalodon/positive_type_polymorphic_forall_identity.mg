Section PolyIdentity.
Variable A : SType.
Variable p : A -> prop.

Theorem poly_forall_identity : (forall x:A, p x) -> forall x:A, p x.
exact (fun hypothesis => fun x:A => hypothesis x).
Qed.

End PolyIdentity.
