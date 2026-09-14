!(import! &self ../../../lib/lib_bnf.metta)

(= (test:text-s)
   (bnf-v1:text-cons 115 (bnf-v1:text-nil)))
(= (test:text-a)
   (bnf-v1:text-cons 97 (bnf-v1:text-nil)))
(= (test:text-document)
   (bnf-v1:text-cons 100
     (bnf-v1:text-cons 111
       (bnf-v1:text-cons 99
         (bnf-v1:text-cons 117
           (bnf-v1:text-cons 109
             (bnf-v1:text-cons 101
               (bnf-v1:text-cons 110
                 (bnf-v1:text-cons 116 (bnf-v1:text-nil))))))))))
(= (test:text-ambiguous)
   (bnf-v1:text-cons 97
     (bnf-v1:text-cons 109
       (bnf-v1:text-cons 98
         (bnf-v1:text-cons 105
           (bnf-v1:text-cons 103
             (bnf-v1:text-cons 117
               (bnf-v1:text-cons 111
                 (bnf-v1:text-cons 117
                   (bnf-v1:text-cons 115 (bnf-v1:text-nil)))))))))))
(= (test:text-digit)
   (bnf-v1:text-cons 100
     (bnf-v1:text-cons 105
       (bnf-v1:text-cons 103
         (bnf-v1:text-cons 105
           (bnf-v1:text-cons 116 (bnf-v1:text-nil)))))))
(= (test:empty-authority $start)
   (bnf:grammar-authority $start (bnf:empty-lexical-environment)))
(= (test:digit-lexical-environment)
   (bnf-v1:lexical-environment
     (bnf-v1:lexical-declarations-cons
       (bnf-v1:lexical-declaration
         (test:text-digit)
         "DigitClass"
         (bnf-v1:lexical-points
           (bnf-v1:scalars-cons 55 (bnf-v1:scalars-nil)))
         "bnf-test:digit"
         (bnf-v1:lexical-origin "plain-bnf-test" 0))
       (bnf-v1:lexical-declarations-nil))))
(= (test:digit-authority $start)
   (bnf:grammar-authority $start (test:digit-lexical-environment)))
(= (test:scalar-lexical-environment $matcher)
   (bnf-v1:lexical-environment
     (bnf-v1:lexical-declarations-cons
       (bnf-v1:lexical-declaration
         (test:text-digit)
         "ScalarClass"
         $matcher
         "bnf-test:scalar"
         (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0))
       (bnf-v1:lexical-declarations-nil))))
(= (test:scalar-authority $matcher)
   (bnf:grammar-authority
     (test:text-s) (test:scalar-lexical-environment $matcher)))
(= (test:scalar-list-1 $first)
   (bnf-v1:scalars-cons $first (bnf-v1:scalars-nil)))
(= (test:scalar-list-2 $first $second)
   (bnf-v1:scalars-cons $first (test:scalar-list-1 $second)))
(= (test:scalar-boundaries)
   (bnf-v1:scalars-cons 0
     (bnf-v1:scalars-cons 55295
       (bnf-v1:scalars-cons 57344
         (bnf-v1:scalars-cons 1114111 (bnf-v1:scalars-nil))))))
(= (test:validate $source $start)
   (bnf:validate
     (bnf:read-text $source)
     (test:empty-authority $start)))

(= (test:list-count LNil) 0)
(= (test:list-count (LCons $_head $tail))
   (+ 1 (test:list-count $tail)))

(= (test:first-two-rules
      (LCons
        (GrammarRule $first-label $first-category
          $first-params $first-syntax EvalNone)
        (LCons
          (GrammarRule $second-label $second-category
            $second-params $second-syntax EvalNone)
          LNil)))
   ($first-label $first-category
    (test:list-count $first-params)
    (test:list-count $first-syntax)
    $second-label $second-category
    (test:list-count $second-params)
    (test:list-count $second-syntax)))

(= (test:candidate-language
      (BNF:DenotationCandidate $language $_profile $_origins))
   $language)
(= (test:candidate-profile
      (BNF:DenotationCandidate $_language $profile $_origins))
   $profile)
(= (test:candidate-origins
      (BNF:DenotationCandidate
        $_language $_profile
        (BNF:DenotationOriginsV1 $origins $_lexical-origins)))
   $origins)
(= (test:candidate-lexical-origins
      (BNF:DenotationCandidate
        $_language $_profile
        (BNF:DenotationOriginsV1 $_origins $lexical-origins)))
   $lexical-origins)

(= (test:rule-labels LNil) LNil)
(= (test:rule-labels
      (LCons
        (GrammarRule $label $_category $_params $_syntax EvalNone)
        $tail))
   (LCons $label (test:rule-labels $tail)))
(= (test:candidate-rule-labels
      (BNF:DenotationCandidate
        (GSLTLanguageDefWireV1
          $_language $_types $rules $_equations $_rewrites)
        $_profile $_origins))
   (test:rule-labels $rules))

(= (test:admitted-candidate
      (BNF:AdmittedDenotation
        $_binding $_source $_document $_authority $_validation $candidate))
   $candidate)

(= (test:validation-class
      (BNF:SemanticGrammarAdmitted
        $_binding $_source $_document $_authority $_validation))
   accepted)
(= (test:validation-class
      (BNF:SemanticGrammarRefused
        $_binding $_source $_document $_authority $_diagnostics))
   refused)

(= (test:diagnostic-count BNFDiagnosticsNilV1) 0)
(= (test:diagnostic-count
      (BNFDiagnosticsConsV1 $_head $tail))
   (+ 1 (test:diagnostic-count $tail)))

(= (test:name-count BNFNamesNilV1) 0)
(= (test:name-count (BNFNamesConsV1 $_head $tail))
   (+ 1 (test:name-count $tail)))

(= (test:refusal-summary
      (BNF:SemanticGrammarRefused
        $_binding $_source $_document $_authority
        (BNFDiagnosticsConsV1 $first $tail)))
   ($first (test:diagnostic-count $tail)))

(= (test:analysis-summary
      (BNF:SemanticGrammarAdmitted
        $_binding $_source $_document $_authority
        (BNFSemanticValidationAcceptedV1
          $_start $_definitions $_lexicals
          (BNFGrammarAnalysisV1 $reachable $nullable $productive))))
   ((test:name-count $reachable)
    (test:name-count $nullable)
    (test:name-count $productive)))

(= (test:language-summary
      (GSLTLanguageDefWireV1
        $language $types $rules $equations $rewrites))
   ($language
    (test:list-count $types)
    (test:list-count $rules)
    (test:list-count $equations)
    (test:list-count $rewrites)))
(= (test:profile-summary
      (GSLTParserProfileLayerV1
        $profile $start $classes $states))
   ($profile $start
    (test:list-count $classes)
    (test:list-count $states)))
(= (test:single-lexical-profile
      (GSLTParserProfileLayerV1
        $_profile $_start
        (LCons (LexicalClassPoints $class (LCons $scalar LNil)) LNil)
        (LCons (LexicalState $sort $state-class $label) LNil)))
   ($class $scalar $sort $state-class $label))

(= (test:accepted $result)
   (case $result
     (((LangDef:AuthoredParseAccepted
         PlainBnfDenotedSyntaxV1 $_binding $_source $_values)
       True)
      ($_ False))))

(= (test:rejected $result)
   (case $result
     (((LangDef:AuthoredParseRejected
         PlainBnfDenotedSyntaxV1
         $_binding $_source $_offset $_expected)
       True)
      ($_ False))))

(= (test:forest-accepted $result)
   (case $result
     (((LangDef:AuthoredParseEvidenceAccepted
         PlainBnfDenotedSyntaxV1
         $_binding $_source $_forest-digest
         (pf-v1 $_start $_scalar-length
           (cons $_root $_roots)
           (cons $_node $_nodes)
           (cons $_choice $_choices))
         $_values)
       True)
      ($_ False))))

(= (test:forest-rejected $result)
   (case $result
     (((LangDef:AuthoredParseEvidenceRejected
         PlainBnfDenotedSyntaxV1
         $_binding $_source $_offset $_expected $_forest-digest
         (pf-v1 $_start $_scalar-length $_roots $_nodes $_choices))
       True)
      ($_ False))))

(= (test:choice-label
      (pf-choice
        $_parent (pp-label $_state $label) $_prefix $_child $_pivot))
   $label)
(= (test:choice-label-present nil $_label) False)
(= (test:choice-label-present (cons $head $tail) $label)
   (let $actual (test:choice-label $head)
     (if (== $actual $label)
         True
         (test:choice-label-present $tail $label))))
(= (test:choice-label-count nil $_label) 0)
(= (test:choice-label-count (cons $head $tail) $label)
   (let $actual (test:choice-label $head)
     (if (== $actual $label)
         (+ 1 (test:choice-label-count $tail $label))
         (test:choice-label-count $tail $label))))

(= (test:pf-list-count nil) 0)
(= (test:pf-list-count (cons $_head $tail))
   (+ 1 (test:pf-list-count $tail)))

(= (test:forest-summary
      (LangDef:AuthoredParseEvidenceAccepted
        PlainBnfDenotedSyntaxV1
        $_binding $_source $_forest-digest
        (pf-v1 $_start $scalar-length $roots $nodes $choices)
        $_values))
   (accepted $scalar-length
     (test:pf-list-count $roots)
     (test:pf-list-count $nodes)
     (test:pf-list-count $choices)))
(= (test:forest-summary
      (LangDef:AuthoredParseEvidenceRejected
        PlainBnfDenotedSyntaxV1
        $_binding $_source $offset $_expected $_forest-digest
        (pf-v1 $_start $scalar-length $roots $nodes $choices)))
   (rejected $scalar-length $offset
     (test:pf-list-count $roots)
     (test:pf-list-count $nodes)
     (test:pf-list-count $choices)))

(= (test:specimen-forest $grammar-path $input)
   (let $executable
        (bnf:denote
          (bnf:validate
            (bnf:read-file $grammar-path)
            (test:empty-authority (test:text-s))))
     (case (bnf:load-parser $executable)
       (((NativeHandle $kind $identifier)
          (let $parser (NativeHandle $kind $identifier)
            (let $evidence
                 (langdef:parse-authored-text-evidence
                   $parser $input 10000)
              (let $_ (langdef:close-authored-parser $parser)
                $evidence))))
        ($result (PlainBnfSpecimenParserFaultV1 $result))))))

(= (test:specimen-validation $grammar-path)
   (bnf:validate
     (bnf:read-file $grammar-path)
     (test:empty-authority (test:text-s))))

(= (test:read-document-value
      (BNF:ReadValue
        (BNF:GrammarDocument $_binding $_source $document)))
   $document)

(= (test:generated-self-parse)
   (let $executable
        (bnf:denote
          (bnf:validate
            (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
            (bnf:self-authority-v1)))
     (case (bnf:load-parser $executable)
       (((NativeHandle $kind $identifier)
          (let $parser (NativeHandle $kind $identifier)
            (let $parsed
                 (langdef:parse-authored-file
                   $parser "langdef/bnf/plain_bnf_self_v1.bnf")
              (let $_ (langdef:close-authored-parser $parser)
                $parsed))))
        ($result (PlainBnfGeneratedSelfParserFaultV1 $result))))))

(= (test:generated-self-cst)
   (let $parsed (test:generated-self-parse)
     (case $parsed
       (((LangDef:AuthoredParseAccepted
           PlainBnfDenotedSyntaxV1 $_binding $_source $values)
          (if (== (size-atom $values) 1)
              (car-atom $values)
              (PlainBnfGeneratedSelfMultiplicityFaultV1 $values)))
        ($_ (PlainBnfGeneratedSelfCstFaultV1 $parsed))))))

(= (test:relabel-and-project-generated-self $generated-cst)
   (let $isomorphism
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_bootstrap_cst_isomorphism_v1.metta"))
     (let $bootstrap-cst
          (langdef:run-deterministic-equations
            $isomorphism
            (LangDef:DeterministicCall
              bnf-v1:generated-cst->bootstrap-cst $generated-cst))
       (let $roundtrip-cst
            (langdef:run-deterministic-equations
              $isomorphism
              (LangDef:DeterministicCall
                bnf-v1:bootstrap-cst->generated-cst $bootstrap-cst))
         (let $_ (langdef:close-deterministic-equations $isomorphism)
           (let $projection
                (langdef:load-deterministic-equations
                  (LangDef:EquationSources
                    "langdef/bnf/plain_bnf_cst_projection_v1.metta"))
             (let $document
                  (langdef:run-deterministic-equations
                    $projection
                    (LangDef:DeterministicCall
                      bnf-v1:project $bootstrap-cst))
               (let $_ (langdef:close-deterministic-equations $projection)
                 ($roundtrip-cst $document)))))))))

(= (test:generated-self-document)
   (let $generated-cst (test:generated-self-cst)
     (let ($_roundtrip-cst $document)
          (test:relabel-and-project-generated-self $generated-cst)
       $document)))

(= (test:forest-bound-refused $result)
   (case $result
     (((Error __cetta_lib_authored_parser_parse_file_evidence
         "canonical forest exceeds the requested materialization bound")
       True)
      ($_ False))))

(= (test:text-forest-bound-refused $result)
   (case $result
     (((Error __cetta_lib_authored_parser_parse_text_evidence
         "canonical forest exceeds the requested materialization bound")
       True)
      ($_ False))))

; Every query returns its marker only after all observations in that query
; match.  Keeping the checks separate makes a failed semantic boundary visible
; without turning implementation-specific handles or counters into the API.
!(let $candidate
     (test:admitted-candidate
       (bnf:denote
         (bnf:validate
           (bnf:read-text "<s> ::= \"a\" <s> |\n")
           (test:empty-authority (test:text-s)))))
   (let ("PlainBnfDenotedSyntaxV1" 1 2 0 0)
        (test:language-summary (test:candidate-language $candidate))
     PlainBnfDenotationLanguageAcceptedV1))

!(let $candidate
     (test:admitted-candidate
       (bnf:denote
         (bnf:validate
           (bnf:read-text "<s> ::= \"a\" <s> |\n")
           (test:empty-authority (test:text-s)))))
   (let ("PlainBnfDenotedParserV1" "s" 0 0)
        (test:profile-summary (test:candidate-profile $candidate))
     PlainBnfDenotationProfileAcceptedV1))

!(let $candidate
     (test:admitted-candidate
       (bnf:denote
         (bnf:validate
           (bnf:read-text "<s> ::= \"a\" <s> |\n")
           (test:empty-authority (test:text-s)))))
   (let 2 (test:list-count (test:candidate-origins $candidate))
     PlainBnfDenotationOriginsAcceptedV1))

!(let $candidate
     (test:admitted-candidate
       (bnf:denote
         (bnf:validate
           (bnf:read-text "<s> ::= \"a\" <s> |\n")
           (test:empty-authority (test:text-s)))))
   (let ("s#" "s" 1 2 "s#x" "s" 0 0)
        (case $candidate
          (((BNF:DenotationCandidate
              (GSLTLanguageDefWireV1
                $_language $_types $rules $_equations $_rewrites)
              $_profile $_origins)
             (test:first-two-rules $rules))))
     PlainBnfDenotationRuleOrderAcceptedV1))

!(let $candidate
     (test:admitted-candidate
       (bnf:denote
         (bnf:validate
           (bnf:read-text "<ambiguous> ::= \"x\" | \"x\"\n")
           (test:empty-authority (test:text-ambiguous)))))
   (let ("ambiguous#" "ambiguous" 0 1
         "ambiguous#x" "ambiguous" 0 1)
        (case $candidate
          (((BNF:DenotationCandidate
              (GSLTLanguageDefWireV1
                $_language $_types $rules $_equations $_rewrites)
              $_profile $_origins)
             (test:first-two-rules $rules))))
     PlainBnfDenotationMultiplicityAcceptedV1))

!(let refused
     (test:validation-class
       (bnf:validate
         (bnf:read-text "")
         (test:empty-authority (test:text-s))))
   PlainBnfEmptyGrammarRefusedV1)

!(let ((BNFDuplicateDefinitionV1 $_name $_first $_duplicate) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= \"a\"\n<s> ::= \"b\"\n")
         (test:empty-authority (test:text-s))))
   PlainBnfDuplicateDefinitionRefusedV1)

!(let ((BNFUnresolvedReferenceV1 $_owner $_missing $_firstSpan) 1)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <missing> <missing>\n")
         (test:empty-authority (test:text-s))))
   PlainBnfReferenceMultiplicityRefusedV1)

!(let (1 1 1)
     (test:analysis-summary
       (bnf:validate
         (bnf:read-text "<s> ::= \"a\" <s> |\n")
         (test:empty-authority (test:text-s))))
   PlainBnfGraphAnalysisAcceptedV1)

!(let (2 2 2)
     (test:analysis-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <a>\n<a> ::= \n")
         (test:empty-authority (test:text-s))))
   PlainBnfNullableClosureAcceptedV1)

!(let ((BNFUnreachableDefinitionV1 $_name $_span) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= \"x\"\n<unused> ::= \"y\"\n")
         (test:empty-authority (test:text-s))))
   PlainBnfUnreachableDefinitionRefusedV1)

!(let ((BNFUnproductiveDefinitionV1 $_name $_span) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <s>\n")
         (test:empty-authority (test:text-s))))
   PlainBnfUnproductiveDefinitionRefusedV1)

!(let ((BNFUnknownStartV1 $_name) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= \"a\"\n")
         (test:empty-authority (test:text-a))))
   PlainBnfExplicitUnknownStartRefusedV1)

!(let ((BNFLexicalRuleNameConflictV1
          $_name $_rule-span $_lexical-origin) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<digit> ::= \"7\"\n")
         (test:digit-authority (test:text-digit))))
   PlainBnfLexicalRuleCollisionRefusedV1)

!(let ((BNFInvalidUnicodeScalarV1
          -1 (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0)) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-list-1 -1)))))
   PlainBnfNegativeUnicodeScalarRefusedV1)

!(let ((BNFInvalidUnicodeScalarV1
          55296 (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0)) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-list-1 55296)))))
   PlainBnfSurrogateScalarRefusedV1)

!(let ((BNFInvalidUnicodeScalarV1
          1114112 (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0)) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-list-1 1114112)))))
   PlainBnfOutOfRangeScalarRefusedV1)

!(let ((BNFNonIncreasingLexicalScalarsV1
          55 55 (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0)) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-list-2 55 55)))))
   PlainBnfDuplicateScalarRefusedV1)

!(let ((BNFNonIncreasingLexicalScalarsV1
          56 55 (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0)) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-list-2 56 55)))))
   PlainBnfDescendingScalarsRefusedV1)

!(let accepted
     (test:validation-class
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-boundaries)))))
   PlainBnfUnicodeScalarBoundariesAcceptedV1)

!(let ((BNFEmptyLexicalClassV1
          "ScalarClass" (bnf-v1:lexical-origin "plain-bnf-scalar-test" 0)) 0)
     (test:refusal-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (bnf-v1:scalars-nil)))))
   PlainBnfEmptyPointClassRefusedV1)

!(let accepted
     (test:validation-class
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-except (bnf-v1:scalars-nil)))))
   PlainBnfEmptyExclusionAcceptedV1)

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-except (bnf-v1:scalars-nil)))))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True (test:accepted (langdef:parse-authored-text $parser "x"))
            (let True (test:accepted (langdef:parse-authored-text $parser "λ"))
              (let True (test:rejected (langdef:parse-authored-text $parser ""))
                (let True (test:rejected (langdef:parse-authored-text $parser "xx"))
                  (let True (langdef:close-authored-parser $parser)
                    PlainBnfEmptyExclusionRuntimeAcceptedV1)))))))
      ($result (PlainBnfUnexpectedEmptyExclusionParserV1 $result)))))

; Selected end-of-list and gap branches, not an end-to-end full-carrier scan.
; Full-carrier emptiness is proved independently in Lean; the authored-rule
; execution correspondence remains a separate obligation.
!(let (gslt:result:BNFExclusionTailInhabitedV1:110 BNFNoV1)
     (gslt:mode:BNFExclusionTailInhabitedV1:110
       1114111 (bnf-v1:scalars-nil))
   PlainBnfExclusionCompleteTailRefusedV1)
!(let (gslt:result:BNFExclusionTailInhabitedV1:110 BNFYesV1)
     (gslt:mode:BNFExclusionTailInhabitedV1:110
       1114110 (bnf-v1:scalars-nil))
   PlainBnfExclusionMissingLastAcceptedV1)
!(let (gslt:result:BNFExclusionGapInhabitedV1:1110 BNFYesV1)
     (gslt:mode:BNFExclusionGapInhabitedV1:1110
       55294 57344 (bnf-v1:scalars-nil))
   PlainBnfExclusionScalarGapAcceptedV1)
!(let (gslt:result:BNFExclusionGapInhabitedV1:1110 BNFYesV1)
     (gslt:mode:BNFExclusionGapInhabitedV1:1110
       0 1 (bnf-v1:scalars-cons 1114111 (bnf-v1:scalars-nil)))
   PlainBnfExclusionLaterGapAcceptedV1)

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:scalar-authority
           (bnf-v1:lexical-points (test:scalar-boundaries)))))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True (langdef:close-authored-parser $parser)
            PlainBnfUnicodeScalarBoundaryParserLoadedV1)))
      ($result (PlainBnfUnicodeScalarBoundaryParserFaultV1 $result)))))

!(let (1 0 1)
     (test:analysis-summary
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:digit-authority (test:text-s))))
   PlainBnfLexicalGraphLeafAcceptedV1)

!(let $candidate
     (test:admitted-candidate
       (bnf:denote
         (bnf:validate
           (bnf:read-text "<s> ::= <digit>\n")
           (test:digit-authority (test:text-s)))))
   (let ("PlainBnfDenotedSyntaxV1" 2 1 0 0)
        (test:language-summary (test:candidate-language $candidate))
     (let ("PlainBnfDenotedParserV1" "s" 1 1)
          (test:profile-summary (test:candidate-profile $candidate))
       (let ("DigitClass" 55 "digit" "DigitClass" "bnf-test:digit")
            (test:single-lexical-profile
              (test:candidate-profile $candidate))
         PlainBnfLexicalDenotationAcceptedV1))))

!(let (LCons
         (BNF:GeneratedLexicalOrigin
           "digit" "DigitClass" "bnf-test:digit"
           (bnf-v1:lexical-points
             (bnf-v1:scalars-cons 55 (bnf-v1:scalars-nil)))
           (bnf-v1:lexical-origin "plain-bnf-test" 0))
         LNil)
     (test:candidate-lexical-origins
       (test:admitted-candidate
         (bnf:denote
           (bnf:validate
             (bnf:read-text "<s> ::= <digit>\n")
             (test:digit-authority (test:text-s))))))
   PlainBnfLexicalOriginAcceptedV1)

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-text "<s> ::= <digit>\n")
         (test:digit-authority (test:text-s))))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:accepted (langdef:parse-authored-text $parser "7"))
            (let True
                 (test:rejected
                   (langdef:parse-authored-text $parser "x"))
              (let True (langdef:close-authored-parser $parser)
                PlainBnfLexicalRuntimeAcceptedV1)))))
      ($result (PlainBnfLexicalRuntimeFaultV1 $result)))))

!(let $refused
     (bnf:denote
       (bnf:validate
         (bnf:read-text "<s> ::= <s>\n")
         (test:empty-authority (test:text-s))))
   (let (BNF:ParserNotLoaded $_diagnostics)
        (bnf:load-parser $refused)
     PlainBnfRefusedParserNotLoadedV1))

!(case (langdef:load-authored-parser-values 1.5 1.5)
   (((Error __cetta_lib_authored_parser_load_values $_message)
      PlainBnfStructuredValueDecoderRefusedV1)
    ($_result (PlainBnfUnexpectedStructuredValueAdmissionV1 $_result))))

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-text
           "<document> ::= <many>\n<many> ::= \"a\" <many> |\n")
         (test:empty-authority (test:text-document))))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:accepted (langdef:parse-authored-text $parser "aaa"))
            (let True
                 (test:rejected
                   (langdef:parse-authored-text $parser "aaab"))
              (let True (langdef:close-authored-parser $parser)
                PlainBnfDenotationRuntimeAcceptedV1)))))
      ($result (PlainBnfDenotationRuntimeFaultV1 $result)))))

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-text
           "<document> ::= <many>\n<many> ::= \"a\" <many> |\n")
         (test:empty-authority (test:text-document))))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:forest-rejected
                 (langdef:parse-authored-text-evidence
                   $parser "aaab" 10000))
            (let True (langdef:close-authored-parser $parser)
              PlainBnfRejectedForestEvidenceAcceptedV1))))
      ($result (PlainBnfRejectedForestEvidenceFaultV1 $result)))))

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-text "<ambiguous> ::= \"x\" | \"x\"\n")
         (test:empty-authority (test:text-ambiguous))))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:text-forest-bound-refused
                 (langdef:parse-authored-text-evidence $parser "x" 1))
            (let $evidence
                 (langdef:parse-authored-text-evidence
                   $parser "x" 10000)
              (let $_ (langdef:close-authored-parser $parser)
                (case $evidence
                  (((LangDef:AuthoredParseEvidenceAccepted
                      PlainBnfDenotedSyntaxV1
                      $_binding $_source $_forest-digest
                      (pf-v1
                        $_start $_scalar-length $_roots $_nodes $choices)
                      $_values)
                    (let True
                         (test:choice-label-present
                           $choices "ambiguous#")
                      (let True
                           (test:choice-label-present
                             $choices "ambiguous#x")
                        PlainBnfAmbiguousForestMultiplicityAcceptedV1)))
                   ($_result
                     (PlainBnfAmbiguousForestMultiplicityFaultV1
                       $_result)))))))))
      ($result (PlainBnfAmbiguousForestMultiplicityFaultV1 $result)))))

!(let accepted
     (test:validation-class
       (bnf:validate
         (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
         (bnf:self-authority-v1)))
   PlainBnfSelfSemanticAdmissionAcceptedV1)

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
         (bnf:self-authority-v1)))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:accepted
                 (langdef:parse-authored-file
                   $parser "langdef/bnf/plain_bnf_self_v1.bnf"))
            (let True (langdef:close-authored-parser $parser)
              PlainBnfSelfGeneratedParserAcceptedV1))))
      ($result (PlainBnfSelfGeneratedParserFaultV1 $result)))))

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
         (bnf:self-authority-v1)))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:forest-accepted
                 (langdef:parse-authored-file-evidence
                   $parser "langdef/bnf/plain_bnf_self_v1.bnf" 10000))
            (let True (langdef:close-authored-parser $parser)
              PlainBnfSelfGeneratedForestEvidenceAcceptedV1))))
      ($result (PlainBnfSelfGeneratedForestEvidenceFaultV1 $result)))))

!(let $executable
     (bnf:denote
       (bnf:validate
         (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
         (bnf:self-authority-v1)))
   (case (bnf:load-parser $executable)
     (((NativeHandle $kind $identifier)
        (let $parser (NativeHandle $kind $identifier)
          (let True
               (test:forest-bound-refused
                 (langdef:parse-authored-file-evidence
                   $parser "langdef/bnf/plain_bnf_self_v1.bnf" 3000))
            (let True (langdef:close-authored-parser $parser)
              PlainBnfForestEvidenceBoundRefusedV1))))
      ($result (PlainBnfForestEvidenceBoundFaultV1 $result)))))

!(let (1 1 1)
     (test:analysis-summary
       (test:specimen-validation
         "tests/langdef/bnf/plain_bnf_dyck_v1.bnf"))
   PlainBnfDyckAnalysisAcceptedV1)
!(let (2 0 2)
     (test:analysis-summary
       (test:specimen-validation
         "tests/langdef/bnf/plain_bnf_mutual_v1.bnf"))
   PlainBnfMutualAnalysisAcceptedV1)
!(let (2 1 2)
     (test:analysis-summary
       (test:specimen-validation
         "tests/langdef/bnf/plain_bnf_nullable_v1.bnf"))
   PlainBnfNullableAnalysisAcceptedV1)
!(let (3 0 3)
     (test:analysis-summary
       (test:specimen-validation
         "tests/langdef/bnf/plain_bnf_ambiguous_v1.bnf"))
   PlainBnfAmbiguousAnalysisAcceptedV1)

!(let True
     (test:forest-accepted
       (test:specimen-forest
         "tests/langdef/bnf/plain_bnf_dyck_v1.bnf" "(()())"))
   PlainBnfDyckForestAcceptedV1)
!(let True
     (test:forest-accepted
       (test:specimen-forest
         "tests/langdef/bnf/plain_bnf_mutual_v1.bnf" "xyz"))
   PlainBnfMutualForestAcceptedV1)
!(let True
     (test:forest-accepted
       (test:specimen-forest
         "tests/langdef/bnf/plain_bnf_nullable_v1.bnf" "x"))
   (let True
        (test:forest-accepted
          (test:specimen-forest
            "tests/langdef/bnf/plain_bnf_nullable_v1.bnf" "aax"))
     PlainBnfNullableForestAcceptedV1))
!(let (LangDef:AuthoredParseEvidenceAccepted
         PlainBnfDenotedSyntaxV1
         $_binding $_source $_forest-digest
         (pf-v1 $_start $_length $_roots $_nodes $choices)
         $_values)
     (test:specimen-forest
       "tests/langdef/bnf/plain_bnf_ambiguous_v1.bnf" "x")
   (let (1 1 1 1)
        ((test:choice-label-count $choices "s#")
         (test:choice-label-count $choices "s#x")
         (test:choice-label-count $choices "left#")
         (test:choice-label-count $choices "right#"))
     PlainBnfAmbiguousDerivationsRetainedV1))
!(let True
     (test:forest-rejected
       (test:specimen-forest
         "tests/langdef/bnf/plain_bnf_dyck_v1.bnf" "(()"))
   PlainBnfDyckMalformedWordRejectedV1)

!(let $generated-cst (test:generated-self-cst)
   (let ($roundtrip-cst $generated-document)
        (test:relabel-and-project-generated-self $generated-cst)
     (let True (== (quote $generated-cst) (quote $roundtrip-cst))
       (let $bootstrap-document
            (test:read-document-value
              (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf"))
         (let True
              (== (quote $generated-document) (quote $bootstrap-document))
           PlainBnfSecondStageDocumentExactV1)))))

!(let $first-denotation
     (bnf:denote
       (bnf:validate
         (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
         (bnf:self-authority-v1)))
   (let $second-document (test:generated-self-document)
     (let $second-denotation
          (bnf:denote
            (bnf:validate
              (BNF:ReadValue
                (BNF:GrammarDocument
                  second-stage-binding second-stage-source
                  $second-document))
              (bnf:self-authority-v1)))
       (let $first-candidate
            (test:admitted-candidate $first-denotation)
         (let $second-candidate
              (test:admitted-candidate $second-denotation)
           (let True
                (== (quote $first-candidate) (quote $second-candidate))
             PlainBnfSecondStageDenotationFixedPointExactV1))))))
