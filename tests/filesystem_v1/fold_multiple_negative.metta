; A single-accumulator fold does not silently choose one of two step answers.
(= (fold-example:multiple $items)
  (let $space (context-space)
    (_minimal-foldl-llist $items 0 $acc $item (superpose (1 2)) $space)))
!(fold-example:multiple (LCons first LNil))
