(p 1)
(p 2)
(p 2)
(q a)
(q b)
(r 1 a)
(r 2 b)
(r 2 c)
(= (symbol-choices) a)
(= (symbol-choices) a)
(= (symbol-choices) b)
(= (choices) 4)
(= (choices) 4)
(= (choices) 5)
(= (never) (never))
(= (touch $x) (let $u (add-atom &self (touched $x)) $x))
(= (count-until-done $h $left $sum)
   (if (== $left 0) gave-up
     (let (counted $n $state $law) (work-count $h)
       (if (== $state complete) (+ $sum $n)
         (count-until-done $h (- $left 1) (+ $sum $n))))))

; Peek preserves the same actual answer. Count consumes every remaining occurrence.
!(let $h (work-new (choices))
   (let $a (work-exists $h)
     (let $b (work-exists $h)
       (let $c (work-take $h 1)
         (let $d (work-count $h) ($a $b $c $d (work-take $h 1)))))))
; The correlated join is counted without visiting its five result templates.
!(let $h (work-new (match &self (, (p $x) (r $x $y)) 1))
   (let $a (work-count $h) ($a (work-take $h 1) (work-count $h))))
; A bounded enumerating fold retains its scalar accumulator and residual.
!(let $h (work-new (choices))
   (let $u (work-budget $h 1) ((count-until-done $h 400 0) (work-take $h 1))))
; A bounded factor cursor pauses, then publishes one complete count.
!(let $h (work-new (match &self (, (p $x) (r $x $y)) 1))
   (let $u (work-budget $h 1) ((count-until-done $h 400 0) (work-take $h 1))))
; Switching observer before a factor count completes preserves the original suffix.
!(let $h (work-new (match &self (, (p $x) (r $x $y)) 1))
   (let $u (work-budget $h 1)
     (let $a (work-count $h)
       (let $u (work-budget $h 0) ($a (work-take $h 8))))))
; A suspended search is unknown, not empty.
!(let $h (work-new (never))
   (let $u (work-budget $h 16)
     (let $a (work-exists $h) (let $u (work-cancel $h) $a))))
; Existential observation must not duplicate effects when counted afterwards.
!(let $h (work-new (touch 9))
   (let $a (work-exists $h)
     (let $b (work-exists $h)
       (let $c (work-count $h) ($a $b $c (collapse (match &self (touched 9) yes)))))))
; An effectful template is interpreted normally, not treated as one row per result.
!(let $h (work-new (match &self (p $x) (touch $x)))
   (let $a (work-count $h) ($a (collapse (match &self (touched $v) $v)))))
; A previously peeked witness survives transfer of the remaining choices.
!(let $h (work-new (symbol-choices))
   (let $a (work-exists $h)
     (let $u (work-route $h fifo) ($a (work-take $h 8)))))
; Mutation invalidates an unpublished count, rather than mixing two views.
!(let $h (work-new (match &self (, (p $x) (r $x $y)) 1))
   (let $u (work-budget $h 1)
     (let $a (work-count $h)
       (let $edit (add-atom &self (r 2 d))
         ($a (work-count $h) (work-take $h 1))))))

; Peeking a resource retains its owner through delivery and handle cancellation.
(= (fresh-payload)
   (let $s (new-space) (let $u (add-atom $s (payload 42)) $s)))
!(let $h (work-new (fresh-payload))
   (let $seen (work-exists $h)
     (let (taken ($s) $state) (work-take $h 1)
       (let $u (work-cancel $h) (match $s (payload $x) $x)))))
; Open templates retain their dialect's ordinary interpretation.
(= (two-answers) a)
(= (two-answers) b)
(code (two-answers))
!(let $h (work-new (match &self (code $x) $x)) (work-count $h))
; No answer is different from a suspended search.
!(let $h (work-new (match &self (absent $x) $x))
   ((work-exists $h) (work-count $h) (work-take $h 1)))

; Completed counting retains its route after later empty observations.
(= (record-route $record)
   (let (route-record (route $r) $strategy $law $modes $observer $footprint
                     $work $retained $owner $state $taken $transfers) $record $r))
!(let $h (work-new (match &self (q $x) 1))
   (let $c (work-count $h)
     (let $t (work-take $h 1)
       (let $again (work-count $h)
         ($c $t $again (record-route (work-record $h)))))))
