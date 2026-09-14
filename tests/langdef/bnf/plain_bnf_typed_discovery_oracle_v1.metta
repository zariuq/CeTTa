; Adapter for the independent declaration fixtures. The tests supply their
; inputs and expected ordered payloads unchanged, using the checked root.
(= (typed-oracle:run $input)
   (let $language (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
     (let $answers
          (collapse (gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $language $input))
       (let $_ (langdef:close-language $language)
         (superpose $answers)))))
