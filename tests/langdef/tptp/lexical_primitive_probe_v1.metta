; Observe primitive control through authored rules. The runner's symbolic
; True/False constructors are not PeTTa's grounded Boolean values.
(gslt-presentation-v1 TptpLexicalPrimitiveProbeV1
  (signature
    (operator metta-equation 2) (operator < 2)
    (operator tptp-lex-test:compare 2) (operator tptp-lex-test:order 1))
  (equations)
  (rewrites
    (rule tptp-lex-test-compare
      (head (metta-equation (tptp-lex-test:compare ?left ?right)
        (tptp-lex-test:order (< ?left ?right)))) (body))
    (rule tptp-lex-test-less
      (head (metta-equation (tptp-lex-test:order True) "less")) (body))
    (rule tptp-lex-test-not-less
      (head (metta-equation (tptp-lex-test:order False) "not-less")) (body))))
