; PeTTa's (cut) is Prolog's cut: it commits a call to the equation the cut
; occurs in.  The call's later equations and the alternatives the goals
; before the cut left go; the caller's alternatives stay.  Every answer below
; is SWI-PeTTa's.

(= (first-equation $x) (let $t (cut) (one $x)))
(= (first-equation $x) (two $x))
!(collapse (first-equation a))

; A committed call whose continuation fails has no answer: its later
; equations are gone.
(= (commit-then-fail) (let $t (cut) (empty)))
(= (commit-then-fail) alternative)
!(collapse (commit-then-fail))

(= (after-choice) (let $v (superpose (1 2 3)) (let $t (cut) $v)))
(= (after-choice) 9)
!(collapse (after-choice))

(= (guarded $x) (if (> $x 0) (let $t (cut) positive) (empty)))
(= (guarded $x) other)
!(collapse (guarded 5))
!(collapse (guarded -5))

; A let binds its pattern before its value's goals run: a pattern that does
; not meet the cut's value fails before the cut.
(= (not-reached $x) (let False (cut) never))
(= (not-reached $x) reached)
!(collapse (not-reached 1))

; A callee's cut commits the callee alone.
(= (inner) (let $t (cut) in))
(= (inner) second)
(= (outer) (let $v (superpose (a b)) (let $i (inner) ($v $i))))
!(collapse (outer))

(= (as-value) (cut))
!(as-value)
(= (in-argument) (pair (cut) done))
!(in-argument)

(= (case-cut $k) (case $k ((a (let $t (cut) chose-a)) ($z other))))
(= (case-cut $k) fallback)
!(collapse (case-cut a))
!(collapse (case-cut b))

; A cut in a recursive call commits that call; the caller's later equation
; stays.
(= (first-big ()) none)
(= (first-big (cons $h $t))
   (if (> $h 10) (let $c (cut) (found $h)) (first-big $t)))
(= (first-big (cons $h $t)) never)
!(collapse (first-big (1 20 30)))
!(collapse (first-big (40)))

; A cut after a goal the host answers drops that goal's other answers too.
(: host-alts (-> Symbol))
(= (host-alts) (superpose (x y z)))
(= (cross-host) (let $v (host-alts) (let $t (cut) $v)))
(= (cross-host) other)
!(collapse (cross-host))

!(import! &self (library lib_combinatorics))
!(chooseKl (a b c) 2)
!(chooseKl () 0)
!(chooseKl (a) 0)
!(collapse (chooseK (a b c d) 2))
; once answers some witness: any of the combinations.
!(let $w (once (chooseK (0 1 2 3 4 5 6 7) 4))
   (is-member $w (chooseKl (0 1 2 3 4 5 6 7) 4)))
!(size-atom (chooseKl (0 1 2 3 4 5 6 7 8 9 10 11) 6))
