!(import! &self langdef)

(= (test:term-rejected $result)
   (case $result
     (((LangDef:TermRejected $_status $_message) True)
      ($_other False))))

(= (test:source)
   (tptp-semantic:source-digest "demo-source"))

(= (test:occurrence)
   (tptp-semantic:occurrence-id (test:source) 0))

(= (test:span)
   (tptp92-ast:source-span 4 23))

(= (test:fof)
   (tptp92-ast:fof-annotated:alt-1
     (tptp92-ast:name:alt-2
       (tptp92-ast:token:integer "sample"))
     (tptp92-ast:formula-role:alt-1
       (tptp92-ast:token:lower-word "sample"))
     (tptp92-ast:fof-formula:alt-2
       (tptp92-ast:fof-sequent:alt-1
         (tptp92-ast:fof-formula-tuple:alt-1)
         (tptp92-ast:gentzen-arrow:alt-1)
         (tptp92-ast:fof-formula-tuple:alt-1)))
     (tptp92-ast:annotations:alt-2)))

(= (test:input)
   (tptp-semantic:fof-input
     (test:occurrence) (test:fof) (test:span)))

(= (test:include)
   (tptp-semantic:document-include
     (test:occurrence)
     (tptp92-ast:include:alt-1
       (tptp92-ast:file-name:alt-1
         (tptp92-ast:atomic-word:alt-1
           (tptp92-ast:token:lower-word "axioms")))
       (tptp92-ast:include-optionals:alt-1))
     (test:span)))

(= (test:document)
   (tptp-semantic:document
     (test:source)
     (tptp-semantic:document-inputs-cons
       (tptp-semantic:document-formula (test:input))
       (tptp-semantic:document-inputs-cons
         (test:include)
         (tptp-semantic:document-inputs-nil)))))

(= (test:derivation)
   (tptp-semantic:derivation
     (test:source)
     (tptp-semantic:derivation-nodes-cons
       (test:input)
       (tptp-semantic:derivation-nodes-nil))))

(= (test:include-as-derivation-node)
   (tptp-semantic:derivation-nodes-cons
     (test:include)
     (tptp-semantic:derivation-nodes-nil)))

(= (test:document-root)
   (tptp-semantic:document
     (test:source)
     (tptp-semantic:document-inputs-nil)))

!(bind! &language
   (langdef:load-language
     "langdef/tptp/official_semantic_carrier_v9200.metta"))

!(assertEqual
   (langdef:admit-term &language "TptpSemantic:annotated-input"
     (test:input))
   (LangDef:TermAdmitted))

!(assertEqual
   (langdef:admit-term &language "TptpSemantic:document-input"
     (test:include))
   (LangDef:TermAdmitted))

!(assertEqual
   (langdef:admit-term &language "TptpSemantic:document"
     (test:document))
   (LangDef:TermAdmitted))

!(assertEqual
   (langdef:admit-term &language "TptpSemantic:derivation"
     (test:derivation))
   (LangDef:TermAdmitted))

!(assertEqual
   (test:term-rejected
     (langdef:admit-term &language "TptpSemantic:annotated-input"
       (tptp-semantic:thf-input
         (test:occurrence) (test:fof) (test:span))))
   True)

!(assertEqual
   (test:term-rejected
     (langdef:admit-term &language "TptpSemantic:derivation-nodes"
       (test:include-as-derivation-node)))
   True)

!(assertEqual
   (test:term-rejected
     (langdef:admit-term &language "TptpSemantic:derivation"
       (test:document-root)))
   True)

!(assertEqual (langdef:close-language &language) True)
!(println! (TptpOfficialSemanticCarrierV9200Summary 8 8 0))
