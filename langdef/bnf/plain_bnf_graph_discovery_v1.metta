; Dependency-local discovery over the existing BNF grammar data.
; Auxiliary source positions, reverse references and pending queues do not
; replace the grammar. Results retain the reference discovery order.
; The queue uses the pairing-heap merge/combine algorithm. Structural
; bijective-binary positions avoid numeric provider assumptions.
(gslt-presentation-v1 PlainBnfGraphDiscoveryV1
  (signature
    (operator different 2)
    (operator bnf-v1:text-cons 2)
    (operator BNFDiscoveryRankOneV1 1)
    (operator BNFDiscoveryRankTwoV1 1)
    (operator BNFDiscoveryRankNextV1 2)
    (operator BNFDiscoveryRankCompareV1 3)
    (operator BNFDiscoveryRankTieV1 3)
    (operator BNFDiscoveryDefinitionV1 4)
    (operator BNFDiscoveryDefinitionsConsV1 2)
    (operator BNFDiscoveryRankDefinitionsV1 3)
    (operator BNFDiscoveryHeapV1 3)
    (operator BNFDiscoveryHeapMergeV1 3)
    (operator BNFDiscoveryHeapMergeAfterCompareV1 4)
    (operator BNFDiscoveryHeapCombineV1 2)
    (operator BNFGraphTrieV1 2)
    (operator BNFGraphTrieEdgesConsV1 3)
    (operator BNFGraphTrieLookupV1 3)
    (operator BNFGraphTrieInsertFirstV1 4)
    (operator BNFGraphTriePutV1 4)
    (operator BNFGraphTrieEdgePutV1 5)
    (operator BNFIndexFoundV1 1)
    (operator BNFDiscoveryKnownV1 2)
    (operator BNFNameLookupV1 3)
    (operator BNFIndexedNameLookupResultV1 2)
    (operator BNFDiscoveryReverseNamesV1 3)
    (operator BNFIndexedDefinitionsV1 1)
    (operator BNFDiscoveryDefinitionIndexV1 3)
    (operator BNFDiscoveryCollectDefinitionsV1 4)
    (operator BNFDiscoveryCollectLoopV1 6)
    (operator BNFDiscoveryCollectAfterLookupV1 8)
    (operator BNFDiscoveryReverseDefinitionsV1 3)
    (operator BNFDuplicateDefinitionV1 3)
    (operator BNFDiagnosticsConsV1 2)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator BNFDefinitionLookupV1 3)
    (operator BNFDefinitionFoundV1 2)
    (operator BNFDiscoveryDefinitionResultV1 2)
    (operator BNFDiscoveryReverseIndexV1 4)
    (operator BNFDiscoveryAddReferencesV1 4)
    (operator BNFDiscoveryDependentsV1 2)
    (operator BNFDiscoveryReadyV1 5)
    (operator BNFDiscoveryQueuesV1 3)
    (operator BNFDiscoveryAfterPositionV1 1)
    (operator BNFDiscoveryScheduleV1 4)
    (operator BNFDiscoveryScheduleAtV1 4)
    (operator BNFDiscoveryScheduleCurrentV1 3)
    (operator BNFDiscoveryScheduleNextV1 3)
    (operator BNFDiscoveryWakeV1 7)
    (operator BNFDiscoveryAfterScheduledV1 9)
    (operator BNFDiscoveryAfterReadyV1 9)
    (operator BNFDiscoveryRunV1 6)
    (operator BNFDiscoveryClosureV1 5)
    (operator BNFAnalyzeDocumentWorklistV1 4)
    (operator BNFValidateGrammarDiscoveryV1 2)
    (operator bnf-v1:grammar-input 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:grammar-authority 2)
    (operator bnf-v1:start 1)
    (operator bnf-v1:lexical-environment 1)
    (operator BNFValidateLexicalsV1 3)
    (operator BNFScanEntryReferencesV1 4)
    (operator BNFStartAfterLookupV1 4)
    (operator BNFFinishValidationV1 6)
    (operator BNFDefinitionsConsV1 2)
    (operator BNFDefinitionV1 3)
    (operator BNFNamesConsV1 2)
    (operator BNFStartSomeV1 2)
    (operator BNFCollectExpressionReferencesV1 3)
    (operator BNFExpressionProductiveV1 4)
    (operator BNFExpressionNullableV1 4)
    (operator BNFAppendNameV1 3)
    (operator BNFReachableLoopV1 5)
    (operator BNFUnreachableDiagnosticsV1 3)
    (operator BNFUnproductiveDiagnosticsV1 3)
    (operator BNFIndexedNamesObserveV1 2)
    (operator BNFAppendDiagnosticsV1 3)
    (operator BNFFinishGraphAnalysisV1 8))
  (equations)
  (rewrites
    ; Internal discovery histories are accumulated backwards and reversed
    ; exactly once at observation. Membership still consults the trie.
    (rule bnf-discovery-known-lookup-v1
      (head (BNFNameLookupV1 ?name (BNFDiscoveryKnownV1 ?index ?reversed) ?result))
      (body (BNFGraphTrieLookupV1 ?name ?index ?lookup)
        (BNFIndexedNameLookupResultV1 ?lookup ?result)))
    (rule bnf-discovery-known-append-v1
      (head (BNFAppendNameV1 (BNFDiscoveryKnownV1 ?index ?reversed) ?name
        (BNFDiscoveryKnownV1 ?next (BNFNamesConsV1 ?name ?reversed))))
      (body (BNFGraphTrieInsertFirstV1 ?name ?name ?index ?next)))
    (rule bnf-discovery-known-observe-v1
      (head (BNFIndexedNamesObserveV1 (BNFDiscoveryKnownV1 ?index ?reversed) ?result))
      (body (BNFDiscoveryReverseNamesV1 ?reversed BNFNamesNilV1 ?result)))
    (rule bnf-discovery-reverse-names-nil-v1
      (head (BNFDiscoveryReverseNamesV1 BNFNamesNilV1 ?result ?result)) (body))
    (rule bnf-discovery-reverse-names-cons-v1
      (head (BNFDiscoveryReverseNamesV1 (BNFNamesConsV1 ?head ?tail) ?before ?after))
      (body (BNFDiscoveryReverseNamesV1 ?tail (BNFNamesConsV1 ?head ?before) ?after)))
    ; Rank = 0 | 2*rank+1 | 2*rank+2. Every finite term is canonical.
    (rule bnf-discovery-rank-next-zero-v1
      (head (BNFDiscoveryRankNextV1 BNFDiscoveryRankZeroV1
        (BNFDiscoveryRankOneV1 BNFDiscoveryRankZeroV1))) (body))
    (rule bnf-discovery-rank-next-one-v1
      (head (BNFDiscoveryRankNextV1 (BNFDiscoveryRankOneV1 ?rank)
        (BNFDiscoveryRankTwoV1 ?rank))) (body))
    (rule bnf-discovery-rank-next-two-v1
      (head (BNFDiscoveryRankNextV1 (BNFDiscoveryRankTwoV1 ?rank)
        (BNFDiscoveryRankOneV1 ?next)))
      (body (BNFDiscoveryRankNextV1 ?rank ?next)))
    (rule bnf-discovery-rank-compare-zero-v1
      (head (BNFDiscoveryRankCompareV1 BNFDiscoveryRankZeroV1 BNFDiscoveryRankZeroV1 BNFDiscoveryEqualV1)) (body))
    (rule bnf-discovery-rank-zero-one-v1
      (head (BNFDiscoveryRankCompareV1 BNFDiscoveryRankZeroV1 (BNFDiscoveryRankOneV1 ?rank) BNFDiscoveryLessV1)) (body))
    (rule bnf-discovery-rank-zero-two-v1
      (head (BNFDiscoveryRankCompareV1 BNFDiscoveryRankZeroV1 (BNFDiscoveryRankTwoV1 ?rank) BNFDiscoveryLessV1)) (body))
    (rule bnf-discovery-rank-one-zero-v1
      (head (BNFDiscoveryRankCompareV1 (BNFDiscoveryRankOneV1 ?rank) BNFDiscoveryRankZeroV1 BNFDiscoveryGreaterV1)) (body))
    (rule bnf-discovery-rank-two-zero-v1
      (head (BNFDiscoveryRankCompareV1 (BNFDiscoveryRankTwoV1 ?rank) BNFDiscoveryRankZeroV1 BNFDiscoveryGreaterV1)) (body))
    (rule bnf-discovery-rank-one-one-v1
      (head (BNFDiscoveryRankCompareV1 (BNFDiscoveryRankOneV1 ?left) (BNFDiscoveryRankOneV1 ?right) ?result))
      (body (BNFDiscoveryRankCompareV1 ?left ?right ?result)))
    (rule bnf-discovery-rank-two-two-v1
      (head (BNFDiscoveryRankCompareV1 (BNFDiscoveryRankTwoV1 ?left) (BNFDiscoveryRankTwoV1 ?right) ?result))
      (body (BNFDiscoveryRankCompareV1 ?left ?right ?result)))
    (rule bnf-discovery-rank-one-two-v1
      (head (BNFDiscoveryRankCompareV1 (BNFDiscoveryRankOneV1 ?left) (BNFDiscoveryRankTwoV1 ?right) ?result))
      (body (BNFDiscoveryRankCompareV1 ?left ?right ?high)
        (BNFDiscoveryRankTieV1 ?high BNFDiscoveryLessV1 ?result)))
    (rule bnf-discovery-rank-two-one-v1
      (head (BNFDiscoveryRankCompareV1 (BNFDiscoveryRankTwoV1 ?left) (BNFDiscoveryRankOneV1 ?right) ?result))
      (body (BNFDiscoveryRankCompareV1 ?left ?right ?high)
        (BNFDiscoveryRankTieV1 ?high BNFDiscoveryGreaterV1 ?result)))
    (rule bnf-discovery-rank-tie-less-v1
      (head (BNFDiscoveryRankTieV1 BNFDiscoveryLessV1 ?tie BNFDiscoveryLessV1)) (body))
    (rule bnf-discovery-rank-tie-equal-v1
      (head (BNFDiscoveryRankTieV1 BNFDiscoveryEqualV1 ?tie ?tie)) (body))
    (rule bnf-discovery-rank-tie-greater-v1
      (head (BNFDiscoveryRankTieV1 BNFDiscoveryGreaterV1 ?tie BNFDiscoveryGreaterV1)) (body))

    (rule bnf-discovery-rank-definitions-nil-v1
      (head (BNFDiscoveryRankDefinitionsV1 BNFDefinitionsNilV1 ?rank BNFDiscoveryDefinitionsNilV1)) (body))
    (rule bnf-discovery-rank-definitions-cons-v1
      (head (BNFDiscoveryRankDefinitionsV1
        (BNFDefinitionsConsV1 (BNFDefinitionV1 ?name ?expression ?span) ?tail) ?rank
        (BNFDiscoveryDefinitionsConsV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?after)))
      (body (BNFDiscoveryRankNextV1 ?rank ?next)
        (BNFDiscoveryRankDefinitionsV1 ?tail ?next ?after)))

    ; As in pairing-heap merge, root siblings are ignored. Queue roots have
    ; no siblings; combine explicitly consumes every child sibling once.
    (rule bnf-discovery-heap-merge-nil-v1
      (head (BNFDiscoveryHeapMergeV1 BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1)) (body))
    (rule bnf-discovery-heap-merge-left-nil-v1
      (head (BNFDiscoveryHeapMergeV1 BNFDiscoveryHeapNilV1
        (BNFDiscoveryHeapV1 ?node ?children ?siblings)
        (BNFDiscoveryHeapV1 ?node ?children BNFDiscoveryHeapNilV1))) (body))
    (rule bnf-discovery-heap-merge-right-nil-v1
      (head (BNFDiscoveryHeapMergeV1 (BNFDiscoveryHeapV1 ?node ?children ?siblings)
        BNFDiscoveryHeapNilV1 (BNFDiscoveryHeapV1 ?node ?children BNFDiscoveryHeapNilV1))) (body))
    (rule bnf-discovery-heap-merge-nodes-v1
      (head (BNFDiscoveryHeapMergeV1
        (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?lr ?ln ?le ?ls) ?lc ?lrest)
        (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?rr ?rn ?re ?rs) ?rc ?rrest) ?result))
      (body (BNFDiscoveryRankCompareV1 ?lr ?rr ?order)
        (BNFDiscoveryHeapMergeAfterCompareV1 ?order
          (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?lr ?ln ?le ?ls) ?lc ?lrest)
          (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?rr ?rn ?re ?rs) ?rc ?rrest) ?result)))
    (rule bnf-discovery-heap-merge-less-v1
      (head (BNFDiscoveryHeapMergeAfterCompareV1 BNFDiscoveryLessV1
        (BNFDiscoveryHeapV1 ?left ?lc ?ls) (BNFDiscoveryHeapV1 ?right ?rc ?rs)
        (BNFDiscoveryHeapV1 ?left (BNFDiscoveryHeapV1 ?right ?rc ?lc) BNFDiscoveryHeapNilV1))) (body))
    (rule bnf-discovery-heap-merge-equal-v1
      (head (BNFDiscoveryHeapMergeAfterCompareV1 BNFDiscoveryEqualV1
        (BNFDiscoveryHeapV1 ?left ?lc ?ls) (BNFDiscoveryHeapV1 ?right ?rc ?rs)
        (BNFDiscoveryHeapV1 ?left (BNFDiscoveryHeapV1 ?right ?rc ?lc) BNFDiscoveryHeapNilV1))) (body))
    (rule bnf-discovery-heap-merge-greater-v1
      (head (BNFDiscoveryHeapMergeAfterCompareV1 BNFDiscoveryGreaterV1
        (BNFDiscoveryHeapV1 ?left ?lc ?ls) (BNFDiscoveryHeapV1 ?right ?rc ?rs)
        (BNFDiscoveryHeapV1 ?right (BNFDiscoveryHeapV1 ?left ?lc ?rc) BNFDiscoveryHeapNilV1))) (body))
    (rule bnf-discovery-heap-combine-nil-v1
      (head (BNFDiscoveryHeapCombineV1 BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1)) (body))
    (rule bnf-discovery-heap-combine-one-v1
      (head (BNFDiscoveryHeapCombineV1 (BNFDiscoveryHeapV1 ?node ?children BNFDiscoveryHeapNilV1)
        (BNFDiscoveryHeapV1 ?node ?children BNFDiscoveryHeapNilV1))) (body))
    (rule bnf-discovery-heap-combine-pair-v1
      (head (BNFDiscoveryHeapCombineV1
        (BNFDiscoveryHeapV1 ?left ?lc (BNFDiscoveryHeapV1 ?right ?rc ?rest)) ?result))
      (body (BNFDiscoveryHeapMergeV1
          (BNFDiscoveryHeapV1 ?left ?lc BNFDiscoveryHeapNilV1)
          (BNFDiscoveryHeapV1 ?right ?rc BNFDiscoveryHeapNilV1) ?pair)
        (BNFDiscoveryHeapCombineV1 ?rest ?following)
        (BNFDiscoveryHeapMergeV1 ?pair ?following ?result)))

    ; Updating a reverse-reference bucket uses the same sparse trie carrier.
    (rule bnf-discovery-trie-put-empty-v1
      (head (BNFGraphTriePutV1 ?key ?value BNFGraphTrieEmptyV1 ?result))
      (body (BNFGraphTriePutV1 ?key ?value
        (BNFGraphTrieV1 BNFIndexMissingV1 BNFGraphTrieEdgesNilV1) ?result)))
    (rule bnf-discovery-trie-put-value-v1
      (head (BNFGraphTriePutV1 (metta-nullary bnf-v1:text-nil) ?value
        (BNFGraphTrieV1 ?old ?edges) (BNFGraphTrieV1 (BNFIndexFoundV1 ?value) ?edges))) (body))
    (rule bnf-discovery-trie-put-cons-v1
      (head (BNFGraphTriePutV1 (bnf-v1:text-cons ?head ?tail) ?value
        (BNFGraphTrieV1 ?old ?edges) (BNFGraphTrieV1 ?old ?next)))
      (body (BNFGraphTrieEdgePutV1 ?head ?tail ?value ?edges ?next)))
    (rule bnf-discovery-trie-edge-put-nil-v1
      (head (BNFGraphTrieEdgePutV1 ?head ?tail ?value BNFGraphTrieEdgesNilV1
        (BNFGraphTrieEdgesConsV1 ?head ?child BNFGraphTrieEdgesNilV1)))
      (body (BNFGraphTriePutV1 ?tail ?value BNFGraphTrieEmptyV1 ?child)))
    (rule bnf-discovery-trie-edge-put-found-v1
      (head (BNFGraphTrieEdgePutV1 ?head ?tail ?value
        (BNFGraphTrieEdgesConsV1 ?head ?child ?rest) (BNFGraphTrieEdgesConsV1 ?head ?next ?rest)))
      (body (BNFGraphTriePutV1 ?tail ?value ?child ?next)))
    (rule bnf-discovery-trie-edge-put-next-v1
      (head (BNFGraphTrieEdgePutV1 ?head ?tail ?value
        (BNFGraphTrieEdgesConsV1 ?other ?child ?rest) (BNFGraphTrieEdgesConsV1 ?other ?child ?next)))
      (body (different ?head ?other) (BNFGraphTrieEdgePutV1 ?head ?tail ?value ?rest ?next)))

    (rule bnf-discovery-definition-index-nil-v1
      (head (BNFDiscoveryDefinitionIndexV1 BNFDiscoveryDefinitionsNilV1 ?index ?index)) (body))
    (rule bnf-discovery-definition-index-cons-v1
      (head (BNFDiscoveryDefinitionIndexV1
        (BNFDiscoveryDefinitionsConsV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?tail) ?index ?result))
      (body (BNFGraphTrieInsertFirstV1 ?name (BNFDefinitionFoundV1 ?expression ?span) ?index ?next)
        (BNFDiscoveryDefinitionIndexV1 ?tail ?next ?result)))
    ; The declaration collector retains the first complete definition and
    ; every duplicate diagnostic occurrence. Only its private accumulator is
    ; reversed; the returned definition list remains in source order.
    (rule bnf-discovery-collect-definitions-v1
      (head (BNFDiscoveryCollectDefinitionsV1 ?entries ?definitions ?diagnostics ?index))
      (body (BNFDiscoveryCollectLoopV1 ?entries BNFGraphTrieEmptyV1 BNFDefinitionsNilV1
        ?definitions ?diagnostics ?index)))
    (rule bnf-discovery-collect-nil-v1
      (head (BNFDiscoveryCollectLoopV1 (metta-nullary bnf-v1:entries-nil)
        ?index ?reversed ?definitions BNFDiagnosticsNilV1 ?index))
      (body (BNFDiscoveryReverseDefinitionsV1 ?reversed BNFDefinitionsNilV1 ?definitions)))
    (rule bnf-discovery-collect-comment-v1
      (head (BNFDiscoveryCollectLoopV1 (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail)
        ?index ?reversed ?definitions ?diagnostics ?finalIndex))
      (body (BNFDiscoveryCollectLoopV1 ?tail ?index ?reversed ?definitions ?diagnostics ?finalIndex)))
    (rule bnf-discovery-collect-blank-v1
      (head (BNFDiscoveryCollectLoopV1 (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail)
        ?index ?reversed ?definitions ?diagnostics ?finalIndex))
      (body (BNFDiscoveryCollectLoopV1 ?tail ?index ?reversed ?definitions ?diagnostics ?finalIndex)))
    (rule bnf-discovery-collect-rule-v1
      (head (BNFDiscoveryCollectLoopV1 (bnf-v1:entries-cons (bnf-v1:rule ?name ?expression ?span) ?tail)
        ?index ?reversed ?definitions ?diagnostics ?finalIndex))
      (body (BNFGraphTrieLookupV1 ?name ?index ?lookup)
        (BNFDiscoveryCollectAfterLookupV1 ?lookup (BNFDefinitionV1 ?name ?expression ?span)
          ?tail ?index ?reversed ?definitions ?diagnostics ?finalIndex)))
    (rule bnf-discovery-collect-fresh-v1
      (head (BNFDiscoveryCollectAfterLookupV1 BNFIndexMissingV1 (BNFDefinitionV1 ?name ?expression ?span)
        ?tail ?index ?reversed ?definitions ?diagnostics ?finalIndex))
      (body (BNFGraphTrieInsertFirstV1 ?name (BNFDefinitionFoundV1 ?expression ?span) ?index ?next)
        (BNFDiscoveryCollectLoopV1 ?tail ?next
          (BNFDefinitionsConsV1 (BNFDefinitionV1 ?name ?expression ?span) ?reversed)
          ?definitions ?diagnostics ?finalIndex)))
    (rule bnf-discovery-collect-duplicate-v1
      (head (BNFDiscoveryCollectAfterLookupV1
        (BNFIndexFoundV1 (BNFDefinitionFoundV1 ?firstExpression ?firstSpan))
        (BNFDefinitionV1 ?name ?expression ?span) ?tail ?index ?reversed ?definitions
        (BNFDiagnosticsConsV1 (BNFDuplicateDefinitionV1 ?name ?firstSpan ?span) ?following) ?finalIndex))
      (body (BNFDiscoveryCollectLoopV1 ?tail ?index ?reversed ?definitions ?following ?finalIndex)))
    (rule bnf-discovery-reverse-definitions-nil-v1
      (head (BNFDiscoveryReverseDefinitionsV1 BNFDefinitionsNilV1 ?result ?result)) (body))
    (rule bnf-discovery-reverse-definitions-cons-v1
      (head (BNFDiscoveryReverseDefinitionsV1 (BNFDefinitionsConsV1 ?head ?tail) ?before ?after))
      (body (BNFDiscoveryReverseDefinitionsV1 ?tail (BNFDefinitionsConsV1 ?head ?before) ?after)))
    (rule bnf-discovery-definition-lookup-v1
      (head (BNFDefinitionLookupV1 ?name (BNFIndexedDefinitionsV1 ?index) ?result))
      (body (BNFGraphTrieLookupV1 ?name ?index ?lookup) (BNFDiscoveryDefinitionResultV1 ?lookup ?result)))
    (rule bnf-discovery-definition-missing-v1
      (head (BNFDiscoveryDefinitionResultV1 BNFIndexMissingV1 BNFDefinitionMissingV1)) (body))
    (rule bnf-discovery-definition-found-v1
      (head (BNFDiscoveryDefinitionResultV1 (BNFIndexFoundV1 ?definition) ?definition)) (body))
    (rule bnf-discovery-reverse-index-nil-v1
      (head (BNFDiscoveryReverseIndexV1 BNFDiscoveryDefinitionsNilV1 ?lexicals ?index ?index)) (body))
    (rule bnf-discovery-reverse-index-cons-v1
      (head (BNFDiscoveryReverseIndexV1
        (BNFDiscoveryDefinitionsConsV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?tail)
        ?lexicals ?index ?result))
      (body (BNFCollectExpressionReferencesV1 ?expression ?lexicals ?references)
        (BNFDiscoveryAddReferencesV1 ?references (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?index ?next)
        (BNFDiscoveryReverseIndexV1 ?tail ?lexicals ?next ?result)))
    (rule bnf-discovery-dependents-missing-v1
      (head (BNFDiscoveryDependentsV1 BNFIndexMissingV1 BNFDiscoveryDefinitionsNilV1)) (body))
    (rule bnf-discovery-dependents-found-v1
      (head (BNFDiscoveryDependentsV1 (BNFIndexFoundV1 ?definitions) ?definitions)) (body))
    (rule bnf-discovery-add-references-nil-v1
      (head (BNFDiscoveryAddReferencesV1 BNFNamesNilV1 ?node ?index ?index)) (body))
    (rule bnf-discovery-add-references-cons-v1
      (head (BNFDiscoveryAddReferencesV1 (BNFNamesConsV1 ?name ?tail) ?node ?index ?result))
      (body (BNFGraphTrieLookupV1 ?name ?index ?lookup)
        (BNFDiscoveryDependentsV1 ?lookup ?previous)
        (BNFGraphTriePutV1 ?name (BNFDiscoveryDefinitionsConsV1 ?node ?previous) ?index ?next)
        (BNFDiscoveryAddReferencesV1 ?tail ?node ?next ?result)))

    (rule bnf-discovery-ready-productive-v1
      (head (BNFDiscoveryReadyV1 BNFDiscoveryProductiveV1 ?expression ?known ?lexicals ?result))
      (body (BNFExpressionProductiveV1 ?expression ?known ?lexicals ?result)))
    (rule bnf-discovery-ready-nullable-v1
      (head (BNFDiscoveryReadyV1 BNFDiscoveryNullableV1 ?expression ?known ?lexicals ?result))
      (body (BNFExpressionNullableV1 ?expression ?known ?lexicals ?result)))
    (rule bnf-discovery-schedule-initial-v1
      (head (BNFDiscoveryScheduleV1 ?node BNFDiscoveryInitialV1 ?before ?after))
      (body (BNFDiscoveryScheduleCurrentV1 ?node ?before ?after)))
    (rule bnf-discovery-schedule-after-position-v1
      (head (BNFDiscoveryScheduleV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span)
        (BNFDiscoveryAfterPositionV1 ?origin) ?before ?after))
      (body (BNFDiscoveryRankCompareV1 ?rank ?origin ?order)
        (BNFDiscoveryScheduleAtV1 ?order (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?before ?after)))
    (rule bnf-discovery-schedule-later-v1
      (head (BNFDiscoveryScheduleAtV1 BNFDiscoveryGreaterV1 ?node ?before ?after))
      (body (BNFDiscoveryScheduleCurrentV1 ?node ?before ?after)))
    (rule bnf-discovery-schedule-earlier-v1
      (head (BNFDiscoveryScheduleAtV1 BNFDiscoveryLessV1 ?node ?before ?after))
      (body (BNFDiscoveryScheduleNextV1 ?node ?before ?after)))
    (rule bnf-discovery-schedule-equal-v1
      (head (BNFDiscoveryScheduleAtV1 BNFDiscoveryEqualV1 ?node ?before ?after))
      (body (BNFDiscoveryScheduleNextV1 ?node ?before ?after)))
    (rule bnf-discovery-schedule-current-v1
      (head (BNFDiscoveryScheduleCurrentV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span)
        (BNFDiscoveryQueuesV1 ?current ?following ?scheduled)
        (BNFDiscoveryQueuesV1 ?nextCurrent ?following ?nextScheduled)))
      (body (BNFDiscoveryHeapMergeV1
          (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1)
          ?current ?nextCurrent)
        (BNFGraphTrieInsertFirstV1 ?name ?name ?scheduled ?nextScheduled)))
    (rule bnf-discovery-schedule-next-v1
      (head (BNFDiscoveryScheduleNextV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span)
        (BNFDiscoveryQueuesV1 ?current ?following ?scheduled)
        (BNFDiscoveryQueuesV1 ?current ?nextFollowing ?nextScheduled)))
      (body (BNFDiscoveryHeapMergeV1
          (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1)
          ?following ?nextFollowing)
        (BNFGraphTrieInsertFirstV1 ?name ?name ?scheduled ?nextScheduled)))
    (rule bnf-discovery-wake-nil-v1
      (head (BNFDiscoveryWakeV1 ?mode BNFDiscoveryDefinitionsNilV1 ?origin ?known ?lexicals ?queues ?queues)) (body))
    (rule bnf-discovery-wake-cons-v1
      (head (BNFDiscoveryWakeV1 ?mode
        (BNFDiscoveryDefinitionsConsV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?rest)
        ?origin ?known ?lexicals (BNFDiscoveryQueuesV1 ?current ?following ?scheduled) ?after))
      (body (BNFGraphTrieLookupV1 ?name ?scheduled ?lookup)
        (BNFDiscoveryAfterScheduledV1 ?lookup ?mode (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span)
          ?rest ?origin ?known ?lexicals (BNFDiscoveryQueuesV1 ?current ?following ?scheduled) ?after)))
    (rule bnf-discovery-already-scheduled-v1
      (head (BNFDiscoveryAfterScheduledV1 (BNFIndexFoundV1 ?name) ?mode ?node ?rest ?origin ?known ?lexicals ?before ?after))
      (body (BNFDiscoveryWakeV1 ?mode ?rest ?origin ?known ?lexicals ?before ?after)))
    (rule bnf-discovery-not-scheduled-v1
      (head (BNFDiscoveryAfterScheduledV1 BNFIndexMissingV1 ?mode (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span)
        ?rest ?origin ?known ?lexicals ?before ?after))
      (body (BNFDiscoveryReadyV1 ?mode ?expression ?known ?lexicals ?ready)
        (BNFDiscoveryAfterReadyV1 ?ready ?mode (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span)
          ?rest ?origin ?known ?lexicals ?before ?after)))
    (rule bnf-discovery-not-ready-v1
      (head (BNFDiscoveryAfterReadyV1 BNFNoV1 ?mode ?node ?rest ?origin ?known ?lexicals ?before ?after))
      (body (BNFDiscoveryWakeV1 ?mode ?rest ?origin ?known ?lexicals ?before ?after)))
    (rule bnf-discovery-ready-v1
      (head (BNFDiscoveryAfterReadyV1 BNFYesV1 ?mode ?node ?rest ?origin ?known ?lexicals ?before ?after))
      (body (BNFDiscoveryScheduleV1 ?node ?origin ?before ?next)
        (BNFDiscoveryWakeV1 ?mode ?rest ?origin ?known ?lexicals ?next ?after)))

    (rule bnf-discovery-run-done-v1
      (head (BNFDiscoveryRunV1 ?mode ?reverse ?lexicals ?known
        (BNFDiscoveryQueuesV1 BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1 ?scheduled) ?known)) (body))
    (rule bnf-discovery-run-next-round-v1
      (head (BNFDiscoveryRunV1 ?mode ?reverse ?lexicals ?known
        (BNFDiscoveryQueuesV1 BNFDiscoveryHeapNilV1 (BNFDiscoveryHeapV1 ?node ?children ?siblings) ?scheduled) ?result))
      (body (BNFDiscoveryRunV1 ?mode ?reverse ?lexicals ?known
        (BNFDiscoveryQueuesV1 (BNFDiscoveryHeapV1 ?node ?children ?siblings) BNFDiscoveryHeapNilV1 ?scheduled) ?result)))
    (rule bnf-discovery-run-publish-v1
      (head (BNFDiscoveryRunV1 ?mode ?reverse ?lexicals ?known
        (BNFDiscoveryQueuesV1
          (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 ?rank ?name ?expression ?span) ?children BNFDiscoveryHeapNilV1)
          ?following ?scheduled) ?result))
      (body (BNFDiscoveryHeapCombineV1 ?children ?remaining)
        (BNFAppendNameV1 ?known ?name ?nextKnown)
        (BNFGraphTrieLookupV1 ?name ?reverse ?lookup)
        (BNFDiscoveryDependentsV1 ?lookup ?dependents)
        (BNFDiscoveryWakeV1 ?mode ?dependents (BNFDiscoveryAfterPositionV1 ?rank) ?nextKnown ?lexicals
          (BNFDiscoveryQueuesV1 ?remaining ?following ?scheduled) ?nextQueues)
        (BNFDiscoveryRunV1 ?mode ?reverse ?lexicals ?nextKnown ?nextQueues ?result)))
    (rule bnf-discovery-closure-v1
      (head (BNFDiscoveryClosureV1 ?mode ?ranked ?reverse ?lexicals ?result))
      (body (BNFDiscoveryWakeV1 ?mode ?ranked BNFDiscoveryInitialV1
          (BNFDiscoveryKnownV1 BNFGraphTrieEmptyV1 BNFNamesNilV1) ?lexicals
          (BNFDiscoveryQueuesV1 BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1 BNFGraphTrieEmptyV1) ?initial)
        (BNFDiscoveryRunV1 ?mode ?reverse ?lexicals
          (BNFDiscoveryKnownV1 BNFGraphTrieEmptyV1 BNFNamesNilV1) ?initial ?result)))

    ; Every diagnostic traversal remains source-ordered. The collector's
    ; index is reused by reference, lexical-conflict and start validation.
    (rule bnf-validate-grammar-discovery-v1
      (head (BNFValidateGrammarDiscoveryV1
        (bnf-v1:grammar-input (bnf-v1:document ?entries ?documentSpan)
          (bnf-v1:grammar-authority (bnf-v1:start ?startName) (bnf-v1:lexical-environment ?lexicals))) ?result))
      (body (BNFDiscoveryCollectDefinitionsV1 ?entries ?definitions ?definitionDiagnostics ?index)
        (BNFValidateLexicalsV1 ?lexicals (BNFIndexedDefinitionsV1 ?index) ?lexicalDiagnostics)
        (BNFScanEntryReferencesV1 ?entries (BNFIndexedDefinitionsV1 ?index) ?lexicals ?referenceDiagnostics)
        (BNFDefinitionLookupV1 ?startName (BNFIndexedDefinitionsV1 ?index) ?startLookup)
        (BNFStartAfterLookupV1 ?startName ?startLookup ?start ?startDiagnostics)
        (BNFAppendDiagnosticsV1 ?definitionDiagnostics ?lexicalDiagnostics ?d01)
        (BNFAppendDiagnosticsV1 ?d01 ?referenceDiagnostics ?d02)
        (BNFAppendDiagnosticsV1 ?d02 ?startDiagnostics ?diagnostics)
        (BNFFinishValidationV1 ?documentSpan ?start ?definitions ?lexicals ?diagnostics ?result)))

    (rule bnf-analyze-document-worklist-v1
      (head (BNFAnalyzeDocumentWorklistV1 (BNFStartSomeV1 ?startName ?startSpan) ?definitions ?lexicals ?result))
      (body (BNFDiscoveryRankDefinitionsV1 ?definitions BNFDiscoveryRankZeroV1 ?ranked)
        (BNFDiscoveryDefinitionIndexV1 ?ranked BNFGraphTrieEmptyV1 ?definitionIndex)
        (BNFDiscoveryReverseIndexV1 ?ranked ?lexicals BNFGraphTrieEmptyV1 ?reverse)
        (BNFReachableLoopV1 (BNFNamesConsV1 ?startName BNFNamesNilV1)
          (BNFIndexedDefinitionsV1 ?definitionIndex) ?lexicals
          (BNFDiscoveryKnownV1 BNFGraphTrieEmptyV1 BNFNamesNilV1) ?reachableIndex)
        (BNFUnreachableDiagnosticsV1 ?definitions ?reachableIndex ?unreachableDiagnostics)
        (BNFDiscoveryClosureV1 BNFDiscoveryProductiveV1 ?ranked ?reverse ?lexicals ?productiveIndex)
        (BNFUnproductiveDiagnosticsV1 ?definitions ?productiveIndex ?unproductiveDiagnostics)
        (BNFDiscoveryClosureV1 BNFDiscoveryNullableV1 ?ranked ?reverse ?lexicals ?nullableIndex)
        (BNFIndexedNamesObserveV1 ?reachableIndex ?reachable)
        (BNFIndexedNamesObserveV1 ?nullableIndex ?nullable)
        (BNFIndexedNamesObserveV1 ?productiveIndex ?productive)
        (BNFAppendDiagnosticsV1 ?unreachableDiagnostics ?unproductiveDiagnostics ?diagnostics)
        (BNFFinishGraphAnalysisV1 (BNFStartSomeV1 ?startName ?startSpan) ?definitions ?lexicals
          ?reachable ?nullable ?productive ?diagnostics ?result)))))
