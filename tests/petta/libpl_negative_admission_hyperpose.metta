; Ordinary native heads must be rejected before a worker crosses the optional
; Prolog boundary.  The runtime-stats gate also checks that no worker engine is
; attached while this recursive branch runs.
(= (burn $n)
   (if (== $n 0)
       done
       (burn (- $n 1))))

!(hyperpose ((burn 2000)))
