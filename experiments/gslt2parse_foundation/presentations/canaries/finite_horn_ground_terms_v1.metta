; Generic finite-Horn carrier canary.  It distinguishes strings, symbols, and
; arbitrary integers while exercising escaped Unicode and embedded U+0000.

(gslt-presentation-v1 FiniteHornGroundTermsV1
  (signature
    (operator string-fact 1)
    (operator string-copy 1)
    (operator integer-fact 1))
  (equations)
  (rewrites
    (rule string-fact-text
      (head (string-fact "same"))
      (body))
    (rule string-fact-symbol
      (head (string-fact same))
      (body))
    (rule string-fact-control
      (head (string-fact "\u03bb\n\b\f\t\r\"\\\u001f"))
      (body))
    (rule string-fact-nul
      (head (string-fact "\u0000"))
      (body))
    (rule string-copy-value
      (head (string-copy ?value))
      (body (string-fact ?value)))
    (rule integer-fact-positive
      (head (integer-fact 123456789012345678901234567890))
      (body))
    (rule integer-fact-negative
      (head (integer-fact -123456789012345678901234567890))
      (body))))
