; CeTTa host projection for the composed PeTTa document and form readers.
; Recognition is owned by the two PeTTa syntax presentations.  This layer
; records the language-specific Atom projection and document-result encoding.
(gslt-presentation-v1 CeTTaPeTTaAtomProjectionV1
  (signature
    (operator sexpr-atom-profile 8)
    (operator sexpr-string-body-codec 4)
    (operator sexpr-atom-rule 4)
    (operator sexpr-variable-rule 4)
    (operator sexpr-document-rule 3))
  (equations)
  (rewrites
    ; The grammar retains the body; this declared projection decodes complete
    ; scalar/byte escapes and preserves unknown escape spellings.
    (rule string-body-codec-cetta-petta
      (head (sexpr-string-body-codec cetta-petta-v1 string escape metta-literal-v1))
      (body))
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

    (rule sexpr-document-rule-cetta-petta-form
      (head (sexpr-document-rule cetta-petta-v1 form value))
      (body))
    (rule sexpr-document-rule-cetta-petta-runnable
      (head (sexpr-document-rule cetta-petta-v1 runnable bang-prefix))
      (body))
  ))
