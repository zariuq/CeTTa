; Ordered live syntax state for textual MM0.
;
; A source file is runtime data.  Sort, term, delimiter, and simple-notation
; declarations update one explicit prefix state in source order; later
; declarations cannot affect the interpretation of earlier math strings.

(gslt-presentation-v1 MM0NotationEnvironmentV1
  (signature
    (operator cp 1)
    (operator node 2)
    (operator pair 2)
    (operator cons 2)
    (operator different 2)
    (operator NatSuccV1 1)
    (operator MM0IdentifierScalarsV1 2)
    (operator MM0MathClassEntryV1 2)
    (operator MM0MathClassTableConsV1 2)
    (operator MM0MathScalarsConsV1 2)
    (operator MM0MathScalarsFromListV1 2)
    (operator MM0DelimiterCharactersFoldV1 4)
    (operator MM0SecondaryLexV1 3)

    (operator MM0PrecedenceFromAstV1 2)
    (operator MM0FinitePrecedenceV1 1)
    (operator MM0ListLengthV1 2)
    (operator MM0MathTokenListLengthV1 2)

    (operator MM0TermEntryV1 4)
    (operator MM0DeclarationEntriesConsV1 2)
    (operator MM0TermSignatureV1 2)
    (operator MM0TermSignatureLookupV1 3)
    (operator MM0TermSignatureFoundV1 2)

    (operator MM0NotationEnvironmentV1 4)
    (operator MM0ConstantEntryV1 3)
    (operator MM0ConstantEntriesConsV1 2)
    (operator MM0ConstantLookupV1 3)
    (operator MM0ConstantFoundV1 2)
    (operator MM0ConstantEntriesAppendV1 3)
    (operator MM0PrefixNotationEntryV1 5)
    (operator MM0PrefixEntriesConsV1 2)
    (operator MM0PrefixLookupV1 3)
    (operator MM0PrefixFoundV1 1)
    (operator MM0PrefixEntriesAppendV1 3)
    (operator MM0InfixNotationEntryV1 5)
    (operator MM0InfixEntriesConsV1 2)
    (operator MM0InfixLookupV1 3)
    (operator MM0InfixFoundV1 1)
    (operator MM0InfixEntriesAppendV1 3)

    (operator MM0SimpleNotationStatementV1 5)
    (operator MM0InfixNotationKindV1 1)
    (operator MM0NotationConstantV1 3)
    (operator MM0NotationConstantClassV1 3)
    (operator MM0NotationTokenAllowedV1 2)
    (operator MM0CheckAcceptedV1 1)
    (operator MM0CheckRefusedV1 1)
    (operator MM0PrepareSimpleNotationV1 8)
    (operator MM0PreparePrefixAfterConstantV1 7)
    (operator MM0PrepareInfixAfterArityV1 8)
    (operator MM0PrepareInfixAfterConstantV1 6)
    (operator MM0PreparedSimpleNotationV1 3)
    (operator MM0InstallSimpleNotationV1 5)
    (operator MM0InstallPrefixFromLookupsV1 6)
    (operator MM0InstallInfixFromLookupsV1 6)

    (operator MM0NotationStepV1 5)
    (operator MM0NotationStepAcceptedV1 2)
    (operator MM0NotationStepRefusedV1 3)
    (operator MM0NotationStepAfterTermLookupV1 8)
    (operator MM0NotationStepAfterPrepareV1 4)
    (operator MM0DeclaredDelimitersV1 2)
    (operator MM0DeclaredSimpleNotationV1 4)
    (operator MM0RefusedUnknownNotationTermV1 2)
    (operator MM0RefusedInfixArityV1 3)
    (operator MM0RefusedInfixMaximumPrecedenceV1 2)
    (operator MM0RefusedNotationConstantTokenCountV1 2)
    (operator MM0RefusedNotationParenthesisV1 2)
    (operator MM0RefusedNotationPrecedenceConflictV1 4)
    (operator MM0RefusedNotationRedeclarationV1 3)

    (operator MM0TextualEnvironmentV1 2)
    (operator MM0TextualStepV1 4)
    (operator MM0TextualStepAcceptedV1 2)
    (operator MM0TextualStepRefusedV1 3)
    (operator MM0SourceStateV1 2)
    (operator MM0StatementClassV1 2)
    (operator MM0SourceStepV1 4)
    (operator MM0SourceStepByClassV1 5)
    (operator MM0SourceStepAcceptedV1 2)
    (operator MM0SourceStepRefusedV1 3)
    (operator MM0SourceFoldV1 6)
    (operator MM0SourceFoldAfterStepV1 6)
    (operator MM0SourceEnvironmentFromAstV1 4)
    (operator MM0SourceHistoryConsV1 2))
  (equations)
  (rewrites
    ; The outer grammar admits only `0` or a nonzero digit followed by digits,
    ; so its number node already carries a canonical compact decimal.  Keep
    ; that certified representation rather than expanding it to unary.
    (rule mm0-notation-precedence-number-v1
      (head
        (MM0PrecedenceFromAstV1
          (node mm0-number (pair ?first ?rest))
          (MM0FinitePrecedenceV1 (cons ?first ?rest))))
      (body))
    (rule mm0-notation-precedence-max-v1
      (head
        (MM0PrecedenceFromAstV1
          (cons (cp 109) (cons (cp 97) (cons (cp 120) nil)))
          MM0MaximumPrecedenceV1))
      (body))

    (rule mm0-notation-list-length-nil-v1
      (head (MM0ListLengthV1 nil NatZeroV1))
      (body))
    (rule mm0-notation-list-length-cons-v1
      (head
        (MM0ListLengthV1 (cons ?head ?rest) (NatSuccV1 ?length)))
      (body (MM0ListLengthV1 ?rest ?length)))

    (rule mm0-notation-token-list-length-nil-v1
      (head (MM0MathTokenListLengthV1 MM0MathTokensNilV1 NatZeroV1))
      (body))
    (rule mm0-notation-token-list-length-cons-v1
      (head
        (MM0MathTokenListLengthV1
          (MM0MathTokensConsV1 ?head ?rest) (NatSuccV1 ?length)))
      (body (MM0MathTokenListLengthV1 ?rest ?length)))

    ; The outer parser and secondary lexer deliberately have distinct list
    ; carriers.  Their total structural codec is part of the authored
    ; composition, not a conversion hidden in a target runtime.
    (rule mm0-notation-scalars-from-list-nil-v1
      (head (MM0MathScalarsFromListV1 nil MM0MathScalarsNilV1))
      (body))
    (rule mm0-notation-scalars-from-list-cons-v1
      (head
        (MM0MathScalarsFromListV1
          (cons ?scalar ?rest)
          (MM0MathScalarsConsV1 ?scalar ?after)))
      (body (MM0MathScalarsFromListV1 ?rest ?after)))

    ; The term environment retains the normalized signature needed to check
    ; notation arity.  Lookup is total and preserves the first position.
    (rule mm0-notation-term-signature-lookup-missing-v1
      (head
        (MM0TermSignatureLookupV1
          ?name MM0DeclarationEntriesNilV1 MM0TermSignatureMissingV1))
      (body))
    (rule mm0-notation-term-signature-lookup-found-v1
      (head
        (MM0TermSignatureLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1 ?name ?signature ?position ?source) ?rest)
          (MM0TermSignatureFoundV1 ?signature ?position)))
      (body))
    (rule mm0-notation-term-signature-lookup-tail-v1
      (head
        (MM0TermSignatureLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1 ?other ?signature ?position ?source) ?rest)
          ?result))
      (body
        (different ?name ?other)
        (MM0TermSignatureLookupV1 ?name ?rest ?result)))

    ; Finite ordered map helpers.  The didactic representation keeps source
    ; order; later target optimization may index the same values.
    (rule mm0-notation-constant-lookup-missing-v1
      (head
        (MM0ConstantLookupV1
          ?token MM0ConstantEntriesNilV1 MM0ConstantMissingV1))
      (body))
    (rule mm0-notation-constant-lookup-found-v1
      (head
        (MM0ConstantLookupV1
          ?token
          (MM0ConstantEntriesConsV1
            (MM0ConstantEntryV1 ?token ?precedence ?position) ?rest)
          (MM0ConstantFoundV1 ?precedence ?position)))
      (body))
    (rule mm0-notation-constant-lookup-tail-v1
      (head
        (MM0ConstantLookupV1
          ?token
          (MM0ConstantEntriesConsV1
            (MM0ConstantEntryV1 ?other ?precedence ?position) ?rest)
          ?result))
      (body
        (different ?token ?other)
        (MM0ConstantLookupV1 ?token ?rest ?result)))
    (rule mm0-notation-constant-append-nil-v1
      (head
        (MM0ConstantEntriesAppendV1
          MM0ConstantEntriesNilV1 ?entry
          (MM0ConstantEntriesConsV1 ?entry MM0ConstantEntriesNilV1)))
      (body))
    (rule mm0-notation-constant-append-cons-v1
      (head
        (MM0ConstantEntriesAppendV1
          (MM0ConstantEntriesConsV1 ?head ?rest) ?entry
          (MM0ConstantEntriesConsV1 ?head ?after)))
      (body (MM0ConstantEntriesAppendV1 ?rest ?entry ?after)))

    (rule mm0-notation-prefix-lookup-missing-v1
      (head
        (MM0PrefixLookupV1
          ?token MM0PrefixEntriesNilV1 MM0PrefixMissingV1))
      (body))
    (rule mm0-notation-prefix-lookup-found-v1
      (head
        (MM0PrefixLookupV1
          ?token
          (MM0PrefixEntriesConsV1
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?precedence ?position) ?rest)
          (MM0PrefixFoundV1 ?position)))
      (body))
    (rule mm0-notation-prefix-lookup-tail-v1
      (head
        (MM0PrefixLookupV1
          ?token
          (MM0PrefixEntriesConsV1
            (MM0PrefixNotationEntryV1
              ?other ?term ?arity ?precedence ?position) ?rest)
          ?result))
      (body
        (different ?token ?other)
        (MM0PrefixLookupV1 ?token ?rest ?result)))
    (rule mm0-notation-prefix-append-nil-v1
      (head
        (MM0PrefixEntriesAppendV1
          MM0PrefixEntriesNilV1 ?entry
          (MM0PrefixEntriesConsV1 ?entry MM0PrefixEntriesNilV1)))
      (body))
    (rule mm0-notation-prefix-append-cons-v1
      (head
        (MM0PrefixEntriesAppendV1
          (MM0PrefixEntriesConsV1 ?head ?rest) ?entry
          (MM0PrefixEntriesConsV1 ?head ?after)))
      (body (MM0PrefixEntriesAppendV1 ?rest ?entry ?after)))

    (rule mm0-notation-infix-lookup-missing-v1
      (head
        (MM0InfixLookupV1
          ?token MM0InfixEntriesNilV1 MM0InfixMissingV1))
      (body))
    (rule mm0-notation-infix-lookup-found-v1
      (head
        (MM0InfixLookupV1
          ?token
          (MM0InfixEntriesConsV1
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?precedence ?position) ?rest)
          (MM0InfixFoundV1 ?position)))
      (body))
    (rule mm0-notation-infix-lookup-tail-v1
      (head
        (MM0InfixLookupV1
          ?token
          (MM0InfixEntriesConsV1
            (MM0InfixNotationEntryV1
              ?other ?term ?associativity ?precedence ?position) ?rest)
          ?result))
      (body
        (different ?token ?other)
        (MM0InfixLookupV1 ?token ?rest ?result)))
    (rule mm0-notation-infix-append-nil-v1
      (head
        (MM0InfixEntriesAppendV1
          MM0InfixEntriesNilV1 ?entry
          (MM0InfixEntriesConsV1 ?entry MM0InfixEntriesNilV1)))
      (body))
    (rule mm0-notation-infix-append-cons-v1
      (head
        (MM0InfixEntriesAppendV1
          (MM0InfixEntriesConsV1 ?head ?rest) ?entry
          (MM0InfixEntriesConsV1 ?head ?after)))
      (body (MM0InfixEntriesAppendV1 ?rest ?entry ?after)))

    ; Outer syntax discriminants become a target-neutral notation kind.
    (rule mm0-notation-simple-statement-prefix-v1
      (head
        (MM0SimpleNotationStatementV1
          (node mm0-simple-notation-statement
            (pair (node mm0-prefix-kind ?keyword)
              (pair ?identifier (pair ?constant ?precedence))))
          MM0PrefixNotationKindV1 ?termName ?constant ?precedence))
      (body (MM0IdentifierScalarsV1 ?identifier ?termName)))
    (rule mm0-notation-simple-statement-infixl-v1
      (head
        (MM0SimpleNotationStatementV1
          (node mm0-simple-notation-statement
            (pair (node mm0-infixl-kind ?keyword)
              (pair ?identifier (pair ?constant ?precedence))))
          (MM0InfixNotationKindV1 MM0LeftAssociativeV1)
          ?termName ?constant ?precedence))
      (body (MM0IdentifierScalarsV1 ?identifier ?termName)))
    (rule mm0-notation-simple-statement-infixr-v1
      (head
        (MM0SimpleNotationStatementV1
          (node mm0-simple-notation-statement
            (pair (node mm0-infixr-kind ?keyword)
              (pair ?identifier (pair ?constant ?precedence))))
          (MM0InfixNotationKindV1 MM0RightAssociativeV1)
          ?termName ?constant ?precedence))
      (body (MM0IdentifierScalarsV1 ?identifier ?termName)))

    ; A notation constant must lex to exactly one non-parenthesis token under
    ; the delimiter state that precedes the declaration.
    (rule mm0-notation-constant-v1
      (head
        (MM0NotationConstantV1
          ?delimiterTable (node mm0-math-string ?scalars) ?result))
      (body
        (MM0MathScalarsFromListV1 ?scalars ?mathScalars)
        (MM0SecondaryLexV1 ?delimiterTable ?mathScalars ?tokens)
        (MM0NotationConstantClassV1 ?tokens ?scalars ?result)))
    (rule mm0-notation-constant-class-single-v1
      (head
        (MM0NotationConstantClassV1
          (MM0MathTokensConsV1
            (MM0MathTokenV1 ?token) MM0MathTokensNilV1)
          ?source ?result))
      (body (MM0NotationTokenAllowedV1 ?token ?result)))
    (rule mm0-notation-constant-class-empty-v1
      (head
        (MM0NotationConstantClassV1
          MM0MathTokensNilV1 ?source
          (MM0CheckRefusedV1
            (MM0RefusedNotationConstantTokenCountV1 ?source NatZeroV1))))
      (body))
    (rule mm0-notation-constant-class-many-v1
      (head
        (MM0NotationConstantClassV1
          (MM0MathTokensConsV1 ?first
            (MM0MathTokensConsV1 ?second ?rest))
          ?source
          (MM0CheckRefusedV1
            (MM0RefusedNotationConstantTokenCountV1
              ?source ?count))))
      (body
        (MM0MathTokenListLengthV1
          (MM0MathTokensConsV1 ?first
            (MM0MathTokensConsV1 ?second ?rest))
          ?count)))
    (rule mm0-notation-token-left-paren-v1
      (head
        (MM0NotationTokenAllowedV1
          (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1)
          (MM0CheckRefusedV1
            (MM0RefusedNotationParenthesisV1 (cp 40) MM0LeftParenthesisV1))))
      (body))
    (rule mm0-notation-token-right-paren-v1
      (head
        (MM0NotationTokenAllowedV1
          (MM0MathScalarsConsV1 (cp 41) MM0MathScalarsNilV1)
          (MM0CheckRefusedV1
            (MM0RefusedNotationParenthesisV1 (cp 41) MM0RightParenthesisV1))))
      (body))
    (rule mm0-notation-token-allowed-v1
      (head
        (MM0NotationTokenAllowedV1
          ?token (MM0CheckAcceptedV1 ?token)))
      (body
        (different ?token
          (MM0MathScalarsConsV1 (cp 40) MM0MathScalarsNilV1))
        (different ?token
          (MM0MathScalarsConsV1 (cp 41) MM0MathScalarsNilV1))))

    ; Prefix arity is the declared term arity.  Infix notation is exactly
    ; binary and cannot use maximum precedence.
    (rule mm0-notation-prepare-prefix-v1
      (head
        (MM0PrepareSimpleNotationV1
          ?position MM0PrefixNotationKindV1 ?termName
          (MM0TermSignatureV1 ?binders ?returnType)
          ?delimiterTable ?constantAst ?precedenceAst ?result))
      (body
        (MM0ListLengthV1 ?binders ?arity)
        (MM0PrecedenceFromAstV1 ?precedenceAst ?precedence)
        (MM0NotationConstantV1 ?delimiterTable ?constantAst ?constantResult)
        (MM0PreparePrefixAfterConstantV1
          ?position ?termName ?arity ?precedence
          ?delimiterTable ?constantResult ?result)))
    (rule mm0-notation-prepare-prefix-constant-accepted-v1
      (head
        (MM0PreparePrefixAfterConstantV1
          ?position ?termName ?arity ?precedence ?delimiterTable
          (MM0CheckAcceptedV1 ?token)
          (MM0CheckAcceptedV1
            (MM0PreparedSimpleNotationV1
              ?token ?precedence
              (MM0PrefixNotationEntryV1
                ?token ?termName ?arity ?precedence ?position)))))
      (body))
    (rule mm0-notation-prepare-prefix-constant-refused-v1
      (head
        (MM0PreparePrefixAfterConstantV1
          ?position ?termName ?arity ?precedence ?delimiterTable
          (MM0CheckRefusedV1 ?reason) (MM0CheckRefusedV1 ?reason)))
      (body))

    (rule mm0-notation-prepare-infix-v1
      (head
        (MM0PrepareSimpleNotationV1
          ?position (MM0InfixNotationKindV1 ?associativity) ?termName
          (MM0TermSignatureV1 ?binders ?returnType)
          ?delimiterTable ?constantAst ?precedenceAst ?result))
      (body
        (MM0ListLengthV1 ?binders ?arity)
        (MM0PrepareInfixAfterArityV1
          ?position ?termName ?associativity ?arity
          ?constantAst ?precedenceAst ?delimiterTable ?result)))
    (rule mm0-notation-prepare-infix-arity-refused-v1
      (head
        (MM0PrepareInfixAfterArityV1
          ?position ?termName ?associativity ?arity
          ?constantAst ?precedenceAst ?delimiterTable
          (MM0CheckRefusedV1
            (MM0RefusedInfixArityV1 ?position ?termName ?arity))))
      (body
        (different ?arity
          (NatSuccV1 (NatSuccV1 NatZeroV1)))))
    (rule mm0-notation-prepare-infix-max-refused-v1
      (head
        (MM0PrepareInfixAfterArityV1
          ?position ?termName ?associativity
          (NatSuccV1 (NatSuccV1 NatZeroV1))
          ?constantAst ?precedenceAst ?delimiterTable
          (MM0CheckRefusedV1
            (MM0RefusedInfixMaximumPrecedenceV1 ?position ?termName))))
      (body
        (MM0PrecedenceFromAstV1 ?precedenceAst MM0MaximumPrecedenceV1)))
    (rule mm0-notation-prepare-infix-finite-v1
      (head
        (MM0PrepareInfixAfterArityV1
          ?position ?termName ?associativity
          (NatSuccV1 (NatSuccV1 NatZeroV1))
          ?constantAst ?precedenceAst ?delimiterTable ?result))
      (body
        (MM0PrecedenceFromAstV1
          ?precedenceAst (MM0FinitePrecedenceV1 ?precedence))
        (MM0NotationConstantV1 ?delimiterTable ?constantAst ?constantResult)
        (MM0PrepareInfixAfterConstantV1
          ?position ?termName ?associativity ?precedence
          ?constantResult ?result)))
    (rule mm0-notation-prepare-infix-constant-accepted-v1
      (head
        (MM0PrepareInfixAfterConstantV1
          ?position ?termName ?associativity ?precedence
          (MM0CheckAcceptedV1 ?token)
          (MM0CheckAcceptedV1
            (MM0PreparedSimpleNotationV1
              ?token (MM0FinitePrecedenceV1 ?precedence)
              (MM0InfixNotationEntryV1
                ?token ?termName ?associativity
                (MM0FinitePrecedenceV1 ?precedence) ?position)))))
      (body))
    (rule mm0-notation-prepare-infix-constant-refused-v1
      (head
        (MM0PrepareInfixAfterConstantV1
          ?position ?termName ?associativity ?precedence
          (MM0CheckRefusedV1 ?reason) (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Install a prepared notation transactionally.  The constant may be
    ; shared across prefix/infix roles only at the same precedence.
    (rule mm0-notation-install-prefix-v1
      (head
        (MM0InstallSimpleNotationV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?result ?source))
      (body
        (MM0ConstantLookupV1
          ?token ?constants ?constantLookup)
        (MM0PrefixLookupV1 ?token ?prefixes ?prefixLookup)
        (MM0InstallPrefixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?constantLookup ?prefixLookup ?result)))
    (rule mm0-notation-install-infix-v1
      (head
        (MM0InstallSimpleNotationV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?result ?source))
      (body
        (MM0ConstantLookupV1
          ?token ?constants ?constantLookup)
        (MM0InfixLookupV1 ?token ?infixes ?infixLookup)
        (MM0InstallInfixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?constantLookup ?infixLookup ?result)))

    (rule mm0-notation-install-prefix-new-constant-v1
      (head
        (MM0InstallPrefixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          MM0ConstantMissingV1 MM0PrefixMissingV1
          (MM0NotationStepAcceptedV1
            (MM0NotationEnvironmentV1
              ?delimiters ?afterConstants ?afterPrefixes ?infixes)
            (MM0DeclaredSimpleNotationV1
              ?position ?term MM0PrefixNotationKindV1 ?token))))
      (body
        (MM0ConstantEntriesAppendV1 ?constants
          (MM0ConstantEntryV1 ?token ?precedence ?position) ?afterConstants)
        (MM0PrefixEntriesAppendV1 ?prefixes
          (MM0PrefixNotationEntryV1
            ?token ?term ?arity ?precedence ?position) ?afterPrefixes)))
    (rule mm0-notation-install-prefix-existing-constant-v1
      (head
        (MM0InstallPrefixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0ConstantFoundV1 ?precedence ?constantPosition)
          MM0PrefixMissingV1
          (MM0NotationStepAcceptedV1
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?afterPrefixes ?infixes)
            (MM0DeclaredSimpleNotationV1
              ?position ?term MM0PrefixNotationKindV1 ?token))))
      (body
        (MM0PrefixEntriesAppendV1 ?prefixes
          (MM0PrefixNotationEntryV1
            ?token ?term ?arity ?precedence ?position) ?afterPrefixes)))
    (rule mm0-notation-install-infix-new-constant-v1
      (head
        (MM0InstallInfixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          MM0ConstantMissingV1 MM0InfixMissingV1
          (MM0NotationStepAcceptedV1
            (MM0NotationEnvironmentV1
              ?delimiters ?afterConstants ?prefixes ?afterInfixes)
            (MM0DeclaredSimpleNotationV1
              ?position ?term
              (MM0InfixNotationKindV1 ?associativity) ?token))))
      (body
        (MM0ConstantEntriesAppendV1 ?constants
          (MM0ConstantEntryV1 ?token ?precedence ?position) ?afterConstants)
        (MM0InfixEntriesAppendV1 ?infixes
          (MM0InfixNotationEntryV1
            ?token ?term ?associativity ?precedence ?position) ?afterInfixes)))
    (rule mm0-notation-install-infix-existing-constant-v1
      (head
        (MM0InstallInfixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0ConstantFoundV1 ?precedence ?constantPosition)
          MM0InfixMissingV1
          (MM0NotationStepAcceptedV1
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?afterInfixes)
            (MM0DeclaredSimpleNotationV1
              ?position ?term
              (MM0InfixNotationKindV1 ?associativity) ?token))))
      (body
        (MM0InfixEntriesAppendV1 ?infixes
          (MM0InfixNotationEntryV1
            ?token ?term ?associativity ?precedence ?position) ?afterInfixes)))
    (rule mm0-notation-install-prefix-precedence-conflict-v1
      (head
        (MM0InstallPrefixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?newPrecedence
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?newPrecedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0ConstantFoundV1 ?oldPrecedence ?constantPosition)
          ?prefixLookup
          (MM0NotationStepRefusedV1
            (MM0RefusedNotationPrecedenceConflictV1
              ?position ?token ?oldPrecedence ?newPrecedence)
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes)
            (MM0RefusedNotationPrecedenceConflictV1
              ?position ?token ?oldPrecedence ?newPrecedence))))
      (body
        (different ?oldPrecedence ?newPrecedence)))
    (rule mm0-notation-install-infix-precedence-conflict-v1
      (head
        (MM0InstallInfixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?newPrecedence
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?newPrecedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0ConstantFoundV1 ?oldPrecedence ?constantPosition)
          ?infixLookup
          (MM0NotationStepRefusedV1
            (MM0RefusedNotationPrecedenceConflictV1
              ?position ?token ?oldPrecedence ?newPrecedence)
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes)
            (MM0RefusedNotationPrecedenceConflictV1
              ?position ?token ?oldPrecedence ?newPrecedence))))
      (body
        (different ?oldPrecedence ?newPrecedence)))
    (rule mm0-notation-install-prefix-redeclared-v1
      (head
        (MM0InstallPrefixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0PrefixNotationEntryV1
              ?token ?term ?arity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0ConstantFoundV1 ?precedence ?constantPosition)
          (MM0PrefixFoundV1 ?firstPosition)
          (MM0NotationStepRefusedV1
            (MM0RefusedNotationRedeclarationV1
              ?position ?token ?firstPosition)
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes)
            (MM0RefusedNotationRedeclarationV1
              ?position ?token ?firstPosition))))
      (body))
    (rule mm0-notation-install-infix-redeclared-v1
      (head
        (MM0InstallInfixFromLookupsV1
          ?position
          (MM0PreparedSimpleNotationV1 ?token ?precedence
            (MM0InfixNotationEntryV1
              ?token ?term ?associativity ?precedence ?position))
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0ConstantFoundV1 ?precedence ?constantPosition)
          (MM0InfixFoundV1 ?firstPosition)
          (MM0NotationStepRefusedV1
            (MM0RefusedNotationRedeclarationV1
              ?position ?token ?firstPosition)
            (MM0NotationEnvironmentV1
              ?delimiters ?constants ?prefixes ?infixes)
            (MM0RefusedNotationRedeclarationV1
              ?position ?token ?firstPosition))))
      (body))

    ; Delimiters and simple notations are state transitions over the current
    ; prefix.  No later statement participates in either transition.
    (rule mm0-notation-step-delimiter-both-v1
      (head
        (MM0NotationStepV1
          ?position
          (node mm0-delimiter-statement
            (pair (node mm0-math-string ?both) none))
          (MM0NotationEnvironmentV1
            ?before ?constants ?prefixes ?infixes)
          ?declarations
          (MM0NotationStepAcceptedV1
            (MM0NotationEnvironmentV1
              ?after ?constants ?prefixes ?infixes)
            (MM0DeclaredDelimitersV1 ?position MM0BothDelimitersV1))))
      (body
        (MM0DelimiterCharactersFoldV1
          ?both MM0MathBothDelimiterV1 ?before ?after)))
    (rule mm0-notation-step-delimiter-left-right-v1
      (head
        (MM0NotationStepV1
          ?position
          (node mm0-delimiter-statement
            (pair (node mm0-math-string ?left)
              (some (node mm0-math-string ?right))))
          (MM0NotationEnvironmentV1
            ?before ?constants ?prefixes ?infixes)
          ?declarations
          (MM0NotationStepAcceptedV1
            (MM0NotationEnvironmentV1
              ?after ?constants ?prefixes ?infixes)
            (MM0DeclaredDelimitersV1 ?position MM0LeftRightDelimitersV1))))
      (body
        (MM0DelimiterCharactersFoldV1
          ?left MM0MathLeftDelimiterV1 ?before ?middle)
        (MM0DelimiterCharactersFoldV1
          ?right MM0MathRightDelimiterV1 ?middle ?after)))
    (rule mm0-notation-step-simple-v1
      (head
        (MM0NotationStepV1
          ?position ?statement
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?declarations ?result))
      (body
        (MM0SimpleNotationStatementV1
          ?statement ?kind ?termName ?constantAst ?precedenceAst)
        (MM0TermSignatureLookupV1 ?termName ?declarations ?lookup)
        (MM0NotationStepAfterTermLookupV1
          ?position ?kind ?termName ?constantAst ?precedenceAst
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?lookup ?result)))
    (rule mm0-notation-step-unknown-term-v1
      (head
        (MM0NotationStepAfterTermLookupV1
          ?position ?kind ?termName ?constantAst ?precedenceAst ?before
          MM0TermSignatureMissingV1
          (MM0NotationStepRefusedV1
            (MM0RefusedUnknownNotationTermV1 ?position ?termName)
            ?before
            (MM0RefusedUnknownNotationTermV1 ?position ?termName))))
      (body))
    (rule mm0-notation-step-found-term-v1
      (head
        (MM0NotationStepAfterTermLookupV1
          ?position ?kind ?termName ?constantAst ?precedenceAst
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          (MM0TermSignatureFoundV1 ?signature ?termPosition) ?result))
      (body
        (MM0PrepareSimpleNotationV1
          ?position ?kind ?termName ?signature
          ?delimiters ?constantAst ?precedenceAst ?prepared)
        (MM0NotationStepAfterPrepareV1
          ?position
          (MM0NotationEnvironmentV1
            ?delimiters ?constants ?prefixes ?infixes)
          ?prepared ?result)))
    (rule mm0-notation-step-prepare-refused-v1
      (head
        (MM0NotationStepAfterPrepareV1
          ?position ?before (MM0CheckRefusedV1 ?reason)
          (MM0NotationStepRefusedV1 ?reason ?before ?reason)))
      (body))
    (rule mm0-notation-step-prepare-accepted-v1
      (head
        (MM0NotationStepAfterPrepareV1
          ?position ?before (MM0CheckAcceptedV1 ?prepared) ?result))
      (body
        (MM0InstallSimpleNotationV1
          ?position ?prepared ?before ?result MM0RuntimeTextualSourceV1)))

    ; Compose the existing ordered sort/term fold with live syntax state.
    ; The source fold dispatches by an authored statement partition.  This
    ; keeps syntax ownership in the source presentation and makes the four
    ; runtime cases constructor-disjoint before either target is selected.
    (rule mm0-source-statement-class-sort-v1
      (head
        (MM0StatementClassV1
          (node mm0-sort-statement ?payload) MM0TextualStatementV1))
      (body))
    (rule mm0-source-statement-class-term-v1
      (head
        (MM0StatementClassV1
          (node mm0-term-statement ?payload) MM0TextualStatementV1))
      (body))
    (rule mm0-source-statement-class-delimiter-v1
      (head
        (MM0StatementClassV1
          (node mm0-delimiter-statement ?payload) MM0NotationStatementV1))
      (body))
    (rule mm0-source-statement-class-simple-notation-v1
      (head
        (MM0StatementClassV1
          (node mm0-simple-notation-statement ?payload)
          MM0NotationStatementV1))
      (body))
    (rule mm0-source-step-dispatch-v1
      (head
        (MM0SourceStepV1 ?position ?statement ?before ?result))
      (body
        (MM0StatementClassV1 ?statement ?class)
        (MM0SourceStepByClassV1
          ?position ?statement ?before ?class ?result)))
    (rule mm0-source-step-textual-accepted-v1
      (head
        (MM0SourceStepByClassV1
          ?position ?statement
          (MM0SourceStateV1 ?textual ?notation)
          MM0TextualStatementV1
          (MM0SourceStepAcceptedV1
            (MM0SourceStateV1 ?afterTextual ?notation) ?historyEntry)))
      (body
        (MM0TextualStepV1
          ?position ?statement ?textual
          (MM0TextualStepAcceptedV1 ?afterTextual ?historyEntry))))
    (rule mm0-source-step-textual-refused-v1
      (head
        (MM0SourceStepByClassV1
          ?position ?statement
          (MM0SourceStateV1 ?textual ?notation)
          MM0TextualStatementV1
          (MM0SourceStepRefusedV1
            ?reason (MM0SourceStateV1 ?textual ?notation) ?historyEntry)))
      (body
        (MM0TextualStepV1
          ?position ?statement ?textual
          (MM0TextualStepRefusedV1
            ?reason ?textual ?historyEntry))))
    (rule mm0-source-step-notation-accepted-v1
      (head
        (MM0SourceStepByClassV1
          ?position ?statement
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          MM0NotationStatementV1
          (MM0SourceStepAcceptedV1
            (MM0SourceStateV1
              (MM0TextualEnvironmentV1 ?sorts ?declarations) ?afterNotation)
            ?historyEntry)))
      (body
        (MM0NotationStepV1
          ?position ?statement ?notation ?declarations
          (MM0NotationStepAcceptedV1 ?afterNotation ?historyEntry))))
    (rule mm0-source-step-notation-refused-v1
      (head
        (MM0SourceStepByClassV1
          ?position ?statement
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          MM0NotationStatementV1
          (MM0SourceStepRefusedV1
            ?reason
            (MM0SourceStateV1
              (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
            ?historyEntry)))
      (body
        (MM0NotationStepV1
          ?position ?statement ?notation ?declarations
          (MM0NotationStepRefusedV1 ?reason ?notation ?historyEntry))))

    (rule mm0-source-fold-nil-v1
      (head
        (MM0SourceFoldV1
          nil ?position ?state MM0AcceptedV1 ?state MM0SourceHistoryNilV1))
      (body))
    (rule mm0-source-fold-cons-v1
      (head
        (MM0SourceFoldV1
          (cons ?statement ?rest) ?position ?before
          ?outcome ?after ?history))
      (body
        (MM0SourceStepV1 ?position ?statement ?before ?stepResult)
        (MM0SourceFoldAfterStepV1
          ?rest (NatSuccV1 ?position) ?stepResult
          ?outcome ?after ?history)))
    (rule mm0-source-fold-after-accepted-v1
      (head
        (MM0SourceFoldAfterStepV1
          ?rest ?position
          (MM0SourceStepAcceptedV1 ?next ?historyEntry)
          ?outcome ?after
          (MM0SourceHistoryConsV1 ?historyEntry ?history)))
      (body
        (MM0SourceFoldV1
          ?rest ?position ?next ?outcome ?after ?history)))
    (rule mm0-source-fold-after-refused-v1
      (head
        (MM0SourceFoldAfterStepV1
          ?rest ?position
          (MM0SourceStepRefusedV1 ?reason ?state ?historyEntry)
          ?reason ?state
          (MM0SourceHistoryConsV1
            ?historyEntry MM0SourceHistoryNilV1)))
      (body))

    (rule mm0-source-environment-from-ast-v1
      (head
        (MM0SourceEnvironmentFromAstV1
          (node mm0-file ?statements) ?outcome ?state ?history))
      (body
        (MM0SourceFoldV1
          ?statements NatZeroV1
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
          ?outcome ?state ?history)))))
