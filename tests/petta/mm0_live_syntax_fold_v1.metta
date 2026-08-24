(= (mm0:test:live-syntax-ast)
   (node mm0-file
     (cons
       (node mm0-delimiter-statement
         (pair
           (node mm0-math-string
             (cons (cp 32) (cons (cp 40) (cons (cp 32) nil))))
           (some
             (node mm0-math-string
               (cons (cp 32) (cons (cp 41) (cons (cp 32) nil)))))))
       (cons
         (node mm0-sort-statement MM0IgnoredSortPayloadV1)
         (cons
           (node mm0-delimiter-statement
             (pair
               (node mm0-math-string
                 (cons (cp 32)
                   (cons (cp 42)
                     (cons (cp 32)
                       (cons (cp 40) (cons (cp 32) nil))))))
               none))
           nil)))))

(= (mm0:test:live-syntax-environment)
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

; The semantic AST is runtime constructor data. Its ordered delimiter
; declarations produce the live secondary-lexer environment.
!(let (gslt:result:MM0DelimiterEnvironmentFromAstV1:10
         (mm0:test:live-syntax-environment))
      (gslt:fn:MM0DelimiterEnvironmentFromAstV1:10
        (mm0:test:live-syntax-ast))
   MM0LiveSyntaxEnvironmentAcceptedV1)

; The same derived environment feeds the separately authored secondary lexer.
!(let
   (gslt:result:MM0LiveDelimiterLexV1:110
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
   (gslt:fn:MM0LiveDelimiterLexV1:110
     (mm0:test:live-syntax-ast)
     (MM0MathScalarsConsV1 (cp 97)
       (MM0MathScalarsConsV1 (cp 42)
         (MM0MathScalarsConsV1 (cp 98)
           (MM0MathScalarsConsV1 (cp 40)
             (MM0MathScalarsConsV1 (cp 99)
               (MM0MathScalarsConsV1 (cp 41)
                 (MM0MathScalarsConsV1 (cp 32)
                   (MM0MathScalarsConsV1
                     (cp 100) MM0MathScalarsNilV1)))))))))
   MM0LiveSyntaxLexAcceptedV1)

; An empty source has only the two whitespace classifications.
!(let
   (gslt:result:MM0DelimiterEnvironmentFromAstV1:10
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         MM0MathClassTableNilV1)))
   (gslt:fn:MM0DelimiterEnvironmentFromAstV1:10
     (node mm0-file nil))
   MM0LiveSyntaxEmptyAcceptedV1)

; Adjacent non-whitespace scalars would be one multi-character delimiter,
; which this single-scalar MM0 profile rejects.
!(collapse
   (gslt:fn:MM0DelimiterEnvironmentFromAstV1:10
     (node mm0-file
       (cons
         (node mm0-delimiter-statement
           (pair
             (node mm0-math-string
               (cons (cp 40) (cons (cp 41) nil)))
             none))
         nil))))

; A non-MM0 root is outside this fold's authored input shape.
!(collapse
   (gslt:fn:MM0DelimiterEnvironmentFromAstV1:10
     (node foreign-file nil)))
