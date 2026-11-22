(= (g 1) 2)
(= (g "a") "b")

!(g 1)

(: h (-> Number Number))
(= (h $x) (g $x))

!(h 1)
