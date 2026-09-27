; eval runs its whole term as code, however the term was built: a list the
; term holds, a cons-built one included, is read as the expression it spells.
; reduce dispatches the head of the application written in it on the values
; of its arguments, and a value it holds is not evaluated again.

(item (+ 1 2))

!(match &self (item $h) (cons-atom $h ()))
!(match &self (item $h) (eval (cons-atom $h ())))
!(match &self (item $h) (let $n (cons-atom $h ()) (eval (quote-free $n))))
!(let $t (cons-atom quote-free (((+ 1 2)))) (eval $t))
!(match &self (item $h) (let $n (cons-atom $h ()) (reduce (quote-free $n))))
