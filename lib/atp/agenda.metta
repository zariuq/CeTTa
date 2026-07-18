; Best-first direct inhabitation as an explicit MeTTa search producer.
;
; A state contains a finite list of typed obligations and a partial proof term
; whose holes name those obligations.  Ordinary MeTTa matching/unification
; expands one obligation with a fact or marked chaining rule.  The agenda,
; loop check, state score, and resource bounds are library policy; acceptance
; remains an ordinary check-type call on a hole-free result.

(= (atp:agenda:append () $right) $right)

(= (atp:agenda:append $left $right)
   (let ($head $tail) (decons-atom $left)
     (let $rest (eval (atp:agenda:append $tail $right))
       (cons-atom $head $rest))))

(= (atp:agenda:replace-hole $proof $hole $replacement)
   (case $proof
     (((ATP.Hole $candidate)
        (if (== $candidate $hole) $replacement $proof))
      ($_
        (if (== (get-metatype $proof) Expression)
            (atp:agenda:replace-hole-list
              $proof $hole $replacement)
            $proof)))))

(= (atp:agenda:replace-hole-list () $hole $replacement) ())

(= (atp:agenda:replace-hole-list $items $hole $replacement)
   (let ($head $tail) (decons-atom $items)
     (let $new-head
          (eval (atp:agenda:replace-hole $head $hole $replacement))
       (let $new-tail
            (eval
              (atp:agenda:replace-hole-list
                $tail $hole $replacement))
         (cons-atom $new-head $new-tail)))))

(= (atp:agenda:fresh-obligations
     () $next-hole $depth $ancestors)
   (ATP.FreshObligations () () $next-hole))

(= (atp:agenda:fresh-obligations
     $premises $next-hole $depth $ancestors)
   (let ($premise $rest) (decons-atom $premises)
     (let (ATP.FreshObligations
            $obligations $holes $after)
          (atp:agenda:fresh-obligations
            $rest (+ $next-hole 1) $depth $ancestors)
       (ATP.FreshObligations
         (cons-atom
           (ATP.Obligation $next-hole $premise $depth $ancestors)
           $obligations)
         (cons-atom (ATP.Hole $next-hole) $holes)
         $after))))

(= (atp:agenda:obligation-weight ()) 0)

(= (atp:agenda:obligation-weight $obligations)
   (let ($obligation $rest) (decons-atom $obligations)
     (let (ATP.Obligation $hole $goal $depth $ancestors) $obligation
       (+ (atp:feature:term-weight $goal)
          (atp:agenda:obligation-weight $rest)))))

(= (atp:agenda:unsupported-obligations
     $knowledge $index ())
   0)

(= (atp:agenda:unsupported-obligations
     $knowledge $index $obligations)
   (let ((ATP.Obligation $hole $goal $depth $ancestors) $rest)
        (decons-atom $obligations)
     (+ (if (== (collapse
                   (atp:indexed:hypothesis $knowledge $index $goal))
                ())
            1
            0)
        (atp:agenda:unsupported-obligations
          $knowledge $index $rest))))

(= (atp:agenda:state-cost
     (ATP.AgendaPolicy
       $goal-coefficient $obligation-coefficient
       $unsupported-coefficient $proof-coefficient
       $guidance-coefficient $age-coefficient
       $max-agenda $max-successors)
     (ATP.AgendaState
       $obligations $proof $next-hole $age
       $guidance-cost $unsupported))
   (+ (* $goal-coefficient
         (atp:agenda:obligation-weight $obligations))
      (+ (* $obligation-coefficient (size-atom $obligations))
         (+ (* $unsupported-coefficient $unsupported)
            (+ (* $proof-coefficient (atp:feature:term-weight $proof))
               (+ (* $guidance-coefficient $guidance-cost)
                  (* $age-coefficient $age)))))))

(= (atp:agenda:set-age
     (ATP.AgendaState
       $obligations $proof $next-hole $old-age
       $guidance-cost $unsupported)
     $age)
   (ATP.AgendaState
     $obligations $proof $next-hole $age
     $guidance-cost $unsupported))

(= (atp:agenda:insert $state () $policy)
   ($state))

(= (atp:agenda:insert $state $agenda $policy)
   (let ($head $tail) (decons-atom $agenda)
     (if (< (atp:agenda:state-cost $policy $state)
            (atp:agenda:state-cost $policy $head))
         (cons-atom $state $agenda)
         (let $inserted
              (eval (atp:agenda:insert $state $tail $policy))
           (cons-atom $head $inserted)))))

(= (atp:agenda:take $limit $items)
   (if (<= $limit 0)
       ()
       (if (== $items ())
           ()
           (let ($head $tail) (decons-atom $items)
             (let $rest
                  (eval (atp:agenda:take (- $limit 1) $tail))
               (cons-atom $head $rest))))))

(= (atp:agenda:add-successor-with-limit
     (ATP.AgendaPack $agenda $truncated $next-age)
     $successor
     (ATP.AgendaPolicy
       $goal-coefficient $obligation-coefficient
       $unsupported-coefficient $proof-coefficient
       $guidance-coefficient $age-coefficient
       $max-agenda $max-successors)
     $limit)
   (let $aged (atp:agenda:set-age $successor $next-age)
     (let $inserted
          (atp:agenda:insert
            $aged $agenda
            (ATP.AgendaPolicy
              $goal-coefficient $obligation-coefficient
              $unsupported-coefficient $proof-coefficient
              $guidance-coefficient $age-coefficient
              $max-agenda $max-successors))
       (if (> (size-atom $inserted) $limit)
           (ATP.AgendaPack
             (atp:agenda:take $limit $inserted)
             True (+ $next-age 1))
           (ATP.AgendaPack
             $inserted $truncated (+ $next-age 1))))))

(= (atp:agenda:add-local-successor
     $pack $successor
     (ATP.AgendaPolicy
       $goal-coefficient $obligation-coefficient
       $unsupported-coefficient $proof-coefficient
       $guidance-coefficient $age-coefficient
       $max-agenda $max-successors))
   (atp:agenda:add-successor-with-limit
     $pack $successor
     (ATP.AgendaPolicy
       $goal-coefficient $obligation-coefficient
       $unsupported-coefficient $proof-coefficient
       $guidance-coefficient $age-coefficient
       $max-agenda $max-successors)
     $max-successors))

(= (atp:agenda:merge-successor
     (ATP.AgendaPack $agenda $truncated $next-age)
     $successor
     (ATP.AgendaPolicy
       $goal-coefficient $obligation-coefficient
       $unsupported-coefficient $proof-coefficient
       $guidance-coefficient $age-coefficient
       $max-agenda $max-successors))
   (let $inserted
        (atp:agenda:insert
          $successor $agenda
          (ATP.AgendaPolicy
            $goal-coefficient $obligation-coefficient
            $unsupported-coefficient $proof-coefficient
            $guidance-coefficient $age-coefficient
            $max-agenda $max-successors))
     (if (> (size-atom $inserted) $max-agenda)
         (ATP.AgendaPack
           (atp:agenda:take $max-agenda $inserted)
           True $next-age)
         (ATP.AgendaPack $inserted $truncated $next-age))))

(= (atp:agenda:merge-local
     $local-successors $rest-agenda
     $local-truncated $global-truncated $after-age $policy)
   (let $combined-truncated
        (if $local-truncated True $global-truncated)
     (if (== $local-successors ())
         (ATP.AgendaPack
           $rest-agenda $combined-truncated $after-age)
         (fold
           (superpose $local-successors)
           (ATP.AgendaPack
             $rest-agenda $combined-truncated $after-age)
           $merge-pack $successor
           (atp:agenda:merge-successor
             $merge-pack $successor $policy)))))

(= (atp:agenda:state-signature
     (ATP.AgendaState
       $obligations $proof $next-hole $age
       $guidance-cost $unsupported))
   $obligations)

(= (atp:agenda:max-successors
     (ATP.AgendaPolicy
       $goal-coefficient $obligation-coefficient
       $unsupported-coefficient $proof-coefficient
       $guidance-coefficient $age-coefficient
       $max-agenda $max-successors))
   $max-successors)

(= (atp:agenda:seen-state $state ()) False)

(= (atp:agenda:seen-state $state $seen)
   (let ($signature $rest) (decons-atom $seen)
     (if (=alpha (atp:agenda:state-signature $state) $signature)
         True
         (atp:agenda:seen-state $state $rest))))

(= (atp:agenda:expand-hypothesis
     $knowledge $index
     (ATP.AgendaState
       $obligations $partial $next-hole $age
       $guidance-cost $unsupported))
   (let ((ATP.Obligation $hole $goal $depth $ancestors) $rest)
        (decons-atom $obligations)
     (let (: $proof $goal)
          (atp:indexed:hypothesis $knowledge $index $goal)
       (ATP.AgendaState
         $rest
         (atp:agenda:replace-hole $partial $hole $proof)
         $next-hole $age $guidance-cost
         (atp:agenda:unsupported-obligations
           $knowledge $index $rest)))))

(= (atp:agenda:expand-theorem-fact
     $knowledge $index
     (ATP.AgendaState
       $obligations $partial $next-hole $age
       $guidance-cost $unsupported)
     $fact-limit)
   (let ((ATP.Obligation $hole $goal $depth $ancestors) $rest)
        (decons-atom $obligations)
     (let (: $proof $goal)
          (atp:indexed:bounded-theorem-fact
            $knowledge $index $goal $fact-limit)
       (ATP.AgendaState
         $rest
         (atp:agenda:replace-hole $partial $hole $proof)
         $next-hole $age $guidance-cost
         (atp:agenda:unsupported-obligations
           $knowledge $index $rest)))))

(= (atp:agenda:candidate-fact-limit $candidate-policy)
   (let (ATP.DirectSearch $selection $premise-order) $candidate-policy
     (case $selection
       (((ATP.TopK $limit $ranker) $limit)
        ((ATP.CoarseToFine
           $coarse-limit $fact-limit $final-limit
           $coarse-ranker $fine-ranker)
          $fact-limit)
        ($_ 1)))))

(= (atp:agenda:candidate-cost $candidate-policy $candidate)
   (let (ATP.DirectSearch $selection $premise-order) $candidate-policy
     (let $raw-cost
          (case $selection
            (((ATP.TopK $limit $ranker)
               (atp:policy:cost $ranker $candidate))
             ((ATP.CoarseToFine
                $coarse-limit $fact-limit $final-limit
                $coarse-ranker $fine-ranker)
               (atp:policy:cost $fine-ranker $candidate))
             ($_ 0)))
       (if (< $raw-cost 0) 0 $raw-cost))))

(= (atp:agenda:expand-ranked-rule
     $knowledge $index
     (ATP.AgendaState
       $obligations $partial $next-hole $age
       $guidance-cost $unsupported)
     $candidate-policy)
   (let ((ATP.Obligation $hole $goal $depth $ancestors) $rest)
        (decons-atom $obligations)
     (if (<= $depth 0)
         Empty
         (let (ATP.Candidate
                $source-age $generality $misses $support
                $rule $premises $goal)
              (atp:indexed:ranked-candidate
                $knowledge $index $goal $candidate-policy)
           (let (ATP.FreshObligations
                  $new-obligations $new-holes $after)
                (atp:agenda:fresh-obligations
                  $premises $next-hole (- $depth 1)
                  (cons-atom $goal $ancestors))
             (let (ATP.DirectSearch $selection $premise-order)
                  $candidate-policy
               (let $scheduled-obligations
                    (atp:indexed:ordered-premises
                      $new-obligations $premise-order)
                 (let $next-obligations
                      (atp:agenda:append $scheduled-obligations $rest)
                   (ATP.AgendaState
                     $next-obligations
                     (atp:agenda:replace-hole
                       $partial $hole (cons-atom $rule $new-holes))
                     $after $age
                     (+ $guidance-cost
                        (atp:agenda:candidate-cost
                          $candidate-policy
                          (ATP.Candidate
                            $source-age $generality $misses $support
                            $rule $premises $goal)))
                     (atp:agenda:unsupported-obligations
                       $knowledge $index $next-obligations))))))))))

(= (atp:agenda:expand $knowledge $index $state $candidate-policy)
   (let (ATP.AgendaState
          $obligations $proof $next-hole $age
          $guidance-cost $unsupported)
        $state
     (if (== $obligations ())
         Empty
         (let ((ATP.Obligation $hole $goal $depth $ancestors) $rest)
              (decons-atom $obligations)
           (if (atp:indexed:seen-goal $goal $ancestors)
               Empty
               (superpose
                  ((atp:agenda:expand-hypothesis
                     $knowledge $index $state)
                   (atp:agenda:expand-ranked-rule
                     $knowledge $index $state $candidate-policy)
                   (atp:agenda:expand-theorem-fact
                     $knowledge $index $state
                     (atp:agenda:candidate-fact-limit
                       $candidate-policy)))))))))

(= (atp:agenda:incomplete-reason $reason False)
   (ATP.AgendaIncomplete $reason CompleteAgenda))

(= (atp:agenda:incomplete-reason $reason True)
   (ATP.AgendaIncomplete $reason TruncatedAgenda))

(= (atp:agenda:search-loop
     $knowledge $index $original-goal
     $agenda $seen $steps $expanded $next-age
     $fuel $policy $candidate-policy $truncated)
   (if (== $agenda ())
       (ATP.AgendaSearchResult
         (atp:agenda:incomplete-reason
           NoInhabitantWithinBounds $truncated)
         (ATP.AgendaStats $expanded (size-atom $seen)))
       (if (<= $steps 0)
           (ATP.AgendaSearchResult
             (atp:agenda:incomplete-reason StepBound $truncated)
             (ATP.AgendaStats $expanded (size-atom $seen)))
           (let ($state $rest-agenda) (decons-atom $agenda)
             (let (ATP.AgendaState
                    $obligations $proof $next-hole $age
                    $guidance-cost $unsupported)
                  $state
               (if (== $obligations ())
                   (case (check-type
                           $knowledge $proof $original-goal $fuel)
                     (((he-accept (exact $checked))
                        (ATP.AgendaSearchResult
                          (ATP.AgendaChecked
                            $proof $original-goal (exact $checked))
                          (ATP.AgendaStats
                            $expanded (size-atom $seen))))
                      ((he-accept (structural $checked))
                        (ATP.AgendaSearchResult
                          (ATP.AgendaChecked
                            $proof $original-goal (structural $checked))
                          (ATP.AgendaStats
                            $expanded (size-atom $seen))))
                      ($_
                        (atp:agenda:search-loop
                          $knowledge $index $original-goal
                          $rest-agenda $seen (- $steps 1)
                          (+ $expanded 1) $next-age
                          $fuel $policy $candidate-policy $truncated))))
                   (if (atp:agenda:seen-state $state $seen)
                       (atp:agenda:search-loop
                         $knowledge $index $original-goal
                         $rest-agenda $seen (- $steps 1)
                         (+ $expanded 1) $next-age
                         $fuel $policy $candidate-policy $truncated)
                       (let $successor-limit
                            (atp:agenda:max-successors $policy)
                         (let $selected-successors
                              (select $successor-limit
                                (atp:agenda:expand
                                  $knowledge $index $state
                                  $candidate-policy))
                           (let (ATP.AgendaPack
                                  $local-successors
                                  $local-truncated $after-age)
                                (fold
                                  (superpose $selected-successors)
                                (ATP.AgendaPack
                                  () True $next-age)
                                $pack $successor
                                (atp:agenda:add-local-successor
                                  $pack $successor $policy))
                         (let (ATP.AgendaPack
                                $next-agenda $next-truncated $merged-age)
                              (atp:agenda:merge-local
                                $local-successors $rest-agenda
                                $local-truncated $truncated
                                $after-age $policy)
                           (atp:agenda:search-loop
                             $knowledge $index $original-goal
                             $next-agenda
                             (cons-atom
                               (atp:agenda:state-signature $state)
                               $seen)
                             (- $steps 1) (+ $expanded 1) $merged-age
                             $fuel $policy $candidate-policy
                             $next-truncated))))))))))))

(= (atp:agenda:first-checked
     $knowledge $index $goal $depth $steps $fuel $policy)
   (let (ATP.AgendaSearch $agenda-policy $candidate-policy) $policy
     (let $initial-obligations
          ((ATP.Obligation 0 $goal $depth ()))
       (let $initial-unsupported
            (atp:agenda:unsupported-obligations
              $knowledge $index $initial-obligations)
         (atp:agenda:search-loop
           $knowledge $index $goal
           ((ATP.AgendaState
              $initial-obligations
              (ATP.Hole 0) 1 0 0 $initial-unsupported))
           () $steps 0 1 $fuel
           $agenda-policy $candidate-policy False)))))

(= (atp:agenda:checked-proof $result)
   (case $result
     (((ATP.AgendaSearchResult
         (ATP.AgendaChecked $proof $goal $verdict) $stats)
        (ATP.Checked $proof $goal $verdict))
      ($_ $result))))
