; A match publishes its rows' template results as one producer: an error
; beside a success is dropped, its effects all happen before its consumer
; runs, and the variables it binds stay bound where its caller sees them.
(op (+ 1 2))
(op (+ 1))
(op (+ 2 2))
!(let $answers (collapse (match &self (op $t) $t)) (sort-atom $answers))
(= (k) (collapse (match &self (op $t) $t)))
!(let $answers (k) (sort-atom $answers))
(= (only-errors) (collapse (match &self (op (+ $a)) (+ $a))))
!(only-errors)

; An effect in the template happens for every row before the consumer reads.
(p 1)
(p 2)
(= (f) (let $r (match &self (p $x) (add-atom &self (q $x)))
          (collapse (match &self (q $y) $y))))
!(let $answers (f) (sort-atom $answers))

; A variable of the call, bound through the callee's match.
(= (h $y) (let $r (match &self (p $y) ok) $y))
!(h $z)
(= (g) (let $r (match &self (p $x) ok) (pair $r $x)))
!(g)

; A root match whose template computes, with no effect anywhere.
(node a)
(node b)
(= (succ a) b)
(= (succ b) c)
!(match &self (node $n) (let $m (succ $n) (step $n $m)))
!(let $answers (collapse (match &self (node $n) (succ $n))) (sort-atom $answers))
