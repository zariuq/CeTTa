!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

; Imported compact values are data even when the receiving space interprets
; the same source head.  The explicit consumer decides whether to evaluate.
(= (p $x) TptpCompactMustRemainData)

(= (compact-test:document
     (TPTP:StageValue extended-bnf-ast
       (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))

(= (compact-test:one-root $result)
   (case $result
     (((LCons (LCons $root $_more-roots) $_more-derivations) $root)
      ($fault (TptpCompactRootFailure $fault)))))

(: compact-test:pack (-> Atom Atom Atom Atom Atom Atom Atom Atom Atom))
(= (compact-test:pack
     $fof $cnf $thf-type $thf-formula
     $right-application $left-application $lexical $families)
   (quote
     (TptpOfficialSyntaxCompactV1
       $fof $cnf $thf-type $thf-formula
       $right-application $left-application $lexical $families)))

(= (compact-test:observe $program $context (EBNF:Parser $handle $receipt) $text)
   (function
     (case (langdef:parse-authored-text $handle $text)
       (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
         (let $root
              (compact-test:one-root
                 (langdef:run-deterministic-equations $program
                   (LangDef:DeterministicCall tptp-read-v1:roots $context
                     (bnf:project-ebnf-trees $receipt $trees))))
           (let $result
                (langdef:run-deterministic-equations-data $program
                  (LangDef:DeterministicCall tptp-v1:node $root))
             (return $result))))
        ($fault (return (TptpCompactParseFailure $fault)))))))

(= (compact-test:observe-file
     $program $context (EBNF:Parser $handle $receipt) $path)
   (case (langdef:parse-authored-file $handle $path)
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (let $root
            (compact-test:one-root
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-read-v1:roots $context
                 (bnf:project-ebnf-trees $receipt $trees))))
         (langdef:run-deterministic-equations-data $program
           (LangDef:DeterministicCall tptp-v1:node $root))))
      ($fault (quote (TptpCompactFileFailure $fault))))))

(= (compact-test:use $program $context (EBNF:Parser $handle $receipt))
   (let $family-path (system:arg 1)
     (let $fof
          (compact-test:observe $program $context (EBNF:Parser $handle $receipt)
            "fof(fof_formula,axiom,![X]:(p(X)=>?[Y]:q(Y))).")
     (let $cnf
          (compact-test:observe $program $context (EBNF:Parser $handle $receipt)
            "cnf(c1,axiom,(~lives(X)|X=agatha)).")
       (let $thf-type
            (compact-test:observe $program $context
              (EBNF:Parser $handle $receipt)
              "thf(mix_type,type,mix:beverage>beverage).")
         (let $thf-formula
              (compact-test:observe $program $context
                (EBNF:Parser $handle $receipt)
                "thf(hot_mix,axiom,![B:beverage,S:syrup]:(hot@(mix@B@S))).")
           (let $right-application
                (compact-test:observe $program $context
                  (EBNF:Parser $handle $receipt)
                  "thf(right_application,axiom,f@(g@X)).")
             (let $left-application
                  (compact-test:observe $program $context
                    (EBNF:Parser $handle $receipt)
                    "thf(left_application,axiom,(f@g)@X).")
               (let $lexical
                    (compact-test:observe $program $context
                      (EBNF:Parser $handle $receipt)
                      "fof('X',axiom,p('X',X,$i,'$i',\"distinct value\",1/3,-2.5,2,123456789012345678901234567890)).fof(`Y,axiom,q).")
                 (let $families
                      (compact-test:observe-file $program $context
                        (EBNF:Parser $handle $receipt) $family-path)
                   (let $_ (bnf:close-ebnf-parser (EBNF:Parser $handle $receipt))
                     (compact-test:pack
                       $fof $cnf $thf-type $thf-formula
                       $right-application $left-application
                       $lexical $families))))))))))))

(= (compact-test:prepare $program $context
     (bnf-v1:grammar-input $document $authority))
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1
         (BNFStartSomeV1 $start $_span) $_definitions $_lexicals)
       (let $selected
            (bnf-internal:ebnf-transform
              (LangDef:EquationSources
                "langdef/bnf/ebnf_declaration_view_v1.metta"
                "langdef/bnf/ebnf_reachable_view_v1.metta")
              (LangDef:DeterministicCall ebnf-reach-v1:select $document $start))
         (compact-test:use $program $context
           (bnf:parser-from-ebnf-document $selected $authority))))
      ($fault (TptpCompactDeclarationFailure $fault)))))

(= (compact-test:run $program $source $document)
   (case (== $source
      "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
     ((True
       (let $lexical
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
                (bnf-v1:lexical-except
                  (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
         (let $context
              (langdef:run-deterministic-equations $program
                (LangDef:DeterministicCall tptp-read-v1:prepare $lexical))
           (compact-test:prepare $program $context
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-syn-v1:compose $lexical))))))
      (False TptpCompactSourceDigestMismatch))))

!(let ($source $document)
       (compact-test:document
         (tptp:extended-bnf-file->ast (system:arg 0)))
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_lowering_v1.metta"
            "langdef/tptp/official_lexical_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_observation_v1.metta"
            "langdef/tptp/official_syntax_records_v1.metta"))
     (case $program
       (((NativeHandle $_kind $_id)
         (let $answers (collapse
           (compact-test:run $program $source $document))
           (let $_ (langdef:close-deterministic-equations $program)
             (superpose $answers))))
        ($fault (TptpCompactProgramFailure $fault))))))
