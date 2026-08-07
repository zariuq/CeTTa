(: bool-through-let (-[det]-> Bool Bool))
(= (bool-through-let $input)
   (let $bound $input
     (and $bound True)))
!(bool-through-let True)
