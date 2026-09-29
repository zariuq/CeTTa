!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

; Scannerless composition integration test, not a full reader-conformance gate.
(= (syntax-test:document
     (TPTP:StageValue extended-bnf-ast (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))
(= (syntax-test:classification $result)
   (case $result
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $_trees) accepted)
      ((LangDef:AuthoredParseRejected $_language $_binding $_source $_offset $_expected) rejected)
      ((LangDef:AuthoredParseIncomplete $_language $_binding $_source $outcome $_detail) (incomplete $outcome))
      ($other unexpected-parse-outcome))))
(= (syntax-test:cases $parser LNil) 0)
(= (syntax-test:cases $parser (LCons (SyntaxCase $text $expected) $tail))
   (let $actual (syntax-test:classification (langdef:parse-authored-text $parser $text))
     (case (== $actual $expected)
       ((True (+ 1 (syntax-test:cases $parser $tail)))
        (False (TptpSyntaxMismatch $text $expected $actual))))))
(= (syntax-test:file $parser $path)
   (let $actual (syntax-test:classification (langdef:parse-authored-file $parser $path))
     (case (== $actual accepted)
       ((True accepted)
        (False (TptpCorpusFileFailure $actual))))))
(= (syntax-test:use $parser)
   (case $parser
     (((EBNF:Parser $handle $receipt)
        (let $_ (println! TptpSyntaxParserPreparedV1)
          (let $answers
               (collapse (syntax-test:cases $handle
                 (LCons (SyntaxCase "" accepted)
                 (LCons (SyntaxCase "% initial comment\nfof(a,axiom,p)." accepted)
                 (LCons (SyntaxCase "cnf(c,axiom,(~p(X) | q(X)))." accepted)
                 (LCons (SyntaxCase "tff(t,type,f:$i>$i)." accepted)
                 (LCons (SyntaxCase "thf(h,axiom,(! [X:$i] : (p @ X)))." accepted)
                 (LCons (SyntaxCase "tpi(i,axiom,p)." accepted)
                 (LCons (SyntaxCase "tcf(c,axiom,! [X:$i] : p(X))." accepted)
                 (LCons (SyntaxCase "fof(a,axiom,(p ~| q))." accepted)
                 (LCons (SyntaxCase "fof(a,axiom,(p ~ | q))." rejected)
                 (LCons (SyntaxCase "include('Axioms/a.ax',[a,b])." accepted)
                 (LCons (SyntaxCase "fof(f,plain,p,inference(resolution,[status(thm)],[a,b]))." accepted)
                 (LCons (SyntaxCase "fof( a , axiom , ![X] : (p(X) => q(X)) )." accepted)
                 (LCons (SyntaxCase "f of(a,axiom,p)." rejected)
                 (LCons (SyntaxCase "fof(a,axiom,(p = > q))." rejected)
                 (LCons (SyntaxCase "fof(a,axiom,p)" rejected)
                 (LCons (SyntaxCase "fof(a,axiom,p))." rejected) LNil))))))))))))))))))
            (let $files
                 (CorpusFiles
                   (syntax-test:file $handle (system:arg 1))
                   (syntax-test:file $handle (system:arg 2))
                   (syntax-test:file $handle (system:arg 3)))
              (let $_ (bnf:close-ebnf-parser $parser)
                (TptpOfficialSyntaxCompositionV1Summary (superpose $answers) $files))))))
      ($fault (TptpSyntaxPreparationFailure $fault)))))
(= (syntax-test:prepare (bnf-v1:grammar-input $document $authority))
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1 (BNFStartSomeV1 $start $_span) $_definitions $_lexicals)
        (let $_ (println! TptpSyntaxDeclarationsAcceptedV1)
          (let $selected
               (bnf-internal:ebnf-transform
                 (LangDef:EquationSources
                   "langdef/bnf/ebnf_declaration_view_v1.metta"
                   "langdef/bnf/ebnf_reachable_view_v1.metta")
                 (LangDef:DeterministicCall ebnf-reach-v1:select $document $start))
            (syntax-test:use (bnf:parser-from-ebnf-document $selected $authority)))))
      ($fault (TptpSyntaxDeclarationFailure $fault)))))
(= (syntax-test:run $program $source $document)
   (case (== $source "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
     ((True (syntax-test:pinned-run $program $source $document))
      (False TptpSyntaxSourceDigestMismatchV1))))
(= (syntax-test:pinned-run $program $source $document)
     (let $lexical
          (langdef:run-deterministic-equations $program
            (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
              (bnf-v1:lexical-except (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
       (let $_ (println! TptpSyntaxLexicalLoweredV1)
         (let $input (langdef:run-deterministic-equations $program
                       (LangDef:DeterministicCall tptp-syn-v1:compose $lexical))
           (let $_ (langdef:close-deterministic-equations $program)
             (syntax-test:prepare $input))))))
!(let ($source $document)
       (syntax-test:document (tptp:extended-bnf-file->ast (system:arg 0)))
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_lowering_v1.metta"
            "langdef/tptp/official_lexical_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_to_ebnf_v1.metta"))
     (case $program
       (((NativeHandle $_kind $_id) (syntax-test:run $program $source $document))
        ($fault (TptpSyntaxProgramFailure $fault))))))
