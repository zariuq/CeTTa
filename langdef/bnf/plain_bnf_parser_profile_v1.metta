(GSLTParserProfileLayerV1
  "PlainBnfParserV1"
  "BnfDocument"
  (LCons (LexicalClassPoints "BnfNameCharClass"
    (LCons 45
    (LCons 48 (LCons 49 (LCons 50 (LCons 51 (LCons 52
    (LCons 53 (LCons 54 (LCons 55 (LCons 56 (LCons 57
    (LCons 65 (LCons 66 (LCons 67 (LCons 68 (LCons 69
    (LCons 70 (LCons 71 (LCons 72 (LCons 73 (LCons 74
    (LCons 75 (LCons 76 (LCons 77 (LCons 78 (LCons 79
    (LCons 80 (LCons 81 (LCons 82 (LCons 83 (LCons 84
    (LCons 85 (LCons 86 (LCons 87 (LCons 88 (LCons 89
    (LCons 90 (LCons 95
    (LCons 97 (LCons 98 (LCons 99 (LCons 100 (LCons 101
    (LCons 102 (LCons 103 (LCons 104 (LCons 105 (LCons 106
    (LCons 107 (LCons 108 (LCons 109 (LCons 110 (LCons 111
    (LCons 112 (LCons 113 (LCons 114 (LCons 115 (LCons 116
    (LCons 117 (LCons 118 (LCons 119 (LCons 120 (LCons 121
    (LCons 122 LNil))))))))))))))))))))))))))))))))))))))))
                  )))))))))))))))))))))))))
  (LCons (LexicalClassExcept "BnfLiteralScalarClass"
    (LCons 10 (LCons 13 (LCons 34 (LCons 92 LNil)))))
  (LCons (LexicalClassPoints "BnfHorizontalSpaceClass"
    (LCons 9 (LCons 32 LNil)))
  (LCons (LexicalClassExcept "BnfLineScalarClass"
    (LCons 10 (LCons 13 LNil)))
  LNil))))
  (LCons (LexicalState "BnfNameChar" "BnfNameCharClass"
            "bnf-source-v1:lex-name-char")
  (LCons (LexicalState "BnfLiteralScalar" "BnfLiteralScalarClass"
            "bnf-source-v1:lex-literal-scalar")
  (LCons (LexicalState "BnfHorizontalSpace" "BnfHorizontalSpaceClass"
            "bnf-source-v1:lex-horizontal-space")
  (LCons (LexicalState "BnfLineScalar" "BnfLineScalarClass"
            "bnf-source-v1:lex-line-scalar")
  LNil)))))
