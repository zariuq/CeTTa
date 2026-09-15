; Shared input construction for the eight-source discovery admission ablation.
; Load lib_bnf and its generated combined discovery candidate before this file.
(= (bnf-work:scalars $position $limit)
   (if (< $position $limit)
     (bnf-v1:scalars-cons $position (bnf-work:scalars (+ $position 1) $limit))
     (bnf-v1:scalars-nil)))
(= (bnf-work:input $count)
   (bnf-v1:grammar-input
     (bnf:document (bnf:read-text "<s> ::= <character>\n"))
     (bnf:grammar-authority-from-string "s"
       (bnf-v1:lexical-environment
         (bnf-v1:lexical-declarations-cons
           (bnf-v1:lexical-declaration
             (bnf:text "character") "Character"
             (bnf-v1:lexical-points (bnf-work:scalars 0 $count))
             "character-token" (bnf-v1:lexical-origin "work-probe" 0))
           (bnf-v1:lexical-declarations-nil))))))
(= (bnf-work:call raw $handle $input)
   (gslt:entry:BNFValidateGrammarDiscoveryV1:10 $input))
(= (bnf-work:call checked $handle $input)
   (gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $handle $input))
(= (bnf-work:run $route $count)
   (let $handle (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
     (let $input (bnf-work:input $count)
       (let $answers (collapse (bnf-work:call $route $handle $input))
         (progn
           (println! $answers)
           (println!
             (case $answers
               ((((gslt:result:BNFValidateGrammarDiscoveryV1:10
                     (BNFDeclarationValidationAcceptedV1 $_start $_defs $_lexicals)))
                   (AdmittedScalarWorkAccepted $count))
                ($other (AdmittedScalarWorkUnexpected $other)))))
           (langdef:close-language $handle))))))
