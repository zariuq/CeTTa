(** Types: set, prop, arrows to the right, parentheses, SType. *)
Parameter f : set -> set -> set.
Parameter g : (set -> set) -> set.
Parameter h : set -> (set -> prop) -> prop.
Parameter k : (set -> prop) -> (prop -> set) -> set.
Definition id : set -> set := fun x => x.
Definition c : set := id x.
