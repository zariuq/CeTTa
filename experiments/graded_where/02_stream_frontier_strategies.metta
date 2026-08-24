; ============================================================================
; Reified-frontier strategies as data (runs today): one search, five
; disciplines, and where the engine could absorb them
; ============================================================================
;
; Companion to 01_forager_userspace.metta.  The reified-frontier lane of the
; where-clause design: the search frontier is an ordinary value, strategies
; are ordinary functions over it, and the graded where-weights travel on the
; frontier elements themselves.  Everything here runs under --lang prime
; today; every [WISHLIST] marks where a per-space, engine-resident discipline
; (the graded where slot + race resolution) would absorb the boilerplate.
;
; Five disciplines over ONE step function (kids), on ONE tiny directed
; graded graph:
;
;   bfs     — queue discipline (append at end)
;   dfs     — stack discipline (prepend at front)
;   greedy  — argmax on the where-weight (needs bag comparisons)
;   beam2   — argmax + width-2 window: a STREAM-NATIVE discipline
;             (bounded frontier: the total race never needs to exist —
;             this is the honest answer to "where is the race set on a
;             stream", cf. normalizedShare_not_candidatewise)
;   fickle  — greedy on gamma-decayed weights: forgetting AS a discipline,
;             the stream-native cure for confidence-rigidity (Greg's
;             inflation-freeze pathology needs exactly this)
;
; The edges' grades ARE what a graded where-clause would deliver per
; candidate — authored at the source, not post-hoc.
;
; NOTE (semantic choice point CP2, semantics_findings/2026-08-17):
; in today's CeTTa, grounded list utils (car-atom/cdr-atom/cons-atom)
; receive their arguments UNEVALUATED — they dereference variables but do
; not reduce non-variable redexes — while arithmetic operators do reduce
; nested redexes.  Until the demand discipline is uniform, the working
; idiom is: let-bind every computed component first (let forces), and hand
; grounded utils only variables or literal data.  `force-value` below is
; the explicit demand marker.  This file is also the regression artifact
; for that note.

; ---------------------------------------------------------------------------
; 0. Explicit demand (CP2 workaround)
; ---------------------------------------------------------------------------

; A one-rule "function" whose body is the variable: rule application
; evaluates the body, so a redex passed here comes out reduced.  Used at
; step boundaries so recursive calls bind their frontier as a VALUE, not
; as the unreduced update-expression.
(= (force-value $v) $v)

; ---------------------------------------------------------------------------
; 1. The world: a directed graded graph (goal: reach h)
; ---------------------------------------------------------------------------

(= (kids a) ((0.9 b) (0.4 c)))
(= (kids b) ((0.6 d) (0.7 e)))
(= (kids c) ((0.6 f) (0.5 g)))
(= (kids d) ((0.9 h)))
(= (kids e) ((0.3 g)))
(= (kids f) ((0.8 h)))
(= (kids g) ((1.0 h)))
(= (kids h) ())

; ---------------------------------------------------------------------------
; 2. List machinery (frontier = list of (weight node) pairs)
;    Idiom: compute into lets; feed grounded utils only variables/data.
; ---------------------------------------------------------------------------

(= (append $xs $ys)
   (if (== $xs ()) $ys
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (let $rec (append $t $ys)
             (cons-atom $h $rec))))))

(= (remove-first $x $xs)
   (if (== $xs ()) ()
       (let $h (car-atom $xs)
         (if (== $h $x)
             (cdr-atom $xs)
             (let $t (cdr-atom $xs)
               (let $rec (remove-first $x $t)
                 (cons-atom $h $rec)))))))

(= (better $x $y)
   (if (> (car-atom $x) (car-atom $y)) $x $y))

(= (argmax $xs)
   (foldl-atom $xs (car-atom $xs) $acc $x (better $acc $x)))

(= (insert-grade $x $xs)
   (if (== $xs ()) (cons-atom $x ())
       (let $h (car-atom $xs)
         (if (> (car-atom $x) (car-atom $h))
             (cons-atom $x $xs)
             (let $t (cdr-atom $xs)
               (let $rec (insert-grade $x $t)
                 (cons-atom $h $rec)))))))

(= (sort-grades $xs)
   (if (== $xs ()) ()
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (let $rec (sort-grades $t)
             (insert-grade $h $rec))))))

(= (take2 $xs)
   (if (== $xs ()) ()
       (let $h (car-atom $xs)
         (let $t (cdr-atom $xs)
           (let $rest2
               (if (== $t ()) ()
                   (let $h2 (car-atom $t)
                     (cons-atom $h2 ())))
             (cons-atom $h $rest2))))))

(= (decay-pair $pair)
   (let ($w $n) $pair
     ((* 0.8 $w) $n)))

(= (decay $xs) (map-atom $xs $p (decay-pair $p)))

; The race total: what a NORMALIZED (sampled) discipline would divide by —
; the whole-bag quantity that cannot be computed per candidate.
(= (grade-sum $xs) (foldl-atom $xs 0 $acc $x (+ $acc (car-atom $x))))

; ---------------------------------------------------------------------------
; 3. Strategies, as data: pick and update per discipline
; ---------------------------------------------------------------------------

(= (pick bfs $frontier) (car-atom $frontier))
(= (pick dfs $frontier) (car-atom $frontier))
(= (pick greedy $frontier) (argmax $frontier))
(= (pick beam2 $frontier) (argmax $frontier))
(= (pick fickle $frontier) (argmax $frontier))

(= (update bfs $rest $kids) (append $rest $kids))
(= (update dfs $rest $kids) (append $kids $rest))
(= (update greedy $rest $kids) (append $rest $kids))
(= (update beam2 $rest $kids) (take2 (sort-grades (append $rest $kids))))
(= (update fickle $rest $kids) (append (decay $rest) (decay $kids)))

; ---------------------------------------------------------------------------
; 4. The search, once, for all disciplines
; ---------------------------------------------------------------------------

(= (search $strat $goal $frontier $steps $visited)
   (if (== $frontier ())
       (failed-in $strat after $steps)
       (if (>= $steps 60)
           (fuel-out-in $strat)
           (let $picked (pick $strat $frontier)
             (let ($w $node) $picked
               (if (== $node $goal)
                   (($strat found ($w $node) visited $visited))
                   (let $nf (force-value
                              (update $strat (remove-first $picked $frontier)
                                       (kids $node)))
                     (let $steps2 (+ $steps 1)
                       (search $strat $goal $nf $steps2
                               (append $visited (($w $node))))))))))))

; ---------------------------------------------------------------------------
; 5. Measure
; ---------------------------------------------------------------------------

; The race-total probe: 0.9 + 0.4 = 1.3 — a sample discipline would divide
; each candidate's grade by this; proving this number exists (finite bag) is
; exactly when a `sample`-family discipline becomes well-posed.
!(grade-sum ((0.9 b) (0.4 c)))

; Same step function, five disciplines, five lives:
!(search bfs h ((1.0 a)) 0 ())     ; queue: a b c d ... later h
!(search dfs h ((1.0 a)) 0 ())     ; stack: dives a b d h, short path
!(search greedy h ((1.0 a)) 0 ())  ; argmax each step
!(search beam2 h ((1.0 a)) 0 ())   ; windowed: only top 2 ever kept
!(search fickle h ((1.0 a)) 0 ())  ; gamma-decayed grades (0.8 per step)

; [WISHLIST] with a per-space discipline declaration these five call sites
; are one call site with five space profiles: the engine's own choice point
; reads the where-weights and applies the discipline; the frontier value,
; pick, update, and remove-first machinery disappears from user code for the
; ambient case — while remaining available (as here) for the reified lane,
; where strategies are first-class programs.  The two lanes are a feature,
; not a redundancy: this file IS what the engine lane would be compiled from.
