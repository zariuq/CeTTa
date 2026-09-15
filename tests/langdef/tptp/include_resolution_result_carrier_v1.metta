!(import! &self lib_tptp)

(= (test:tptp-include-result-admitted
      (TPTP:StageValue include-resolution-result $expected)
      $expected)
   True)

(= (test:tptp-include-result-rejected
      (TPTP:StageRejected include-resolution-result $_reason $_message))
   True)

!(bind! &empty-success
   (tptp-include-result:resolution-ok
     (tptp-include-result:resolved-document
       "root.p" "root-digest"
       (tptp-include-result:resolved-formulas-nil)
       (tptp-include-result:include-edges-nil))))

!(bind! &selection-error
   (tptp-include-result:resolution-error
     (tptp-include-result:error-missing-selection-name
       "library/leaf.p" "wanted")))

!(bind! &malformed-success
   (tptp-include-result:resolution-ok
     (tptp-include-result:resolved-document
       "root.p" "root-digest"
       (tptp-include-result:resolved-formulas-cons
         (tptp-include-result:resolved-formula
           "bad"
           (not-an-official-tptp-input)
           (tptp-include-result:formula-origin
             "root.p" "root-digest" 0
             (tptp-include-result:include-edges-nil)))
         (tptp-include-result:resolved-formulas-nil))
       (tptp-include-result:include-edges-nil))))

!(assertEqual
   (test:tptp-include-result-admitted
     (tptp:admit-include-resolution-result &empty-success)
     &empty-success)
   True)

!(assertEqual
   (test:tptp-include-result-admitted
     (tptp:admit-include-resolution-result &selection-error)
     &selection-error)
   True)

!(assertEqual
   (test:tptp-include-result-rejected
     (tptp:admit-include-resolution-result &malformed-success))
   True)

!(assertEqual
   (test:tptp-include-result-rejected
     (tptp:admit-include-resolution-result
       (tptp-include-result:resolution-not-declared &empty-success)))
   True)

!(println! (TptpIncludeResolutionResultCarrierV1Summary 4 4 0))
