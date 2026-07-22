; CeTTa host projection for the composed PeTTa document and form readers.
; Recognition is owned by the two PeTTa syntax presentations.  This layer
; records the language-specific Atom projection and document-result encoding.
(gslt-presentation-v1 CeTTaPeTTaAtomProjectionV1
  (signature
    (operator sexpr-atom-profile 8)
    (operator sexpr-atom-rule 4)
    (operator sexpr-variable-rule 4)
    (operator sexpr-wrapper-map 4)
    (operator sexpr-codepoint-map 2)
    (operator sexpr-projection-cons 2)
    (operator sexpr-document-rule 3))
  (equations)
  (rewrites
    (rule sexpr-atom-profile-cetta-petta-v1
      (head
        (sexpr-atom-profile
          cetta-petta-v1 node pair cons nil cp form expression))
      (body))

    (rule sexpr-atom-rule-cetta-petta-token
      (head
        (sexpr-atom-rule
          cetta-petta-v1 token symbol swi-number-bool))
      (body))
    (rule sexpr-atom-rule-cetta-petta-string
      (head
        (sexpr-atom-rule cetta-petta-v1 string string decoded))
      (body))
    (rule sexpr-atom-rule-cetta-petta-quoted-token
      (head
        (sexpr-atom-rule
          cetta-petta-v1 quoted-token string retain-backslashes))
      (body))
    (rule sexpr-atom-rule-cetta-petta-dollar
      (head
        (sexpr-atom-rule cetta-petta-v1 dollar-symbol symbol raw))
      (body))
    (rule sexpr-variable-rule-cetta-petta-variable
      (head
        (sexpr-variable-rule
          cetta-petta-v1 variable raw underscore-fresh))
      (body))

    (rule sexpr-wrapper-map-cetta-petta-string-escape
      (head
        (sexpr-wrapper-map
          cetta-petta-v1 escape identity
          (sexpr-projection-cons
            (sexpr-codepoint-map 110 10)
            (sexpr-projection-cons
              (sexpr-codepoint-map 116 9)
              (sexpr-projection-cons
                (sexpr-codepoint-map 114 13)
                sexpr-projection-nil)))))
      (body))

    (rule sexpr-document-rule-cetta-petta-form
      (head (sexpr-document-rule cetta-petta-v1 form value))
      (body))
    (rule sexpr-document-rule-cetta-petta-runnable
      (head (sexpr-document-rule cetta-petta-v1 runnable bang-prefix))
      (body))
  ))
