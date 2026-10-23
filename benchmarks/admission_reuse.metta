; A live argument crosses repeated calls through a pure recursive family.
; The generated size ladder varies the number of distinct relation heads.
(= (admission-left $remaining $shared)
   (case (> $remaining 0)
     ((True (admission-right (- $remaining 1) $shared))
      (False $remaining))))
(= (admission-right $remaining $shared)
   (case (> $remaining 0)
     ((True (admission-left (- $remaining 1) $shared))
      (False $remaining))))
!(admission-left 20 $input)
