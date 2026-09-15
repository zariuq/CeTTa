; Finite graph and language analyses for declaration-valid plain BNF.
;
; Reachability is computed from the selected start definition.  Productivity
; and nullability are least fixed points over the finite, source-ordered
; definition list.  Unreachable and unproductive definitions are diagnostics;
; nullability is retained as derived information.  No parser backend is used.

(gslt-presentation-v1 PlainBnfGraphAnalysisV1
  (signature
    (operator different 2)
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2)
    (operator ground-integer-gap 2)
    (operator ground-integer-no-gap 2)

    (operator bnf-v1:text-cons 2)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator bnf-v1:lexical-points 1)
    (operator bnf-v1:lexical-except 1)
    (operator bnf-v1:scalars-cons 2)

    (operator BNFAnalyzeDocumentV1 4)
    (operator BNFNameLookupV1 3)
    (operator BNFAppendNameV1 3)
    (operator BNFAppendNamesV1 3)
    (operator BNFLexicalReferenceLookupV1 3)
    (operator BNFLexicalFoundV1 1)
    (operator BNFCollectExpressionReferencesV1 3)
    (operator BNFCollectAlternativesReferencesV1 3)
    (operator BNFCollectElementsReferencesV1 3)
    (operator BNFCollectElementReferencesV1 3)
    (operator BNFCollectReferenceAfterLexicalLookupV1 3)
    (operator BNFReachableLoopV1 5)
    (operator BNFReachableAfterVisitedV1 8)
    (operator BNFReachableQueueV1 6)
    (operator BNFReachableReverseV1 3)
    (operator BNFUnreachableDiagnosticsV1 3)
    (operator BNFUnreachableAfterLookupV1 5)

    (operator BNFExpressionProductiveV1 4)
    (operator BNFAlternativesProductiveV1 4)
    (operator BNFAlternativesProductiveAfterHeadV1 5)
    (operator BNFAlternativeProductiveV1 4)
    (operator BNFElementsProductiveV1 4)
    (operator BNFElementsProductiveAfterHeadV1 5)
    (operator BNFElementProductiveV1 4)
    (operator BNFProductiveReferenceAfterLookupV1 4)
    (operator BNFProductiveReferenceAfterLexicalLookupV1 2)
    (operator BNFLexicalMatcherInhabitedV1 2)
    (operator BNFExclusionTailInhabitedV1 3)
    (operator BNFExclusionGapInhabitedV1 4)
    (operator BNFScalarPairV1 2)
    (operator BNFProductivityPassV1 5)
    (operator BNFProductivityStepV1 7)
    (operator BNFProductivityAfterExpressionV1 7)
    (operator BNFProductivityClosureV1 4)
    (operator BNFProductivityClosureAfterPassV1 5)
    (operator BNFUnproductiveDiagnosticsV1 3)
    (operator BNFUnproductiveAfterLookupV1 5)

    (operator BNFExpressionNullableV1 4)
    (operator BNFAlternativesNullableV1 4)
    (operator BNFAlternativesNullableAfterHeadV1 5)
    (operator BNFAlternativeNullableV1 4)
    (operator BNFElementsNullableV1 4)
    (operator BNFElementsNullableAfterHeadV1 5)
    (operator BNFElementNullableV1 4)
    (operator BNFNullableReferenceAfterLookupV1 4)
    (operator BNFNullableReferenceAfterLexicalLookupV1 2)
    (operator BNFNullabilityPassV1 5)
    (operator BNFNullabilityStepV1 7)
    (operator BNFNullabilityAfterExpressionV1 7)
    (operator BNFNullabilityClosureV1 4)
    (operator BNFNullabilityClosureAfterPassV1 5)

    (operator BNFAppendDiagnosticsV1 3)
    (operator BNFFinishGraphAnalysisV1 8)

    (operator BNFDefinitionsConsV1 2)
    (operator BNFDefinitionV1 3)
    (operator BNFStartSomeV1 2)
    (operator BNFNamesConsV1 2)
    (operator BNFNameFoundV1 1)
    (operator BNFDiagnosticsConsV1 2)
    (operator BNFUnreachableDefinitionV1 2)
    (operator BNFUnproductiveDefinitionV1 2)
    (operator BNFGrammarAnalysisV1 3)
    (operator BNFSemanticValidationAcceptedV1 4)
    (operator BNFSemanticValidationRefusedV1 1))
  (equations)
  (rewrites
    ; Ordered finite-set lookup and append.  Lists retain discovery order.
    (rule bnf-name-lookup-missing-v1
      (head (BNFNameLookupV1
        ?name BNFNamesNilV1 BNFNameMissingV1))
      (body))
    (rule bnf-name-lookup-found-v1
      (head (BNFNameLookupV1
        ?name (BNFNamesConsV1 ?name ?tail) (BNFNameFoundV1 ?name)))
      (body))
    (rule bnf-name-lookup-tail-v1
      (head (BNFNameLookupV1
        ?name (BNFNamesConsV1 ?other ?tail) ?result))
      (body
        (different ?name ?other)
        (BNFNameLookupV1 ?name ?tail ?result)))

    (rule bnf-append-name-nil-v1
      (head (BNFAppendNameV1
        BNFNamesNilV1 ?name (BNFNamesConsV1 ?name BNFNamesNilV1)))
      (body))
    (rule bnf-append-name-cons-v1
      (head (BNFAppendNameV1
        (BNFNamesConsV1 ?head ?tail) ?name
        (BNFNamesConsV1 ?head ?after)))
      (body (BNFAppendNameV1 ?tail ?name ?after)))

    (rule bnf-append-names-nil-v1
      (head (BNFAppendNamesV1 BNFNamesNilV1 ?right ?right))
      (body))
    (rule bnf-append-names-cons-v1
      (head (BNFAppendNamesV1
        (BNFNamesConsV1 ?head ?tail) ?right
        (BNFNamesConsV1 ?head ?after)))
      (body (BNFAppendNamesV1 ?tail ?right ?after)))

    ; Collect grammar-rule references in source order without deduplication.
    ; Lexical references are leaves, so they are not graph edges.
    (rule bnf-collect-expression-references-v1
      (head (BNFCollectExpressionReferencesV1
        (bnf-v1:expression ?alternatives ?span) ?lexicals ?references))
      (body
        (BNFCollectAlternativesReferencesV1
          ?alternatives ?lexicals ?references)))
    (rule bnf-collect-alternatives-references-nil-v1
      (head (BNFCollectAlternativesReferencesV1
        (metta-nullary bnf-v1:alternatives-nil)
        ?lexicals BNFNamesNilV1))
      (body))
    (rule bnf-collect-alternatives-references-cons-v1
      (head (BNFCollectAlternativesReferencesV1
        (bnf-v1:alternatives-cons
          (bnf-v1:alternative ?elements ?span) ?tail)
        ?lexicals ?references))
      (body
        (BNFCollectElementsReferencesV1
          ?elements ?lexicals ?current)
        (BNFCollectAlternativesReferencesV1
          ?tail ?lexicals ?following)
        (BNFAppendNamesV1 ?current ?following ?references)))
    (rule bnf-collect-elements-references-nil-v1
      (head (BNFCollectElementsReferencesV1
        (metta-nullary bnf-v1:elements-nil)
        ?lexicals BNFNamesNilV1))
      (body))
    (rule bnf-collect-elements-references-cons-v1
      (head (BNFCollectElementsReferencesV1
        (bnf-v1:elements-cons ?element ?tail) ?lexicals ?references))
      (body
        (BNFCollectElementReferencesV1 ?element ?lexicals ?current)
        (BNFCollectElementsReferencesV1 ?tail ?lexicals ?following)
        (BNFAppendNamesV1 ?current ?following ?references)))
    (rule bnf-collect-literal-reference-v1
      (head (BNFCollectElementReferencesV1
        (bnf-v1:literal ?text ?span) ?lexicals BNFNamesNilV1))
      (body))
    (rule bnf-collect-nonterminal-reference-v1
      (head (BNFCollectElementReferencesV1
        (bnf-v1:reference ?name ?span) ?lexicals ?references))
      (body
        (BNFLexicalReferenceLookupV1 ?name ?lexicals ?lookup)
        (BNFCollectReferenceAfterLexicalLookupV1
          ?name ?lookup ?references)))
    (rule bnf-collect-grammar-reference-v1
      (head (BNFCollectReferenceAfterLexicalLookupV1
        ?name BNFLexicalMissingV1
        (BNFNamesConsV1 ?name BNFNamesNilV1)))
      (body))
    (rule bnf-collect-lexical-reference-v1
      (head (BNFCollectReferenceAfterLexicalLookupV1
        ?name (BNFLexicalFoundV1 ?declaration) BNFNamesNilV1))
      (body))

    ; The pending FIFO denotes front ++ reverse(back). Enqueue traverses
    ; only newly collected references, never the remaining pending queue.
    (rule bnf-reachable-queue-nil-v1
      (head (BNFReachableQueueV1
        BNFNamesNilV1 BNFNamesNilV1 ?definitions ?lexicals ?visited ?visited))
      (body))
    (rule bnf-reachable-queue-cons-v1
      (head (BNFReachableQueueV1
        (BNFNamesConsV1 ?name ?rest) ?back ?definitions ?lexicals
        ?visited ?reachable))
      (body
        (BNFNameLookupV1 ?name ?visited ?lookup)
        (BNFReachableAfterVisitedV1
          ?lookup ?name ?rest ?back ?definitions ?lexicals
          ?visited ?reachable)))
    (rule bnf-reachable-already-visited-v1
      (head (BNFReachableAfterVisitedV1
        (BNFNameFoundV1 ?name) ?name ?rest ?back ?definitions ?lexicals
        ?visited ?reachable))
      (body
        (BNFReachableQueueV1
          ?rest ?back ?definitions ?lexicals ?visited ?reachable)))
    (rule bnf-reachable-first-visit-v1
      (head (BNFReachableAfterVisitedV1
        BNFNameMissingV1 ?name ?rest ?back ?definitions ?lexicals
        ?visited ?reachable))
      (body
        (BNFDefinitionLookupV1
          ?name ?definitions (BNFDefinitionFoundV1 ?expression ?span))
        (BNFCollectExpressionReferencesV1
          ?expression ?lexicals ?references)
        (BNFReachableReverseV1 ?references ?back ?nextBack)
        (BNFAppendNameV1 ?visited ?name ?nextVisited)
        (BNFReachableQueueV1
          ?rest ?nextBack ?definitions ?lexicals ?nextVisited ?reachable)))

    (rule bnf-unreachable-diagnostics-nil-v1
      (head (BNFUnreachableDiagnosticsV1
        BNFDefinitionsNilV1 ?reachable BNFDiagnosticsNilV1))
      (body))
    (rule bnf-unreachable-diagnostics-cons-v1
      (head (BNFUnreachableDiagnosticsV1
        (BNFDefinitionsConsV1
          (BNFDefinitionV1 ?name ?expression ?span) ?tail)
        ?reachable ?diagnostics))
      (body
        (BNFNameLookupV1 ?name ?reachable ?lookup)
        (BNFUnreachableAfterLookupV1
          ?lookup (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?reachable ?diagnostics)))
    (rule bnf-reachable-definition-v1
      (head (BNFUnreachableAfterLookupV1
        (BNFNameFoundV1 ?name) ?definition ?tail ?reachable ?diagnostics))
      (body
        (BNFUnreachableDiagnosticsV1 ?tail ?reachable ?diagnostics)))
    (rule bnf-unreachable-definition-v1
      (head (BNFUnreachableAfterLookupV1
        BNFNameMissingV1 (BNFDefinitionV1 ?name ?expression ?span)
        ?tail ?reachable
        (BNFDiagnosticsConsV1
          (BNFUnreachableDefinitionV1 ?name ?span) ?diagnostics)))
      (body
        (BNFUnreachableDiagnosticsV1 ?tail ?reachable ?diagnostics)))

    ; Productivity: some alternative consists entirely of terminals or
    ; already-productive references.  Empty alternatives are productive.
    (rule bnf-expression-productive-v1
      (head (BNFExpressionProductiveV1
        (bnf-v1:expression ?alternatives ?span)
        ?productive ?lexicals ?result))
      (body
        (BNFAlternativesProductiveV1
          ?alternatives ?productive ?lexicals ?result)))
    (rule bnf-alternatives-productive-nil-v1
      (head (BNFAlternativesProductiveV1
        (metta-nullary bnf-v1:alternatives-nil)
        ?productive ?lexicals BNFNoV1))
      (body))
    (rule bnf-alternatives-productive-cons-v1
      (head (BNFAlternativesProductiveV1
        (bnf-v1:alternatives-cons ?alternative ?tail)
        ?productive ?lexicals ?result))
      (body
        (BNFAlternativeProductiveV1
          ?alternative ?productive ?lexicals ?headResult)
        (BNFAlternativesProductiveAfterHeadV1
          ?headResult ?tail ?productive ?lexicals ?result)))
    (rule bnf-alternatives-productive-head-yes-v1
      (head (BNFAlternativesProductiveAfterHeadV1
        BNFYesV1 ?tail ?productive ?lexicals BNFYesV1))
      (body))
    (rule bnf-alternatives-productive-head-no-v1
      (head (BNFAlternativesProductiveAfterHeadV1
        BNFNoV1 ?tail ?productive ?lexicals ?result))
      (body
        (BNFAlternativesProductiveV1
          ?tail ?productive ?lexicals ?result)))
    (rule bnf-alternative-productive-v1
      (head (BNFAlternativeProductiveV1
        (bnf-v1:alternative ?elements ?span)
        ?productive ?lexicals ?result))
      (body
        (BNFElementsProductiveV1
          ?elements ?productive ?lexicals ?result)))
    (rule bnf-elements-productive-nil-v1
      (head (BNFElementsProductiveV1
        (metta-nullary bnf-v1:elements-nil)
        ?productive ?lexicals BNFYesV1))
      (body))
    (rule bnf-elements-productive-cons-v1
      (head (BNFElementsProductiveV1
        (bnf-v1:elements-cons ?element ?tail)
        ?productive ?lexicals ?result))
      (body
        (BNFElementProductiveV1
          ?element ?productive ?lexicals ?headResult)
        (BNFElementsProductiveAfterHeadV1
          ?headResult ?tail ?productive ?lexicals ?result)))
    (rule bnf-elements-productive-head-no-v1
      (head (BNFElementsProductiveAfterHeadV1
        BNFNoV1 ?tail ?productive ?lexicals BNFNoV1))
      (body))
    (rule bnf-elements-productive-head-yes-v1
      (head (BNFElementsProductiveAfterHeadV1
        BNFYesV1 ?tail ?productive ?lexicals ?result))
      (body
        (BNFElementsProductiveV1
          ?tail ?productive ?lexicals ?result)))
    (rule bnf-literal-productive-v1
      (head (BNFElementProductiveV1
        (bnf-v1:literal ?text ?span)
        ?productive ?lexicals BNFYesV1))
      (body))
    (rule bnf-reference-productive-v1
      (head (BNFElementProductiveV1
        (bnf-v1:reference ?name ?span)
        ?productive ?lexicals ?result))
      (body
        (BNFNameLookupV1 ?name ?productive ?lookup)
        (BNFProductiveReferenceAfterLookupV1
          ?name ?lookup ?lexicals ?result)))
    (rule bnf-productive-reference-found-v1
      (head (BNFProductiveReferenceAfterLookupV1
        ?name (BNFNameFoundV1 ?name) ?lexicals BNFYesV1))
      (body))
    (rule bnf-productive-reference-missing-v1
      (head (BNFProductiveReferenceAfterLookupV1
        ?name BNFNameMissingV1 ?lexicals ?result))
      (body
        (BNFLexicalReferenceLookupV1 ?name ?lexicals ?lexicalLookup)
        (BNFProductiveReferenceAfterLexicalLookupV1
          ?lexicalLookup ?result)))
    (rule bnf-productive-lexical-reference-v1
      (head (BNFProductiveReferenceAfterLexicalLookupV1
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin)) ?result))
      (body (BNFLexicalMatcherInhabitedV1 ?matcher ?result)))
    (rule bnf-unproductive-unknown-reference-v1
      (head (BNFProductiveReferenceAfterLexicalLookupV1
        BNFLexicalMissingV1 BNFNoV1))
      (body))

    ; Called only after scalar validity and strict ordering are established.
    ; Exclusions cover Unicode exactly iff they start at zero, end at the
    ; largest scalar, and have no gaps except the surrogate interval.
    (rule bnf-points-empty-uninhabited-v1
      (head (BNFLexicalMatcherInhabitedV1
        (bnf-v1:lexical-points (metta-nullary bnf-v1:scalars-nil)) BNFNoV1))
      (body))
    (rule bnf-points-cons-inhabited-v1
      (head (BNFLexicalMatcherInhabitedV1
        (bnf-v1:lexical-points (bnf-v1:scalars-cons ?first ?tail)) BNFYesV1))
      (body))
    (rule bnf-exclusion-empty-inhabited-v1
      (head (BNFLexicalMatcherInhabitedV1
        (bnf-v1:lexical-except (metta-nullary bnf-v1:scalars-nil)) BNFYesV1))
      (body))
    (rule bnf-exclusion-zero-inhabited-v1
      (head (BNFLexicalMatcherInhabitedV1
        (bnf-v1:lexical-except (bnf-v1:scalars-cons 0 ?tail)) ?result))
      (body (BNFExclusionTailInhabitedV1 0 ?tail ?result)))
    (rule bnf-exclusion-missing-zero-inhabited-v1
      (head (BNFLexicalMatcherInhabitedV1
        (bnf-v1:lexical-except (bnf-v1:scalars-cons ?first ?tail)) BNFYesV1))
      (body (different ?first 0)))
    (rule bnf-exclusion-tail-missing-last-v1
      (head (BNFExclusionTailInhabitedV1
        ?last (metta-nullary bnf-v1:scalars-nil) BNFYesV1))
      (body (ground-integer-less ?last 1114111)))
    (rule bnf-exclusion-tail-complete-v1
      (head (BNFExclusionTailInhabitedV1
        ?last (metta-nullary bnf-v1:scalars-nil) BNFNoV1))
      (body (ground-integer-not-less ?last 1114111)))
    (rule bnf-exclusion-tail-cons-v1
      (head (BNFExclusionTailInhabitedV1
        ?left (bnf-v1:scalars-cons ?right ?tail) ?result))
      (body (BNFExclusionGapInhabitedV1 ?left ?right ?tail ?result)))
    (rule bnf-exclusion-surrogate-interval-v1
      (head (BNFExclusionGapInhabitedV1 55295 57344 ?tail ?result))
      (body (BNFExclusionTailInhabitedV1 57344 ?tail ?result)))
    (rule bnf-exclusion-gap-v1
      (head (BNFExclusionGapInhabitedV1 ?left ?right ?tail BNFYesV1))
      (body
        (different (BNFScalarPairV1 ?left ?right) (BNFScalarPairV1 55295 57344))
        (ground-integer-gap ?left ?right)))
    (rule bnf-exclusion-no-gap-v1
      (head (BNFExclusionGapInhabitedV1 ?left ?right ?tail ?result))
      (body
        (different (BNFScalarPairV1 ?left ?right) (BNFScalarPairV1 55295 57344))
        (ground-integer-no-gap ?left ?right)
        (BNFExclusionTailInhabitedV1 ?right ?tail ?result)))

    (rule bnf-productivity-pass-nil-v1
      (head (BNFProductivityPassV1
        BNFDefinitionsNilV1 ?before ?lexicals ?before BNFNoV1))
      (body))
    (rule bnf-productivity-pass-definition-v1
      (head (BNFProductivityPassV1
        (BNFDefinitionsConsV1
          (BNFDefinitionV1 ?name ?expression ?span) ?tail)
        ?before ?lexicals ?after ?changed))
      (body
        (BNFNameLookupV1 ?name ?before ?lookup)
        (BNFProductivityStepV1
          ?lookup (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-productivity-known-v1
      (head (BNFProductivityStepV1
        (BNFNameFoundV1 ?name) ?definition ?tail
        ?before ?lexicals ?after ?changed))
      (body
        (BNFProductivityPassV1
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-productivity-unknown-v1
      (head (BNFProductivityStepV1
        BNFNameMissingV1
        (BNFDefinitionV1 ?name ?expression ?span) ?tail
        ?before ?lexicals ?after ?changed))
      (body
        (BNFExpressionProductiveV1
          ?expression ?before ?lexicals ?result)
        (BNFProductivityAfterExpressionV1
          ?result (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-productivity-new-v1
      (head (BNFProductivityAfterExpressionV1
        BNFYesV1 (BNFDefinitionV1 ?name ?expression ?span)
        ?tail ?before ?lexicals ?after BNFYesV1))
      (body
        (BNFAppendNameV1 ?before ?name ?next)
        (BNFProductivityPassV1
          ?tail ?next ?lexicals ?after ?tailChanged)))
    (rule bnf-productivity-still-unknown-v1
      (head (BNFProductivityAfterExpressionV1
        BNFNoV1 ?definition ?tail ?before ?lexicals ?after ?changed))
      (body
        (BNFProductivityPassV1
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-productivity-closure-v1
      (head (BNFProductivityClosureV1
        ?definitions ?before ?lexicals ?productive))
      (body
        (BNFProductivityPassV1
          ?definitions ?before ?lexicals ?next ?changed)
        (BNFProductivityClosureAfterPassV1
          ?changed ?definitions ?next ?lexicals ?productive)))
    (rule bnf-productivity-closure-done-v1
      (head (BNFProductivityClosureAfterPassV1
        BNFNoV1 ?definitions ?next ?lexicals ?next))
      (body))
    (rule bnf-productivity-closure-continue-v1
      (head (BNFProductivityClosureAfterPassV1
        BNFYesV1 ?definitions ?next ?lexicals ?productive))
      (body
        (BNFProductivityClosureV1
          ?definitions ?next ?lexicals ?productive)))

    (rule bnf-unproductive-diagnostics-nil-v1
      (head (BNFUnproductiveDiagnosticsV1
        BNFDefinitionsNilV1 ?productive BNFDiagnosticsNilV1))
      (body))
    (rule bnf-unproductive-diagnostics-cons-v1
      (head (BNFUnproductiveDiagnosticsV1
        (BNFDefinitionsConsV1
          (BNFDefinitionV1 ?name ?expression ?span) ?tail)
        ?productive ?diagnostics))
      (body
        (BNFNameLookupV1 ?name ?productive ?lookup)
        (BNFUnproductiveAfterLookupV1
          ?lookup (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?productive ?diagnostics)))
    (rule bnf-productive-definition-v1
      (head (BNFUnproductiveAfterLookupV1
        (BNFNameFoundV1 ?name) ?definition ?tail ?productive ?diagnostics))
      (body
        (BNFUnproductiveDiagnosticsV1 ?tail ?productive ?diagnostics)))
    (rule bnf-unproductive-definition-v1
      (head (BNFUnproductiveAfterLookupV1
        BNFNameMissingV1 (BNFDefinitionV1 ?name ?expression ?span)
        ?tail ?productive
        (BNFDiagnosticsConsV1
          (BNFUnproductiveDefinitionV1 ?name ?span) ?diagnostics)))
      (body
        (BNFUnproductiveDiagnosticsV1 ?tail ?productive ?diagnostics)))

    ; Nullability is the analogous least fixed point, except that only empty
    ; terminals and nullable references satisfy an element.
    (rule bnf-expression-nullable-v1
      (head (BNFExpressionNullableV1
        (bnf-v1:expression ?alternatives ?span)
        ?nullable ?lexicals ?result))
      (body
        (BNFAlternativesNullableV1
          ?alternatives ?nullable ?lexicals ?result)))
    (rule bnf-alternatives-nullable-nil-v1
      (head (BNFAlternativesNullableV1
        (metta-nullary bnf-v1:alternatives-nil)
        ?nullable ?lexicals BNFNoV1))
      (body))
    (rule bnf-alternatives-nullable-cons-v1
      (head (BNFAlternativesNullableV1
        (bnf-v1:alternatives-cons ?alternative ?tail)
        ?nullable ?lexicals ?result))
      (body
        (BNFAlternativeNullableV1
          ?alternative ?nullable ?lexicals ?headResult)
        (BNFAlternativesNullableAfterHeadV1
          ?headResult ?tail ?nullable ?lexicals ?result)))
    (rule bnf-alternatives-nullable-head-yes-v1
      (head (BNFAlternativesNullableAfterHeadV1
        BNFYesV1 ?tail ?nullable ?lexicals BNFYesV1))
      (body))
    (rule bnf-alternatives-nullable-head-no-v1
      (head (BNFAlternativesNullableAfterHeadV1
        BNFNoV1 ?tail ?nullable ?lexicals ?result))
      (body
        (BNFAlternativesNullableV1
          ?tail ?nullable ?lexicals ?result)))
    (rule bnf-alternative-nullable-v1
      (head (BNFAlternativeNullableV1
        (bnf-v1:alternative ?elements ?span)
        ?nullable ?lexicals ?result))
      (body (BNFElementsNullableV1
        ?elements ?nullable ?lexicals ?result)))
    (rule bnf-elements-nullable-nil-v1
      (head (BNFElementsNullableV1
        (metta-nullary bnf-v1:elements-nil)
        ?nullable ?lexicals BNFYesV1))
      (body))
    (rule bnf-elements-nullable-cons-v1
      (head (BNFElementsNullableV1
        (bnf-v1:elements-cons ?element ?tail)
        ?nullable ?lexicals ?result))
      (body
        (BNFElementNullableV1
          ?element ?nullable ?lexicals ?headResult)
        (BNFElementsNullableAfterHeadV1
          ?headResult ?tail ?nullable ?lexicals ?result)))
    (rule bnf-elements-nullable-head-no-v1
      (head (BNFElementsNullableAfterHeadV1
        BNFNoV1 ?tail ?nullable ?lexicals BNFNoV1))
      (body))
    (rule bnf-elements-nullable-head-yes-v1
      (head (BNFElementsNullableAfterHeadV1
        BNFYesV1 ?tail ?nullable ?lexicals ?result))
      (body (BNFElementsNullableV1
        ?tail ?nullable ?lexicals ?result)))
    (rule bnf-empty-literal-nullable-v1
      (head (BNFElementNullableV1
        (bnf-v1:literal (metta-nullary bnf-v1:text-nil) ?span)
        ?nullable ?lexicals BNFYesV1))
      (body))
    (rule bnf-nonempty-literal-not-nullable-v1
      (head (BNFElementNullableV1
        (bnf-v1:literal (bnf-v1:text-cons ?head ?tail) ?span)
        ?nullable ?lexicals BNFNoV1))
      (body))
    (rule bnf-reference-nullable-v1
      (head (BNFElementNullableV1
        (bnf-v1:reference ?name ?span)
        ?nullable ?lexicals ?result))
      (body
        (BNFNameLookupV1 ?name ?nullable ?lookup)
        (BNFNullableReferenceAfterLookupV1
          ?name ?lookup ?lexicals ?result)))
    (rule bnf-nullable-reference-found-v1
      (head (BNFNullableReferenceAfterLookupV1
        ?name (BNFNameFoundV1 ?name) ?lexicals BNFYesV1))
      (body))
    (rule bnf-nullable-reference-missing-v1
      (head (BNFNullableReferenceAfterLookupV1
        ?name BNFNameMissingV1 ?lexicals ?result))
      (body
        (BNFLexicalReferenceLookupV1 ?name ?lexicals ?lexicalLookup)
        (BNFNullableReferenceAfterLexicalLookupV1
          ?lexicalLookup ?result)))
    (rule bnf-lexical-reference-not-nullable-v1
      (head (BNFNullableReferenceAfterLexicalLookupV1
        (BNFLexicalFoundV1 ?declaration) BNFNoV1))
      (body))
    (rule bnf-unknown-reference-not-nullable-v1
      (head (BNFNullableReferenceAfterLexicalLookupV1
        BNFLexicalMissingV1 BNFNoV1))
      (body))

    (rule bnf-nullability-pass-nil-v1
      (head (BNFNullabilityPassV1
        BNFDefinitionsNilV1 ?before ?lexicals ?before BNFNoV1))
      (body))
    (rule bnf-nullability-pass-definition-v1
      (head (BNFNullabilityPassV1
        (BNFDefinitionsConsV1
          (BNFDefinitionV1 ?name ?expression ?span) ?tail)
        ?before ?lexicals ?after ?changed))
      (body
        (BNFNameLookupV1 ?name ?before ?lookup)
        (BNFNullabilityStepV1
          ?lookup (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-nullability-known-v1
      (head (BNFNullabilityStepV1
        (BNFNameFoundV1 ?name) ?definition ?tail
        ?before ?lexicals ?after ?changed))
      (body
        (BNFNullabilityPassV1
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-nullability-unknown-v1
      (head (BNFNullabilityStepV1
        BNFNameMissingV1
        (BNFDefinitionV1 ?name ?expression ?span) ?tail
        ?before ?lexicals ?after ?changed))
      (body
        (BNFExpressionNullableV1
          ?expression ?before ?lexicals ?result)
        (BNFNullabilityAfterExpressionV1
          ?result (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-nullability-new-v1
      (head (BNFNullabilityAfterExpressionV1
        BNFYesV1 (BNFDefinitionV1 ?name ?expression ?span)
        ?tail ?before ?lexicals ?after BNFYesV1))
      (body
        (BNFAppendNameV1 ?before ?name ?next)
        (BNFNullabilityPassV1
          ?tail ?next ?lexicals ?after ?tailChanged)))
    (rule bnf-nullability-still-unknown-v1
      (head (BNFNullabilityAfterExpressionV1
        BNFNoV1 ?definition ?tail ?before ?lexicals ?after ?changed))
      (body
        (BNFNullabilityPassV1
          ?tail ?before ?lexicals ?after ?changed)))
    (rule bnf-nullability-closure-v1
      (head (BNFNullabilityClosureV1
        ?definitions ?before ?lexicals ?nullable))
      (body
        (BNFNullabilityPassV1
          ?definitions ?before ?lexicals ?next ?changed)
        (BNFNullabilityClosureAfterPassV1
          ?changed ?definitions ?next ?lexicals ?nullable)))
    (rule bnf-nullability-closure-done-v1
      (head (BNFNullabilityClosureAfterPassV1
        BNFNoV1 ?definitions ?next ?lexicals ?next))
      (body))
    (rule bnf-nullability-closure-continue-v1
      (head (BNFNullabilityClosureAfterPassV1
        BNFYesV1 ?definitions ?next ?lexicals ?nullable))
      (body
        (BNFNullabilityClosureV1
          ?definitions ?next ?lexicals ?nullable)))

    (rule bnf-finish-analysis-accepted-v1
      (head (BNFFinishGraphAnalysisV1
        ?start ?definitions ?lexicals ?reachable ?nullable ?productive
        BNFDiagnosticsNilV1
        (BNFSemanticValidationAcceptedV1
          ?start ?definitions ?lexicals
          (BNFGrammarAnalysisV1 ?reachable ?nullable ?productive))))
      (body))
    (rule bnf-finish-analysis-refused-v1
      (head (BNFFinishGraphAnalysisV1
        ?start ?definitions ?lexicals ?reachable ?nullable ?productive
        (BNFDiagnosticsConsV1 ?head ?tail)
        (BNFSemanticValidationRefusedV1
          (BNFDiagnosticsConsV1 ?head ?tail))))
      (body))

    (rule bnf-analyze-document-v1
      (head (BNFAnalyzeDocumentV1
        (BNFStartSomeV1 ?startName ?startSpan)
        ?definitions ?lexicals ?result))
      (body
        (BNFReachableLoopV1
          (BNFNamesConsV1 ?startName BNFNamesNilV1)
          ?definitions ?lexicals BNFNamesNilV1 ?reachable)
        (BNFUnreachableDiagnosticsV1
          ?definitions ?reachable ?unreachableDiagnostics)
        (BNFProductivityClosureV1
          ?definitions BNFNamesNilV1 ?lexicals ?productive)
        (BNFUnproductiveDiagnosticsV1
          ?definitions ?productive ?unproductiveDiagnostics)
        (BNFNullabilityClosureV1
          ?definitions BNFNamesNilV1 ?lexicals ?nullable)
        (BNFAppendDiagnosticsV1
          ?unreachableDiagnostics ?unproductiveDiagnostics ?diagnostics)
        (BNFFinishGraphAnalysisV1
          (BNFStartSomeV1 ?startName ?startSpan) ?definitions ?lexicals
          ?reachable ?nullable ?productive ?diagnostics ?result)))

    (rule bnf-reachable-loop-v1
      (head (BNFReachableLoopV1 ?work ?definitions ?lexicals ?visited ?result))
      (body (BNFReachableQueueV1 ?work BNFNamesNilV1
        ?definitions ?lexicals ?visited ?result)))
    (rule bnf-reachable-queue-rotate-v1
      (head (BNFReachableQueueV1 BNFNamesNilV1
        (BNFNamesConsV1 ?head ?tail) ?definitions ?lexicals ?visited ?result))
      (body (BNFReachableReverseV1 (BNFNamesConsV1 ?head ?tail) BNFNamesNilV1 ?front)
        (BNFReachableQueueV1 ?front BNFNamesNilV1 ?definitions ?lexicals ?visited ?result)))
    (rule bnf-reachable-reverse-nil-v1
      (head (BNFReachableReverseV1 BNFNamesNilV1 ?tail ?tail)) (body))
    (rule bnf-reachable-reverse-cons-v1
      (head (BNFReachableReverseV1 (BNFNamesConsV1 ?head ?rest) ?tail ?result))
      (body (BNFReachableReverseV1 ?rest (BNFNamesConsV1 ?head ?tail) ?result)))))
