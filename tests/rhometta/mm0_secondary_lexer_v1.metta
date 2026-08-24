; The MM0 specification example, with the token stream left observable.
!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0SecondaryLexSpecExampleV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 42) MM0MathBothDelimiterV1)
           (MM0MathClassTableConsV1
             (MM0MathClassEntryV1 (cp 40) MM0MathLeftDelimiterV1)
             (MM0MathClassTableConsV1
               (MM0MathClassEntryV1 (cp 41) MM0MathRightDelimiterV1)
               MM0MathClassTableNilV1)))))
     (MM0MathScalarsConsV1 (cp 97)
       (MM0MathScalarsConsV1 (cp 42)
         (MM0MathScalarsConsV1 (cp 98)
           (MM0MathScalarsConsV1 (cp 40)
             (MM0MathScalarsConsV1 (cp 42)
               (MM0MathScalarsConsV1 (cp 42)
                 (MM0MathScalarsConsV1 (cp 123)
                   (MM0MathScalarsConsV1 (cp 125)
                     (MM0MathScalarsConsV1 (cp 41)
                       (MM0MathScalarsConsV1 (cp 32)
                         (MM0MathScalarsConsV1 (cp 99)
                           MM0MathScalarsNilV1)))))))))))
     $tokens))

!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0SecondaryLexEmptyV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         MM0MathClassTableNilV1))
     MM0MathScalarsNilV1
     MM0MathTokensNilV1))

; Each malformed class table must have no observation.
!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0SecondaryLexDuplicateClassV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 32) MM0MathOrdinaryV1)
           MM0MathClassTableNilV1)))
     MM0MathScalarsNilV1
     $tokens))

!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0SecondaryLexMissingNewlineV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       MM0MathClassTableNilV1)
     MM0MathScalarsNilV1
     $tokens))

!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0SecondaryLexForeignWhitespaceV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 9) MM0MathWhitespaceV1)
           MM0MathClassTableNilV1)))
     MM0MathScalarsNilV1
     $tokens))
