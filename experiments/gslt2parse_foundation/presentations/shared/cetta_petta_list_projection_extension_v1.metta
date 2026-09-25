; CeTTa's list projection for the PeTTa reader: a list node is a list value and its
; rest node closes a list pattern; the token, variable and dollar symbol inside a list
; project like the ones outside.  Composed over CeTTaPeTTaAtomProjectionV1.
(gslt-presentation-v1 CeTTaPeTTaListProjectionExtensionV1
  (signature
    (operator sexpr-list-rule 6))
  (equations)
  (rewrites
    (rule sexpr-list-rule-cetta-petta-list
      (head
        (sexpr-list-rule
          cetta-petta-v1 list list-rest list-token list-variable
          list-dollar-symbol))
      (body))
  ))
