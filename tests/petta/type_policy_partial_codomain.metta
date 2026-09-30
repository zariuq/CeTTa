(: number-partial (-> Number Number Number))
(= (number-partial $x $y) (+ $x $y))
!(collapse (number-partial 2))
