; Lets whose destination certainly meets what their value reaches, or
; prunes only call-free work, compute their values first.
(= (step $a $b) (pair $b (% (+ $a $b) 1000007)))
(= (fib-loop $n $a $b)
   (if (== $n 0) $a (let (pair $x $y) (step $a $b) (fib-loop (- $n 1) $x $y))))
!(fib-loop 2000 0 1)
(= (lookup $k Nil) Nothing)
(= (lookup $k (Cons (Pair $k2 $v) $rest))
   (if (== $k $k2) (Just $v) (lookup $k $rest)))
(= (sum-found $ks $t $acc)
   (if (== $ks Nil) $acc
       (let (Cons $k $more) $ks
         (let (Just $v) (lookup $k $t) (sum-found $more $t (+ $acc $v))))))
!(sum-found (Cons b (Cons a Nil)) (Cons (Pair a 1) (Cons (Pair b 2) Nil)) 0)
!(collapse (sum-found (Cons z Nil) (Cons (Pair a 1) Nil) 0))
(= (order $a $b) (if (< $a $b) (MM $a $b) (MM $b $a)))
(= (sum-mins $n $acc)
   (if (== $n 0) $acc
       (let (MM $lo $hi) (order $n (% (* $n 37) 101)) (sum-mins (- $n 1) (+ $acc $lo)))))
!(sum-mins 500 0)
