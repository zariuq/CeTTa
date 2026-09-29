!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

; Run in PeTTa with the generated BNF semantic-admission artifact loaded.
; Token definitions come from the official file, never copied into the test.
(= (lex-test:document
     (TPTP:StageValue extended-bnf-ast
       (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))
(= (lex-test:rules (bnf-v1:entries-nil)) 0)
(= (lex-test:rules (bnf-v1:entries-cons $_head $tail))
   (+ 1 (lex-test:rules $tail)))
(= (lex-test:classification $result)
   (case $result
     (((EBNF:Parse (EBNF:Accepted $_b $_s $_trees) $_r) accepted)
      ((EBNF:Parse (EBNF:Rejected $_b $_s $_offset $_expected) $_r) rejected)
      ($unexpected unexpected-parse-outcome))))
(= (lex-test:cases $parser LNil) 0)
(= (lex-test:cases $parser (LCons (TestCase $text $expected) $tail))
   (let $actual (lex-test:classification (bnf:parse-ebnf-text $parser $text))
     (case (== $actual $expected)
       ((True (+ 1 (lex-test:cases $parser $tail)))
        (False (TptpLexicalMismatch $text $expected $actual))))))
(= (lex-test:pairs $document $env LNil) 0)
(= (lex-test:pairs $document $env (LCons (LexicalPair $start $good $bad) $tail))
   (+ (lex-test:suite $document $env $start
        (LCons (TestCase $good accepted) (LCons (TestCase $bad rejected) LNil)))
      (lex-test:pairs $document $env $tail)))
(= (lex-test:suite $document $env $start $cases)
   (let $authority (bnf:grammar-authority-from-string $start $env)
     (case (bnf:validate-ebnf-declarations $document $authority)
       (((BNFDeclarationValidationAcceptedV1 $_start $_defs $_lexicals)
          (lex-test:admitted-suite $document $authority $start $cases))
        ($fault (TptpLexicalDeclarationFailure $start $fault))))))
(= (lex-test:admitted-suite $document $authority $start $cases)
   (let $selected
        (bnf-internal:ebnf-transform
          (LangDef:EquationSources
            "langdef/bnf/ebnf_declaration_view_v1.metta"
            "langdef/bnf/ebnf_reachable_view_v1.metta")
          (LangDef:DeterministicCall ebnf-reach-v1:select $document (bnf:text $start)))
     (lex-test:selected-suite (bnf:parser-from-ebnf-document $selected $authority) $start $cases)))
(= (lex-test:selected-suite $parser $start $cases)
     (case $parser
       (((EBNF:Parser $handle $receipt)
          (let $answers (collapse (lex-test:cases $parser $cases))
            (let $_ (bnf:close-ebnf-parser $parser) (superpose $answers))))
        ($unexpected (TptpLexicalParserConstructionFailed $start $unexpected)))))

(= (lex-test:check $original
     (tptp-lex-v1:lowered (bnf-v1:document $entries $span) $env $_origins $retained $_dot))
   (case ((== (lex-test:rules $entries) 56) (== $retained $original))
     (((True True)
       (let $document (bnf-v1:document $entries $span)
         (TptpOfficialLexicalToEbnfV1Summary
           (lex-test:suite $document $env "lower_word"
             (LCons (TestCase "cat_42" accepted)
             (LCons (TestCase "Cat" rejected)
             (LCons (TestCase "cat dog" rejected)
             (LCons (TestCase "" rejected) LNil)))))
           (lex-test:suite $document $env "single_quoted"
             (LCons (TestCase "'cat'" accepted)
             (LCons (TestCase "'a\\'b'" accepted)
             (LCons (TestCase "''" rejected)
             (LCons (TestCase "'unterminated" rejected) LNil)))))
           (lex-test:suite $document $env "rational"
             (LCons (TestCase "-123/456" accepted)
             (LCons (TestCase "1/0" rejected)
             (LCons (TestCase "01/2" rejected) LNil))))
           (lex-test:suite $document $env "comment_block"
             (LCons (TestCase "/*x**/" accepted)
             (LCons (TestCase "/*a\nb*/" accepted)
             (LCons (TestCase "/*a*/b" rejected)
             (LCons (TestCase "/*unterminated" rejected) LNil)))))
           (lex-test:pairs $document $env
             (LCons (LexicalPair "back_quoted" "`X_1" "`x")
             (LCons (LexicalPair "distinct_object" "\"a\\\"b\"" "\"unterminated")
             (LCons (LexicalPair "dollar_word" "$name" "$ name")
             (LCons (LexicalPair "dollar_dollar_word" "$$name" "$name")
             (LCons (LexicalPair "upper_word" "X_42" "x_42")
             (LCons (LexicalPair "real" "-1.25E+3" "1.")
             (LCons (LexicalPair "integer" "+123" "01")
             (LCons (LexicalPair "slash" "/" "\\")
             (LCons (LexicalPair "slosh" "\\" "/")
             (LCons (LexicalPair "plus" "+" "++")
             (LCons (LexicalPair "arrow" ">" ">=")
             (LCons (LexicalPair "less_sign" "<" "<=")
             (LCons (LexicalPair "hash" "#" "##")
             (LCons (LexicalPair "comment" "/* x */" "/**")
             (LCons (LexicalPair "comment_line" "% hi" "% hi\n") LNil))))))))))))))))
           ; The source [ . \\n ] class means literal dot or newline,
           ; not the wildcard interpretation supplied for a bare regex dot.
           (lex-test:suite $document $env "viewable_char"
             (LCons (TestCase "." accepted)
             (LCons (TestCase "\n" accepted)
             (LCons (TestCase "x" rejected) LNil)))))))
      ($failed (TptpLexicalSourceRetentionFailure $failed)))))

(= (lex-test:primitive-checks $program)
   (PrimitiveChecks
        (==
          (langdef:run-deterministic-equations $program
            (LangDef:DeterministicCall tptp-lex-test:compare -9223372036854775808 9223372036854775807)) "less")
          (==
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-test:compare 65 65)) "not-less")
            (==
              (case (langdef:run-deterministic-equations $program
                      (LangDef:DeterministicCall < 65.0 66))
                (((Error $_op $_detail) integer-type-refusal)
                 ($unexpected wrong-outcome))) integer-type-refusal)
         (==
           (langdef:run-deterministic-equations $program
             (LangDef:DeterministicCall tptp-lex-v1:union
               (bnf-v1:scalars-cons 65 (bnf-v1:scalars-cons 67 (bnf-v1:scalars-nil)))
               (bnf-v1:scalars-cons 65 (bnf-v1:scalars-cons 66 (bnf-v1:scalars-nil)))))
           (bnf-v1:scalars-cons 65 (bnf-v1:scalars-cons 66
             (bnf-v1:scalars-cons 67 (bnf-v1:scalars-nil)))))))

; Equal source spans do not authorize merging distinct class occurrences.
(= (lex-test:freshness-check $program)
   (let $matcher (bnf-v1:lexical-points (bnf-v1:scalars-cons 65 (bnf-v1:scalars-nil)))
     (let $span (tptp-ebnf-v1:source-span 0 1)
       (let (tptp-lex-v1:result (bnf-v1:reference $first $_span1) $state)
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:class $matcher $span
                (tptp-lex-v1:state (bnf-v1:text-nil) 0
                  (bnf-v1:lexical-declarations-nil) (tptp-lex-v1:origins-nil) $matcher "same-span")))
         (let (tptp-lex-v1:result (bnf-v1:reference $second $_span2) $_state2)
              (langdef:run-deterministic-equations $program
                (LangDef:DeterministicCall tptp-lex-v1:class $matcher $span $state))
           (== (== $first $second) False))))))

(= (lex-test:run $program $source $document)
   (case ((== $source "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
          (lex-test:primitive-checks $program) (lex-test:freshness-check $program))
     (((True (PrimitiveChecks True True True True) True)
        (lex-test:checked-run $program $source $document))
      ($failed (TptpLexicalUnitFailure $failed)))))
(= (lex-test:checked-run $program $source $document)
   (case
       ((==
          (langdef:run-deterministic-equations $program
            (LangDef:DeterministicCall tptp-lex-v1:range 65 67 (bnf-v1:scalars-nil)))
          (bnf-v1:scalars-cons 65 (bnf-v1:scalars-cons 66
            (bnf-v1:scalars-cons 67 (bnf-v1:scalars-nil)))))
          (==
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:range 67 65 (bnf-v1:scalars-nil)))
            (tptp-lex-v1:invalid-range 67 65)))
     (((True True) (lex-test:lower-and-check $program $source $document))
      ($failed (TptpLexicalRangeFailure $failed)))))
(= (lex-test:lower-and-check $program $source $document)
       (let $lexical
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
                (bnf-v1:lexical-except (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
         (let $_ (langdef:close-deterministic-equations $program)
           (lex-test:check $document $lexical))))

!(let ($source $document)
       (lex-test:document (tptp:extended-bnf-file->ast (system:arg 0)))
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_lowering_v1.metta"
            "langdef/tptp/official_lexical_to_ebnf_v1.metta"
            "tests/langdef/tptp/lexical_primitive_probe_v1.metta"))
     (lex-test:run $program $source $document)))
