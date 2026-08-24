; Right-associative parsing from a runtime notation value.
!(gslt-rho:query:MM0RuntimeMathParseV1
   QueryMM0MathParserRightAssociativeV1
   (MM0RuntimeMathParseV1
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         MM0SortEntriesNilV1 MM0DeclarationEntriesNilV1)
       (MM0NotationEnvironmentV1
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
           MM0MathClassTableNilV1))
       MM0ConstantEntriesNilV1
       MM0PrefixEntriesNilV1
       (MM0InfixEntriesConsV1
         (MM0InfixNotationEntryV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1))
           (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
           MM0RightAssociativeV1
           (MM0FinitePrecedenceV1 (cons (cp 49) nil))
           NatZeroV1)
         MM0InfixEntriesNilV1)))
     (MM0MathScopeConsV1
       (cons (cp 120) nil)
       (MM0MathScopeConsV1
         (cons (cp 121) nil) MM0MathScopeNilV1))
     (MM0MathScalarsConsV1 (cp 120)
       (MM0MathScalarsConsV1 (cp 32)
         (MM0MathScalarsConsV1 (cp 45)
           (MM0MathScalarsConsV1 (cp 62)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1 (cp 121)
                 (MM0MathScalarsConsV1 (cp 32)
                   (MM0MathScalarsConsV1 (cp 45)
                     (MM0MathScalarsConsV1 (cp 62)
                       (MM0MathScalarsConsV1 (cp 32)
                         (MM0MathScalarsConsV1
                           (cp 120) MM0MathScalarsNilV1)))))))))))
     (GSLTPrattApplicationV1
       (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
       (GSLTPrattArgumentsConsV1
         (GSLTPrattIdentifierApplicationV1
           (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
         (GSLTPrattArgumentsConsV1
           (GSLTPrattApplicationV1
             (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
             (GSLTPrattArgumentsConsV1
               (GSLTPrattIdentifierApplicationV1
                 (cons (cp 121) nil) GSLTPrattArgumentsNilV1)
               (GSLTPrattArgumentsConsV1
                 (GSLTPrattIdentifierApplicationV1
                   (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
                 GSLTPrattArgumentsNilV1)))
           GSLTPrattArgumentsNilV1)))))

; Prefix parsing uses the same runtime environment interface.
!(gslt-rho:query:MM0RuntimeMathParseV1
   QueryMM0MathParserPrefixV1
   (MM0RuntimeMathParseV1
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         MM0SortEntriesNilV1 MM0DeclarationEntriesNilV1)
       (MM0NotationEnvironmentV1
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
           MM0MathClassTableNilV1))
       MM0ConstantEntriesNilV1
       (MM0PrefixEntriesConsV1
         (MM0PrefixNotationEntryV1
           (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1)
           (cons (cp 110) (cons (cp 101) (cons (cp 103) nil)))
           (NatSuccV1 NatZeroV1)
           (MM0FinitePrecedenceV1 (cons (cp 50) nil))
           NatZeroV1)
         MM0PrefixEntriesNilV1)
       MM0InfixEntriesNilV1))
     (MM0MathScopeConsV1
       (cons (cp 120) nil) MM0MathScopeNilV1)
     (MM0MathScalarsConsV1 (cp 126)
       (MM0MathScalarsConsV1 (cp 32)
         (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1)))
     (GSLTPrattApplicationV1
       (cons (cp 110) (cons (cp 101) (cons (cp 103) nil)))
       (GSLTPrattArgumentsConsV1
         (GSLTPrattIdentifierApplicationV1
           (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
         GSLTPrattArgumentsNilV1))))

; Ordinary identifier application is resolved from declarations in the live
; textual environment.  It is not generated from the source file.
!(gslt-rho:query:MM0RuntimeMathParseV1
   QueryMM0MathParserIdentifierApplicationV1
   (MM0RuntimeMathParseV1
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         MM0SortEntriesNilV1
         (MM0DeclarationEntriesConsV1
           (MM0TermEntryV1
             (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
             (MM0TermSignatureV1
               (cons MM0MathParserTestBinderV1
                 (cons MM0MathParserTestBinderV1 nil))
               MM0MathParserTestReturnV1)
             NatZeroV1 nil)
           MM0DeclarationEntriesNilV1))
       (MM0NotationEnvironmentV1
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
           (MM0MathClassTableConsV1
             (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
             MM0MathClassTableNilV1))
         MM0ConstantEntriesNilV1
         MM0PrefixEntriesNilV1
         MM0InfixEntriesNilV1))
     (MM0MathScopeConsV1
       (cons (cp 120) nil)
       (MM0MathScopeConsV1
         (cons (cp 121) nil) MM0MathScopeNilV1))
     (MM0MathScalarsConsV1 (cp 105)
       (MM0MathScalarsConsV1 (cp 109)
         (MM0MathScalarsConsV1 (cp 112)
           (MM0MathScalarsConsV1 (cp 32)
             (MM0MathScalarsConsV1 (cp 120)
               (MM0MathScalarsConsV1 (cp 32)
                 (MM0MathScalarsConsV1
                   (cp 121) MM0MathScalarsNilV1)))))))
     (GSLTPrattIdentifierApplicationV1
       (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
       (GSLTPrattArgumentsConsV1
         (GSLTPrattIdentifierApplicationV1
           (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
         (GSLTPrattArgumentsConsV1
           (GSLTPrattIdentifierApplicationV1
             (cons (cp 121) nil) GSLTPrattArgumentsNilV1)
           GSLTPrattArgumentsNilV1)))))

; An undeclared identifier is not admitted into the math tree.
!(gslt-rho:query:MM0RuntimeMathParseV1
   QueryMM0MathParserUnknownIdentifierAcceptedV1
   (MM0RuntimeMathParseV1
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         MM0SortEntriesNilV1 MM0DeclarationEntriesNilV1)
       (MM0NotationEnvironmentV1
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
           (MM0MathClassTableConsV1
             (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
             MM0MathClassTableNilV1))
         MM0ConstantEntriesNilV1
         MM0PrefixEntriesNilV1
         MM0InfixEntriesNilV1))
     MM0MathScopeNilV1
     (MM0MathScalarsConsV1 (cp 122) MM0MathScalarsNilV1)
     $tree))

; A missing right operand must have no observation.
!(gslt-rho:query:MM0RuntimeMathParseV1
   QueryMM0MathParserIncompleteAcceptedV1
   (MM0RuntimeMathParseV1
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         MM0SortEntriesNilV1 MM0DeclarationEntriesNilV1)
       (MM0NotationEnvironmentV1
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
           MM0MathClassTableNilV1))
       MM0ConstantEntriesNilV1
       MM0PrefixEntriesNilV1
       (MM0InfixEntriesConsV1
         (MM0InfixNotationEntryV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1))
           (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
           MM0RightAssociativeV1
           (MM0FinitePrecedenceV1 (cons (cp 49) nil))
           NatZeroV1)
         MM0InfixEntriesNilV1)))
     (MM0MathScopeConsV1
       (cons (cp 120) nil) MM0MathScopeNilV1)
     (MM0MathScalarsConsV1 (cp 120)
       (MM0MathScalarsConsV1 (cp 32)
         (MM0MathScalarsConsV1 (cp 45)
           (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1))))
     $tree))

; The generic parser boundary can also be observed independently of the
; secondary lexer.  This keeps lexer and precedence failures distinguishable.
!(gslt-rho:query:GSLTPrattParseAllV1
   QueryMM0PrattRightAssociativeV1
   (GSLTPrattParseAllV1
     (MM0MathEnvironmentV1 MM0DeclarationEntriesNilV1
       (MM0NotationEnvironmentV1
       MM0MathClassTableNilV1
       MM0ConstantEntriesNilV1
       MM0PrefixEntriesNilV1
       (MM0InfixEntriesConsV1
         (MM0InfixNotationEntryV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1))
           (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
           MM0RightAssociativeV1
           (MM0FinitePrecedenceV1 (cons (cp 49) nil))
           NatZeroV1)
         MM0InfixEntriesNilV1)))
     (MM0MathScopeConsV1
       (cons (cp 120) nil)
       (MM0MathScopeConsV1
         (cons (cp 121) nil) MM0MathScopeNilV1))
     (GSLTPrattTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1))
       (GSLTPrattTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
         (GSLTPrattTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 121) MM0MathScalarsNilV1))
           (GSLTPrattTokensConsV1
             (MM0MathTokenV1
               (MM0MathScalarsConsV1
                 (cp 45)
                 (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
             (GSLTPrattTokensConsV1
               (MM0MathTokenV1
                 (MM0MathScalarsConsV1
                   (cp 120) MM0MathScalarsNilV1))
               GSLTPrattTokensNilV1)))))
     (GSLTPrattApplicationV1
       (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
       (GSLTPrattArgumentsConsV1
         (GSLTPrattIdentifierApplicationV1
           (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
         (GSLTPrattArgumentsConsV1
           (GSLTPrattApplicationV1
             (cons (cp 105) (cons (cp 109) (cons (cp 112) nil)))
             (GSLTPrattArgumentsConsV1
               (GSLTPrattIdentifierApplicationV1
                 (cons (cp 121) nil) GSLTPrattArgumentsNilV1)
               (GSLTPrattArgumentsConsV1
                 (GSLTPrattIdentifierApplicationV1
                   (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
                 GSLTPrattArgumentsNilV1)))
           GSLTPrattArgumentsNilV1)))))

!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0MathParserRightLexV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         MM0MathClassTableNilV1))
     (MM0MathScalarsConsV1 (cp 120)
       (MM0MathScalarsConsV1 (cp 32)
         (MM0MathScalarsConsV1 (cp 45)
           (MM0MathScalarsConsV1 (cp 62)
             (MM0MathScalarsConsV1 (cp 32)
               (MM0MathScalarsConsV1 (cp 121)
                 (MM0MathScalarsConsV1 (cp 32)
                   (MM0MathScalarsConsV1 (cp 45)
                     (MM0MathScalarsConsV1 (cp 62)
                       (MM0MathScalarsConsV1 (cp 32)
                         (MM0MathScalarsConsV1
                           (cp 120) MM0MathScalarsNilV1)))))))))))
     (MM0MathTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1))
       (MM0MathTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
         (MM0MathTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 121) MM0MathScalarsNilV1))
           (MM0MathTokensConsV1
             (MM0MathTokenV1
               (MM0MathScalarsConsV1
                 (cp 45)
                 (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
             (MM0MathTokensConsV1
               (MM0MathTokenV1
                 (MM0MathScalarsConsV1
                   (cp 120) MM0MathScalarsNilV1))
               MM0MathTokensNilV1)))))))

!(gslt-rho:query:MM0MathTokensToPrattV1
   QueryMM0MathParserRightTokenCodecV1
   (MM0MathTokensToPrattV1
     (MM0MathTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1))
       (MM0MathTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
         (MM0MathTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 121) MM0MathScalarsNilV1))
           (MM0MathTokensConsV1
             (MM0MathTokenV1
               (MM0MathScalarsConsV1
                 (cp 45)
                 (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
             (MM0MathTokensConsV1
               (MM0MathTokenV1
                 (MM0MathScalarsConsV1
                   (cp 120) MM0MathScalarsNilV1))
               MM0MathTokensNilV1)))))
     (GSLTPrattTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1))
       (GSLTPrattTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1
             (cp 45)
             (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
         (GSLTPrattTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 121) MM0MathScalarsNilV1))
           (GSLTPrattTokensConsV1
             (MM0MathTokenV1
               (MM0MathScalarsConsV1
                 (cp 45)
                 (MM0MathScalarsConsV1 (cp 62) MM0MathScalarsNilV1)))
             (GSLTPrattTokensConsV1
               (MM0MathTokenV1
                 (MM0MathScalarsConsV1
                   (cp 120) MM0MathScalarsNilV1))
               GSLTPrattTokensNilV1)))))))

; The other associativity branch is target-independent parser behavior.
!(gslt-rho:query:GSLTPrattParseAllV1
   QueryMM0PrattLeftAssociativeV1
   (GSLTPrattParseAllV1
     (MM0MathEnvironmentV1 MM0DeclarationEntriesNilV1
       (MM0NotationEnvironmentV1
       MM0MathClassTableNilV1
       MM0ConstantEntriesNilV1
       MM0PrefixEntriesNilV1
       (MM0InfixEntriesConsV1
         (MM0InfixNotationEntryV1
           (MM0MathScalarsConsV1 (cp 38) MM0MathScalarsNilV1)
           (cons (cp 97) (cons (cp 110) (cons (cp 100) nil)))
           MM0LeftAssociativeV1
           (MM0FinitePrecedenceV1 (cons (cp 49) nil))
           NatZeroV1)
         MM0InfixEntriesNilV1)))
     (MM0MathScopeConsV1
       (cons (cp 120) nil)
       (MM0MathScopeConsV1
         (cons (cp 121) nil) MM0MathScopeNilV1))
     (GSLTPrattTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 120) MM0MathScalarsNilV1))
       (GSLTPrattTokensConsV1
         (MM0MathTokenV1
           (MM0MathScalarsConsV1 (cp 38) MM0MathScalarsNilV1))
         (GSLTPrattTokensConsV1
           (MM0MathTokenV1
             (MM0MathScalarsConsV1 (cp 121) MM0MathScalarsNilV1))
           (GSLTPrattTokensConsV1
             (MM0MathTokenV1
               (MM0MathScalarsConsV1 (cp 38) MM0MathScalarsNilV1))
             (GSLTPrattTokensConsV1
               (MM0MathTokenV1
                 (MM0MathScalarsConsV1
                   (cp 120) MM0MathScalarsNilV1))
               GSLTPrattTokensNilV1)))))
     (GSLTPrattApplicationV1
       (cons (cp 97) (cons (cp 110) (cons (cp 100) nil)))
       (GSLTPrattArgumentsConsV1
         (GSLTPrattApplicationV1
           (cons (cp 97) (cons (cp 110) (cons (cp 100) nil)))
           (GSLTPrattArgumentsConsV1
             (GSLTPrattIdentifierApplicationV1
               (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
             (GSLTPrattArgumentsConsV1
               (GSLTPrattIdentifierApplicationV1
                 (cons (cp 121) nil) GSLTPrattArgumentsNilV1)
               GSLTPrattArgumentsNilV1)))
         (GSLTPrattArgumentsConsV1
           (GSLTPrattIdentifierApplicationV1
             (cons (cp 120) nil) GSLTPrattArgumentsNilV1)
           GSLTPrattArgumentsNilV1)))))
