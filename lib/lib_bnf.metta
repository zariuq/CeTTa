; Plain BNF as structured, source-spanned MeTTa data.
;
; The reader follows one visible construction:
;
;   authored syntax LanguageDef + lexical profile
;     -> ParserPack -> packed GLL CST
;     -> authored projection GSLT
;     -> admitted BnfGrammarDocument
;
; This module does not select one parse from an ambiguous result.  Declaration
; and graph validation precede denotation; only an admitted grammar can yield
; a LanguageDef, parser profile, and source-occurrence map that may be loaded
; as a parser.  The native qualification gate also runs GLR and requires the
; two engines to agree.

!(import! &self langdef)

; Semantic relations are supplied by a target realization.  The current
; generated PeTTa artifact is generated/plain_bnf_semantic_admission_v1.metta; other
; dialects remain free to leave bnf:validate-document uninterpreted until they
; have their own generated realization.
; The PeTTa lowering consumes source-derived integer completion NativeTypes.
; Public validation admits its structured input before selecting that checked
; entry; malformed input is not sent to the generated raw fallback.

(= (bnf:empty-lexical-environment)
   (bnf-v1:lexical-environment
     (bnf-v1:lexical-declarations-nil)))

(= (bnf:grammar-authority $start-name $lexical-environment)
   (bnf-v1:grammar-authority
     (bnf-v1:start $start-name) $lexical-environment))

; The default supplies no external lexical categories. The caller always
; names the start explicitly; declaration order never selects it implicitly.
(= (bnf:default-authority $start-string)
   (bnf:grammar-authority-from-string
     $start-string (bnf:empty-lexical-environment)))

(= (bnf-internal:text-codec $operation $value)
   (let $codec
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"))
     (case $codec
       (((NativeHandle $kind $identifier)
          (let $result
               (langdef:run-deterministic-equations
                 $codec (LangDef:DeterministicCall $operation $value))
            (let $_ (langdef:close-deterministic-equations $codec)
              $result)))
        ($failure (BNF:ReadFault text-codec $failure))))))

; Only UTF-8 decoding is native. The carrier conversion is the executed
; authored denotation GSLT, not a render-and-reparse conversion.
(= (bnf:text $string)
   (case (bnf-internal:text-codec bnf-v1:string->text $string)
     (((Error $operation $detail)
        (BNF:ReadFault text-codec (Error $operation $detail)))
      ($result $result))))

(= (bnf:text->string $text)
   (bnf-internal:text-codec bnf-v1:text->string $text))

(= (bnf:grammar-authority-from-string $start-string $lexical-environment)
   (case (bnf:text $start-string)
     (((BNF:ReadFault $stage $detail) (BNF:ReadFault $stage $detail))
      ($name (bnf:grammar-authority $name $lexical-environment)))))

; Convenience composition retains all public stage outcomes. The explicit
; authority variants admit lexical environments such as bnf:self-authority.
(= (bnf:parser-from-text $source $start-string)
   (bnf:parser-from-text-with-authority
     $source (bnf:default-authority $start-string)))
(= (bnf:parser-from-file $path $start-string)
   (bnf:parser-from-file-with-authority
     $path (bnf:default-authority $start-string)))

(= (bnf:parser-from-text-with-authority $source $authority)
   (case $authority
     (((BNF:ReadFault $stage $detail) (BNF:ReadFault $stage $detail))
      ($value
        (bnf:load-parser
          (bnf:denote (bnf:validate (bnf:read-text $source) $value)))))))
(= (bnf:parser-from-file-with-authority $path $authority)
   (case $authority
     (((BNF:ReadFault $stage $detail) (BNF:ReadFault $stage $detail))
      ($value
        (bnf:load-parser
          (bnf:denote (bnf:validate (bnf:read-file $path) $value)))))))

; Structured input is validated afresh. It makes no claim to be the exact
; parse of a source digest, so callers may construct or edit grammar data
; without forging a ReadValue or rendering it back to source text.
(= (bnf:parser-from-document $document $authority)
   (case $authority
     (((BNF:ReadFault $stage $detail) (BNF:ReadFault $stage $detail))
      ($value
        (bnf:load-parser
          (bnf:denote (bnf:validate-document $document $value)))))))

(= (bnf:admit-grammar-document $value)
   (let $language
        (langdef:load-language
          "langdef/bnf/plain_bnf_grammar_v1.metta")
     (case $language
       (((NativeHandle $kind $identifier)
          (let $admission
               (langdef:admit-term
                 $language "BnfGrammarDocument" $value)
            (let $_ (langdef:close-language $language)
              (case $admission
                (((LangDef:TermAdmitted)
                   (BNF:GrammarDocumentAdmitted $value))
                 ((LangDef:TermRejected $reason $message)
                   (BNF:GrammarDocumentRejected $reason $message))
                 ($_ (BNF:ReadFault grammar-admission $admission)))))))
        ($_ (BNF:ReadFault grammar-language $language))))))

(= (bnf:admit-grammar-input $value)
   (let $language
        (langdef:load-language
          "langdef/bnf/plain_bnf_grammar_v1.metta")
     (case $language
       (((NativeHandle $kind $identifier)
          (let $admission
               (langdef:admit-term
                 $language "BnfGrammarInput" $value)
            (let $_ (langdef:close-language $language)
              (case $admission
                (((LangDef:TermAdmitted)
                   (BNF:GrammarInputAdmitted $value))
                 ((LangDef:TermRejected $reason $message)
                   (BNF:GrammarInputRejected $reason $message))
                 ($_ (BNF:ReadFault grammar-input-admission $admission)))))))
        ($_ (BNF:ReadFault grammar-language $language))))))

(= (bnf:project-single-cst $binding-digest $source-digest $cst)
   (let $projection
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_cst_projection_v1.metta"))
     (case $projection
       (((NativeHandle $kind $identifier)
          (let $projected
               (langdef:run-deterministic-equations
                 $projection
                 (LangDef:DeterministicCall bnf-v1:project $cst))
            (let $_ (langdef:close-deterministic-equations $projection)
              (case (bnf:admit-grammar-document $projected)
                (((BNF:GrammarDocumentAdmitted $document)
                   (BNF:ReadValue
                     (BNF:GrammarDocument
                       $binding-digest $source-digest $document)))
                 ((BNF:GrammarDocumentRejected $reason $message)
                   (BNF:ReadRejected
                     grammar-document $reason $message))
                 ($result $result))))))
        ($_ (BNF:ReadFault cst-projection $projection))))))

(= (bnf:finish-parse-result $parsed)
   (case $parsed
     (((LangDef:AuthoredParseAccepted
         PlainBnfSourceV1 $binding-digest $source-digest ($cst))
        (bnf:project-single-cst
          $binding-digest $source-digest $cst))
      ((LangDef:AuthoredParseAccepted
         PlainBnfSourceV1 $binding-digest $source-digest $csts)
        (BNF:ReadAmbiguous $binding-digest $source-digest $csts))
      ((LangDef:AuthoredParseRejected
         PlainBnfSourceV1 $binding-digest $source-digest
         $byte-offset $expected)
        (BNF:ReadRejected
          source-syntax $byte-offset $expected))
      ((LangDef:AuthoredParseIncomplete
         PlainBnfSourceV1 $binding-digest $source-digest
         $outcome $detail)
        (BNF:ReadIncomplete
          $binding-digest $source-digest $outcome $detail))
      ($_ (BNF:ReadFault source-parser $parsed)))))

(= (bnf:read-file $path)
   (let $parser
        (langdef:load-authored-parser
          "langdef/bnf/plain_bnf_source_v1.metta"
          "langdef/bnf/plain_bnf_parser_profile_v1.metta")
     (case $parser
       (((NativeHandle $kind $identifier)
          (let $parsed (langdef:parse-authored-file $parser $path)
            (let $_ (langdef:close-authored-parser $parser)
              (bnf:finish-parse-result $parsed))))
        ($_ (BNF:ReadFault source-parser $parser))))))

(= (bnf:read-text $source)
   (let $parser
        (langdef:load-authored-parser
          "langdef/bnf/plain_bnf_source_v1.metta"
          "langdef/bnf/plain_bnf_parser_profile_v1.metta")
     (case $parser
       (((NativeHandle $kind $identifier)
          (let $parsed (langdef:parse-authored-text $parser $source)
            (let $_ (langdef:close-authored-parser $parser)
              (bnf:finish-parse-result $parsed))))
        ($_ (BNF:ReadFault source-parser $parser))))))

(= (bnf-internal:denote-document-candidate $document $validation)
   (let $denotation
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"))
     (case $denotation
       (((NativeHandle $kind $identifier)
          (let $candidate
               (langdef:run-deterministic-equations
                 $denotation
                 (LangDef:DeterministicCall
                   bnf-v1:denote-candidate $document $validation))
            (let $_ (langdef:close-deterministic-equations $denotation)
              $candidate)))
        ($_ (BNF:DenotationFault $denotation))))))

(= (bnf:validate-declarations $input)
   (let (gslt:result:BNFValidateGrammarDiscoveryV1:10 $result)
        (gslt:entry:BNFValidateGrammarDiscoveryV1:10 $input)
     $result))

(= (bnf:analyze-declaration-valid-document
      $start $definitions $lexicals)
   (let (gslt:result:BNFAnalyzeDocumentWorklistV1:1110 $result)
        (gslt:entry:BNFAnalyzeDocumentWorklistV1:1110
          $start $definitions $lexicals)
     $result))

(= (bnf:validate-input $input)
   (case (bnf:validate-declarations $input)
     (((BNFDeclarationValidationAcceptedV1
         $start $definitions $lexicals)
        (bnf:analyze-declaration-valid-document
          $start $definitions $lexicals))
      ((BNFDeclarationValidationRefusedV1 $diagnostics)
        (BNFSemanticValidationRefusedV1 $diagnostics))
      ($result (BNFSemanticValidationFaultV1 $result)))))

(= (bnf-typed-discovery:validate-input $language $input)
   (let (gslt:result:BNFValidateGrammarDiscoveryV1:10 $declarations)
        (gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $language $input)
     (case $declarations
       (((BNFDeclarationValidationAcceptedV1 $start $definitions $lexicals)
          (bnf:analyze-declaration-valid-document $start $definitions $lexicals))
        ((BNFDeclarationValidationRefusedV1 $diagnostics)
          (BNFSemanticValidationRefusedV1 $diagnostics))
        ($other (BNFSemanticValidationFaultV1 $other))))))

(= (bnf-typed-discovery:checked-document $language $document $authority)
   (let $input (bnf-v1:grammar-input $document $authority)
     (case (langdef:admit-term $language "BnfGrammarInput" $input)
       (((LangDef:TermAdmitted)
          (case (bnf-typed-discovery:validate-input $language $input)
             (((BNFSemanticValidationAcceptedV1 $start $definitions $lexicals $analysis)
               (BNFSemanticValidationAcceptedV1 $start $definitions $lexicals $analysis))
             ((BNFSemanticValidationRefusedV1 $diagnostics)
               (BNFSemanticValidationRefusedV1 $diagnostics))
             ($other (BNF:SemanticGrammarFault $other)))))
        ((LangDef:TermRejected $reason $message)
          (BNF:ReadRejected grammar-input $reason $message))
        ($other (BNF:ReadFault grammar-input-admission $other))))))

; Collect once at the finite resource boundary, not at each recursive call.
; Every answer occurrence is retained and explicit close runs once, including
; zero answers. CeTTa's arena-backed holders additionally cover engine abort;
; the collection itself is not a cancellation-safe bracket in another host.
; Collect validation payloads before attaching the unchanged document and
; authority. This keeps those large inputs out of each collected answer while
; preserving the close-once boundary for zero, one, or several answers.
(= (bnf-typed-discovery:validate-owned-document $language $document $authority)
   (let $answers
        (collapse (bnf-typed-discovery:checked-document $language $document $authority))
     (let $_ (langdef:close-language $language)
       (case (superpose $answers)
         (((BNFSemanticValidationAcceptedV1 $start $definitions $lexicals $analysis)
            (BNF:StructuredGrammarAdmitted $document $authority
              (BNFSemanticValidationAcceptedV1 $start $definitions $lexicals $analysis)))
          ((BNFSemanticValidationRefusedV1 $diagnostics)
            (BNF:StructuredGrammarRefused $document $authority $diagnostics))
          ($other $other))))))

(= (bnf:validate-document $document $authority)
   (let $language
        (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
     (case $language
       (((NativeHandle $kind $identifier)
          (bnf-typed-discovery:validate-owned-document $language $document $authority))
        ($other (BNF:ReadFault grammar-language $other))))))

; Source provenance is attached only to a document obtained by the reader.
; The same validation operation is shared with the structured-data route.
(= (bnf:validate
      (BNF:ReadValue
        (BNF:GrammarDocument $binding $source $document))
      $authority)
   (case (bnf:validate-document $document $authority)
     (((BNF:StructuredGrammarAdmitted $checked $policy $validation)
        (BNF:SemanticGrammarAdmitted
          $binding $source $checked $policy $validation))
      ((BNF:StructuredGrammarRefused $checked $policy $diagnostics)
        (BNF:SemanticGrammarRefused
          $binding $source $checked $policy $diagnostics))
      ($result $result))))

(= (bnf:validate (BNF:ReadAmbiguous $binding $source $csts) $authority)
   (BNF:ReadAmbiguous $binding $source $csts))
(= (bnf:validate (BNF:ReadRejected $stage $reason $message) $authority)
   (BNF:ReadRejected $stage $reason $message))
(= (bnf:validate
      (BNF:ReadIncomplete $binding $source $outcome $detail) $authority)
   (BNF:ReadIncomplete $binding $source $outcome $detail))
(= (bnf:validate (BNF:ReadFault $stage $detail) $authority)
   (BNF:ReadFault $stage $detail))

(= (bnf:denote
      (BNF:SemanticGrammarAdmitted
        $binding $source $document $authority $validation))
   (case (bnf-internal:denote-document-candidate $document $validation)
     (((BNF:DenotationCandidate $language $profile $origins)
        (BNF:AdmittedDenotation
          $binding $source $document $authority $validation
          (BNF:DenotationCandidate $language $profile $origins)))
      ($failure (BNF:DenotationFault $failure)))))

(= (bnf:denote
      (BNF:SemanticGrammarRefused
        $binding $source $document $authority $diagnostics))
   (BNF:SemanticGrammarRefused
     $binding $source $document $authority $diagnostics))

(= (bnf:denote
      (BNF:StructuredGrammarAdmitted $document $authority $validation))
   (case (bnf-internal:denote-document-candidate $document $validation)
     (((BNF:DenotationCandidate $language $profile $origins)
        (BNF:StructuredDenotation
          $document $authority $validation
          (BNF:DenotationCandidate $language $profile $origins)))
      ($failure (BNF:DenotationFault $failure)))))
(= (bnf:denote
      (BNF:StructuredGrammarRefused $document $authority $diagnostics))
   (BNF:StructuredGrammarRefused $document $authority $diagnostics))
(= (bnf:denote (BNF:SemanticGrammarFault $detail))
   (BNF:SemanticGrammarFault $detail))

; Unsuccessful reads remain the same structured observation through the
; complete read/validate/denote/load sequence. No parser is installed.
(= (bnf:denote (BNF:ReadAmbiguous $binding $source $csts))
   (BNF:ReadAmbiguous $binding $source $csts))
(= (bnf:denote (BNF:ReadRejected $stage $reason $message))
   (BNF:ReadRejected $stage $reason $message))
(= (bnf:denote
      (BNF:ReadIncomplete $binding $source $outcome $detail))
   (BNF:ReadIncomplete $binding $source $outcome $detail))
(= (bnf:denote (BNF:ReadFault $stage $detail))
   (BNF:ReadFault $stage $detail))

; Parser construction is intentionally available only after semantic
; admission.  A raw DenotationCandidate is inspectable construction data, not
; authority to install a parser.
(= (bnf:load-parser
      (BNF:AdmittedDenotation
        $_binding $_source $_document $_authority $_validation
        (BNF:DenotationCandidate $language $profile $_origins)))
   (langdef:load-authored-parser-values $language $profile))

(= (bnf:load-parser
      (BNF:StructuredDenotation
        $_document $_authority $_validation
        (BNF:DenotationCandidate $language $profile $_origins)))
   (langdef:load-authored-parser-values $language $profile))
(= (bnf:load-parser
      (BNF:StructuredGrammarRefused $_document $_authority $diagnostics))
   (BNF:ParserNotLoaded $diagnostics))

(= (bnf:load-parser
      (BNF:SemanticGrammarRefused
        $_binding $_source $_document $_authority $diagnostics))
   (BNF:ParserNotLoaded $diagnostics))
(= (bnf:load-parser (BNF:SemanticGrammarFault $detail))
   (BNF:ParserNotLoaded $detail))
(= (bnf:load-parser (BNF:DenotationFault $detail))
   (BNF:ParserNotLoaded (BNF:DenotationFault $detail)))

(= (bnf:load-parser (BNF:ReadAmbiguous $binding $source $csts))
   (BNF:ReadAmbiguous $binding $source $csts))
(= (bnf:load-parser (BNF:ReadRejected $stage $reason $message))
   (BNF:ReadRejected $stage $reason $message))
(= (bnf:load-parser
      (BNF:ReadIncomplete $binding $source $outcome $detail))
   (BNF:ReadIncomplete $binding $source $outcome $detail))
(= (bnf:load-parser (BNF:ReadFault $stage $detail))
   (BNF:ReadFault $stage $detail))

; Structural accessors are ordinary MeTTa relations over admitted grammar
; data.  They preserve entry, alternative, and element multiplicity.
(= (bnf:document
      (BNF:ReadValue
        (BNF:GrammarDocument $_binding $_source $document)))
   $document)

(= (bnf:document-entries (bnf-v1:document $entries $_span))
   $entries)

(= (bnf:entry
      (bnf-v1:entries-cons $head $_tail))
   $head)
(= (bnf:entry
      (bnf-v1:entries-cons $_head $tail))
   (bnf:entry $tail))

(= (bnf:entry-kind (bnf-v1:rule $_name $_expression $_span)) rule)
(= (bnf:entry-kind (bnf-v1:comment $_text $_span)) comment)
(= (bnf:entry-kind (bnf-v1:blank $_span)) blank)

(= (bnf:rule-name (bnf-v1:rule $name $_expression $_span)) $name)
(= (bnf:rule-expression
      (bnf-v1:rule $_name $expression $_span))
   $expression)

(= (bnf:alternative
      (bnf-v1:expression $alternatives $_span))
   (bnf:alternative-in $alternatives))
(= (bnf:alternative-in
      (bnf-v1:alternatives-cons $head $_tail))
   $head)
(= (bnf:alternative-in
      (bnf-v1:alternatives-cons $_head $tail))
   (bnf:alternative-in $tail))

(= (bnf:element
      (bnf-v1:alternative $elements $_span))
   (bnf:element-in $elements))
(= (bnf:element-in
      (bnf-v1:elements-cons $head $_tail))
   $head)
(= (bnf:element-in
      (bnf-v1:elements-cons $_head $tail))
   (bnf:element-in $tail))

(= (bnf:element-kind (bnf-v1:reference $_name $_span)) reference)
(= (bnf:element-kind (bnf-v1:literal $_text $_span)) literal)
(= (bnf:reference-name (bnf-v1:reference $name $_span)) $name)
(= (bnf:literal-text (bnf-v1:literal $text $_span)) $text)

; EBNF is a client of the BNF workbench: its concrete syntax is authored in
; BNF, then admitted and denoted by the same executed transformations.
; Structured authority for the plain-BNF self-description.  The lexical
; declarations are ordinary BnfGrammarAuthority data; they are not hidden
; callbacks and they remain visible to semantic admission and denotation.

(= (bnf:self-text-document-v1)
   (bnf-v1:text-cons 100
     (bnf-v1:text-cons 111
       (bnf-v1:text-cons 99
         (bnf-v1:text-cons 117
           (bnf-v1:text-cons 109
             (bnf-v1:text-cons 101
               (bnf-v1:text-cons 110
                 (bnf-v1:text-cons 116 (bnf-v1:text-nil))))))))))

(= (bnf:self-text-name-char-v1)
   (bnf-v1:text-cons 110
     (bnf-v1:text-cons 97
       (bnf-v1:text-cons 109
         (bnf-v1:text-cons 101
           (bnf-v1:text-cons 45
             (bnf-v1:text-cons 99
               (bnf-v1:text-cons 104
                 (bnf-v1:text-cons 97
                   (bnf-v1:text-cons 114 (bnf-v1:text-nil)))))))))))

(= (bnf:self-text-literal-scalar-v1)
   (bnf-v1:text-cons 108
     (bnf-v1:text-cons 105
       (bnf-v1:text-cons 116
         (bnf-v1:text-cons 101
           (bnf-v1:text-cons 114
             (bnf-v1:text-cons 97
               (bnf-v1:text-cons 108
                 (bnf-v1:text-cons 45
                   (bnf-v1:text-cons 115
                     (bnf-v1:text-cons 99
                       (bnf-v1:text-cons 97
                         (bnf-v1:text-cons 108
                           (bnf-v1:text-cons 97
                             (bnf-v1:text-cons 114
                               (bnf-v1:text-nil))))))))))))))))

(= (bnf:self-text-horizontal-space-v1)
   (bnf-v1:text-cons 104
     (bnf-v1:text-cons 111
       (bnf-v1:text-cons 114
         (bnf-v1:text-cons 105
           (bnf-v1:text-cons 122
             (bnf-v1:text-cons 111
               (bnf-v1:text-cons 110
                 (bnf-v1:text-cons 116
                   (bnf-v1:text-cons 97
                     (bnf-v1:text-cons 108
                       (bnf-v1:text-cons 45
                         (bnf-v1:text-cons 115
                           (bnf-v1:text-cons 112
                             (bnf-v1:text-cons 97
                               (bnf-v1:text-cons 99
                                 (bnf-v1:text-cons 101
                                   (bnf-v1:text-nil))))))))))))))))))

(= (bnf:self-text-line-scalar-v1)
   (bnf-v1:text-cons 108
     (bnf-v1:text-cons 105
       (bnf-v1:text-cons 110
         (bnf-v1:text-cons 101
           (bnf-v1:text-cons 45
             (bnf-v1:text-cons 115
               (bnf-v1:text-cons 99
                 (bnf-v1:text-cons 97
                   (bnf-v1:text-cons 108
                     (bnf-v1:text-cons 97
                       (bnf-v1:text-cons 114
                         (bnf-v1:text-nil))))))))))))
)

(= (bnf:self-name-char-scalars-v1)
   (bnf-v1:scalars-cons 45
   (bnf-v1:scalars-cons 48 (bnf-v1:scalars-cons 49
   (bnf-v1:scalars-cons 50 (bnf-v1:scalars-cons 51
   (bnf-v1:scalars-cons 52 (bnf-v1:scalars-cons 53
   (bnf-v1:scalars-cons 54 (bnf-v1:scalars-cons 55
   (bnf-v1:scalars-cons 56 (bnf-v1:scalars-cons 57
   (bnf-v1:scalars-cons 65 (bnf-v1:scalars-cons 66
   (bnf-v1:scalars-cons 67 (bnf-v1:scalars-cons 68
   (bnf-v1:scalars-cons 69 (bnf-v1:scalars-cons 70
   (bnf-v1:scalars-cons 71 (bnf-v1:scalars-cons 72
   (bnf-v1:scalars-cons 73 (bnf-v1:scalars-cons 74
   (bnf-v1:scalars-cons 75 (bnf-v1:scalars-cons 76
   (bnf-v1:scalars-cons 77 (bnf-v1:scalars-cons 78
   (bnf-v1:scalars-cons 79 (bnf-v1:scalars-cons 80
   (bnf-v1:scalars-cons 81 (bnf-v1:scalars-cons 82
   (bnf-v1:scalars-cons 83 (bnf-v1:scalars-cons 84
   (bnf-v1:scalars-cons 85 (bnf-v1:scalars-cons 86
   (bnf-v1:scalars-cons 87 (bnf-v1:scalars-cons 88
   (bnf-v1:scalars-cons 89 (bnf-v1:scalars-cons 90
   (bnf-v1:scalars-cons 95
   (bnf-v1:scalars-cons 97 (bnf-v1:scalars-cons 98
   (bnf-v1:scalars-cons 99 (bnf-v1:scalars-cons 100
   (bnf-v1:scalars-cons 101 (bnf-v1:scalars-cons 102
   (bnf-v1:scalars-cons 103 (bnf-v1:scalars-cons 104
   (bnf-v1:scalars-cons 105 (bnf-v1:scalars-cons 106
   (bnf-v1:scalars-cons 107 (bnf-v1:scalars-cons 108
   (bnf-v1:scalars-cons 109 (bnf-v1:scalars-cons 110
   (bnf-v1:scalars-cons 111 (bnf-v1:scalars-cons 112
   (bnf-v1:scalars-cons 113 (bnf-v1:scalars-cons 114
   (bnf-v1:scalars-cons 115 (bnf-v1:scalars-cons 116
   (bnf-v1:scalars-cons 117 (bnf-v1:scalars-cons 118
   (bnf-v1:scalars-cons 119 (bnf-v1:scalars-cons 120
     (bnf-v1:scalars-cons 121 (bnf-v1:scalars-cons 122
       (bnf-v1:scalars-nil))))))))))))))))))))))))))))))))
     ))))))))))))))))))))))))))))))))
))

(= (bnf:self-authority-v1)
   (bnf-v1:grammar-authority
     (bnf-v1:start (bnf:self-text-document-v1))
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:self-text-name-char-v1)
           "BnfNameCharClass"
           (bnf-v1:lexical-points (bnf:self-name-char-scalars-v1))
           "bnf-source-v1:lex-name-char"
           (bnf-v1:lexical-origin "plain-bnf-self-authority-v1" 0))
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:self-text-literal-scalar-v1)
           "BnfLiteralScalarClass"
           (bnf-v1:lexical-except
             (bnf-v1:scalars-cons 10
               (bnf-v1:scalars-cons 13
                 (bnf-v1:scalars-cons 34
                   (bnf-v1:scalars-cons 92
                     (bnf-v1:scalars-nil))))))
           "bnf-source-v1:lex-literal-scalar"
           (bnf-v1:lexical-origin "plain-bnf-self-authority-v1" 1))
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:self-text-horizontal-space-v1)
           "BnfHorizontalSpaceClass"
           (bnf-v1:lexical-points
             (bnf-v1:scalars-cons 9
               (bnf-v1:scalars-cons 32 (bnf-v1:scalars-nil))))
           "bnf-source-v1:lex-horizontal-space"
           (bnf-v1:lexical-origin "plain-bnf-self-authority-v1" 2))
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:self-text-line-scalar-v1)
           "BnfLineScalarClass"
           (bnf-v1:lexical-except
             (bnf-v1:scalars-cons 10
               (bnf-v1:scalars-cons 13 (bnf-v1:scalars-nil))))
           "bnf-source-v1:lex-line-scalar"
           (bnf-v1:lexical-origin "plain-bnf-self-authority-v1" 3))
         (bnf-v1:lexical-declarations-nil)))))))
)

(= (bnf:open-ebnf-reader)
   (bnf:parser-from-file-with-authority
     "langdef/bnf/ebnf_source_v1.bnf" (bnf:self-authority-v1)))

(= (bnf-internal:ebnf-transform $sources $call)
   (let $program (langdef:load-deterministic-equations $sources)
     (case $program
       (((NativeHandle $kind $id)
          (let $answers (collapse (langdef:run-deterministic-equations $program $call))
            (let $_ (langdef:close-deterministic-equations $program)
              (superpose $answers))))
        ($fault (BNF:ReadFault ebnf-transformation $fault))))))

; Test-scoped transform with an explicit work allowance, for oracle
; comparisons at sizes beyond the fixed production budget.
(= (bnf-internal:ebnf-transform-allowance $sources $call $allowance)
   (let $program (langdef:load-deterministic-equations $sources)
     (case $program
       (((NativeHandle $kind $id)
          (let $answers (collapse (langdef:run-deterministic-equations-allowance $program $call $allowance))
            (let $_ (langdef:close-deterministic-equations $program)
              (superpose $answers))))
        ($fault (BNF:ReadFault ebnf-transformation $fault))))))

(= (bnf-internal:ebnf-admit $category $value)
   (let $language (langdef:load-language "langdef/bnf/ebnf_grammar_v1.metta")
     (case $language
       (((NativeHandle $kind $id)
          (let $answer (langdef:admit-term $language $category $value)
            (let $_ (langdef:close-language $language) $answer)))
        ($fault (BNF:ReadFault ebnf-authority $fault))))))

(= (bnf-internal:ebnf-project $binding $source $cst)
   (let $document
        (bnf-internal:ebnf-transform
          (LangDef:EquationSources "langdef/bnf/ebnf_cst_projection_v1.metta")
          (LangDef:DeterministicCall ebnf-v1:project $cst))
     (case (bnf-internal:ebnf-admit "BnfGrammarDocument" $document)
       (((LangDef:TermAdmitted)
          (BNF:ReadValue (BNF:GrammarDocument $binding $source $document)))
        ((LangDef:TermRejected $reason $message)
          (BNF:ReadRejected ebnf-document $reason $message))
        ($fault (BNF:ReadFault ebnf-projection $fault))))))

(= (bnf-internal:finish-ebnf-read $parsed)
   (case $parsed
     (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1 $binding $source ($cst))
        (bnf-internal:ebnf-project $binding $source $cst))
      ((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1 $binding $source $csts)
        (BNF:ReadAmbiguous $binding $source $csts))
      ((LangDef:AuthoredParseRejected PlainBnfDenotedSyntaxV1 $binding $source $offset $expected)
        (BNF:ReadRejected ebnf-source-syntax $offset $expected))
      ((LangDef:AuthoredParseIncomplete PlainBnfDenotedSyntaxV1 $binding $source $outcome $detail)
        (BNF:ReadIncomplete $binding $source $outcome $detail))
      ($fault (BNF:ReadFault ebnf-source-parser $fault)))))

(= (bnf:read-ebnf-text $text)
   (let $parser (bnf:open-ebnf-reader)
     (case $parser
       (((NativeHandle $kind $id)
          (let $parsed (langdef:parse-authored-text $parser $text)
            (let $_ (langdef:close-authored-parser $parser)
              (bnf-internal:finish-ebnf-read $parsed))))
        ($fault (BNF:ReadFault ebnf-reader-construction $fault))))))

(= (bnf:read-ebnf-file $path)
   (let $parser (bnf:open-ebnf-reader)
     (case $parser
       (((NativeHandle $kind $id)
          (let $parsed (langdef:parse-authored-file $parser $path)
            (let $_ (langdef:close-authored-parser $parser)
              (bnf-internal:finish-ebnf-read $parsed))))
        ($fault (BNF:ReadFault ebnf-reader-construction $fault))))))

; The lowering result contains ordinary BNF grammar data and a helper-origin
; map. Keep the original EBNF document: the lowered document is not a claim
; that those generated productions were written in the source file.
(= (bnf:lower-ebnf-document $document $authority)
   (case (bnf-internal:ebnf-admit "BnfGrammarInput"
           (bnf-v1:grammar-input $document $authority))
     (((LangDef:TermAdmitted)
        (case (bnf-internal:ebnf-transform
                (LangDef:EquationSources
                  "langdef/bnf/plain_bnf_denotation_v1.metta"
                  "langdef/bnf/ebnf_lowering_v1.metta")
                (LangDef:DeterministicCall ebnf-v1:lower $document $authority))
          (((ebnf-v1:lowered $plain $origins)
             (case (bnf:admit-grammar-document $plain)
               (((BNF:GrammarDocumentAdmitted $checked)
                  (EBNF:LoweredGrammar $document $authority $checked $origins))
                ($fault (BNF:ReadFault ebnf-lowered-document $fault)))))
           ($fault (BNF:ReadFault ebnf-lowering $fault)))))
      ((LangDef:TermRejected $reason $message)
        (BNF:ReadRejected ebnf-input $reason $message))
      ($fault (BNF:ReadFault ebnf-input-admission $fault)))))

(= (bnf-internal:load-lowered-ebnf $document $authority)
   (case (bnf:lower-ebnf-document $document $authority)
     (((EBNF:LoweredGrammar $original $policy $plain $origins)
        (case (bnf:parser-from-document $plain $policy)
          (((NativeHandle $kind $id)
             (EBNF:Parser (NativeHandle $kind $id)
               (EBNF:LoweredGrammar $original $policy $plain $origins)))
           ((BNF:ParserNotLoaded (BNFDiagnosticsConsV1 $head $tail))
             (bnf-internal:ebnf-graph-refusal
               (EBNF:LoweredGrammar $original $policy $plain $origins)
               (BNFDiagnosticsConsV1 $head $tail)))
           ($failure $failure))))
      ($failure $failure))))

; Source diagnostics count authored reference occurrences, not copies made
; by repetition expansion. The declaration view deliberately has no claimed
; language semantics; only the declaration entry consumes it.
(= (bnf-internal:ebnf-check-declarations $language $view $authority)
   (let $input (bnf-v1:grammar-input $view $authority)
     (case (langdef:admit-term $language "BnfGrammarInput" $input)
       (((LangDef:TermAdmitted)
          (let (gslt:result:BNFValidateGrammarDiscoveryV1:10 $answer)
               (gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 $language $input)
            $answer))
        ((LangDef:TermRejected $reason $message)
          (BNF:ReadRejected ebnf-declaration-view $reason $message))
        ($fault (BNF:ReadFault ebnf-declaration-view $fault))))))

(= (bnf-internal:ebnf-owned-declarations $language $view $authority)
   (let $answers
        (collapse (bnf-internal:ebnf-check-declarations $language $view $authority))
     (let $_ (langdef:close-language $language) (superpose $answers))))

(= (bnf:validate-ebnf-declarations $document $authority)
   (case (bnf-internal:ebnf-admit "BnfGrammarInput"
           (bnf-v1:grammar-input $document $authority))
     (((LangDef:TermAdmitted)
        (let $view
             (bnf-internal:ebnf-transform
               (LangDef:EquationSources "langdef/bnf/ebnf_declaration_view_v1.metta")
               (LangDef:DeterministicCall ebnf-v1:declaration-view $document))
          (let $language (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
            (case $language
              (((NativeHandle $kind $id)
                 (bnf-internal:ebnf-owned-declarations $language $view $authority))
               ($fault (BNF:ReadFault ebnf-declaration-authority $fault)))))))
      ((LangDef:TermRejected $reason $message)
        (BNF:ReadRejected ebnf-input $reason $message))
      ($fault (BNF:ReadFault ebnf-input-admission $fault)))))

(= (bnf:parser-from-ebnf-document $document $authority)
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1 $start $definitions $lexicals)
        (bnf-internal:load-lowered-ebnf $document $authority))
      ((BNFDeclarationValidationRefusedV1 $diagnostics)
        (BNF:ParserNotLoaded $diagnostics))
      ($failure $failure))))

(= (bnf-internal:parser-from-ebnf-read $read $authority)
   (case $read
     (((BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
        (case (bnf:parser-from-ebnf-document $document $authority)
          (((EBNF:Parser $parser $receipt)
             (EBNF:SourceParser $parser $binding $source $receipt))
           ($failure $failure))))
      ($failure $failure))))

(= (bnf:parser-from-ebnf-text-with-authority $text $authority)
   (bnf-internal:parser-from-ebnf-read (bnf:read-ebnf-text $text) $authority))
(= (bnf:parser-from-ebnf-file-with-authority $path $authority)
   (bnf-internal:parser-from-ebnf-read (bnf:read-ebnf-file $path) $authority))
(= (bnf:parser-from-ebnf-text $text $start)
   (bnf:parser-from-ebnf-text-with-authority $text (bnf:default-authority $start)))
(= (bnf:parser-from-ebnf-file $path $start)
   (bnf:parser-from-ebnf-file-with-authority $path (bnf:default-authority $start)))

; Native parser results pass through the authored derivation projection.
; The lowered receipt stays available for inspection and source provenance.
(= (bnf:parse-ebnf-text (EBNF:Parser $parser $receipt) $text)
   (EBNF:Parse
     (bnf-internal:ebnf-observation $receipt (langdef:parse-authored-text $parser $text))
     $receipt))
(= (bnf:parse-ebnf-text
      (EBNF:SourceParser $parser $binding $source $receipt) $text)
   (EBNF:SourceParse
     (bnf-internal:ebnf-observation $receipt (langdef:parse-authored-text $parser $text))
     $binding $source $receipt))
(= (bnf:close-ebnf-parser (EBNF:Parser $parser $_receipt))
   (langdef:close-authored-parser $parser))
(= (bnf:close-ebnf-parser
      (EBNF:SourceParser $parser $_binding $_source $_receipt))
   (langdef:close-authored-parser $parser))

(= (bnf:parse-ebnf-text-evidence (EBNF:Parser $parser $receipt) $text $limit)
   (EBNF:Parse
     (bnf-internal:ebnf-observation $receipt
       (langdef:parse-authored-text-evidence $parser $text $limit)) $receipt))
(= (bnf:parse-ebnf-text-evidence
      (EBNF:SourceParser $parser $binding $source $receipt) $text $limit)
   (EBNF:SourceParse
     (bnf-internal:ebnf-observation $receipt
       (langdef:parse-authored-text-evidence $parser $text $limit))
     $binding $source $receipt))
(= (bnf:parse-ebnf-file (EBNF:Parser $parser $receipt) $path)
   (EBNF:Parse
     (bnf-internal:ebnf-observation $receipt (langdef:parse-authored-file $parser $path))
     $receipt))
(= (bnf:parse-ebnf-file
      (EBNF:SourceParser $parser $binding $source $receipt) $path)
   (EBNF:SourceParse
     (bnf-internal:ebnf-observation $receipt (langdef:parse-authored-file $parser $path))
     $binding $source $receipt))

(= (bnf-internal:ebnf-observation $receipt $parsed)
   (case $parsed
     (((LangDef:AuthoredParseAccepted $language $binding $source $trees)
        (case (bnf:project-ebnf-trees $receipt $trees)
          (((EBNF:DerivationsCons $head $tail)
             (EBNF:Accepted $binding $source (EBNF:DerivationsCons $head $tail)))
           ((EBNF:DerivationsNil)
             (EBNF:Accepted $binding $source (EBNF:DerivationsNil)))
           ($fault (BNF:ReadFault ebnf-derivation-projection $fault)))))
      ((LangDef:AuthoredParseEvidenceAccepted $language $binding $source $digest $forest $trees)
        (case (bnf:project-ebnf-trees $receipt $trees)
          (((EBNF:DerivationsCons $head $tail)
             (EBNF:EvidenceAccepted $binding $source $digest $forest
               (EBNF:DerivationsCons $head $tail)))
           ((EBNF:DerivationsNil)
             (EBNF:EvidenceAccepted $binding $source $digest $forest (EBNF:DerivationsNil)))
           ($fault (BNF:ReadFault ebnf-derivation-projection $fault)))))
      ((LangDef:AuthoredParseRejected $language $binding $source $offset $expected)
        (EBNF:Rejected $binding $source $offset $expected))
      ((LangDef:AuthoredParseEvidenceRejected $language $binding $source $offset $expected $digest $forest)
        (EBNF:EvidenceRejected $binding $source $offset $expected $digest $forest))
      ((LangDef:AuthoredParseCyclic $language $binding $source $accepted $digest)
        (EBNF:Cyclic $binding $source $accepted $digest))
      ((LangDef:AuthoredParseEvidenceCyclic $language $binding $source $accepted $digest $forest)
        (EBNF:EvidenceCyclic $binding $source $accepted $digest $forest))
      ((LangDef:AuthoredParseIncomplete $language $binding $source $outcome $detail)
        (EBNF:Incomplete $binding $source $outcome $detail))
      ($fault (BNF:ReadFault ebnf-parser $fault)))))

(= (bnf:element-kind (ebnf-v1:group $_body $_span)) group)
(= (bnf:element-kind (ebnf-v1:optional $_body $_span)) optional)
(= (bnf:element-kind (ebnf-v1:zero-or-more $_body $_span)) zero-or-more)
(= (bnf:element-kind (ebnf-v1:one-or-more $_body $_span)) one-or-more)
(= (bnf:element-body (ebnf-v1:group $body $_span)) $body)
(= (bnf:element-body (ebnf-v1:optional $body $_span)) $body)
(= (bnf:element-body (ebnf-v1:zero-or-more $body $_span)) $body)
(= (bnf:element-body (ebnf-v1:one-or-more $body $_span)) $body)

; Explicit projection accepts the lowered grammar receipt and the parser's
; finite CST tuple. It preserves one derivation per tuple occurrence.
; The native realization is generic over receipt data; the authored
; deterministic transformation remains the independently executable oracle,
; pinned output-identical by dedicated equivalence tests.
(= (bnf-internal:project-ebnf-native $plain $origins $authority $trees)
   (function
     (case (__cetta_lib_bnf_ebnf_project_v1 $plain $origins $authority $trees)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (bnf:project-ebnf-trees $receipt $trees)
   (case $receipt
     (((EBNF:LoweredGrammar $original $authority $plain $origins)
        (case (bnf-internal:project-ebnf-native $plain $origins $authority $trees)
          (((Error $call $message) (Error $call $message))
           ($result $result))))
      ($malformed (BNF:ReadFault ebnf-lowering-receipt $malformed)))))

; The authored oracle: same observation through the deterministic-equation
; transformation. Used by equivalence tests, never by the native path.
(= (bnf:project-ebnf-trees-authored $receipt $trees)
   (case $receipt
     (((EBNF:LoweredGrammar $original $authority $plain $origins)
        (bnf-internal:ebnf-transform
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_derivation_projection_v1.metta")
          (LangDef:DeterministicCall ebnf-v1:project-derivations
            $plain $origins $authority $trees)))
      ($malformed (BNF:ReadFault ebnf-lowering-receipt $malformed)))))

; Test-scoped authored oracle with an explicit work allowance, used by the
; native-versus-authored equivalence gate at extended sizes.
(= (bnf:project-ebnf-trees-authored-allowance $receipt $trees $allowance)
   (case $receipt
     (((EBNF:LoweredGrammar $original $authority $plain $origins)
        (bnf-internal:ebnf-transform-allowance
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_derivation_projection_v1.metta")
          (LangDef:DeterministicCall ebnf-v1:project-derivations
            $plain $origins $authority $trees)
          $allowance))
      ($malformed (BNF:ReadFault ebnf-lowering-receipt $malformed)))))

; Graph diagnostics arise after lowering; source-declaration diagnostics
; remain on the original pre-lowering path. Preserve the raw graph evidence
; alongside the source-level view, including any future unmapped diagnostic.
(= (bnf:project-ebnf-graph-diagnostics $receipt $diagnostics)
   (case $receipt
     (((EBNF:LoweredGrammar $original $authority $plain $origins)
        (bnf-internal:ebnf-transform
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_diagnostic_projection_v1.metta")
          (LangDef:DeterministicCall ebnf-v1:project-graph-diagnostics
            $origins $diagnostics)))
      ($malformed (BNF:ReadFault ebnf-lowering-receipt $malformed)))))

(= (bnf-internal:ebnf-graph-refusal $receipt $diagnostics)
   (case (bnf:project-ebnf-graph-diagnostics $receipt $diagnostics)
     (((EBNF:Diagnostics $items)
        (EBNF:GraphRefused (EBNF:Diagnostics $items) $diagnostics $receipt))
      ($fault (BNF:ReadFault ebnf-graph-diagnostics
        (EBNF:UnprojectedDiagnostics $diagnostics $receipt $fault))))))
