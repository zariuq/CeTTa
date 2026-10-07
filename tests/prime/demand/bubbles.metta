; Prime's top commits to no evaluation strategy: a bubble commits the region
; written inside it.  The region's binding sites are the arguments of the
; calls written there, passed lazily by default, and its `let` bindings,
; evaluated eagerly by default.  A strategy is eager, lazy or resample.  Two
; forms stand side by side:
;   (demand:licensed S e)  applies S only where the answers are proved to
;     stay the same, and records why it kept the default elsewhere;
;   (demand:committed S e) applies S everywhere, and its records name where
;     the answers change.
; (demand:report e) gives e's values with the records its bubbles made.
(= coin heads)
(= coin tails)
(= (k $x $y) $x)

; ---- Discarding.  k never uses its second argument.  Lazily coin is not
; evaluated: one answer.  Committed to eagerness, coin is evaluated first and
; each of its answers gives a copy of the result: two answers.  Licensed,
; eagerness is not applied, since k discards an argument that may have other
; than one answer.
!(k 1 coin)
!(demand:committed eager (k 1 coin))
!(demand:licensed eager (k 1 coin))
!(demand:report (demand:committed eager (k 1 coin)))
!(demand:report (demand:licensed eager (k 1 coin)))
; Where an argument is demanded anyway, eagerness changes nothing, and the
; licensed form applies it, naming the theorem that licenses it.
(= (first (Cons $h $t)) $h)
(= (flip) (Cons heads Nil))
(= (flip) (Cons tails Nil))
!(demand:report (demand:licensed eager (first (flip))))
!(demand:report (demand:licensed eager (+ (k 1 coin) 1)))
!(demand:report (demand:committed eager (+ (k 1 coin) 1)))
; A name is produced once in a branch: evaluating coin a second time,
; eagerly, finds the first choice.
!(demand:report (demand:committed eager (k (k 1 coin) coin)))
; Eagerness also evaluates a discarded computation that has no value: the
; commitment meets the cycle that laziness never runs into.
(= loop-a loop-b)
(= loop-b loop-a)
!(k 1 loop-a)
!(demand:committed eager (k 1 loop-a))
!(demand:licensed eager (k 1 loop-a))

; ---- Copying.  A binding of coin used twice: shared, two pairs; resampled,
; four.  Licensed, resampling is not applied, since the binding may be used
; twice and may have more than one answer.
!(let $c coin (P $c $c))
!(demand:committed resample (let $c coin (P $c $c)))
!(demand:licensed resample (let $c coin (P $c $c)))
!(demand:report (demand:committed resample (let $c coin (P $c $c))))
!(demand:report (demand:licensed resample (let $c coin (P $c $c))))
; A binding used exactly once: resampling changes nothing, and the licensed
; form applies it.
(= (die) 1)
(= (die) 2)
!(demand:report (demand:licensed resample (let $d (die) (+ $d 10))))

; ---- Laziness for a `let`, eager by default.  An unused binding of coin
; still contributes its two answers eagerly; lazily it is never run.
!(let $c coin done)
!(demand:committed lazy (let $c coin done))
!(demand:report (demand:committed lazy (let $c coin done)))
!(demand:report (demand:licensed lazy (let $c coin done)))
!(demand:report (demand:licensed lazy (let $d (die) (+ $d 10))))

; ---- An argument passed by call is one shared suspension: it cannot be
; resampled, and the site says so.
(= (twice $x) (P $x $x))
!(demand:report (demand:committed resample (twice coin)))

; ---- A bubble with a strategy Prime does not know stays as written.
!(demand:committed sideways (k 1 coin))
