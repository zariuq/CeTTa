(= (tail-stats-loop $n)
   (if (== $n 0)
       done
       (tail-stats-loop (- $n 1))))

!(reset-runtime-stats!)
!(tail-stats-loop 3)
!(bind! &tail-stats (runtime-stats!))
!(assertEqualToResult
  (match &tail-stats (runtime-counter eval-tail-reenter-count $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &tail-stats (runtime-counter eval-arena-live-bytes-peak $n) (> $n 0))
  (True))

!(reset-runtime-stats!)
!(chain 1 $x $x)
!(bind! &chain-stats (runtime-stats!))
!(assertEqualToResult
  (match &chain-stats (runtime-counter eval-chain-call-count $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &chain-stats (runtime-counter eval-chain-single-result-fastpath-count $n) (> $n 0))
  (True))

(= (tail-unify-stats-loop $n $accum)
   (chain (eval (< $n 0)) $done?
     (chain (unify $done?
                   True
                   $accum
                   (chain (eval (- $n 1)) $next
                     (chain (eval (+ $accum 1)) $inc
                       (tail-unify-stats-loop $next $inc))))
            $result
            $result)))

!(reset-runtime-stats!)
!(tail-unify-stats-loop 8 0)
!(bind! &unify-stats (runtime-stats!))
!(assertEqualToResult
  (match &unify-stats (runtime-counter eval-tail-live-binding-entries-peak $n) (< $n 12))
  (True))
