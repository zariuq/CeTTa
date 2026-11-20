; `repra` of a list cell whose tail is not a list: SWI's term_to_atom writes
; Prolog's partial list [H1,...,Hn|T].  The answers are SWI-PeTTa's; the gate
; runs them in an equation, on the tier and in the machine.

(= (render $x) (repra $x))
!(render (cons 1 2))
!(render (cons 1 (cons 2 3)))
!(render (cons a (cons b ())))
!(render ((cons 1 2) (cons x y)))
!(render (f (cons "s" 2.5) (cons (cons 1 2) z)))
!(repra (cons -1 (cons foo-bar Ab)))
