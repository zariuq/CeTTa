!(import! &self ../../../lib/lib_bnf.metta)

; These are propagation tests, not evidence of a real resource exhaustion
; or an ambiguous parse of the BNF metalanguage. Payloads remain unchanged.
(= (test:through-public-stages $read)
   (bnf:load-parser
     (bnf:denote
       (bnf:validate $read UnusedAuthority))))

!(test:through-public-stages
   (BNF:ReadAmbiguous binding source (first-cst second-cst first-cst)))
!(test:through-public-stages
   (BNF:ReadRejected source-syntax 7 ("::=" "<")))
!(test:through-public-stages
   (BNF:ReadRejected grammar-input invalid-start "invalid start shape"))
!(test:through-public-stages
   (BNF:ReadIncomplete binding source descriptor-limit pending))
!(test:through-public-stages
   (BNF:ReadFault source-parser "cannot read source"))
!(bnf:load-parser
   (BNF:DenotationFault (Error primitive "cannot construct result")))

; This case exercises an actual source parser rejection, retaining its exact
; offset and expected terminals through every remaining public stage.
!(let $read (bnf:read-text "<broken> := \"x\"\n")
   (case $read
     (((BNF:ReadRejected source-syntax $offset $expected)
        (case (test:through-public-stages $read)
          (((BNF:ReadRejected source-syntax $offset $expected)
             PlainBnfPublicSyntaxRejectionPreservedV1)
           ($unexpected (PlainBnfPublicOutcomeMismatchV1 $unexpected)))))
      ($unexpected (PlainBnfExpectedSourceRejectionV1 $unexpected)))))

; A real semantic refusal also stays a refusal rather than installing a
; parser. The empty input does not need a chosen start occurrence.
!(case
   (bnf:load-parser
     (bnf:denote
       (bnf:validate
         (bnf:read-text "")
         (bnf:grammar-authority
           (bnf-v1:text-cons 115 (bnf-v1:text-nil))
           (bnf:empty-lexical-environment)))))
   (((BNF:ParserNotLoaded $diagnostics)
      PlainBnfPublicSemanticRefusalPreservedV1)
    ($unexpected (PlainBnfExpectedSemanticRefusalV1 $unexpected))))

; Inject malformed intermediate data to exercise denotation's fault branch.
; This does not claim that semantic admission produced the malformed packet.
!(case
   (bnf:denote
     (BNF:SemanticGrammarAdmitted
       binding source malformed-document authority malformed-validation))
   (((BNF:DenotationFault $detail)
      (case (bnf:load-parser (BNF:DenotationFault $detail))
        (((BNF:ParserNotLoaded (BNF:DenotationFault $detail))
           PlainBnfPublicDenotationFaultPreservedV1)
         ($unexpected (PlainBnfExpectedDenotationRefusalV1 $unexpected)))))
    ($unexpected (PlainBnfExpectedDenotationFaultV1 $unexpected))))
