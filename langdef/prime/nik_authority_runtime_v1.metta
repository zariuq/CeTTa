; Generated from the Lean-emitted Prime NIK authority catalog.
; Regenerate; do not edit this specialization by hand.
(gslt-presentation-v1 PrimeNikAuthorityRuntimeV1
  (signature
    (operator GProof 2)
    (operator GRuleInst 2)
    (operator LCons 2)
    (operator NIKAuthorityV1 4)
    (operator PApp 2)
    (operator PrCons 2)
    (operator nik-authority 1)
    (operator nik-check 3)
  )
  (equations)
  (rewrites
    (rule nik-authority-0
      (head (nik-authority (NIKAuthorityV1 DTT "prime.dtt.calibration" "1" "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe")))
      (body))
    (rule nik-rule-0-0
      (head (nik-check (NIKAuthorityV1 DTT "prime.dtt.calibration" "1" "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe") (PApp "T" (LCons (PApp "C" LNil) (LCons (PApp "Z" LNil) (LCons (PApp "N" LNil) LNil)))) (GProof (GRuleInst "z" LNil) PrNil)))
      (body))
    (rule nik-rule-0-1
      (head (nik-check (NIKAuthorityV1 DTT "prime.dtt.calibration" "1" "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe") (PApp "T" (LCons (PApp "C" LNil) (LCons (PApp "i" LNil) (LCons (PApp "F" (LCons (PApp "N" LNil) (LCons (PApp "N" LNil) LNil))) LNil)))) (GProof (GRuleInst "j" LNil) PrNil)))
      (body))
    (rule nik-rule-0-2
      (head (nik-check (NIKAuthorityV1 DTT "prime.dtt.calibration" "1" "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe") (PApp "T" (LCons (PApp "C" LNil) (LCons (PApp "@" (LCons ?a0_2_0 (LCons ?a0_2_1 LNil))) (LCons ?a0_2_3 LNil)))) (GProof (GRuleInst "k" (LCons ?a0_2_0 (LCons ?a0_2_1 (LCons ?a0_2_2 (LCons ?a0_2_3 LNil))))) (PrCons ?p0_2_0 (PrCons ?p0_2_1 PrNil)))))
      (body
        (nik-check (NIKAuthorityV1 DTT "prime.dtt.calibration" "1" "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe") (PApp "T" (LCons (PApp "C" LNil) (LCons ?a0_2_0 (LCons (PApp "F" (LCons ?a0_2_2 (LCons ?a0_2_3 LNil))) LNil)))) ?p0_2_0)
        (nik-check (NIKAuthorityV1 DTT "prime.dtt.calibration" "1" "fd25250f162952bb4bd227fb3406755e894cee1a5e53e486299dcf4fd8a4c2fe") (PApp "T" (LCons (PApp "C" LNil) (LCons ?a0_2_1 (LCons ?a0_2_2 LNil)))) ?p0_2_1)
      ))
    (rule nik-authority-1
      (head (nik-authority (NIKAuthorityV1 HOTG "prime.hotg.calibration" "1" "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c")))
      (body))
    (rule nik-rule-1-0
      (head (nik-check (NIKAuthorityV1 HOTG "prime.hotg.calibration" "1" "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c") (PApp "H" (LCons (PApp "I" (LCons ?a1_0_0 (LCons (PApp "U" (LCons ?a1_0_0 LNil)) LNil))) LNil)) (GProof (GRuleInst "q" (LCons ?a1_0_0 LNil)) PrNil)))
      (body))
    (rule nik-rule-1-1
      (head (nik-check (NIKAuthorityV1 HOTG "prime.hotg.calibration" "1" "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c") (PApp "H" (LCons (PApp "C" (LCons (PApp "U" (LCons ?a1_1_0 LNil)) LNil)) LNil)) (GProof (GRuleInst "r" (LCons ?a1_1_0 LNil)) PrNil)))
      (body))
    (rule nik-rule-1-2
      (head (nik-check (NIKAuthorityV1 HOTG "prime.hotg.calibration" "1" "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c") (PApp "H" (LCons (PApp "I" (LCons (PApp "W" (LCons ?a1_2_1 LNil)) (LCons ?a1_2_0 LNil))) LNil)) (GProof (GRuleInst "s" (LCons ?a1_2_0 (LCons ?a1_2_1 LNil))) (PrCons ?p1_2_0 (PrCons ?p1_2_1 PrNil)))))
      (body
        (nik-check (NIKAuthorityV1 HOTG "prime.hotg.calibration" "1" "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c") (PApp "H" (LCons (PApp "C" (LCons ?a1_2_0 LNil)) LNil)) ?p1_2_0)
        (nik-check (NIKAuthorityV1 HOTG "prime.hotg.calibration" "1" "c37fdb3dd1a4b820e7adbdf06c1ced2753ad59ec034b4504a86a71978687984c") (PApp "H" (LCons (PApp "I" (LCons ?a1_2_1 (LCons ?a1_2_0 LNil))) LNil)) ?p1_2_1)
      ))
  ))
