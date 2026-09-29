; CeTTa's list projection for the HE reader: a list node is a list value and its
; rest node closes a list pattern; words and variables inside a list project like the
; ones outside.  Composed over CeTTaHEAtomProjectionV1.
(gslt-presentation-v1 CeTTaHEListProjectionExtensionV1
  (signature
    (operator sexpr-list-rule 5))
  (equations)
  (rewrites
    (rule sexpr-list-rule-cetta-he-list
      (head (sexpr-list-rule cetta-he-v1 list list-rest list-word list-variable))
      (body))
  ))
