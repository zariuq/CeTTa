(** Document items. *)
(* Title "every item" *)
(* Author "A" *)
(* Salt "a salt" *)
(* Treasure "1JMr8Xw5V4Pzkw8XzQBpu9SEzQkwcK4ZD7" *)
(* ShowProofTerms *)
(* HideProofTerms *)
(* Parameter Eps_i "174b78e53fc239e8c2aab4ab5a996a27e3e5741e88070dad186e05fb13f275e5" *)
Parameter Eps_i : (set->prop)->set.
Axiom ax1 : forall P:set->prop, forall x:set, P x -> P (Eps_i P).
Definition d1 : set -> set := fun x => x.
Definition d2 := fun x:set => x.
Opaque d1 d2.
Transparent d2.
Theorem th1 : p.
Admitted.
Lemma le1 : p.
Admitted.
Example ex1 : p.
Admitted.
Fact fa1 : p.
Admitted.
Remark re1 : p.
Admitted.
Corollary co1 : p.
Admitted.
Proposition pr1 : p.
Admitted.
Property pp1 : p.
Admitted.
