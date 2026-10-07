; A revision of the rule space.  A symbol's cell belongs to a world, the
; space with its equations as they are; after a change of the equations a
; demand names a new cell.  A value produced before the change keeps the
; value it had (here the binding of $x), and a demand after it sees the new
; rules: nothing produced in the old world is reused for the new one.
(= c 5)
!(let $x c (let $_ (add-atom &self (= c 6)) (P $x c)))
!(P c c)
; Removing the rule leaves the symbol undefined, so it is itself.
(= d 1)
!(let $x d (let $_ (remove-atom &self (= d 1)) (P $x d)))
; A change of data, not of equations, keeps the world: the second use of
; `c` still shares the first one's choice.
!(P c (let $_ (add-atom &self (fact)) c))
