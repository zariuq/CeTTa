!(import! &self ../../../lib/lib_bnf.metta)

; Occurrence numbering is local to a receipt, not a public numbering policy.
(= (ebnf-diagnostic-test:visible (EBNF:Diagnostics $items))
   (ebnf-diagnostic-test:visible $items))
(= (ebnf-diagnostic-test:visible LNil) LNil)
(= (ebnf-diagnostic-test:visible (LCons $head $tail))
   (LCons (ebnf-diagnostic-test:visible $head) (ebnf-diagnostic-test:visible $tail)))
(= (ebnf-diagnostic-test:visible (EBNF:DefinitionDiagnostic $kind $owner $span))
   (definition $kind $owner $span))
(= (ebnf-diagnostic-test:visible
     (EBNF:ExpressionDiagnostic $kind $owner $operator $id $rule-span $span))
   (expression $kind $owner $operator $rule-span $span))

(= (ebnf-diagnostic-test:public $grammar)
   (case (bnf:parser-from-ebnf-text $grammar "s")
     (((EBNF:GraphRefused $projected $raw $receipt)
        (EbnfGraphDiagnosticsV1 (ebnf-diagnostic-test:visible $projected)
          (== $projected (bnf:project-ebnf-graph-diagnostics $receipt $raw))))
      ($unexpected (EbnfUnexpectedGraphOutcomeV1 $unexpected)))))

; The unused rule and each of its generated operators retain their own
; source occurrence; graph refusal policy is unchanged.
!(ebnf-diagnostic-test:public "<s> ::= \"x\"\n<dead> ::= (\"y\")?\n")
!(ebnf-diagnostic-test:public "<s> ::= (<s>)+\n")

; Edited nodes may have identical spans in different rules. Owner identity
; must come from traversal state, never a guess from span containment.
(= (ebnf-diagnostic-test:span) (bnf-v1:source-span 0 0))
(= (ebnf-diagnostic-test:rule $name)
   (bnf-v1:rule (bnf:text $name)
     (bnf-v1:expression
       (bnf-v1:alternatives-cons
         (bnf-v1:alternative
           (bnf-v1:elements-cons
             (ebnf-v1:optional
               (bnf-v1:literal (bnf:text "x") (ebnf-diagnostic-test:span))
               (ebnf-diagnostic-test:span))
             (bnf-v1:elements-nil)) (ebnf-diagnostic-test:span))
         (bnf-v1:alternatives-nil)) (ebnf-diagnostic-test:span))
     (ebnf-diagnostic-test:span)))
(= (ebnf-diagnostic-test:receipt)
   (bnf:lower-ebnf-document
     (bnf-v1:document
       (bnf-v1:entries-cons (ebnf-diagnostic-test:rule "left")
         (bnf-v1:entries-cons (ebnf-diagnostic-test:rule "right")
           (bnf-v1:entries-nil))) (ebnf-diagnostic-test:span))
     (bnf:default-authority "left")))

!(let $receipt (ebnf-diagnostic-test:receipt)
   (case $receipt
     (((EBNF:LoweredGrammar $original $authority $plain
         (ebnf-v1:origins-cons (ebnf-v1:helper-origin $a optional $owner-a $span-a)
           (ebnf-v1:origins-cons (ebnf-v1:helper-origin $b optional $owner-b $span-b)
             (ebnf-v1:origins-nil))))
        (case (bnf:project-ebnf-graph-diagnostics $receipt
                (BNFDiagnosticsConsV1 (BNFUnproductiveDefinitionV1 $b $span-b)
                  (BNFDiagnosticsConsV1 (BNFUnreachableDefinitionV1 $a $span-a)
                    (BNFDiagnosticsConsV1 (BNFUnproductiveDefinitionV1 $b $span-b)
                      BNFDiagnosticsNilV1))))
          (((EBNF:Diagnostics
              (LCons (EBNF:ExpressionDiagnostic unproductive "right" optional $ib $rb $sb)
                (LCons (EBNF:ExpressionDiagnostic unreachable "left" optional $ia $ra $sa)
                  (LCons (EBNF:ExpressionDiagnostic unproductive "right" optional $ic $rc $sc)
                    LNil))))
             (EbnfDiagnosticOwnersV1
               (== $ia $ib) (== $ib $ic) (== $sa $sb) (== $sb $sc)
               (== $ra $rb) (== $rb $rc)))
           ($unexpected (EbnfUnexpectedOwnersV1 $unexpected)))))
      ($unexpected (EbnfUnexpectedReceiptV1 $unexpected)))))

; Forward-compatible diagnostics remain explicit data and in order.
!(bnf:project-ebnf-graph-diagnostics (ebnf-diagnostic-test:receipt)
   (BNFDiagnosticsConsV1 (FutureGraphDiagnosticV1 "detail") BNFDiagnosticsNilV1))
!(bnf:project-ebnf-graph-diagnostics (ebnf-diagnostic-test:receipt) BNFDiagnosticsNilV1)
!(case (bnf:project-ebnf-graph-diagnostics 7 BNFDiagnosticsNilV1)
   (((BNF:ReadFault ebnf-lowering-receipt 7) EbnfDiagnosticBadReceiptV1)
    ($unexpected (EbnfUnexpectedBadReceiptV1 $unexpected))))
!(case (bnf:project-ebnf-graph-diagnostics (ebnf-diagnostic-test:receipt)
         (BNFDiagnosticsConsV1 (BNFUnreachableDefinitionV1 (bnf:text "s")) BNFDiagnosticsNilV1))
   (((Error $call $detail) EbnfDiagnosticMalformedV1)
    ($unexpected (EbnfUnexpectedMalformedV1 $unexpected))))

; Pre-lowering unresolved-reference diagnostics remain authored once,
; despite plus expansion copying its operand into two alternatives.
!(case (bnf:parser-from-ebnf-text "<s> ::= <missing>+\n" "s")
   (((BNF:ParserNotLoaded
       (BNFDiagnosticsConsV1 (BNFUnresolvedReferenceV1 $owner $name $span)
         BNFDiagnosticsNilV1))
      (EbnfDeclarationStillAuthoredV1 (bnf:text->string $owner)
        (bnf:text->string $name) $span))
    ($unexpected (EbnfUnexpectedDeclarationV1 $unexpected))))
