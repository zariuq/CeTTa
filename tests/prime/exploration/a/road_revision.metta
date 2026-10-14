; Which results survive which change.  A road map with four towns: major
; roads a-b and b-d, minor roads a-c and c-d.  The world is evidence about
; each road, written (road open-reports closed-reports); a road counts as
; open when its open reports outnumber its closed ones.  The question is
; whether d can be reached from a.  Four kinds of change are kept apart:
; the world is revised (a report that a road is closed arrives), the view
; is changed (minor roads are hidden), the strategy is changed (the search
; stops early), and the implementation is changed (the world is kept as a
; log of reports).
;
; A view of the world supports a revision when the view after the revision
; can be computed from the view before it.  It fails to when two worlds with
; one view now have different views after the revision: the view splits.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

(road ab a b)
(road bd b d)
(road ac a c)
(road cd c d)

(= (world all-open)    ((ab 1 0) (bd 1 0) (ac 1 0) (cd 1 0)))
(= (world major-only)  ((ab 1 0) (bd 1 0) (ac 0 1) (cd 0 1)))
(= (world wide-margin) ((ab 1 0) (bd 5 0) (ac 0 1) (cd 0 1)))

; ---- evidence and revision --------------------------------------------------

(= (evidence $w $r)
   (let ($road $open $closed) (car-atom $w)
     (if (== $road $r) ($open $closed)
         (let $rest (cdr-atom $w) (evidence $rest $r)){$rest})))
(= (is-open $w $r) (let ($open $closed) (evidence $w $r) (< $closed $open)))

; Revision adds the evidence of a report, road by road.
(= (combine ($o1 $c1) ($o2 $c2)) ((+ $o1 $o2) (+ $c1 $c2)))
(= (revise $w $report)
   (map-atom $w $entry
     (let ($road $open $closed) $entry
       (let ($o $c) (combine ($open $closed) (evidence $report $road))
         ($road $o $c)))))
(= (closed-report $r)
   (map-atom (ab bd ac cd) $road (if (== $road $r) ($road 0 1) ($road 0 0))))
(= (close $r $w) (let $report (closed-report $r) (revise $w $report)))

; ---- the answer: breadth-first search, three rounds for four towns ---------

(= (major ab) True)
(= (major bd) True)
(= (major ac) False)
(= (major cd) False)
; The roads a search may use: every open road, or the open major roads.
(= (usable every-road $w $r) (is-open $w $r))
(= (usable major-road $w $r) (and (major $r) (is-open $w $r)))
(= (adjacent $use $w $s $t)
   (let $roads (collapse (let ($r $x $y) (match &self (road $r $x $y) ($r $x $y)){$r $x $y}
                           (if (and (usable $use $w $r)
                                    (or (and (== $x $s) (== $y $t))
                                        (and (== $x $t) (== $y $s))))
                               $r (empty))){$r $x $y})
     (not (== $roads ()))))
; The towns reached from the source after n rounds.
(= (reached-after $use $w $source $n $t)
   (if (== $n 0) (== $t $source)
       (let $m (- $n 1)
         (or (reached-after $use $w $source $m $t)
             (let $via (collapse (let $s (superpose (a b c d))
                                   (if (and (reached-after $use $w $source $m $s)
                                            (adjacent $use $w $s $t))
                                       $s (empty))){$s})
               (not (== $via ()))){$via})){$m}))
(= (answer $w) (reached-after every-road $w a 3 d))
(= (open-map $w) (map-atom (ab bd ac cd) $r (is-open $w $r)))

; A view splits under closing road r at two worlds.
(= (splits $view $r $w1 $w2)
   (let* (($v1 ($view $w1)) ($v2 ($view $w2))
          ($x1 (close $r $w1)) ($x2 (close $r $w2)))
     (and (== $v1 $v2) (not (== ($view $x1) ($view $x2))))))

; ---- world revision: a road closes ------------------------------------------

; 1. With every road open, and with only the major roads open, d is reachable
;    (answer_allOpen, answer_majorOnly).
!(let $w (world all-open) (answer $w))
!(let $w (world major-only) (answer $w))
; 2. Road b-d closes.  With the minor roads open there is still a way
;    (answer_close_allOpen); with only the major roads there is none
;    (answer_close_majorOnly).
!(let $w (world all-open) (answer (close bd $w)))
!(let $w (world major-only) (answer (close bd $w)))
; 3. So the present answer does not support the revision: the two worlds
;    agree on it now and disagree after (answer_not_supports_close), and the
;    old answer is no longer valid (answer_not_valid_close).
!(let* (($w1 (world all-open)) ($w2 (world major-only))) (splits answer bd $w1 $w2))
!(let $w (world major-only) (== (answer (close bd $w)) (answer $w)))
; 4. The evidence supports it: reading a road after the revision is combining
;    what was read before with the report (evidence_supports_close, from the
;    reading's laws, coreLaws).
(= (square-closes $r $w)
   (let* (($after (close $r $w)) ($report (closed-report $r))
          ($bad (collapse (let $q (superpose (ab bd ac cd))
                            (if (== (evidence $after $q)
                                    (combine (evidence $w $q) (evidence $report $q)))
                                (empty) $q)){$q})))
     (== $bad ())))
!(let $w (world all-open) (square-closes bd $w))
!(let $w (world major-only) (square-closes bd $w))
; 5. Even the whole map of open roads, which fixes every present answer, does
;    not support the revision: one map, and one closed report closes b-d in
;    one world only (openMap_not_supports_close).
!(let* (($w1 (world wide-margin)) ($w2 (world major-only))) (splits open-map bd $w1 $w2))

; ---- abstraction: the minor roads are hidden ----------------------------------

(= (major-view $w) (filter-atom $w $entry (let ($road $open $closed) $entry (major $road))))
(= (major-answer $w) (reached-after major-road $w a 3 d))
(= (detour) (let $w (world all-open) (close bd $w)))
(= (blocked) (let $w (world major-only) (close bd $w)))

; 6. After b-d closes, the world with open minor roads and the world without
;    them look alike through the major roads (majorView_detour_blocked), yet
;    d is reachable in one and not in the other: the answer cannot be read
;    off the major view (answer_not_descends_majorView).
!(let* (($x (detour)) ($y (blocked))) (== (major-view $x) (major-view $y)))
!(let* (($x (detour)) ($y (blocked))) (== (answer $x) (answer $y)))
; 7. The answer over the major roads can be read off the major view
;    (majorAnswer_descends), and it is not the full answer
;    (majorAnswer_ne_answer).
!(let* (($x (detour)) ($y (blocked))) (== (major-answer $x) (major-answer $y)))
!(let $x (detour) (== (major-answer $x) (answer $x)))

; ---- strategy: the search stops early ---------------------------------------

; A strategy's answer and the rounds it ran.  The exhaustive search runs all
; three rounds; early exit stops at the first round that reaches the target.
(= (search-from $w $s $t $round $fuel)
   (let $hit (reached-after every-road $w $s $round $t)
     (if (== $fuel 0) ($hit $round)
         (if $hit (True $round) (search-from $w $s $t (+ $round 1) (- $fuel 1))))))
(= (search-with exhaustive $w $s $t) (let $hit (reached-after every-road $w $s 3 $t) ($hit 3)))
(= (search-with early-exit $w $s $t) (search-from $w $s $t 0 3))

; 8. Early exit keeps every answer and never runs more rounds
;    (earlyExit_improves), also on the revised world
;    (earlyExit_answer_after_close) ...
!(let $w (world all-open)
   (let* ((($h1 $n1) (search-with early-exit $w a b)) (($h2 $n2) (search-with exhaustive $w a b)))
     (and (== $h1 $h2) (<= $n1 $n2))))
!(let $w (blocked)
   (let* ((($h1 $n1) (search-with early-exit $w a d)) (($h2 $n2) (search-with exhaustive $w a d)))
     (and (== $h1 $h2) (<= $n1 $n2))))
; 9. ... but its cost differs, so demanding the same cost would forbid it
;    (earlyExit_not_preserves_cost).
!(let $w (world all-open)
   (let* ((($h1 $n1) (search-with early-exit $w a b)) (($h2 $n2) (search-with exhaustive $w a b)))
     (== $n1 $n2)))

; ---- implementation: the world as a log of reports -----------------------------

; A report is (road seen-open).  The tally of a log is the evidence it holds.
(= (count-reports $log $r $seen)
   (foldl-atom $log 0 $n $report (if (== $report ($r $seen)) (+ $n 1) $n)))
(= (tally $log)
   (map-atom (ab bd ac cd) $r
     (let* (($open (count-reports $log $r True)) ($closed (count-reports $log $r False)))
       ($r $open $closed))))
(= (append-report $log $report)
   (if (== $log ()) ($report)
       (let* (($h (car-atom $log)) ($t (cdr-atom $log)) ($rest (append-report $t $report)))
         (cons-atom $h $rest)){$h $t $rest}))
; Closing a road in the log appends a closed report; a compacting version
; keeps only that latest report about the road.
(= (close-log $r $log) (append-report $log ($r False)))
(= (close-latest $r $log)
   (let $kept (filter-atom $log $report (let ($road $seen) $report (not (== $road $r))))
     (append-report $kept ($r False))))

; 10. Appending a closed report and then tallying is tallying and then
;     closing the road (tally_close) ...
!(let* (($log ((ab True) (bd True) (ac True) (cd True) (bd True)))
        ($x (close-log bd $log)) ($y (tally $log)))
   (== (tally $x) (close bd $y)))
; 11. ... while compacting to the latest report is not a refinement of
;     closing (closeLatest_not_refines) ...
!(let* (($log ((bd True) (bd True)))
        ($x (close-latest bd $log)) ($y (tally $log)))
   (== (tally $x) (close bd $y)))
; 12. ... and the first report cannot be read off the tally: two orders of
;     the same reports (firstReport_not_descends).
!(let* (($l1 ((ab True) (bd True))) ($l2 ((bd True) (ab True))))
   (and (== (tally $l1) (tally $l2)) (not (== (car-atom $l1) (car-atom $l2)))))
