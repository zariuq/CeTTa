!(import! &self ../../../lib/lib_bnf.metta)

(= (ebnf-projection-test:run $grammar $word)
   (let $parser (bnf:parser-from-ebnf-text $grammar "s")
     (case $parser
       (((EBNF:SourceParser $handle $binding $source $receipt)
          (let $parsed (bnf:parse-ebnf-text $parser $word)
            (let $_ (bnf:close-ebnf-parser $parser)
              (case $parsed
                (((EBNF:SourceParse (EBNF:Accepted $b $s $trees) $gb $gs $r)
                   $trees)
                 ($failure $failure))))))
        ($failure $failure)))))

; Observations specify source choices and spans, not generated helper names
; or native enumeration order.
(= (ebnf-projection-test:empty-choice
     (EBNF:DerivationsCons
       (EBNF:Rule "s" $rs0 0 $as0 (EBNF:InputSpan 0 0)
         (EBNF:ElementsCons
           (EBNF:OptionalAbsent $i0 $s0 (EBNF:InputSpan 0 0)) (EBNF:ElementsNil)))
       (EBNF:DerivationsCons
         (EBNF:Rule "s" $rs1 0 $as1 (EBNF:InputSpan 0 0)
           (EBNF:ElementsCons
             (EBNF:OptionalPresent $i1 $s1 (EBNF:InputSpan 0 0)
               (EBNF:Literal "" (bnf-v1:source-span 8 10) (EBNF:InputSpan 0 0)))
             (EBNF:ElementsNil))) (EBNF:DerivationsNil))))
   (EbnfOptionalEmptyV1 (== $i0 $i1) (== $s0 $s1)
     (== $s0 (bnf-v1:source-span 8 11)) (== $rs0 $rs1) (== $as0 $as1)))
!(ebnf-projection-test:empty-choice (ebnf-projection-test:run "<s> ::= \"\"?\n" ""))

(= (ebnf-projection-test:pair
     (EBNF:Rule "s" $rs 0 $as $input
       (EBNF:ElementsCons
         (EBNF:Repetition one-or-more $id $span $whole
           (EBNF:IterationsCons
             (EBNF:Group $a $sa $choice-a $aa (EBNF:InputSpan 0 1) $ea)
             (EBNF:IterationsCons
               (EBNF:Group $b $sb $choice-b $ab (EBNF:InputSpan 1 2) $eb)
               (EBNF:IterationsNil)))) (EBNF:ElementsNil))))
   (pair $choice-a $choice-b))
(= (ebnf-projection-test:count-pair $pair (EBNF:DerivationsNil)) 0)
(= (ebnf-projection-test:count-pair $pair (EBNF:DerivationsCons $tree $rest))
   (+ (if (== $pair (ebnf-projection-test:pair $tree)) 1 0)
      (ebnf-projection-test:count-pair $pair $rest)))
!(let $trees (ebnf-projection-test:run "<s> ::= (\"x\" | \"x\")+\n" "xx")
   (EbnfChoiceMultiplicityV1
     (ebnf-projection-test:count-pair (pair 0 0) $trees)
     (ebnf-projection-test:count-pair (pair 0 1) $trees)
     (ebnf-projection-test:count-pair (pair 1 0) $trees)
     (ebnf-projection-test:count-pair (pair 1 1) $trees)))

(= (ebnf-projection-test:append LNil $tail) $tail)
(= (ebnf-projection-test:append (LCons $head $rest) $tail)
   (LCons $head (ebnf-projection-test:append $rest $tail)))
(= (ebnf-projection-test:leaves (EBNF:Rule $name $rs $choice $as $input $elements))
   (ebnf-projection-test:leaves $elements))
(= (ebnf-projection-test:leaves (EBNF:Group $id $span $choice $as $input $elements))
   (ebnf-projection-test:leaves $elements))
(= (ebnf-projection-test:leaves (EBNF:Reference $name $span $input $tree))
   (ebnf-projection-test:leaves $tree))
(= (ebnf-projection-test:leaves (EBNF:ElementsNil)) LNil)
(= (ebnf-projection-test:leaves (EBNF:IterationsNil)) LNil)
(= (ebnf-projection-test:leaves (EBNF:ElementsCons $head $rest))
   (ebnf-projection-test:append (ebnf-projection-test:leaves $head) (ebnf-projection-test:leaves $rest)))
(= (ebnf-projection-test:leaves (EBNF:IterationsCons $head $rest))
   (ebnf-projection-test:append (ebnf-projection-test:leaves $head) (ebnf-projection-test:leaves $rest)))
(= (ebnf-projection-test:leaves (EBNF:Repetition $kind $id $span $input $iterations))
   (ebnf-projection-test:leaves $iterations))
(= (ebnf-projection-test:leaves (EBNF:OptionalAbsent $id $span $input)) LNil)
(= (ebnf-projection-test:leaves (EBNF:OptionalPresent $id $span $input $body))
   (ebnf-projection-test:leaves $body))
(= (ebnf-projection-test:leaves (EBNF:Literal $text $span $input))
   (LCons (literal $text $input) LNil))
(= (ebnf-projection-test:leaves (EBNF:Lexical $name $label $origin $input $scalar))
   (LCons (lexical $name $label $input $scalar) LNil))
(= (ebnf-projection-test:one-leaves (EBNF:DerivationsCons $tree (EBNF:DerivationsNil)))
   (ebnf-projection-test:leaves $tree))
!(EbnfRecursiveLeavesV1 (ebnf-projection-test:one-leaves
    (ebnf-projection-test:run "<s> ::= \"(\" <s>* \")\"\n" "(()())")))
!(EbnfUnicodeLeavesV1 (ebnf-projection-test:one-leaves
    (ebnf-projection-test:run "<s> ::= (\"λ\" | \"雪\")+ \"!\"?\n" "λ雪!")))

; Lexical leaves remain distinct from literals; their profile label and
; Unicode scalar are retained by the authored projection.
!(let $parser (bnf:parser-from-ebnf-text-with-authority
                "<document> ::= <name-char>+\n" (bnf:self-authority-v1))
   (let $result (bnf:parse-ebnf-text $parser "xy")
     (let $_ (bnf:close-ebnf-parser $parser)
       (case $result
         (((EBNF:SourceParse (EBNF:Accepted $b $s $trees) $gb $gs $receipt)
            (EbnfLexicalLeavesV1 (ebnf-projection-test:one-leaves $trees)))
          ($failure $failure))))))

; Self-reading traverses the same projected file API, not just the native
; parser. This is an executable smoke test, not a fixed-point theorem.
!(let $parser (bnf:parser-from-ebnf-file-with-authority
                "langdef/bnf/ebnf_self_v1.ebnf" (bnf:self-authority-v1))
   (let $result (bnf:parse-ebnf-file $parser "langdef/bnf/ebnf_self_v1.ebnf")
     (let $_ (bnf:close-ebnf-parser $parser)
       (case $result
         (((EBNF:SourceParse
             (EBNF:Accepted $b $s (EBNF:DerivationsCons
               (EBNF:Rule "document" $rs 0 $as (EBNF:InputSpan 0 $end) $elements)
               (EBNF:DerivationsNil))) $gb $gs $receipt)
            (EbnfProjectedSelfReadV1 (> $end 0)))
          ($failure $failure))))))

(= (ebnf-projection-test:reject (Error $op $detail)) EbnfProjectionRefusedV1)
(= (ebnf-projection-test:receipt)
   (bnf:lower-ebnf-document
     (bnf:document (bnf:read-ebnf-text "<s> ::= \"x\"\n")) (bnf:default-authority "s")))
; Unknown production, wrong extent, extra child, and non-CST input.
!(ebnf-projection-test:reject (bnf:project-ebnf-trees (ebnf-projection-test:receipt)
    ((CstRuleV1 "not-a-production" 0 1))))
!(ebnf-projection-test:reject (bnf:project-ebnf-trees (ebnf-projection-test:receipt)
    ((CstRuleV1 "s#" 0 0))))
!(ebnf-projection-test:reject (bnf:project-ebnf-trees (ebnf-projection-test:receipt)
    ((CstRuleV1 "s#" 0 1 (CstRuleV1 "s#" 0 1)))))
!(ebnf-projection-test:reject (bnf:project-ebnf-trees (ebnf-projection-test:receipt)
    (42)))
; Projection of a deliberately supplied duplicate tuple preserves both
; occurrences; it is not a set conversion or an independent parse check.
!(case (bnf:project-ebnf-trees (ebnf-projection-test:receipt)
          ((CstRuleV1 "s#" 0 1) (CstRuleV1 "s#" 0 1)))
   (((EBNF:DerivationsCons $a (EBNF:DerivationsCons $b (EBNF:DerivationsNil)))
      (EbnfDuplicateCstOccurrencesV1 (== $a $b)))
    ($failure $failure)))

; Exercise the two structural C primitives independently of a grammar.
(= (ebnf-projection-test:primitive $call)
   (bnf-internal:ebnf-transform
     (LangDef:EquationSources "langdef/bnf/ebnf_derivation_projection_v1.metta") $call))
!(EbnfExpressionItemsV1 (ebnf-projection-test:primitive
    (LangDef:DeterministicCall langdef:expression->list (tag 7 (nested "雪") 7))))
!(EbnfEmptyExpressionV1 (ebnf-projection-test:primitive
    (LangDef:DeterministicCall langdef:expression->list ())))
!(EbnfGroundEqualityV1
   (ebnf-projection-test:primitive (LangDef:DeterministicCall langdef:ground-equal (a 1) (a 1)))
   (ebnf-projection-test:primitive (LangDef:DeterministicCall langdef:ground-equal (a 1) (a "1")))
   (ebnf-projection-test:primitive (LangDef:DeterministicCall langdef:ground-equal (a 1) (a 1 1))))
!(ebnf-projection-test:reject (ebnf-projection-test:primitive
    (LangDef:DeterministicCall langdef:expression->list 7)))
!(ebnf-projection-test:reject (ebnf-projection-test:primitive
    (LangDef:DeterministicCall langdef:ground-equal $open 7)))

!(case (bnf:project-ebnf-trees (ebnf-projection-test:receipt) ())
   (((EBNF:DerivationsNil) EbnfEmptyCstTupleV1)
    ($failure $failure)))

; Identical edited data at two grammar positions is still two occurrences.
(= (ebnf-projection-test:edited-span) (bnf-v1:source-span 0 0))
(= (ebnf-projection-test:edited-optional)
   (ebnf-v1:optional (bnf-v1:literal (bnf:text "x") (ebnf-projection-test:edited-span))
     (ebnf-projection-test:edited-span)))
(= (ebnf-projection-test:identity (EBNF:OptionalAbsent $id $span $input)) (pair $id $span))
(= (ebnf-projection-test:identity (EBNF:OptionalPresent $id $span $input $body)) (pair $id $span))
(= (ebnf-projection-test:identities
     (EBNF:Rule "s" $rs 0 $as $input
       (EBNF:ElementsCons $a (EBNF:ElementsCons $b (EBNF:ElementsNil)))))
   (let (pair $ia $sa) (ebnf-projection-test:identity $a)
     (let (pair $ib $sb) (ebnf-projection-test:identity $b)
       (distinct-occurrences (== $ia $ib) (== $sa $sb)))))
!(let $document
       (bnf-v1:document
         (bnf-v1:entries-cons
           (bnf-v1:rule (bnf:text "s")
             (bnf-v1:expression
               (bnf-v1:alternatives-cons
                 (bnf-v1:alternative
                   (bnf-v1:elements-cons (ebnf-projection-test:edited-optional)
                     (bnf-v1:elements-cons (ebnf-projection-test:edited-optional)
                       (bnf-v1:elements-nil))) (ebnf-projection-test:edited-span))
                 (bnf-v1:alternatives-nil)) (ebnf-projection-test:edited-span))
             (ebnf-projection-test:edited-span))
           (bnf-v1:entries-nil)) (ebnf-projection-test:edited-span))
   (let $parser (bnf:parser-from-ebnf-document $document (bnf:default-authority "s"))
     (let $result (bnf:parse-ebnf-text $parser "x")
       (let $_ (bnf:close-ebnf-parser $parser)
         (case $result
           (((EBNF:Parse (EBNF:Accepted $b $s
               (EBNF:DerivationsCons $a (EBNF:DerivationsCons $c (EBNF:DerivationsNil)))) $receipt)
              (EbnfEditedOccurrenceProjectionV1
                (ebnf-projection-test:identities $a) (ebnf-projection-test:identities $c)))
            ($failure $failure)))))))

; An uninterpreted projection or a failed projection is never acceptance.
!(case (bnf-internal:ebnf-observation 7
          (LangDef:AuthoredParseAccepted Test "binding" "source" ((CstRuleV1 "s#" 0 1))))
   (((BNF:ReadFault ebnf-derivation-projection $detail) EbnfBadReceiptNotAcceptedV1)
    ($failure $failure)))
!(case (bnf-internal:ebnf-observation (ebnf-projection-test:receipt)
          (LangDef:AuthoredParseEvidenceAccepted Test "binding" "source" "digest" TestForest
            ((CstRuleV1 "wrong" 0 1))))
   (((BNF:ReadFault ebnf-derivation-projection $detail) EbnfBadTreeNotAcceptedV1)
    ($failure $failure)))
