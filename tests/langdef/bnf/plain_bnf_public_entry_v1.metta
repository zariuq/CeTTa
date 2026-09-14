!(import! &self ../../../lib/lib_bnf.metta)

; String names and literal text use the same source-derived structured codec.
!(bnf:text "")
!(bnf:text "s")
!(bnf:text->string (bnf:text "λ雪😀"))
; Construct quote/backslash as data, independently of host reader escaping.
!(bnf:text (bnf:text->string
   (bnf-v1:text-cons 34 (bnf-v1:text-cons 92 (bnf-v1:text-nil)))))
!(case (bnf:text 7)
   (((BNF:ReadFault text-codec $_detail) PlainBnfStringTypeRefusedV1)
    ($unexpected (PlainBnfUnexpectedStringOutcomeV1 $unexpected))))

; One public sequence: grammar text -> parser -> structured source result.
; Close the parser after the observation, not before consuming it.
!(let $parser (bnf:parser-from-text "<s> ::= \"λ\" <s> |\n" "s")
   (case $parser
     (((NativeHandle $kind $identifier)
        (let $result (langdef:parse-authored-text $parser "λλ")
          (let $_ (langdef:close-authored-parser $parser)
            (case $result
              (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                  $_binding $_source ($tree))
                 PlainBnfPublicUnicodeParserAcceptedV1)
               ($unexpected (PlainBnfUnexpectedUnicodeParseV1 $unexpected)))))))
      ($unexpected (PlainBnfUnexpectedParserV1 $unexpected)))))

; A start supplied by the caller is not silently replaced by the first rule.
!(case (bnf:parser-from-text "<s> ::= \"x\"\n" "missing")
   (((BNF:ParserNotLoaded $_diagnostics) PlainBnfPublicUnknownStartRefusedV1)
    ($unexpected (PlainBnfUnexpectedStartOutcomeV1 $unexpected))))

; The file route uses exactly the same default authority and stages.
!(let $parser
       (bnf:parser-from-file "tests/langdef/bnf/plain_bnf_dyck_v1.bnf" "s")
   (case $parser
     (((NativeHandle $kind $identifier)
        (let $result (langdef:parse-authored-text $parser "(())()")
          (let $_ (langdef:close-authored-parser $parser)
            (case $result
              (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                  $_binding $_source ($tree)) PlainBnfPublicFileParserAcceptedV1)
               ($unexpected (PlainBnfUnexpectedFileParseV1 $unexpected)))))))
      ($unexpected (PlainBnfUnexpectedFileParserV1 $unexpected)))))

; The explicit authority variant supplies a lexical leaf as structured data.
!(let $authority
       (bnf:grammar-authority-from-string "s"
         (bnf-v1:lexical-environment
           (bnf-v1:lexical-declarations-cons
             (bnf-v1:lexical-declaration
               (bnf:text "digit") "Digit"
               (bnf-v1:lexical-points
                 (bnf-v1:scalars-cons 55 (bnf-v1:scalars-nil)))
               "digit-token" (bnf-v1:lexical-origin "example" 0))
             (bnf-v1:lexical-declarations-nil))))
   (let $parser
        (bnf:parser-from-text-with-authority "<s> ::= <digit>\n" $authority)
     (case $parser
       (((NativeHandle $kind $identifier)
          (let $result (langdef:parse-authored-text $parser "7")
            (let $_ (langdef:close-authored-parser $parser)
              (case $result
                (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                    $_binding $_source ($tree)) PlainBnfPublicLexicalOverrideAcceptedV1)
                 ($unexpected (PlainBnfUnexpectedLexicalParseV1 $unexpected)))))))
        ($unexpected (PlainBnfUnexpectedLexicalParserV1 $unexpected))))))

; Parse ambiguity is retained rather than resolved by this convenience API.
!(let $parser (bnf:parser-from-text "<s> ::= \"x\" | \"x\"\n" "s")
   (case $parser
     (((NativeHandle $kind $identifier)
        (let $result (langdef:parse-authored-text $parser "x")
          (let $_ (langdef:close-authored-parser $parser)
            (case $result
              (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                  $_binding $_source ($first $second)) PlainBnfPublicAmbiguityRetainedV1)
               ($unexpected (PlainBnfUnexpectedAmbiguityV1 $unexpected)))))))
      ($unexpected (PlainBnfUnexpectedAmbiguousParserV1 $unexpected)))))

; Source syntax rejection remains distinguishable from semantic refusal.
!(case (bnf:parser-from-text "<broken> := \"x\"\n" "broken")
   (((BNF:ReadRejected source-syntax $_offset $_expected)
      PlainBnfPublicSourceRejectionRetainedV1)
    ($unexpected (PlainBnfUnexpectedSourceOutcomeV1 $unexpected))))

!(case (bnf:parser-from-text "<s> ::= \"x\"\n" 7)
   (((BNF:ReadFault text-codec $_detail) PlainBnfPublicStartTypeRefusedV1)
    ($unexpected (PlainBnfUnexpectedStartTypeOutcomeV1 $unexpected))))

; The host's NUL-terminated string cannot represent scalar zero. Conversion
; must refuse it, never silently return a truncated string. This is not a
; restriction on the separate BNF lexical scalar domain.
!(case (bnf:text->string (bnf-v1:text-cons 0 (bnf-v1:text-nil)))
   (((Error $_operation $_detail) PlainBnfNulStringConversionRefusedV1)
    ($unexpected (PlainBnfUnexpectedNulStringV1 $unexpected))))
!(case (bnf:text->string (bnf-v1:text-cons 55296 (bnf-v1:text-nil)))
   (((Error $_operation $_detail) PlainBnfSurrogateStringConversionRefusedV1)
    ($unexpected (PlainBnfUnexpectedSurrogateStringV1 $unexpected))))

; A loaded parser rejects trailing input at its source byte offset.
!(let $parser (bnf:parser-from-text "<s> ::= \"x\"\n" "s")
   (let $result (langdef:parse-authored-text $parser "xx")
     (let $_ (langdef:close-authored-parser $parser)
       (case $result
         (((LangDef:AuthoredParseRejected PlainBnfDenotedSyntaxV1
             $_binding $_source 1 $expected)
            (case $expected
              ((() (PlainBnfUnexpectedEmptyExpectationV1))
               ($_ PlainBnfPublicInputRejectionRetainedV1))))
          ($unexpected (PlainBnfUnexpectedInputRejectionV1 $unexpected)))))))

; Evidence materialization has a caller-supplied bound. Exceeding it must
; not become rejection of a word which the same parser subsequently accepts.
!(let $parser (bnf:parser-from-text "<s> ::= \"x\" | \"x\"\n" "s")
   (let $limited (langdef:parse-authored-text-evidence $parser "x" 1)
     (let $complete (langdef:parse-authored-text-evidence $parser "x" 10000)
       (let $_ (langdef:close-authored-parser $parser)
         (case ($limited $complete)
           ((((Error __cetta_lib_authored_parser_parse_text_evidence
                "canonical forest exceeds the requested materialization bound")
              (LangDef:AuthoredParseEvidenceAccepted PlainBnfDenotedSyntaxV1
                $_binding $_source $_forest-digest $_forest ($first $second)))
             PlainBnfPublicForestLimitRetainedV1)
            ($unexpected (PlainBnfUnexpectedForestLimitV1 $unexpected))))))))
