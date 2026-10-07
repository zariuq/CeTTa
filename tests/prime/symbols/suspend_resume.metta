; A search suspended after its first answer and then resumed gives the
; answers of the uninterrupted run, with the same shared choices: the first
; answer, then the residual with exactly the rest.
(= coin heads)
(= coin tails)
!(let (Observation $all $s $c $r $w $t)
   (select 9 (search-policy (control FIFO (retain True))) (P coin coin))
   $all)
!(let (Observation $prefix $s $c (residual $r) $w $t)
   (select 1 (search-policy (control FIFO (retain True))) (P coin coin))
   (let (Observation $rest $s2 $c2 $r2 $w2 $t2)
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
     (Pair $prefix $rest)))
; The same for a prefix of a cyclic symbol with two alternatives: the choice
; made before the suspension holds after it.
(= bits (Cons 0 bits))
(= bits (Cons 1 bits))
(= (take 0 $s) Nil)
(= (take $n (Cons $h $t)) (if (> $n 0) (Cons $h (take (- $n 1) $t)) (empty)))
!(let (Observation $all $s $c $r $w $t)
   (select 9 (search-policy (control FIFO (retain True))) (take 3 bits))
   $all)
!(let (Observation $prefix $s $c (residual $r) $w $t)
   (select 1 (search-policy (control FIFO (retain True))) (take 3 bits))
   (let (Observation $rest $s2 $c2 $r2 $w2 $t2)
     (select 0 (search-policy (control FIFO (demand all) (retain True))) $r)
     (Pair $prefix $rest)))
