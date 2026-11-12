; Scaling probe for 02_atomic_indirection: an index of 10,000 entries, each
; naming a space of its own that holds one value, and the indirection query:
; find the space an index entry names and read the value inside it.
;
; Three forms of the keyed query, each run once for every key:
;   one firing:   the index row and the value inside, in one transform;
;   two firings:  one transform reads the name, a second reads inside;
;   nested match: a query of the store, no firing.
; Three forms of the sweep over every entry:
;   one firing with `every` over the staged conjunction;
;   `match` lists the names, then one firing reads inside each;
;   nested match.
; Each line answers the sum of the values found and the firings taken.
;
; Batches.  In this draft the cost of an effect (making a space, adding an
; atom, a firing) grows with the number of effects already made in the same
; top-level evaluation, so a loop of n effects written as one recursion costs
; about n squared.  The index is therefore built, and the keys are looked up,
; in batches of 1,000, one top-level query per batch; the lookups of one batch
; then cost the same whatever the size of the index.  The argument `whole`
; runs the same work as one recursion per phase instead.
;
; With no argument the program runs the batched phases and prints only
; answers, as checked in the .expected file.  The first argument selects phases
; for measurement:
;   build | atomic | two-step | match | sweep   the batched build and one form
;   timed            every batched phase, each with its milliseconds
;   whole            build and keyed lookups as one recursion each, timed
;   every-then-each  the batched build, then a sweep in which one firing with
;                    `every` lists the names and each of its answers runs a
;                    second firing; its memory grows with the square of the
;                    number of entries
; Milliseconds vary from run to run and are not part of the .expected file.
!(import! &self interaction)
!(import! &self system)

(= (mode) (if (system:has-args) (system:arg 0) all))
(= (on $p) (let $m (mode) (if (== $m $p) True (if (== $m all) True (== $m timed)))))
(= (built) (let $m (mode) (if (== $m whole) False True)))
(= (shown) (let $m (mode) (if (== $m timed) True (== $m whole))))

(= (fires) (let (agenda $parked $ready $parks $attempts (fires $n) $woken $unchanged) (agenda-stats) $n))
(= (sum-of $xs) (foldl-atom $xs 0 $acc $x (+ $acc $x)))

(: run-phase (-> Atom Atom %Undefined%))
(= (run-phase $name $q)
   (let* (($f0 (fires)) ($t0 (system:monotonic-ns)) ($r (eval $q)) ($t1 (system:monotonic-ns)) ($f1 (fires)))
     (if (shown)
         ($name $r (steps (- $f1 $f0)) (ms (/ (- $t1 $t0) 1000000)))
         ($name $r (steps (- $f1 $f0))))))

!(bind! &index (new-space))
(= (fill $i $n)
   (if (> $i $n) (entries-to $n)
       (let $s (new-space)
         (let* (($_1 (add-atom $s (val $i))) ($_2 (add-atom &index (entry $i $s))))
           (fill (+ $i 1) $n)))))

(= (lookup-atomic $i) (transform &index (, (read (entry $i $h)) (read $h (val $v))) $v){$i})
(= (lookup-two-step $i) (let $h (transform &index (read (entry $i $h)) $h){$i} (transform $h (read (val $v)) $v)))
(= (lookup-match $i) (match &index (entry $i $h) (match $h (val $v) $v)){$i})

(= (sum-lookups $f $i $n $acc)
   (if (> $i $n) $acc
       (let $v ($f $i) (sum-lookups $f (+ $i 1) $n (+ $acc $v)))))

(= (sweep-atomic) (sum-of (collapse (transform &index (every (, (read (entry $k $h)) (read $h (val $v)))) $v))))
(= (sweep-match-then-each)
   (sum-of (collapse (let $h (match &index (entry $k $h) $h) (transform $h (read (val $v)) $v)))))
(= (sweep-match) (sum-of (collapse (match &index (entry $k $h) (match $h (val $v) $v)))))
(= (sweep-every-then-each)
   (sum-of (collapse (let $h (transform &index (every (read (entry $k $h))) $h) (transform $h (read (val $v)) $v)))))

; ---- the index, in ten batches
!(if (built) (run-phase build (fill 1 1000)) skipped)
!(if (built) (run-phase build (fill 1001 2000)) skipped)
!(if (built) (run-phase build (fill 2001 3000)) skipped)
!(if (built) (run-phase build (fill 3001 4000)) skipped)
!(if (built) (run-phase build (fill 4001 5000)) skipped)
!(if (built) (run-phase build (fill 5001 6000)) skipped)
!(if (built) (run-phase build (fill 6001 7000)) skipped)
!(if (built) (run-phase build (fill 7001 8000)) skipped)
!(if (built) (run-phase build (fill 8001 9000)) skipped)
!(if (built) (run-phase build (fill 9001 10000)) skipped)

; ---- keyed lookups, one firing each
!(if (on atomic) (run-phase (one-firing 1 1000) (sum-lookups lookup-atomic 1 1000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 1001 2000) (sum-lookups lookup-atomic 1001 2000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 2001 3000) (sum-lookups lookup-atomic 2001 3000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 3001 4000) (sum-lookups lookup-atomic 3001 4000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 4001 5000) (sum-lookups lookup-atomic 4001 5000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 5001 6000) (sum-lookups lookup-atomic 5001 6000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 6001 7000) (sum-lookups lookup-atomic 6001 7000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 7001 8000) (sum-lookups lookup-atomic 7001 8000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 8001 9000) (sum-lookups lookup-atomic 8001 9000 0)) skipped)
!(if (on atomic) (run-phase (one-firing 9001 10000) (sum-lookups lookup-atomic 9001 10000 0)) skipped)

; ---- keyed lookups, two firings each
!(if (on two-step) (run-phase (two-firings 1 1000) (sum-lookups lookup-two-step 1 1000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 1001 2000) (sum-lookups lookup-two-step 1001 2000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 2001 3000) (sum-lookups lookup-two-step 2001 3000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 3001 4000) (sum-lookups lookup-two-step 3001 4000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 4001 5000) (sum-lookups lookup-two-step 4001 5000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 5001 6000) (sum-lookups lookup-two-step 5001 6000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 6001 7000) (sum-lookups lookup-two-step 6001 7000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 7001 8000) (sum-lookups lookup-two-step 7001 8000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 8001 9000) (sum-lookups lookup-two-step 8001 9000 0)) skipped)
!(if (on two-step) (run-phase (two-firings 9001 10000) (sum-lookups lookup-two-step 9001 10000 0)) skipped)

; ---- keyed lookups, nested match
!(if (on match) (run-phase (nested-match 1 1000) (sum-lookups lookup-match 1 1000 0)) skipped)
!(if (on match) (run-phase (nested-match 1001 2000) (sum-lookups lookup-match 1001 2000 0)) skipped)
!(if (on match) (run-phase (nested-match 2001 3000) (sum-lookups lookup-match 2001 3000 0)) skipped)
!(if (on match) (run-phase (nested-match 3001 4000) (sum-lookups lookup-match 3001 4000 0)) skipped)
!(if (on match) (run-phase (nested-match 4001 5000) (sum-lookups lookup-match 4001 5000 0)) skipped)
!(if (on match) (run-phase (nested-match 5001 6000) (sum-lookups lookup-match 5001 6000 0)) skipped)
!(if (on match) (run-phase (nested-match 6001 7000) (sum-lookups lookup-match 6001 7000 0)) skipped)
!(if (on match) (run-phase (nested-match 7001 8000) (sum-lookups lookup-match 7001 8000 0)) skipped)
!(if (on match) (run-phase (nested-match 8001 9000) (sum-lookups lookup-match 8001 9000 0)) skipped)
!(if (on match) (run-phase (nested-match 9001 10000) (sum-lookups lookup-match 9001 10000 0)) skipped)

; ---- sweeps over every entry
!(if (on sweep) (run-phase sweep-one-firing (sweep-atomic)) skipped)
!(if (on sweep) (run-phase sweep-match-then-one-firing-each (sweep-match-then-each)) skipped)
!(if (on sweep) (run-phase sweep-nested-match (sweep-match)) skipped)
!(if (== (mode) every-then-each) (run-phase sweep-every-then-one-firing-each (sweep-every-then-each)) skipped)

; ---- the same work as one recursion per phase (argument whole)
!(if (== (mode) whole) (run-phase whole-build (fill 1 10000)) skipped)
!(if (== (mode) whole) (run-phase whole-one-firing (sum-lookups lookup-atomic 1 10000 0)) skipped)
!(if (== (mode) whole) (run-phase whole-two-firings (sum-lookups lookup-two-step 1 10000 0)) skipped)
!(if (== (mode) whole) (run-phase whole-nested-match (sum-lookups lookup-match 1 10000 0)) skipped)
