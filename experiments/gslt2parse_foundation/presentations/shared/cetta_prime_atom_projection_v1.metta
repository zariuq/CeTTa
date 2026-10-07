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
    (operator sexpr-list-rule 6)
    (operator sexpr-list-prefix-rule 3)
    (operator sexpr-meta-rule 4)
    (operator sexpr-braces-rule 2)
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

    ; A list node is a list value; its rest node closes a list pattern.  The
    ; word, variables and prefix forms inside a list project like the ones
    ; outside: each list prefix form names the prefix form it mirrors.
    (rule sexpr-list-rule-cetta-prime-list
      (head
        (sexpr-list-rule
          cetta-prime-v1 list list-rest list-word list-variable
          list-anonymous-variable))
      (body))
    (rule sexpr-list-prefix-rule-cetta-prime-quote
      (head (sexpr-list-prefix-rule cetta-prime-v1 list-quote quote))
      (body))
    (rule sexpr-list-prefix-rule-cetta-prime-unquote
      (head (sexpr-list-prefix-rule cetta-prime-v1 list-unquote unquote))
      (body))
    (rule sexpr-list-prefix-rule-cetta-prime-structural-variable
      (head
        (sexpr-list-prefix-rule
          cetta-prime-v1 list-structural-variable structural-variable))
      (body))
    (rule sexpr-list-prefix-rule-cetta-prime-structural-reference
      (head
        (sexpr-list-prefix-rule
          cetta-prime-v1 list-structural-reference structural-reference))
      (body))

    ; A braces node {x y}, the set notation, is the Atom (BRACES x y), whose
    ; tag no source spells.  Its elements are read as an expression's.
    (rule sexpr-braces-rule-cetta-prime-braces
      (head (sexpr-braces-rule cetta-prime-v1 braces))
      (body))

    ; A meta-argument: a bracket list or a braces node TOUCHING a term's end,
    ; T[...] and T{...}, is the Atom (meta T [...]) or (meta T {...}); they
    ; apply left to right, and a prefix form binds tighter.  The reader gives
    ; no head a meaning.  Inside a touching bracket the binding mark is a
    ; word (T[k := v]); in a list after layout it is refused, so the
    ; one-space slip T [k := v] is caught.
    (rule sexpr-meta-rule-cetta-prime-meta
      (head (sexpr-meta-rule cetta-prime-v1 meta-list meta-braces
                             cetta-prime-binding-mark))
      (body))
  ))
