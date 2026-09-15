; Candidate component laws, not a normative grammar representation.
; Check observations and answer occurrences, without pinning trie layout.
(= (graph-trie:insert $key $value $trie)
   (let (gslt:result:BNFGraphTrieInsertFirstV1:1110 $after)
        (gslt:entry:BNFGraphTrieInsertFirstV1:1110 $key $value $trie) $after))
(= (graph-trie:lookup $key $trie)
   (let (gslt:result:BNFGraphTrieLookupV1:110 $result)
        (gslt:entry:BNFGraphTrieLookupV1:110 $key $trie) $result))
(= (graph-trie:check $marker $actual $expected)
   (if (== $actual $expected) $marker (GraphTrieDifference $marker $actual $expected)))
(= (graph-trie:append $names $name)
   (let (gslt:result:BNFAppendNameV1:110 $after)
        (gslt:entry:BNFAppendNameV1:110 $names $name) $after))
(= (graph-trie:observe $names)
   (let (gslt:result:BNFIndexedNamesObserveV1:10 $ordered)
        (gslt:entry:BNFIndexedNamesObserveV1:10 $names) $ordered))

!(graph-trie:check GraphTrieEmptyV1
   (collapse (graph-trie:lookup (bnf-v1:text-nil) BNFGraphTrieEmptyV1))
   (BNFIndexMissingV1))

!(let $a (bnf-v1:text-cons 97 (bnf-v1:text-nil))
   (let $ab (bnf-v1:text-cons 97 (bnf-v1:text-cons 98 (bnf-v1:text-nil)))
     (let $b (bnf-v1:text-cons 98 (bnf-v1:text-nil))
       (let $t1 (graph-trie:insert $ab Longer BNFGraphTrieEmptyV1)
         (let $t2 (graph-trie:insert $a Prefix $t1)
           (let $t3 (graph-trie:insert (bnf-v1:text-nil) Root $t2)
             (let $t4 (graph-trie:insert $b Sibling $t3)
               (let $t5 (graph-trie:insert $a Replacement $t4)
                 (graph-trie:check GraphTriePrefixesAndFirstBindingV1
                   (collapse (GraphTrieAnswers
                     (graph-trie:lookup (bnf-v1:text-nil) $t5)
                     (graph-trie:lookup $a $t5)
                     (graph-trie:lookup $ab $t5)
                     (graph-trie:lookup $b $t5)
                     (graph-trie:lookup (bnf-v1:text-cons 99 (bnf-v1:text-nil)) $t5)
                     (graph-trie:lookup (bnf-v1:text-cons 97 (bnf-v1:text-cons 99 (bnf-v1:text-nil))) $t5)))
                   ((GraphTrieAnswers (BNFIndexFoundV1 Root) (BNFIndexFoundV1 Prefix)
                     (BNFIndexFoundV1 Longer) (BNFIndexFoundV1 Sibling)
                     BNFIndexMissingV1 BNFIndexMissingV1)))))))))))

; The name carrier admits character-list operations independently of the
; narrower public BNF identifier spelling profile.
!(let $key (bnf-v1:text-cons 955 (bnf-v1:text-cons 128512 (bnf-v1:text-nil)))
   (graph-trie:check GraphTrieCharacterCarrierV1
     (collapse (graph-trie:lookup $key (graph-trie:insert $key Unicode BNFGraphTrieEmptyV1)))
     ((BNFIndexFoundV1 Unicode))))

!(let $name (bnf-v1:text-cons 97 (bnf-v1:text-nil))
   (let $once (graph-trie:append (BNFIndexedNamesV1 BNFGraphTrieEmptyV1 BNFNamesNilV1) $name)
     (let $twice (graph-trie:append $once $name)
       (graph-trie:check GraphTrieOrderedDuplicatesV1
         (collapse (graph-trie:observe $twice))
         ((BNFNamesConsV1 $name (BNFNamesConsV1 $name BNFNamesNilV1)))))))

; A forged wrapper with a stale index must disagree with list membership.
; This control demonstrates that the index is consumed, not ceremonial data.
!(let $name (bnf-v1:text-cons 97 (bnf-v1:text-nil))
   (graph-trie:check GraphTrieStaleIndexDetectedV1
     (collapse (gslt:entry:BNFNameLookupV1:110 $name
       (BNFIndexedNamesV1 BNFGraphTrieEmptyV1 (BNFNamesConsV1 $name BNFNamesNilV1))))
     ((gslt:result:BNFNameLookupV1:110 BNFNameMissingV1))))
