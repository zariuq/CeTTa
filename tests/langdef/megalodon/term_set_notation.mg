(** Set notation: replacement with and without a condition, separation,
    enumeration, the empty enumeration and set lambdas. *)
Notation Repl Repl.
Notation ReplSep ReplSep.
Notation Sep Sep.
Notation SetEnum UPair.
Notation SetEnum0 Empty.
Notation SetEnum1 Sing.
Notation SetEnum2 UPair.
Notation SetLam lam.
Axiom h1 : {F x | x :e X}.
Axiom h2 : {F x | x :e X, p x}.
Axiom h3 : {x :e X | p x}.
Axiom h4 : {a, b, c}.
Axiom h5 : {}.
Axiom h6 : fun x :e X => F x.
