; Threaded hyperpose select: with more than one worker allowed, a later cheap
; pure branch can satisfy once/select 1 before an earlier recursive branch.

(= (burn $n)
   (if (== $n 0)
       False
       (burn (- $n 1))))

!(pragma! hyperpose-threads 2)
!(assertEqual
  (once (hyperpose ((burn 2000000) True)))
  True)
!(assertEqual
  (select 1 (hyperpose ((burn 2000000) ok)))
  ok)
!(assertEqual
  (once (hyperpose ((Error fair-probe fail) after-error)))
  after-error)
