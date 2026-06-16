; A user-defined pure head ending in ! must not be screened out of threaded
; hyperpose just because of its spelling. With more than one worker available,
; the cheap branch may win the once race.

(= (burn! $n)
   (if (== $n 0)
       False
       (burn! (- $n 1))))

!(pragma! num-threads 2)
!(once (hyperpose ((burn! 500000) bang-ok)))
