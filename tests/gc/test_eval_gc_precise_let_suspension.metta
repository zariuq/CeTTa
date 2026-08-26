; A let frame waiting for its source registers both the child and parent
; OutcomeSets as moving roots.  The recursive child may therefore collect at
; nested tail safe points without retaining the whole source evaluation.

(= (gc-precise-let-payload $n)
   (if (== $n -1)
       (println! unreachable-gc-precise-let-sentinel)
       (payload $n
         r00 r01 r02 r03 r04 r05 r06 r07
         r08 r09 r10 r11 r12 r13 r14 r15
         r16 r17 r18 r19 r20 r21 r22 r23
         r24 r25 r26 r27 r28 r29 r30 r31)))

(= (gc-precise-let-burn $n $acc)
   (if (== $n -1)
       (println! unreachable-gc-precise-let-burn-sentinel)
       (if (== $n 0)
           $acc
           (gc-precise-let-burn
             (- $n 1)
             (gc-precise-let-payload $n)))))

!(assertEqual
   (let $result (gc-precise-let-burn 8192 seed)
     $result)
   (payload 1
     r00 r01 r02 r03 r04 r05 r06 r07
     r08 r09 r10 r11 r12 r13 r14 r15
     r16 r17 r18 r19 r20 r21 r22 r23
     r24 r25 r26 r27 r28 r29 r30 r31))
