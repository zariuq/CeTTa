; Shared test values are constructors, not executable MM0 database facts.
(= (mm0:test:base-table)
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
             MM0MathClassTableNilV1))))))

(= (mm0:test:whitespace-table)
   (MM0MathClassTableConsV1
     (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
       MM0MathClassTableNilV1)))

; MM0 specification example: '(' splits after, ')' before, and '*' both.
!(let $table (mm0:test:base-table)
   (let
     (gslt:result:MM0SecondaryLexV1:110
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
                 (MM0MathScalarsConsV1 (cp 42) MM0MathScalarsNilV1))
               (MM0MathTokensConsV1
                 (MM0MathTokenV1
                   (MM0MathScalarsConsV1 (cp 42) MM0MathScalarsNilV1))
                 (MM0MathTokensConsV1
                   (MM0MathTokenV1
                     (MM0MathScalarsConsV1 (cp 123)
                       (MM0MathScalarsConsV1 (cp 125) MM0MathScalarsNilV1)))
                   (MM0MathTokensConsV1
                     (MM0MathTokenV1
                       (MM0MathScalarsConsV1 (cp 41) MM0MathScalarsNilV1))
                     (MM0MathTokensConsV1
                       (MM0MathTokenV1
                         (MM0MathScalarsConsV1 (cp 99) MM0MathScalarsNilV1))
                       MM0MathTokensNilV1)))))))))
     (gslt:fn:MM0SecondaryLexV1:110
       $table
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
                             MM0MathScalarsNilV1))))))))))))
     MM0SecondaryLexSpecExampleAcceptedV1))

; Directional delimiters retain the delimiter on the documented side.
!(let $table (mm0:test:base-table)
   (let
     (gslt:result:MM0SecondaryLexV1:110
       (MM0MathTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1 (cp 97) MM0MathScalarsNilV1))
         (MM0MathTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 41)
               (MM0MathScalarsConsV1 (cp 98) MM0MathScalarsNilV1)))
           MM0MathTokensNilV1)))
     (gslt:fn:MM0SecondaryLexV1:110
       $table
       (MM0MathScalarsConsV1 (cp 97)
         (MM0MathScalarsConsV1 (cp 41)
           (MM0MathScalarsConsV1 (cp 98) MM0MathScalarsNilV1))))
     MM0SecondaryLexRightDelimiterAcceptedV1))

!(let $table (mm0:test:base-table)
   (let
     (gslt:result:MM0SecondaryLexV1:110
       (MM0MathTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1 (cp 97)
             (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1)))
         (MM0MathTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 98) MM0MathScalarsNilV1))
           MM0MathTokensNilV1)))
     (gslt:fn:MM0SecondaryLexV1:110
       $table
       (MM0MathScalarsConsV1 (cp 97)
         (MM0MathScalarsConsV1 (cp 40)
           (MM0MathScalarsConsV1 (cp 98) MM0MathScalarsNilV1))))
     MM0SecondaryLexLeftDelimiterAcceptedV1))

; Empty input and repeated whitespace produce no empty tokens.
!(let $table (mm0:test:whitespace-table)
   (let
     (gslt:result:MM0SecondaryLexV1:110 MM0MathTokensNilV1)
     (gslt:fn:MM0SecondaryLexV1:110 $table MM0MathScalarsNilV1)
     MM0SecondaryLexEmptyAcceptedV1))

!(let $table (mm0:test:whitespace-table)
   (let
     (gslt:result:MM0SecondaryLexV1:110 MM0MathTokensNilV1)
     (gslt:fn:MM0SecondaryLexV1:110
       $table
       (MM0MathScalarsConsV1 (cp 32)
         (MM0MathScalarsConsV1 (cp 10)
           (MM0MathScalarsConsV1 (cp 32) MM0MathScalarsNilV1))))
     MM0SecondaryLexWhitespaceAcceptedV1))

; Duplicate classifications, missing mandatory newline, and foreign whitespace fail closed.
!(collapse
   (gslt:fn:MM0SecondaryLexV1:110
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 32) MM0MathOrdinaryV1)
           MM0MathClassTableNilV1)))
     MM0MathScalarsNilV1))

!(collapse
   (gslt:fn:MM0SecondaryLexV1:110
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       MM0MathClassTableNilV1)
     MM0MathScalarsNilV1))

!(collapse
   (gslt:fn:MM0SecondaryLexV1:110
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 9) MM0MathWhitespaceV1)
           MM0MathClassTableNilV1)))
     MM0MathScalarsNilV1))
