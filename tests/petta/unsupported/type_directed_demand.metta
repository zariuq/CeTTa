(: typed-pair (-> Number Atom Expression))
(= (typed-pair $strict $syntax)
   (pair $strict $syntax))

!(typed-pair (+ 2 4) (+ 4 2))
