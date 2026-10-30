; A pattern of match, unify or let is syntax, but a Need cell in it is a
; value the pattern consumes: the cell is forced and the pattern matches its
; value.  Syntax written in the pattern stays data, and a cell with several
; values matches once per value.
(fact 1 one)
(fact 2 two)
(fact (+ 1 1) syn)
(fact $any wild)
(pair 2 b)
(pair (+ 1 1) c)
(= (q $x) (match &self (fact $x $y) $y))
(= (qp $x) (match &self (pair $x $y) $y))
(= (qq $x) (quote $x))
(= (u $x) (unify (fact $x $y) (fact 2 two) $y no))
(= (l $x) (let (fact $x $y) (fact 2 two) $y))
(= (either) 1)
(= (either) 2)
; The cell (+ 1 1) is the value 2.
!(q (+ 1 1))
!(qp (+ 1 1))
; unify and let read their patterns the same way.
!(u (+ 1 1))
!(l (+ 1 1))
; One match per value of the cell.
!(q (either))
!(u (either))
; Written syntax in a pattern is not evaluated.
!(match &self (fact (+ 1 1) $y) $y)
; A value needs no forcing; quote keeps its argument as written.
!(q 2)
!(qq (+ 1 1))
; A bound pattern cell can choose, and repeated occurrences share its value
; and committed effects. Other syntax in the same pattern stays data.
(= (lp $x) (let (pair $x $x $y) (pair 2 2 shared) $y))
(= (noisy-two) (let $_ (println! demanded) 2))
!(l (either))
!(lp (either))
!(lp (noisy-two))
!(collapse (let (fact (+ 1 1) $y) (fact 2 two) $y))
; A mismatching cell and a quoted expression do not become fresh binders.
!(collapse (l 1))
!(collapse (l (quote (+ 1 1))))
