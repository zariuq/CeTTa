!(import! &self ../../../lib/lib_bnf.metta)

(= (ebnf-test:one (LangDef:AuthoredParseAccepted $lang $binding $source ($cst))) one)
(= (ebnf-test:reject (LangDef:AuthoredParseRejected $lang $binding $source $offset $expected))
   (rejected $offset))

; File grammar loading and GLL/GLR agreement on the complete packed forest.
!(let $parser (bnf:parser-from-ebnf-file "tests/langdef/bnf/ebnf_arithmetic_v1.ebnf" "s")
   (case $parser
     (((EBNF:SourceParser $handle $binding $source $receipt)
        (let $yes (bnf:parse-ebnf-text-evidence $parser "12*(3+4)+5" 10000)
          (let $no (langdef:parse-authored-text $handle "1+*2")
            (let $_ (bnf:close-ebnf-parser $parser)
              (case $yes
                (((EBNF:SourceParse
                    (EBNF:EvidenceAccepted $b $s $digest $forest
                      (EBNF:DerivationsCons $tree (EBNF:DerivationsNil)))
                    $grammar-binding $grammar-source $lowering)
                   (EbnfArithmeticForestV1 (ebnf-test:reject $no)))
                 ($unexpected (EbnfUnexpectedForestV1 $unexpected))))))))
      ($unexpected (EbnfUnexpectedArithmeticV1 $unexpected)))))

; A self-reading smoke test, not the stronger fixed-point/forest theorem.
!(let $parser (bnf:parser-from-ebnf-file-with-authority
                "langdef/bnf/ebnf_self_v1.ebnf" (bnf:self-authority-v1))
   (case $parser
     (((EBNF:SourceParser $handle $binding $source $receipt)
        (let $parsed (langdef:parse-authored-file $handle "langdef/bnf/ebnf_self_v1.ebnf")
          (let $_ (bnf:close-ebnf-parser $parser)
            (EbnfSelfReadV1 (ebnf-test:one $parsed)))))
      ($unexpected (EbnfUnexpectedSelfParserV1 $unexpected)))))

; Genuine structured editing: keep the old span as an origin, change only
; the optional literal, and admit/lower the new value without source text.
!(case (bnf:document (bnf:read-ebnf-text "<s> ::= \"x\"?\n"))
   (((bnf-v1:document
       (bnf-v1:entries-cons
         (bnf-v1:rule $name
           (bnf-v1:expression
             (bnf-v1:alternatives-cons
               (bnf-v1:alternative
                 (bnf-v1:elements-cons
                   (ebnf-v1:optional (bnf-v1:literal $old $literal-span) $optional-span)
                   (bnf-v1:elements-nil)) $alternative-span)
               (bnf-v1:alternatives-nil)) $expression-span) $rule-span)
         (bnf-v1:entries-nil)) $document-span)
      (let $edited
           (bnf-v1:document
             (bnf-v1:entries-cons
               (bnf-v1:rule $name
                 (bnf-v1:expression
                   (bnf-v1:alternatives-cons
                     (bnf-v1:alternative
                       (bnf-v1:elements-cons
                         (ebnf-v1:optional (bnf-v1:literal (bnf:text "雪") $literal-span) $optional-span)
                         (bnf-v1:elements-nil)) $alternative-span)
                     (bnf-v1:alternatives-nil)) $expression-span) $rule-span)
               (bnf-v1:entries-nil)) $document-span)
        (case (bnf:parser-from-ebnf-document $edited (bnf:default-authority "s"))
          (((EBNF:Parser $handle $receipt)
             (let $yes (langdef:parse-authored-text $handle "雪")
               (let $no (langdef:parse-authored-text $handle "x")
                 (let $_ (langdef:close-authored-parser $handle)
                   (EbnfEditedV1 (ebnf-test:one $yes) (ebnf-test:reject $no))))))
           ($unexpected (EbnfUnexpectedEditV1 $unexpected))))))
    ($unexpected (EbnfUnexpectedDocumentV1 $unexpected))))

; Two identical edited spans are still two helper occurrences. Names may
; contain the helper prefix through the structured-data API.
(= (ebnf-test:span) (bnf-v1:source-span 0 0))
(= (ebnf-test:rule $name $elements)
   (bnf-v1:rule (bnf:text $name)
     (bnf-v1:expression
       (bnf-v1:alternatives-cons
         (bnf-v1:alternative $elements (ebnf-test:span))
         (bnf-v1:alternatives-nil)) (ebnf-test:span)) (ebnf-test:span)))
(= (ebnf-test:literal $string)
   (bnf-v1:literal (bnf:text $string) (ebnf-test:span)))
(= (ebnf-test:collision-document)
   (bnf-v1:document
     (bnf-v1:entries-cons
       (ebnf-test:rule "s"
         (bnf-v1:elements-cons
           (bnf-v1:reference (bnf:text "##0") (ebnf-test:span))
           (bnf-v1:elements-cons
             (ebnf-v1:optional (ebnf-test:literal "x") (ebnf-test:span))
             (bnf-v1:elements-cons
               (ebnf-v1:optional (ebnf-test:literal "x") (ebnf-test:span))
               (bnf-v1:elements-nil)))))
       (bnf-v1:entries-cons
         (ebnf-test:rule "##0" (bnf-v1:elements-cons (ebnf-test:literal "z") (bnf-v1:elements-nil)))
         (bnf-v1:entries-nil))) (ebnf-test:span)))
!(case (bnf:parser-from-ebnf-document (ebnf-test:collision-document) (bnf:default-authority "s"))
   (((EBNF:Parser $handle (EBNF:LoweredGrammar $original $authority $plain
       (ebnf-v1:origins-cons (ebnf-v1:helper-origin $a optional $owner-a $span-a)
         (ebnf-v1:origins-cons (ebnf-v1:helper-origin $b optional $owner-b $span-b) (ebnf-v1:origins-nil)))))
      (let $result (langdef:parse-authored-text $handle "zx")
        (let $_ (langdef:close-authored-parser $handle)
          (case $result
            (((LangDef:AuthoredParseAccepted $lang $binding $source ($left $right))
               (EbnfFreshOccurrencesV1 (== $a $b) (== $span-a $span-b)))
             ($unexpected (EbnfUnexpectedOccurrencesV1 $unexpected)))))))
    ($unexpected (EbnfUnexpectedCollisionV1 $unexpected))))

!(case (bnf:lower-ebnf-document (bnf-v1:document (bnf-v1:entries-nil) (bnf-v1:source-span 0 0)) 7)
   (((BNF:ReadRejected ebnf-input $reason $message) EbnfMalformedAuthorityRefusedV1)
    ($unexpected (EbnfUnexpectedAuthorityV1 $unexpected))))

; Exercise the lowering itself, bypassing source declaration prechecking:
; an unresolved user reference must not bind to a newly introduced helper.
!(let $doc
       (bnf-v1:document
         (bnf-v1:entries-cons
           (ebnf-test:rule "s"
             (bnf-v1:elements-cons
               (ebnf-v1:optional (ebnf-test:literal "x") (ebnf-test:span))
               (bnf-v1:elements-cons
                 (bnf-v1:reference (bnf:text "##0") (ebnf-test:span))
                 (bnf-v1:elements-nil))))
           (bnf-v1:entries-nil)) (ebnf-test:span))
   (case (bnf:lower-ebnf-document $doc (bnf:default-authority "s"))
     (((EBNF:LoweredGrammar $original $authority $plain $origins)
        (case (bnf:validate-document $plain $authority)
          (((BNF:StructuredGrammarRefused $checked $policy
              (BNFDiagnosticsConsV1 (BNFUnresolvedReferenceV1 $owner $name $span) BNFDiagnosticsNilV1))
             (EbnfUnresolvedNotCapturedV1 (bnf:text->string $owner) (bnf:text->string $name)))
           ($unexpected (EbnfUnexpectedCaptureV1 $unexpected)))))
      ($unexpected (EbnfUnexpectedLoweringV1 $unexpected)))))
