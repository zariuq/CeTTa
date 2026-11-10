Parameter In:set->set->prop.
(* Parameter Sep "f7e63d81e8f98ac9bc7864e0b01f93952ef3b0cbf9777abab27bcbd743b6b079" "f336a4ec8d55185095e45a638507748bac5384e04e0c48d008e4f6a9653e9c44" *)
Parameter Sep : set -> (set -> prop) -> set.

Definition Members : set -> set := fun X => Sep X (fun f => f :e X).

(** Its identity is Sep's: the same object under another spelling. **)
Definition sep_alias : set -> (set -> prop) -> set := fun X P => Sep X P.

Theorem Members_unfold : forall X:set, forall Q:set -> prop, Q (Sep X (fun f => f :e X)) -> Q (Members X).
let X Q. assume H. exact H.
Qed.

Theorem alias_is_Sep : forall X:set, forall P:set -> prop, forall Q:set -> prop, Q (Sep X P) -> Q (sep_alias X P).
let X P Q. assume H. exact H.
Qed.
