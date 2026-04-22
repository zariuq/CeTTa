; match should keep the pattern slot raw in Petta lowering.

(= (f (42) 40.7 2)
   (42.7 42))

(= (myfunc)
   (match &self (= (f (42) 40.7 2) $x) $x))

!(test (reduce (myfunc))
       (42.7 42))
