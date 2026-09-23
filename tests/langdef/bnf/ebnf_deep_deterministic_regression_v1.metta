!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

!(let $parser
      (bnf:parser-from-ebnf-file (system:arg 0) "s")
   (case $parser
     (((EBNF:SourceParser $handle $grammar-binding $grammar-source $receipt)
       (let $parsed (bnf:parse-ebnf-text $parser (system:arg 1))
         (let $closed (bnf:close-ebnf-parser $parser)
           (case $parsed
             (((EBNF:SourceParse
                 (EBNF:Accepted $input-binding $input-source $trees)
                 $returned-grammar-binding $returned-grammar-source
                 $grammar-receipt)
               EbnfDeepDeterministicAcceptedV1)
              ($_parse-failure
               EbnfDeepDeterministicRejectedV1))))))
      ($_build-failure
       EbnfDeepDeterministicBuildRejectedV1))))
