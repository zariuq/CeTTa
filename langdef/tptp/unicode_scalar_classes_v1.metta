; Unicode-scalar character relations for TPTP comments.  Input decoding owns
; scalar validity; GroundRelationsV1 supplies structural disequality.  TPTP
; tokens remain governed by the narrower official lexical classes.

(gslt-presentation-v1 TptpUnicodeScalarClassesV1
  (signature)
  (equations)
  (rewrites
    (rule member-tptp-newline-scalar-10
      (head (member tptp-newline-scalar (cp 10)))
      (body))
    (rule member-tptp-non-newline-scalar
      (head (member tptp-non-newline-scalar ?codepoint))
      (body
        (different ?codepoint (cp 10))))
    (rule member-tptp-block-non-star-scalar
      (head (member tptp-block-non-star-scalar ?codepoint))
      (body
        (different ?codepoint (cp 42))))
    (rule member-tptp-block-non-slash-scalar
      (head (member tptp-block-non-slash-scalar ?codepoint))
      (body
        (different ?codepoint (cp 47))))))
