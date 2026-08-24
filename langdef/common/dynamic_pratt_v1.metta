; Generic dynamic Pratt parser as an authored GSLT.
;
; The parser is parameterized by relations supplied by a language adapter:
; atom, prefix, infix, grouping, precedence comparison, and precedence bounds.
; A source file therefore changes the environment value, never this program.

(gslt-presentation-v1 GSLTDynamicPrattV1
  (signature
    (operator GSLTPrattApplicationV1 2)
    (operator GSLTPrattIdentifierApplicationV1 2)
    (operator GSLTPrattArgumentsConsV1 2)
    (operator GSLTPrattStrictMinimumV1 1)
    (operator GSLTPrattTokensConsV1 2)
    (operator GSLTPrattParseAllV1 4)
    (operator GSLTPrattParseV1 6)
    (operator GSLTPrattParsePrefixArgumentsV1 7)
    (operator GSLTPrattParseIdentifierArityV1 8)
    (operator GSLTPrattParseIdentifierArgumentsV1 6)
    (operator GSLTPrattParseTailV1 7)

    ; Adapter relations.
    (operator GSLTPrattMinimumV1 2)
    (operator GSLTPrattMaximumV1 2)
    (operator GSLTPrattApplicationPrecedenceV1 2)
    (operator GSLTPrattAtLeastV1 2)
    (operator GSLTPrattNotAtLeastV1 2)
    (operator GSLTPrattIdentifierV1 5)
    (operator GSLTPrattPrefixV1 5)
    (operator GSLTPrattInfixV1 5)
    (operator GSLTPrattNotInfixV1 2)
    (operator GSLTPrattGroupV1 3)
    (operator NatSuccV1 1))
  (equations)
  (rewrites
    (rule gslt-pratt-parse-all-v1
      (head
        (GSLTPrattParseAllV1 ?environment ?scope ?tokens ?tree))
      (body
        (GSLTPrattMinimumV1 ?environment ?minimum)
        (GSLTPrattParseV1
          ?environment ?scope ?minimum ?tokens ?tree
          GSLTPrattTokensNilV1)))

    ; Identifier arity comes from the live language environment.  Parsing an
    ; application therefore consumes exactly the authored number of arguments
    ; instead of guessing from token lookahead.
    (rule gslt-pratt-parse-identifier-v1
      (head
        (GSLTPrattParseV1
          ?environment ?scope ?minimum
          (GSLTPrattTokensConsV1 ?token ?tail) ?tree ?rest))
      (body
        (GSLTPrattIdentifierV1
          ?environment ?scope ?token ?identifier ?arity)
        (GSLTPrattParseIdentifierArityV1
          ?environment ?scope ?minimum ?identifier ?arity ?tail
          ?tree ?rest)))

    (rule gslt-pratt-identifier-zero-v1
      (head
        (GSLTPrattParseIdentifierArityV1
          ?environment ?scope ?minimum ?identifier NatZeroV1 ?tokens
          ?tree ?rest))
      (body
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum
          (GSLTPrattIdentifierApplicationV1
            ?identifier GSLTPrattArgumentsNilV1)
          ?tokens ?tree ?rest)))
    (rule gslt-pratt-identifier-successor-v1
      (head
        (GSLTPrattParseIdentifierArityV1
          ?environment ?scope ?minimum ?identifier
          (NatSuccV1 ?remaining) ?tokens ?tree ?rest))
      (body
        (GSLTPrattApplicationPrecedenceV1
          ?environment ?applicationPrecedence)
        (GSLTPrattAtLeastV1 ?applicationPrecedence ?minimum)
        (GSLTPrattParseIdentifierArgumentsV1
          ?environment ?scope (NatSuccV1 ?remaining) ?tokens
          ?arguments ?afterArguments)
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum
          (GSLTPrattIdentifierApplicationV1 ?identifier ?arguments)
          ?afterArguments ?tree ?rest)))

    (rule gslt-pratt-parse-prefix-v1
      (head
        (GSLTPrattParseV1
          ?environment ?scope ?minimum
          (GSLTPrattTokensConsV1 ?token ?tail) ?tree ?rest))
      (body
        (GSLTPrattPrefixV1
          ?environment ?token ?constructor ?arity ?precedence)
        (GSLTPrattAtLeastV1 ?precedence ?minimum)
        (GSLTPrattParsePrefixArgumentsV1
          ?environment ?scope ?precedence ?arity ?tail
          ?arguments ?afterArguments)
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum
          (GSLTPrattApplicationV1 ?constructor ?arguments)
          ?afterArguments ?tree ?rest)))

    (rule gslt-pratt-parse-group-v1
      (head
        (GSLTPrattParseV1
          ?environment ?scope ?minimum
          (GSLTPrattTokensConsV1 ?open ?tail) ?tree ?rest))
      (body
        (GSLTPrattGroupV1 ?environment ?open ?close)
        (GSLTPrattMinimumV1 ?environment ?innerMinimum)
        (GSLTPrattParseV1
          ?environment ?scope ?innerMinimum ?tail ?inside
          (GSLTPrattTokensConsV1 ?close ?afterClose))
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum ?inside ?afterClose ?tree ?rest)))

    (rule gslt-pratt-prefix-arguments-zero-v1
      (head
        (GSLTPrattParsePrefixArgumentsV1
          ?environment ?scope ?precedence NatZeroV1 ?tokens
          GSLTPrattArgumentsNilV1 ?tokens))
      (body))

    ; MM0's prefix convention parses the last argument at the operator's
    ; precedence and every earlier argument at maximum precedence.
    (rule gslt-pratt-prefix-arguments-one-v1
      (head
        (GSLTPrattParsePrefixArgumentsV1
          ?environment ?scope ?precedence
          (NatSuccV1 NatZeroV1) ?tokens
          (GSLTPrattArgumentsConsV1
            ?argument GSLTPrattArgumentsNilV1)
          ?rest))
      (body
        (GSLTPrattParseV1
          ?environment ?scope ?precedence ?tokens ?argument ?rest)))

    (rule gslt-pratt-prefix-arguments-many-v1
      (head
        (GSLTPrattParsePrefixArgumentsV1
          ?environment ?scope ?precedence
          (NatSuccV1 (NatSuccV1 ?remaining)) ?tokens
          (GSLTPrattArgumentsConsV1 ?argument ?arguments) ?rest))
      (body
        (GSLTPrattMaximumV1 ?environment ?maximum)
        (GSLTPrattParseV1
          ?environment ?scope ?maximum ?tokens ?argument ?afterArgument)
        (GSLTPrattParsePrefixArgumentsV1
          ?environment ?scope ?precedence
          (NatSuccV1 ?remaining) ?afterArgument
          ?arguments ?rest)))

    (rule gslt-pratt-identifier-arguments-zero-v1
      (head
        (GSLTPrattParseIdentifierArgumentsV1
          ?environment ?scope NatZeroV1 ?tokens
          GSLTPrattArgumentsNilV1 ?tokens))
      (body))
    (rule gslt-pratt-identifier-arguments-successor-v1
      (head
        (GSLTPrattParseIdentifierArgumentsV1
          ?environment ?scope (NatSuccV1 ?remaining) ?tokens
          (GSLTPrattArgumentsConsV1 ?argument ?arguments) ?rest))
      (body
        (GSLTPrattMaximumV1 ?environment ?maximum)
        (GSLTPrattParseV1
          ?environment ?scope ?maximum ?tokens ?argument ?afterArgument)
        (GSLTPrattParseIdentifierArgumentsV1
          ?environment ?scope ?remaining ?afterArgument ?arguments ?rest)))

    (rule gslt-pratt-tail-nil-v1
      (head
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum ?current
          GSLTPrattTokensNilV1 ?current GSLTPrattTokensNilV1))
      (body))

    ; A non-infix token belongs to the enclosing parse (a following prefix
    ; argument, a group closer, or an eventual top-level error).
    (rule gslt-pratt-tail-stop-non-infix-v1
      (head
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum ?current
          (GSLTPrattTokensConsV1 ?token ?tail)
          ?current (GSLTPrattTokensConsV1 ?token ?tail)))
      (body
        (GSLTPrattNotInfixV1 ?environment ?token)))

    (rule gslt-pratt-tail-stop-bound-v1
      (head
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum ?current
          (GSLTPrattTokensConsV1 ?token ?tail)
          ?current (GSLTPrattTokensConsV1 ?token ?tail)))
      (body
        (GSLTPrattInfixV1
          ?environment ?token ?constructor ?association ?precedence)
        (GSLTPrattNotAtLeastV1 ?precedence ?minimum)))

    (rule gslt-pratt-tail-left-v1
      (head
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum ?current
          (GSLTPrattTokensConsV1 ?token ?tail) ?tree ?rest))
      (body
        (GSLTPrattInfixV1
          ?environment ?token ?constructor
          GSLTPrattLeftAssociativeV1 ?precedence)
        (GSLTPrattAtLeastV1 ?precedence ?minimum)
        (GSLTPrattParseV1
          ?environment ?scope (GSLTPrattStrictMinimumV1 ?precedence)
          ?tail ?right ?afterRight)
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum
          (GSLTPrattApplicationV1
            ?constructor
            (GSLTPrattArgumentsConsV1 ?current
              (GSLTPrattArgumentsConsV1
                ?right GSLTPrattArgumentsNilV1)))
          ?afterRight ?tree ?rest)))

    (rule gslt-pratt-tail-right-v1
      (head
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum ?current
          (GSLTPrattTokensConsV1 ?token ?tail) ?tree ?rest))
      (body
        (GSLTPrattInfixV1
          ?environment ?token ?constructor
          GSLTPrattRightAssociativeV1 ?precedence)
        (GSLTPrattAtLeastV1 ?precedence ?minimum)
        (GSLTPrattParseV1
          ?environment ?scope ?precedence ?tail ?right ?afterRight)
        (GSLTPrattParseTailV1
          ?environment ?scope ?minimum
          (GSLTPrattApplicationV1
            ?constructor
            (GSLTPrattArgumentsConsV1 ?current
              (GSLTPrattArgumentsConsV1
                ?right GSLTPrattArgumentsNilV1)))
          ?afterRight ?tree ?rest)))))
