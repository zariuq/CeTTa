; Minimal relation-aware if witness distilled from functionhead's h_old form.

(= (myfunc $A $B)
   (append (append (42) $A) $B))

(= (h_old $A $C)
   (if (= $A (myfunc (10) $B))
       ($B $C)
       (empty)))

!(test (h_old (42 10 40) 42000)
       ((40) 42000))

!(test (collapse (h_old (42 11 40) 42000))
       ())
