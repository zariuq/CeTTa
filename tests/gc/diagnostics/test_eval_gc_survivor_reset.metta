; Diagnostic gate for form-boundary tail-safe GC survivor reset.
;
; This file intentionally depends on compile-time runtime stats and is run by
; the dedicated test-eval-gc-survivor-reset target with a tiny GC budget.

(= (gc-reset-payload $n)
   (payload $n
     r00 r01 r02 r03 r04 r05 r06 r07
     r08 r09 r10 r11 r12 r13 r14 r15
     r16 r17 r18 r19 r20 r21 r22 r23
     r24 r25 r26 r27 r28 r29 r30 r31
     r32 r33 r34 r35 r36 r37 r38 r39
     r40 r41 r42 r43 r44 r45 r46 r47
     r48 r49 r50 r51 r52 r53 r54 r55
     r56 r57 r58 r59 r60 r61 r62 r63))

(= (gc-reset-burn $n $acc)
   (if (== $n 0)
       reset-done
       (gc-reset-burn (- $n 1) (gc-reset-payload $n))))

!(reset-runtime-stats!)

!(assertEqual (gc-reset-burn 1024 seed) reset-done)
!(assertEqual (gc-reset-burn 1024 seed) reset-done)
!(assertEqual (gc-reset-burn 1024 seed) reset-done)

!(bind! &gc-reset-stats (runtime-stats!))

!(assertEqualToResult
  (match &gc-reset-stats
    (runtime-counter eval-tail-safe-point-count $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &gc-reset-stats
    (runtime-counter eval-tail-survivor-reset-count $n)
    (> $n 1))
  (True))

!(assertEqualToResult
  (match &gc-reset-stats
    (runtime-counter eval-tail-survivor-reset-bytes $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &gc-reset-stats
    (runtime-counter eval-tail-survivor-arena-live-bytes-peak $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &gc-reset-stats
    (runtime-counter eval-tail-survivor-arena-reserved-bytes-peak $n)
    (> $n 0))
  (True))
