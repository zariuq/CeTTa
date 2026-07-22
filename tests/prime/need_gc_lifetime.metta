; Prime Need heap lifetime across tail-safe evaluator collection.
;
; Algebraic contract:
;   force(delay(e)) after any sequence of semantically dead tail steps
;   has the same branch-local observations as force(delay(e)) immediately.
;
; Ownership contract:
;   the persistent Need heap belongs to the top-level evaluation episode,
;   never to the resettable evaluator scratch arena.  Under a tiny scratch-GC
;   budget, a correct run need not collect merely because its persistent heap
;   grows; the scratch-owner mutation both creates that false pressure and
;   exposes its frames to reclamation before the final force.

(= (prime-need:gc-payload $n)
   (payload $n
     p00 p01 p02 p03 p04 p05 p06 p07
     p08 p09 p10 p11 p12 p13 p14 p15
     p16 p17 p18 p19 p20 p21 p22 p23
     p24 p25 p26 p27 p28 p29 p30 p31
     p32 p33 p34 p35 p36 p37 p38 p39
     p40 p41 p42 p43 p44 p45 p46 p47
     p48 p49 p50 p51 p52 p53 p54 p55
     p56 p57 p58 p59 p60 p61 p62 p63))

(= (prime-need:gc-force-after-tail $n $promise)
   (if (== $n 0)
       (force $promise)
       (let $dead (prime-need:gc-payload $n)
         ; Equality is strict in its operands.  It forces construction of the
         ; wide payload, while the result carries none of that payload into
         ; the next tail step.
         (if (== $dead $dead)
             (prime-need:gc-force-after-tail (- $n 1) $promise)
             impossible))))

!(assertEqualToResult
  (let $promise (delay (superpose (A B)))
    (prime-need:gc-force-after-tail 1024 $promise))
  (A B))
