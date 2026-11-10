Section Eq.
Variable A:SType.
Definition eq : A->A->prop := fun x y:A => forall Q:A->A->prop, Q x y -> Q y x.
End Eq.
Infix = 502 := eq.
Section Ex.
Variable A:SType.
Definition ex : (A->prop)->prop := fun Q:A->prop => forall P:prop, (forall x:A, Q x -> P) -> P.
End Ex.
Binder+ exists , := ex.
Parameter In:set->set->prop.
Parameter Empty : set.
Parameter Power : set->set.
Parameter Repl : set -> (set -> set) -> set.
Notation Repl Repl.
Definition and : prop -> prop -> prop := fun A B:prop => forall P:prop, (A -> B -> P) -> P.
Infix /\ 780 left := and.
Definition or : prop -> prop -> prop := fun A B:prop => forall P:prop, (A -> P) -> (B -> P) -> P.
Infix \/ 785 left := or.
Definition not : prop -> prop := fun A:prop => A -> forall P:prop, P.
Prefix ~ 700 := not.
Definition Subq : set -> set -> prop := fun A B => forall x :e A, x :e B.
Definition Sep : set -> (set -> prop) -> set := fun X P => Repl X (fun x => x).
Notation Sep Sep.
Definition ordsucc : set -> set := fun x => Power x.
Notation Nat Empty ordsucc.
Definition If_i : prop -> set -> set -> set := fun p x y => x.
Notation IfThenElse If_i.
Definition lam : set -> (set -> set) -> set := fun A F => Repl A F.
Notation SetLam lam.
Definition UPair : set -> set -> set := fun x y => Power x.
Definition Sing : set -> set := fun x => UPair x x.
Definition binunion : set -> set -> set := fun x y => Power y.
Notation SetEnum Empty Sing UPair binunion.
Postfix + 410 := ordsucc.
Infix :/\: 340 left := binunion.
Binder+ exists_in , := fun X P => exists x, x :e X /\ P x.
Theorem T1 : forall x y :e Empty, exists z, z :e Power x /\ ~ (y = z).
admit.
Qed.
Theorem T2 : {x :e Power Empty | x = x} = {Power x|x :e Empty}.
admit.
Qed.
Theorem T3 : 3 :e {0, 1, 2, 3} \/ 2+ = (if 0 :e 1 then 2 else 3).
admit.
Qed.
Definition all_set : (set->prop)->prop := fun P => forall x, P x.
Binder+ all4 , := all_set.
Binder+ all5 , := all_set ; and.
Binder+ ex3 , := ex ; and.
Definition famun : set -> (set -> set) -> set := fun X F => Repl X F.
Binder famun_ , := famun.
Definition fset : (set->prop)->set := fun P => Empty.
Binder fset , := fset.
Theorem B2 : exists x y : set, exists z, x = z.
admit.
Qed.
Theorem B3 : ex3 x :e Empty, ex3 y z c= Power x, y = z.
admit.
Qed.
Theorem B4 : all4 x y, x = y.
admit.
Qed.
Theorem B5 : all5 x :e Empty, all5 y c= x, all5 z : set, y = z.
admit.
Qed.
Theorem B6 : famun_ x :e Empty, Power x = famun_ y c= Empty, Power y.
admit.
Qed.
Theorem B7 : fset x, x = x = fset y : set, y = y.
admit.
Qed.
Theorem B8 : forall x y :e Empty, forall z c= x, z = y.
admit.
Qed.
