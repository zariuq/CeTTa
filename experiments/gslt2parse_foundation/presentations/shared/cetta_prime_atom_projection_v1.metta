; CeTTa host projection for Prime's universal-name concrete syntax.
; Prefix recognition belongs to CeTTaPrimeReaderSyntaxV1; this presentation
; owns the mapping from its syntax nodes to Prime's canonical Atom forms.
(gslt-presentation-v1 CeTTaPrimeAtomProjectionV1
  (signature
    (operator sexpr-atom-profile 8)
    (operator sexpr-atom-rule 4)
    (operator sexpr-variable-rule 4)
    (operator sexpr-wrapper-map 4)
    (operator sexpr-prefix-rule 4)
    (operator sexpr-codepoint-map 2)
    (operator sexpr-projection-cons 2))
  (equations)
  (rewrites
    (rule sexpr-atom-profile-cetta-prime-v1
      (head
        (sexpr-atom-profile
          cetta-prime-v1 node pair cons nil cp file expression))
      (body))

    (rule sexpr-atom-rule-cetta-prime-word
      (head (sexpr-atom-rule cetta-prime-v1 word symbol host-token))
      (body))
    (rule sexpr-atom-rule-cetta-prime-string
      (head (sexpr-atom-rule cetta-prime-v1 string string decoded))
      (body))
    (rule sexpr-variable-rule-cetta-prime-variable
      (head (sexpr-variable-rule cetta-prime-v1 variable raw no-anonymous))
      (body))
    (rule sexpr-variable-rule-cetta-prime-anonymous-variable
      (head
        (sexpr-variable-rule
          cetta-prime-v1 anonymous-variable empty fresh-anonymous))
      (body))

    (rule sexpr-wrapper-map-cetta-prime-escape
      (head
        (sexpr-wrapper-map
          cetta-prime-v1 escape identity
          (sexpr-projection-cons
            (sexpr-codepoint-map 110 10)
            (sexpr-projection-cons
              (sexpr-codepoint-map 114 13)
              (sexpr-projection-cons
                (sexpr-codepoint-map 116 9)
                sexpr-projection-nil)))))
      (body))

    (rule sexpr-prefix-rule-cetta-prime-quote
      (head (sexpr-prefix-rule cetta-prime-v1 quote quote open))
      (body))
    (rule sexpr-prefix-rule-cetta-prime-unquote
      (head (sexpr-prefix-rule cetta-prime-v1 unquote unquote open))
      (body))
    (rule sexpr-prefix-rule-cetta-prime-structural-variable
      (head
        (sexpr-prefix-rule
          cetta-prime-v1 structural-variable named-variable closed))
      (body))
    (rule sexpr-prefix-rule-cetta-prime-structural-reference
      (head
        (sexpr-prefix-rule
          cetta-prime-v1 structural-reference resolve-name closed))
      (body))
  ))
