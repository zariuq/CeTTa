; Runtime data for the direct rho target.  The generated program contains only
; the reusable assertion semantics derived from the authored MM0 coGSLTs.

!(gslt-rho:query:MM0AssertionValidateV1
   QueryMM0AssertionEnvironmentAcceptedV1
   (MM0AssertionValidateV1
     NatZeroV1
     (cons (cp 97) (cons (cp 120) nil))
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         (MM0SortEntriesConsV1
           (MM0SortEntryV1
             (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
             (MM0SortModifiersV1
               MM0ModifierAbsentV1 MM0ModifierAbsentV1
               MM0ModifierPresentV1 MM0ModifierAbsentV1)
             NatZeroV1)
           MM0SortEntriesNilV1)
         (MM0DeclarationEntriesConsV1
           (MM0TermEntryV1
             (cons (cp 116)
               (cons (cp 114)
                 (cons (cp 117) (cons (cp 116) (cons (cp 104) nil)))))
             (MM0TermSignatureV1 nil
               (MM0TypeV1
                 (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))) nil))
             (NatSuccV1 NatZeroV1)
             MM0RuntimeTextualSourceV1)
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
     nil
     (node mm0-formula-arrow-type
       (pair
         (node mm0-math-string
           (cons (cp 32)
             (cons (cp 116)
               (cons (cp 114)
                 (cons (cp 117)
                   (cons (cp 116) (cons (cp 104) (cons (cp 32) nil))))))))
         none))
     (MM0CheckAcceptedV1
       (MM0AssertionSignatureV1 nil nil
         (MM0ExpressionTermApplicationV1
           (cons (cp 116)
             (cons (cp 114)
               (cons (cp 117) (cons (cp 116) (cons (cp 104) nil)))))
           MM0ExpressionArgumentsNilV1
           (cons (cp 119) (cons (cp 102) (cons (cp 102) nil))))))))

!(gslt-rho:query:MM0AssertionValidateV1
   QueryMM0AssertionEnvironmentTypedConclusionRefusedV1
   (MM0AssertionValidateV1
     (NatSuccV1 NatZeroV1)
     (cons (cp 98) (cons (cp 97) (cons (cp 100) nil)))
     (MM0SourceStateV1
       (MM0TextualEnvironmentV1
         (MM0SortEntriesConsV1
           (MM0SortEntryV1
             (cons (cp 119) (cons (cp 102) (cons (cp 102) nil)))
             (MM0SortModifiersV1
               MM0ModifierAbsentV1 MM0ModifierAbsentV1
               MM0ModifierPresentV1 MM0ModifierAbsentV1)
             NatZeroV1)
           MM0SortEntriesNilV1)
         MM0DeclarationEntriesNilV1)
       (MM0NotationEnvironmentV1
         (MM0MathClassTableConsV1
           (MM0MathClassEntryV1 (cp 32) MM0MathWhitespaceV1)
           (MM0MathClassTableConsV1
             (MM0MathClassEntryV1 (cp 10) MM0MathWhitespaceV1)
             MM0MathClassTableNilV1))
         MM0ConstantEntriesNilV1
         MM0PrefixEntriesNilV1
         MM0InfixEntriesNilV1))
     nil
     (node mm0-formula-arrow-type
       (pair
         (node mm0-type
           (pair
             (node mm0-identifier
               (pair (cp 119) (cons (cp 102) (cons (cp 102) nil))))
             nil))
         none))
     (MM0CheckRefusedV1
       (MM0RefusedAssertionConclusionTypeV1
         (NatSuccV1 NatZeroV1)
         (cons (cp 98) (cons (cp 97) (cons (cp 100) nil)))))))
