; Pure occurrence-weighted transition selection.
;
; This opt-in library does not change a language's transition relation.  Its
; input is a finite expression of explicit occurrences:
;
;   (lts:weighted:event semantic-id occurrence-id rate target)
;
; `semantic-id` identifies the authored event to which an implementation
; occurrence maps.  Distinct `occurrence-id` values remain distinct draws.
; Rates over a semantic fiber add; the target alone never determines a rate.
;
; Random state is an explicit argument and result.  Replaying a seed therefore
; replays the same draw, and using this module cannot introduce hidden effects.
; The linear congruential generator is an experiment carrier, not a claim of
; cryptographic or statistical quality.

(: lts:weighted:next-seed (-> Number Number))
(: lts:weighted:unit-after (-> Number Number))
(: lts:weighted:event (-> Atom Atom Number Atom Atom))
(: lts:weighted:validate (-> Expression %Undefined%))
(: lts:weighted:total-rate (-> Expression Number))
(: lts:weighted:semantic-rate (-> Atom Expression Number))
(: lts:weighted:draw (-> Number Expression %Undefined%))
(: lts:weighted:exponential-wait (-> Number Number %Undefined%))
(: lts:weighted:fire (-> Number Expression %Undefined%))

(= (lts:weighted:next-seed $seed)
   (% (+ (* 1103515245 $seed) 12345) 2147483648))

; A reproducible open-unit-interval variate associated with the next seed.
(= (lts:weighted:unit-after $seed)
   (/ (+ (lts:weighted:next-seed $seed) 1.0) 2147483649.0))

(= (lts:weighted:validate ()) True)

(= (lts:weighted:validate $events)
   (let ((lts:weighted:event
           $semantic $occurrence $rate $target)
          $rest)
        (decons-atom $events)
     (if (< $rate 0)
         (lts:weighted:invalid-rate $occurrence $rate)
         (lts:weighted:validate $rest))))

(= (lts:weighted:total-rate ()) 0)

(= (lts:weighted:total-rate $events)
   (let ((lts:weighted:event
           $semantic $occurrence $rate $target)
          $rest)
        (decons-atom $events)
     (+ $rate (lts:weighted:total-rate $rest))))

(= (lts:weighted:semantic-rate $wanted ()) 0)

(= (lts:weighted:semantic-rate $wanted $events)
   (let ((lts:weighted:event
           $semantic $occurrence $rate $target)
          $rest)
        (decons-atom $events)
     (+ (if (noreduce-eq $wanted $semantic) $rate 0)
        (lts:weighted:semantic-rate $wanted $rest))))

(= (lts:weighted:pick-slot $slot ())
   (lts:weighted:selection-invariant-broken $slot))

(= (lts:weighted:pick-slot $slot $events)
   (let ($event $rest) (decons-atom $events)
     (let (lts:weighted:event
            $semantic $occurrence $rate $target)
          $event
       (if (< $slot $rate)
           $event
           (lts:weighted:pick-slot (- $slot $rate) $rest)))))

; A draw advances the explicit seed exactly once.  Zero-rate occurrences are
; legal inputs but cannot be selected.  A frontier with no positive total
; rate returns data rather than pretending to make a transition.
(= (lts:weighted:draw $seed $events)
   (case (lts:weighted:validate $events)
     ((True
        (let $total (lts:weighted:total-rate $events)
          (if (<= $total 0)
              (lts:weighted:no-positive-rate $seed)
              (let $next (lts:weighted:next-seed $seed)
                (let $slot (* (lts:weighted:unit-after $seed) $total)
                  (let $event
                       (lts:weighted:pick-slot $slot $events)
                    (lts:weighted:draw-result $event $next)))))))
      ($invalid $invalid))))

; Waiting time for an enabled CTMC frontier.  The rate is the total outgoing
; hazard, and the returned seed makes the consumed random state explicit.
(= (lts:weighted:exponential-wait $seed $rate)
   (if (<= $rate 0)
       (lts:weighted:no-positive-rate $seed)
       (let $next (lts:weighted:next-seed $seed)
         (let $unit (lts:weighted:unit-after $seed)
           (lts:weighted:wait-result
             (/ (- 0 (log-math EXP $unit)) $rate)
             $next)))))

; One direct-method (Gillespie) firing: one variate for the waiting time and a
; second for occurrence selection.  Selection is still over occurrences, not
; over deduplicated successor states.
(= (lts:weighted:fire $seed $events)
   (case (lts:weighted:validate $events)
     ((True
        (let $total (lts:weighted:total-rate $events)
          (case (lts:weighted:exponential-wait $seed $total)
            (((lts:weighted:wait-result $duration $wait-seed)
               (case (lts:weighted:draw $wait-seed $events)
                 (((lts:weighted:draw-result $event $draw-seed)
                    (lts:weighted:firing
                      $event $duration $draw-seed))
                  ($draw-failure $draw-failure))))
             ($wait-failure $wait-failure)))))
      ($invalid $invalid))))
