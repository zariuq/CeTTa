; Indexed membership for the existing ordered BNF graph algorithms.
;
; The wrapper retains the original discovery list and a sparse character trie.
; Trie branches use structural equality, not numeric ordering or rotations.
; Pass scheduling, diagnostics and returned lists are unchanged. This candidate
; does not replace public graph analysis; generated costs are measured separately.
(gslt-presentation-v1 PlainBnfGraphIndexV1
  (signature
    (operator different 2)
    (operator bnf-v1:text-cons 2)
    (operator BNFGraphTrieV1 2)
    (operator BNFGraphTrieEdgesConsV1 3)
    (operator BNFGraphTrieLookupV1 3)
    (operator BNFGraphTrieEdgeLookupV1 4)
    (operator BNFGraphTrieInsertFirstV1 4)
    (operator BNFGraphTrieEdgeInsertV1 5)
    (operator BNFGraphTrieFirstValueV1 3)
    (operator BNFIndexedNamesV1 2)
    (operator BNFNameLookupV1 3)
    (operator BNFAppendNameV1 3)
    (operator BNFIndexFoundV1 1)
    (operator BNFNameFoundV1 1)
    (operator BNFIndexedNameLookupResultV1 2)
    (operator BNFIndexedNamesObserveV1 2)
    (operator BNFAnalyzeDocumentIndexedV1 4)
    (operator BNFStartSomeV1 2)
    (operator BNFNamesConsV1 2)
    (operator BNFReachableLoopV1 5)
    (operator BNFUnreachableDiagnosticsV1 3)
    (operator BNFProductivityClosureV1 4)
    (operator BNFUnproductiveDiagnosticsV1 3)
    (operator BNFNullabilityClosureV1 4)
    (operator BNFAppendDiagnosticsV1 3)
    (operator BNFFinishGraphAnalysisV1 8))
  (equations)
  (rewrites
    (rule bnf-graph-trie-lookup-empty-v1
      (head (BNFGraphTrieLookupV1 ?key BNFGraphTrieEmptyV1 BNFIndexMissingV1))
      (body))
    (rule bnf-graph-trie-lookup-value-v1
      (head (BNFGraphTrieLookupV1 (metta-nullary bnf-v1:text-nil)
        (BNFGraphTrieV1 ?value ?edges) ?value))
      (body))
    (rule bnf-graph-trie-lookup-cons-v1
      (head (BNFGraphTrieLookupV1 (bnf-v1:text-cons ?head ?tail)
        (BNFGraphTrieV1 ?value ?edges) ?result))
      (body (BNFGraphTrieEdgeLookupV1 ?head ?tail ?edges ?result)))
    (rule bnf-graph-trie-edge-lookup-nil-v1
      (head (BNFGraphTrieEdgeLookupV1 ?head ?tail BNFGraphTrieEdgesNilV1 BNFIndexMissingV1))
      (body))
    (rule bnf-graph-trie-edge-lookup-found-v1
      (head (BNFGraphTrieEdgeLookupV1 ?head ?tail
        (BNFGraphTrieEdgesConsV1 ?head ?child ?rest) ?result))
      (body (BNFGraphTrieLookupV1 ?tail ?child ?result)))
    (rule bnf-graph-trie-edge-lookup-next-v1
      (head (BNFGraphTrieEdgeLookupV1 ?head ?tail
        (BNFGraphTrieEdgesConsV1 ?other ?child ?rest) ?result))
      (body
        (different ?head ?other)
        (BNFGraphTrieEdgeLookupV1 ?head ?tail ?rest ?result)))
    (rule bnf-graph-trie-insert-empty-v1
      (head (BNFGraphTrieInsertFirstV1 ?key ?value BNFGraphTrieEmptyV1 ?result))
      (body (BNFGraphTrieInsertFirstV1 ?key ?value
        (BNFGraphTrieV1 BNFIndexMissingV1 BNFGraphTrieEdgesNilV1) ?result)))
    (rule bnf-graph-trie-insert-value-v1
      (head (BNFGraphTrieInsertFirstV1 (metta-nullary bnf-v1:text-nil) ?value
        (BNFGraphTrieV1 ?old ?edges) (BNFGraphTrieV1 ?next ?edges)))
      (body (BNFGraphTrieFirstValueV1 ?old ?value ?next)))
    (rule bnf-graph-trie-insert-cons-v1
      (head (BNFGraphTrieInsertFirstV1 (bnf-v1:text-cons ?head ?tail) ?value
        (BNFGraphTrieV1 ?old ?edges) (BNFGraphTrieV1 ?old ?nextEdges)))
      (body (BNFGraphTrieEdgeInsertV1 ?head ?tail ?value ?edges ?nextEdges)))
    (rule bnf-graph-trie-first-value-empty-v1
      (head (BNFGraphTrieFirstValueV1 BNFIndexMissingV1 ?value (BNFIndexFoundV1 ?value)))
      (body))
    (rule bnf-graph-trie-first-value-present-v1
      (head (BNFGraphTrieFirstValueV1 (BNFIndexFoundV1 ?old) ?value (BNFIndexFoundV1 ?old)))
      (body))
    (rule bnf-graph-trie-edge-insert-nil-v1
      (head (BNFGraphTrieEdgeInsertV1 ?head ?tail ?value BNFGraphTrieEdgesNilV1
        (BNFGraphTrieEdgesConsV1 ?head ?child BNFGraphTrieEdgesNilV1)))
      (body (BNFGraphTrieInsertFirstV1 ?tail ?value BNFGraphTrieEmptyV1 ?child)))
    (rule bnf-graph-trie-edge-insert-found-v1
      (head (BNFGraphTrieEdgeInsertV1 ?head ?tail ?value
        (BNFGraphTrieEdgesConsV1 ?head ?child ?rest)
        (BNFGraphTrieEdgesConsV1 ?head ?nextChild ?rest)))
      (body (BNFGraphTrieInsertFirstV1 ?tail ?value ?child ?nextChild)))
    (rule bnf-graph-trie-edge-insert-next-v1
      (head (BNFGraphTrieEdgeInsertV1 ?head ?tail ?value
        (BNFGraphTrieEdgesConsV1 ?other ?child ?rest)
        (BNFGraphTrieEdgesConsV1 ?other ?child ?nextRest)))
      (body
        (different ?head ?other)
        (BNFGraphTrieEdgeInsertV1 ?head ?tail ?value ?rest ?nextRest)))
    (rule bnf-name-lookup-indexed-v1
      (head (BNFNameLookupV1
        ?name (BNFIndexedNamesV1 ?index ?ordered) ?result))
      (body
        (BNFGraphTrieLookupV1 ?name ?index ?indexed)
        (BNFIndexedNameLookupResultV1 ?indexed ?result)))
    (rule bnf-indexed-name-lookup-missing-v1
      (head (BNFIndexedNameLookupResultV1 BNFIndexMissingV1 BNFNameMissingV1))
      (body))
    (rule bnf-indexed-name-lookup-found-v1
      (head (BNFIndexedNameLookupResultV1
        (BNFIndexFoundV1 ?name) (BNFNameFoundV1 ?name)))
      (body))
    (rule bnf-append-name-indexed-v1
      (head (BNFAppendNameV1
        (BNFIndexedNamesV1 ?index ?ordered) ?name
        (BNFIndexedNamesV1 ?nextIndex ?nextOrdered)))
      (body
        (BNFGraphTrieInsertFirstV1 ?name ?name ?index ?nextIndex)
        (BNFAppendNameV1 ?ordered ?name ?nextOrdered)))
    (rule bnf-indexed-names-observe-v1
      (head (BNFIndexedNamesObserveV1 (BNFIndexedNamesV1 ?index ?ordered) ?ordered))
      (body))
    (rule bnf-analyze-document-indexed-v1
      (head (BNFAnalyzeDocumentIndexedV1
        (BNFStartSomeV1 ?startName ?startSpan) ?definitions ?lexicals ?result))
      (body
        (BNFReachableLoopV1
          (BNFNamesConsV1 ?startName BNFNamesNilV1)
          ?definitions ?lexicals
          (BNFIndexedNamesV1 BNFGraphTrieEmptyV1 BNFNamesNilV1) ?reachableIndex)
        (BNFUnreachableDiagnosticsV1
          ?definitions ?reachableIndex ?unreachableDiagnostics)
        (BNFProductivityClosureV1
          ?definitions (BNFIndexedNamesV1 BNFGraphTrieEmptyV1 BNFNamesNilV1)
          ?lexicals ?productiveIndex)
        (BNFUnproductiveDiagnosticsV1
          ?definitions ?productiveIndex ?unproductiveDiagnostics)
        (BNFNullabilityClosureV1
          ?definitions (BNFIndexedNamesV1 BNFGraphTrieEmptyV1 BNFNamesNilV1)
          ?lexicals ?nullableIndex)
        (BNFIndexedNamesObserveV1 ?reachableIndex ?reachable)
        (BNFIndexedNamesObserveV1 ?nullableIndex ?nullable)
        (BNFIndexedNamesObserveV1 ?productiveIndex ?productive)
        (BNFAppendDiagnosticsV1
          ?unreachableDiagnostics ?unproductiveDiagnostics ?diagnostics)
        (BNFFinishGraphAnalysisV1
          (BNFStartSomeV1 ?startName ?startSpan) ?definitions ?lexicals
          ?reachable ?nullable ?productive ?diagnostics ?result)))))
