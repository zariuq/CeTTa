; Builtin names that PeTTa does not register as functions are data in an
; equation body and in an equation head: the expression keeps its head and
; its arguments are evaluated, as for any unregistered head.  Inside a
; `match` query, `,` is still the conjunction of two patterns.
!(, (+ 1 2) x)
!(| (+ 1 2) x)
!(True (+ 1 2))
!(Atom (+ 1 2) (Expression))
!(LNil)
(= (pair-of $x $y) (, $x $y))
!(pair-of (+ 1 2) b)
(= (first-of (, $x $y)) $x)
!(first-of (, a b))
(= (swap (, $x $y)) (, $y $x))
!(swap (swap (, a b)))
(= (sized (, $x $y)) (Sized 2 (, $x $y)))
!(sized (, a (+ 1 1)))
(edge 1 2)
(edge 2 3)
!(match &self (, (edge 1 $m) (edge $m $z)) (, $m $z))
