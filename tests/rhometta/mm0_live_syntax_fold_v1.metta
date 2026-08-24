!(gslt-rho:query:MM0DelimiterEnvironmentFromAstV1
   QueryMM0LiveSyntaxEnvironmentV1
   (MM0DelimiterEnvironmentFromAstV1
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
             nil))))
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
               MM0MathClassTableNilV1)))))))

!(gslt-rho:query:MM0LiveDelimiterLexV1
   QueryMM0LiveSyntaxLexV1
   (MM0LiveDelimiterLexV1
     (node mm0-file
       (cons
         (node mm0-delimiter-statement
           (pair
             (node mm0-math-string
               (cons (cp 40) nil))
             (some (node mm0-math-string (cons (cp 41) nil)))))
         (cons
           (node mm0-delimiter-statement
             (pair (node mm0-math-string (cons (cp 42) nil)) none))
           nil)))
     (MM0MathScalarsConsV1 (cp 97)
       (MM0MathScalarsConsV1 (cp 42)
         (MM0MathScalarsConsV1 (cp 98)
           (MM0MathScalarsConsV1 (cp 40)
             (MM0MathScalarsConsV1 (cp 99)
               (MM0MathScalarsConsV1 (cp 41)
                 (MM0MathScalarsConsV1 (cp 32)
                   (MM0MathScalarsConsV1
                     (cp 100) MM0MathScalarsNilV1))))))))
     (MM0MathTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 97) MM0MathScalarsNilV1))
       (MM0MathTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1 (cp 42) MM0MathScalarsNilV1))
         (MM0MathTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 98)
               (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1)))
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

!(gslt-rho:query:MM0DelimiterEnvironmentFromAstV1
   QueryMM0LiveSyntaxEmptyV1
   (MM0DelimiterEnvironmentFromAstV1
     (node mm0-file nil)
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         MM0MathClassTableNilV1))))

!(gslt-rho:query:MM0DelimiterEnvironmentFromAstV1
   QueryMM0LiveSyntaxAdjacentScalarsV1
   (MM0DelimiterEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-delimiter-statement
           (pair
             (node mm0-math-string
               (cons (cp 40) (cons (cp 41) nil)))
             none))
         nil))
     $environment))

!(gslt-rho:query:MM0DelimiterEnvironmentFromAstV1
   QueryMM0LiveSyntaxForeignRootV1
   (MM0DelimiterEnvironmentFromAstV1
     (node foreign-file nil) $environment))
