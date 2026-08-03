; Alpha-equivalent elements collapse, while distinct variable-sharing
; partitions remain distinct.
!(test (=alpha
    (alpha-unique-atom
      ((pair $x $x) (pair $y $y) (pair $u $v)))
    ((pair $a $a) (pair $b $c)))
  True)

; The ordinary predicate uses exact Prolog variable identity.
!(test (=alpha
    (unique-atom ((tag $x) (tag $y) (tag $x)))
    ((tag $a) (tag $b)))
  True)

; The alpha-aware predicate deliberately collapses those variants.
!(test (=alpha
    (alpha-unique-atom ((tag $x) (tag $y) (tag $x)))
    ((tag $a)))
  True)

; First occurrence and ground ordering are stable.
!(test (=alpha
    (alpha-unique-atom (a b a c b d))
    (a b c d))
  True)

; Different constructors are not collapsed merely because their variables
; have the same shape.
!(test (=alpha
    (alpha-unique-atom ((left $x) (right $y) (left $z)))
    ((left $a) (right $b)))
  True)

; The input is evaluated once before stable deduplication.
!(test (=alpha
    (alpha-unique-atom
      (append ((computed $x)) ((computed $y))))
    ((computed $a)))
  True)
