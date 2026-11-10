(** Goals for typed and basic megaauto over the August 2022 Egal preamble, given with -I. *)
Theorem tma_lam_Pi : forall X:set, (fun x :e X => x) :e Pi_ x :e X, X.
exact ?t:*?.
Qed.
Theorem tma_pair_setprod : forall X Y:set, forall x :e X, forall y :e Y, (x,y) :e X :*: Y.
exact ?t:*?.
Qed.
Theorem tma_pair_Sigma : forall X:set, forall x :e X, forall y :e X, (x,y) :e Sigma_ x :e X, X.
exact ?t:*?.
Qed.
Theorem tma_setsum0 : forall X Y x:set, x :e X -> (0,x) :e X :+: Y.
exact ?t:*?.
Qed.
Theorem tma_setsum1 : forall X Y y:set, y :e Y -> (1,y) :e X :+: Y.
exact ?t:*?.
Qed.
Theorem tma_proj0 : forall x y:set, (x,y) 0 = x.
exact ?t:*?.
Qed.
Theorem tma_proj1 : forall x y:set, (x,y) 1 = y.
exact ?t:*?.
Qed.
Theorem tma_zero_in_two : 0 :e 2.
exact ?t:*?.
Qed.
Theorem tma_one_in_two : 1 :e 2.
exact ?t:*?.
Qed.
Theorem tma_beta_id : forall X:set, forall x :e X, (fun x :e X => x) x = x.
exact ?t:*?.
Qed.
Theorem tma_ap0_prod : forall X Y z:set, z :e X :*: Y -> z 0 :e X.
exact ?t:*?.
Qed.
Theorem tma_ap1_prod : forall X Y z:set, z :e X :*: Y -> z 1 :e Y.
exact ?t:*?.
Qed.
Theorem tma_ap_Pi : forall X Y:set, forall f :e (Pi_ x :e X, Y), forall x :e X, f x :e Y.
exact ?t:*?.
Qed.
Theorem tma_exp : forall X Y:set, forall y :e Y, (fun x :e X => y) :e Y :^: X.
exact ?t:*?.
Qed.
Theorem tma_lam_ext : forall X:set, (fun x :e X => (x,x) 0) = (fun x :e X => x).
exact ?t:*?.
Qed.
Theorem tma_Pi_cod : forall X:set, (Pi_ x :e X, (x,x) 0) = (Pi_ x :e X, x).
exact ?t:*?.
Qed.
Theorem tma_conv_lam : forall X Y:set, (fun z :e (X,Y) 0 => z) :e Pi_ z :e X, X.
exact ?t:*?.
Qed.
Theorem tma_trans : forall x y z:set, ((x,y) 0, z) 0 = x.
exact ?t:*?.
Qed.
Theorem tma_sym : forall x y:set, x = (x,y) 0.
exact ?t:*?.
Qed.
Theorem tma_imp : forall X:set, forall x:set, x :e X -> x :e X.
exact ?t:*?.
Qed.
Theorem tma_small : forall X:set, (fun x :e X => x) :e Pi_ x :e X, X.
exact ?t:+?.
Qed.
Theorem tma_from_long : forall X Y:set, (fun z :e (X,Y) 0 => z) :e Pi_ z :e X, X.
exact ?t:*0000000000000001?.
Qed.
Theorem tma_basic_star : forall A B:prop, A -> B -> A.
exact ?*?.
Qed.
Theorem tma_basic_small : forall A B:prop, A -> B -> A.
exact ?+?.
Qed.
