!(import! &self langdef)

(= (mm0:test:runtime-live-syntax-environment)
   (MM0MathClassTableConsV1
     (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 40) MM0MathBothDelimiterV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 41) MM0MathRightDelimiterV1)
           (MM0MathClassTableConsV1
             (MM0MathClassEntryV1 (cp 42) MM0MathBothDelimiterV1)
             MM0MathClassTableNilV1))))))

(= (mm0:test:runtime-live-syntax-input)
   (MM0MathScalarsConsV1 (cp 97)
     (MM0MathScalarsConsV1 (cp 42)
       (MM0MathScalarsConsV1 (cp 98)
         (MM0MathScalarsConsV1 (cp 40)
           (MM0MathScalarsConsV1 (cp 99)
             (MM0MathScalarsConsV1 (cp 41)
               (MM0MathScalarsConsV1 (cp 32)
                 (MM0MathScalarsConsV1
                   (cp 100) MM0MathScalarsNilV1)))))))))

(= (mm0:test:runtime-live-syntax-tokens)
   (MM0MathTokensConsV1
     (MM0MathTokenV1
       (MM0MathScalarsConsV1 (cp 97) MM0MathScalarsNilV1))
     (MM0MathTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 42) MM0MathScalarsNilV1))
       (MM0MathTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1 (cp 98) MM0MathScalarsNilV1))
         (MM0MathTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1))
           (MM0MathTokensConsV1
             (MM0MathTokenV1
               (MM0MathScalarsConsV1 (cp 99) MM0MathScalarsNilV1))
             (MM0MathTokensConsV1
               (MM0MathTokenV1
                 (MM0MathScalarsConsV1 (cp 41) MM0MathScalarsNilV1))
               (MM0MathTokensConsV1
                 (MM0MathTokenV1
                   (MM0MathScalarsConsV1 (cp 100) MM0MathScalarsNilV1))
                 MM0MathTokensNilV1))))))))

; Concrete MM0 text is parsed at runtime. The compiler sees only the authored
; source-fold and secondary-lexer presentations loaded before this query.
!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/live_syntax_delimiters.mm0")
     (let
       (gslt:result:MM0DelimiterEnvironmentFromAstV1:10
         (mm0:test:runtime-live-syntax-environment))
       (eval
         (quote
           (gslt:fn:MM0DelimiterEnvironmentFromAstV1:10 $ast)))
       MM0RuntimeLiveSyntaxEnvironmentAcceptedV1)))

!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/live_syntax_delimiters.mm0")
     (let
       (gslt:result:MM0LiveDelimiterLexV1:110
         (mm0:test:runtime-live-syntax-tokens))
       (eval
         (quote
           (gslt:fn:MM0LiveDelimiterLexV1:110
             $ast (mm0:test:runtime-live-syntax-input))))
       MM0RuntimeLiveSyntaxLexAcceptedV1)))

; Primary syntax accepts the math string as opaque data; the live syntax fold
; then rejects its unsupported adjacent-scalar delimiter.
!(let $mm0-langdef (langdef:load "langdef/mm0/langdef.metta")
   (let
     (LangDef:ParseAccepted MM0TextualV1 $digest ($ast))
     (langdef:parse-file $mm0-langdef
       "tests/langdef/mm0/invalid_multi_scalar_delimiter.mm0")
     (collapse
       (eval
         (quote
           (gslt:fn:MM0DelimiterEnvironmentFromAstV1:10 $ast))))))
