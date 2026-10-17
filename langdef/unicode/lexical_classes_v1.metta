(gslt-presentation-v1 UnicodeLexicalClassesV1
  (signature
    (operator cp 1)
    (operator lexical-class-v1 3)
    (operator lexical-class-equality-v1 4)
    (operator digit-values-v1 4)
    (operator domain 1)
    (operator property 3)
    (operator range 2)
    (operator union 2)
    (operator intersection 2)
    (operator difference 2)
    (operator complement 2))
  (equations)
  (rewrites
    (rule scalar-class
      (head (lexical-class-v1 UnicodeScalar Scalars (domain Scalars)))
      (body))
    (rule unicode-decimal-class
      (head (lexical-class-v1 UnicodeDecimal Scalars
        (property Unicode16 "gc" "Nd")))
      (body))
    (rule regex-whitespace-class
      (head (lexical-class-v1 RegexWhitespace Scalars
        (property Unicode16 "White_Space" "Y")))
      (body))
    (rule regex-word-class
      (head (lexical-class-v1 RegexWord Scalars
        (union (property Unicode16 "Alphabetic" "Y")
          (union (property Unicode16 "gc" "M")
            (union (property Unicode16 "gc" "Nd")
              (union (property Unicode16 "gc" "Pc")
                (property Unicode16 "Join_Control" "Y")))))))
      (body))
    (rule ascii-decimal-class
      (head (lexical-class-v1 AsciiDecimal Scalars
        (intersection (property Unicode16 "gc" "Nd") (range (cp 0) (cp 127)))))
      (body))
    (rule ascii-hex-class
      (head (lexical-class-v1 AsciiHex Scalars (property Unicode16 "AHex" "Y")))
      (body))
    (rule ascii-hex-expansion
      (head (lexical-class-equality-v1 AsciiHexExpansion Scalars
        (property Unicode16 "AHex" "Y")
        (union (range (cp 48) (cp 57))
          (union (range (cp 65) (cp 70)) (range (cp 97) (cp 102))))))
      (body))
    (rule scalar-projection
      (head (lexical-class-equality-v1 ScalarProjection Scalars
        (domain Scalars)
        (difference (domain CodePoints) (range (cp 55296) (cp 57343)))))
      (body))
    (rule decimal-double-complement
      (head (lexical-class-equality-v1 DecimalDoubleComplement Scalars
        (property Unicode16 "gc" "Nd")
        (complement Scalars (complement Scalars (property Unicode16 "gc" "Nd")))))
      (body))
    (rule ascii-decimal-values
      (head (digit-values-v1 AsciiDecimal (cp 48) (cp 57) 0))
      (body))
    (rule ascii-hex-decimal-values
      (head (digit-values-v1 AsciiHex (cp 48) (cp 57) 0))
      (body))
    (rule ascii-hex-upper-values
      (head (digit-values-v1 AsciiHex (cp 65) (cp 70) 10))
      (body))
    (rule ascii-hex-lower-values
      (head (digit-values-v1 AsciiHex (cp 97) (cp 102) 10))
      (body))))
