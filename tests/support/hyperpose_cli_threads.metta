; CLI-only fixture: --hyperpose-threads 2 enables the same threaded select path
; as the pragma, without putting scheduling policy in source.

(= (burn $n)
   (if (== $n 0)
       False
       (burn (- $n 1))))

!(assertEqual
  (once (hyperpose ((burn 2000000) cli-ok)))
  cli-ok)
