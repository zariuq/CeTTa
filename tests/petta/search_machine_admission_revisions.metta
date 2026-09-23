; How a relation call inside an equation body is dispatched depends on
; program and library facts that may change while the program runs.  Each
; relation below is called from a body before and after such a change, and
; every answer must be the one the changed program gives.
!(import! &self (library lib_tabling))
!(import! &self (library lib_memo))

; Declaring a relation tabled after it has been called.
(= (admission_dup one) same)
(= (admission_dup one) same)
(= (admission-dup-caller) (admission_dup one))
!(collapse (admission-dup-caller))
!(tabled (admission_dup $x))
!(collapse (admission-dup-caller))

; Memoizing a relation after it has been called.
(= (admission-square $x) (* $x $x))
(= (admission-square-caller) (admission-square 7))
!(admission-square-caller)
!(memoize admission-square)
!(admission-square-caller)
!(admission-square-caller)

; Adding an equation of a second arity after the first was called.
(= (admission-grow $x) (item $x))
(= (admission-grow-caller) (admission-grow 1))
!(collapse (admission-grow-caller))
(= (admission-grow $x $y) (pair $x $y))
!(collapse (admission-grow-caller))
!(collapse (admission-grow 1 2))
