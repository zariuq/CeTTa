; Component observations for the generated dependency-local queue.
; Numeric decoding is an independent test observer, not the implementation.
(= (discovery-test:value BNFDiscoveryRankZeroV1) 0)
(= (discovery-test:value (BNFDiscoveryRankOneV1 $rank))
   (+ (* 2 (discovery-test:value $rank)) 1))
(= (discovery-test:value (BNFDiscoveryRankTwoV1 $rank))
   (+ (* 2 (discovery-test:value $rank)) 2))
(= (discovery-test:next $rank)
   (let (gslt:result:BNFDiscoveryRankNextV1:10 $next)
        (gslt:entry:BNFDiscoveryRankNextV1:10 $rank) $next))
(= (discovery-test:rank $number)
   (if (== $number 0) BNFDiscoveryRankZeroV1
     (discovery-test:next (discovery-test:rank (- $number 1)))))
(= (discovery-test:compare $left $right)
   (let (gslt:result:BNFDiscoveryRankCompareV1:110 $result)
        (gslt:entry:BNFDiscoveryRankCompareV1:110 $left $right) $result))
(= (discovery-test:check $marker $actual $expected)
   (if (== $actual $expected) $marker (DiscoveryComponentDifference $marker $actual $expected)))
(= (discovery-test:rank-sequence $remaining $number $rank)
   (if (== $remaining 0) True
     (if (== (discovery-test:value $rank) $number)
       (discovery-test:rank-sequence (- $remaining 1) (+ $number 1) (discovery-test:next $rank)) False)))
(= (discovery-test:compare-row $left $right)
   (if (== $right 16) True
     (if (== (discovery-test:compare (discovery-test:rank $left) (discovery-test:rank $right))
             (if (< $left $right) BNFDiscoveryLessV1
               (if (== $left $right) BNFDiscoveryEqualV1 BNFDiscoveryGreaterV1)))
       (discovery-test:compare-row $left (+ $right 1)) False)))
(= (discovery-test:compare-rows $left)
   (if (== $left 16) True
     (if (discovery-test:compare-row $left 0)
       (discovery-test:compare-rows (+ $left 1)) False)))

(= (discovery-test:merge $left $right)
   (let (gslt:result:BNFDiscoveryHeapMergeV1:110 $result)
        (gslt:entry:BNFDiscoveryHeapMergeV1:110 $left $right) $result))
(= (discovery-test:combine $forest)
   (let (gslt:result:BNFDiscoveryHeapCombineV1:10 $result)
        (gslt:entry:BNFDiscoveryHeapCombineV1:10 $forest) $result))
(= (discovery-test:node $rank $label)
   (BNFDiscoveryDefinitionV1 (discovery-test:rank $rank) $label TestExpression TestSpan))
(= (discovery-test:insert $rank $label $heap)
   (discovery-test:merge
     (BNFDiscoveryHeapV1 (discovery-test:node $rank $label) BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1) $heap))
(= (discovery-test:drain BNFDiscoveryHeapNilV1) DiscoveryItemsNil)
(= (discovery-test:drain
     (BNFDiscoveryHeapV1 (BNFDiscoveryDefinitionV1 $rank $label $expression $span)
       $children BNFDiscoveryHeapNilV1))
   (DiscoveryItemsCons (DiscoveryItem (discovery-test:value $rank) $label)
     (discovery-test:drain (discovery-test:combine $children))))

!(discovery-test:check DiscoveryRankSequenceV1
   (collapse (discovery-test:rank-sequence 65 0 BNFDiscoveryRankZeroV1)) (True))
!(discovery-test:check DiscoveryRankAllPairsV1
   (collapse (discovery-test:compare-rows 0)) (True))
!(discovery-test:check DiscoveryRankOuterDigitNegativeV1
   (collapse (discovery-test:compare (BNFDiscoveryRankTwoV1 BNFDiscoveryRankZeroV1)
     (BNFDiscoveryRankOneV1 (BNFDiscoveryRankOneV1 BNFDiscoveryRankZeroV1))))
   (BNFDiscoveryLessV1))
!(let $heap (discovery-test:insert 7 G
     (discovery-test:insert 2 Duplicate
       (discovery-test:insert 3 C
         (discovery-test:insert 0 A
           (discovery-test:insert 2 Duplicate BNFDiscoveryHeapNilV1)))))
   (discovery-test:check DiscoveryHeapOrderedDuplicatesV1
     (collapse (discovery-test:drain $heap))
     ((DiscoveryItemsCons (DiscoveryItem 0 A)
       (DiscoveryItemsCons (DiscoveryItem 2 Duplicate)
         (DiscoveryItemsCons (DiscoveryItem 2 Duplicate)
           (DiscoveryItemsCons (DiscoveryItem 3 C)
             (DiscoveryItemsCons (DiscoveryItem 7 G) DiscoveryItemsNil))))))))
!(let $forest
     (BNFDiscoveryHeapV1 (discovery-test:node 3 C) BNFDiscoveryHeapNilV1
       (BNFDiscoveryHeapV1 (discovery-test:node 0 A) BNFDiscoveryHeapNilV1
         (BNFDiscoveryHeapV1 (discovery-test:node 2 B) BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1)))
   (discovery-test:check DiscoveryHeapCombineSiblingsV1
     (collapse (discovery-test:drain (discovery-test:combine $forest)))
     ((DiscoveryItemsCons (DiscoveryItem 0 A)
       (DiscoveryItemsCons (DiscoveryItem 2 B)
         (DiscoveryItemsCons (DiscoveryItem 3 C) DiscoveryItemsNil))))))
; Merge alone is not a valid replacement for combine on a sibling forest.
!(let $forest
     (BNFDiscoveryHeapV1 (discovery-test:node 3 C) BNFDiscoveryHeapNilV1
       (BNFDiscoveryHeapV1 (discovery-test:node 0 A) BNFDiscoveryHeapNilV1 BNFDiscoveryHeapNilV1))
   (discovery-test:check DiscoveryHeapWrongMergeDetectedV1
     (== (discovery-test:drain (discovery-test:merge $forest BNFDiscoveryHeapNilV1))
         (discovery-test:drain (discovery-test:combine $forest))) False))

(= (discovery-test:put $key $value $trie)
   (let (gslt:result:BNFGraphTriePutV1:1110 $after)
        (gslt:entry:BNFGraphTriePutV1:1110 $key $value $trie) $after))
(= (discovery-test:lookup $key $trie)
   (let (gslt:result:BNFGraphTrieLookupV1:110 $result)
        (gslt:entry:BNFGraphTrieLookupV1:110 $key $trie) $result))
!(let $prefix (bnf-v1:text-cons 97 (bnf-v1:text-nil))
   (let $longer (bnf-v1:text-cons 97 (bnf-v1:text-cons 98 (bnf-v1:text-nil)))
     (let $trie (discovery-test:put $prefix New
       (discovery-test:put $longer Unchanged
         (discovery-test:put $prefix Old BNFGraphTrieEmptyV1)))
       (discovery-test:check DiscoveryTrieReplacementV1
         (collapse (DiscoveryLookups (discovery-test:lookup $prefix $trie)
           (discovery-test:lookup $longer $trie) (discovery-test:lookup (bnf-v1:text-nil) $trie)))
         ((DiscoveryLookups (BNFIndexFoundV1 New) (BNFIndexFoundV1 Unchanged) BNFIndexMissingV1))))))
