; MM0 secondary math lexer as a deterministic relational fold.
;
; The primary parser leaves each math string opaque.  The notation-state fold
; supplies a total scalar-class table whose only whitespace entries are space
; and newline.  This presentation validates that table and transforms the
; scalar stream into forward, positioned-ready token payloads.  Delimiter
; declarations affect data in the class table; they do not generate host code.

(gslt-presentation-v1 MM0SecondaryLexerV1
  (signature
    (operator cp 1)
    (operator MM0MathClassTableConsV1 2)
    (operator MM0MathClassEntryV1 2)
    (operator MM0MathScalarsConsV1 2)
    (operator MM0MathTokensConsV1 2)
    (operator MM0MathTokenV1 1)
    (operator MM0MathLexerStateV1 2)
    (operator different 2)
    (operator MM0MathAllowedWhitespaceV1 1)
    (operator MM0MathClassEntryValidV1 2)
    (operator MM0MathClassTableNotMemberV1 2)
    (operator MM0MathClassTableValidV1 1)
    (operator MM0MathClassFindV1 3)
    (operator MM0MathClassLookupV1 3)
    (operator MM0MathReverseScalarsV1 2)
    (operator MM0MathReverseScalarsAccV1 3)
    (operator MM0MathReverseTokensV1 2)
    (operator MM0MathReverseTokensAccV1 3)
    (operator MM0MathLexerFlushV1 2)
    (operator MM0MathLexerStepV1 4)
    (operator MM0MathLexerFinishV1 2)
    (operator MM0MathLexerLoopV1 4)
    (operator MM0SecondaryLexV1 3))
  (equations)
  (rewrites
    (rule mm0-math-whitespace-space-v1
      (head (MM0MathAllowedWhitespaceV1 (cp 32)))
      (body))
    (rule mm0-math-whitespace-newline-v1
      (head (MM0MathAllowedWhitespaceV1 (cp 10)))
      (body))

    (rule mm0-math-class-entry-ordinary-v1
      (head
        (MM0MathClassEntryValidV1
          ?scalar MM0MathOrdinaryV1))
      (body))
    (rule mm0-math-class-entry-left-v1
      (head
        (MM0MathClassEntryValidV1
          ?scalar MM0MathLeftDelimiterV1))
      (body))
    (rule mm0-math-class-entry-right-v1
      (head
        (MM0MathClassEntryValidV1
          ?scalar MM0MathRightDelimiterV1))
      (body))
    (rule mm0-math-class-entry-both-v1
      (head
        (MM0MathClassEntryValidV1
          ?scalar MM0MathBothDelimiterV1))
      (body))
    (rule mm0-math-class-entry-whitespace-v1
      (head
        (MM0MathClassEntryValidV1
          ?scalar MM0MathWhitespaceV1))
      (body
        (MM0MathAllowedWhitespaceV1 ?scalar)))

    (rule mm0-math-class-table-not-member-nil-v1
      (head
        (MM0MathClassTableNotMemberV1
          ?scalar MM0MathClassTableNilV1))
      (body))
    (rule mm0-math-class-table-not-member-cons-v1
      (head
        (MM0MathClassTableNotMemberV1
          ?scalar
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?other ?class) ?rest)))
      (body
        (different ?scalar ?other)
        (MM0MathClassTableNotMemberV1 ?scalar ?rest)))

    (rule mm0-math-class-table-valid-nil-v1
      (head (MM0MathClassTableValidV1 MM0MathClassTableNilV1))
      (body))
    (rule mm0-math-class-table-valid-cons-v1
      (head
        (MM0MathClassTableValidV1
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?scalar ?class) ?rest)))
      (body
        (MM0MathClassEntryValidV1 ?scalar ?class)
        (MM0MathClassTableNotMemberV1 ?scalar ?rest)
        (MM0MathClassTableValidV1 ?rest)))

    (rule mm0-math-class-find-head-v1
      (head
        (MM0MathClassFindV1
          ?scalar
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?scalar ?class) ?rest)
          ?class))
      (body))
    (rule mm0-math-class-find-tail-v1
      (head
        (MM0MathClassFindV1
          ?scalar
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?other ?class) ?rest)
          ?found))
      (body
        (different ?scalar ?other)
        (MM0MathClassFindV1 ?scalar ?rest ?found)))

    (rule mm0-math-class-lookup-default-v1
      (head
        (MM0MathClassLookupV1
          ?scalar MM0MathClassTableNilV1 MM0MathOrdinaryV1))
      (body))
    (rule mm0-math-class-lookup-head-v1
      (head
        (MM0MathClassLookupV1
          ?scalar
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?scalar ?class) ?rest)
          ?class))
      (body))
    (rule mm0-math-class-lookup-tail-v1
      (head
        (MM0MathClassLookupV1
          ?scalar
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?other ?class) ?rest)
          ?found))
      (body
        (different ?scalar ?other)
        (MM0MathClassLookupV1 ?scalar ?rest ?found)))

    (rule mm0-math-reverse-scalars-v1
      (head (MM0MathReverseScalarsV1 ?input ?output))
      (body
        (MM0MathReverseScalarsAccV1
          ?input MM0MathScalarsNilV1 ?output)))
    (rule mm0-math-reverse-scalars-acc-nil-v1
      (head
        (MM0MathReverseScalarsAccV1
          MM0MathScalarsNilV1 ?accumulator ?accumulator))
      (body))
    (rule mm0-math-reverse-scalars-acc-cons-v1
      (head
        (MM0MathReverseScalarsAccV1
          (MM0MathScalarsConsV1 ?scalar ?rest)
          ?accumulator ?output))
      (body
        (MM0MathReverseScalarsAccV1
          ?rest
          (MM0MathScalarsConsV1 ?scalar ?accumulator)
          ?output)))

    (rule mm0-math-reverse-tokens-v1
      (head (MM0MathReverseTokensV1 ?input ?output))
      (body
        (MM0MathReverseTokensAccV1
          ?input MM0MathTokensNilV1 ?output)))
    (rule mm0-math-reverse-tokens-acc-nil-v1
      (head
        (MM0MathReverseTokensAccV1
          MM0MathTokensNilV1 ?accumulator ?accumulator))
      (body))
    (rule mm0-math-reverse-tokens-acc-cons-v1
      (head
        (MM0MathReverseTokensAccV1
          (MM0MathTokensConsV1 ?token ?rest)
          ?accumulator ?output))
      (body
        (MM0MathReverseTokensAccV1
          ?rest
          (MM0MathTokensConsV1 ?token ?accumulator)
          ?output)))

    (rule mm0-math-lexer-flush-empty-v1
      (head
        (MM0MathLexerFlushV1
          (MM0MathLexerStateV1
            MM0MathScalarsNilV1 ?tokens)
          (MM0MathLexerStateV1
            MM0MathScalarsNilV1 ?tokens)))
      (body))
    (rule mm0-math-lexer-flush-nonempty-v1
      (head
        (MM0MathLexerFlushV1
          (MM0MathLexerStateV1
            (MM0MathScalarsConsV1 ?head ?tail) ?tokens)
          (MM0MathLexerStateV1
            MM0MathScalarsNilV1
            (MM0MathTokensConsV1
              (MM0MathTokenV1 ?forward) ?tokens))))
      (body
        (MM0MathReverseScalarsV1
          (MM0MathScalarsConsV1 ?head ?tail) ?forward)))

    (rule mm0-math-lexer-step-ordinary-v1
      (head
        (MM0MathLexerStepV1
          MM0MathOrdinaryV1 ?scalar
          (MM0MathLexerStateV1 ?current ?tokens)
          (MM0MathLexerStateV1
            (MM0MathScalarsConsV1 ?scalar ?current) ?tokens)))
      (body))
    (rule mm0-math-lexer-step-whitespace-v1
      (head
        (MM0MathLexerStepV1
          MM0MathWhitespaceV1 ?scalar ?before ?after))
      (body
        (MM0MathLexerFlushV1 ?before ?after)))
    (rule mm0-math-lexer-step-left-v1
      (head
        (MM0MathLexerStepV1
          MM0MathLeftDelimiterV1 ?scalar
          (MM0MathLexerStateV1 ?current ?tokens)
          ?after))
      (body
        (MM0MathLexerFlushV1
          (MM0MathLexerStateV1
            (MM0MathScalarsConsV1 ?scalar ?current) ?tokens)
          ?after)))
    (rule mm0-math-lexer-step-right-v1
      (head
        (MM0MathLexerStepV1
          MM0MathRightDelimiterV1 ?scalar ?before
          (MM0MathLexerStateV1
            (MM0MathScalarsConsV1 ?scalar MM0MathScalarsNilV1)
            ?tokens)))
      (body
        (MM0MathLexerFlushV1
          ?before
          (MM0MathLexerStateV1 MM0MathScalarsNilV1 ?tokens))))
    (rule mm0-math-lexer-step-both-v1
      (head
        (MM0MathLexerStepV1
          MM0MathBothDelimiterV1 ?scalar ?before
          (MM0MathLexerStateV1
            MM0MathScalarsNilV1
            (MM0MathTokensConsV1
              (MM0MathTokenV1
                (MM0MathScalarsConsV1
                  ?scalar MM0MathScalarsNilV1))
              ?tokens))))
      (body
        (MM0MathLexerFlushV1
          ?before
          (MM0MathLexerStateV1 MM0MathScalarsNilV1 ?tokens))))

    (rule mm0-math-lexer-finish-v1
      (head (MM0MathLexerFinishV1 ?state ?tokens))
      (body
        (MM0MathLexerFlushV1
          ?state
          (MM0MathLexerStateV1 MM0MathScalarsNilV1 ?reverseTokens))
        (MM0MathReverseTokensV1 ?reverseTokens ?tokens)))

    (rule mm0-math-lexer-loop-nil-v1
      (head
        (MM0MathLexerLoopV1
          ?table MM0MathScalarsNilV1 ?state ?tokens))
      (body
        (MM0MathLexerFinishV1 ?state ?tokens)))
    (rule mm0-math-lexer-loop-cons-v1
      (head
        (MM0MathLexerLoopV1
          ?table
          (MM0MathScalarsConsV1 ?scalar ?rest)
          ?state ?tokens))
      (body
        (MM0MathClassLookupV1 ?scalar ?table ?class)
        (MM0MathLexerStepV1 ?class ?scalar ?state ?next)
        (MM0MathLexerLoopV1 ?table ?rest ?next ?tokens)))

    (rule mm0-secondary-lex-v1
      (head (MM0SecondaryLexV1 ?table ?scalars ?tokens))
      (body
        (MM0MathClassTableValidV1 ?table)
        (MM0MathClassFindV1
          (cp 32) ?table MM0MathWhitespaceV1)
        (MM0MathClassFindV1
          (cp 10) ?table MM0MathWhitespaceV1)
        (MM0MathLexerLoopV1
          ?table ?scalars
          (MM0MathLexerStateV1
            MM0MathScalarsNilV1 MM0MathTokensNilV1)
          ?tokens)))))
