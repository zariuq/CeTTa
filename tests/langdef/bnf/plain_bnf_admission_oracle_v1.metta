; Declaration admission over constructed grammar data, independently of readers.
; Empty alternatives keep the cases focused on ordered definitions and diagnostics.
(= (oracle:s) (bnf-v1:text-cons 115 (bnf-v1:text-nil)))
(= (oracle:a) (bnf-v1:text-cons 97 (bnf-v1:text-nil)))
(= (oracle:empty-expression $span)
   (bnf-v1:expression
     (bnf-v1:alternatives-cons
       (bnf-v1:alternative (bnf-v1:elements-nil) $span)
       (bnf-v1:alternatives-nil)) $span))
(= (oracle:rule $name $span)
   (bnf-v1:rule $name (oracle:empty-expression $span) $span))
(= (oracle:entries $second)
   (bnf-v1:entries-cons
     (oracle:rule (oracle:s) (bnf-v1:source-span 0 10))
     (bnf-v1:entries-cons
       (oracle:rule $second (bnf-v1:source-span 10 20))
       (bnf-v1:entries-nil))))
(= (oracle:input $entries)
   (bnf-v1:grammar-input
     (bnf-v1:document $entries (bnf-v1:source-span 0 20))
     (bnf-v1:grammar-authority
       (bnf-v1:start (oracle:s))
       (bnf-v1:lexical-environment (bnf-v1:lexical-declarations-nil)))))
; Constructed data may repeat an occurrence verbatim. Equal names and spans
; must not collapse two duplicate diagnostics into one. Comments/blanks do
; not alter which declaration is first.
(= (oracle:repeated-duplicates)
   (bnf-v1:entries-cons
     (oracle:rule (oracle:s) (bnf-v1:source-span 0 5))
     (bnf-v1:entries-cons
       (bnf-v1:comment (oracle:a) (bnf-v1:source-span 5 7))
       (bnf-v1:entries-cons
         (bnf-v1:blank (bnf-v1:source-span 7 10))
         (bnf-v1:entries-cons
           (oracle:rule (oracle:s) (bnf-v1:source-span 10 20))
           (bnf-v1:entries-cons
             (oracle:rule (oracle:s) (bnf-v1:source-span 10 20))
             (bnf-v1:entries-nil)))))))
(= (oracle:observe
     (gslt:result:BNFValidateGrammarV1:10
       (BNFDeclarationValidationAcceptedV1 $_start
         (BNFDefinitionsConsV1 (BNFDefinitionV1 $first $_e1 $span1)
           (BNFDefinitionsConsV1 (BNFDefinitionV1 $second $_e2 $span2)
             BNFDefinitionsNilV1)) $_lexicals)))
   (quote (Accepted $first $span1 $second $span2)))
(= (oracle:observe
     (gslt:result:BNFValidateGrammarV1:10
       (BNFDeclarationValidationRefusedV1 $diagnostics)))
   (quote (Refused $diagnostics)))
!(collapse (oracle:observe
   (gslt:entry:BNFValidateGrammarV1:10
     (oracle:input (oracle:entries (oracle:a))))))
!(collapse (oracle:observe
   (gslt:entry:BNFValidateGrammarV1:10
     (oracle:input (oracle:entries (oracle:s))))))
!(collapse (oracle:observe
   (gslt:entry:BNFValidateGrammarV1:10
     (oracle:input (bnf-v1:entries-nil)))))
!(collapse (oracle:observe
   (gslt:entry:BNFValidateGrammarV1:10
     (oracle:input (oracle:repeated-duplicates)))))
