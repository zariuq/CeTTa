!(import! &self lts:weighted)

(= (prism:events)
   ((lts:weighted:event authored-a concrete-a 1 shared-target)
    (lts:weighted:event authored-b concrete-b 3 shared-target)))

(= (prism:refactored-events)
   ((lts:weighted:event authored-work encoding-left 1 done)
    (lts:weighted:event authored-work encoding-right 2 done)
    (lts:weighted:event authored-other concrete-other 5 done)))

(= (prism:authored-events)
   ((lts:weighted:event authored-work source-work 3 done)
    (lts:weighted:event authored-other source-other 5 done)))

(= (prism:count-draws
     $remaining $seed $events $left $right)
   (if (<= $remaining 0)
       (prism:draw-counts $left $right $seed)
       (let (lts:weighted:draw-result
              (lts:weighted:event
                $semantic $occurrence $rate $target)
              $next-seed)
            (lts:weighted:draw $seed $events)
         (prism:count-draws
           (- $remaining 1)
           $next-seed
           $events
           (+ $left (if (noreduce-eq $occurrence concrete-a) 1 0))
           (+ $right (if (noreduce-eq $occurrence concrete-b) 1 0))))))

(= (prism:simulate-two-state
     $remaining A $seed $time-a $time-b)
   (if (<= $remaining 0)
       (prism:ctmc-times $time-a $time-b $seed)
       (let (lts:weighted:wait-result $duration $next-seed)
            (lts:weighted:exponential-wait $seed 1)
         (prism:simulate-two-state
           (- $remaining 1) B $next-seed
           (+ $time-a $duration) $time-b))))

(= (prism:simulate-two-state
     $remaining B $seed $time-a $time-b)
   (if (<= $remaining 0)
       (prism:ctmc-times $time-a $time-b $seed)
       (let (lts:weighted:wait-result $duration $next-seed)
            (lts:weighted:exponential-wait $seed 3)
         (prism:simulate-two-state
           (- $remaining 1) A $next-seed
           $time-a (+ $time-b $duration)))))

; Pure seed replay and input validation.
!(assertEqual (lts:weighted:next-seed 1) 1103527590)
!(assertEqual
   (lts:weighted:draw 1 (prism:events))
   (lts:weighted:draw 1 (prism:events)))
!(assertEqual
   (lts:weighted:draw 7 ())
   (lts:weighted:no-positive-rate 7))
!(assertEqual
   (lts:weighted:draw
     7 ((lts:weighted:event authored bad -1 target)))
   (lts:weighted:invalid-rate bad -1))

; Administrative encodings sum over their declared authored fiber.
!(assertEqual
   (lts:weighted:semantic-rate authored-work
     (prism:refactored-events))
   3)
!(assertEqual
   (lts:weighted:total-rate (prism:refactored-events))
   8)
!(assertEqual
   (lts:weighted:semantic-rate authored-work
     (prism:refactored-events))
   (lts:weighted:semantic-rate authored-work
     (prism:authored-events)))
!(assertEqual
   (lts:weighted:total-rate (prism:refactored-events))
   (lts:weighted:total-rate (prism:authored-events)))

; Distinct authored occurrences remain distinct even at one successor state.
!(assertEqual
   (lts:weighted:semantic-rate authored-a (prism:events))
   1)
!(assertEqual
   (lts:weighted:semantic-rate authored-b (prism:events))
   3)
!(assertEqual
   (lts:weighted:total-rate (prism:events))
   4)

; Machine-readable experimental observations.  The external gate compares
; these deterministic observations with the analytic 1:3 competing-hazard and
; stationary CTMC laws.
!(prism:count-draws 128 1 (prism:events) 0 0)
!(prism:simulate-two-state 128 A 1 0.0 0.0)
