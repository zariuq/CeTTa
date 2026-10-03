; A symbol is a constant with no arguments.  A symbol the program defines
; evaluates by its definition, as a request and as an argument; a symbol it
; does not define stays itself, never empty and never an error.  A request
; for `c` is written `! c`: `!c` is one symbol, `!c`, stored like any other
; atom, as HE's reader takes it.

; Undefined symbols are data, as requests and as arguments.
(= (wrap $x) (got $x))
! c
!(wrap c)
!(f c)
(: k T)
! k
!(wrap k)
!c
!(match &self !c stored)

; A symbol defined by an equation evaluates, as a request and as an argument.
(= d 5)
! d
!(wrap d)
!(+ d 1)
(= e d)
! e
(: g Number)
(= g 7)
(: inc (-> Number Number))
(= (inc $x) (+ $x 1))
! g
!(inc g)
; Two equations give two values.
(= two-ways first)
(= two-ways second)
!(let $values (collapse two-ways) (sort-atom $values))
; A symbol defined to produce nothing produces nothing, as a request and as
; an argument.
(= none (empty))
!(collapse none)
!(collapse (wrap none))
; A symbol whose equation gives the symbol itself is its own value.
(= fixed fixed)
! fixed
; Where an atom is held as written, the symbol stays as written, and
; evaluating it explicitly runs its definition.
(: hold-it (-> (Data Atom) (Data Atom)))
(= (hold-it $x) $x)
!(hold-it d)
!(quote d)
!(eval d)
!(match &self (= d $value) $value)
; A function whose value is a defined symbol gives the symbol's value.
(= (give-d) d)
!(give-d)
; A call with no arguments is another term: `(d)` is not `d`.
(= (call0) 5)
!(call0)
!(wrap call0)

; A constant declared by `set:define` runs, is typed and is reasoned about:
; the kernel unfolds it when it compares terms, and running it unfolds it in
; the same way, one firing.
!(set:inductive &self nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(set:define &self two nat (= two (succ (succ zero))))
!(set:define &self dbl (-> nat nat) (= (dbl zero) zero) (= (dbl (succ $n)) (succ (succ (dbl $n)))))
! two
!(dbl two)
!(type:eq (dbl two) (succ (succ (succ (succ zero)))))
!(cost:firings two)
!(cost:firings (dbl two))
!(set:define &self four nat (= four (dbl two)))
! four
!(type:eq four (succ (succ (succ (succ zero)))))
; A constructor is not a definition: it stays itself.
! zero
!(dbl zero)
; Negative: the kernel and running agree that two is not four.
!(type:eq two four)
