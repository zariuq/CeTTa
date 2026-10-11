!(import! &self ../../../lib/native_search_goals.metta)

(= (later $n $value)
   (if (== $n 0) $value (later (- $n 1) $value)))
(= (routes) (Route a (0) 8))
(= (routes) (Route b (0) (later 48 1)))
(= (routes) (Route c (1) (later 48 2)))
(= (routes) (Route d (2) (later 48 3)))
(= (routes) (Route e (3) (later 48 4)))
(= (routes) (Route f (4) (later 48 5)))
(= (routes) (Route g (5) (later 48 6)))
(= (routes) (Route h (6) (later 48 7)))

; Keep producer work pending while the initial joint predicate and revised
; marginal-coverage advice execute through their owned Need continuations.
; Count marginal coverage against a program-owned partial witness collection.
(= (new-coverage $coverage $chosen)
   (if (== $coverage ()) 0
     (let ($head $tail) (decons-atom $coverage)
       (+ (if (search:covered? $head $chosen) 0 1)
          (new-coverage $tail $chosen))){$head $tail}))

; The first witness is tentative: a cannot coexist with the necessary h.
; Changing the cardinality/goal restarts only the consumer, retaining the source.
; The final key-only resume preserves that joint cursor and its pending goals.
!(let (Observation $seed (satisfied $s0) (closed $c0) (residual $r0) (work $w0) (status $t0))
   (select 1 (search-policy (control WO
     (key (Route $id $coverage $cost) 0)
     (goal $selected (search:joint? (0) () $selected))
     (retain True))) (routes))
   (let (Observation $partial (satisfied $s1) (closed $c1) (residual $r1) (work $w1) (status $t1)
          (receipt-capture $capture1) (consumer-revisions $revisions1))
     (select 7 (search-policy (control WO
       (key (Route $id $coverage $cost) 0)
       (goal $selected (search:joint? (0 1 2 3 4 5 6) ((a h)) $selected))
       (receipts True) (budget 80))) $r0)
     (Feedback $seed $partial $w1
       (select 7 (search-policy (control WO
         (key (Route $id $coverage $cost) (- 0 (new-coverage $coverage $seed)))
         (goal $selected (search:joint? (0 1 2 3 4 5 6) ((a h)) $selected))
         (receipts True) (budget 200000))) $r1))))
