(: invalid-bool-through-let (-[det]-> Number Bool))
(= (invalid-bool-through-let $input)
   (let $bound $input
     (and $bound True)))
!(invalid-bool-through-let 1)
