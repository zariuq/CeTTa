!(tabled (bagged $x))
(= (bagged one) same)
(= (bagged one) same)
!(collapse (bagged one))

(= (plain one) same)
(= (plain one) same)
!(collapse (plain one))

!(tabled (terminating-loop $x))
(= (terminating-loop $x) (terminating-loop $x))
!(collapse (terminating-loop one))

!(tabled (alpha-bag $x))
(= (alpha-bag left) one)
(= (alpha-bag right) two)
(= (alpha-pair)
   (superpose ((alpha-bag $left)
               (alpha-bag $right))))
!(collapse (alpha-pair))

!(tabled not-a-call)
