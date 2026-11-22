(= (g 1) 2)
(= (g "a") "b")

!(g 1)

(: h (-> Number String))
(= (h $x) (g $x))

!(h 1)
