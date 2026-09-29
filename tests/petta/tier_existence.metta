; Whether a match has a row, observed as (== () (collapse (once (match ...)))):
; a membership question inside relations, over a named space, the program's
; own space, patterns with and without variables, and either side of ==.

(= (seen-or-add $space $x)
   (let $st (s $x)
        (if (== () (collapse (once (match $space $st True))))
            (add-atom $space $st)
            (empty))))
(= (probe $x) (collapse (seen-or-add &dup $x)))

!(probe a)
!(probe a)
!(probe (b 1))
!(probe (b 1))
!(probe (b 2))
!(collapse (match &dup $y $y))

; The collection on the left.
(= (known $x) (if (== (collapse (once (match &dup (s $x) yes))) ()) no yes))
!(known a)
!(known c)

; A pattern with a variable.
(= (any-b) (if (== () (collapse (once (match &dup (s (b $n)) True)))) none some))
!(any-b)

; The program's own space.
(fact 1)
(= (has-fact $n) (== () (collapse (once (match &self (fact $n) True)))))
!(has-fact 1)
!(has-fact 2)

; A template that is not a constant stays whole.
(= (first-b) (collapse (once (match &dup (s (b $n)) $n))))
!(first-b)
