; Declarative host-Atom projection profiles for S-expression syntax trees.
; Concrete reader presentations remain the recognition authority; these facts
; describe the separate semantic projection from neutral ParserPack nodes.
(gslt-presentation-v1 SExprAtomProjectionV1
  (signature
    (operator sexpr-atom-profile 8)
    (operator sexpr-atom-rule 4)
    (operator sexpr-variable-rule 4)
    (operator sexpr-wrapper-map 4)
    (operator sexpr-wrapper-hex 6)
    (operator sexpr-codepoint-map 2)
    (operator sexpr-projection-cons 2))
  (equations)
  (rewrites
    (rule sexpr-atom-profile-he-v1
      (head
        (sexpr-atom-profile
          he-v1 node pair cons nil cp document expression))
      (body))

    (rule sexpr-atom-rule-he-word
      (head (sexpr-atom-rule he-v1 word symbol raw))
      (body))
    (rule sexpr-atom-rule-he-string
      (head (sexpr-atom-rule he-v1 string symbol decoded-quoted))
      (body))
    (rule sexpr-variable-rule-he-variable
      (head (sexpr-variable-rule he-v1 variable raw no-anonymous))
      (body))

    (rule sexpr-wrapper-map-he-simple-escape
      (head
        (sexpr-wrapper-map
          he-v1 simple-escape reject
          (sexpr-projection-cons
            (sexpr-codepoint-map 110 10)
            (sexpr-projection-cons
              (sexpr-codepoint-map 114 13)
              (sexpr-projection-cons
                (sexpr-codepoint-map 116 9)
                (sexpr-projection-cons
                  (sexpr-codepoint-map 34 34)
                  (sexpr-projection-cons
                    (sexpr-codepoint-map 39 39)
                    (sexpr-projection-cons
                      (sexpr-codepoint-map 92 92)
                      sexpr-projection-nil))))))))
      (body))
    (rule sexpr-wrapper-hex-he-byte-escape
      (head
        (sexpr-wrapper-hex
          he-v1 byte-escape 2 2 127 16))
      (body))
    (rule sexpr-wrapper-hex-he-unicode-escape
      (head
        (sexpr-wrapper-hex
          he-v1 unicode-escape 1 6 1114111 16))
      (body))
  ))
