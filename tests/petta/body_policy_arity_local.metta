; Whether an equation's body is held as data is decided by the declaration
; of the equation's own number of arguments.  A declaration of another
; number of arguments says nothing of it.

; A nullary Atom declaration beside a unary numeric one: the nullary body is
; held, the unary body runs.
(: probe (-> Atom))
(: probe (-> Number Number))
(= (probe) (+ 1 2))
(= (probe $n) (+ $n 1))
!(nullary-held (collapse (probe)))
!(unary-runs (collapse (probe 1)))

; The other way round: the unary body is held, the nullary body runs.
(: turned (-> Number))
(: turned (-> Number Atom))
(= (turned) (+ 1 2))
(= (turned $n) (+ $n 1))
!(nullary-runs (collapse (turned)))
!(unary-held (collapse (turned 1)))

; Without the foreign declaration, for comparison.
(: plain (-> Number Number))
(= (plain) (+ 1 2))
(= (plain $n) (+ $n 1))
!(plain-unary (collapse (plain 1)))

; Two declarations of the same number of arguments, one giving an Atom:
; the equations of that number hold their bodies.
(: both (-> Number Atom))
(: both (-> String Number))
(= (both $n) (+ $n 1))
!(same-arity-held (collapse (both 1)))

; An equation of fewer arguments than the call, whose body applies a function
; to fewer arguments than it takes, is that function over the rest.  A
; nullary Atom declaration does not hold the unary body, so it still is.
(= (add2 $x $y) (+ $x $y))
(= (plain-pick $k) (add2 $k))
!(extension-plain (collapse (plain-pick 1 5)))
(: pick (-> Atom))
(= (pick $k) (add2 $k))
!(extension-runs (collapse (pick 1 5)))
