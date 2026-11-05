(** Prefix and postfix operators. *)
Prefix ~ 700 := not.
Postfix ' 100 := tag.
Postfix '' 100 := ctag.
Infix + 400 left := add.
Axiom b1 : ~ p.
Axiom b2 : x '.
Axiom b3 : x ' ''.
Axiom b4 : ~ x + y '.
