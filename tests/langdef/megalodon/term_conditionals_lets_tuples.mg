(** if-then-else, let terms, let with a pattern, and tuples. *)
Notation IfThenElse If_i.
Infix = 502 := eq.
Axiom i1 : (if p then x else y) = z.
Axiom i2 : let x := y in x = y.
Axiom i3 : let x : set := y in x = y.
Axiom i4 : let [u, v] := w in u = v.
Axiom i5 : [a, b, c] = (a, b, c).
