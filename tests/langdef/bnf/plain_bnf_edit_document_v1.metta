!(import! &self ../../../lib/lib_bnf.metta)

; A MeTTa program can inspect and reconstruct the documented grammar data.
; Spans below are retained as edit origins, not positions in invented bytes.
(= (example:replace-single-literal
      (bnf-v1:document
        (bnf-v1:entries-cons
          (bnf-v1:rule $name
            (bnf-v1:expression
              (bnf-v1:alternatives-cons
                (bnf-v1:alternative
                  (bnf-v1:elements-cons
                    (bnf-v1:literal $_old $literal-span)
                    (bnf-v1:elements-nil)) $alternative-span)
                (bnf-v1:alternatives-nil)) $expression-span) $rule-span)
          (bnf-v1:entries-nil)) $document-span)
      $new-text)
   (bnf-v1:document
     (bnf-v1:entries-cons
       (bnf-v1:rule $name
         (bnf-v1:expression
           (bnf-v1:alternatives-cons
             (bnf-v1:alternative
               (bnf-v1:elements-cons
                 (bnf-v1:literal $new-text $literal-span)
                 (bnf-v1:elements-nil)) $alternative-span)
             (bnf-v1:alternatives-nil)) $expression-span) $rule-span)
       (bnf-v1:entries-nil)) $document-span))

; Inspect the source-derived name, edit a literal, and install only after
; the edited document has passed both structural and semantic admission.
!(let $original (bnf:document (bnf:read-text "<s> ::= \"x\"\n"))
   (let "s"
        (bnf:text->string
          (bnf:rule-name (bnf:entry (bnf:document-entries $original))))
     (let $edited (example:replace-single-literal $original (bnf:text "雪"))
       (let $parser
            (bnf:parser-from-document $edited (bnf:default-authority "s"))
         (let $accepted (langdef:parse-authored-text $parser "雪")
           (let $rejected (langdef:parse-authored-text $parser "x")
             (let $_ (langdef:close-authored-parser $parser)
               (case ($accepted $rejected)
                 ((((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                      $_binding $_source ($tree))
                    (LangDef:AuthoredParseRejected PlainBnfDenotedSyntaxV1
                      $_other-binding $_other-source 0 $_expected))
                   PlainBnfStructuredEditAcceptedV1)
                  ($unexpected (PlainBnfUnexpectedEditV1 $unexpected)))))))))))

; A ground value is not automatically a well-formed grammar document.
!(case (bnf:parser-from-document 7 (bnf:default-authority "s"))
   (((BNF:ReadRejected grammar-input $_reason $_message)
      PlainBnfMalformedEditRefusedV1)
    ($unexpected (PlainBnfUnexpectedMalformedEditV1 $unexpected))))

; Structured editing does not bypass semantic checks such as start lookup.
!(case
     (bnf:parser-from-document
       (bnf:document (bnf:read-text "<s> ::= \"x\"\n"))
       (bnf:default-authority "missing"))
   (((BNF:ParserNotLoaded $_diagnostics) PlainBnfEditedUnknownStartRefusedV1)
    ($unexpected (PlainBnfUnexpectedEditedStartV1 $unexpected))))

; A failed convenience conversion remains a fault, not a grammar rejection.
!(case (bnf:parser-from-document
         (bnf:document (bnf:read-text "<s> ::= \"x\"\n"))
         (bnf:default-authority 7))
   (((BNF:ReadFault text-codec $_detail) PlainBnfEditedAuthorityFaultRetainedV1)
    ($unexpected (PlainBnfUnexpectedEditedAuthorityV1 $unexpected))))
