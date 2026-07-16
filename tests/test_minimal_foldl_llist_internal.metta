; Internal stack-safe fold over the GIC LCons/LNil representation.

(= (llist-sum $items)
   (function
     (chain (context-space) $space
       (eval
         (_minimal-foldl-llist $items 0 $acc $item
           (eval (+ $acc $item)) $space)))))

(= (llist-record-step $target $acc $item)
   (let $added (add-atom $target (seen $item)) $acc))

(= (llist-record $items)
   (let $target (new-space)
     (case
       (chain (context-space) $space
         (eval
           (_minimal-foldl-llist $items True $acc $item
             (llist-record-step $target $acc $item) $space)))
       (((return $result)
          (collapse (match $target (seen $value) $value)))
        ($_ FoldFailed)))))

(= (llist-valid-fold $items)
   (case
     (chain (context-space) $space
       (eval
         (_minimal-foldl-llist $items True $acc $item $acc $space)))
     (((return $result) $result)
      ($_ False))))

!(assertEqual
   (llist-sum (LCons 1 (LCons 2 (LCons 3 LNil))))
   6)
!(assertEqual
   (llist-record (LCons first (LCons second LNil)))
   (first second))
!(assertEqual (llist-valid-fold malformed) False)
