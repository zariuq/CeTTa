; Private candidate resource contract: finite zero, one, and two-answer
; validation streams retain their occurrences and close their owned handle.
; Run with the corresponding source-regenerated program and candidate library.

(= (typed-owner:accepted
      (BNF:StructuredGrammarAdmitted $document $authority
        (BNFSemanticValidationAcceptedV1 $start $definitions $lexicals $analysis)))
   OwnedAnswersAcceptedV1)
(= (typed-owner:valid-answers $answers)
   (case $answers
     ((() OwnedAnswersAcceptedV1)
      (($one) (typed-owner:accepted $one))
      (($first $second)
         (if (== $first $second) (typed-owner:accepted $first) False))
      ($other False))))

!(let (BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
      (bnf:read-text "<s> ::= \"x\"\n")
   (let $authority (bnf:default-authority "s")
     (let $language (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
       (let $answers
            (collapse
              (bnf-typed-discovery:validate-owned-document $language $document $authority))
         (PlainBnfTypedDiscoveryOwnershipV1
           (size-atom $answers)
           (typed-owner:valid-answers $answers)
           (case (langdef:close-language $language)
             ((False OwnedHandleClosedV1)
              ($other (OwnedHandleUnexpectedV1 $other)))))))))
