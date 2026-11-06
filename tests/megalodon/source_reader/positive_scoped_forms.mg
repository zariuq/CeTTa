Section S.
Infix pp 400 left := add.
Axiom a : x pp y pp z.
End S.
Axiom b : x pp y.
Theorem t : forall x :e X, {y :e X | y = x} = {x}.
let x. assume H: x :e X.
admit.
rewrite <- foo at 1 2.
cases H: a: A, b: B | c: C.
Qed.
Infix /\ 780 left := and.
Binder+ exists , := ex; and.
Axiom c : exists x y :e X, x c= y /\ if p then q else r.
Axiom d : let [u, v] := w in [u, v] = (u, v).
Axiom e : \x41 "str" ?q? -1.5e3 _0.
