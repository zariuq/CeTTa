; Both guards succeed on [0,1]: the second clause maps its names to the
; opposite input positions. A type-aware compiler must retain both answers.
(gslt-presentation-v1 AdmittedIntegerAlignmentV1
  (signature
    (operator AdmittedIntegerAlignmentV1 2)
    (operator bnf-v1:scalars-cons 2)
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2))
  (equations)
  (rewrites
    (rule increasing-original-positions
      (head (AdmittedIntegerAlignmentV1
        (bnf-v1:scalars-cons ?a (bnf-v1:scalars-cons ?b ?tail)) left))
      (body (ground-integer-less ?a ?b)))
    (rule not-less-swapped-positions
      (head (AdmittedIntegerAlignmentV1
        (bnf-v1:scalars-cons ?b (bnf-v1:scalars-cons ?a ?tail)) right))
      (body (ground-integer-not-less ?a ?b)))))
