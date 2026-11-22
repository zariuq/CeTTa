; A collection during an evaluation never resets the arena that outlives it.
; The RhoMeTTa command line evaluates each command in its persistent arena,
; where the space keeps the equations it has read.  Here a command reads two
; stored equations and then runs a loop of tail calls long enough to collect;
; the commands after it read the same equations again.  (The unreachable
; effects keep the loop with the evaluator that collects.)
!(import! &self rhometta)
(= (stored-call $x) (Stored $x (stored-inner $x)))
(= (stored-inner $x) (Inner $x))
(= (gc-payload $n) (if (== $n -1) (println! unreachable) (payload $n r00 r01 r02 r03 r04 r05 r06 r07 r08 r09 r10 r11 r12 r13 r14 r15 r16 r17 r18 r19 r20 r21 r22 r23 r24 r25 r26 r27 r28 r29 r30 r31 r32 r33 r34 r35 r36 r37 r38 r39 r40 r41 r42 r43 r44 r45 r46 r47 r48 r49 r50 r51 r52 r53 r54 r55 r56 r57 r58 r59 r60 r61 r62 r63)))
(= (gc-burn $n $acc) (if (== $n -1) (println! unreachable) (if (== $n 0) $acc (gc-burn (- $n 1) (gc-payload $n)))))
!(let $first (stored-call 1) (gc-burn 4096 $first))
!(stored-call 2)
!(stored-call 3)
