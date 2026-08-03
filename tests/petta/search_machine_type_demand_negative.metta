(: typed-pair (-> Number Expression Expression))
(: typed-pair (-> Number Expression Expression))
(: typed-pair (-> Number Expression Expression Extra))

(= (typed-pair $strict $syntax)
   (pair $strict $syntax))

!(typed-pair (+ 2 4) (+ 4 2))
