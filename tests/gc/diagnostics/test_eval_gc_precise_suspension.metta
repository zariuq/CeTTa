; Diagnostic gate for nested collection licensed by a complete suspension map.
; The HE and Prime let continuations register every moving carrier while a
; recursively evaluated source is suspended.  Each dialect must therefore
; collect more than once instead of retaining the source computation until the
; enclosing form exits.

(= (gc-precise-diagnostic-payload $n)
   (if (== $n -1)
       (println! unreachable-gc-precise-diagnostic-sentinel)
       (payload $n
         r00 r01 r02 r03 r04 r05 r06 r07
         r08 r09 r10 r11 r12 r13 r14 r15
         r16 r17 r18 r19 r20 r21 r22 r23
         r24 r25 r26 r27 r28 r29 r30 r31)))

(= (gc-precise-diagnostic-burn $n $acc)
   (if (== $n -1)
       (println! unreachable-gc-precise-diagnostic-burn-sentinel)
       (if (== $n 0)
           $acc
           (gc-precise-diagnostic-burn
             (- $n 1)
             (gc-precise-diagnostic-payload $n)))))

!(reset-runtime-stats!)

!(let $result (gc-precise-diagnostic-burn 8192 seed)
   (if (== $result
           (payload 1
             r00 r01 r02 r03 r04 r05 r06 r07
             r08 r09 r10 r11 r12 r13 r14 r15
             r16 r17 r18 r19 r20 r21 r22 r23
             r24 r25 r26 r27 r28 r29 r30 r31))
       precise-suspension-done
       precise-suspension-wrong-result))

!(bind! &gc-precise-stats (runtime-stats!))

!(assertEqualToResult
  (match &gc-precise-stats
    (runtime-counter eval-tail-safe-point-count $n)
    (> $n 1))
  (True))

!(assertEqualToResult
  (match &gc-precise-stats
    (runtime-counter eval-tail-reclaimed-bytes $n)
    (> $n 0))
  (True))
