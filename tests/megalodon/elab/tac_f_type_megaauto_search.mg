(** Typed megaauto: the set functions, pairs and sums of Egal as parameters with their identities, the two products defined over them, and the knowns megaauto recognizes. *)
Section Eq.
Variable A:SType.
Definition eq : A->A->prop := fun x y:A => forall Q:A->A->prop, Q x y -> Q y x.
End Eq.
Infix = 502 := eq.
Parameter In:set->set->prop.
Parameter Empty : set.
(* Parameter lam "93592da87a6f2da9f7eb0fbd449e0dc4730682572e0685b6a799ae16c236dcae" "8acbb2b5de4ab265344d713b111d82b0048c8a4bf732a67ad35f1054a4eb4642" *)
Parameter lam : set -> (set -> set) -> set.
(* Parameter ordsucc "9db634daee7fc36315ddda5f5f694934869921e9c5f55e8b25c91c0a07c5cbec" "65d8837d7b0172ae830bed36c8407fcd41b7d875033d2284eb2df245b42295a6" *)
Parameter ordsucc : set -> set.
Notation Nat Empty ordsucc.
(* Parameter If_i "8c8f550868df4fdc93407b979afa60092db4b1bb96087bc3c2f17fadf3f35cbf" "b8ff52f838d0ff97beb955ee0b26fad79602e1529f8a2854bda0ecd4193a8a3c" *)
Parameter If_i : prop -> set -> set -> set.
Notation IfThenElse If_i.
(* Parameter setsum "b260cb5327df5c1f762d4d3068ddb3c7cc96a9cccf7c89cee6abe113920d16f1" "afe37e3e9df1ab0b3a1c5daa589ec6a68c18bf14b3d81364ac41e1812672537a" *)
Parameter setsum : set -> set -> set.
Infix :+: 450 left := setsum.
(* Parameter Pi "8ab5fa18b3cb4b4b313a431cc37bdd987f036cc47f175379215f69af5977eb3b" "40f1c38f975e69e2ae664b3395551df5820b0ba6a93a228eba771fc2a4551293" *)
Parameter Pi : set -> (set -> set) -> set.
(* Parameter ap "58c1782da006f2fb2849c53d5d8425049fad551eb4f8025055d260f0c9e1fe40" "c7aaa670ef9b6f678b8cf10b13b2571e4dc1e6fde061d1f641a5fa6c30c09d46" *)
Parameter ap : set -> set -> set.
Notation SetImplicitOp ap.
Notation SetLam lam.
Binder+ Sigma_ , := lam.
Binder+ Pi_ , := Pi.
Definition setprod : set->set->set := fun X Y:set => Sigma_ x :e X, Y.
Infix :*: 440 left := setprod.
Definition setexp : set->set->set := fun X Y:set => Pi_ y :e Y, X.
Infix :^: 430 left := setexp.
Axiom In_0_2 : 0 :e 2.
Axiom In_1_2 : 1 :e 2.
Axiom tupleI0 : forall X Y x, x :e X -> (0,x) :e (X,Y).
Axiom tupleI1 : forall X Y y, y :e Y -> (1,y) :e (X,Y).
Axiom tuple0_setsum : (forall x0 x1 x2, x2 :e x0 -> lam 2 (fun x3 : set => If_i (x3 = 0) 0 x2) :e setsum x0 x1).
Axiom tuple1_setsum : (forall x0 x1 x2, x2 :e x1 -> lam 2 (fun x3 : set => If_i (x3 = 0) 1 x2) :e setsum x0 x1).
Section Tuples.
Variable x0 x1: set.
Axiom tuple_2_0_eq: (x0,x1) 0 = x0.
Axiom tuple_2_1_eq: (x0,x1) 1 = x1.
End Tuples.
Axiom lamI2 : forall X, forall F:set->set, forall x :e X, forall y :e F x, (x,y) :e fun x :e X => F x.
Axiom tuple_2_setprod : forall X:set, forall Y:set, forall x :e X, forall y :e Y, (x,y) :e X :*: Y.
Axiom ap0_Sigma : forall X:set, forall Y:set -> set, forall z:set, z :e (Sigma_ x :e X, Y x) -> (z 0) :e X.
Axiom ap1_Sigma : forall X:set, forall Y:set -> set, forall z:set, z :e (Sigma_ x :e X, Y x) -> (z 1) :e (Y (z 0)).
Axiom lam_ext: forall X, forall F G:set -> set, (forall x :e X, F x = G x) -> (fun x :e X => F x) = (fun x :e X => G x).
Axiom lam_Pi : forall X:set, forall Y:set -> set, forall F:set -> set,
 (forall x :e X, F x :e Y x) -> (fun x :e X => F x) :e (Pi_ x :e X, Y x).
Axiom ap_Pi : forall X:set, forall Y:set -> set, forall f:set, forall x:set, f :e (Pi_ x :e X, Y x) -> x :e X -> f x :e Y x.
Axiom beta : forall X:set, forall F:set -> set, forall x:set, x :e X -> (fun x :e X => F x) x = F x.
Axiom Pi_cod_ext : (forall x0, forall x1 x2 : set -> set, (forall x3, x3 :e x0 -> x1 x3 = x2 x3) -> Pi x0 x1 = Pi x0 x2).
Theorem tma_none : forall X:set, X :e X.
exact ?t:*?.
Qed.
