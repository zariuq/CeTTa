; A call that answers and keeps an untried equation is suspended until a
; backtrack resumes it.  What it keeps must be enough to resume it however
; long it waits and whatever runs meanwhile: its answers, duplicates and
; effects are those of an uninterrupted enumeration.

; A closed call whose second equation stays pending after the first answer.
(= (pick ()) a)
(= (pick $x) b)
; The pending call is inside its caller's continuation, which keeps x.
(= (outer $x) (let $y (pick $x) (pair $x $y)))
; Distinct equations answering alike: both answers stay.
(= (dup 1) same)
(= (dup $x) same)
; Each equation's effect happens when its alternative runs.
(= (noisy ()) (let $_ (println! noisy-first) one))
(= (noisy $x) (let $_ (println! noisy-second) two))
; An open call: each answer binds the caller's variable.
(= (color red) warm)
(= (color blue) cool)
(= (color green) cool)
; Work between an answer and the backtrack that resumes its call: a loop
; leaving one alternative per iteration, which allocates enough for the
; machine to collect its heap while the call waits.
(= (tick) (and True (case (pick ()) ((a True)))))
(= (work $n) (if (== $n 0) done (let $_ (tick) (work (- $n 1)))))
; An open call whose body waits for the host's effect between its answers.
(= (loud $c) (let $_ (println! $c) $c))
(= (hot $x) (let $c (color $x) (loud $c)))
; Every suspended call of a recursion is resumed after the deeper ones.
(= (closed-run $n $acc)
   (if (== $n 0) $acc (let $v (pick ()) (closed-run (- $n 1) ($v $acc)))))
(= (open-run $n $acc)
   (if (== $n 0) $acc (let $c (color $x) (open-run (- $n 1) ($x $acc)))))

!(collapse (pick ()))
!(collapse (outer ()))
!(collapse (dup 1))
!(collapse (noisy ()))
!(collapse (let $c (color $x) (pair $x $c)))
!(collapse (let $y (pick ()) (pair $y (work 3000))))
!(collapse (let $c (color $x) (pair $x (work 3000))))
!(collapse (hot $x))
!(collapse (let $c (hot $x) (pair $x (work 2000))))
!(collapse (closed-run 3 ()))
!(collapse (open-run 2 ()))
!(collapse (once (pick ())))
!(collapse (once (color $x)))
!(collapse (let $y (pick ()) (case $y ((b found)))))
!(collapse (let $c (color $x) (case $c ((cool $x)))))
!(collapse (let $y (pick ()) (let $_ (add-atom &self (= (pick ()) c)) $y)))
!(collapse (pick ()))
