; Persistent first-binding index for structured BNF names.
;
; This is auxiliary grammar-analysis data, not a grammar representation or
; rule interpreter. Two/three-node insertion splits a full node on the return
; path. Existing payloads win on equal keys. Source-ordered declarations and
; diagnostics remain separate from this lookup structure.

(gslt-presentation-v1 PlainBnfNameIndexV1
  (signature
    (operator different 2)
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2)
    (operator bnf-v1:text-cons 2)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator BNFDefinitionV1 3)
    (operator BNFDefinitionsConsV1 2)
    (operator BNFDiagnosticsConsV1 2)
    (operator BNFDuplicateDefinitionV1 3)
    (operator BNFIndexedCollectDefinitionsV1 4)
    (operator BNFIndexSeedDefinitionsV1 5)
    (operator BNFIndexCollectLoopV1 6)
    (operator BNFIndexCollectAfterLookupV1 10)
    (operator BNFIndexReverseDefinitionsV1 3)
    (operator BNFIndexReverseDiagnosticsV1 3)
    (operator BNFIndexTwoV1 4)
    (operator BNFIndexThreeV1 7)
    (operator BNFIndexDoneV1 1)
    (operator BNFIndexSplitV1 4)
    (operator BNFIndexFoundV1 1)
    (operator BNFIndexCompareV1 3)
    (operator BNFIndexLookupV1 3)
    (operator BNFIndexLookupTwoV1 7)
    (operator BNFIndexLookupThreeFirstV1 10)
    (operator BNFIndexLookupThreeSecondV1 7)
    (operator BNFIndexInsertFirstV1 4)
    (operator BNFIndexInsertRootV1 2)
    (operator BNFIndexInsertWalkV1 4)
    (operator BNFIndexInsertTwoV1 8)
    (operator BNFIndexInsertTwoLeftV1 5)
    (operator BNFIndexInsertTwoRightV1 5)
    (operator BNFIndexInsertThreeFirstV1 11)
    (operator BNFIndexInsertThreeSecondV1 11)
    (operator BNFIndexInsertThreeLeftV1 8)
    (operator BNFIndexInsertThreeMiddleV1 8)
    (operator BNFIndexInsertThreeRightV1 8)
    (operator BNFValidateGrammarIndexedV1 2)
    (operator bnf-v1:grammar-input 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:grammar-authority 2)
    (operator bnf-v1:start 1)
    (operator bnf-v1:lexical-environment 1)
    (operator BNFValidateLexicalsV1 3)
    (operator BNFScanEntryReferencesV1 4)
    (operator BNFDefinitionLookupV1 3)
    (operator BNFStartAfterLookupV1 4)
    (operator BNFAppendDiagnosticsV1 3)
    (operator BNFFinishValidationV1 6))
  (equations)
  (rewrites
    (rule bnf-index-compare-empty-v1
      (head (BNFIndexCompareV1
        (metta-nullary bnf-v1:text-nil)
        (metta-nullary bnf-v1:text-nil) BNFIndexEqualV1))
      (body))
    (rule bnf-index-compare-empty-left-v1
      (head (BNFIndexCompareV1
        (metta-nullary bnf-v1:text-nil)
        (bnf-v1:text-cons ?head ?tail) BNFIndexLessV1))
      (body))
    (rule bnf-index-compare-empty-right-v1
      (head (BNFIndexCompareV1
        (bnf-v1:text-cons ?head ?tail)
        (metta-nullary bnf-v1:text-nil) BNFIndexGreaterV1))
      (body))
    (rule bnf-index-compare-equal-head-v1
      (head (BNFIndexCompareV1
        (bnf-v1:text-cons ?head ?left)
        (bnf-v1:text-cons ?head ?right) ?order))
      (body (BNFIndexCompareV1 ?left ?right ?order)))
    (rule bnf-index-compare-less-head-v1
      (head (BNFIndexCompareV1
        (bnf-v1:text-cons ?left ?leftTail)
        (bnf-v1:text-cons ?right ?rightTail) BNFIndexLessV1))
      (body (ground-integer-less ?left ?right)))
    (rule bnf-index-compare-greater-head-v1
      (head (BNFIndexCompareV1
        (bnf-v1:text-cons ?left ?leftTail)
        (bnf-v1:text-cons ?right ?rightTail) BNFIndexGreaterV1))
      (body
        (different ?left ?right)
        (ground-integer-not-less ?left ?right)))

    (rule bnf-index-lookup-empty-v1
      (head (BNFIndexLookupV1 ?key BNFIndexEmptyV1 BNFIndexMissingV1))
      (body))
    (rule bnf-index-lookup-two-v1
      (head (BNFIndexLookupV1
        ?key (BNFIndexTwoV1 ?left ?stored ?value ?right) ?result))
      (body
        (BNFIndexCompareV1 ?key ?stored ?order)
        (BNFIndexLookupTwoV1 ?order ?key ?left ?stored ?value ?right ?result)))
    (rule bnf-index-lookup-two-equal-v1
      (head (BNFIndexLookupTwoV1
        BNFIndexEqualV1 ?key ?left ?stored ?value ?right (BNFIndexFoundV1 ?value)))
      (body))
    (rule bnf-index-lookup-two-less-v1
      (head (BNFIndexLookupTwoV1
        BNFIndexLessV1 ?key ?left ?stored ?value ?right ?result))
      (body (BNFIndexLookupV1 ?key ?left ?result)))
    (rule bnf-index-lookup-two-greater-v1
      (head (BNFIndexLookupTwoV1
        BNFIndexGreaterV1 ?key ?left ?stored ?value ?right ?result))
      (body (BNFIndexLookupV1 ?key ?right ?result)))
    (rule bnf-index-lookup-three-v1
      (head (BNFIndexLookupV1 ?key
        (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right)
        ?result))
      (body
        (BNFIndexCompareV1 ?key ?first ?order)
        (BNFIndexLookupThreeFirstV1
          ?order ?key ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result)))
    (rule bnf-index-lookup-three-first-equal-v1
      (head (BNFIndexLookupThreeFirstV1
        BNFIndexEqualV1 ?key ?left ?first ?firstValue ?middle ?second ?secondValue ?right
        (BNFIndexFoundV1 ?firstValue)))
      (body))
    (rule bnf-index-lookup-three-first-less-v1
      (head (BNFIndexLookupThreeFirstV1
        BNFIndexLessV1 ?key ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result))
      (body (BNFIndexLookupV1 ?key ?left ?result)))
    (rule bnf-index-lookup-three-first-greater-v1
      (head (BNFIndexLookupThreeFirstV1
        BNFIndexGreaterV1 ?key ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result))
      (body
        (BNFIndexCompareV1 ?key ?second ?order)
        (BNFIndexLookupThreeSecondV1
          ?order ?key ?middle ?second ?secondValue ?right ?result)))
    (rule bnf-index-lookup-three-second-equal-v1
      (head (BNFIndexLookupThreeSecondV1
        BNFIndexEqualV1 ?key ?middle ?second ?secondValue ?right (BNFIndexFoundV1 ?secondValue)))
      (body))
    (rule bnf-index-lookup-three-second-less-v1
      (head (BNFIndexLookupThreeSecondV1
        BNFIndexLessV1 ?key ?middle ?second ?secondValue ?right ?result))
      (body (BNFIndexLookupV1 ?key ?middle ?result)))
    (rule bnf-index-lookup-three-second-greater-v1
      (head (BNFIndexLookupThreeSecondV1
        BNFIndexGreaterV1 ?key ?middle ?second ?secondValue ?right ?result))
      (body (BNFIndexLookupV1 ?key ?right ?result)))

    (rule bnf-index-insert-first-v1
      (head (BNFIndexInsertFirstV1 ?key ?value ?before ?after))
      (body
        (BNFIndexInsertWalkV1 ?key ?value ?before ?result)
        (BNFIndexInsertRootV1 ?result ?after)))
    (rule bnf-index-insert-root-done-v1
      (head (BNFIndexInsertRootV1 (BNFIndexDoneV1 ?tree) ?tree))
      (body))
    (rule bnf-index-insert-root-split-v1
      (head (BNFIndexInsertRootV1
        (BNFIndexSplitV1 ?left ?key ?value ?right)
        (BNFIndexTwoV1 ?left ?key ?value ?right)))
      (body))
    (rule bnf-index-insert-empty-v1
      (head (BNFIndexInsertWalkV1 ?key ?value BNFIndexEmptyV1
        (BNFIndexSplitV1 BNFIndexEmptyV1 ?key ?value BNFIndexEmptyV1)))
      (body))
    (rule bnf-index-insert-two-v1
      (head (BNFIndexInsertWalkV1 ?key ?value
        (BNFIndexTwoV1 ?left ?stored ?oldValue ?right) ?result))
      (body
        (BNFIndexCompareV1 ?key ?stored ?order)
        (BNFIndexInsertTwoV1 ?order ?key ?value ?left ?stored ?oldValue ?right ?result)))
    (rule bnf-index-insert-two-equal-v1
      (head (BNFIndexInsertTwoV1
        BNFIndexEqualV1 ?key ?value ?left ?stored ?oldValue ?right
        (BNFIndexDoneV1 (BNFIndexTwoV1 ?left ?stored ?oldValue ?right))))
      (body))
    (rule bnf-index-insert-two-less-v1
      (head (BNFIndexInsertTwoV1
        BNFIndexLessV1 ?key ?value ?left ?stored ?oldValue ?right ?result))
      (body
        (BNFIndexInsertWalkV1 ?key ?value ?left ?child)
        (BNFIndexInsertTwoLeftV1 ?child ?stored ?oldValue ?right ?result)))
    (rule bnf-index-insert-two-greater-v1
      (head (BNFIndexInsertTwoV1
        BNFIndexGreaterV1 ?key ?value ?left ?stored ?oldValue ?right ?result))
      (body
        (BNFIndexInsertWalkV1 ?key ?value ?right ?child)
        (BNFIndexInsertTwoRightV1 ?left ?stored ?oldValue ?child ?result)))
    (rule bnf-index-insert-two-left-done-v1
      (head (BNFIndexInsertTwoLeftV1 (BNFIndexDoneV1 ?left) ?key ?value ?right
        (BNFIndexDoneV1 (BNFIndexTwoV1 ?left ?key ?value ?right))))
      (body))
    (rule bnf-index-insert-two-left-split-v1
      (head (BNFIndexInsertTwoLeftV1
        (BNFIndexSplitV1 ?left ?first ?firstValue ?middle) ?second ?secondValue ?right
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-two-right-done-v1
      (head (BNFIndexInsertTwoRightV1 ?left ?key ?value (BNFIndexDoneV1 ?right)
        (BNFIndexDoneV1 (BNFIndexTwoV1 ?left ?key ?value ?right))))
      (body))
    (rule bnf-index-insert-two-right-split-v1
      (head (BNFIndexInsertTwoRightV1 ?left ?first ?firstValue
        (BNFIndexSplitV1 ?middle ?second ?secondValue ?right)
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-v1
      (head (BNFIndexInsertWalkV1 ?key ?value
        (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right) ?result))
      (body
        (BNFIndexCompareV1 ?key ?first ?order)
        (BNFIndexInsertThreeFirstV1
          ?order ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result)))
    (rule bnf-index-insert-three-first-equal-v1
      (head (BNFIndexInsertThreeFirstV1
        BNFIndexEqualV1 ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-first-less-v1
      (head (BNFIndexInsertThreeFirstV1
        BNFIndexLessV1 ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result))
      (body
        (BNFIndexInsertWalkV1 ?key ?value ?left ?child)
        (BNFIndexInsertThreeLeftV1
          ?child ?first ?firstValue ?middle ?second ?secondValue ?right ?result)))
    (rule bnf-index-insert-three-first-greater-v1
      (head (BNFIndexInsertThreeFirstV1
        BNFIndexGreaterV1 ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result))
      (body
        (BNFIndexCompareV1 ?key ?second ?order)
        (BNFIndexInsertThreeSecondV1
          ?order ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result)))
    (rule bnf-index-insert-three-second-equal-v1
      (head (BNFIndexInsertThreeSecondV1
        BNFIndexEqualV1 ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-second-less-v1
      (head (BNFIndexInsertThreeSecondV1
        BNFIndexLessV1 ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result))
      (body
        (BNFIndexInsertWalkV1 ?key ?value ?middle ?child)
        (BNFIndexInsertThreeMiddleV1
          ?left ?first ?firstValue ?child ?second ?secondValue ?right ?result)))
    (rule bnf-index-insert-three-second-greater-v1
      (head (BNFIndexInsertThreeSecondV1
        BNFIndexGreaterV1 ?key ?value ?left ?first ?firstValue ?middle ?second ?secondValue ?right ?result))
      (body
        (BNFIndexInsertWalkV1 ?key ?value ?right ?child)
        (BNFIndexInsertThreeRightV1
          ?left ?first ?firstValue ?middle ?second ?secondValue ?child ?result)))
    (rule bnf-index-insert-three-left-done-v1
      (head (BNFIndexInsertThreeLeftV1 (BNFIndexDoneV1 ?left)
        ?first ?firstValue ?middle ?second ?secondValue ?right
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-left-split-v1
      (head (BNFIndexInsertThreeLeftV1
        (BNFIndexSplitV1 ?a ?key ?value ?b)
        ?first ?firstValue ?middle ?second ?secondValue ?right
        (BNFIndexSplitV1 (BNFIndexTwoV1 ?a ?key ?value ?b)
          ?first ?firstValue (BNFIndexTwoV1 ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-middle-done-v1
      (head (BNFIndexInsertThreeMiddleV1 ?left ?first ?firstValue
        (BNFIndexDoneV1 ?middle) ?second ?secondValue ?right
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-middle-split-v1
      (head (BNFIndexInsertThreeMiddleV1 ?left ?first ?firstValue
        (BNFIndexSplitV1 ?a ?key ?value ?b) ?second ?secondValue ?right
        (BNFIndexSplitV1 (BNFIndexTwoV1 ?left ?first ?firstValue ?a)
          ?key ?value (BNFIndexTwoV1 ?b ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-right-done-v1
      (head (BNFIndexInsertThreeRightV1 ?left ?first ?firstValue
        ?middle ?second ?secondValue (BNFIndexDoneV1 ?right)
        (BNFIndexDoneV1
          (BNFIndexThreeV1 ?left ?first ?firstValue ?middle ?second ?secondValue ?right))))
      (body))
    (rule bnf-index-insert-three-right-split-v1
      (head (BNFIndexInsertThreeRightV1 ?left ?first ?firstValue
        ?middle ?second ?secondValue (BNFIndexSplitV1 ?a ?key ?value ?b)
        (BNFIndexSplitV1 (BNFIndexTwoV1 ?left ?first ?firstValue ?middle)
          ?second ?secondValue (BNFIndexTwoV1 ?a ?key ?value ?b))))
      (body))

    ; The index is auxiliary: output definitions and duplicate diagnostics
    ; retain their source-ordered lists. Existing input prefixes may contain
    ; duplicate names; their oldest indexed payload remains authoritative.
    (rule bnf-indexed-collect-definitions-v1
      (head (BNFIndexedCollectDefinitionsV1 ?entries ?before ?after ?diagnostics))
      (body
        (BNFIndexSeedDefinitionsV1
          ?before BNFIndexEmptyV1 BNFDefinitionsNilV1 ?index ?reversed)
        (BNFIndexCollectLoopV1
          ?entries ?index ?reversed BNFDiagnosticsNilV1 ?after ?diagnostics)))
    (rule bnf-index-seed-definitions-nil-v1
      (head (BNFIndexSeedDefinitionsV1 BNFDefinitionsNilV1 ?index ?reversed ?index ?reversed))
      (body))
    (rule bnf-index-seed-definitions-cons-v1
      (head (BNFIndexSeedDefinitionsV1
        (BNFDefinitionsConsV1 (BNFDefinitionV1 ?name ?expression ?span) ?tail)
        ?index ?reversed ?afterIndex ?afterReversed))
      (body
        (BNFIndexInsertFirstV1 ?name (BNFDefinitionV1 ?name ?expression ?span) ?index ?next)
        (BNFIndexSeedDefinitionsV1 ?tail ?next
          (BNFDefinitionsConsV1 (BNFDefinitionV1 ?name ?expression ?span) ?reversed)
          ?afterIndex ?afterReversed)))
    (rule bnf-index-collect-nil-v1
      (head (BNFIndexCollectLoopV1
        (metta-nullary bnf-v1:entries-nil) ?index ?reversed ?diagnosticsReversed ?after ?diagnostics))
      (body
        (BNFIndexReverseDefinitionsV1 ?reversed BNFDefinitionsNilV1 ?after)
        (BNFIndexReverseDiagnosticsV1 ?diagnosticsReversed BNFDiagnosticsNilV1 ?diagnostics)))
    (rule bnf-index-collect-comment-v1
      (head (BNFIndexCollectLoopV1
        (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail)
        ?index ?reversed ?diagnosticsReversed ?after ?diagnostics))
      (body (BNFIndexCollectLoopV1
        ?tail ?index ?reversed ?diagnosticsReversed ?after ?diagnostics)))
    (rule bnf-index-collect-blank-v1
      (head (BNFIndexCollectLoopV1
        (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail)
        ?index ?reversed ?diagnosticsReversed ?after ?diagnostics))
      (body (BNFIndexCollectLoopV1
        ?tail ?index ?reversed ?diagnosticsReversed ?after ?diagnostics)))
    (rule bnf-index-collect-rule-v1
      (head (BNFIndexCollectLoopV1
        (bnf-v1:entries-cons (bnf-v1:rule ?name ?expression ?span) ?tail)
        ?index ?reversed ?diagnosticsReversed ?after ?diagnostics))
      (body
        (BNFIndexLookupV1 ?name ?index ?lookup)
        (BNFIndexCollectAfterLookupV1 ?lookup ?name ?expression ?span ?tail
          ?index ?reversed ?diagnosticsReversed ?after ?diagnostics)))
    (rule bnf-index-collect-fresh-v1
      (head (BNFIndexCollectAfterLookupV1 BNFIndexMissingV1
        ?name ?expression ?span ?tail ?index ?reversed ?diagnosticsReversed ?after ?diagnostics))
      (body
        (BNFIndexInsertFirstV1 ?name (BNFDefinitionV1 ?name ?expression ?span) ?index ?next)
        (BNFIndexCollectLoopV1 ?tail ?next
          (BNFDefinitionsConsV1 (BNFDefinitionV1 ?name ?expression ?span) ?reversed)
          ?diagnosticsReversed ?after ?diagnostics)))
    (rule bnf-index-collect-duplicate-v1
      (head (BNFIndexCollectAfterLookupV1
        (BNFIndexFoundV1 (BNFDefinitionV1 ?name ?firstExpression ?firstSpan))
        ?name ?expression ?span ?tail ?index ?reversed ?diagnosticsReversed ?after ?diagnostics))
      (body
        (BNFIndexCollectLoopV1 ?tail ?index ?reversed
          (BNFDiagnosticsConsV1 (BNFDuplicateDefinitionV1 ?name ?firstSpan ?span) ?diagnosticsReversed)
          ?after ?diagnostics)))
    (rule bnf-index-reverse-definitions-nil-v1
      (head (BNFIndexReverseDefinitionsV1 BNFDefinitionsNilV1 ?reversed ?reversed))
      (body))
    (rule bnf-index-reverse-definitions-cons-v1
      (head (BNFIndexReverseDefinitionsV1 (BNFDefinitionsConsV1 ?head ?tail) ?reversed ?result))
      (body (BNFIndexReverseDefinitionsV1 ?tail (BNFDefinitionsConsV1 ?head ?reversed) ?result)))
    (rule bnf-index-reverse-diagnostics-nil-v1
      (head (BNFIndexReverseDiagnosticsV1 BNFDiagnosticsNilV1 ?reversed ?reversed))
      (body))
    (rule bnf-index-reverse-diagnostics-cons-v1
      (head (BNFIndexReverseDiagnosticsV1 (BNFDiagnosticsConsV1 ?head ?tail) ?reversed ?result))
      (body (BNFIndexReverseDiagnosticsV1 ?tail (BNFDiagnosticsConsV1 ?head ?reversed) ?result)))

    ; Candidate full declaration pass. Only collection is indexed; lexical
    ; checks, reference checks, start selection and diagnostic order use the
    ; same authored operations as BNFValidateGrammarV1. Names have the
    ; structured integer-text domain of PlainBnfGrammarV1.
    (rule bnf-validate-grammar-indexed-v1
      (head
        (BNFValidateGrammarIndexedV1
          (bnf-v1:grammar-input
            (bnf-v1:document ?entries ?documentSpan)
            (bnf-v1:grammar-authority
              (bnf-v1:start ?startName)
              (bnf-v1:lexical-environment ?lexicals)))
          ?result))
      (body
        (BNFIndexedCollectDefinitionsV1
          ?entries BNFDefinitionsNilV1
          ?definitions ?definitionDiagnostics)
        (BNFValidateLexicalsV1
          ?lexicals ?definitions ?lexicalDiagnostics)
        (BNFScanEntryReferencesV1
          ?entries ?definitions ?lexicals ?referenceDiagnostics)
        (BNFDefinitionLookupV1 ?startName ?definitions ?startLookup)
        (BNFStartAfterLookupV1
          ?startName ?startLookup ?start ?startDiagnostics)
        (BNFAppendDiagnosticsV1
          ?definitionDiagnostics ?lexicalDiagnostics ?d01)
        (BNFAppendDiagnosticsV1 ?d01 ?referenceDiagnostics ?d02)
        (BNFAppendDiagnosticsV1 ?d02 ?startDiagnostics ?diagnostics)
        (BNFFinishValidationV1
          ?documentSpan ?start ?definitions ?lexicals
          ?diagnostics ?result)))))
