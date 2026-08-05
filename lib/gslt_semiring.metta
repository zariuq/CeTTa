; Canonical algebraic specification for query connectives and additive spaces.
; Laws are observed as answer bags: declaration order is not semantic, while
; multiplicity is.  The provenance fields reference the sealed Lean results;
; this file does not duplicate their proofs.

(gslt semiring
  (sort Elem)
  (op otimes 2)
  (op one 0)
  (op oplus 2)
  (op zero 0)
  (law assoc-otimes
    (= (otimes (otimes $a $b) $c)
       (otimes $a (otimes $b $c)))
    (obs bag))
  (law unit-otimes-left
    (= (otimes one $a) $a)
    (obs bag))
  (law unit-otimes-right
    (= (otimes $a one) $a)
    (obs bag))
  (law assoc-oplus
    (= (oplus (oplus $a $b) $c)
       (oplus $a (oplus $b $c)))
    (obs bag))
  (law comm-oplus
    (= (oplus $a $b) (oplus $b $a))
    (obs bag))
  (law unit-oplus-left
    (= (oplus zero $a) $a)
    (obs bag))
  (law annihilate-left
    (= (otimes zero $a) zero)
    (obs bag))
  (law annihilate-right
    (= (otimes $a zero) zero)
    (obs bag))
  (law distrib-left
    (= (otimes $a (oplus $b $c))
       (oplus (otimes $a $b) (otimes $a $c)))
    (obs bag))
  (law distrib-right
    (= (otimes (oplus $a $b) $c)
       (oplus (otimes $a $c) (otimes $b $c)))
    (obs bag)))

(interpretation query-connectives (of semiring)
  (profiles he-extended prime petta-extended)
  (map otimes ,)
  (map one (,))
  (map oplus |)
  (map zero (|))
  (provenance unit-otimes-left
    "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.conj_unit_left")
  (provenance distrib-left
    "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.conj_disj_distrib_left")
  (provenance fold
    "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.foldConj_eq_meet"))

(interpretation space-additive (of semiring)
  (map oplus space-union)
  (provenance oplus
    "Lean:Mettapedia.GSLT.Dynamics.SpaceQueryAlgebra.weightedAnswers_union"))
