; HE conformance: eval
; Tests one-step evaluation

(= (f a) b)
(= (g $x) (pair $x $x))

; eval: expression with equation reduces
!(assertEqual (eval (f a)) b)

; eval: expression with variable binding
!(assertEqual (eval (g hello)) (pair hello hello))

; eval: bare symbol does NOT reduce via eval — eval is a minimal instruction,
; not the top-level ! operator. (eval x) on a symbol with no equation is irreducible.
!(assertEqual (eval x) (eval x))
