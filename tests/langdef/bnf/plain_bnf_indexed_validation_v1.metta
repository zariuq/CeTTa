!(import! &self ../../../lib/lib_bnf.metta)

; Both entries run from ONE generated source composition. Compare complete
; ordered answer occurrences, including diagnostic payloads and source spans.
(= (indexed-validation:old $input)
   (let (gslt:result:BNFValidateGrammarV1:10 $result)
        (gslt:entry:BNFValidateGrammarV1:10 $input) $result))
(= (indexed-validation:new $input)
   (let (gslt:result:BNFValidateGrammarIndexedV1:10 $result)
        (gslt:entry:BNFValidateGrammarIndexedV1:10 $input) $result))
(= (indexed-validation:compare $marker $document $authority)
   (let $input (bnf-v1:grammar-input $document $authority)
     (case (bnf:admit-grammar-input $input)
       (((BNF:GrammarInputAdmitted $checked)
          (let $actual (collapse (indexed-validation:new $checked))
            (let $reference (collapse (indexed-validation:old $checked))
              (case $actual
                ((($one) (if (== $actual $reference) $marker
                            (IndexedValidationDifferent $marker $actual $reference)))
                 ($many (IndexedValidationMultiplicity $marker $many)))))))
        ($other (IndexedValidationInputRejected $marker $other))))))
(= (indexed-validation:read $marker $read $authority)
   (case $read
     (((BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
        (indexed-validation:compare $marker $document $authority))
      ($other (IndexedValidationReadRejected $marker $other)))))
(= (indexed-validation:text $marker $source $start)
   (indexed-validation:read $marker (bnf:read-text $source)
     (bnf:grammar-authority (bnf:text $start) (bnf:empty-lexical-environment))))

!(indexed-validation:text IndexedValidationLiteralV1 "<s> ::= \"x\"\n" "s")
!(indexed-validation:text IndexedValidationRecursiveV1 "<s> ::= \"(\" <s> \")\" <s> |\n" "s")
!(indexed-validation:text IndexedValidationDuplicateOrderV1
   "<s> ::= <missing>\n<a> ::= \"x\"\n<s> ::= \"y\"\n<a> ::= \"z\"\n<s> ::=\n" "unknown")
!(indexed-validation:text IndexedValidationEmptyV1 "% no definitions\n\n" "s")
!(indexed-validation:text IndexedValidationUnicodeV1
   "<s> ::= <a>\n<a> ::= \"λ雪😀\"\n<s> ::= \"x\"\n" "s")
!(indexed-validation:text IndexedValidationAmbiguousV1
   "<s> ::= <s> <s> | \"a\" | \"a\"\n" "s")
!(indexed-validation:read IndexedValidationSelfV1
   (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf") (bnf:self-authority-v1))
