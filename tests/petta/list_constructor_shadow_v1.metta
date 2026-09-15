; An authored equation must continue to take priority over the value test.
; Keep this separate so it does not change earlier source call classification.
(= (LCons replace $tail) (replaced $tail))
(= (LCons alternatives $tail) (list-value:shadow-first $tail))
(= (LCons alternatives $tail) (list-value:shadow-second $tail))
!(LCons replace LNil)
!(collapse (LCons alternatives LNil))
!(collapse (LCons absent LNil))
