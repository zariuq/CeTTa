; Example: occurrence-weighted scheduling for the Prime ATP agenda.
;
; This module reuses ATP rule expansion and Prime's ordinary checker.  It
; changes only which concrete agenda-state occurrence is expanded next.  The
; random seed is explicit, and a strictly positive baseline hazard ensures
; that demand weighting cannot make an agenda occurrence unreachable.
;
; The experiment intentionally covers indexed rules and hypotheses only.
; Theorem facts are outside this producer's declared transition subrelation.

!(import! &self ./search_modules.metta)
!(import! &self lts:weighted)

(: atp:weighted-agenda:first-checked
  (-> SpaceType SpaceType Atom Number Number Number
      Atom Atom Number %Undefined%))

(= (atp:weighted-agenda:state-age
     (ATP.AgendaState
       $obligations $proof $next-hole $age
       $guidance-cost $unsupported))
   $age)

(= (atp:weighted-agenda:state-rate
     (atp:weighted-agenda:policy
       $baseline atp:weighted-agenda:uniform)
     $agenda-policy $state)
   $baseline)

(= (atp:weighted-agenda:state-rate
     (atp:weighted-agenda:policy
       $baseline
       (atp:weighted-agenda:demand $ceiling $scale))
     $agenda-policy $state)
   (let $cost (atp:agenda:state-cost $agenda-policy $state)
     (+ $baseline
        (* $scale
           (if (< $cost $ceiling)
               (- $ceiling $cost)
               0)))))

(= (atp:weighted-agenda:valid-policy
     (atp:weighted-agenda:policy
       $baseline atp:weighted-agenda:uniform))
   (if (> $baseline 0)
       True
       (atp:weighted-agenda:invalid-baseline $baseline)))

(= (atp:weighted-agenda:valid-policy
     (atp:weighted-agenda:policy
       $baseline
       (atp:weighted-agenda:demand $ceiling $scale)))
   (if (<= $baseline 0)
       (atp:weighted-agenda:invalid-baseline $baseline)
       (if (< $scale 0)
           (atp:weighted-agenda:invalid-scale $scale)
           True)))

(= (atp:weighted-agenda:events
     $agenda $weighted-policy $agenda-policy)
   (if (== $agenda ())
       ()
       (let ($state $rest) (decons-atom $agenda)
         (let $age (atp:weighted-agenda:state-age $state)
           (let $rate
                (atp:weighted-agenda:state-rate
                  $weighted-policy $agenda-policy $state)
             (cons-atom
               (lts:weighted:event
                 (atp:agenda:state-signature $state)
                 $age $rate $state)
               (atp:weighted-agenda:events
                 $rest $weighted-policy $agenda-policy)))))))

(= (atp:weighted-agenda:remove-age $wanted $agenda)
   (if (== $agenda ())
       ()
       (let ($state $rest) (decons-atom $agenda)
         (if (== (atp:weighted-agenda:state-age $state) $wanted)
             $rest
             (cons-atom
               $state
               (atp:weighted-agenda:remove-age $wanted $rest))))))

(= (atp:weighted-agenda:pop
     $seed $agenda $weighted-policy $agenda-policy)
   (case (atp:weighted-agenda:valid-policy $weighted-policy)
     ((True
        (case
          (lts:weighted:draw
            $seed
            (atp:weighted-agenda:events
              $agenda $weighted-policy $agenda-policy))
          (((lts:weighted:draw-result
              (lts:weighted:event
                $semantic $age $rate $state)
              $next-seed)
             (atp:weighted-agenda:pop-result
               $state
               (atp:weighted-agenda:remove-age $age $agenda)
               $next-seed $age $rate))
           ($failure $failure))))
      ($invalid $invalid))))

; The legal producer is the existing indexed hypothesis/rule expansion.  The
; checker below remains the sole authority for accepting a terminal proof.
(= (atp:weighted-agenda:expand $knowledge $index $state $candidate-policy)
   (let (ATP.AgendaState
          $obligations $proof $next-hole $age
          $guidance-cost $unsupported)
        $state
     (if (== $obligations ())
         (empty)
         (let ((ATP.Obligation $hole $goal $depth $ancestors) $rest)
              (decons-atom $obligations)
           (if (atp:indexed:seen-goal $goal $ancestors)
               (empty)
               (superpose
                 ((atp:agenda:expand-hypothesis
                    $knowledge $index $state)
                  (atp:agenda:expand-ranked-rule
                    $knowledge $index $state $candidate-policy))))))))

(= (atp:weighted-agenda:bounded-successors
     $knowledge $index $state $candidate-policy $limit)
   (let $all
        (reify
          (atp:weighted-agenda:expand
            $knowledge $index $state $candidate-policy))
     (atp:weighted-agenda:successor-batch
       (atp:agenda:take $limit $all)
       (if (> (size-atom $all) $limit) True False))))

(= (atp:weighted-agenda:finish
     $outcome $expanded $seen $seed)
   (atp:weighted-agenda:run-result
     (ATP.AgendaSearchResult
       $outcome (ATP.AgendaStats $expanded (size-atom $seen)))
     $seed))

(= (atp:weighted-agenda:search-loop
     $knowledge $index $original-goal
     $agenda $seen $steps $expanded $next-age
     $fuel $agenda-policy $candidate-policy
     $weighted-policy $seed $truncated)
   (if (== $agenda ())
       (atp:weighted-agenda:finish
         (atp:agenda:incomplete-reason
           NoInhabitantWithinBounds $truncated)
         $expanded $seen $seed)
       (if (<= $steps 0)
           (atp:weighted-agenda:finish
             (atp:agenda:incomplete-reason StepBound $truncated)
             $expanded $seen $seed)
           (case
             (atp:weighted-agenda:pop
               $seed $agenda $weighted-policy $agenda-policy)
             (((atp:weighted-agenda:pop-result
                 $state $rest-agenda $after-pop-seed
                 $occurrence $rate)
                (let (ATP.AgendaState
                       $obligations $proof $next-hole $age
                       $guidance-cost $unsupported)
                     $state
                  (if (== $obligations ())
                      (case
                        (check-type
                          $knowledge $proof $original-goal $fuel)
                        (((he-accept (exact $checked))
                           (atp:weighted-agenda:finish
                             (ATP.AgendaChecked
                               $proof $original-goal (exact $checked))
                             $expanded $seen $after-pop-seed))
                         ((he-accept (structural $checked))
                           (atp:weighted-agenda:finish
                             (ATP.AgendaChecked
                               $proof $original-goal (structural $checked))
                             $expanded $seen $after-pop-seed))
                         ($_
                           (atp:weighted-agenda:search-loop
                             $knowledge $index $original-goal
                             $rest-agenda $seen (- $steps 1)
                             (+ $expanded 1) $next-age
                             $fuel $agenda-policy $candidate-policy
                             $weighted-policy $after-pop-seed $truncated))))
                      (if (atp:agenda:seen-state $state $seen)
                          (atp:weighted-agenda:search-loop
                            $knowledge $index $original-goal
                            $rest-agenda $seen (- $steps 1)
                            (+ $expanded 1) $next-age
                            $fuel $agenda-policy $candidate-policy
                            $weighted-policy $after-pop-seed $truncated)
                          (let $successor-limit
                               (eval
                                 (atp:agenda:max-successors
                                   $agenda-policy))
                            (let (atp:weighted-agenda:successor-batch
                                   $selected-successors $local-truncated)
                                 (atp:weighted-agenda:bounded-successors
                                   $knowledge $index $state
                                   $candidate-policy $successor-limit)
                              (let (ATP.AgendaPack
                                     $local-successors
                                     $after-local-truncated $after-age)
                                   (fold
                                     (superpose $selected-successors)
                                     (ATP.AgendaPack
                                       () $local-truncated $next-age)
                                     $pack $successor
                                     (atp:agenda:add-local-successor
                                       $pack $successor $agenda-policy))
                                (let (ATP.AgendaPack
                                       $next-agenda
                                       $next-truncated $merged-age)
                                     (atp:agenda:merge-local
                                       $local-successors $rest-agenda
                                       $after-local-truncated $truncated
                                       $after-age $agenda-policy)
                                  (atp:weighted-agenda:search-loop
                                    $knowledge $index $original-goal
                                    $next-agenda
                                    (cons-atom
                                      (atp:agenda:state-signature $state)
                                      $seen)
                                    (- $steps 1) (+ $expanded 1) $merged-age
                                    $fuel $agenda-policy $candidate-policy
                                    $weighted-policy $after-pop-seed
                                    $next-truncated)))))))))
              ($failure
                (atp:weighted-agenda:run-result
                  $failure $seed)))))))

(= (atp:weighted-agenda:first-checked
     $knowledge $index $goal $depth $steps $fuel
     (ATP.AgendaSearch $agenda-policy $candidate-policy)
     $weighted-policy $seed)
   (let $initial-obligations
        ((ATP.Obligation 0 $goal $depth ()))
     (let $initial-unsupported
          (atp:agenda:unsupported-obligations
            $knowledge $index $initial-obligations)
       (atp:weighted-agenda:search-loop
         $knowledge $index $goal
         ((ATP.AgendaState
            $initial-obligations
            (ATP.Hole 0) 1 0 0 $initial-unsupported))
         () $steps 0 1 $fuel
         $agenda-policy $candidate-policy
         $weighted-policy $seed False))))
