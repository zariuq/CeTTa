; Basic equation rewriting
(= (f a) b)
(= (g $x) (pair $x $x))

; Simple equation lookup
!(f a)

; Equation with variable binding
!(g hello)

; No matching equation — symbol returned as-is
!(f b)
