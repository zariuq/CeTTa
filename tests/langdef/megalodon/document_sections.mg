(** Sections with variables, hypotheses and local definitions, nested. *)
Section Outer.
Variable A:SType.
Variable x y:A.
Hypothesis H1 : x = y.
Let z : A := x.
Section Inner.
Variable P:A -> prop.
Hypothesis H2 : P x.
Theorem in1 : P z.
Admitted.
End Inner.
End Outer.
