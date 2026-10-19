!(import! &self system)
(= (fold-example:write $items)
  (let $space (context-space)
    (case (_minimal-foldl-llist $items 7 $acc $item
        (let $written (system:write stdout $item) $acc) $space)
      (((return $result) (FoldEffects $result))
       ($fault (FoldRefused $fault))))))
!(fold-example:write (LCons "a" (LCons "b" LNil)))
