; A retained computation held by handle (work-new): takes observe prefixes
; and keep the rest, a budget pauses a take at an answer boundary, effects
; happen once, when their answers are produced, and a handle kept in the
; space keeps its computation across queries.  The record names the route:
; the general machine for a computation HE's shared execution admits, the
; completed route otherwise.

(parent p1 p0)
(parent p2 p1)
(parent p3 p2)
(parent p4 p3)
(parent p5 p4)
(= (ancestors $c) (match &self (parent $p $c) $p))
(= (ancestors $c) (match &self (parent $p $c) (ancestors $p)))

; The record's stable fields: route, law, observer, the work charged (a
; positive count on a metered route), whether bytes are retained, state and
; answers taken.
(= (work-summary $w) (case $w ((unmetered unmetered) ($n (> $n 0)))))
(= (record-summary $r)
   (case $r
     (((route-record (route $route) (strategy $strategy) (law $law) $modes (observer $o)
                     $footprint (work $w) (retained $b) $owner
                     (state $s) (taken $n) (transfers $transfers))
       ($route $law $o (work-summary $w) (> $b 0) $s $n)))))

; Two prefixes, then the record.
!(let $h (work-new (ancestors p0))
   (let $a (work-take $h 2)
     (let $b (work-take $h 10)
       ($a $b (record-summary (work-record $h))))))

; A budget of four transitions per take: takes pause, and draining the
; work with them gives every answer once, in order.
(= (drain $h $acc $paused)
   (let $t (work-take $h 10)
     (case $t
       (((taken $xs complete) ((append-all $acc $xs) $paused))
        ((taken $xs paused) (drain $h (append-all $acc $xs) True))
        ((taken $xs more-possible) (drain $h (append-all $acc $xs) $paused))))))
(= (append-all $acc $xs)
   (if (== $xs ())
       $acc
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (append-all (snoc $acc $h) $t)))))
(= (snoc $l $x)
   (if (== $l ())
       ($x)
       (let $h (car-atom $l)
         (let $t (cdr-atom $l)
           (let $r (snoc $t $x) (cons-atom $h $r))))))
!(let $h (work-new (ancestors p0))
   (let $u (work-budget $h 4) (drain $h () False)))

; A take of zero answers advances nothing.
!(let $h (work-new (ancestors p0))
   (let $a (work-take $h 0)
     (let $b (work-take $h 1) ($a $b))))

; A handle kept in the space keeps its computation across queries.
!(let $h (work-new (ancestors p0)) (add-atom &self (kept $h)))
!(match &self (kept $h) (work-take $h 2))
!(match &self (kept $h) (work-take $h 2))
!(match &self (kept $h) (work-take $h 2))

; Effects happen once.  HE completes the root's alternatives, effects
; included, before its consumer reads one: the first take makes all three
; marks, and the later take delivers the rest.
(= (mark $x) (let $u (add-atom &self (seen $x)) $x))
!(let $h (work-new (mark (superpose (1 2 3))))
   (let $a (work-take $h 1)
     (let $b (collapse (match &self (seen $s) $s))
       (let $c (work-take $h 5) ($a $b $c)))))
!(collapse (match &self (seen $s) $s))

; A computation the shared execution does not admit runs on the completed
; route: here a relation whose recursion reads a match's binding past it.
(edge a b)
(edge b c)
(= (path $x $y) (match &self (edge $x $y) $y))
(= (path $x $z) (match &self (edge $x $y) (path $y $z)))
(= (reach $x) (path $x $y))
!(let $h (work-new (reach a))
   (let $a (work-take $h 1)
     (let $b (work-take $h 5) ($a $b (record-summary (work-record $h))))))

; Cancel releases the computation: its handle names none afterwards.
!(let $h (work-new (ancestors p0))
   (let $a (work-take $h 1)
     (let $u (work-cancel $h) (work-take $h 1))))
