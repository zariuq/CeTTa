; An equation whose body begins by destructuring its argument with decons-atom
; has no answer for an argument decons-atom has none for, so once the other
; equation has answered, the call leaves no alternative behind.
(= (cnt ()) 0)
(= (cnt $xs) (let ($h $t) (decons-atom $xs) (+ 1 (cnt $t))))
!(println! (collapse (cnt (a b c))))
!(println! (collapse (cnt ())))
; Two equations that both answer still give two answers.
(= (dup ()) a)
(= (dup $x) a)
!(println! (collapse (dup ())))
; cdr-atom answers () for (): its equation is tried, and both answers stay.
(= (pop ()) ())
(= (pop $s) (cdr-atom $s))
!(println! (collapse (pop ())))
; A body that acts before it destructures is run whole.
(= (visit ()) 0)
(= (visit $xs) (let $_ (println! visited) (let ($h $t) (decons-atom $xs) 1)))
!(println! (collapse (visit ())))
; A value decons-atom answers for keeps the equation; so does a variable.
(= (split ()) none)
(= (split $xs) (let ($h $t) (decons-atom $xs) ($h $t)))
!(println! (collapse (split (x y))))
!(println! (collapse (let $r (split $v) ($v $r))))
(= (loop $n)
   (if (== $n 0) done
     (let $_ (and True (case (cnt (a b c)) ((3 True) ($other False))))
       (loop (- $n 1)))))
!(println! (loop 200))
