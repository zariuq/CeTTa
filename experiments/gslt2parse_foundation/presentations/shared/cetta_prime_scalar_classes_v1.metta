; Scalarized character relations for the current CeTTa reader profile.
; Input decoding guarantees that every item presented to a class is a Unicode
; scalar. GroundRelationsV1 supplies exact structural disequality.

(gslt-presentation-v1 CeTTaPrimeScalarClassesV1
  (signature)
  (equations)
  (rewrites
    (rule member-cetta-prime-whitespace-scalar-9
      (head (member cetta-prime-whitespace-scalar (cp 9)))
      (body))
    (rule member-cetta-prime-whitespace-scalar-10
      (head (member cetta-prime-whitespace-scalar (cp 10)))
      (body))
    (rule member-cetta-prime-whitespace-scalar-11
      (head (member cetta-prime-whitespace-scalar (cp 11)))
      (body))
    (rule member-cetta-prime-whitespace-scalar-12
      (head (member cetta-prime-whitespace-scalar (cp 12)))
      (body))
    (rule member-cetta-prime-whitespace-scalar-13
      (head (member cetta-prime-whitespace-scalar (cp 13)))
      (body))
    (rule member-cetta-prime-whitespace-scalar-32
      (head (member cetta-prime-whitespace-scalar (cp 32)))
      (body))
    (rule member-cetta-prime-newline-scalar-10
      (head (member cetta-prime-newline-scalar (cp 10)))
      (body))

    (rule member-cetta-prime-non-newline-scalar
      (head (member cetta-prime-non-newline-scalar ?codepoint))
      (body
        (different ?codepoint (cp 10))))

    (rule member-cetta-prime-string-escape-scalar
      (head (member cetta-prime-string-escape-scalar ?codepoint))
      (body))

    (rule member-cetta-prime-string-plain-scalar
      (head (member cetta-prime-string-plain-scalar ?codepoint))
      (body
        (different ?codepoint (cp 34))
        (different ?codepoint (cp 92))))

    (rule member-cetta-prime-token-scalar
      (head (member cetta-prime-token-scalar ?codepoint))
      (body
        (different ?codepoint (cp 9))
        (different ?codepoint (cp 10))
        (different ?codepoint (cp 11))
        (different ?codepoint (cp 12))
        (different ?codepoint (cp 13))
        (different ?codepoint (cp 32))
        (different ?codepoint (cp 34))
        (different ?codepoint (cp 40))
        (different ?codepoint (cp 41))
        (different ?codepoint (cp 59))))

    (rule member-cetta-prime-token-start-nonat-scalar
      (head (member cetta-prime-token-start-nonat-scalar ?codepoint))
      (body
        (different ?codepoint (cp 9))
        (different ?codepoint (cp 10))
        (different ?codepoint (cp 11))
        (different ?codepoint (cp 12))
        (different ?codepoint (cp 13))
        (different ?codepoint (cp 32))
        (different ?codepoint (cp 34))
        (different ?codepoint (cp 40))
        (different ?codepoint (cp 41))
        (different ?codepoint (cp 59))
        (different ?codepoint (cp 64))))

    (rule member-cetta-prime-token-start-plain-scalar
      (head (member cetta-prime-token-start-plain-scalar ?codepoint))
      (body
        (different ?codepoint (cp 9))
        (different ?codepoint (cp 10))
        (different ?codepoint (cp 11))
        (different ?codepoint (cp 12))
        (different ?codepoint (cp 13))
        (different ?codepoint (cp 32))
        (different ?codepoint (cp 34))
        (different ?codepoint (cp 36))
        (different ?codepoint (cp 38))
        (different ?codepoint (cp 40))
        (different ?codepoint (cp 41))
        (different ?codepoint (cp 42))
        (different ?codepoint (cp 59))
        (different ?codepoint (cp 64))))

    (rule member-cetta-prime-star-payload-start-scalar
      (head (member cetta-prime-star-payload-start-scalar ?codepoint))
      (body
        (different ?codepoint (cp 0))
        (different ?codepoint (cp 9))
        (different ?codepoint (cp 10))
        (different ?codepoint (cp 11))
        (different ?codepoint (cp 12))
        (different ?codepoint (cp 13))
        (different ?codepoint (cp 32))
        (different ?codepoint (cp 41))
        (different ?codepoint (cp 59))))
  ))
