; Controls for the decons-atom entry guard: the answers below are the same
; with and without it, on both routes.
; A head variable repeated: the guard reads the first occurrence.
(= (rep () ()) empty)
(= (rep $x $x) (let ($h $t) (decons-atom $x) ($h $t)))
!(println! (collapse (rep () ())))
!(println! (collapse (rep (a) (a))))
!(println! (collapse (rep () (a))))
; The output names the input: decons-atom's answer must equal the argument.
(= (alias ()) zero)
(= (alias $xs) (let $xs (decons-atom $xs) aliased))
!(println! (collapse (alias ())))
!(println! (collapse (alias (a b))))
; An equation added while a call over the relation is suspended.
(= (grow ()) first)
(= (grow $xs) (let ($h $t) (decons-atom $xs) second))
!(println! (collapse (let $r (grow ()) (let $_ (add-atom &self (= (grow ()) added)) $r))))
!(println! (collapse (grow ())))
