!(import! &self langdef)

(= (test:tptp-document-imported
      (LangDef:Imported TptpFofCnfV1 $pack 1))
   True)

(= (test:expected-document $digest
      (tptp-fo:document
        (tptp-fo:source-digest $digest)
        (tptp-fo:inputs-cons
          (tptp-fo:formula-input
            (tptp-fo:occurrence (tptp-fo:source-digest $digest) 0)
            (tptp-fo:dialect-fof)
            (tptp-fo:formula-name-atomic
              (tptp-fo:symbol-name
                (tptp-fo:symbol-lower) "forall_formula"))
            (tptp-fo:role "axiom" (tptp-fo:annotation-none))
            (tptp-fo:formula-forall
              (tptp-fo:variables-cons
                (tptp-fo:variable-id
                  (tptp-fo:occurrence
                    (tptp-fo:source-digest $digest) 0)
                  (tptp-fo:variable-name "X"))
                (tptp-fo:variables-cons
                  (tptp-fo:variable-id
                    (tptp-fo:occurrence
                      (tptp-fo:source-digest $digest) 0)
                    (tptp-fo:variable-name "Y"))
                  (tptp-fo:variables-nil)))
              (tptp-fo:formula-atom
                (tptp-fo:atom-predicate
                  (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
                  (tptp-fo:terms-cons
                    (tptp-fo:term-variable
                      (tptp-fo:variable-id
                        (tptp-fo:occurrence
                          (tptp-fo:source-digest $digest) 0)
                        (tptp-fo:variable-name "X")))
                    (tptp-fo:terms-cons
                      (tptp-fo:term-variable
                        (tptp-fo:variable-id
                          (tptp-fo:occurrence
                            (tptp-fo:source-digest $digest) 0)
                          (tptp-fo:variable-name "Y")))
                      (tptp-fo:terms-nil))))))
            (tptp-fo:annotation-none)
            (tptp-fo:source-span-unknown))
          (tptp-fo:inputs-cons
            (tptp-fo:formula-input
              (tptp-fo:occurrence
                (tptp-fo:source-digest $digest) 1)
              (tptp-fo:dialect-cnf)
              (tptp-fo:formula-name-atomic
                (tptp-fo:symbol-name
                  (tptp-fo:symbol-lower) "clause_formula"))
              (tptp-fo:role "axiom" (tptp-fo:annotation-none))
              (tptp-fo:formula-or
                (tptp-fo:formula-atom
                  (tptp-fo:atom-predicate
                    (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
                    (tptp-fo:terms-cons
                      (tptp-fo:term-variable
                        (tptp-fo:variable-id
                          (tptp-fo:occurrence
                            (tptp-fo:source-digest $digest) 1)
                          (tptp-fo:variable-name "X")))
                      (tptp-fo:terms-nil))))
                (tptp-fo:formula-or
                  (tptp-fo:formula-not
                    (tptp-fo:formula-atom
                      (tptp-fo:atom-predicate
                        (tptp-fo:symbol-name
                          (tptp-fo:symbol-lower) "q")
                        (tptp-fo:terms-cons
                          (tptp-fo:term-variable
                            (tptp-fo:variable-id
                              (tptp-fo:occurrence
                                (tptp-fo:source-digest $digest) 1)
                              (tptp-fo:variable-name "X")))
                          (tptp-fo:terms-nil)))))
                  (tptp-fo:formula-atom
                    (tptp-fo:atom-equality
                      (tptp-fo:term-variable
                        (tptp-fo:variable-id
                          (tptp-fo:occurrence
                            (tptp-fo:source-digest $digest) 1)
                          (tptp-fo:variable-name "X")))
                      (tptp-fo:term-function
                        (tptp-fo:symbol-name
                          (tptp-fo:symbol-lower) "a")
                        (tptp-fo:terms-nil))))))
              (tptp-fo:annotation-none)
              (tptp-fo:source-span-unknown))
            (tptp-fo:inputs-cons
              (tptp-fo:include-input
                (tptp-fo:occurrence
                  (tptp-fo:source-digest $digest) 2)
                (tptp-fo:symbol-name
                  (tptp-fo:symbol-quoted) "Axioms/SET003+0.ax")
                (tptp-fo:include-names
                  (tptp-fo:formula-names-cons
                    (tptp-fo:formula-name-atomic
                      (tptp-fo:symbol-name
                        (tptp-fo:symbol-lower) "forall_formula"))
                    (tptp-fo:formula-names-cons
                      (tptp-fo:formula-name-integer "17")
                      (tptp-fo:formula-names-nil))))
                (tptp-fo:formula-name-some
                  (tptp-fo:formula-name-atomic
                    (tptp-fo:symbol-name
                      (tptp-fo:symbol-lower) "chosen_formula")))
                (tptp-fo:source-span-unknown))
              (tptp-fo:inputs-nil))))))
   True)

(= (test:expected-exists $scope
      (tptp-fo:formula-exists
        (tptp-fo:variables-cons
          (tptp-fo:variable-id $scope (tptp-fo:variable-name "X"))
          (tptp-fo:variables-nil))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))))
   True)

(= (test:expected-binary iff
      (tptp-fo:formula-iff
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
            (tptp-fo:terms-nil)))))
   True)
(= (test:expected-binary implies
      (tptp-fo:formula-implies
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
            (tptp-fo:terms-nil)))))
   True)
(= (test:expected-binary reverse-implies
      (tptp-fo:formula-reverse-implies
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
            (tptp-fo:terms-nil)))))
   True)
(= (test:expected-binary xor
      (tptp-fo:formula-xor
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
            (tptp-fo:terms-nil)))))
   True)
(= (test:expected-binary nor
      (tptp-fo:formula-nor
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
            (tptp-fo:terms-nil)))))
   True)
(= (test:expected-binary nand
      (tptp-fo:formula-nand
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
            (tptp-fo:terms-nil)))
        (tptp-fo:formula-atom
          (tptp-fo:atom-predicate
            (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
            (tptp-fo:terms-nil)))))
   True)

; The Document carrier preserves a variable's input-qualified spelling.  It
; does not pretend that a spelling is a binder identity: the outer and inner
; X below intentionally have the same VariableId.  The later scoped-formula
; transformation is responsible for minting distinct binder identities.
(= (test:expected-named-shadowing-formula $scope)
   (tptp-fo:formula-forall
     (tptp-fo:variables-cons
       (tptp-fo:variable-id $scope (tptp-fo:variable-name "X"))
       (tptp-fo:variables-nil))
     (tptp-fo:formula-implies
       (tptp-fo:formula-atom
         (tptp-fo:atom-predicate
           (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
           (tptp-fo:terms-cons
             (tptp-fo:term-variable
               (tptp-fo:variable-id
                 $scope (tptp-fo:variable-name "X")))
             (tptp-fo:terms-nil))))
       (tptp-fo:formula-exists
         (tptp-fo:variables-cons
           (tptp-fo:variable-id $scope (tptp-fo:variable-name "X"))
           (tptp-fo:variables-nil))
         (tptp-fo:formula-atom
           (tptp-fo:atom-predicate
             (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
             (tptp-fo:terms-cons
               (tptp-fo:term-variable
                 (tptp-fo:variable-id
                   $scope (tptp-fo:variable-name "X")))
               (tptp-fo:terms-nil))))))))

!(bind! &tptp-langdef (langdef:load "langdef/tptp/langdef.metta"))
!(bind! &prepared
   (langdef:prepare-import-file &tptp-langdef
     "tests/langdef/tptp/fof_cnf_document_features.p"))
!(bind! &raw (tptp:prepared-raw &prepared))
!(bind! &digest (tptp:prepared-digest &prepared))
!(bind! &forward
   (langdef:load-structural-tree-relabel
     "langdef/tptp/cnf_clause_data_bridge_v1.metta"
     tptp:raw->syntax-tree tptp:syntax-tree-label))
!(bind! &inverse
   (langdef:load-structural-tree-relabel
     "langdef/tptp/cnf_clause_data_bridge_v1.metta"
     tptp:syntax-tree->raw tptp:syntax-tree-raw-label))
!(bind! &syntax-tree
   (langdef:structural-tree-relabel &forward &raw))
!(bind! &roundtrip
   (langdef:structural-tree-relabel &inverse &syntax-tree))
!(assertEqual &roundtrip &raw)
!(bind! &document-plan
   (langdef:load-deterministic-equations
     (LangDef:EquationSources
       "langdef/tptp/cnf_clause_data_bridge_v1.metta"
       "langdef/tptp/first_order_document_bridge_v1.metta")))
!(bind! &document
   (langdef:run-deterministic-equations &document-plan
     (LangDef:DeterministicCall
       tptp-doc:syntax-tree->document &syntax-tree &digest)))
!(bind! &syntax-language
   (langdef:load-language "langdef/tptp/fof_cnf_syntax_tree_v1.metta"))
!(bind! &document-language
   (langdef:load-language "langdef/logic/tptp_first_order_document_v1.metta"))

; The exact mixed document is the authority gate: formula dialects, scoped
; variables, roles, include selection, qualifier, input order, and occurrence
; identities must all originate in the authored bridge equations.
!(assertEqual (test:expected-document &digest &document) True)
!(assertEqual
   (langdef:admit-term &syntax-language "SyntaxTree" &syntax-tree)
   (LangDef:TermAdmitted))
!(assertEqual
   (langdef:admit-term &document-language "Document" &document)
   (LangDef:TermAdmitted))

; The public manifest consumes the same two authored bridge GSLTs as one
; compiled program.  Its published value must equal the independently
; evaluated reference-plan result above.
!(bind! &public-import
   (langdef:import-file &tptp-langdef
     "tests/langdef/tptp/fof_cnf_document_features.p"))
!(assertEqual (test:tptp-document-imported &public-import) True)
!(assertEqual (match &self &document True) True)

; A public import result is checked against the manifest-owned Document
; LanguageDef before publication.  A well-formed ClauseData value is therefore
; rejected on this path, and the failed transaction cannot add it to &self.
!(bind! &wrong-result
   (fo-cnf:problem
     (fo-cnf:source-digest "not-a-document")
     (fo-cnf:clauses-nil)))
!(assertEqual (collapse (match &self &wrong-result True)) ())
!(assertEqual
   (case
     (__cetta_lib_langdef_publish &tptp-langdef
       (LangDef:ImportReady &syntax-tree
         (cons-atom &wrong-result (tptp:empty empty))))
     (((return (LangDef:PublishFailure $_source $_message)) True)
      ($_ False)))
   True)
!(assertEqual (collapse (match &self &wrong-result True)) ())

; Nested shadowing is a parser-to-Document canary, not a hand-constructed term.
!(bind! &shadow-prepared
   (langdef:prepare-import-file &tptp-langdef
     "tests/langdef/tptp/fof_shadowing.p"))
!(bind! &shadow-raw (tptp:prepared-raw &shadow-prepared))
!(bind! &shadow-digest (tptp:prepared-digest &shadow-prepared))
!(bind! &shadow-syntax-tree
   (langdef:structural-tree-relabel &forward &shadow-raw))
!(bind! &shadow-document
   (langdef:run-deterministic-equations &document-plan
     (LangDef:DeterministicCall
       tptp-doc:syntax-tree->document
       &shadow-syntax-tree &shadow-digest)))
!(assertEqual
   &shadow-document
   (tptp-fo:document
     (tptp-fo:source-digest &shadow-digest)
     (tptp-fo:inputs-cons
       (tptp-fo:formula-input
         (tptp-fo:occurrence
           (tptp-fo:source-digest &shadow-digest) 0)
         (tptp-fo:dialect-fof)
         (tptp-fo:formula-name-atomic
           (tptp-fo:symbol-name
             (tptp-fo:symbol-lower) "shadowing"))
         (tptp-fo:role "axiom" (tptp-fo:annotation-none))
         (test:expected-named-shadowing-formula
           (tptp-fo:occurrence
             (tptp-fo:source-digest &shadow-digest) 0))
         (tptp-fo:annotation-none)
         (tptp-fo:source-span-unknown))
       (tptp-fo:inputs-nil))))
!(assertEqual
   (langdef:admit-term &document-language "Document" &shadow-document)
   (LangDef:TermAdmitted))
!(bind! &shadow-public-import
   (langdef:import-file &tptp-langdef
     "tests/langdef/tptp/fof_shadowing.p"))
!(assertEqual (test:tptp-document-imported &shadow-public-import) True)
!(assertEqual (match &self &shadow-document True) True)

; The corpus above already exercises atom, negation, disjunction, equality,
; and universal quantification.  These direct calls cover every remaining
; first-order formula constructor in the authored bridge.
!(bind! &raw-p
   (node atomic
     (node constant (node lower-word (pair (cp 112) nil)))))
!(bind! &raw-q
   (node atomic
     (node constant (node lower-word (pair (cp 113) nil)))))
!(assertEqual
   (langdef:run-deterministic-equations &document-plan
     (LangDef:DeterministicCall tptp-doc:formula
       (node disequality
         (pair
           (node constant (node lower-word (pair (cp 112) nil)))
           (node constant (node lower-word (pair (cp 113) nil)))))
       (tptp-fo:occurrence
         (tptp-fo:source-digest "formula-control") 9)))
   (tptp-fo:formula-not
     (tptp-fo:formula-atom
       (tptp-fo:atom-equality
         (tptp-fo:term-function
           (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
           (tptp-fo:terms-nil))
         (tptp-fo:term-function
           (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
           (tptp-fo:terms-nil))))))
!(assertEqual
   (langdef:run-deterministic-equations &document-plan
     (LangDef:DeterministicCall tptp-doc:formula
       (node paren &raw-p)
       (tptp-fo:occurrence
         (tptp-fo:source-digest "formula-control") 9)))
   (tptp-fo:formula-atom
     (tptp-fo:atom-predicate
       (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
       (tptp-fo:terms-nil))))
!(assertEqual
   (langdef:run-deterministic-equations &document-plan
     (LangDef:DeterministicCall tptp-doc:formula
       (node and (pair &raw-p (cons &raw-q (cons &raw-p nil))))
       (tptp-fo:occurrence
         (tptp-fo:source-digest "formula-control") 9)))
   (tptp-fo:formula-and
     (tptp-fo:formula-atom
       (tptp-fo:atom-predicate
         (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
         (tptp-fo:terms-nil)))
     (tptp-fo:formula-and
       (tptp-fo:formula-atom
         (tptp-fo:atom-predicate
           (tptp-fo:symbol-name (tptp-fo:symbol-lower) "q")
           (tptp-fo:terms-nil)))
       (tptp-fo:formula-atom
         (tptp-fo:atom-predicate
           (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
           (tptp-fo:terms-nil))))))
!(assertEqual
   (test:expected-binary iff
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node iff (pair &raw-p &raw-q))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)
!(assertEqual
   (test:expected-binary implies
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node implies (pair &raw-p &raw-q))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)
!(assertEqual
   (test:expected-binary reverse-implies
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node reverse-implies (pair &raw-p &raw-q))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)
!(assertEqual
   (test:expected-binary xor
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node xor (pair &raw-p &raw-q))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)
!(assertEqual
   (test:expected-binary nor
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node nor (pair &raw-p &raw-q))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)
!(assertEqual
   (test:expected-binary nand
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node nand (pair &raw-p &raw-q))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)
!(assertEqual
   (test:expected-exists
     (tptp-fo:occurrence
       (tptp-fo:source-digest "formula-control") 9)
     (langdef:run-deterministic-equations &document-plan
       (LangDef:DeterministicCall tptp-doc:formula
         (node exists
           (pair
             (node variable-list
               (pair (node upper-word (pair (cp 88) nil)) nil))
             &raw-p))
         (tptp-fo:occurrence
           (tptp-fo:source-digest "formula-control") 9))))
   True)

!(assertEqual
   (langdef:close-deterministic-equations &document-plan) True)
!(assertEqual (langdef:close-structural-tree-relabel &forward) True)
!(assertEqual (langdef:close-structural-tree-relabel &inverse) True)
!(assertEqual (langdef:close-language &syntax-language) True)
!(assertEqual (langdef:close-language &document-language) True)
!(assertEqual (langdef:close &tptp-langdef) True)
!(println! (TptpFirstOrderDocumentV1Summary 23 23 0))
