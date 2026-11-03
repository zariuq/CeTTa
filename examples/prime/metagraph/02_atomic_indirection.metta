; Atomic indirection: one firing reads a space's name from an index and then
; reads, takes or makes inside that space.
;
; The draft forms a transform whose source names its space by a variable when
; an earlier source of the same transform binds that variable (part A).  The
; two-step version, a firing that reads the name and a second firing that uses
; it, has an intermediate state that other rules can see, and an extra step
; (part B).  Part C asks "search every space I hold for a contradiction".
;
; Queries that have no answer print nothing in this runtime, so a query whose
; answer may be empty is wrapped in `collapse`, which shows ().  No answer
; prints a space: the draft prints a space as its address, which changes from
; run to run.
!(import! &self interaction)

(= (fires) (let (agenda $parked $ready $parks $attempts (fires $n) $woken $unchanged) (agenda-stats) $n))
(: counted (-> Atom %Undefined%))
(= (counted $q)
   (let* (($before (fires))
          ($answers (sort-atom (collapse (eval $q))))
          ($after (fires)))
     (answers $answers (steps (- $after $before)))))

!(bind! &index (new-space))
!(bind! &out (new-space))
!(let* (($b1 (new-space)) ($b2 (new-space))
        ($_1 (add-atom $b1 (item apple)))
        ($_2 (add-atom $b1 (item pear)))
        ($_3 (add-atom $b2 (item fig)))
        ($_4 (add-atom &index (box b1 $b1)))
        ($_5 (add-atom &index (box b2 $b2))))
   built)

; ==== A. What the draft forms and runs.

; A1. Read the name, then read inside, in one firing.
!(counted (transform &index (, (read (box b1 $h)) (read $h (item pear))) (found pear in b1)))
; A2. Read the name, then take inside and make the taken item elsewhere: one
;     commit over three spaces.
!(counted (transform &index (, (read (box b2 $h)) (take $h (item $x))) (add-atom &out (moved $x))))
; A3. Read the name, then make into the named space.
!(counted (transform &index (read (box b2 $h)) (add-atom $h (item plum))))
; A4. Absence inside the named space.
!(counted (transform &index (, (read (box b1 $h)) (absent $h (item banana))) (no-banana-in b1)))
; A5. All matches inside the named space, as one collection.
!(counted (transform &index (, (read (box b1 $h)) (all (read $h (item $x)) $xs)) (b1-holds $xs)))
; A6. Every box with every item: every row of the staged conjunction, one commit.
!(counted (transform &index (every (, (read (box $k $h)) (read $h (item $x)))) (in $k $x)))
; A7. A name computed by a `let` source (here by a function that reads the
;     index itself) also places a later source.
(= (box-named $k) (transform &index (read (box $k $h)) $h){$k})
!(counted (transform &index (, (let $h (box-named b1)) (read $h (item apple))) (found apple via let)))

; ---- What is refused.
; R1. A space variable that no source binds: "not formed", for every program.
!(transform &index (read $s (item $x)) (anywhere $x))
; R2. Order matters: the same two sources with the binding source written second.
!(transform &index (, (read $h (item $x)) (read (box b1 $h))) (reversed $x))
; R3. A location written as a pattern.
!(transform &index (read (box b1 $h) (item $x)) (pattern-location $x))
; R4. A name that data supplies but that is not a space is refused when the
;     firing reaches it, and the refusal takes the whole firing: one bad entry
;     in the index turns the `every` query of A6 into an error, good rows and all.
!(add-atom &index (box broken not-a-space))
!(collapse (transform &index (every (, (read (box $k $h)) (read $h (item $x)))) (in $k $x)))
; R4'. A guard source that keeps only spaces restores the query.
!(sort-atom (collapse (transform &index
   (every (, (read (box $k $h)) (let (Space $t) (get-type $h)) (read $h (item $x))))
   (in $k $x))))
!(remove-atom &index (box broken not-a-space))
; R5. Not refused, and not atomic: the function a `let` source runs may fire on
;     its own.  Here it takes a token from another space; the enclosing firing
;     then finds no row and does not commit, yet the inner take stands.
!(bind! &tokens (new-space))
!(add-atom &tokens (token t1))
(= (grab-token) (transform &tokens (take (token $t)) $t))
!(let* (($outer (collapse (transform &index (, (let $t (grab-token)) (read (wanted $t))) (got $t))))
        ($left (collapse (match &tokens (token $t) $t))))
   (enclosing-firing-answers $outer tokens-left $left))

; ==== B. One firing against two: the intermediate state.
;
; A delivery moves a job's item out of its box and retires the job.  An
; auditor rule watches for a job that is neither waiting in the index nor
; delivered: a state that should never be seen.  It is installed with
; `whenever`, so the runtime wakes it after every commit that changes a space
; it read.

!(bind! &audit (new-space))
!(let* (($j1 (new-space)) ($j2 (new-space))
        ($_1 (add-atom $j1 (item book)))
        ($_2 (add-atom $j2 (item lamp)))
        ($_3 (add-atom &index (job atomic $j1)))
        ($_4 (add-atom &index (job two-step $j2)))
        ($_5 (add-atom &audit (known-job atomic)))
        ($_6 (add-atom &audit (known-job two-step))))
   jobs-posted)
!(whenever (transform &audit
   (, (read (known-job $j)) (absent &index (job $j $b)) (absent &out (delivered $j $x))
      (absent (gap-seen $j)))
   (add-atom &audit (gap-seen $j))))

; B1. Atomic: one firing takes the job, takes the item and records the delivery.
!(counted (transform &index (, (take (job atomic $b)) (take $b (item $x)))
   (add-atom &out (delivered atomic $x))))
; B2. Two firings: the first takes the job and hands its box on; the second
;     takes the item and records the delivery.  The count is 3: the two
;     delivery firings and the auditor's firing on the state between them
;     (in B1 the auditor is woken but finds no gap, so it does not fire).
!(counted (let $b (transform &index (take (job two-step $b)) $b)
   (transform $b (take (item $x)) (add-atom &out (delivered two-step $x)))))
; What the auditor saw: the gap of the two-step delivery only.
!(sort-atom (collapse (transform &audit (every (read (gap-seen $j))) (gap-seen $j))))
!(sort-atom (collapse (transform &out (every (read (delivered $j $x))) (delivered $j $x))))

; B3. A name read in one firing can be stale in the next.  The index names the
;     current log; between the writer's two firings a rotation replaces it.
;     The schedule is written out by hand here; the agenda produces it whenever
;     another process runs between the writer's two firings.  The one-firing
;     writer has no such schedule: it writes into the log that is current at
;     its own commit.
(= (rotate $g)
   (let $new (new-space)
     (let $_ (add-atom $new (log-generation $g))
       (transform &index (take (current-log $old)) (add-atom &index (current-log $new))))))
(= (generation-of $l) (transform $l (read (log-generation $g)) $g))
(= (current-generation) (transform &index (, (read (current-log $l)) (read $l (log-generation $g))) $g))
!(let $l (new-space)
   (let $_ (add-atom $l (log-generation 1)) (add-atom &index (current-log $l))))
!(let* (($l (transform &index (read (current-log $l)) $l))
        ($_ (rotate 2))
        ($w (transform $l () (add-atom $l (entry written-two-step)))))
   (two-step-wrote-into-generation (generation-of $l) while-current (current-generation)))
!(let* (($_ (rotate 3))
        ($w (transform &index (read (current-log $l)) (add-atom $l (entry written-atomically))))
        ($g (transform &index (, (read (current-log $l)) (read $l (entry written-atomically)) (read $l (log-generation $g))) $g)))
   (atomic-wrote-into-generation $g while-current (current-generation)))

; ==== C. "Search every space I hold for a contradiction."
;
; The natural program leaves the space open.  The draft refuses it before it
; runs: an unbound space variable would read every space in the runtime.
!(bind! &me (new-space))
!(transform &me (, (read $s (P $x)) (read $s (not (P $x)))) (contradiction-at $x))
; The nearest form that runs: the spaces I hold are listed in my own space, and
; the search reads that list first, so each row's space is bound by the
; earlier source.  One firing searches every listed space.
!(let* (($k1 (new-space)) ($k2 (new-space)) ($k3 (new-space))
        ($_1 (add-atom $k1 (P a)))
        ($_2 (add-atom $k1 (not (P a))))
        ($_3 (add-atom $k2 (P b)))
        ($_4 (add-atom $k3 (not (P c))))
        ($_5 (add-atom $k3 (P c)))
        ($_6 (add-atom $k3 (label k3)))
        ($_7 (add-atom $k1 (label k1)))
        ($_8 (add-atom $k2 (label k2)))
        ($_9 (add-atom &me (holds $k1)))
        ($_10 (add-atom &me (holds $k2)))
        ($_11 (add-atom &me (holds $k3))))
   holdings-listed)
!(counted (transform &me
   (every (, (read (holds $s)) (read $s (P $x)) (read $s (not (P $x))) (read $s (label $l))))
   (contradiction $l (P $x))))
; A space held one level further down is not in my list: one that k2 names is
; not searched unless the search follows the name.  Following names to any
; depth is a recursive walk; here the walk uses `match` (a query, no firing)
; and the search fires once per space reached, five spaces in all.  An `every`
; with no rows commits nothing and answers (), so it cannot list the children
; of a leaf in such a walk; `match` answers nothing there.
!(let* (($deep (new-space))
        ($_1 (add-atom $deep (P d)))
        ($_2 (add-atom $deep (not (P d))))
        ($_3 (add-atom $deep (label deep)))
        ($_4 (transform &me (, (read (holds $s)) (read $s (label k2))) (add-atom $s (holds $deep)))))
   nested-one-level)
(= (reachable $s) $s)
(= (reachable $s) (let $t (match $s (holds $t) $t) (reachable $t)))
(= (contradictions-in $s)
   (transform $s (every (, (read (P $x)) (read (not (P $x))) (read (label $l)))) (contradiction $l (P $x))))
!(counted (contradictions-in (reachable &me)))
