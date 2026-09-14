; Boolean, aggregation, and function consumers each publish their own live
; continuation roots.  This recursive producer is admitted to the explicit
; Prime equation machine, so those complete roots license moving collection
; without changing any consumer result.  A separate negative gate covers a
; monolithic producer that must remain imprecise.

(= (gc-control-payload $n)
   (if (== $n -1)
       (println! unreachable-gc-control-payload-sentinel)
       (payload $n
         r00 r01 r02 r03 r04 r05 r06 r07
         r08 r09 r10 r11 r12 r13 r14 r15
         r16 r17 r18 r19 r20 r21 r22 r23
         r24 r25 r26 r27 r28 r29 r30 r31)))

(= (gc-control-burn $n $acc)
   (if (== $n -1)
       (println! unreachable-gc-control-burn-sentinel)
       (if (== $n 0)
           $acc
           (gc-control-burn
             (- $n 1)
             (gc-control-payload $n)))))

; The condition itself must be the allocating call.  Wrapping it in another
; strict grounded operation would exercise that operation's argument frame
; instead of isolating the Boolean continuation boundary.
(= (gc-control-burn-boolean $n $acc)
   (if (== $n -1)
       (println! unreachable-gc-control-boolean-sentinel)
       (if (== $n 0)
           True
           (gc-control-burn-boolean
             (- $n 1)
             (gc-control-payload $n)))))

!(reset-runtime-stats!)

!(if (gc-control-burn-boolean 8192 seed)
     boolean-suspension-done
     boolean-suspension-wrong-result)

!(bind! &gc-boolean-stats (runtime-stats!))

!(assertEqualToResult
  (match &gc-boolean-stats
    (runtime-counter eval-tail-safe-point-count $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &gc-boolean-stats
    (runtime-counter eval-tail-reclaimed-bytes $n)
    (> $n 0))
  (True))

!(reset-runtime-stats!)

!(let $observed (collapse (gc-control-burn 8192 seed))
   (if (== $observed
           ((payload 1
              r00 r01 r02 r03 r04 r05 r06 r07
              r08 r09 r10 r11 r12 r13 r14 r15
              r16 r17 r18 r19 r20 r21 r22 r23
              r24 r25 r26 r27 r28 r29 r30 r31)))
       aggregation-suspension-done
       aggregation-suspension-wrong-result))

!(bind! &gc-aggregation-stats (runtime-stats!))

!(assertEqualToResult
  (match &gc-aggregation-stats
    (runtime-counter eval-tail-safe-point-count $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &gc-aggregation-stats
    (runtime-counter eval-tail-reclaimed-bytes $n)
    (> $n 0))
  (True))

; `function` retains its body and the inner occurrence bag while the body is
; evaluated.  The same generated outcome-continuation protocol licenses the
; admitted explicit producer without changing the returned value.
!(reset-runtime-stats!)

!(function
  (chain (gc-control-burn 8192 seed) $observed
    (if (== $observed
            (payload 1
              r00 r01 r02 r03 r04 r05 r06 r07
              r08 r09 r10 r11 r12 r13 r14 r15
              r16 r17 r18 r19 r20 r21 r22 r23
              r24 r25 r26 r27 r28 r29 r30 r31))
        (return function-suspension-done)
        (return function-suspension-wrong-result))))

!(bind! &gc-function-stats (runtime-stats!))

!(assertEqualToResult
  (match &gc-function-stats
    (runtime-counter eval-tail-safe-point-count $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &gc-function-stats
    (runtime-counter eval-tail-reclaimed-bytes $n)
    (> $n 0))
  (True))
