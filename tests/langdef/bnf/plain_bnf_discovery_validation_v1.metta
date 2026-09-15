!(import! &self ../../../lib/lib_bnf.metta)

; Both entries run from ONE generated source composition. Compare complete
; ordered answer occurrences, including diagnostic payloads and source spans.
(= (discovery-validation:old $input)
   (let (gslt:result:BNFValidateGrammarV1:10 $result)
        (gslt:entry:BNFValidateGrammarV1:10 $input) $result))
(= (discovery-validation:new $input)
   (let (gslt:result:BNFValidateGrammarDiscoveryV1:10 $result)
        (gslt:entry:BNFValidateGrammarDiscoveryV1:10 $input) $result))
(= (discovery-validation:compare $marker $document $authority)
   (let $input (bnf-v1:grammar-input $document $authority)
     (case (bnf:admit-grammar-input $input)
       (((BNF:GrammarInputAdmitted $checked)
          (let $actual (collapse (discovery-validation:new $checked))
            (let $reference (collapse (discovery-validation:old $checked))
              (case $actual
                ((($one) (if (== $actual $reference) $marker
                            (DiscoveryValidationDifferent $marker $actual $reference)))
                 ($many (DiscoveryValidationMultiplicity $marker $many)))))))
        ($other (DiscoveryValidationInputRejected $marker $other))))))
(= (discovery-validation:read $marker $read $authority)
   (case $read
     (((BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
        (discovery-validation:compare $marker $document $authority))
      ($other (DiscoveryValidationReadRejected $marker $other)))))
(= (discovery-validation:text $marker $source $start)
   (discovery-validation:read $marker (bnf:read-text $source)
     (bnf:grammar-authority (bnf:text $start) (bnf:empty-lexical-environment))))

!(discovery-validation:text DiscoveryValidationLiteralV1 "<s> ::= \"x\"\n" "s")
!(discovery-validation:text DiscoveryValidationRecursiveV1 "<s> ::= \"(\" <s> \")\" <s> |\n" "s")
!(discovery-validation:text DiscoveryValidationDuplicateOrderV1
   "<s> ::= <missing>\n<a> ::= \"x\"\n<s> ::= \"y\"\n<a> ::= \"z\"\n<s> ::=\n" "unknown")
!(discovery-validation:text DiscoveryValidationEmptyV1 "% no definitions\n\n" "s")
!(discovery-validation:text DiscoveryValidationUnicodeV1
   "<s> ::= <a>\n<a> ::= \"λ雪😀\"\n<s> ::= \"x\"\n" "s")
!(discovery-validation:text DiscoveryValidationAmbiguousV1
   "<s> ::= <s> <s> | \"a\" | \"a\"\n" "s")
!(discovery-validation:read DiscoveryValidationSelfV1
   (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf") (bnf:self-authority-v1))
