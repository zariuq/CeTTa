; A deep constructor spine separates checkpoint traffic from flat arity.
(= (nested-match
      (Nested (Left $a (Leaf $b))
              (Right (Pair $c $d) (Leaf $e))))
   ok)
(= (nested-run $n $a $b $c $d $e)
   (if (== $n 0)
       nested-done
       (let $_
         (nested-match
           (Nested (Left $a (Leaf $b))
                   (Right (Pair $c $d) (Leaf $e))))
         (nested-run (- $n 1) $a $b $c $d $e))))

!(nested-run 50000 $a $b $c $d $e)
