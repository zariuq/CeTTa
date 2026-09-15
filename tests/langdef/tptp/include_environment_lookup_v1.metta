!(import! &self lib_tptp)

(= (test:p0 $head)
   (PApp $head LNil))

(= (test:equality-row $receipt $left $right $decision)
   (LangDef:RelationRow "PatternEqualityDecision" $receipt
     (test:p0 $left) (test:p0 $right) (test:p0 $decision)))

(= (test:lookup-value
      (TPTP:TransformationValueV1 $result $_receipt))
   $result)

(= (test:lookup-receipt
      (TPTP:TransformationValueV1 $_result
        (TPTP:TransformationReceiptV1
          official-include-environment-lookup
          $_wire $_request $_target $_trace)))
   TptpOfficialIncludeEnvironmentLookupV1ReceiptRetained)

!(bind! &relations
   (LangDef:RelationRows
     (test:equality-row 2000 "A" "A" "pattern-equality-decision:equal")
     (test:equality-row 2001 "B" "A" "pattern-equality-decision:different")
     (test:equality-row 2002 "B" "B" "pattern-equality-decision:equal")
     (test:equality-row 2003 "missing" "A" "pattern-equality-decision:different")
     (test:equality-row 2004 "x.p" "x.p" "pattern-equality-decision:equal")
     (test:equality-row 2005 "missing.p" "x.p" "pattern-equality-decision:different")
     (test:equality-row 2006 "A" "B" "pattern-equality-decision:different")))

!(bind! &empty-file
   (tptp92-ast:tptp-file:alt-1
     (tptp92-ast:list:tptp92ast-tptp-input:nil)))

!(bind! &document-a
   (tptp-include-resolution:source-document
     "A" "digest-a" &empty-file))

!(bind! &document-a-duplicate
   (tptp-include-resolution:source-document
     "A" "digest-a-duplicate" &empty-file))

!(bind! &binding-ax
   (tptp-include-resolution:include-binding "A" "x.p" "AX"))

!(bind! &binding-bx
   (tptp-include-resolution:include-binding "B" "x.p" "BX"))

!(bind! &binding-ax-duplicate
   (tptp-include-resolution:include-binding "A" "x.p" "AX-duplicate"))

!(bind! &documents-one
   (tptp-include-resolution:source-documents-cons &document-a
     (tptp-include-resolution:source-documents-nil)))

!(bind! &documents-ambiguous
   (tptp-include-resolution:source-documents-cons &document-a
     (tptp-include-resolution:source-documents-cons
       &document-a-duplicate
       (tptp-include-resolution:source-documents-nil))))

!(bind! &bindings-parent-relative
   (tptp-include-resolution:include-bindings-cons &binding-ax
     (tptp-include-resolution:include-bindings-cons &binding-bx
       (tptp-include-resolution:include-bindings-nil))))

!(bind! &bindings-ambiguous
   (tptp-include-resolution:include-bindings-cons &binding-ax
     (tptp-include-resolution:include-bindings-cons
       &binding-ax-duplicate
       (tptp-include-resolution:include-bindings-nil))))

!(bind! &environment
   (tptp-include-resolution:source-environment
     &documents-one &bindings-parent-relative))

!(bind! &ambiguous-document-environment
   (tptp-include-resolution:source-environment
     &documents-ambiguous &bindings-parent-relative))

!(bind! &ambiguous-binding-environment
   (tptp-include-resolution:source-environment
     &documents-one &bindings-ambiguous))

!(bind! &document-result
   (tptp:lookup-include-document
     64 1000000 &relations "A" &environment))

!(assertEqual
   (test:lookup-value &document-result)
   (tptp-include-lookup:document-ok
     (tptp-include-resolution:source-document
       "A" "digest-a"
       (tptp92-ast:tptp-file:alt-1
         (tptp92-ast:list:tptp92ast-tptp-input:nil)))))

!(assertEqual
   (test:lookup-receipt &document-result)
   TptpOfficialIncludeEnvironmentLookupV1ReceiptRetained)

!(assertEqual
   (test:lookup-value
     (tptp:lookup-include-document
       64 1000000 &relations "missing" &environment))
   (tptp-include-lookup:document-error
     (tptp-include-result:error-missing-document "missing")))

!(assertEqual
   (test:lookup-value
     (tptp:lookup-include-document
       64 1000000 &relations "A"
       &ambiguous-document-environment))
   (tptp-include-lookup:document-error
     (tptp-include-result:error-ambiguous-document "A")))

!(assertEqual
   (test:lookup-value
     (tptp:lookup-include-binding
       64 1000000 &relations "A" "x.p" &environment))
   (tptp-include-lookup:binding-ok
     (tptp-include-resolution:include-binding "A" "x.p" "AX")))

!(assertEqual
   (test:lookup-value
     (tptp:lookup-include-binding
       64 1000000 &relations "B" "x.p" &environment))
   (tptp-include-lookup:binding-ok
     (tptp-include-resolution:include-binding "B" "x.p" "BX")))

!(assertEqual
   (test:lookup-value
     (tptp:lookup-include-binding
       64 1000000 &relations "A" "missing.p" &environment))
   (tptp-include-lookup:binding-error
     (tptp-include-result:error-missing-binding "A" "missing.p")))

!(assertEqual
   (test:lookup-value
     (tptp:lookup-include-binding
       64 1000000 &relations "A" "x.p"
       &ambiguous-binding-environment))
   (tptp-include-lookup:binding-error
     (tptp-include-result:error-ambiguous-binding "A" "x.p")))

!(println! (TptpOfficialIncludeEnvironmentLookupV1Summary 8 8 0))
