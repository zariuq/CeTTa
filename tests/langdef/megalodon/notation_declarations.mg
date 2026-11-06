(** Notation declarations: replacement, numerals, set enumerations,
    separation, conditionals and set lambdas. *)
Notation Repl Repl.
Notation Nat Empty ordsucc.
Notation SetEnum UPair.
Notation SetEnum1 Sing.
Notation SetEnum2 UPair.
Notation Sep Sep.
Notation ReplSep ReplSep.
Notation IfThenElse If_i.
Notation SetLam lam.
Notation SetImplicitOp ap.
Axiom e1 : {F x | x :e X}.
Axiom e2 : 3.
Axiom e3 : {x :e X | p x}.
Axiom e4 : {a, b}.
Axiom e5 : {a}.
Axiom e6 : if p then x else y.
