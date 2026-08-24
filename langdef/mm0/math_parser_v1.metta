; MM0 adapter for the generic dynamic Pratt GSLT.
;
; The outer .mm0 parser supplies math strings as scalar data.  The current
; notation environment drives secondary lexing and precedence parsing at
; runtime; no source-file declaration is compiled into this presentation.

(gslt-presentation-v1 MM0MathParserV1
  (signature
    (operator cp 1)
    (operator cons 2)
    (operator different 2)
    (operator NatSuccV1 1)
    (operator MM0MathScalarsConsV1 2)
    (operator MM0MathTokenV1 1)
    (operator MM0MathTokensConsV1 2)
    (operator MM0SecondaryLexV1 3)
    (operator GSLTPrattTokensConsV1 2)

    (operator MM0SourceStateV1 2)
    (operator MM0TextualEnvironmentV1 2)
    (operator MM0MathEnvironmentV1 2)
    (operator MM0NotationEnvironmentV1 4)
    (operator MM0TermEntryV1 4)
    (operator MM0TermSignatureV1 2)
    (operator MM0DeclarationEntriesConsV1 2)
    (operator MM0DeclarationLookupV1 3)
    (operator MM0DeclarationFoundV1 2)
    (operator MM0PrefixNotationEntryV1 5)
    (operator MM0PrefixEntriesConsV1 2)
    (operator MM0InfixNotationEntryV1 5)
    (operator MM0InfixEntriesConsV1 2)
    (operator MM0InfixLookupV1 3)
    (operator MM0FinitePrecedenceV1 1)

    (operator MM0MathScopeConsV1 2)
    (operator MM0MathScalarsMatchNameV1 2)
    (operator MM0MathScalarsDifferentNameV1 2)
    (operator MM0MathScopeTokenLookupV1 3)
    (operator MM0MathScopeTokenFoundV1 1)
    (operator MM0MathBinderCountV1 2)
    (operator MM0MathTermTokenArityLookupV1 4)
    (operator MM0MathIdentifierAfterScopeV1 5)
    (operator MM0MathTokensToPrattV1 2)
    (operator MM0DecimalDigitValueV1 2)
    (operator MM0DecimalLengthV1 2)
    (operator MM0NatCompareV1 3)
    (operator MM0DecimalLexCompareV1 3)
    (operator MM0DecimalLexAfterDigitV1 4)
    (operator MM0DecimalCompareV1 3)
    (operator MM0DecimalCompareAfterLengthV1 4)
    (operator MM0RuntimeMathParseV1 4)

    ; Generic Pratt interface and result constructors.
    (operator GSLTPrattParseAllV1 4)
    (operator GSLTPrattMinimumV1 2)
    (operator GSLTPrattMaximumV1 2)
    (operator GSLTPrattApplicationPrecedenceV1 2)
    (operator GSLTPrattStrictMinimumV1 1)
    (operator GSLTPrattAtLeastV1 2)
    (operator GSLTPrattNotAtLeastV1 2)
    (operator GSLTPrattIdentifierV1 5)
    (operator GSLTPrattIdentifierApplicationV1 2)
    (operator GSLTPrattPrefixV1 5)
    (operator GSLTPrattInfixV1 5)
    (operator GSLTPrattNotInfixV1 2)
    (operator GSLTPrattGroupV1 3))
  (equations)
  (rewrites
    ; Structural codec from the authored secondary lexer to the generic
    ; parser's inert token carrier.
    (rule mm0-math-tokens-to-pratt-nil-v1
      (head
        (MM0MathTokensToPrattV1
          MM0MathTokensNilV1 GSLTPrattTokensNilV1))
      (body))
    (rule mm0-math-tokens-to-pratt-cons-v1
      (head
        (MM0MathTokensToPrattV1
          (MM0MathTokensConsV1 ?token ?rest)
          (GSLTPrattTokensConsV1 ?token ?after)))
      (body
        (MM0MathTokensToPrattV1 ?rest ?after)))

    ; Finite precedence remains a compact canonical decimal list.  Length
    ; comparison followed by lexicographic digit comparison gives exact u32
    ; ordering without expanding values into unary target terms.
    (rule mm0-decimal-digit-zero-v1
      (head (MM0DecimalDigitValueV1 (cp 48) NatZeroV1)) (body))
    (rule mm0-decimal-digit-one-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 49) (NatSuccV1 NatZeroV1)))
      (body))
    (rule mm0-decimal-digit-two-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 50) (NatSuccV1 (NatSuccV1 NatZeroV1))))
      (body))
    (rule mm0-decimal-digit-three-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 51)
          (NatSuccV1 (NatSuccV1 (NatSuccV1 NatZeroV1)))))
      (body))
    (rule mm0-decimal-digit-four-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 52)
          (NatSuccV1 (NatSuccV1 (NatSuccV1
            (NatSuccV1 NatZeroV1))))))
      (body))
    (rule mm0-decimal-digit-five-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 53)
          (NatSuccV1 (NatSuccV1 (NatSuccV1
            (NatSuccV1 (NatSuccV1 NatZeroV1)))))))
      (body))
    (rule mm0-decimal-digit-six-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 54)
          (NatSuccV1 (NatSuccV1 (NatSuccV1
            (NatSuccV1 (NatSuccV1 (NatSuccV1 NatZeroV1))))))))
      (body))
    (rule mm0-decimal-digit-seven-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 55)
          (NatSuccV1 (NatSuccV1 (NatSuccV1 (NatSuccV1
            (NatSuccV1 (NatSuccV1 (NatSuccV1 NatZeroV1)))))))))
      (body))
    (rule mm0-decimal-digit-eight-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 56)
          (NatSuccV1 (NatSuccV1 (NatSuccV1 (NatSuccV1
            (NatSuccV1 (NatSuccV1 (NatSuccV1
              (NatSuccV1 NatZeroV1))))))))))
      (body))
    (rule mm0-decimal-digit-nine-v1
      (head
        (MM0DecimalDigitValueV1
          (cp 57)
          (NatSuccV1 (NatSuccV1 (NatSuccV1 (NatSuccV1
            (NatSuccV1 (NatSuccV1 (NatSuccV1
              (NatSuccV1 (NatSuccV1 NatZeroV1)))))))))))
      (body))

    (rule mm0-decimal-length-nil-v1
      (head (MM0DecimalLengthV1 nil NatZeroV1))
      (body))
    (rule mm0-decimal-length-cons-v1
      (head
        (MM0DecimalLengthV1
          (cons ?digit ?rest) (NatSuccV1 ?length)))
      (body
        (MM0DecimalLengthV1 ?rest ?length)))

    (rule mm0-nat-compare-zero-zero-v1
      (head (MM0NatCompareV1 NatZeroV1 NatZeroV1 MM0EqualV1))
      (body))
    (rule mm0-nat-compare-zero-succ-v1
      (head
        (MM0NatCompareV1
          NatZeroV1 (NatSuccV1 ?right) MM0LessV1))
      (body))
    (rule mm0-nat-compare-succ-zero-v1
      (head
        (MM0NatCompareV1
          (NatSuccV1 ?left) NatZeroV1 MM0GreaterV1))
      (body))
    (rule mm0-nat-compare-succ-succ-v1
      (head
        (MM0NatCompareV1
          (NatSuccV1 ?left) (NatSuccV1 ?right) ?order))
      (body
        (MM0NatCompareV1 ?left ?right ?order)))

    (rule mm0-decimal-lex-equal-v1
      (head (MM0DecimalLexCompareV1 nil nil MM0EqualV1))
      (body))
    (rule mm0-decimal-lex-cons-v1
      (head
        (MM0DecimalLexCompareV1
          (cons ?leftDigit ?leftRest)
          (cons ?rightDigit ?rightRest) ?order))
      (body
        (MM0DecimalDigitValueV1 ?leftDigit ?leftValue)
        (MM0DecimalDigitValueV1 ?rightDigit ?rightValue)
        (MM0NatCompareV1 ?leftValue ?rightValue ?digitOrder)
        (MM0DecimalLexAfterDigitV1
          ?digitOrder ?leftRest ?rightRest ?order)))
    (rule mm0-decimal-lex-after-less-v1
      (head
        (MM0DecimalLexAfterDigitV1
          MM0LessV1 ?left ?right MM0LessV1))
      (body))
    (rule mm0-decimal-lex-after-greater-v1
      (head
        (MM0DecimalLexAfterDigitV1
          MM0GreaterV1 ?left ?right MM0GreaterV1))
      (body))
    (rule mm0-decimal-lex-after-equal-v1
      (head
        (MM0DecimalLexAfterDigitV1
          MM0EqualV1 ?left ?right ?order))
      (body
        (MM0DecimalLexCompareV1 ?left ?right ?order)))

    (rule mm0-decimal-compare-v1
      (head (MM0DecimalCompareV1 ?left ?right ?order))
      (body
        (MM0DecimalLengthV1 ?left ?leftLength)
        (MM0DecimalLengthV1 ?right ?rightLength)
        (MM0NatCompareV1 ?leftLength ?rightLength ?lengthOrder)
        (MM0DecimalCompareAfterLengthV1
          ?lengthOrder ?left ?right ?order)))
    (rule mm0-decimal-compare-length-less-v1
      (head
        (MM0DecimalCompareAfterLengthV1
          MM0LessV1 ?left ?right MM0LessV1))
      (body))
    (rule mm0-decimal-compare-length-greater-v1
      (head
        (MM0DecimalCompareAfterLengthV1
          MM0GreaterV1 ?left ?right MM0GreaterV1))
      (body))
    (rule mm0-decimal-compare-length-equal-v1
      (head
        (MM0DecimalCompareAfterLengthV1
          MM0EqualV1 ?left ?right ?order))
      (body
        (MM0DecimalLexCompareV1 ?left ?right ?order)))

    ; MM0 precedence is a finite decimal or the greatest element `max`.
    ; Application has its specified precedence 1024, represented in four
    ; digit constructors rather than 1024 unary constructors.
    (rule mm0-pratt-minimum-v1
      (head
        (GSLTPrattMinimumV1
          ?environment
          (MM0FinitePrecedenceV1 (cons (cp 48) nil))))
      (body))
    (rule mm0-pratt-maximum-v1
      (head
        (GSLTPrattMaximumV1 ?environment MM0MaximumPrecedenceV1))
      (body))
    (rule mm0-pratt-application-precedence-v1
      (head
        (GSLTPrattApplicationPrecedenceV1
          ?environment
          (MM0FinitePrecedenceV1
            (cons (cp 49) (cons (cp 48)
              (cons (cp 50) (cons (cp 52) nil)))))))
      (body))

    (rule mm0-pratt-at-least-maximum-maximum-v1
      (head
        (GSLTPrattAtLeastV1
          MM0MaximumPrecedenceV1 MM0MaximumPrecedenceV1))
      (body))
    (rule mm0-pratt-at-least-maximum-finite-v1
      (head
        (GSLTPrattAtLeastV1
          MM0MaximumPrecedenceV1
          (MM0FinitePrecedenceV1 ?minimum)))
      (body))
    (rule mm0-pratt-at-least-maximum-strict-finite-v1
      (head
        (GSLTPrattAtLeastV1
          MM0MaximumPrecedenceV1
          (GSLTPrattStrictMinimumV1
            (MM0FinitePrecedenceV1 ?minimum))))
      (body))
    (rule mm0-pratt-at-least-finite-equal-v1
      (head
        (GSLTPrattAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (MM0FinitePrecedenceV1 ?minimum)))
      (body
        (MM0DecimalCompareV1
          ?precedence ?minimum MM0EqualV1)))
    (rule mm0-pratt-at-least-finite-greater-v1
      (head
        (GSLTPrattAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (MM0FinitePrecedenceV1 ?minimum)))
      (body
        (MM0DecimalCompareV1
          ?precedence ?minimum MM0GreaterV1)))
    (rule mm0-pratt-at-least-finite-strict-v1
      (head
        (GSLTPrattAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (GSLTPrattStrictMinimumV1
            (MM0FinitePrecedenceV1 ?minimum))))
      (body
        (MM0DecimalCompareV1
          ?precedence ?minimum MM0GreaterV1)))

    (rule mm0-pratt-not-at-least-finite-maximum-v1
      (head
        (GSLTPrattNotAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          MM0MaximumPrecedenceV1))
      (body))
    (rule mm0-pratt-not-at-least-finite-less-v1
      (head
        (GSLTPrattNotAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (MM0FinitePrecedenceV1 ?minimum)))
      (body
        (MM0DecimalCompareV1
          ?precedence ?minimum MM0LessV1)))
    (rule mm0-pratt-not-at-least-finite-strict-less-v1
      (head
        (GSLTPrattNotAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (GSLTPrattStrictMinimumV1
            (MM0FinitePrecedenceV1 ?minimum))))
      (body
        (MM0DecimalCompareV1
          ?precedence ?minimum MM0LessV1)))
    (rule mm0-pratt-not-at-least-finite-strict-equal-v1
      (head
        (GSLTPrattNotAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (GSLTPrattStrictMinimumV1
            (MM0FinitePrecedenceV1 ?minimum))))
      (body
        (MM0DecimalCompareV1
          ?precedence ?minimum MM0EqualV1)))
    (rule mm0-pratt-not-at-least-finite-strict-maximum-v1
      (head
        (GSLTPrattNotAtLeastV1
          (MM0FinitePrecedenceV1 ?precedence)
          (GSLTPrattStrictMinimumV1 MM0MaximumPrecedenceV1)))
      (body))
    (rule mm0-pratt-not-at-least-maximum-strict-maximum-v1
      (head
        (GSLTPrattNotAtLeastV1
          MM0MaximumPrecedenceV1
          (GSLTPrattStrictMinimumV1 MM0MaximumPrecedenceV1)))
      (body))

    ; Prefix and infix tables are ordered runtime values.  These adapters
    ; preserve the first declaration and expose its term constructor.
    (rule mm0-pratt-prefix-head-v1
      (head
        (GSLTPrattPrefixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants
              (MM0PrefixEntriesConsV1
                (MM0PrefixNotationEntryV1
                  ?token ?term ?arity ?precedence ?position)
                ?rest)
              ?infixes))
          (MM0MathTokenV1 ?token) ?term ?arity ?precedence))
      (body))
    (rule mm0-pratt-prefix-tail-v1
      (head
        (GSLTPrattPrefixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants
              (MM0PrefixEntriesConsV1
                (MM0PrefixNotationEntryV1
                  ?other ?headTerm ?headArity ?headPrecedence ?position)
                ?rest)
              ?infixes))
          (MM0MathTokenV1 ?token) ?term ?arity ?precedence))
      (body
        (different ?token ?other)
        (GSLTPrattPrefixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?rest ?infixes))
          (MM0MathTokenV1 ?token) ?term ?arity ?precedence)))

    (rule mm0-pratt-infix-head-left-v1
      (head
        (GSLTPrattInfixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes
              (MM0InfixEntriesConsV1
                (MM0InfixNotationEntryV1
                  ?token ?term MM0LeftAssociativeV1 ?precedence ?position)
                ?rest)))
          (MM0MathTokenV1 ?token) ?term
          GSLTPrattLeftAssociativeV1 ?precedence))
      (body))
    (rule mm0-pratt-infix-head-right-v1
      (head
        (GSLTPrattInfixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes
              (MM0InfixEntriesConsV1
                (MM0InfixNotationEntryV1
                  ?token ?term MM0RightAssociativeV1 ?precedence ?position)
                ?rest)))
          (MM0MathTokenV1 ?token) ?term
          GSLTPrattRightAssociativeV1 ?precedence))
      (body))
    (rule mm0-pratt-infix-tail-v1
      (head
        (GSLTPrattInfixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes
              (MM0InfixEntriesConsV1
                (MM0InfixNotationEntryV1
                  ?other ?headTerm ?headAssociation ?headPrecedence ?position)
                ?rest)))
          (MM0MathTokenV1 ?token)
          ?term ?association ?precedence))
      (body
        (different ?token ?other)
        (GSLTPrattInfixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?rest))
          (MM0MathTokenV1 ?token)
          ?term ?association ?precedence)))
    (rule mm0-pratt-not-infix-v1
      (head
        (GSLTPrattNotInfixV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes))
          (MM0MathTokenV1 ?token)))
      (body
        (MM0InfixLookupV1 ?token ?infixes MM0InfixMissingV1)))

    ; Secondary tokens and declaration names use different list carriers.
    ; Compare them structurally without constructing a target-specific list.
    (rule mm0-math-scalars-match-name-nil-v1
      (head (MM0MathScalarsMatchNameV1 MM0MathScalarsNilV1 nil))
      (body))
    (rule mm0-math-scalars-match-name-cons-v1
      (head
        (MM0MathScalarsMatchNameV1
          (MM0MathScalarsConsV1 ?scalar ?scalars)
          (cons ?scalar ?name)))
      (body (MM0MathScalarsMatchNameV1 ?scalars ?name)))

    ; Inequality has one derivation: the first unequal codepoint.
    (rule mm0-math-scalars-different-name-left-nil-v1
      (head
        (MM0MathScalarsDifferentNameV1
          MM0MathScalarsNilV1 (cons ?right ?rightRest)))
      (body))
    (rule mm0-math-scalars-different-name-right-nil-v1
      (head
        (MM0MathScalarsDifferentNameV1
          (MM0MathScalarsConsV1 ?left ?leftRest) nil))
      (body))
    (rule mm0-math-scalars-different-name-head-v1
      (head
        (MM0MathScalarsDifferentNameV1
          (MM0MathScalarsConsV1 ?left ?leftRest)
          (cons ?right ?rightRest)))
      (body (different ?left ?right)))
    (rule mm0-math-scalars-different-name-tail-v1
      (head
        (MM0MathScalarsDifferentNameV1
          (MM0MathScalarsConsV1 ?same ?leftRest)
          (cons ?same ?rightRest)))
      (body
        (MM0MathScalarsDifferentNameV1 ?leftRest ?rightRest)))

    (rule mm0-math-scope-token-lookup-missing-v1
      (head
        (MM0MathScopeTokenLookupV1
          ?scalars MM0MathScopeNilV1 MM0MathScopeMissingV1))
      (body))
    (rule mm0-math-scope-token-lookup-found-v1
      (head
        (MM0MathScopeTokenLookupV1
          ?scalars (MM0MathScopeConsV1 ?name ?rest)
          (MM0MathScopeTokenFoundV1 ?name)))
      (body (MM0MathScalarsMatchNameV1 ?scalars ?name)))
    (rule mm0-math-scope-token-lookup-tail-v1
      (head
        (MM0MathScopeTokenLookupV1
          ?scalars (MM0MathScopeConsV1 ?other ?rest) ?result))
      (body
        (MM0MathScalarsDifferentNameV1 ?scalars ?other)
        (MM0MathScopeTokenLookupV1 ?scalars ?rest ?result)))

    (rule mm0-math-binder-count-nil-v1
      (head (MM0MathBinderCountV1 nil NatZeroV1))
      (body))
    (rule mm0-math-binder-count-cons-v1
      (head
        (MM0MathBinderCountV1
          (cons ?binder ?rest) (NatSuccV1 ?count)))
      (body (MM0MathBinderCountV1 ?rest ?count)))

    (rule mm0-math-term-token-arity-found-v1
      (head
        (MM0MathTermTokenArityLookupV1
          ?scalars
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1 ?name
              (MM0TermSignatureV1 ?binders ?returnType)
              ?firstPosition ?source)
            ?rest)
          ?name ?arity))
      (body
        (MM0MathScalarsMatchNameV1 ?scalars ?name)
        (MM0MathBinderCountV1 ?binders ?arity)))
    (rule mm0-math-term-token-arity-tail-v1
      (head
        (MM0MathTermTokenArityLookupV1
          ?scalars
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1 ?other ?signature ?firstPosition ?source)
            ?rest)
          ?name ?arity))
      (body
        (MM0MathScalarsDifferentNameV1 ?scalars ?other)
        (MM0MathTermTokenArityLookupV1
          ?scalars ?rest ?name ?arity)))

    ; Scoped variables have arity zero.  Declared terms consume exactly the
    ; binder count stored in their live normalized signature.
    (rule mm0-pratt-identifier-v1
      (head
        (GSLTPrattIdentifierV1
          (MM0MathEnvironmentV1 ?declarations ?notation)
          ?scope (MM0MathTokenV1 ?scalars) ?name ?arity))
      (body
        (MM0MathScopeTokenLookupV1 ?scalars ?scope ?scopeResult)
        (MM0MathIdentifierAfterScopeV1
          ?scopeResult ?declarations ?scalars ?name ?arity)))
    (rule mm0-math-identifier-scope-v1
      (head
        (MM0MathIdentifierAfterScopeV1
          (MM0MathScopeTokenFoundV1 ?name)
          ?declarations ?scalars ?name NatZeroV1))
      (body))
    (rule mm0-math-identifier-term-v1
      (head
        (MM0MathIdentifierAfterScopeV1
          MM0MathScopeMissingV1 ?declarations ?scalars ?name ?arity))
      (body
        (MM0MathTermTokenArityLookupV1
          ?scalars ?declarations ?name ?arity)))

    ; Parentheses are reserved by MM0 and therefore never enter a notation
    ; table.  The secondary lexer may recognize them by whitespace alone or
    ; via the current delimiter state.
    (rule mm0-pratt-parentheses-v1
      (head
        (GSLTPrattGroupV1
          ?environment
          (MM0MathTokenV1
            (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1))
          (MM0MathTokenV1
            (MM0MathScalarsConsV1 (cp 41) MM0MathScalarsNilV1))))
      (body))

    ; Public runtime composition: live delimiter state -> tokens -> generic
    ; Pratt relation.  Both environment and math string are ordinary inputs.
    (rule mm0-runtime-math-parse-v1
      (head
        (MM0RuntimeMathParseV1
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations)
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes))
          ?scope ?scalars ?tree))
      (body
        (MM0SecondaryLexV1 ?delimiters ?scalars ?mathTokens)
        (MM0MathTokensToPrattV1 ?mathTokens ?prattTokens)
        (GSLTPrattParseAllV1
          (MM0MathEnvironmentV1 ?declarations
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes))
          ?scope ?prattTokens ?tree)))))
