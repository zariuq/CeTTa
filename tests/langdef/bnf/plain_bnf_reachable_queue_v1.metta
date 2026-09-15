!(import! &self ../../../lib/lib_bnf.metta)

; Ordered names are observable analysis data. A stack or an incorrectly
; reversed FIFO can accept the same grammar while changing this result.
(= (fifo:reachable (BNF:ReadValue (BNF:GrammarDocument $_ $_ $document)))
   (let (BNF:StructuredGrammarAdmitted $_ $_
          (BNFSemanticValidationAcceptedV1 $_ $_ $_
            (BNFGrammarAnalysisV1 $reachable $_ $_)))
        (bnf:validate-document $document (bnf:default-authority "s"))
     $reachable))

(= (fifo:expected)
   (quote (BNFNamesConsV1 (bnf-v1:text-cons 115 (bnf-v1:text-nil))
     (BNFNamesConsV1 (bnf-v1:text-cons 97 (bnf-v1:text-nil))
       (BNFNamesConsV1 (bnf-v1:text-cons 98 (bnf-v1:text-nil))
         (BNFNamesConsV1 (bnf-v1:text-cons 99 (bnf-v1:text-nil))
           (BNFNamesConsV1 (bnf-v1:text-cons 100 (bnf-v1:text-nil)) BNFNamesNilV1)))))))

!(if (== (fifo:reachable (bnf:read-text
       "<s> ::= <a> <b> <b>\n<a> ::= <c> |\n<b> ::= <d> | <c>\n<c> ::= <s> |\n<d> ::=\n"))
     (fifo:expected)) FifoDiscoveryOrderPreservedV1 FifoDiscoveryOrderMismatchV1)

; The accumulator must retain both occurrences, even when their payloads
; are equal, and must append onto the existing reverse back.
!(gslt:fn:BNFReachableReverseV1:110
    (BNFNamesConsV1 a (BNFNamesConsV1 a BNFNamesNilV1))
    (BNFNamesConsV1 b BNFNamesNilV1))
!(gslt:fn:BNFReachableReverseV1:110
    (BNFNamesConsV1 a (BNFNamesConsV1 c BNFNamesNilV1))
    (BNFNamesConsV1 b BNFNamesNilV1))
!(gslt:fn:BNFReachableReverseV1:110 BNFNamesNilV1 (BNFNamesConsV1 b BNFNamesNilV1))
