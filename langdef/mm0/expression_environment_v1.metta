; MM0 expression elaboration over the runtime-produced Pratt tree.
;
; This presentation realizes the Expr, ExprBi, and ExprProv judgments from
; the MM0 self-specification.  Concrete source text and live notation remain
; runtime inputs: the generated target contains only these reusable rules.

(gslt-presentation-v1 MM0ExpressionEnvironmentV1
  (signature
    (operator cons 2)
    (operator different 2)
    (operator MM0SourceStateV1 2)
    (operator MM0TextualEnvironmentV1 2)
    (operator MM0SortModifiersV1 4)
    (operator MM0SortLookupV1 3)
    (operator MM0SortFoundV1 2)
    (operator MM0TermEntryV1 4)
    (operator MM0TermSignatureV1 2)
    (operator MM0DeclarationEntriesConsV1 2)
    (operator MM0TypeV1 2)
    (operator MM0BinderV1 3)
    (operator MM0NamedBinderV1 1)

    (operator GSLTPrattApplicationV1 2)
    (operator GSLTPrattIdentifierApplicationV1 2)
    (operator GSLTPrattArgumentsConsV1 2)
    (operator MM0RuntimeMathParseV1 4)
    (operator MM0MathScopeConsV1 2)

    (operator MM0BinderLookupV1 3)
    (operator MM0BinderFoundV1 1)
    (operator MM0TermSignatureLookupV1 3)
    (operator MM0TermSignatureFoundV1 2)
    (operator MM0BinderScopeV1 2)

    (operator MM0ExpressionInferV1 6)
    (operator MM0ExpressionIdentifierAfterBinderV1 9)
    (operator MM0ExpressionApplicationV1 8)
    (operator MM0ExpressionApplicationAfterLookupV1 8)
    (operator MM0ExpressionApplicationAfterArgumentsV1 5)
    (operator MM0ExpressionArgumentsV1 7)
    (operator MM0ExpressionArgumentsAfterHeadV1 6)
    (operator MM0ExpressionArgumentsPendingV1 3)
    (operator MM0ExpressionArgumentsPrependV1 3)
    (operator MM0ExpressionArgumentV1 7)
    (operator MM0ExpressionBoundArgumentV1 5)
    (operator MM0ExpressionBoundAfterLookupV1 6)
    (operator MM0ExpressionRegularAfterInferV1 4)

    (operator MM0ExpressionProvableV1 6)
    (operator MM0ExpressionProvableAfterInferV1 4)
    (operator MM0ExpressionProvableAfterSortV1 4)
    (operator MM0ExpressionResultSortV1 2)
    (operator MM0RuntimeExpressionElaborateV1 5)
    (operator MM0RuntimeProvableExpressionElaborateV1 5)

    (operator MM0ExpressionAcceptedV1 2)
    (operator MM0ExpressionArgumentsAcceptedV1 1)
    (operator MM0ExpressionArgumentsConsV1 2)
    (operator MM0ExpressionRefusedV1 1)
    (operator MM0ExpressionVariableV1 2)
    (operator MM0ExpressionTermApplicationV1 3)

    (operator MM0RefusedUnknownExpressionNameV1 2)
    (operator MM0RefusedAppliedVariableV1 2)
    (operator MM0RefusedExpressionArityV1 2)
    (operator MM0RefusedBoundArgumentShapeV1 2)
    (operator MM0RefusedBoundArgumentKindV1 3)
    (operator MM0RefusedExpressionSortV1 4)
    (operator MM0RefusedNonProvableSortV1 2))
  (equations)
  (rewrites
    ; Context lookup is total over the normalized binder list.  Anonymous
    ; binders are deliberately absent from the source-level name scope.
    (rule mm0-binder-lookup-missing-v1
      (head
        (MM0BinderLookupV1
          ?name nil MM0BinderMissingV1))
      (body))
    (rule mm0-binder-lookup-found-v1
      (head
        (MM0BinderLookupV1
          ?name
          (cons
            (MM0BinderV1
              ?kind (MM0NamedBinderV1 ?name) ?type)
            ?rest)
          (MM0BinderFoundV1
            (MM0BinderV1
              ?kind (MM0NamedBinderV1 ?name) ?type))))
      (body))
    (rule mm0-binder-lookup-named-tail-v1
      (head
        (MM0BinderLookupV1
          ?name
          (cons
            (MM0BinderV1
              ?kind (MM0NamedBinderV1 ?other) ?type)
            ?rest)
          ?result))
      (body
        (different ?name ?other)
        (MM0BinderLookupV1 ?name ?rest ?result)))
    (rule mm0-binder-lookup-anonymous-tail-v1
      (head
        (MM0BinderLookupV1
          ?name
          (cons
            (MM0BinderV1 ?kind MM0AnonymousBinderV1 ?type)
            ?rest)
          ?result))
      (body
        (MM0BinderLookupV1 ?name ?rest ?result)))

    ; The parser's scope is derived from the same normalized binder context
    ; used by expression checking.  This makes live syntax and Expr agree on
    ; which identifiers denote variables.
    (rule mm0-binder-scope-nil-v1
      (head
        (MM0BinderScopeV1 nil MM0MathScopeNilV1))
      (body))
    (rule mm0-binder-scope-named-v1
      (head
        (MM0BinderScopeV1
          (cons
            (MM0BinderV1
              ?kind (MM0NamedBinderV1 ?name) ?type)
            ?rest)
          (MM0MathScopeConsV1 ?name ?scope)))
      (body
        (MM0BinderScopeV1 ?rest ?scope)))
    (rule mm0-binder-scope-anonymous-v1
      (head
        (MM0BinderScopeV1
          (cons
            (MM0BinderV1 ?kind MM0AnonymousBinderV1 ?type)
            ?rest)
          ?scope))
      (body
        (MM0BinderScopeV1 ?rest ?scope)))

    ; ExprVar first consults the binder context.  If the name is absent, the
    ; same syntax denotes a nullary or explicitly applied declared term.
    (rule mm0-expression-infer-identifier-v1
      (head
        (MM0ExpressionInferV1
          ?position ?sorts ?declarations ?context
          (GSLTPrattIdentifierApplicationV1 ?name ?arguments)
          ?result))
      (body
        (MM0BinderLookupV1 ?name ?context ?lookup)
        (MM0ExpressionIdentifierAfterBinderV1
          ?position ?sorts ?declarations ?context
          ?name ?arguments ?lookup MM0ExpressionIdentifierV1 ?result)))
    (rule mm0-expression-identifier-variable-v1
      (head
        (MM0ExpressionIdentifierAfterBinderV1
          ?position ?sorts ?declarations ?context ?name
          GSLTPrattArgumentsNilV1
          (MM0BinderFoundV1
            (MM0BinderV1 ?kind ?binderName
              (MM0TypeV1 ?sort ?dependencies)))
          MM0ExpressionIdentifierV1
          (MM0ExpressionAcceptedV1
            (MM0ExpressionVariableV1 ?name ?sort) ?sort)))
      (body))
    (rule mm0-expression-identifier-applied-variable-v1
      (head
        (MM0ExpressionIdentifierAfterBinderV1
          ?position ?sorts ?declarations ?context ?name
          (GSLTPrattArgumentsConsV1 ?argument ?rest)
          (MM0BinderFoundV1 ?binder)
          MM0ExpressionIdentifierV1
          (MM0ExpressionRefusedV1
            (MM0RefusedAppliedVariableV1 ?position ?name))))
      (body))
    (rule mm0-expression-identifier-term-v1
      (head
        (MM0ExpressionIdentifierAfterBinderV1
          ?position ?sorts ?declarations ?context ?name ?arguments
          MM0BinderMissingV1 MM0ExpressionIdentifierV1 ?result))
      (body
        (MM0ExpressionApplicationV1
          ?position ?sorts ?declarations ?context
          ?name ?arguments MM0IdentifierApplicationV1 ?result)))

    ; Notation applications and plain identifier applications share exactly
    ; the same ExprApp rule after parsing.
    (rule mm0-expression-infer-notation-application-v1
      (head
        (MM0ExpressionInferV1
          ?position ?sorts ?declarations ?context
          (GSLTPrattApplicationV1 ?name ?arguments) ?result))
      (body
        (MM0ExpressionApplicationV1
          ?position ?sorts ?declarations ?context
          ?name ?arguments MM0NotationApplicationV1 ?result)))
    (rule mm0-expression-application-v1
      (head
        (MM0ExpressionApplicationV1
          ?position ?sorts ?declarations ?context
          ?name ?arguments ?application-kind ?result))
      (body
        (MM0TermSignatureLookupV1 ?name ?declarations ?lookup)
        (MM0ExpressionApplicationAfterLookupV1
          ?position ?sorts ?declarations ?context
          ?name ?arguments ?lookup ?result)))
    (rule mm0-expression-application-missing-v1
      (head
        (MM0ExpressionApplicationAfterLookupV1
          ?position ?sorts ?declarations ?context ?name ?arguments
          MM0TermSignatureMissingV1
          (MM0ExpressionRefusedV1
            (MM0RefusedUnknownExpressionNameV1 ?position ?name))))
      (body))
    (rule mm0-expression-application-found-v1
      (head
        (MM0ExpressionApplicationAfterLookupV1
          ?position ?sorts ?declarations ?context ?name ?arguments
          (MM0TermSignatureFoundV1
            (MM0TermSignatureV1 ?binders
              (MM0TypeV1 ?returnSort ?returnDependencies))
            ?firstPosition)
          ?result))
      (body
        (MM0ExpressionArgumentsV1
          ?position ?sorts ?declarations ?context
          ?arguments ?binders ?argumentResult)
        (MM0ExpressionApplicationAfterArgumentsV1
          ?name ?returnSort ?argumentResult ?firstPosition ?result)))
    (rule mm0-expression-application-arguments-accepted-v1
      (head
        (MM0ExpressionApplicationAfterArgumentsV1
          ?name ?returnSort
          (MM0ExpressionArgumentsAcceptedV1 ?arguments)
          ?firstPosition
          (MM0ExpressionAcceptedV1
            (MM0ExpressionTermApplicationV1
              ?name ?arguments ?returnSort)
            ?returnSort)))
      (body))
    (rule mm0-expression-application-arguments-refused-v1
      (head
        (MM0ExpressionApplicationAfterArgumentsV1
          ?name ?returnSort
          (MM0ExpressionRefusedV1 ?reason) ?firstPosition
          (MM0ExpressionRefusedV1 ?reason)))
      (body))

    ; all2 ExprBi: the tree arguments and authored binders are consumed in
    ; lockstep.  Arity disagreement is an explicit semantic refusal.
    (rule mm0-expression-arguments-nil-v1
      (head
        (MM0ExpressionArgumentsV1
          ?position ?sorts ?declarations ?context
          GSLTPrattArgumentsNilV1 nil
          (MM0ExpressionArgumentsAcceptedV1
            MM0ExpressionArgumentsNilV1)))
      (body))
    (rule mm0-expression-arguments-cons-v1
      (head
        (MM0ExpressionArgumentsV1
          ?position ?sorts ?declarations ?context
          (GSLTPrattArgumentsConsV1 ?argument ?arguments)
          (cons ?binder ?binders)
          ?result))
      (body
        (MM0ExpressionArgumentV1
          ?position ?sorts ?declarations ?context
          ?argument ?binder ?headResult)
        (MM0ExpressionArgumentsAfterHeadV1
          ?position ?sorts ?declarations ?context
          (MM0ExpressionArgumentsPendingV1
            ?arguments ?binders ?headResult)
          ?result)))
    (rule mm0-expression-arguments-too-many-v1
      (head
        (MM0ExpressionArgumentsV1
          ?position ?sorts ?declarations ?context
          (GSLTPrattArgumentsConsV1 ?argument ?arguments) nil
          (MM0ExpressionRefusedV1
            (MM0RefusedExpressionArityV1
              ?position MM0TooManyArgumentsV1))))
      (body))
    (rule mm0-expression-arguments-too-few-v1
      (head
        (MM0ExpressionArgumentsV1
          ?position ?sorts ?declarations ?context
          GSLTPrattArgumentsNilV1 (cons ?binder ?binders)
          (MM0ExpressionRefusedV1
            (MM0RefusedExpressionArityV1
              ?position MM0TooFewArgumentsV1))))
      (body))
    (rule mm0-expression-arguments-head-refused-v1
      (head
        (MM0ExpressionArgumentsAfterHeadV1
          ?position ?sorts ?declarations ?context
          (MM0ExpressionArgumentsPendingV1
            ?arguments ?binders (MM0ExpressionRefusedV1 ?reason))
          (MM0ExpressionRefusedV1 ?reason)))
      (body))
    (rule mm0-expression-arguments-head-accepted-v1
      (head
        (MM0ExpressionArgumentsAfterHeadV1
          ?position ?sorts ?declarations ?context
          (MM0ExpressionArgumentsPendingV1
            ?arguments ?binders
            (MM0ExpressionAcceptedV1 ?expression ?sort))
          ?result))
      (body
        (MM0ExpressionArgumentsV1
          ?position ?sorts ?declarations ?context
          ?arguments ?binders ?restResult)
        (MM0ExpressionArgumentsPrependV1
          ?expression ?restResult ?result)))
    (rule mm0-expression-arguments-prepend-accepted-v1
      (head
        (MM0ExpressionArgumentsPrependV1
          ?expression
          (MM0ExpressionArgumentsAcceptedV1 ?rest)
          (MM0ExpressionArgumentsAcceptedV1
            (MM0ExpressionArgumentsConsV1 ?expression ?rest))))
      (body))
    (rule mm0-expression-arguments-prepend-refused-v1
      (head
        (MM0ExpressionArgumentsPrependV1
          ?expression (MM0ExpressionRefusedV1 ?reason)
          (MM0ExpressionRefusedV1 ?reason)))
      (body))

    ; ExprBiBound requires a source variable whose context entry is itself a
    ; bound binder of the expected sort.  General expressions are refused.
    (rule mm0-expression-argument-bound-v1
      (head
        (MM0ExpressionArgumentV1
          ?position ?sorts ?declarations ?context ?argument
          (MM0BinderV1 MM0BoundBinderKindV1 ?binderName
            (MM0TypeV1 ?expectedSort ?dependencies))
          ?result))
      (body
        (MM0ExpressionBoundArgumentV1
          ?position ?context ?argument ?expectedSort ?result)))
    (rule mm0-expression-bound-identifier-v1
      (head
        (MM0ExpressionBoundArgumentV1
          ?position ?context
          (GSLTPrattIdentifierApplicationV1
            ?name GSLTPrattArgumentsNilV1)
          ?expectedSort ?result))
      (body
        (MM0BinderLookupV1 ?name ?context ?lookup)
        (MM0ExpressionBoundAfterLookupV1
          ?position ?name ?expectedSort ?lookup
          MM0BoundArgumentV1 ?result)))
    (rule mm0-expression-bound-applied-identifier-v1
      (head
        (MM0ExpressionBoundArgumentV1
          ?position ?context
          (GSLTPrattIdentifierApplicationV1
            ?name (GSLTPrattArgumentsConsV1 ?argument ?rest))
          ?expectedSort
          (MM0ExpressionRefusedV1
            (MM0RefusedBoundArgumentShapeV1 ?position ?name))))
      (body))
    (rule mm0-expression-bound-application-v1
      (head
        (MM0ExpressionBoundArgumentV1
          ?position ?context
          (GSLTPrattApplicationV1 ?name ?arguments)
          ?expectedSort
          (MM0ExpressionRefusedV1
            (MM0RefusedBoundArgumentShapeV1 ?position ?name))))
      (body))
    (rule mm0-expression-bound-missing-v1
      (head
        (MM0ExpressionBoundAfterLookupV1
          ?position ?name ?expectedSort MM0BinderMissingV1
          MM0BoundArgumentV1
          (MM0ExpressionRefusedV1
            (MM0RefusedUnknownExpressionNameV1 ?position ?name))))
      (body))
    (rule mm0-expression-bound-regular-v1
      (head
        (MM0ExpressionBoundAfterLookupV1
          ?position ?name ?expectedSort
          (MM0BinderFoundV1
            (MM0BinderV1 MM0RegularBinderKindV1 ?binderName
              (MM0TypeV1 ?actualSort ?dependencies)))
          MM0BoundArgumentV1
          (MM0ExpressionRefusedV1
            (MM0RefusedBoundArgumentKindV1
              ?position ?name MM0RegularBinderKindV1))))
      (body))
    (rule mm0-expression-bound-sort-accepted-v1
      (head
        (MM0ExpressionBoundAfterLookupV1
          ?position ?name ?sort
          (MM0BinderFoundV1
            (MM0BinderV1 MM0BoundBinderKindV1 ?binderName
              (MM0TypeV1 ?sort ?dependencies)))
          MM0BoundArgumentV1
          (MM0ExpressionAcceptedV1
            (MM0ExpressionVariableV1 ?name ?sort) ?sort)))
      (body))
    (rule mm0-expression-bound-sort-refused-v1
      (head
        (MM0ExpressionBoundAfterLookupV1
          ?position ?name ?expectedSort
          (MM0BinderFoundV1
            (MM0BinderV1 MM0BoundBinderKindV1 ?binderName
              (MM0TypeV1 ?actualSort ?dependencies)))
          MM0BoundArgumentV1
          (MM0ExpressionRefusedV1
            (MM0RefusedExpressionSortV1
              ?position ?name ?expectedSort ?actualSort))))
      (body
        (different ?expectedSort ?actualSort)))

    ; ExprBiReg delegates to Expr and then checks the authored binder sort.
    (rule mm0-expression-argument-regular-v1
      (head
        (MM0ExpressionArgumentV1
          ?position ?sorts ?declarations ?context ?argument
          (MM0BinderV1 MM0RegularBinderKindV1 ?binderName
            (MM0TypeV1 ?expectedSort ?dependencies))
          ?result))
      (body
        (MM0ExpressionInferV1
          ?position ?sorts ?declarations ?context ?argument ?inferred)
        (MM0ExpressionRegularAfterInferV1
          ?position ?expectedSort ?inferred ?result)))
    (rule mm0-expression-regular-refused-v1
      (head
        (MM0ExpressionRegularAfterInferV1
          ?position ?expectedSort
          (MM0ExpressionRefusedV1 ?reason)
          (MM0ExpressionRefusedV1 ?reason)))
      (body))
    (rule mm0-expression-regular-sort-accepted-v1
      (head
        (MM0ExpressionRegularAfterInferV1
          ?position ?sort
          (MM0ExpressionAcceptedV1 ?expression ?sort)
          (MM0ExpressionAcceptedV1 ?expression ?sort)))
      (body))
    (rule mm0-expression-regular-sort-refused-v1
      (head
        (MM0ExpressionRegularAfterInferV1
          ?position ?expectedSort
          (MM0ExpressionAcceptedV1 ?expression ?actualSort)
          (MM0ExpressionRefusedV1
            (MM0RefusedExpressionSortV1
              ?position ?expression ?expectedSort ?actualSort))))
      (body
        (different ?expectedSort ?actualSort)))

    ; ExprProv is Expr followed by a lookup of the result sort's provable
    ; modifier.  Non-provable is a semantic refusal, not parser failure.
    (rule mm0-expression-provable-v1
      (head
        (MM0ExpressionProvableV1
          ?position ?sorts ?declarations ?context ?tree ?result))
      (body
        (MM0ExpressionInferV1
          ?position ?sorts ?declarations ?context ?tree ?inferred)
        (MM0ExpressionProvableAfterInferV1
          ?position ?sorts ?inferred ?result)))
    (rule mm0-expression-provable-infer-refused-v1
      (head
        (MM0ExpressionProvableAfterInferV1
          ?position ?sorts (MM0ExpressionRefusedV1 ?reason)
          (MM0ExpressionRefusedV1 ?reason)))
      (body))
    (rule mm0-expression-provable-inferred-v1
      (head
        (MM0ExpressionProvableAfterInferV1
          ?position ?sorts
          (MM0ExpressionAcceptedV1 ?expression ?sort) ?result))
      (body
        (MM0SortLookupV1 ?sort ?sorts ?lookup)
        (MM0ExpressionProvableAfterSortV1
          ?position ?expression ?lookup ?result)))
    (rule mm0-expression-provable-sort-v1
      (head
        (MM0ExpressionProvableAfterSortV1
          ?position ?expression
          (MM0SortFoundV1
            (MM0SortModifiersV1
              ?pure ?strict MM0ModifierPresentV1 ?free)
            ?firstPosition)
          (MM0ExpressionAcceptedV1 ?expression ?sort)))
      (body
        (MM0ExpressionResultSortV1 ?expression ?sort)))
    (rule mm0-expression-non-provable-sort-v1
      (head
        (MM0ExpressionProvableAfterSortV1
          ?position ?expression
          (MM0SortFoundV1
            (MM0SortModifiersV1
              ?pure ?strict MM0ModifierAbsentV1 ?free)
            ?firstPosition)
          (MM0ExpressionRefusedV1
            (MM0RefusedNonProvableSortV1 ?position ?sort))))
      (body
        (MM0ExpressionResultSortV1 ?expression ?sort)))
    (rule mm0-expression-provable-sort-missing-v1
      (head
        (MM0ExpressionProvableAfterSortV1
          ?position ?expression MM0SortMissingV1
          (MM0ExpressionRefusedV1
            (MM0RefusedUnknownExpressionNameV1
              ?position MM0ExpressionResultSortV1))))
      (body))
    (rule mm0-expression-result-sort-variable-v1
      (head
        (MM0ExpressionResultSortV1
          (MM0ExpressionVariableV1 ?name ?sort) ?sort))
      (body))
    (rule mm0-expression-result-sort-application-v1
      (head
        (MM0ExpressionResultSortV1
          (MM0ExpressionTermApplicationV1 ?name ?arguments ?sort) ?sort))
      (body))

    ; Runtime composition: fixed compiled rules consume a live environment,
    ; binder scope, and source math scalars.  No source file is compiler input.
    (rule mm0-runtime-expression-elaborate-v1
      (head
        (MM0RuntimeExpressionElaborateV1
          ?position
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?context ?scalars ?result))
      (body
        (MM0BinderScopeV1 ?context ?scope)
        (MM0RuntimeMathParseV1
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?scope ?scalars ?tree)
        (MM0ExpressionInferV1
          ?position ?sorts ?declarations ?context ?tree ?result)))
    (rule mm0-runtime-provable-expression-elaborate-v1
      (head
        (MM0RuntimeProvableExpressionElaborateV1
          ?position
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?context ?scalars ?result))
      (body
        (MM0BinderScopeV1 ?context ?scope)
        (MM0RuntimeMathParseV1
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?scope ?scalars ?tree)
        (MM0ExpressionProvableV1
          ?position ?sorts ?declarations ?context ?tree ?result)))))
