(= (fold-example:depth $remaining)
  (if (== $remaining 0) 7
    (let $child (fold-example:depth (- $remaining 1)) (+ $child 0))))
(= (fold-example:deep $items)
  (let $space (context-space)
    (case (_minimal-foldl-llist $items 0 $acc $item
        (fold-example:depth $item) $space)
      (((return $result) (FoldDeepCallback $result))
       ($fault (FoldRefused $fault))))))
!(fold-example:deep (LCons 800 (LCons 800 LNil)))
