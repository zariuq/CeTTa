; Query-connective semiring: positive laws and a bag-level negative witness.
;
; The imported GSLT declares the algebra.  Comma is multiplication, pipe is
; addition, (,) is one, and (|) is zero.  Equalities are observed as answer
; bags, so order is irrelevant and multiplicity remains visible.
!(import! &self gslt_semiring)

; The specification is ordinary inspectable MeTTa data.
!(match &self
  (interpretation query-connectives (of semiring)
    $profiles $otimes $one $oplus $zero $unit-law $distrib-law $fold-law)
  (declared-query-semiring $profiles))

!(bind! &semiring-demo (new-space))
!(add-atom &semiring-demo (f a))
!(add-atom &semiring-demo (f a))
!(add-atom &semiring-demo (g a))
!(add-atom &semiring-demo (h a))

; Positive: left distributivity.  The two lines carry the same four-answer
; bag even though their traversal order is not part of the law.
!(match &semiring-demo
  (, (f $x) (| (g $x) (h $x)))
  (distributed-left $x))
!(match &semiring-demo
  (| (, (f $x) (g $x)) (, (f $x) (h $x)))
  (distributed-right $x))

; Positive: the multiplicative unit succeeds once without adding bindings.
!(match &semiring-demo (, (,) (g $x)) (unit $x))

; Negative witness: query addition is not idempotent under bag observation.
; The first query returns two copies; repeating the disjunct returns four.
; Thus `p | p = p` must never be admitted as a bag-level law.
!(match &semiring-demo (f $x) (single-reading $x))
!(match &semiring-demo (| (f $x) (f $x)) (twice $x))
