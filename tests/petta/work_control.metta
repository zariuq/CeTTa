; A retained computation held by handle (work-new): takes observe prefixes
; and keep the rest, a budget pauses a take at an answer boundary, effects
; happen once, when their answers are produced, and a handle kept in the
; space keeps its computation across queries.  The record names the route:
; the general machine for a root the PeTTa machine admits.  Which answers a
; prefix holds is open (the answers are a bag), so a take is read by its size
; and state, and the takes of one computation together as a sorted list.

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
   (let (taken $a $sa) (work-take $h 2)
     (let (taken $b $sb) (work-take $h 10)
       ((size-atom $a) $sa (size-atom $b) $sb (sort-atom (union-atom $a $b))
        (record-summary (work-record $h))))))

; A budget of four transitions per take: takes pause, and draining the
; work with them gives every answer once.
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
   (let $u (work-budget $h 4)
     (let ($all $paused) (drain $h () False) ((sort-atom $all) $paused))))

; A take of zero answers advances nothing.
!(let $h (work-new (ancestors p0))
   (let $a (work-take $h 0)
     (let (taken $b $sb) (work-take $h 1)
       ($a (size-atom $b) $sb (subtraction-atom $b (p1 p2 p3 p4 p5))))))

; A handle kept in the space keeps its computation across queries: each
; take is noted in the space, and the notes hold every answer once.
(= (note-taken $xs)
   (if (== $xs ())
       ()
       (let $x (car-atom $xs)
         (let $u (add-atom &self (took $x)) (note-taken (cdr-atom $xs))))))
(= (take-kept $h $k)
   (let (taken $xs $state) (work-take $h $k)
     (let $u (note-taken $xs) ((size-atom $xs) $state))))
!(let $h (work-new (ancestors p0)) (add-atom &self (kept $h)))
!(match &self (kept $h) (take-kept $h 2))
!(match &self (kept $h) (take-kept $h 2))
!(match &self (kept $h) (take-kept $h 2))
!(sort-atom (collapse (match &self (took $x) $x)))

; Effects happen once, when their answers are produced: the first take
; makes the mark of the answer it took, the later take the rest.
(= (mark $x) (let $u (add-atom &self (seen $x)) $x))
!(let $h (work-new (mark (superpose (1 2 3))))
   (let (taken $a $sa) (work-take $h 1)
     (let $b (collapse (match &self (seen $s) $s))
       (let (taken $c $sc) (work-take $h 5)
         ((size-atom $a) $sa (== (sort-atom $b) (sort-atom $a))
          (size-atom $c) $sc (sort-atom (union-atom $a $c)))))))
!(sort-atom (collapse (match &self (seen $s) $s)))

; Every source here runs on the general machine in PeTTa, the recursion
; through a match binding included.
(edge a b)
(edge b c)
(= (path $x $y) (match &self (edge $x $y) $y))
(= (path $x $z) (match &self (edge $x $y) (path $y $z)))
(= (reach $x) (path $x $y))
!(let $h (work-new (reach a))
   (let (taken $a $sa) (work-take $h 1)
     (let (taken $b $sb) (work-take $h 5)
       ((size-atom $a) $sa (size-atom $b) $sb (sort-atom (union-atom $a $b))
        (record-summary (work-record $h))))))

; Cancel releases the computation: its handle names none afterwards.
!(let $h (work-new (ancestors p0))
   (let $a (work-take $h 1)
     (let $u (work-cancel $h) (work-take $h 1))))
