; Independently executable authored compact-record oracle.
; Compose parser + EBNF derivation forest + tptp-rec-v1. Not the default
; problem-file path. The records GSLT path is substituted for the mutated oracle.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

(= (auth:document
     (TPTP:StageValue extended-bnf-ast (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))
(= (auth:one-root $result)
   (case $result
     (((LCons (LCons $root $_more) $_rest) $root)
      ($fault (TptpAuthoredRootFailure $fault)))))
(= (auth:observe-text $program $context (EBNF:Parser $handle $receipt) $text)
   (case (langdef:parse-authored-text $handle $text)
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (langdef:run-deterministic-equations $program
         (LangDef:DeterministicCall tptp-rec-v1:node
           (auth:one-root
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-read-v1:roots $context
                 (bnf:project-ebnf-trees $receipt $trees)))))))
      ($fault (TptpAuthoredParseFailure $fault)))))
(= (auth:observe-file $program $context (EBNF:Parser $handle $receipt) $path)
   (case (langdef:parse-authored-file $handle $path)
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (langdef:run-deterministic-equations $program
         (LangDef:DeterministicCall tptp-rec-v1:node
           (auth:one-root
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-read-v1:roots $context
                 (bnf:project-ebnf-trees $receipt $trees)))))))
      ($fault (TptpAuthoredParseFailure $path $fault)))))
(= (auth:emit $tag $records)
   (println! (TptpDump $tag $records)))
(= (auth:dump $program $context $parser)
   (let $_ (auth:emit empty (auth:observe-text $program $context $parser ""))
     (let $_ (auth:emit fof-atom
               (auth:observe-text $program $context $parser "fof(a,axiom,p)."))
       (let $_ (auth:emit quoted
                 (auth:observe-text $program $context $parser
                   "fof('X',axiom,p(X,'X'))."))
         (let $_ (auth:emit lexical-kinds
                   (auth:observe-text $program $context $parser
                     "fof(42,axiom,p(42,1/3,-2.5,\"distinct value\"))."))
           (let $_ (auth:emit cnf-or
                   (auth:observe-text $program $context $parser
                     "cnf(c,axiom,(~p(X) | q(X)))."))
           (let $_ (auth:emit qualified
                     (auth:observe-text $program $context $parser
                       "include('Axioms/A.ax',[a],'space')."))
             (let $_ (auth:emit puz001-1
                       (auth:observe-file $program $context $parser (system:arg 1)))
               (let $_ (auth:emit puz001+1
                         (auth:observe-file $program $context $parser (system:arg 2)))
                 (let $_ (auth:emit syn000_1
                           (auth:observe-file $program $context $parser (system:arg 3)))
                   (let $_ (auth:emit puz140
                             (auth:observe-file $program $context $parser (system:arg 4)))
                     (let $_ (auth:emit syn000_4
                               (auth:observe-file $program $context $parser (system:arg 5)))
                       (let $_ (auth:emit families
                                 (auth:observe-file $program $context $parser (system:arg 6)))
                         (println! TptpAuthoredDumpDone))))))))))))))
(= (auth:use $program $context $parser)
   (case $parser
     (((EBNF:Parser $handle $receipt)
        (let $_ (auth:dump $program $context (EBNF:Parser $handle $receipt))
          (bnf:close-ebnf-parser $parser)))
      ($fault (println! (TptpAuthoredPreparationFailure $fault))))))
(= (auth:prepare $program $context (bnf-v1:grammar-input $document $authority))
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1 (BNFStartSomeV1 $start $_span) $_definitions $_lexicals)
       (let $selected
            (bnf-internal:ebnf-transform
              (LangDef:EquationSources
                "langdef/bnf/ebnf_declaration_view_v1.metta"
                "langdef/bnf/ebnf_reachable_view_v1.metta")
              (LangDef:DeterministicCall ebnf-reach-v1:select $document $start))
         (auth:use $program $context (bnf:parser-from-ebnf-document $selected $authority))))
      ($fault (println! (TptpAuthoredDeclarationFailure $fault))))))
(= (auth:run $program $source $document)
   (case (== $source "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
     ((True
       (let $lexical
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
                (bnf-v1:lexical-except (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
         (let $context (langdef:run-deterministic-equations $program
                        (LangDef:DeterministicCall tptp-read-v1:prepare $lexical))
           (auth:prepare $program $context
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-syn-v1:compose $lexical))))))
      (False (println! TptpAuthoredSourceDigestMismatch)))))
!(let ($source $document)
       (auth:document (tptp:extended-bnf-file->ast (system:arg 0)))
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
         (let $_ (auth:run $program $source $document)
           (langdef:close-deterministic-equations $program)))
        ($fault (println! (TptpAuthoredProgramFailure $fault)))))))
