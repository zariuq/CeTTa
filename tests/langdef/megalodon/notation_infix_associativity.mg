(** Infix operators: left, right and non-associative, read by priority. *)
Infix + 400 left := add.
Infix * 300 right := mul.
Infix <= 600 := le.
Axiom a1 : x + y + z.
Axiom a2 : x * y * z.
Axiom a3 : x + y * z + w.
Axiom a4 : x + y <= z * w.
Axiom a5 : (x + y) * z.
