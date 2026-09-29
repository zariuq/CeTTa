; foldl over a relation, which the open-equation tier runs as foldl/4's
; clauses, and errors inside calls dispatched at run time, which fail that
; path alone on the tier as in the machine (swi_differences.metta, 1).  The
; gate runs it on both routes.

; foldl threads the accumulator element first over flat lists, cons cells
; and the empty list; a step's answers branch, and a failing step prunes.
(= (nest $item $acc) (pair $item $acc))
(= (fold-flat $l) (foldl nest $l z))
!(fold-flat (a b c))
!(fold-flat ())
(= (fold-cells $x) (foldl nest (cons $x (cons b ())) z))
!(fold-cells a)
(= (branch $item $acc) (superpose ((left $item $acc) (right $item $acc))))
(= (fold-branching $l) (foldl branch $l z))
!(msort (collapse (fold-branching (1 2))))
(= (only-a a $acc) (kept $acc))
(= (fold-pruned $l) (foldl only-a $l z))
!(collapse (fold-pruned (a b)))
!(collapse (fold-pruned (a a)))
(= (fold-non-list $x) (foldl nest $x z))
!(collapse (fold-non-list foo))

; A functional queue threaded through foldl in a loop.
(= (enqueue $e (queue $in $out $n)) (queue (cons $e $in) $out (+ $n 1)))
(= (dequeue $e (queue $in (cons $e $out) $n)) (queue $in $out (- $n 1)))
(= (dequeue $e (queue $in () $n))
   (let (cons $e $r) (reverse $in) (queue () $r (- $n 1))))
(= (churn $q $k $sum)
   (if (< $k 1)
       $sum
       (let* (($q1 (once (dequeue $x $q)))
              ($q2 (foldl enqueue ($k (+ $k 1000)) $q1)))
         (churn $q2 (- $k 1) (+ $sum $x)))))
(= (start-churn $k) (churn (enqueue 0 (queue () () 0)) $k 0))
!(start-churn 200)

; An error raised in a step fails that step's path: here every path.
(= (divide $item $acc) (/ $acc $item))
(= (fold-divide $l) (foldl divide $l 60))
!(collapse (fold-divide (2 3)))
!(collapse (fold-divide (2 0 3)))

; A dynamic application the tier enters recovers its callee's error.
(= (bad $x) (/ $x 0))
(= (good $x) (+ $x 1))
(= (apply-it $f $v) ($f $v))
(= (both $v) (superpose ((apply-it bad $v) (apply-it good $v))))
!(collapse (both 1))

; So does a goal the tier hands to the machine inside such a call.
(= (sorter $x) (msort $x))
(= (keep $x) $x)
(= (both-sorted $v) (superpose ((apply-it sorter $v) (apply-it keep $v))))
!(collapse (both-sorted 5))
!(msort (collapse (both-sorted (b a))))

; A direct call is no dispatch: its error is raised.
(= (direct $x) (/ $x 0))
!(catch (collapse (direct 1)))

; After the program changes, a fold keeps running its own clauses and calls
; the step's equations current then.
(= (step-v $item $acc) (+ $acc $item))
(= (grow-then-fold $l)
   (let $unused (add-atom &self (= (extra) 1)) (foldl step-v $l 0)))
!(grow-then-fold (1 2 3))
