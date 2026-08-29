; `once` stops after a witness.  It must not by itself commit to this first,
; divergent occurrence before a witness exists on the live sibling.

(= (controller-diversity:once-loop $n)
   (controller-diversity:once-loop (S $n)))
(= (controller-diversity:once-loop $n) (answer $n))

!(once (controller-diversity:once-loop Z))
