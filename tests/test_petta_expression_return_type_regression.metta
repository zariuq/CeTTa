(: keep-syntax (-> Number Expression Expression))
(= (keep-syntax $a $b)
   (kept $a $b))

!(test
  (keep-syntax (+ 2 4) (+ 4 2))
  (quote (kept 6 (+ 4 2))))
