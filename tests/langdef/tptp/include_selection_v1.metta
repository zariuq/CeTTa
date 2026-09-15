!(import! &self lib_tptp)

(= (test:p0 $head)
   (PApp $head LNil))

(= (test:selection-value
      (TPTP:TransformationValueV1 $result $_receipt))
   $result)

(= (test:selection-receipt
      (TPTP:TransformationValueV1 $_result
        (TPTP:TransformationReceiptV1
          official-include-selection $_wire $_request $_target $_trace)))
   TptpOfficialIncludeSelectionV1ReceiptRetained)

(= (test:selection-ok $formulas)
   (tptp-include-selection:ok $formulas))

(= (test:official-input $index)
   (tptp92-ast:tptp-input:alt-1
     (tptp92-ast:annotated-formula:alt-4
       (tptp92-ast:fof-annotated:alt-1
         (tptp92-ast:name:alt-2
           (tptp92-ast:token:integer "0"))
         (tptp92-ast:formula-role:alt-1
           (tptp92-ast:token:lower-word "axiom"))
         (tptp92-ast:fof-formula:alt-2
           (tptp92-ast:fof-sequent:alt-1
             (tptp92-ast:fof-formula-tuple:alt-1)
             (tptp92-ast:gentzen-arrow:alt-1)
             (tptp92-ast:fof-formula-tuple:alt-1)))
         (tptp92-ast:annotations:alt-2)))
     (tptp92-ast:source-span $index $index)))

(= (test:formula-origin $index)
   (tptp-include-result:formula-origin
     "test.p" "test-digest" $index
     (tptp-include-result:include-edges-nil)))

(= (test:formula $name $index)
   (tptp-include-result:resolved-formula
     $name (test:official-input $index) (test:formula-origin $index)))

(= (test:equality-row $receipt $left $right $decision)
   (LangDef:RelationRow "PatternEqualityDecision" $receipt
     (test:p0 $left) (test:p0 $right) (test:p0 $decision)))

!(bind! &relations
   (LangDef:RelationRows
     (test:equality-row 1000 "a" "a" "pattern-equality-decision:equal")
     (test:equality-row 1001 "b" "b" "pattern-equality-decision:equal")
     (test:equality-row 1002 "b" "a" "pattern-equality-decision:different")
     (test:equality-row 1003 "a" "b" "pattern-equality-decision:different")
     (test:equality-row 1004 "c" "b" "pattern-equality-decision:different")
     (test:equality-row 1005 "c" "a" "pattern-equality-decision:different")
     (test:equality-row 1006 "z" "a" "pattern-equality-decision:different")
     (test:equality-row 1007 "z" "b" "pattern-equality-decision:different")
     (test:equality-row 1008 "z" "c" "pattern-equality-decision:different")
     (test:equality-row 1009 "z" "z" "pattern-equality-decision:equal")
     (test:equality-row 1010 "a" "z" "pattern-equality-decision:different")
     (test:equality-row 1011 "b" "z" "pattern-equality-decision:different")
     (test:equality-row 1012 "c" "z" "pattern-equality-decision:different")))

!(bind! &formula-a0 (test:formula "a" 0))
!(bind! &formula-a1 (test:formula "a" 1))
!(bind! &formula-b1 (test:formula "b" 1))
!(bind! &formula-c2 (test:formula "c" 2))

!(bind! &ordered-formulas
   (tptp-include-result:resolved-formulas-cons &formula-a0
     (tptp-include-result:resolved-formulas-cons &formula-b1
       (tptp-include-result:resolved-formulas-cons &formula-c2
         (tptp-include-result:resolved-formulas-nil)))))

!(bind! &source-order-request
   (tptp-include-selection:apply "leaf"
     (tptp-include-result:selection-named
       (tptp-include-result:strings-cons "b"
         (tptp-include-result:strings-cons "a"
           (tptp-include-result:strings-nil))))
     &ordered-formulas))

!(bind! &source-order-result
   (tptp:apply-include-selection
     64 1000000 &relations "leaf"
     (tptp-include-result:selection-named
       (tptp-include-result:strings-cons "b"
         (tptp-include-result:strings-cons "a"
           (tptp-include-result:strings-nil))))
     &ordered-formulas))

!(assertEqual
   (test:selection-value &source-order-result)
   (tptp-include-selection:ok
     (tptp-include-result:resolved-formulas-cons &formula-a0
       (tptp-include-result:resolved-formulas-cons &formula-b1
         (tptp-include-result:resolved-formulas-nil)))))

!(assertEqual
   (test:selection-receipt &source-order-result)
   TptpOfficialIncludeSelectionV1ReceiptRetained)

!(bind! &duplicate-request
   (tptp-include-selection:apply "leaf"
     (tptp-include-result:selection-named
       (tptp-include-result:strings-cons "a"
         (tptp-include-result:strings-cons "a"
           (tptp-include-result:strings-nil))))
     &ordered-formulas))

!(bind! &duplicate-result
   (tptp:include-selection-execution
     64 1000000 &relations &duplicate-request))

!(assertEqual
   (test:selection-value
     (tptp:require-single-stage-execution &duplicate-result))
   (tptp-include-selection:error
     (tptp-include-result:error-duplicate-selection-name "leaf" "a")))

!(bind! &missing-request
   (tptp-include-selection:apply "leaf"
     (tptp-include-result:selection-named
       (tptp-include-result:strings-cons "z"
         (tptp-include-result:strings-nil)))
     &ordered-formulas))

!(bind! &missing-result
   (tptp:include-selection-execution
     64 1000000 &relations &missing-request))

!(assertEqual
   (test:selection-value
     (tptp:require-single-stage-execution &missing-result))
   (tptp-include-selection:error
     (tptp-include-result:error-missing-selection-name "leaf" "z")))

!(bind! &ambiguous-formulas
   (tptp-include-result:resolved-formulas-cons &formula-a0
     (tptp-include-result:resolved-formulas-cons &formula-a1
       (tptp-include-result:resolved-formulas-nil))))

!(bind! &ambiguous-request
   (tptp-include-selection:apply "duplicates"
     (tptp-include-result:selection-named
       (tptp-include-result:strings-cons "a"
         (tptp-include-result:strings-nil)))
     &ambiguous-formulas))

!(bind! &ambiguous-result
   (tptp:include-selection-execution
     64 1000000 &relations &ambiguous-request))

!(assertEqual
   (test:selection-value
     (tptp:require-single-stage-execution &ambiguous-result))
   (tptp-include-selection:error
     (tptp-include-result:error-ambiguous-selection-name
       "duplicates" "a")))

!(bind! &explicit-all-request
   (tptp-include-selection:apply "duplicates"
     (tptp-include-result:selection-explicit-all)
     &ambiguous-formulas))

!(bind! &explicit-all-result
   (tptp:include-selection-execution
     1 1000000 &relations &explicit-all-request))

!(assertEqual
   (test:selection-value
     (tptp:require-single-stage-execution &explicit-all-result))
   (test:selection-ok &ambiguous-formulas))

!(println! (TptpOfficialIncludeSelectionV1Summary 6 6 0))
