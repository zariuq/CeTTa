!(import! &self ../../../lib/lib_bnf.metta)

; Exercise the actual public admission boundary, not a constructed success
; tag. Scalar domains do not constrain the unrelated origin integers.
(= (bnf-domain-test:authority $matcher $origin)
   (bnf:grammar-authority-from-string "s"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:text "character") "Character" $matcher "character-token"
           (bnf-v1:lexical-origin "domain-test" $origin))
         (bnf-v1:lexical-declarations-nil)))))

(= (bnf-domain-test:classify $result)
   (case $result
     (((BNF:StructuredGrammarAdmitted $_document $_authority $_validation)
        admitted)
      ((BNF:StructuredGrammarRefused $_document $_authority $_diagnostics)
        semantic-refusal)
      ((BNF:ReadRejected grammar-input $_reason $_message)
        structural-refusal)
      ($unexpected (unexpected $unexpected)))))

(= (bnf-domain-test:validate $matcher $origin)
   (bnf-domain-test:classify
     (bnf:validate-document
       (bnf:document (bnf:read-text "<s> ::= <character>\n"))
       (bnf-domain-test:authority $matcher $origin))))

; Unicode boundaries are integer values, not merely ground or Number values.
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 0 (bnf-v1:scalars-nil))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 1114111 (bnf-v1:scalars-nil))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 65.0 (bnf-v1:scalars-nil))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons character-code (bnf-v1:scalars-nil))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons -1 (bnf-v1:scalars-nil))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 1114112 (bnf-v1:scalars-nil))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 55296 (bnf-v1:scalars-nil))) 0)

; Both sides of the surrogate interval are valid and retain their order.
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 55295
       (bnf-v1:scalars-cons 57344 (bnf-v1:scalars-nil)))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 66
       (bnf-v1:scalars-cons 65 (bnf-v1:scalars-nil)))) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points
     (bnf-v1:scalars-cons 65
       (bnf-v1:scalars-cons 65 (bnf-v1:scalars-nil)))) 0)

; The empty complement is inhabited, unlike an empty points declaration.
!(bnf-domain-test:validate
   (bnf-v1:lexical-except (bnf-v1:scalars-nil)) 0)
!(bnf-domain-test:validate
   (bnf-v1:lexical-points (bnf-v1:scalars-nil)) 0)

; A scalar bound must not be propagated to an authored occurrence identifier.
; Match the retained authority as well as the successful result.
!(let $authority
       (bnf-domain-test:authority
         (bnf-v1:lexical-points
           (bnf-v1:scalars-cons 65 (bnf-v1:scalars-nil)))
         9223372036854775807)
   (case (bnf:validate-document
           (bnf:document (bnf:read-text "<s> ::= <character>\n"))
           $authority)
     (((BNF:StructuredGrammarAdmitted $_document $authority $_validation)
        large-origin-retained)
      ($unexpected (unexpected $unexpected)))))

; A returned-looking packet is not grammar data or a validation capability.
!(bnf-domain-test:classify
   (bnf:validate-document
     (BNFDeclarationValidationAcceptedV1
       start BNFDefinitionsNilV1 BNFLexicalMissingV1)
     (bnf:default-authority "s")))
