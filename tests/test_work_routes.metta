; Moving a suspended residual exposes the alternatives hidden below it.
(= (deep-choice) (deep-choice))
(= (deep-choice) found)
(= (until-answer $h $n)
   (if (== $n 0) gave-up
     (let $t (work-take $h 1)
       (case $t
         (((taken () $state) (until-answer $h (- $n 1)))
          ((taken $xs $state) $xs))))))
!(let $h (work-new (deep-choice))
   (let $u (work-budget $h 64)
     (let $before (work-take $h 1)
       (let $moved (work-route $h fifo)
         ($before $moved (until-answer $h 20))))))

; Repeated equal replies remain separate occurrences after a delivered prefix.
(= (finite-choice) a)
(= (finite-choice) a)
(= (finite-choice) b)
(= (finite-choice) c)
!(let $h (work-new (finite-choice))
   (let $before (work-take $h 1)
     (let $moved (work-route $h fifo)
       ($before $moved (work-take $h 20)))))

; Rejected routing leaves the previous route and its unstarted effects intact.
(= (effectful $x) (let $u (add-atom &self (effect-seen $x)) $x))
!(let $h (work-new (effectful 7)) (add-atom &self (effect-work $h)))
!(match &self (effect-work $h) (work-route $h completed))
!(collapse (match &self (effect-work $h) (work-route $h fifo)))
!(collapse (match &self (effect-seen $x) $x))
!(match &self (effect-work $h) (work-take $h 10))
!(collapse (match &self (effect-seen $x) $x))

; An unknown route also leaves the live residual untouched.
!(let $h (work-new (finite-choice)) (add-atom &self (finite-work $h)))
!(match &self (finite-work $h) (work-take $h 1))
!(collapse (match &self (finite-work $h) (work-route $h unknown-route)))
!(match &self (finite-work $h) (work-take $h 20))

; Taking zero answers does not start a completed route or spend its effects.
!(let $h (work-new (effectful 8)) (add-atom &self (zero-work $h)))
!(match &self (zero-work $h) (work-route $h completed))
!(match &self (zero-work $h) (work-take $h 0))
!(collapse (match &self (effect-seen 8) yes))
!(match &self (zero-work $h) (work-take $h 10))
!(collapse (match &self (effect-seen 8) yes))
