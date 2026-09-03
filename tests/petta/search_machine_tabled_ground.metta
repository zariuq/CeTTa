!(import! &self (library lib_tabling))

!(tabled (bagged $x))
(= (bagged one) same)
(= (bagged one) same)
!(collapse (bagged one))

(= (plain one) same)
(= (plain one) same)
!(collapse (plain one))

!(tabled (terminating_loop $x))
(= (terminating_loop $x) (terminating_loop $x))
!(collapse (terminating_loop one))

!(tabled (alpha_bag $x))
(= (alpha_bag left) one)
(= (alpha_bag right) two)
(= (alpha-pair)
   (superpose ((alpha_bag $left)
               (alpha_bag $right))))
!(collapse (alpha-pair))
