; Ordered MM0 sort-and-term declaration semantics.
;
; Concrete .mm0 files remain runtime inputs.  This presentation consumes the
; parser's semantic AST, composes the existing sort transition family, and
; validates term signatures without introducing a guest-shaped host runtime.

(gslt-presentation-v1 MM0TermEnvironmentV1
  (signature
    (operator node 2)
    (operator pair 2)
    (operator some 1)
    (operator cons 2)
    (operator different 2)
    (operator NatSuccV1 1)
    (operator MM0IdentifierScalarsV1 2)
    (operator MM0SortModifiersV1 4)
    (operator MM0SortEntryV1 3)
    (operator MM0SortEntriesConsV1 2)
    (operator MM0SortLookupV1 3)
    (operator MM0SortFoundV1 2)
    (operator MM0SortStepV1 6)

    (operator MM0TextualEnvironmentV1 2)
    (operator MM0TermEntryV1 4)
    (operator MM0DeclarationEntriesConsV1 2)
    (operator MM0DeclarationLookupV1 3)
    (operator MM0DeclarationFoundV1 2)
    (operator MM0DeclarationEntriesAppendV1 3)
    (operator MM0NameLookupV1 3)
    (operator MM0ListAppendV1 3)

    (operator MM0TypeV1 2)
    (operator MM0TypeAstV1 3)
    (operator MM0ResolvedTypeV1 2)
    (operator MM0CheckAcceptedV1 1)
    (operator MM0CheckRefusedV1 1)
    (operator MM0ResolveTypeV1 5)
    (operator MM0ResolveTypeAfterLookupV1 5)
    (operator MM0ValidateDependenciesV1 5)
    (operator MM0ValidateDependencyAfterLookupV1 7)
    (operator MM0PrependDependencyV1 3)
    (operator MM0ValidateBoundTypeV1 5)
    (operator MM0ValidateBoundTypeAfterResolveV1 4)
    (operator MM0ValidateRegularTypeV1 6)
    (operator MM0ValidateRegularTypeAfterResolveV1 5)
    (operator MM0RegularTypeAfterDependenciesV1 3)
    (operator MM0ValidateReturnTypeV1 6)
    (operator MM0ValidateReturnTypeAfterResolveV1 5)
    (operator MM0ReturnTypeAfterDependenciesV1 5)

    (operator MM0BinderV1 3)
    (operator MM0NamedBinderV1 1)
    (operator MM0BinderSequenceV1 3)
    (operator MM0PrependBinderV1 3)
    (operator MM0ValidateBoundNamesV1 7)
    (operator MM0ValidateBoundNameAfterLookupV1 9)
    (operator MM0ValidateRegularNamesV1 7)
    (operator MM0ValidateRegularNameAfterLookupV1 9)
    (operator MM0ValidateBinderGroupV1 7)
    (operator MM0ValidateBoundGroupAfterTypeV1 7)
    (operator MM0ValidateRegularGroupAfterTypeV1 7)
    (operator MM0ValidateExplicitBindersV1 7)
    (operator MM0ValidateExplicitAfterGroupV1 6)
    (operator MM0ConcatBinderSequenceV1 3)

    (operator MM0TermTailV1 2)
    (operator MM0ValidateArrowTypeV1 6)
    (operator MM0ValidateArrowAfterRegularV1 7)
    (operator MM0PrependArrowBinderV1 3)
    (operator MM0ReturnTailV1 2)
    (operator MM0TermSignatureV1 2)
    (operator MM0ValidateTermSignatureV1 6)
    (operator MM0ValidateTermAfterBindersV1 6)
    (operator MM0CombineTermSignatureV1 3)
    (operator MM0TermStatementV1 4)

    (operator MM0TermStepV1 4)
    (operator MM0TermStepAfterLookupV1 7)
    (operator MM0TermStepAfterSignatureV1 5)
    (operator MM0TextualStepV1 4)
    (operator MM0TextualSortOutcomeV1 5)
    (operator MM0TextualStepAcceptedV1 2)
    (operator MM0TextualStepRefusedV1 3)
    (operator MM0TextualFoldV1 6)
    (operator MM0TextualFoldAfterStepV1 6)
    (operator MM0TextualEnvironmentFromAstV1 4)
    (operator MM0TextualHistoryConsV1 2)
    (operator MM0DeclaredTermV1 3)

    (operator MM0RefusedUnknownSortV1 3)
    (operator MM0RefusedUnknownDependencyV1 3)
    (operator MM0RefusedBoundDependenciesV1 3)
    (operator MM0RefusedStrictBoundSortV1 3)
    (operator MM0RefusedDuplicateBinderV1 3)
    (operator MM0RefusedPureTermTargetV1 3)
    (operator MM0RefusedTermRedeclarationV1 3))
  (equations)
  (rewrites
    ; Small total finite lookups keep absence explicit.
    (rule mm0-name-lookup-missing-v1
      (head (MM0NameLookupV1 ?name nil MM0NameMissingV1))
      (body))
    (rule mm0-name-lookup-found-v1
      (head
        (MM0NameLookupV1
          ?name (cons ?name ?rest) MM0NameFoundV1))
      (body))
    (rule mm0-name-lookup-tail-v1
      (head
        (MM0NameLookupV1
          ?name (cons ?other ?rest) ?result))
      (body
        (different ?name ?other)
        (MM0NameLookupV1 ?name ?rest ?result)))

    (rule mm0-list-append-nil-v1
      (head (MM0ListAppendV1 nil ?right ?right))
      (body))
    (rule mm0-list-append-cons-v1
      (head
        (MM0ListAppendV1
          (cons ?head ?left) ?right (cons ?head ?after)))
      (body
        (MM0ListAppendV1 ?left ?right ?after)))

    (rule mm0-declaration-lookup-missing-v1
      (head
        (MM0DeclarationLookupV1
          ?name MM0DeclarationEntriesNilV1 MM0DeclarationMissingV1))
      (body))
    (rule mm0-declaration-lookup-found-v1
      (head
        (MM0DeclarationLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1 ?name ?signature ?firstPosition ?source)
            ?rest)
          (MM0DeclarationFoundV1 MM0TermDeclarationV1 ?firstPosition)))
      (body))
    (rule mm0-declaration-lookup-tail-v1
      (head
        (MM0DeclarationLookupV1
          ?name
          (MM0DeclarationEntriesConsV1
            (MM0TermEntryV1 ?other ?signature ?firstPosition ?source)
            ?rest)
          ?result))
      (body
        (different ?name ?other)
        (MM0DeclarationLookupV1 ?name ?rest ?result)))

    (rule mm0-declaration-entries-append-nil-v1
      (head
        (MM0DeclarationEntriesAppendV1
          MM0DeclarationEntriesNilV1 ?entry
          (MM0DeclarationEntriesConsV1
            ?entry MM0DeclarationEntriesNilV1)))
      (body))
    (rule mm0-declaration-entries-append-cons-v1
      (head
        (MM0DeclarationEntriesAppendV1
          (MM0DeclarationEntriesConsV1 ?head ?rest) ?entry
          (MM0DeclarationEntriesConsV1 ?head ?after)))
      (body
        (MM0DeclarationEntriesAppendV1 ?rest ?entry ?after)))

    ; Parser type nodes retain the result sort and ordered dependency names.
    (rule mm0-type-ast-v1
      (head
        (MM0TypeAstV1
          (node mm0-type (pair ?sortIdentifier ?dependencyIdentifiers))
          ?sortName ?dependencyIdentifiers))
      (body
        (MM0IdentifierScalarsV1 ?sortIdentifier ?sortName)))

    (rule mm0-resolve-type-v1
      (head
        (MM0ResolveTypeV1
          ?position ?termName ?sorts ?typeAst ?result))
      (body
        (MM0TypeAstV1 ?typeAst ?sortName ?dependencies)
        (MM0SortLookupV1 ?sortName ?sorts ?lookup)
        (MM0ResolveTypeAfterLookupV1
          ?position ?termName
          (MM0TypeV1 ?sortName ?dependencies) ?lookup ?result)))
    (rule mm0-resolve-type-found-v1
      (head
        (MM0ResolveTypeAfterLookupV1
          ?position ?termName ?type
          (MM0SortFoundV1 ?modifiers ?sortPosition)
          (MM0CheckAcceptedV1
            (MM0ResolvedTypeV1 ?type ?modifiers))))
      (body))
    (rule mm0-resolve-type-missing-v1
      (head
        (MM0ResolveTypeAfterLookupV1
          ?position ?termName (MM0TypeV1 ?sortName ?dependencies)
          MM0SortMissingV1
          (MM0CheckRefusedV1
            (MM0RefusedUnknownSortV1 ?position ?termName ?sortName))))
      (body))

    ; A dependency may mention only a previously declared bound variable.
    (rule mm0-validate-dependencies-nil-v1
      (head
        (MM0ValidateDependenciesV1
          ?position ?termName ?boundNames nil
          (MM0CheckAcceptedV1 nil)))
      (body))
    (rule mm0-validate-dependencies-cons-v1
      (head
        (MM0ValidateDependenciesV1
          ?position ?termName ?boundNames
          (cons ?identifier ?rest) ?result))
      (body
        (MM0IdentifierScalarsV1 ?identifier ?name)
        (MM0NameLookupV1 ?name ?boundNames ?lookup)
        (MM0ValidateDependencyAfterLookupV1
          ?position ?termName ?boundNames ?rest ?name ?lookup ?result)))
    (rule mm0-validate-dependency-found-v1
      (head
        (MM0ValidateDependencyAfterLookupV1
          ?position ?termName ?boundNames ?rest ?name MM0NameFoundV1
          ?result))
      (body
        (MM0ValidateDependenciesV1
          ?position ?termName ?boundNames ?rest ?restResult)
        (MM0PrependDependencyV1 ?name ?restResult ?result)))
    (rule mm0-validate-dependency-missing-v1
      (head
        (MM0ValidateDependencyAfterLookupV1
          ?position ?termName ?boundNames ?rest ?name MM0NameMissingV1
          (MM0CheckRefusedV1
            (MM0RefusedUnknownDependencyV1
              ?position ?termName ?name))))
      (body))
    (rule mm0-prepend-dependency-accepted-v1
      (head
        (MM0PrependDependencyV1
          ?name (MM0CheckAcceptedV1 ?rest)
          (MM0CheckAcceptedV1 (cons ?name ?rest))))
      (body))
    (rule mm0-prepend-dependency-refused-v1
      (head
        (MM0PrependDependencyV1
          ?name (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Bound variables have no dependencies and cannot inhabit strict sorts.
    (rule mm0-validate-bound-type-v1
      (head
        (MM0ValidateBoundTypeV1
          ?position ?termName ?sorts ?typeAst ?result))
      (body
        (MM0ResolveTypeV1
          ?position ?termName ?sorts ?typeAst ?resolved)
        (MM0ValidateBoundTypeAfterResolveV1
          ?position ?termName ?resolved ?result)))
    (rule mm0-validate-bound-type-resolve-refused-v1
      (head
        (MM0ValidateBoundTypeAfterResolveV1
          ?position ?termName (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-bound-type-dependencies-v1
      (head
        (MM0ValidateBoundTypeAfterResolveV1
          ?position ?termName
          (MM0CheckAcceptedV1
            (MM0ResolvedTypeV1
              (MM0TypeV1 ?sortName (cons ?dependency ?rest)) ?modifiers))
          (MM0CheckRefusedV1
            (MM0RefusedBoundDependenciesV1
              ?position ?termName (cons ?dependency ?rest)))))
      (body))
    (rule mm0-validate-bound-type-strict-v1
      (head
        (MM0ValidateBoundTypeAfterResolveV1
          ?position ?termName
          (MM0CheckAcceptedV1
            (MM0ResolvedTypeV1
              (MM0TypeV1 ?sortName nil)
              (MM0SortModifiersV1
                ?pure MM0ModifierPresentV1 ?provable ?free)))
          (MM0CheckRefusedV1
            (MM0RefusedStrictBoundSortV1
              ?position ?termName ?sortName))))
      (body))
    (rule mm0-validate-bound-type-accepted-v1
      (head
        (MM0ValidateBoundTypeAfterResolveV1
          ?position ?termName
          (MM0CheckAcceptedV1
            (MM0ResolvedTypeV1
              (MM0TypeV1 ?sortName nil)
              (MM0SortModifiersV1
                ?pure MM0ModifierAbsentV1 ?provable ?free)))
          (MM0CheckAcceptedV1 (MM0TypeV1 ?sortName nil))))
      (body))

    ; Regular arguments may depend on earlier bound variables.
    (rule mm0-validate-regular-type-v1
      (head
        (MM0ValidateRegularTypeV1
          ?position ?termName ?sorts ?boundNames ?typeAst ?result))
      (body
        (MM0ResolveTypeV1
          ?position ?termName ?sorts ?typeAst ?resolved)
        (MM0ValidateRegularTypeAfterResolveV1
          ?position ?termName ?boundNames ?resolved ?result)))
    (rule mm0-validate-regular-type-resolve-refused-v1
      (head
        (MM0ValidateRegularTypeAfterResolveV1
          ?position ?termName ?boundNames
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-regular-type-resolved-v1
      (head
        (MM0ValidateRegularTypeAfterResolveV1
          ?position ?termName ?boundNames
          (MM0CheckAcceptedV1
            (MM0ResolvedTypeV1
              (MM0TypeV1 ?sortName ?dependencies) ?modifiers))
          ?result))
      (body
        (MM0ValidateDependenciesV1
          ?position ?termName ?boundNames ?dependencies ?dependencyResult)
        (MM0RegularTypeAfterDependenciesV1
          (MM0TypeV1 ?sortName ?dependencies) ?dependencyResult ?result)))
    (rule mm0-regular-type-dependencies-accepted-v1
      (head
        (MM0RegularTypeAfterDependenciesV1
          (MM0TypeV1 ?sortName ?oldDependencies)
          (MM0CheckAcceptedV1 ?dependencies)
          (MM0CheckAcceptedV1
            (MM0TypeV1 ?sortName ?dependencies))))
      (body))
    (rule mm0-regular-type-dependencies-refused-v1
      (head
        (MM0RegularTypeAfterDependenciesV1
          ?type (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Return types obey the regular dependency rule and reject pure targets.
    (rule mm0-validate-return-type-v1
      (head
        (MM0ValidateReturnTypeV1
          ?position ?termName ?sorts ?boundNames ?typeAst ?result))
      (body
        (MM0ResolveTypeV1
          ?position ?termName ?sorts ?typeAst ?resolved)
        (MM0ValidateReturnTypeAfterResolveV1
          ?position ?termName ?boundNames ?resolved ?result)))
    (rule mm0-validate-return-type-resolve-refused-v1
      (head
        (MM0ValidateReturnTypeAfterResolveV1
          ?position ?termName ?boundNames
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-return-type-resolved-v1
      (head
        (MM0ValidateReturnTypeAfterResolveV1
          ?position ?termName ?boundNames
          (MM0CheckAcceptedV1
            (MM0ResolvedTypeV1
              (MM0TypeV1 ?sortName ?dependencies) ?modifiers))
          ?result))
      (body
        (MM0ValidateDependenciesV1
          ?position ?termName ?boundNames
          ?dependencies ?dependencyResult)
        (MM0ReturnTypeAfterDependenciesV1
          ?position ?termName
          (MM0ResolvedTypeV1
            (MM0TypeV1 ?sortName ?dependencies) ?modifiers)
          ?dependencyResult ?result)))
    (rule mm0-return-type-pure-v1
      (head
        (MM0ReturnTypeAfterDependenciesV1
          ?position ?termName
          (MM0ResolvedTypeV1
            (MM0TypeV1 ?sortName ?oldDependencies)
            (MM0SortModifiersV1
              MM0ModifierPresentV1 ?strict ?provable ?free))
          (MM0CheckAcceptedV1 ?dependencies)
          (MM0CheckRefusedV1
            (MM0RefusedPureTermTargetV1
              ?position ?termName ?sortName))))
      (body))
    (rule mm0-return-type-accepted-v1
      (head
        (MM0ReturnTypeAfterDependenciesV1
          ?position ?termName
          (MM0ResolvedTypeV1
            (MM0TypeV1 ?sortName ?oldDependencies)
            (MM0SortModifiersV1
              MM0ModifierAbsentV1 ?strict ?provable ?free))
          (MM0CheckAcceptedV1 ?dependencies)
          (MM0CheckAcceptedV1
            (MM0TypeV1 ?sortName ?dependencies))))
      (body))
    (rule mm0-return-type-dependencies-refused-v1
      (head
        (MM0ReturnTypeAfterDependenciesV1
          ?position ?termName ?resolved
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; Binder-list construction preserves source order while context lookups use
    ; the accumulated named-variable lists.
    (rule mm0-prepend-binder-accepted-v1
      (head
        (MM0PrependBinderV1
          ?binder
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 ?binders ?boundNames ?allNames))
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1
              (cons ?binder ?binders) ?boundNames ?allNames))))
      (body))
    (rule mm0-prepend-binder-refused-v1
      (head
        (MM0PrependBinderV1
          ?binder (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    (rule mm0-validate-bound-names-nil-v1
      (head
        (MM0ValidateBoundNamesV1
          ?position ?termName nil ?type ?boundNames ?allNames
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 nil ?boundNames ?allNames))))
      (body))
    (rule mm0-validate-bound-names-cons-v1
      (head
        (MM0ValidateBoundNamesV1
          ?position ?termName (cons ?identifier ?rest) ?type
          ?boundNames ?allNames ?result))
      (body
        (MM0IdentifierScalarsV1 ?identifier ?name)
        (MM0NameLookupV1 ?name ?allNames ?lookup)
        (MM0ValidateBoundNameAfterLookupV1
          ?position ?termName ?rest ?type ?boundNames ?allNames
          ?name ?lookup ?result)))
    (rule mm0-validate-bound-name-duplicate-v1
      (head
        (MM0ValidateBoundNameAfterLookupV1
          ?position ?termName ?rest ?type ?boundNames ?allNames
          ?name MM0NameFoundV1
          (MM0CheckRefusedV1
            (MM0RefusedDuplicateBinderV1
              ?position ?termName ?name))))
      (body))
    (rule mm0-validate-bound-name-fresh-v1
      (head
        (MM0ValidateBoundNameAfterLookupV1
          ?position ?termName ?rest ?type ?boundNames ?allNames
          ?name MM0NameMissingV1 ?result))
      (body
        (MM0ValidateBoundNamesV1
          ?position ?termName ?rest ?type
          (cons ?name ?boundNames) (cons ?name ?allNames) ?restResult)
        (MM0PrependBinderV1
          (MM0BinderV1
            MM0BoundBinderKindV1 (MM0NamedBinderV1 ?name) ?type)
          ?restResult ?result)))

    (rule mm0-validate-regular-names-nil-v1
      (head
        (MM0ValidateRegularNamesV1
          ?position ?termName nil ?type ?boundNames ?allNames
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 nil ?boundNames ?allNames))))
      (body))
    (rule mm0-validate-regular-name-anonymous-v1
      (head
        (MM0ValidateRegularNamesV1
          ?position ?termName
          (cons (node mm0-anonymous ?source) ?rest) ?type
          ?boundNames ?allNames ?result))
      (body
        (MM0ValidateRegularNamesV1
          ?position ?termName ?rest ?type ?boundNames ?allNames ?restResult)
        (MM0PrependBinderV1
          (MM0BinderV1
            MM0RegularBinderKindV1 MM0AnonymousBinderV1 ?type)
          ?restResult ?result)))
    (rule mm0-validate-regular-name-named-v1
      (head
        (MM0ValidateRegularNamesV1
          ?position ?termName
          (cons (node mm0-identifier ?identifierPayload) ?rest) ?type
          ?boundNames ?allNames ?result))
      (body
        (MM0IdentifierScalarsV1
          (node mm0-identifier ?identifierPayload) ?name)
        (MM0NameLookupV1 ?name ?allNames ?lookup)
        (MM0ValidateRegularNameAfterLookupV1
          ?position ?termName ?rest ?type ?boundNames ?allNames
          ?name ?lookup ?result)))
    (rule mm0-validate-regular-name-duplicate-v1
      (head
        (MM0ValidateRegularNameAfterLookupV1
          ?position ?termName ?rest ?type ?boundNames ?allNames
          ?name MM0NameFoundV1
          (MM0CheckRefusedV1
            (MM0RefusedDuplicateBinderV1
              ?position ?termName ?name))))
      (body))
    (rule mm0-validate-regular-name-fresh-v1
      (head
        (MM0ValidateRegularNameAfterLookupV1
          ?position ?termName ?rest ?type ?boundNames ?allNames
          ?name MM0NameMissingV1 ?result))
      (body
        (MM0ValidateRegularNamesV1
          ?position ?termName ?rest ?type ?boundNames
          (cons ?name ?allNames) ?restResult)
        (MM0PrependBinderV1
          (MM0BinderV1
            MM0RegularBinderKindV1 (MM0NamedBinderV1 ?name) ?type)
          ?restResult ?result)))

    (rule mm0-validate-bound-binder-group-v1
      (head
        (MM0ValidateBinderGroupV1
          ?position ?termName ?sorts
          (node mm0-type-binder-bound (pair ?names ?typeAst))
          ?boundNames ?allNames ?result))
      (body
        (MM0ValidateBoundTypeV1
          ?position ?termName ?sorts ?typeAst ?typeResult)
        (MM0ValidateBoundGroupAfterTypeV1
          ?position ?termName ?names ?boundNames ?allNames
          ?typeResult ?result)))
    (rule mm0-validate-regular-binder-group-v1
      (head
        (MM0ValidateBinderGroupV1
          ?position ?termName ?sorts
          (node mm0-type-binder-regular (pair ?names ?typeAst))
          ?boundNames ?allNames ?result))
      (body
        (MM0ValidateRegularTypeV1
          ?position ?termName ?sorts ?boundNames ?typeAst ?typeResult)
        (MM0ValidateRegularGroupAfterTypeV1
          ?position ?termName ?names ?boundNames ?allNames
          ?typeResult ?result)))
    (rule mm0-validate-bound-group-type-refused-v1
      (head
        (MM0ValidateBoundGroupAfterTypeV1
          ?position ?termName ?names ?boundNames ?allNames
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-bound-group-type-accepted-v1
      (head
        (MM0ValidateBoundGroupAfterTypeV1
          ?position ?termName ?names ?boundNames ?allNames
          (MM0CheckAcceptedV1 ?type) ?result))
      (body
        (MM0ValidateBoundNamesV1
          ?position ?termName ?names ?type ?boundNames ?allNames ?result)))
    (rule mm0-validate-regular-group-type-refused-v1
      (head
        (MM0ValidateRegularGroupAfterTypeV1
          ?position ?termName ?names ?boundNames ?allNames
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-regular-group-type-accepted-v1
      (head
        (MM0ValidateRegularGroupAfterTypeV1
          ?position ?termName ?names ?boundNames ?allNames
          (MM0CheckAcceptedV1 ?type) ?result))
      (body
        (MM0ValidateRegularNamesV1
          ?position ?termName ?names ?type ?boundNames ?allNames ?result)))

    (rule mm0-validate-explicit-binders-nil-v1
      (head
        (MM0ValidateExplicitBindersV1
          ?position ?termName ?sorts nil ?boundNames ?allNames
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 nil ?boundNames ?allNames))))
      (body))
    (rule mm0-validate-explicit-binders-cons-v1
      (head
        (MM0ValidateExplicitBindersV1
          ?position ?termName ?sorts (cons ?group ?rest)
          ?boundNames ?allNames ?result))
      (body
        (MM0ValidateBinderGroupV1
          ?position ?termName ?sorts ?group ?boundNames ?allNames
          ?groupResult)
        (MM0ValidateExplicitAfterGroupV1
          ?position ?termName ?sorts ?rest ?groupResult ?result)))
    (rule mm0-validate-explicit-group-refused-v1
      (head
        (MM0ValidateExplicitAfterGroupV1
          ?position ?termName ?sorts ?rest
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-explicit-group-accepted-v1
      (head
        (MM0ValidateExplicitAfterGroupV1
          ?position ?termName ?sorts ?rest
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1
              ?groupBinders ?boundNames ?allNames))
          ?result))
      (body
        (MM0ValidateExplicitBindersV1
          ?position ?termName ?sorts ?rest ?boundNames ?allNames
          ?restResult)
        (MM0ConcatBinderSequenceV1
          ?groupBinders ?restResult ?result)))
    (rule mm0-concat-binder-sequence-accepted-v1
      (head
        (MM0ConcatBinderSequenceV1
          ?left
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 ?right ?boundNames ?allNames))
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1 ?binders ?boundNames ?allNames))))
      (body
        (MM0ListAppendV1 ?left ?right ?binders)))
    (rule mm0-concat-binder-sequence-refused-v1
      (head
        (MM0ConcatBinderSequenceV1
          ?left (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    ; The recursive arrow syntax contributes anonymous regular arguments until
    ; its final type, which is checked as the return type.
    (rule mm0-validate-arrow-return-v1
      (head
        (MM0ValidateArrowTypeV1
          ?position ?termName ?sorts ?boundNames
          (node mm0-arrow-type (pair ?typeAst none)) ?result))
      (body
        (MM0ValidateReturnTypeV1
          ?position ?termName ?sorts ?boundNames ?typeAst ?returnResult)
        (MM0ReturnTailV1 ?returnResult ?result)))
    (rule mm0-validate-arrow-argument-v1
      (head
        (MM0ValidateArrowTypeV1
          ?position ?termName ?sorts ?boundNames
          (node mm0-arrow-type (pair ?typeAst (some ?tail))) ?result))
      (body
        (MM0ValidateRegularTypeV1
          ?position ?termName ?sorts ?boundNames ?typeAst ?typeResult)
        (MM0ValidateArrowAfterRegularV1
          ?position ?termName ?sorts ?boundNames ?tail
          ?typeResult ?result)))
    (rule mm0-validate-arrow-regular-refused-v1
      (head
        (MM0ValidateArrowAfterRegularV1
          ?position ?termName ?sorts ?boundNames ?tail
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-arrow-regular-accepted-v1
      (head
        (MM0ValidateArrowAfterRegularV1
          ?position ?termName ?sorts ?boundNames ?tail
          (MM0CheckAcceptedV1 ?type) ?result))
      (body
        (MM0ValidateArrowTypeV1
          ?position ?termName ?sorts ?boundNames ?tail ?tailResult)
        (MM0PrependArrowBinderV1 ?type ?tailResult ?result)))
    (rule mm0-prepend-arrow-binder-accepted-v1
      (head
        (MM0PrependArrowBinderV1
          ?type
          (MM0CheckAcceptedV1
            (MM0TermTailV1 ?binders ?returnType))
          (MM0CheckAcceptedV1
            (MM0TermTailV1
              (cons
                (MM0BinderV1
                  MM0RegularBinderKindV1 MM0AnonymousBinderV1 ?type)
                ?binders)
              ?returnType))))
      (body))
    (rule mm0-prepend-arrow-binder-refused-v1
      (head
        (MM0PrependArrowBinderV1
          ?type (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-return-tail-accepted-v1
      (head
        (MM0ReturnTailV1
          (MM0CheckAcceptedV1 ?returnType)
          (MM0CheckAcceptedV1
            (MM0TermTailV1 nil ?returnType))))
      (body))
    (rule mm0-return-tail-refused-v1
      (head
        (MM0ReturnTailV1
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    (rule mm0-validate-term-signature-v1
      (head
        (MM0ValidateTermSignatureV1
          ?position ?termName ?sorts ?explicitBinders ?arrowType ?result))
      (body
        (MM0ValidateExplicitBindersV1
          ?position ?termName ?sorts ?explicitBinders nil nil
          ?binderResult)
        (MM0ValidateTermAfterBindersV1
          ?position ?termName ?sorts ?arrowType ?binderResult ?result)))
    (rule mm0-validate-term-binders-refused-v1
      (head
        (MM0ValidateTermAfterBindersV1
          ?position ?termName ?sorts ?arrowType
          (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))
    (rule mm0-validate-term-binders-accepted-v1
      (head
        (MM0ValidateTermAfterBindersV1
          ?position ?termName ?sorts ?arrowType
          (MM0CheckAcceptedV1
            (MM0BinderSequenceV1
              ?explicitBinders ?boundNames ?allNames))
          ?result))
      (body
        (MM0ValidateArrowTypeV1
          ?position ?termName ?sorts ?boundNames ?arrowType ?tailResult)
        (MM0CombineTermSignatureV1
          ?explicitBinders ?tailResult ?result)))
    (rule mm0-combine-term-signature-accepted-v1
      (head
        (MM0CombineTermSignatureV1
          ?explicitBinders
          (MM0CheckAcceptedV1
            (MM0TermTailV1 ?arrowBinders ?returnType))
          (MM0CheckAcceptedV1
            (MM0TermSignatureV1 ?binders ?returnType))))
      (body
        (MM0ListAppendV1 ?explicitBinders ?arrowBinders ?binders)))
    (rule mm0-combine-term-signature-refused-v1
      (head
        (MM0CombineTermSignatureV1
          ?explicitBinders (MM0CheckRefusedV1 ?reason)
          (MM0CheckRefusedV1 ?reason)))
      (body))

    (rule mm0-term-statement-v1
      (head
        (MM0TermStatementV1
          (node mm0-term-statement
            (pair ?identifier (pair ?binders ?arrowType)))
          ?name ?binders ?arrowType))
      (body
        (MM0IdentifierScalarsV1 ?identifier ?name)))

    ; A term declaration first checks the shared declaration namespace, then
    ; validates and appends its normalized signature.
    (rule mm0-term-step-v1
      (head
        (MM0TermStepV1
          ?position ?statement
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          ?result))
      (body
        (MM0TermStatementV1
          ?statement ?name ?binders ?arrowType)
        (MM0DeclarationLookupV1
          ?name ?declarations ?lookup)
        (MM0TermStepAfterLookupV1
          ?position ?name ?binders ?arrowType
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          ?lookup ?result)))
    (rule mm0-term-step-redeclared-v1
      (head
        (MM0TermStepAfterLookupV1
          ?position ?name ?binders ?arrowType ?before
          (MM0DeclarationFoundV1 ?kind ?firstPosition)
          (MM0TextualStepRefusedV1
            (MM0RefusedTermRedeclarationV1
              ?position ?name ?firstPosition)
            ?before
            (MM0RefusedTermRedeclarationV1
              ?position ?name ?firstPosition))))
      (body))
    (rule mm0-term-step-fresh-v1
      (head
        (MM0TermStepAfterLookupV1
          ?position ?name ?binders ?arrowType
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          MM0DeclarationMissingV1 ?result))
      (body
        (MM0ValidateTermSignatureV1
          ?position ?name ?sorts ?binders ?arrowType ?signatureResult)
        (MM0TermStepAfterSignatureV1
          ?position ?name
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          ?signatureResult ?result)))
    (rule mm0-term-step-signature-refused-v1
      (head
        (MM0TermStepAfterSignatureV1
          ?position ?name ?before
          (MM0CheckRefusedV1 ?reason)
          (MM0TextualStepRefusedV1 ?reason ?before ?reason)))
      (body))
    (rule mm0-term-step-signature-accepted-v1
      (head
        (MM0TermStepAfterSignatureV1
          ?position ?name
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          (MM0CheckAcceptedV1 ?signature)
          (MM0TextualStepAcceptedV1
            (MM0TextualEnvironmentV1 ?sorts ?afterDeclarations)
            (MM0DeclaredTermV1 ?position ?name ?signature))))
      (body
        (MM0DeclarationEntriesAppendV1
          ?declarations
          (MM0TermEntryV1
            ?name ?signature ?position MM0RuntimeTextualSourceV1)
          ?afterDeclarations)))

    ; Sort and term transitions compose at one ordered source fold.
    (rule mm0-textual-step-sort-v1
      (head
        (MM0TextualStepV1
          ?position (node mm0-sort-statement ?payload)
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          ?result))
      (body
        (MM0SortStepV1
          ?position (node mm0-sort-statement ?payload)
          ?sorts ?outcome ?afterSorts ?historyEntry)
        (MM0TextualSortOutcomeV1
          ?outcome ?afterSorts ?historyEntry
          (MM0TextualEnvironmentV1 ?sorts ?declarations) ?result)))
    (rule mm0-textual-sort-outcome-accepted-v1
      (head
        (MM0TextualSortOutcomeV1
          MM0AcceptedV1 ?afterSorts ?historyEntry
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          (MM0TextualStepAcceptedV1
            (MM0TextualEnvironmentV1 ?afterSorts ?declarations)
            ?historyEntry)))
      (body))
    (rule mm0-textual-sort-outcome-refused-v1
      (head
        (MM0TextualSortOutcomeV1
          (MM0RefusedSortRedeclarationV1
            ?position ?name ?oldModifiers ?newModifiers)
          ?sorts ?historyEntry
          (MM0TextualEnvironmentV1 ?sorts ?declarations)
          (MM0TextualStepRefusedV1
            (MM0RefusedSortRedeclarationV1
              ?position ?name ?oldModifiers ?newModifiers)
            (MM0TextualEnvironmentV1 ?sorts ?declarations)
            ?historyEntry)))
      (body))
    (rule mm0-textual-step-term-v1
      (head
        (MM0TextualStepV1
          ?position (node mm0-term-statement ?payload) ?before ?result))
      (body
        (MM0TermStepV1
          ?position (node mm0-term-statement ?payload) ?before ?result)))

    (rule mm0-textual-fold-nil-v1
      (head
        (MM0TextualFoldV1
          nil ?position ?state MM0AcceptedV1 ?state
          MM0TextualHistoryNilV1))
      (body))
    (rule mm0-textual-fold-cons-v1
      (head
        (MM0TextualFoldV1
          (cons ?statement ?rest) ?position ?before
          ?outcome ?after ?history))
      (body
        (MM0TextualStepV1
          ?position ?statement ?before ?stepResult)
        (MM0TextualFoldAfterStepV1
          ?rest (NatSuccV1 ?position) ?stepResult
          ?outcome ?after ?history)))
    (rule mm0-textual-fold-after-accepted-v1
      (head
        (MM0TextualFoldAfterStepV1
          ?rest ?nextPosition
          (MM0TextualStepAcceptedV1 ?next ?historyEntry)
          ?outcome ?after
          (MM0TextualHistoryConsV1 ?historyEntry ?history)))
      (body
        (MM0TextualFoldV1
          ?rest ?nextPosition ?next ?outcome ?after ?history)))
    (rule mm0-textual-fold-after-refused-v1
      (head
        (MM0TextualFoldAfterStepV1
          ?rest ?nextPosition
          (MM0TextualStepRefusedV1 ?reason ?state ?historyEntry)
          ?reason ?state
          (MM0TextualHistoryConsV1
            ?historyEntry MM0TextualHistoryNilV1)))
      (body))

    ; The current entry is deliberately limited to sort and term statements.
    ; Future declaration families extend MM0TextualStepV1 without regenerating
    ; for any concrete .mm0 input.
    (rule mm0-textual-environment-from-ast-v1
      (head
        (MM0TextualEnvironmentFromAstV1
          (node mm0-file ?statements) ?outcome ?environment ?history))
      (body
        (MM0TextualFoldV1
          ?statements NatZeroV1
          (MM0TextualEnvironmentV1
            MM0SortEntriesNilV1 MM0DeclarationEntriesNilV1)
          ?outcome ?environment ?history)))))
