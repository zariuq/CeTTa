; Recursive subtraction expressed through ordinary authored PeTTa equations.
; The quotient is 70, requiring 70 recursive source-level calls.  This is a
; readable example, not a performance claim.  Use
; scripts/bench_petta_recursive_chain_depth.sh for the depth ladder.

(= (recursive-div $x $y $accum)
   (chain (eval (- $x $y)) $difference
     (chain (eval (< $difference 0)) $finished
       (chain (unify $finished True
                $accum
                (chain (eval (+ 1 $accum)) $next
                  (chain (eval (recursive-div $difference $y $next))
                         $answer $answer)))
              $result $result))))

!(chain (eval (recursive-div 350 5 0)) $answer $answer)
