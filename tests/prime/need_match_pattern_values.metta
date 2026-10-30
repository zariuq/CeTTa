; A pattern of match, unify or let is syntax, but a Need cell in it is a
; value the pattern consumes when explicitly captured from its caller. The
; cell is forced and the pattern matches its value. Written syntax stays data,
; and a cell with several values matches once per value. An uncaptured pattern
; name introduces a fresh lexical binder.
(fact 1 one)
(fact 2 two)
(fact (+ 1 1) syn)
(fact $any wild)
(pair 2 b)
(pair (+ 1 1) c)
(= (q $x) (match &self (fact $x $y) $y){$x})
(= (qp $x) (match &self (pair $x $y) $y){$x})
(= (qq $x) (quote $x){$x})
(= (u $x) (unify (fact $x $y) (fact 2 two) $y no))
(= (l $x) (let (fact $x $y) (fact 2 two) $y){$x})
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
; A value needs no forcing.  A quotation's crossing name denotes what its
; parameter's type says, as a pattern's captured name does: the value 2 for
; a value parameter, the syntax written for a `Atom` one.
!(q 2)
!(qq (+ 1 1))
(: qq-syntax (-> Atom Proc))
(= (qq-syntax $x) (quote $x){$x})
!(qq-syntax (+ 1 1))

; Negative controls: the same spelling without capture is fresh, so it does
; not constrain the stored row to the caller's argument.
(= (q-fresh $x) (match &self (fact $x $y) $y))
(= (qp-fresh $x) (match &self (pair $x $y) $y))
!(q-fresh (+ 1 1))
!(qp-fresh (+ 1 1))

; A pattern also reads an already completed cell through nested calls.
; Reading the ready value does no new work, and an absent head still misses.
(: ready-pattern-parent (-> Person (-> Person Bool)))
(= (ready-pattern:empty-entry $p) none)
(= (ready-pattern:lookup (: (quote $p) $t))
   (let $entry (ready-pattern:empty-entry $p)
     (if (== $entry none)
         (match &self (: $p $t) $t){$p $t}
         absent)))
(= (ready-pattern:probe $term)
   (unify $term (quote ($r $u $v))
     (ready-pattern:lookup (: @$r (-> $a (-> $b Bool)))) no))
!(collapse (ready-pattern:probe @(ready-pattern-parent A A)))
!(collapse (ready-pattern:probe @(ready-pattern-missing A A)))
