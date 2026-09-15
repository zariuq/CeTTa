!(import! &self ../../../lib/lib_bnf.metta)

!(let $parser (bnf:parser-from-ebnf-text "<s> ::= (\"λ\" | \"雪\")+ \"!\"?\n" "s")
   (case $parser
     (((EBNF:SourceParser $handle $binding $source $receipt)
        (let $accepted (bnf:parse-ebnf-text $parser "λ雪!")
          (let $rejected (bnf:parse-ebnf-text $parser "!")
            (let $_ (bnf:close-ebnf-parser $parser)
              ($accepted $rejected)))))
      ($failure (EbnfPublicFailedV1 $failure)))))
