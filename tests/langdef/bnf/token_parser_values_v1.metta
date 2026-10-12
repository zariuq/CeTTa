!(import! &self langdef)

(= (token-demo:syntax $policy)
   (langdef:syntax-gslt
     (GSLTLanguageDefWireV1 "token-example"
       (LCons (TypeDecl "Root" CarrierAst)
         (LCons (TypeDecl "Word" CarrierTokenRaw) LNil))
       (LCons (GrammarRule "Wrap" "Root"
         (LCons (TermSimple "word" (TBase "Word")) LNil)
         (LCons (SyntaxTerminal "word") (LCons (SyntaxNonTerminal "word") LNil)) EvalNone)
         (LCons (GrammarRule "Wrap" "Root"
           (LCons (TermSimple "word" (TBase "Word")) LNil)
           (LCons (SyntaxTerminal "word") (LCons (SyntaxNonTerminal "word") LNil)) EvalNone)
           LNil)) LNil LNil)
     (GSLTParserProfileLayerV2 "token-example" "Root" LNil LNil
       (ParserTokenLayerV1 $policy
         (LCons (RegularTokenDeclV1 "Word" WordToken (rx-char (cp 97))
                  TokenTextV1 (SyntaxOriginV1 "token-example" 0))
           (LCons (RegularTriviaDeclV1 WhiteSpace
                    (rx-seq (rx-char (cp 32)) (rx-star (rx-char (cp 32))))
                    (SyntaxOriginV1 "token-example" 1)) LNil))
         (LCons (ParserBlockCommentV1 "{-" "-}" (SyntaxOriginV1 "token-example" 2)) LNil)
         (LCons (ParserProductionMetadataV1 (SyntaxOriginV1 "token-example" 3)
                  ParserVisibleV1 (BnfcConstructorV1 "Wrap" "Root" (LCons "Word" LNil)))
           (LCons (ParserProductionMetadataV1 (SyntaxOriginV1 "token-example" 4)
                    ParserVisibleV1 (BnfcConstructorV1 "Wrap" "Root" (LCons "Word" LNil))) LNil))
         (LCons "Root" LNil)))
     $policy
     (SyntaxOriginsV1
       (LCons (SyntaxOriginV1 "token-types" 0)
         (LCons (SyntaxOriginV1 "token-types" 1) LNil))
       (LCons (SyntaxOriginV1 "token-example" 3)
         (LCons (SyntaxOriginV1 "token-example" 4) LNil)) LNil LNil)))

(= (token-demo:parser $policy)
   (let $syntax (token-demo:syntax $policy)
     (langdef:load-syntax-gslt-parser $syntax)))

!(let $parser (token-demo:parser (SyntaxLexicalPolicyV1 bnfc-documented))
   (let $answer (langdef:parse-authored-text-evidence $parser " worda " 2000)
     (let $_closed (langdef:close-authored-parser $parser)
       (case $answer
         (((LangDef:AuthoredParseEvidenceAccepted $_name $_binding $_input $_digest
             (pf-v1 $_entry $_extent $_roots $_nodes $_choices) ($value))
            (TokenCheckV1 documented yes))
          ($other (TokenCheckV1 documented no $other)))))))

!(let $parser (token-demo:parser (SyntaxLexicalPolicyV1 bnfc-2.9.6.3-c-flex-2.6.4))
   (let $answer (langdef:parse-authored-text $parser "word{- not nested -}a")
     (let $_closed (langdef:close-authored-parser $parser)
       (case $answer
         (((LangDef:AuthoredParseAccepted $_name $_binding $_input ($value))
            (TokenCheckV1 c-flex yes))
          ($other (TokenCheckV1 c-flex no $other)))))))

!(let $parser (token-demo:parser (SyntaxLexicalPolicyV1 bnfc-documented))
   (let $answer (langdef:parse-authored-text $parser "wordb")
     (let $_closed (langdef:close-authored-parser $parser)
       (case $answer
         (((LangDef:AuthoredLexicalRefusedV1 $_name $_binding $_input
             (SyntaxLexicalPolicyV1 bnfc-documented)
             (ParserLexicalWitnessV1 1 4 4 $_detail))
            (TokenCheckV1 positioned-refusal yes))
          ($other (TokenCheckV1 positioned-refusal no $other)))))))

!(let $parser (token-demo:parser (SyntaxLexicalPolicyV1 bnfc-documented))
   (case (langdef:close-authored-parser $parser)
     ((True (TokenCheckV1 close yes))
      ($other (TokenCheckV1 close no $other)))))

!(let $syntax (token-demo:syntax (SyntaxLexicalPolicyV1 bnfc-documented))
   (case (langdef:inspect-syntax-gslt $syntax)
     (((SyntaxGSLTInspectionV1 $_language
         (GSLTParserProfileLayerV2 $_name $_start $_classes $_states
           (ParserTokenLayerV1 (SyntaxLexicalPolicyV1 bnfc-documented)
             $_declarations $_comments
             (LCons (ParserProductionMetadataV1 (SyntaxOriginV1 "token-example" 3)
                       $_visible1 $_projection1)
               (LCons (ParserProductionMetadataV1 (SyntaxOriginV1 "token-example" 4)
                         $_visible2 $_projection2) LNil)) $_entries))
         $_policy $_origins)
       (TokenCheckV1 distinct-occurrences yes))
      ($other (TokenCheckV1 distinct-occurrences no $other)))))

!(let $syntax (token-demo:syntax (SyntaxLexicalPolicyV1 bnfc-documented))
   (let $joined (langdef:join-syntax-gslts "token-joined" "Root" $syntax $syntax)
     (let $parser (langdef:load-syntax-gslt-parser $joined)
       (let $answer (langdef:parse-authored-text-evidence $parser "worda" 2000)
         (let $_closed (langdef:close-authored-parser $parser)
           (case $answer
             (((LangDef:AuthoredParseEvidenceAccepted $_name $_binding $_input $_digest
                 (pf-v1 $_entry $_extent $_roots $_nodes $_choices)
                 ((LbnfAstNodeV1 "Root" "Wrap" $_span $_children)))
                (TokenCheckV1 repeated-join yes))
              ($other (TokenCheckV1 repeated-join no $other)))))))))

!(let $first (token-demo:syntax (SyntaxLexicalPolicyV1 bnfc-documented))
   (let $second (token-demo:syntax (SyntaxLexicalPolicyV1 bnfc-2.9.6.3-c-flex-2.6.4))
     (case (langdef:join-syntax-gslts "token-mismatch" "Root" $first $second)
       (((SyntaxGSLTRefusedV1 policy-mismatch
           (SyntaxPolicyMismatchV1 (SyntaxLexicalPolicyV1 bnfc-documented)
             (SyntaxLexicalPolicyV1 bnfc-2.9.6.3-c-flex-2.6.4)))
          (TokenCheckV1 policy-mismatch yes))
        ($other (TokenCheckV1 policy-mismatch no $other))))))
