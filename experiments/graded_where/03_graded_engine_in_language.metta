; ============================================================================
; The graded where-clause, DEFINED IN-LANGUAGE (runs today)
; ============================================================================
;
; The answer to "can the 3-arity graded form be defined in-language?"
; is: yes — here it is.  The engine below is the complete semantics of
;
;   (= pattern transform (grade-expr))
;
; in a dozen lines of ordinary MeTTa: graded rules are TRIPLES stored as
; data; per rule, its own pattern is matched (against a call space) twice —
; once to instantiate the transform row, once to instantiate the grade
; expression — the grade is evaluated (at the source, where the bindings
; and the ambient beliefs live) and becomes the candidate's weight; the
; named discipline resolves the weighted candidates; the winner's transform
; fires.
;
; Discipline is a PLUGGED FUNCTION, which is exactly the design point:
; disciplines are named and swappable, and the Bool corner is one of them
; (fire-first-nonzero is the classic first-matching-rule reading — it
; reproduces the CRISP forager).  This engine is what a native version
; would be compiled from; running the same forager through it recovers
; file 01's trajectories, which is the conformance witness.
;
; Data notes.  Rules close over their environment literally: the grade and
; transform expressions stored in the rules space carry the run's belief
; and clock spaces baked in as values ("closures as data").  Belief rows
; are epoch-tagged and append-only (no remove-atom needed — and silent
; remove-atom in prime is documented in semantics_findings/), which is also
; the receipts/provenance pattern.
;
; Restriction (documented): one match row per rule per call in this demo.
; The multi-row case needs match to expose the substitution so the two
; instantiations zip per row instead of crossing — engine work, not
; semantics work.
;
; ---------------------------------------------------------------------------
; 0. Machinery (CP2 idiom: let-bind before grounded utils)
; ---------------------------------------------------------------------------

(= (force-value $v) $v)

(= (better $x $y)
   (if (> (car-atom $x) (car-atom $y)) $x $y))

(= (argmax $xs)
   (foldl-atom $xs (car-atom $xs) $acc $x (better $acc $x)))

(= (rev-list $xs) (rev-onto $xs ()))
(= (rev-onto $xs $acc)
   (if (== $xs ()) $acc
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (rev-onto $t (cons-atom $h $acc))))))

; ---------------------------------------------------------------------------
; 1. THE ENGINE (the where-clause, defined)
; ---------------------------------------------------------------------------

; Weighted candidates for one call: every (grule p t g) row both grades and
; produces.  The grade expression is evaluated with the rule's pattern
; bindings and its baked environments in scope — weight at the source.
(= (weighted-candidates $rules $call)
   (match $rules (grule $p $t $g)
     (let $ti (match $call $p $t)
       (let $ge (match $call $p $g)
         ((eval $ge) $ti)))))

; Discipline: argmax — the forager's race.
(= (fire-argmax $rules $call)
   (let $cands (collapse (weighted-candidates $rules $call))
     (if (== $cands ())
         (no-candidate)
         (let ($g $winner) (argmax $cands)
           ($g (eval $winner))))))

; Discipline: first-nonzero — the classic crisp where-clause semantics:
; the first candidate whose grade is not zero fires; value 0 abstains.
; The Bool corner lives in the discipline, which is exactly why crisp-where
; is sugar (proved in Lean: whereDeliver_eq_ifDeliver).
(= (fire-first-nonzero $rules $call)
   (let $cands (collapse (weighted-candidates $rules $call))
     (let $kept (keep-nonzero $cands)
       (if (== $kept ())
           (no-candidate)
           (let ($g $winner) (car-atom $kept)
             ($g (eval $winner)))))))

(= (keep-nonzero $xs)
   (if (== $xs ()) ()
       (let $x (car-atom $xs)
         (let ($g $w) $x
           (let $t (cdr-atom $xs)
             (let $r (keep-nonzero $t)
               (if (> $g 0)
                   (cons-atom $x $r)
                   $r)))))))

; ---------------------------------------------------------------------------
; 2. The world (hostile, 5% rich; chi uninformative — as in file 01)
; ---------------------------------------------------------------------------

(= (specimen $i) (if (== (% $i 20) 19) rich decoy))
(= (outcome rich) 1)
(= (outcome decoy) 0)
(= (payoff rich) 2)
(= (payoff decoy) -1)
(= (graze-payoff) 0.2)
(= (k-scepticism) 4.0)
(= (confidence $n) (/ $n (+ $n (k-scepticism))))
(= (power $n $s) (* $s (confidence $n)))
(= (revise $n $s $x) ((+ $n 1) (/ (+ (* $n $s) $x) (+ $n 1))))

; ---------------------------------------------------------------------------
; 3. The forager AS GRADED RULES (data) — the whole point of the file
; ---------------------------------------------------------------------------
;
; Two candidate rules over the call (decide).  Read literally:
;
;   (= (decide) (open-outcome)   (grade (fetch-power)))
;   (= (decide) (graze-outcome)  (grade (graze-value)))

(= (graze-value) 0.045)

(= (better-e $x $y)
   (let ($e1 $n1 $s1) $x
     (let ($e2 $n2 $s2) $y
       (if (> $e1 $e2) $x $y))))

(= (argmax-epoch $xs)
   (foldl-atom $xs (car-atom $xs) $acc $x (better-e $acc $x)))

; current belief of a belief space = its row of maximal epoch
(= (belief-now $b)
   (let $rows (collapse (match $b (belief (epoch $e) $n $s) ($e $n $s)))
     (argmax-epoch $rows)))

; The open grade: computed from the run's belief atoms (baked in at spawn).
(= (fetch-power $b)
   (let ($e $n $s) (belief-now $b)
     (power $n $s)))

(= (current-tick $ck)
   (argmax-int (collapse (match $ck (tick $i) $i))))

(= (argmax-int $xs) (foldl-atom $xs (car-atom $xs) $acc $x (math:max $acc $x)))

(= (open-outcome $ck)
   (did open (specimen (current-tick $ck))))

(= (graze-outcome $ck)
   (did graze (specimen (current-tick $ck))))

; ---------------------------------------------------------------------------
; 4. The runner (world laws): engine chooses, world applies
; ---------------------------------------------------------------------------

(= (apply-outcome $grade $acted $i $holdings $belief)
   (let ($d $act $spec) $acted
     (if (== $act open)
         (let ($e $n $s) (belief-now $belief)
           (let ($n2 $s2) (revise $n $s (outcome $spec))
             (let $i2 (+ $i 1)
               (let $epoch-add (add-atom $belief (belief (epoch $i2) $n2 $s2))
                 (($n2 $s2) (+ $holdings (payoff $spec)) (opened $spec for-weight $grade))))))
         (($silent) (+ $holdings (graze-payoff)) (grazed-over $spec at-weight $grade)))))

(= (engine-run $fire $rules $belief $clock $call $i $stop $holdings $trace)
   (if (>= $i $stop)
       (done $holdings (rev-list $trace))
       (let $tick (add-atom $clock (tick $i))
         (let ($grade $acted) ($fire $rules $call)
           (let ($belief-note $h2 $action) (apply-outcome $grade $acted $i $holdings $belief)
             (let $i2 (+ $i 1)
               (engine-run $fire $rules $belief $clock $call $i2 $stop $h2
                  (cons-atom ($i $action) $trace))))))))

; Spawn a forager run: fresh spaces; the graded rules are stored as DATA
; with the run's belief and clock spaces baked into grade and transform.
(= (spawn-run $fire $n0 $s0 $stop)
   (let $rules (new-space)
     (let $belief (new-space)
       (let $clock (new-space)
         (let $call (new-space)
           (let $seed (add-atom $belief (belief (epoch 0) $n0 $s0))
             (let $r1 (add-atom $rules (grule (decide) (open-outcome $clock) (fetch-power $belief)))
               (let $r2 (add-atom $rules (grule (decide) (graze-outcome $clock) (graze-value)))
                 (let $c (add-atom $call (decide))
                   (engine-run $fire $rules $belief $clock $call 0 $stop 0 ()))))))))))

; ---------------------------------------------------------------------------
; 5. The measure: file 01's three protagonists, through the engine
; ---------------------------------------------------------------------------

; A: BLIND (no prior evidence) -> every open grades 0, wall wins (bootstrap
;    trap as an engine outcome — a weight, not a verdict)
!(spawn-run fire-argmax 0 0.5 12)

; B: LEARNER -> opens, drags s·c through the wall, stops (file 01's first
;    12 steps: 7 opens then grazes)
!(spawn-run fire-argmax 1 0.5 12)

; C: CRISP (same rules, discipline swapped to first-nonzero):
;    the classic where-clause opens every time
!(spawn-run fire-first-nonzero 1 0.5 12)

; ---------------------------------------------------------------------------
; 6. [WISHLIST] what the native engine buys (03b_wish_syntax notes):
;
;   (discipline &forager (argmax-by grade))
;   (= (decide) (open-outcome)  (grade (fetch-power)))
;   (= (decide) (graze-outcome) (grade (graze-value)))
;
;   plus the loop of life as the ambient committing step, stream
;   disciplines (beam k / decayed γ / sample-with-window), and zero
;   machinery from sections 0–4.  In-language semantics here; the native
;   engine is its optimization.  Same rules.
; ---------------------------------------------------------------------------
