Section PolyIdentity.
Variable A : SType.
Variable p : A -> prop.

Theorem poly_forall_identity : (forall x:A, p x) -> forall x:A, p x.
exact (fun hypothesis => fun x:A => hypothesis x).
Qed.

End PolyIdentity.

Section PolyIdentityReuse.
Variable B : SType.
Variable q : B -> prop.

Theorem poly_forall_identity_reuse :
  (forall y:B, q y) -> forall y:B, q y.
exact (poly_forall_identity B q).
Qed.

End PolyIdentityReuse.
