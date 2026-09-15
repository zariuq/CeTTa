; A callable declaration blocks the constructor shortcut.
(: LCons (-> Number Atom Atom))
(= (LCons $head $tail) (typed-list $head $tail))
!(LCons 1 LNil)
!(collapse (LCons (superpose (1 2)) LNil))
!(collapse (LCons "bad" LNil))
