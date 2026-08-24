; Ordered MM0 axiom-and-theorem declaration semantics.
;
; The textual stage elaborates both declarations to the same well-formed
; assertion signature.  A theorem's proof is checked later by the distinct
; .mmb transition system.  Concrete .mm0 files remain runtime inputs.

(gslt-presentation-v1 MM0AssertionEnvironmentV1
  (signature
    (operator node 2)
    (operator pair 2)
    (operator some 1)
    (operator cons 2)
    (operator different 2)
    (operator MM0IdentifierScalarsV1 2)
    (operator MM0ListAppendV1 3)

    (operator MM0SourceStateV1 2)
    (operator MM0TextualEnvironmentV1 2)
    (operator MM0TermEntryV1 4)
    (operator MM0DeclarationEntriesConsV1 2)
    (operator MM0DeclarationLookupV1 3)
    (operator MM0TermSignatureLookupV1 3)
    (operator MM0CheckAcceptedV1 1)
    (operator MM0CheckRefusedV1 1)
    (operator MM0ValidateExplicitBindersV1 7)
    (operator MM0BinderSequenceV1 3)
    (operator MM0RuntimeProvableExpressionElaborateV1 5)
    (operator MM0ExpressionAcceptedV1 2)
    (operator MM0ExpressionRefusedV1 1)

    (operator MM0AssertionEntryV1 5)
    (operator MM0AssertionLookupV1 3)
    (operator MM0AssertionFoundV1 2)
    (operator MM0AssertionEntriesAppendV1 3)
    (operator MM0AssertionStatementV1 5)
    (operator MM0AssertionStepV1 4)
    (operator MM0AssertionStepAfterLookupV1 8)
    (operator MM0AssertionStepAfterValidationV1 6)
    (operator MM0AssertionValidateV1 6)
    (operator MM0AssertionAfterExplicitItemsV1 7)
    (operator MM0AssertionAfterArrowV1 7)
    (operator MM0AssertionAfterPartitionV1 6)
    (operator MM0AssertionAfterBindersV1 7)
    (operator MM0AssertionAfterHypothesesV1 7)
    (operator MM0AssertionAfterConclusionV1 5)

    (operator MM0AssertionItemsFromAstV1 4)
    (operator MM0AssertionItemsPrependV1 3)
    (operator MM0AssertionArrowNormalizeV1 4)
    (operator MM0AssertionArrowPrependV1 3)
    (operator MM0AssertionArrowV1 2)
    (operator MM0AssertionTypedItemV1 1)
    (operator MM0AssertionHypothesisItemV1 2)
    (operator MM0AssertionPartitionV1 2)
    (operator MM0AssertionPartitionTypedV1 4)
    (operator MM0AssertionPartitionTypedPrependV1 3)
    (operator MM0AssertionPartitionHypothesesV1 4)
    (operator MM0AssertionPartitionHypothesisPrependV1 3)

    (operator MM0AssertionHypothesesV1 6)
    (operator MM0AssertionHypothesisAfterExpressionV1 8)
    (operator MM0HypothesisEntriesFromNamesV1 4)
    (operator MM0HypothesisEntriesPrependV1 3)
    (operator MM0HypothesisV1 2)
    (operator MM0AssertionSignatureV1 3)
    (operator MM0DeclaredAssertionV1 4)

    (operator MM0AssertionStepAcceptedV1 2)
    (operator MM0AssertionStepRefusedV1 3)
    (operator MM0StatementClassV1 2)
    (operator MM0SourceStepByClassV1 5)
    (operator MM0SourceStepAcceptedV1 2)
    (operator MM0SourceStepRefusedV1 3)

    (operator MM0RefusedAssertionRedeclarationV1 3)
    (operator MM0RefusedAssertionBinderOrderV1 2)
    (operator MM0RefusedAssertionConclusionTypeV1 2))
  (equations)
  (rewrites
    ; Assertions share the declaration sequence with terms so source order is
    ; retained, but MM0 gives terms and assertions distinct name spaces.
    (rule mm0-declaration-lookup-skip-assertion-v1
      (head
        (MM0DeclarationLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0AssertionEntryV1
              ?kind ?assertionName ?signature ?position ?source)
            ?rest)
          ?result))
      (body
        (MM0DeclarationLookupV1 ?name ?rest ?result)))
    (rule mm0-term-signature-lookup-skip-assertion-v1
      (head
        (MM0TermSignatureLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0AssertionEntryV1
              ?kind ?assertionName ?signature ?position ?source)
            ?rest)
          ?result))
      (body
        (MM0TermSignatureLookupV1 ?name ?rest ?result)))

    (rule mm0-assertion-lookup-missing-v1
      (head
        (MM0AssertionLookupV1
          ?name MM0DeclarationEntriesNilV1 MM0AssertionMissingV1))
      (body))
    (rule mm0-assertion-lookup-found-v1
      (head
        (MM0AssertionLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0AssertionEntryV1
              ?kind ?name ?signature ?position ?source)
            ?rest)
          (MM0AssertionFoundV1 ?kind ?position)))
      (body))
    (rule mm0-assertion-lookup-other-assertion-v1
      (head
        (MM0AssertionLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0AssertionEntryV1
              ?kind ?other ?signature ?position ?source)
            ?rest)
          ?result))
      (body
        (different ?name ?other)
        (MM0AssertionLookupV1 ?name ?rest ?result)))
    (rule mm0-assertion-lookup-skip-term-v1
      (head
        (MM0AssertionLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1
              ?termName ?signature ?position ?source)
            ?rest)
          ?result))
      (body
        (MM0AssertionLookupV1 ?name ?rest ?result)))

    (rule mm0-assertion-entries-append-nil-v1
      (head
        (MM0AssertionEntriesAppendV1
          MM0DeclarationEntriesNilV1 ?entry
          (MM0DeclarationEntriesConsV1
            ?entry MM0DeclarationEntriesNilV1)))
      (body))
    (rule mm0-assertion-entries-append-cons-v1
      (head
        (MM0AssertionEntriesAppendV1
          (MM0DeclarationEntriesConsV1 ?head ?rest) ?entry
          (MM0DeclarationEntriesConsV1 ?head ?after)))
      (body
        (MM0AssertionEntriesAppendV1 ?rest ?entry ?after)))

    ; The outer grammar retains assertion kind, label, binder groups, and the
    ; recursive arrow syntax without specializing them to either target.
    (rule mm0-axiom-statement-v1
      (head
        (MM0AssertionStatementV1
          (node mm0-assertion-statement
            (pair
              (node mm0-axiom-kind ?keyword)
              (pair ?identifier (pair ?binders ?arrow))))
          MM0AxiomDeclarationV1 ?name ?binders ?arrow))
      (body
        (MM0IdentifierScalarsV1 ?identifier ?name)))
    (rule mm0-theorem-statement-v1
      (head
        (MM0AssertionStatementV1
          (node mm0-assertion-statement
            (pair
              (node mm0-theorem-kind ?keyword)
              (pair ?identifier (pair ?binders ?arrow))))
          MM0TheoremDeclarationV1 ?name ?binders ?arrow))
      (body
        (MM0IdentifierScalarsV1 ?identifier ?name)))

    ; Convert the explicit binder syntax to a neutral ordered item list.
    ; Type items are later validated by the existing term-binder semantics;
    ; formula items become proof hypotheses.
    (rule mm0-assertion-items-nil-v1
      (head
        (MM0AssertionItemsFromAstV1
          ?position ?name nil (MM0CheckAcceptedV1 nil)))
      (body))
    (rule mm0-assertion-items-bound-v1
      (head
        (MM0AssertionItemsFromAstV1
          ?position ?name
          (cons
            (node mm0-formula-binder-bound (pair ?names ?type)) ?rest)
          ?result))
      (body
        (MM0AssertionItemsFromAstV1
          ?position ?name ?rest ?restResult)
        (MM0AssertionItemsPrependV1
          (MM0AssertionTypedItemV1
            (node mm0-type-binder-bound (pair ?names ?type)))
          ?restResult ?result)))
    (rule mm0-assertion-items-regular-type-v1
      (head
        (MM0AssertionItemsFromAstV1
          ?position ?name
          (cons
            (node mm0-formula-binder-regular
              (pair ?names (node mm0-type ?typePayload)))
            ?rest)
          ?result))
      (body
        (MM0AssertionItemsFromAstV1
          ?position ?name ?rest ?restResult)
        (MM0AssertionItemsPrependV1
          (MM0AssertionTypedItemV1
            (node mm0-type-binder-regular
              (pair ?names (node mm0-type ?typePayload))))
          ?restResult ?result)))
    (rule mm0-assertion-items-hypothesis-v1
      (head
        (MM0AssertionItemsFromAstV1
          ?position ?name
          (cons
            (node mm0-formula-binder-regular
              (pair ?names (node mm0-math-string ?scalars)))
            ?rest)
          ?result))
      (body
        (MM0AssertionItemsFromAstV1
          ?position ?name ?rest ?restResult)
        (MM0AssertionItemsPrependV1
          (MM0AssertionHypothesisItemV1 ?names ?scalars)
          ?restResult ?result)))
    (rule mm0-assertion-items-prepend-accepted-v1
      (head
        (MM0AssertionItemsPrependV1
          ?item (MM0CheckAcceptedV1 ?rest)
          (MM0CheckAcceptedV1 (cons ?item ?rest))))
      (body))
    (rule mm0-assertion-items-prepend-refused-v1
      (head
        (MM0AssertionItemsPrependV1
          ?item (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Every non-final arrow unit is an anonymous binder or hypothesis.  The
    ; final unit must be a formula, exactly as in MM0's parseArrows/elaboration.
    (rule mm0-assertion-arrow-conclusion-v1
      (head
        (MM0AssertionArrowNormalizeV1
          ?position ?name
          (node mm0-formula-arrow-type
            (pair (node mm0-math-string ?scalars) none))
          (MM0CheckAcceptedV1 (MM0AssertionArrowV1 nil ?scalars))))
      (body))
    (rule mm0-assertion-arrow-conclusion-type-v1
      (head
        (MM0AssertionArrowNormalizeV1
          ?position ?name
          (node mm0-formula-arrow-type
            (pair (node mm0-type ?typePayload) none))
          (MM0CheckRefusedV1
            (MM0RefusedAssertionConclusionTypeV1 ?position ?name))))
      (body))
    (rule mm0-assertion-arrow-type-argument-v1
      (head
        (MM0AssertionArrowNormalizeV1
          ?position ?name
          (node mm0-formula-arrow-type
            (pair (node mm0-type ?typePayload) (some ?tail)))
          ?result))
      (body
        (MM0AssertionArrowNormalizeV1
          ?position ?name ?tail ?tailResult)
        (MM0AssertionArrowPrependV1
          (MM0AssertionTypedItemV1
            (node mm0-type-binder-regular
              (pair
                (cons
                  (node mm0-anonymous MM0ArrowAnonymousSourceV1) nil)
                (node mm0-type ?typePayload))))
          ?tailResult ?result)))
    (rule mm0-assertion-arrow-hypothesis-v1
      (head
        (MM0AssertionArrowNormalizeV1
          ?position ?name
          (node mm0-formula-arrow-type
            (pair (node mm0-math-string ?scalars) (some ?tail)))
          ?result))
      (body
        (MM0AssertionArrowNormalizeV1
          ?position ?name ?tail ?tailResult)
        (MM0AssertionArrowPrependV1
          (MM0AssertionHypothesisItemV1
            (cons MM0AnonymousHypothesisNameV1 nil) ?scalars)
          ?tailResult ?result)))
    (rule mm0-assertion-arrow-prepend-accepted-v1
      (head
        (MM0AssertionArrowPrependV1
          ?item
          (MM0CheckAcceptedV1
            (MM0AssertionArrowV1 ?items ?conclusion))
          (MM0CheckAcceptedV1
            (MM0AssertionArrowV1 (cons ?item ?items) ?conclusion))))
      (body))
    (rule mm0-assertion-arrow-prepend-refused-v1
      (head
        (MM0AssertionArrowPrependV1
          ?item (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Typed binders must form a prefix.  Once a hypothesis appears, a later
    ; typed binder would not admit the ctx ++ hyps split required by MM0.
    (rule mm0-assertion-partition-typed-nil-v1
      (head
        (MM0AssertionPartitionTypedV1
          ?position ?name nil
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 nil nil))))
      (body))
    (rule mm0-assertion-partition-typed-cons-v1
      (head
        (MM0AssertionPartitionTypedV1
          ?position ?name
          (cons (MM0AssertionTypedItemV1 ?group) ?rest)
          ?result))
      (body
        (MM0AssertionPartitionTypedV1
          ?position ?name ?rest ?restResult)
        (MM0AssertionPartitionTypedPrependV1
          ?group ?restResult ?result)))
    (rule mm0-assertion-partition-first-hypothesis-v1
      (head
        (MM0AssertionPartitionTypedV1
          ?position ?name
          (cons (MM0AssertionHypothesisItemV1 ?names ?scalars) ?rest)
          ?result))
      (body
        (MM0AssertionPartitionHypothesesV1
          ?position ?name
          (cons (MM0AssertionHypothesisItemV1 ?names ?scalars) ?rest)
          ?hypothesisResult)
        (MM0AssertionPartitionTypedPrependV1
          MM0NoTypedBinderV1 ?hypothesisResult ?result)))
    (rule mm0-assertion-partition-typed-prepend-v1
      (head
        (MM0AssertionPartitionTypedPrependV1
          ?group
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 ?groups ?hypotheses))
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1
              (cons ?group ?groups) ?hypotheses))))
      (body
        (different ?group MM0NoTypedBinderV1)))
    (rule mm0-assertion-partition-no-typed-prepend-v1
      (head
        (MM0AssertionPartitionTypedPrependV1
          MM0NoTypedBinderV1
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 ?groups ?hypotheses))
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 ?groups ?hypotheses))))
      (body))
    (rule mm0-assertion-partition-typed-prepend-refused-v1
      (head
        (MM0AssertionPartitionTypedPrependV1
          ?group (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    (rule mm0-assertion-partition-hypotheses-nil-v1
      (head
        (MM0AssertionPartitionHypothesesV1
          ?position ?name nil
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 nil nil))))
      (body))
    (rule mm0-assertion-partition-hypotheses-cons-v1
      (head
        (MM0AssertionPartitionHypothesesV1
          ?position ?name
          (cons (MM0AssertionHypothesisItemV1 ?names ?scalars) ?rest)
          ?result))
      (body
        (MM0AssertionPartitionHypothesesV1
          ?position ?name ?rest ?restResult)
        (MM0AssertionPartitionHypothesisPrependV1
          (MM0AssertionHypothesisItemV1 ?names ?scalars)
          ?restResult ?result)))
    (rule mm0-assertion-partition-hypotheses-typed-v1
      (head
        (MM0AssertionPartitionHypothesesV1
          ?position ?name
          (cons (MM0AssertionTypedItemV1 ?group) ?rest)
          (MM0CheckRefusedV1
            (MM0RefusedAssertionBinderOrderV1 ?position ?name))))
      (body))
    (rule mm0-assertion-partition-hypothesis-prepend-v1
      (head
        (MM0AssertionPartitionHypothesisPrependV1
          ?hypothesis
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 ?groups ?hypotheses))
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1
              ?groups (cons ?hypothesis ?hypotheses)))))
      (body))
    (rule mm0-assertion-partition-hypothesis-prepend-refused-v1
      (head
        (MM0AssertionPartitionHypothesisPrependV1
          ?hypothesis (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Elaborate hypotheses using the same live parser and ExprProv relation as
    ; conclusions.  Labels are retained for the later .mmb proof stage.
    (rule mm0-assertion-hypotheses-nil-v1
      (head
        (MM0AssertionHypothesesV1
          ?position ?state ?context nil ?name
          (MM0CheckAcceptedV1 nil)))
      (body))
    (rule mm0-assertion-hypotheses-cons-v1
      (head
        (MM0AssertionHypothesesV1
          ?position ?state ?context
          (cons
            (MM0AssertionHypothesisItemV1 ?names ?scalars) ?rest)
          ?name ?result))
      (body
        (MM0MathScalarsFromListV1 ?scalars ?mathScalars)
        (MM0RuntimeProvableExpressionElaborateV1
          ?position ?state ?context ?mathScalars ?expressionResult)
        (MM0AssertionHypothesisAfterExpressionV1
          ?position ?state ?context ?names ?rest ?name
          ?expressionResult ?result)))
    (rule mm0-assertion-hypothesis-expression-refused-v1
      (head
        (MM0AssertionHypothesisAfterExpressionV1
          ?position ?state ?context ?names ?rest ?name
          (MM0ExpressionRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-hypothesis-expression-accepted-v1
      (head
        (MM0AssertionHypothesisAfterExpressionV1
          ?position ?state ?context ?names ?rest ?name
          (MM0ExpressionAcceptedV1 ?expression ?sort) ?result))
      (body
        (MM0AssertionHypothesesV1
          ?position ?state ?context ?rest ?name ?restResult)
        (MM0HypothesisEntriesFromNamesV1
          ?names ?expression ?restResult ?result)))
    (rule mm0-hypothesis-entries-nil-v1
      (head
        (MM0HypothesisEntriesFromNamesV1
          nil ?expression ?restResult ?restResult))
      (body))
    (rule mm0-hypothesis-entries-cons-v1
      (head
        (MM0HypothesisEntriesFromNamesV1
          (cons ?hypothesisName ?names) ?expression ?restResult ?result))
      (body
        (MM0HypothesisEntriesFromNamesV1
          ?names ?expression ?restResult ?afterNames)
        (MM0HypothesisEntriesPrependV1
          (MM0HypothesisV1 ?hypothesisName ?expression)
          ?afterNames ?result)))
    (rule mm0-hypothesis-entries-prepend-accepted-v1
      (head
        (MM0HypothesisEntriesPrependV1
          ?hypothesis (MM0CheckAcceptedV1 ?rest)
          (MM0CheckAcceptedV1 (cons ?hypothesis ?rest))))
      (body))
    (rule mm0-hypothesis-entries-prepend-refused-v1
      (head
        (MM0HypothesisEntriesPrependV1
          ?hypothesis (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Validation pipeline: normalize syntax, partition the ordered context,
    ; reuse term-binder checks, then elaborate hypotheses and conclusion.
    (rule mm0-assertion-validate-v1
      (head
        (MM0AssertionValidateV1
          ?position ?name ?state ?binders ?arrow ?result))
      (body
        (MM0AssertionItemsFromAstV1
          ?position ?name ?binders ?itemsResult)
        (MM0AssertionAfterExplicitItemsV1
          ?position ?name ?state ?arrow ?binders ?itemsResult ?result)))
    (rule mm0-assertion-explicit-items-refused-v1
      (head
        (MM0AssertionAfterExplicitItemsV1
          ?position ?name ?state ?arrow ?binders
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-explicit-items-accepted-v1
      (head
        (MM0AssertionAfterExplicitItemsV1
          ?position ?name ?state ?arrow ?binders
          (MM0CheckAcceptedV1 ?explicitItems) ?result))
      (body
        (MM0AssertionArrowNormalizeV1
          ?position ?name ?arrow ?arrowResult)
        (MM0AssertionAfterArrowV1
          ?position ?name ?state ?explicitItems ?binders
          ?arrowResult ?result)))
    (rule mm0-assertion-arrow-refused-v1
      (head
        (MM0AssertionAfterArrowV1
          ?position ?name ?state ?explicitItems ?binders
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-arrow-accepted-v1
      (head
        (MM0AssertionAfterArrowV1
          ?position ?name ?state ?explicitItems ?binders
          (MM0CheckAcceptedV1
            (MM0AssertionArrowV1 ?arrowItems ?conclusion))
          ?result))
      (body
        (MM0ListAppendV1 ?explicitItems ?arrowItems ?items)
        (MM0AssertionPartitionTypedV1
          ?position ?name ?items ?partitionResult)
        (MM0AssertionAfterPartitionV1
          ?position ?name ?state ?conclusion ?partitionResult ?result)))
    (rule mm0-assertion-partition-refused-v1
      (head
        (MM0AssertionAfterPartitionV1
          ?position ?name ?state ?conclusion
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-partition-accepted-v1
      (head
        (MM0AssertionAfterPartitionV1
          ?position ?name
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?conclusion
          (MM0CheckAcceptedV1
            (MM0AssertionPartitionV1 ?typedGroups ?hypotheses))
          ?result))
      (body
        (MM0ValidateExplicitBindersV1
          ?position ?name ?sorts ?typedGroups nil nil ?binderResult)
        (MM0AssertionAfterBindersV1
          ?position ?name
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?conclusion ?hypotheses ?binderResult ?result)))
    (rule mm0-assertion-binders-refused-v1
      (head
        (MM0AssertionAfterBindersV1
          ?position ?name ?state ?conclusion ?hypotheses
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-binders-accepted-v1
      (head
        (MM0AssertionAfterBindersV1
          ?position ?name ?state ?conclusion ?hypotheses
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 ?context ?boundNames ?allNames))
          ?result))
      (body
        (MM0AssertionHypothesesV1
          ?position ?state ?context ?hypotheses ?name ?hypothesisResult)
        (MM0AssertionAfterHypothesesV1
          ?position ?name ?state ?context ?conclusion
          ?hypothesisResult ?result)))
    (rule mm0-assertion-hypotheses-refused-v1
      (head
        (MM0AssertionAfterHypothesesV1
          ?position ?name ?state ?context ?conclusion
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-hypotheses-accepted-v1
      (head
        (MM0AssertionAfterHypothesesV1
          ?position ?name ?state ?context ?conclusion
          (MM0CheckAcceptedV1 ?hypotheses) ?result))
      (body
        (MM0MathScalarsFromListV1 ?conclusion ?mathScalars)
        (MM0RuntimeProvableExpressionElaborateV1
          ?position ?state ?context ?mathScalars ?conclusionResult)
        (MM0AssertionAfterConclusionV1
          ?position ?context ?hypotheses ?conclusionResult ?result)))
    (rule mm0-assertion-conclusion-refused-v1
      (head
        (MM0AssertionAfterConclusionV1
          ?position ?context ?hypotheses
          (MM0ExpressionRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-assertion-conclusion-accepted-v1
      (head
        (MM0AssertionAfterConclusionV1
          ?position ?context ?hypotheses
          (MM0ExpressionAcceptedV1 ?conclusion ?sort)
          (MM0CheckAcceptedV1
            (MM0AssertionSignatureV1
              ?context ?hypotheses ?conclusion))))
      (body))

    ; Ordered transition and source-fold integration.
    (rule mm0-assertion-step-v1
      (head
        (MM0AssertionStepV1
          ?position ?statement
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?result))
      (body
        (MM0AssertionStatementV1
          ?statement ?kind ?name ?binders ?arrow)
        (MM0AssertionLookupV1 ?name ?declarations ?lookup)
        (MM0AssertionStepAfterLookupV1
          ?position ?kind ?name ?binders ?arrow
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          ?lookup ?result)))
    (rule mm0-assertion-step-redeclared-v1
      (head
        (MM0AssertionStepAfterLookupV1
          ?position ?kind ?name ?binders ?arrow ?before
          (MM0AssertionFoundV1 ?oldKind ?firstPosition)
          (MM0AssertionStepRefusedV1
            (MM0RefusedAssertionRedeclarationV1
              ?position ?name ?firstPosition)
            ?before
            (MM0RefusedAssertionRedeclarationV1
              ?position ?name ?firstPosition))))
      (body))
    (rule mm0-assertion-step-fresh-v1
      (head
        (MM0AssertionStepAfterLookupV1
          ?position ?kind ?name ?binders ?arrow ?before
          MM0AssertionMissingV1 ?result))
      (body
        (MM0AssertionValidateV1
          ?position ?name ?before ?binders ?arrow ?validation)
        (MM0AssertionStepAfterValidationV1
          ?position ?kind ?name ?before ?validation ?result)))
    (rule mm0-assertion-step-validation-refused-v1
      (head
        (MM0AssertionStepAfterValidationV1
          ?position ?kind ?name ?before
          (MM0CheckRefusedV1 ?reason)
          (MM0AssertionStepRefusedV1 ?reason ?before ?reason)))
      (body))
    (rule mm0-assertion-step-validation-accepted-v1
      (head
        (MM0AssertionStepAfterValidationV1
          ?position ?kind ?name
          (MM0SourceStateV1
            (MM0TextualEnvironmentV1 ?sorts ?declarations) ?notation)
          (MM0CheckAcceptedV1 ?signature)
          (MM0AssertionStepAcceptedV1
            (MM0SourceStateV1
              (MM0TextualEnvironmentV1 ?sorts ?afterDeclarations) ?notation)
            (MM0DeclaredAssertionV1
              ?position ?kind ?name ?signature))))
      (body
        (MM0AssertionEntriesAppendV1
          ?declarations
          (MM0AssertionEntryV1
            ?kind ?name ?signature ?position MM0RuntimeTextualSourceV1)
          ?afterDeclarations)))

    (rule mm0-source-statement-class-assertion-v1
      (head
        (MM0StatementClassV1
          (node mm0-assertion-statement ?payload)
          MM0AssertionStatementClassV1))
      (body))
    (rule mm0-source-step-assertion-accepted-v1
      (head
        (MM0SourceStepByClassV1
          ?position ?statement ?before MM0AssertionStatementClassV1
          (MM0SourceStepAcceptedV1 ?after ?historyEntry)))
      (body
        (MM0AssertionStepV1
          ?position ?statement ?before
          (MM0AssertionStepAcceptedV1 ?after ?historyEntry))))
    (rule mm0-source-step-assertion-refused-v1
      (head
        (MM0SourceStepByClassV1
          ?position ?statement ?before MM0AssertionStatementClassV1
          (MM0SourceStepRefusedV1 ?reason ?before ?historyEntry)))
      (body
        (MM0AssertionStepV1
          ?position ?statement ?before
          (MM0AssertionStepRefusedV1
            ?reason ?before ?historyEntry))))
  ))
