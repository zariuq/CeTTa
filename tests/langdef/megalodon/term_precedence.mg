(** Priorities: application binds tightest, then the declared operators,
    then implication; parentheses override. *)
Infix /\ 780 left := and.
Infix \/ 785 left := or.
Infix = 502 := eq.
Infix + 360 right := add.
Prefix ~ 700 := not.
Axiom f1 : f x y = g (h z).
Axiom f2 : ~ p /\ q \/ r -> s.
Axiom f3 : x + y + z = (x + y) + z.
Axiom f4 : p -> q -> r.
Axiom f5 : (p -> q) -> r.
Axiom f6 : ~ (p \/ q) /\ ~ ~ r.
