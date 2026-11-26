; Hyperpose branches read a space's rows as decoded copies, rewrite stored
; state, and answer with what they read.  A branch's answers are copied out
; as it publishes them, so once the workers have left, the epochs they read
; close and are freed while the program keeps reading, and the answers stay.
!(bind! &rows (new-space))
(= (fill $n)
   (if (<= $n 0) done
       (let $_ (add-atom &rows (row $n (payload $n (part a $n) (part b $n))))
         (fill (- $n 1)))))
!(fill 300)
(= (pick $k) (match &rows (row $k $p) $p))
(= (spin $n $last)
   (if (<= $n 0) $last (spin (- $n 1) (pick (+ 1 (% $n 300))))))
!(sort-atom (collapse (hyperpose ((pick 1) (pick 2) (spin 4000 none) (pick 3)))))
!(bind! &st (new-space))
(= (st-set $k $v)
   (let $_ (collapse (match &st (S $k $old) (remove-atom &st (S $k $old))))
     (add-atom &st (S $k $v))))
(= (rewrite $k $n)
   (if (<= $n 0) (match &st (S $k $v) $v)
       (let $_ (st-set $k (R $n $k (pick (+ 1 (% $n 300))))) (rewrite $k (- $n 1)))))
!(sort-atom (collapse (hyperpose ((rewrite a 3000) (rewrite b 3000) (rewrite c 3000)))))
(= (churn $n $last)
   (if (<= $n 0) $last (churn (- $n 1) (pick (+ 1 (% $n 300))))))
!(churn 30000 none)
!(let $kept (collapse (hyperpose ((pick 4) (spin 3000 none) (pick 5))))
   (let $_ (churn 30000 none) (sort-atom $kept)))
