(gslt-presentation-v1 IntegerNativeTypeCallsV1
  (signature
    (operator IntegerLiteralPairV1 1)
    (operator IntegerLiteralUseV1 1)
    (operator IntegerLiteralGapV1 1)
    (operator IntegerLiteralAbsentV1 1)
    (operator IntegerLiteralDuplicateV1 1)
    (operator IntegerLiteralFloatV1 1)
    (operator IntegerLiteralSymbolV1 1)
    (operator IntegerLiteralOverflowV1 1)
    (operator IntegerVariableCallV1 2)
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2)
    (operator ground-integer-gap 2)
    (operator ground-integer-no-gap 2))
  (equations)
  (rewrites
    (rule literal-pair-less
      (head (IntegerLiteralPairV1 LowerV1))
      (body (ground-integer-less 0 2)))
    (rule literal-pair-not-less
      (head (IntegerLiteralPairV1 NotLowerV1))
      (body (ground-integer-not-less 0 2)))
    (rule literal-use
      (head (IntegerLiteralUseV1 ?answer))
      (body (IntegerLiteralPairV1 ?answer)))
    (rule literal-gap
      (head (IntegerLiteralGapV1 GapV1))
      (body (ground-integer-gap 0 1)))
    (rule literal-no-gap
      (head (IntegerLiteralGapV1 NoGapV1))
      (body (ground-integer-no-gap 0 1)))
    (rule literal-absent
      (head (IntegerLiteralAbsentV1 AbsentV1))
      (body (ground-integer-less 2 0)))
    (rule literal-duplicate-first
      (head (IntegerLiteralDuplicateV1 SameV1))
      (body (ground-integer-less 0 2)))
    (rule literal-duplicate-second
      (head (IntegerLiteralDuplicateV1 SameV1))
      (body (ground-integer-less 0 2)))
    (rule literal-float
      (head (IntegerLiteralFloatV1 FloatV1))
      (body (ground-integer-less 0.0 2)))
    (rule literal-symbol
      (head (IntegerLiteralSymbolV1 SymbolV1))
      (body (ground-integer-less UnknownV1 2)))
    (rule literal-overflow
      (head (IntegerLiteralOverflowV1 OverflowV1))
      (body (ground-integer-gap 9223372036854775807 0)))
    (rule variable-call
      (head (IntegerVariableCallV1 ?input VariableV1))
      (body (ground-integer-less ?input 2)))))
