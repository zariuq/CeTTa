; Conjunction unit laws, comma-meet of spaces, and the sourced multi-space join.
; (,) is the unit query (one success, no bindings); (, p) is p; (, &a &b) in
; space position is the space intersection (per-atom minimum multiplicity);
; space-union adds multiplicities; (| &a &b) in space position is the additive
; union (the same fold symbolically), with (| &a) = &a; the two-argument match form
; (match (, (&s1 p1) ... (&sn pn)) template) joins legs, each against its own
; named space.
!(bind! &a (new-space))
!(bind! &b (new-space))
!(add-atom &a (f 1))
!(add-atom &a (f 1))
!(add-atom &a (f 2))
!(add-atom &b (f 1))
!(add-atom &b (f 3))
!(bind! &kb (new-space))
!(bind! &lab (new-space))
!(add-atom &kb (edge a b))
!(add-atom &lab (label b hub))
!(match (, &a &b) (f $x) (Meet $x))
!(match (, (&kb (edge $x $y)) (&lab (label $y $w))) (Sourced $x $w))
!(match &kb (, (edge $x $y)) (One $x $y))
!(match &kb (,) unit-hit)
!(match (, &a) (f $x) (Single $x))
!(match (| &a &b) (f $x) (Union $x))
!(match (| &a) (f $x) (SingleU $x))
