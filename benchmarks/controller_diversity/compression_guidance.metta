; A successful branch is oldest and a divergent distractor is newest.  A
; fresh depth-biased policy spends its bounded priority prefix in the
; distractor.  After a generous training run, conditional compression against
; the answer-producing continuation identifies the successful shape on the
; next run; the protected oldest tick remains available independently.

(= (controller-diversity:compression-success Z)
   (answer compression-guided))
(= (controller-diversity:compression-success (S $n))
   (controller-diversity:compression-success $n))

(= (controller-diversity:compression-loop $n)
   (controller-diversity:compression-loop (S $n)))
(= (controller-diversity:compression-loop $n) (empty))

(= (controller-diversity:compression-choice)
   (controller-diversity:compression-success
     (S (S (S (S Z))))))
(= (controller-diversity:compression-choice)
   (controller-diversity:compression-loop Z))

!(controller-diversity:compression-choice)
