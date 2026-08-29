; Native finite-prefix demand is transmitted to the relational frontier.
; The recursive-first occurrence needs a recurrent-oldest schedule, while
; the enclosing `select 2` must stop the episode after exactly two answers.

(= (controller-diversity:prefix-loop $n)
   (controller-diversity:prefix-loop (S $n)))
(= (controller-diversity:prefix-loop $n) (answer $n))

!(select 2 (controller-diversity:prefix-loop Z))
