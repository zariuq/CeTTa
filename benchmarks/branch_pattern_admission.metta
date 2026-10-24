; A tuple binder shared textually by two exclusive branches.
(branch-pattern-input (10 3))
(= (branch-pattern-step $flag $pair)
   (if (== $flag add)
       (let ($left $right) $pair (+ $left $right))
       (let ($left $right) $pair (- $left $right))))
(= (branch-pattern-repeat $n $flag $sum)
   (if (== $n 0) $sum
       (let $next (match &self (branch-pattern-input $pair) (branch-pattern-step $flag $pair))
          (branch-pattern-repeat (- $n 1) $flag (+ $sum $next)))))
!(branch-pattern-repeat 1000 $flag 0)
!(branch-pattern-repeat 10000 $flag 0)
!(branch-pattern-repeat 100000 $flag 0)
