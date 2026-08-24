; The source AST is runtime data.  The generated rho program is fixed.
!(gslt-rho:query:MM0PrecedenceFromAstV1
   QueryMM0NotationPrecedenceV1
   (MM0PrecedenceFromAstV1
     (node mm0-number (pair (cp 53) (cons (cp 48) nil)))
     (MM0FinitePrecedenceV1 $precedence)))

!(gslt-rho:query:MM0NotationConstantV1
   QueryMM0NotationConstantV1
   (MM0NotationConstantV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         MM0MathClassTableNilV1))
     (node mm0-math-string
       (cons (cp 32) (cons (cp 126) (cons (cp 32) nil))))
     (MM0CheckAcceptedV1
       (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))))

!(gslt-rho:query:MM0MathScalarsFromListV1
   QueryMM0NotationScalarCodecV1
   (MM0MathScalarsFromListV1
     (cons (cp 32) (cons (cp 126) (cons (cp 32) nil)))
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 126)
         (MM0MathScalarsConsV1 (cp 32) MM0MathScalarsNilV1)))))

!(gslt-rho:query:MM0SecondaryLexV1
   QueryMM0NotationSecondaryLexV1
   (MM0SecondaryLexV1
     (MM0MathClassTableConsV1
       (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
         MM0MathClassTableNilV1))
     (MM0MathScalarsConsV1 (cp 32)
       (MM0MathScalarsConsV1 (cp 126)
         (MM0MathScalarsConsV1 (cp 32) MM0MathScalarsNilV1)))
     (MM0MathTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))
       MM0MathTokensNilV1)))

!(gslt-rho:query:MM0NotationConstantClassV1
   QueryMM0NotationConstantClassV1
   (MM0NotationConstantClassV1
     (MM0MathTokensConsV1
       (MM0MathTokenV1
         (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))
       MM0MathTokensNilV1)
     (cons (cp 32) (cons (cp 126) (cons (cp 32) nil)))
     (MM0CheckAcceptedV1
       (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))))

!(gslt-rho:query:MM0NotationTokenAllowedV1
   QueryMM0NotationTokenAllowedV1
   (MM0NotationTokenAllowedV1
     (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1)
     (MM0CheckAcceptedV1
       (MM0MathScalarsConsV1 (cp 126) MM0MathScalarsNilV1))))

; The same live-notation transition can be invoked directly, independently of
; the outer file fold.
!(gslt-rho:query:MM0NotationStepV1
   QueryMM0NotationPrefixStepV1
   (MM0NotationStepV1
     NatZeroV1
     (node mm0-simple-notation-statement
       (pair
         (node mm0-prefix-kind prefix-source-v1)
         (pair
           (node mm0-identifier
             (pair (cp 110) (cons (cp 101) (cons (cp 103) nil))))
           (pair
             (node mm0-math-string
               (cons (cp 32) (cons (cp 126) (cons (cp 32) nil))))
             (node mm0-number
               (pair (cp 53) (cons (cp 48) nil)))))))
     (MM0NotationEnvironmentV1
       (MM0MathClassTableConsV1
         (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
           MM0MathClassTableNilV1))
       MM0ConstantEntriesNilV1
       MM0PrefixEntriesNilV1
       MM0InfixEntriesNilV1)
     (MM0DeclarationEntriesConsV1
       (MM0TermEntryV1
         (cons (cp 110) (cons (cp 101) (cons (cp 103) nil)))
         (MM0TermSignatureV1
           (cons
             (MM0BinderV1
               MM0RegularBinderKindV1 MM0AnonymousBinderV1
               (MM0TypeV1
                 (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
                 nil))
             nil)
           (MM0TypeV1
             (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
             nil))
         NatZeroV1 MM0RuntimeTextualSourceV1)
       MM0DeclarationEntriesNilV1)
     (MM0NotationStepAcceptedV1 $after $history)))

!(gslt-rho:query:MM0SourceEnvironmentFromAstV1
   QueryMM0NotationEnvironmentEmptyV1
   (MM0SourceEnvironmentFromAstV1
     (node mm0-file nil)
     MM0AcceptedV1
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
     MM0SourceHistoryNilV1))

!(gslt-rho:query:MM0SourceEnvironmentFromAstV1
   QueryMM0NotationEnvironmentPrefixV1
   (MM0SourceEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 119)
                       (cons (cp 102) (cons (cp 102) nil)))))))))
         (cons
           (node mm0-term-statement
             (pair
               (node mm0-identifier
                 (pair (cp 110) (cons (cp 101) (cons (cp 103) nil))))
               (pair nil
                 (node mm0-arrow-type
                   (pair
                     (node mm0-type
                       (pair
                         (node mm0-identifier
                           (pair (cp 119)
                             (cons (cp 102) (cons (cp 102) nil))))
                         nil))
                     (some
                       (node mm0-arrow-type
                         (pair
                           (node mm0-type
                             (pair
                               (node mm0-identifier
                                 (pair (cp 119)
                                   (cons (cp 102) (cons (cp 102) nil))))
                               nil))
                           none))))))))
           (cons
             (node mm0-simple-notation-statement
               (pair
                 (node mm0-prefix-kind
                   (cons (cp 112)
                     (cons (cp 114)
                       (cons (cp 101)
                         (cons (cp 102)
                           (cons (cp 105) (cons (cp 120) nil)))))))
                 (pair
                   (node mm0-identifier
                     (pair (cp 110) (cons (cp 101) (cons (cp 103) nil))))
                   (pair
                     (node mm0-math-string
                       (cons (cp 32) (cons (cp 126) (cons (cp 32) nil))))
                     (node mm0-number
                       (pair (cp 53) (cons (cp 48) nil)))))))
             nil))))
     MM0AcceptedV1 $state $history))

; A notation declaration cannot name an absent term.
!(gslt-rho:query:MM0SourceEnvironmentFromAstV1
   QueryMM0NotationEnvironmentUnknownTermV1
   (MM0SourceEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 119)
                       (cons (cp 102) (cons (cp 102) nil)))))))))
         (cons
           (node mm0-simple-notation-statement
             (pair
               (node mm0-prefix-kind
                 (cons (cp 112)
                   (cons (cp 114)
                     (cons (cp 101)
                       (cons (cp 102)
                         (cons (cp 105) (cons (cp 120) nil)))))))
               (pair
                 (node mm0-identifier
                   (pair (cp 109)
                     (cons (cp 105)
                       (cons (cp 115)
                         (cons (cp 115)
                           (cons (cp 105)
                             (cons (cp 110) (cons (cp 103) nil))))))))
                 (pair
                   (node mm0-math-string
                     (cons (cp 32) (cons (cp 126) (cons (cp 32) nil))))
                   (node mm0-number
                     (pair (cp 53) (cons (cp 48) nil)))))))
           nil)))
     (MM0RefusedUnknownNotationTermV1
       (NatSuccV1 NatZeroV1)
       (cons (cp 109)
         (cons (cp 105)
           (cons (cp 115)
             (cons (cp 115)
               (cons (cp 105)
                 (cons (cp 110) (cons (cp 103) nil))))))))
     $state $history))

; The same source cannot also be accepted.
!(gslt-rho:query:MM0SourceEnvironmentFromAstV1
   QueryMM0NotationEnvironmentUnknownTermAcceptedV1
   (MM0SourceEnvironmentFromAstV1
     (node mm0-file
       (cons
         (node mm0-sort-statement
           (pair none
             (pair none
               (pair none
                 (pair none
                   (node mm0-identifier
                     (pair (cp 119)
                       (cons (cp 102) (cons (cp 102) nil)))))))))
         (cons
           (node mm0-simple-notation-statement
             (pair
               (node mm0-prefix-kind
                 (cons (cp 112)
                   (cons (cp 114)
                     (cons (cp 101)
                       (cons (cp 102)
                         (cons (cp 105) (cons (cp 120) nil)))))))
               (pair
                 (node mm0-identifier
                   (pair (cp 109)
                     (cons (cp 105)
                       (cons (cp 115)
                         (cons (cp 115)
                           (cons (cp 105)
                             (cons (cp 110) (cons (cp 103) nil))))))))
                 (pair
                   (node mm0-math-string
                     (cons (cp 32) (cons (cp 126) (cons (cp 32) nil))))
                   (node mm0-number
                     (pair (cp 53) (cons (cp 48) nil)))))))
           nil)))
     MM0AcceptedV1 $state $history))
