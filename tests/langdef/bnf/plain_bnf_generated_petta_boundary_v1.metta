; Observations of freshly generated component entries, not another implementation.
(= (generated-boundary:lookup $key $index)
   (let (gslt:result:BNFGraphTrieLookupV1:110 $result)
        (gslt:entry:BNFGraphTrieLookupV1:110 $key $index) $result))
(= (generated-boundary:insert $key $value $index)
   (let (gslt:result:BNFGraphTrieInsertFirstV1:1110 $result)
        (gslt:entry:BNFGraphTrieInsertFirstV1:1110 $key $value $index) $result))
(= (generated-boundary:reverse $reversed $before)
   (let (gslt:result:BNFDiscoveryReverseDefinitionsV1:110 $result)
        (gslt:entry:BNFDiscoveryReverseDefinitionsV1:110 $reversed $before) $result))
(= (generated-boundary:trigger) PayloadExecuted)

!(BoundaryEmpty
   (collapse (generated-boundary:lookup (bnf-v1:text-nil) BNFGraphTrieEmptyV1)))

!(BoundaryNoClause
   (collapse (generated-boundary:lookup (BrokenKey)
     (BNFGraphTrieV1 BNFIndexMissingV1 BNFGraphTrieEdgesNilV1))))

!(BoundaryConstrainedResult
   (collapse (let (gslt:result:BNFGraphTrieLookupV1:110 WrongValue)
       (gslt:entry:BNFGraphTrieLookupV1:110 (bnf-v1:text-nil) BNFGraphTrieEmptyV1)
       (quote UnexpectedSuccess))))

!(let $a (bnf-v1:text-cons 97 (bnf-v1:text-nil))
   (let $ab (bnf-v1:text-cons 97 (bnf-v1:text-cons 98 (bnf-v1:text-nil)))
     (let $b (bnf-v1:text-cons 98 (bnf-v1:text-nil))
       (let $first (generated-boundary:insert $ab Longer BNFGraphTrieEmptyV1)
         (let $second (generated-boundary:insert $a Prefix $first)
           (let $third (generated-boundary:insert $b Sibling $second)
             (let $fourth (generated-boundary:insert $a Replacement $third)
               (BoundaryPrefixes
                 (collapse (generated-boundary:lookup $a $fourth))
                 (collapse (generated-boundary:lookup $ab $fourth))
                 (collapse (generated-boundary:lookup $b $fourth))
                 (collapse (generated-boundary:lookup (bnf-v1:text-nil) $fourth))))))))))

!(let $key (bnf-v1:text-cons -1 (bnf-v1:text-cons 955 (bnf-v1:text-nil)))
   (BoundaryIntegerKey
     (collapse (generated-boundary:lookup $key
       (generated-boundary:insert $key SignedKey BNFGraphTrieEmptyV1)))))

!(let $head CallerHead
   (let $tail CallerTail
     (let $kept CallerUnrelated
       (let $key (bnf-v1:text-cons 97 (bnf-v1:text-cons 98 (bnf-v1:text-nil)))
         (let $result (generated-boundary:lookup $key
                (generated-boundary:insert $key FreshLocals BNFGraphTrieEmptyV1))
           (quote (BoundaryCaller $head $tail $kept $result)))))))

!(let $payload (quote (+ 20 22))
   (let $result (generated-boundary:lookup (bnf-v1:text-nil)
          (generated-boundary:insert (bnf-v1:text-nil) $payload BNFGraphTrieEmptyV1))
     (quote (BoundaryArithmeticPayload $result))))

!(let $payload (quote (generated-boundary:trigger))
   (let $result (generated-boundary:lookup (bnf-v1:text-nil)
          (generated-boundary:insert (bnf-v1:text-nil) $payload BNFGraphTrieEmptyV1))
     (quote (BoundaryCallablePayload $result))))

!(let $atom Payload
   (let $list (quote (Payload))
     (let $first (generated-boundary:lookup (bnf-v1:text-nil)
            (generated-boundary:insert (bnf-v1:text-nil) $atom BNFGraphTrieEmptyV1))
       (let $second (generated-boundary:lookup (bnf-v1:text-nil)
              (generated-boundary:insert (bnf-v1:text-nil) $list BNFGraphTrieEmptyV1))
         (quote (BoundaryAtomAndList $first $second))))))

; Repeated edges are deliberate component inputs, not public representation policy.
!(let $index
     (BNFGraphTrieV1 BNFIndexMissingV1
       (BNFGraphTrieEdgesConsV1 97
         (BNFGraphTrieV1 (BNFIndexFoundV1 FirstEdge) BNFGraphTrieEdgesNilV1)
         (BNFGraphTrieEdgesConsV1 97
           (BNFGraphTrieV1 (BNFIndexFoundV1 SecondEdge) BNFGraphTrieEdgesNilV1)
           BNFGraphTrieEdgesNilV1)))
   (let $key (bnf-v1:text-cons 97 (bnf-v1:text-nil))
     (BoundaryRepeatedEdges
       (collapse (generated-boundary:lookup $key $index))
       (collapse (generated-boundary:lookup $key
         (generated-boundary:insert $key Replacement $index))))))

!(let $index
     (BNFGraphTrieV1 BNFIndexMissingV1
       (BNFGraphTrieEdgesConsV1 97
         (BNFGraphTrieV1 (BNFIndexFoundV1 FirstEdge) BNFGraphTrieEdgesNilV1)
         (BNFGraphTrieEdgesConsV1 97
           (BNFGraphTrieV1 (BNFIndexFoundV1 SecondEdge) BNFGraphTrieEdgesNilV1)
           BNFGraphTrieEdgesNilV1)))
   (let (BNFGraphTrieV1 $value (BNFGraphTrieEdgesConsV1 $head $first $rest))
        (generated-boundary:insert (bnf-v1:text-cons 97 (bnf-v1:text-nil)) Replacement $index)
     (BoundaryRetainedSecondEdge
       (collapse (generated-boundary:lookup (bnf-v1:text-cons 97 (bnf-v1:text-nil))
         (BNFGraphTrieV1 $value $rest))))))

!(BoundaryReverse
   (collapse (generated-boundary:reverse
     (BNFDefinitionsConsV1 A (BNFDefinitionsConsV1 A
       (BNFDefinitionsConsV1 B BNFDefinitionsNilV1)))
     (BNFDefinitionsConsV1 Z BNFDefinitionsNilV1))))

!(let $name (bnf-v1:text-cons 97 (bnf-v1:text-nil))
   (let $entries
     (bnf-v1:entries-cons (bnf-v1:comment "ignored" (bnf-v1:source-span 0 1))
       (bnf-v1:entries-cons (bnf-v1:rule $name FirstExpression (bnf-v1:source-span 2 3))
         (bnf-v1:entries-cons (bnf-v1:blank (bnf-v1:source-span 3 4))
           (bnf-v1:entries-cons (bnf-v1:rule $name LaterExpression (bnf-v1:source-span 5 6))
             (bnf-v1:entries-cons (bnf-v1:rule (bnf-v1:text-cons 98 (bnf-v1:text-nil))
                 SecondExpression (bnf-v1:source-span 7 8))
               (bnf-v1:entries-cons (bnf-v1:rule $name LaterExpression (bnf-v1:source-span 5 6))
                 (bnf-v1:entries-nil)))))))
     (let (gslt:result:BNFDiscoveryCollectDefinitionsV1:1000 $definitions $diagnostics $index)
          (gslt:entry:BNFDiscoveryCollectDefinitionsV1:1000 $entries)
       (let $lookup (generated-boundary:lookup $name $index)
         (quote (BoundaryCollector $definitions $diagnostics $lookup))))))

!(let (gslt:result:BNFDiscoveryCollectDefinitionsV1:1000 $definitions $diagnostics $index)
      (gslt:entry:BNFDiscoveryCollectDefinitionsV1:1000 (bnf-v1:entries-nil))
   (quote (BoundaryCollectorEmpty $definitions $diagnostics $index)))

; Target calling-convention controls, not BNF implementations. A let result
; constrains the call before once commits; only occurrence bounds license
; changing that scheduling. The independent singleton case must still work.
(= (generated-boundary:choice) (quote first))
(= (generated-boundary:choice) (quote second))
(= (generated-boundary:singleton) (quote second))
!(BoundaryConstraintBeforeOnce
   (collapse (let second (once (generated-boundary:choice)) (quote selected))))
!(BoundarySingletonBeforeOnce
   (collapse (let second (once (generated-boundary:singleton)) (quote selected))))
!(BoundaryIgnoredResultOccurrences
   (collapse (let $_ (generated-boundary:choice) (quote repeated))))
