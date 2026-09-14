!(import! &self ../../../lib/lib_bnf.metta)

; Compare ordered answer occurrences through the real checked entry. The
; candidate composition contains the unchanged raw family exactly once.
(= (admitted-compiler:raw $input)
   (gslt:entry:BNFValidateGrammarV1:10 $input))
(= (admitted-compiler:checked $handle $input)
   (gslt:admitted-entry:BNFValidateGrammarV1:10 $handle $input))
(= (admitted-compiler:compare $handle $input $marker)
   (let $raw (collapse (admitted-compiler:raw $input))
     (let $typed (collapse (admitted-compiler:checked $handle $input))
       (if (== $raw $typed) $marker
           (AdmittedCompilerDifference $marker $raw $typed)))))
(= (admitted-compiler:input $values $origin)
   (bnf-v1:grammar-input
     (bnf:document (bnf:read-text "<s> ::= <character>\n"))
     (bnf:grammar-authority-from-string "s"
       (bnf-v1:lexical-environment
         (bnf-v1:lexical-declarations-cons
           (bnf-v1:lexical-declaration
             (bnf:text "character") "Character"
             (bnf-v1:lexical-points $values) "character-token"
             (bnf-v1:lexical-origin "typed-compiler" $origin))
           (bnf-v1:lexical-declarations-nil))))))
(= (admitted-compiler:pair $left $right)
   (admitted-compiler:input
     (bnf-v1:scalars-cons $left
       (bnf-v1:scalars-cons $right (bnf-v1:scalars-nil)))
     9223372036854775807))

!(let $handle (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
   (progn
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair 65 66) AdmittedCompilerIncreasingV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair 66 65) AdmittedCompilerDescendingV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair 65 65) AdmittedCompilerDuplicateV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair -9223372036854775808 9223372036854775807)
       AdmittedCompilerExtremesV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair 55295 57344) AdmittedCompilerUnicodeGapV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair 65.0 66.0) AdmittedCompilerFloatFallbackV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:input (bnf-v1:scalars-nil) 0)
       AdmittedCompilerEmptyV1))
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:input (bnf-v1:scalars-cons 65 BrokenTail) 0)
       AdmittedCompilerMalformedFallbackV1))
     (langdef:close-language $handle)))

; A real but different authority must select the raw family, not license
; the candidate with a merely well-shaped handle.
!(let $handle (langdef:load-language "langdef/bnf/plain_bnf_source_v1.metta")
   (progn
     (println! (admitted-compiler:compare $handle
       (admitted-compiler:pair 65 66) AdmittedCompilerAuthorityFallbackV1))
     (langdef:close-language $handle)))
