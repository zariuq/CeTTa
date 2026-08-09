; Indexed direct proof search.
;
; The optional index contains only search hints:
;
;   (ATP.Index.Rule goal-head source-age rule-label)
;   (ATP.Index.Fact goal-head source-age proof-label)
;   (ATP.Index.Hypothesis goal-head source-age proof-label)
;
; Rule content remains in the ordinary typing space.  Each hinted label must
; still resolve to a marked chaining rule there, and every resulting proof is
; checked independently.  A stale or incomplete index can therefore affect
; discovery, never proof authority.

(= (atp:indexed:head-key $goal)
   (if (== (get-metatype $goal) Expression)
       (car-atom $goal)
       $goal))

(= (atp:indexed:lookup-key $knowledge $index $goal $key)
   (match $index (ATP.Index.Rule $key $age $rule)
     (match $knowledge (chaining-rule $rule)
       (match $knowledge (: $rule $type)
         (let (ATP.Rule $rule $premises $goal)
              (atp:direct:arrow-candidate $goal $rule $type)
           (ATP.Candidate
             $age
             (if (== $key ATP.AnyHead) 1 0)
             0
             0
             $rule $premises $goal))))))

(= (atp:indexed:rule-candidate $knowledge $index $goal)
   (let $head (atp:indexed:head-key $goal)
     (if (== (get-metatype $head) Variable)
         ; A genuinely open goal may be discharged by any indexed head.
         ; Do one variable-key query rather than adding the AnyHead fallback
         ; a second time.
         (atp:indexed:lookup-key $knowledge $index $goal $head)
         (if (== $head ATP.AnyHead)
         (atp:indexed:lookup-key $knowledge $index $goal ATP.AnyHead)
         (superpose
           ((atp:indexed:lookup-key $knowledge $index $goal $head)
            (atp:indexed:lookup-key
              $knowledge $index $goal ATP.AnyHead)))))))

(= (atp:indexed:lookup-fact-key $knowledge $index $goal $key)
   (match $index (ATP.Index.Fact $key $age $proof)
     (match $knowledge (: $proof $type)
       (atp:direct:fact-instance $knowledge $goal $proof $type))))

(= (atp:indexed:lookup-fact-key $knowledge $index $goal $key)
   (match $index (ATP.Index.Hypothesis $key $age $proof)
     (match $knowledge (: $proof $type)
       (atp:direct:fact-instance $knowledge $goal $proof $type))))

(= (atp:indexed:fact $knowledge $index $goal)
   (let $head (atp:indexed:head-key $goal)
     (if (== (get-metatype $head) Variable)
         (atp:indexed:lookup-fact-key $knowledge $index $goal $head)
         (if (== $head ATP.AnyHead)
         (atp:indexed:lookup-fact-key
           $knowledge $index $goal ATP.AnyHead)
         (superpose
           ((atp:indexed:lookup-fact-key
              $knowledge $index $goal $head)
            (atp:indexed:lookup-fact-key
              $knowledge $index $goal ATP.AnyHead)))))))

(= (atp:indexed:lookup-theorem-fact-key
     $knowledge $index $goal $key)
   (match $index (ATP.Index.Fact $key $age $proof)
     (match $knowledge (: $proof $type)
       (atp:direct:fact-instance $knowledge $goal $proof $type))))

(= (atp:indexed:theorem-fact $knowledge $index $goal)
   (let $head (atp:indexed:head-key $goal)
     (if (== (get-metatype $head) Variable)
         (atp:indexed:lookup-theorem-fact-key
           $knowledge $index $goal $head)
         (if (== $head ATP.AnyHead)
             (atp:indexed:lookup-theorem-fact-key
               $knowledge $index $goal ATP.AnyHead)
             (superpose
               ((atp:indexed:lookup-theorem-fact-key
                  $knowledge $index $goal $head)
                (atp:indexed:lookup-theorem-fact-key
                  $knowledge $index $goal ATP.AnyHead)))))))

(= (atp:indexed:bounded-theorem-fact
     $knowledge $index $goal $fact-limit)
   (superpose
     (select $fact-limit
       (search-policy recursive-dependent-proof (order reverse))
       (atp:indexed:theorem-fact $knowledge $index $goal))))

(= (atp:indexed:premise-misses $knowledge $index ()) 0)

(= (atp:indexed:premise-misses $knowledge $index $premises)
   (let ($premise $rest) (decons-atom $premises)
     (+ (if (== (reify
                   (select
                     (search-policy
                       recursive-dependent-proof (order reverse))
                     (atp:indexed:fact
                       $knowledge $index $premise)))
                ())
            1
            0)
        (atp:indexed:premise-misses $knowledge $index $rest))))

(= (atp:indexed:lookup-hypothesis-key
     $knowledge $index $goal $key)
   (match $index (ATP.Index.Hypothesis $key $age $proof)
     (match $knowledge (: $proof $type)
       (atp:direct:fact-instance $knowledge $goal $proof $type))))

(= (atp:indexed:hypothesis $knowledge $index $goal)
   (let $head (atp:indexed:head-key $goal)
     (if (== (get-metatype $head) Variable)
         (atp:indexed:lookup-hypothesis-key
           $knowledge $index $goal $head)
         (if (== $head ATP.AnyHead)
         (atp:indexed:lookup-hypothesis-key
           $knowledge $index $goal ATP.AnyHead)
         (superpose
           ((atp:indexed:lookup-hypothesis-key
              $knowledge $index $goal $head)
            (atp:indexed:lookup-hypothesis-key
              $knowledge $index $goal ATP.AnyHead)))))))

(= (atp:indexed:bounded-hypothesis
     $knowledge $index $goal $fact-limit)
   ; The exact-hypothesis probe is deliberately bounded and newest-first.  It
   ; is a ranking observation, not a completeness or proof-authority claim.
   (superpose
     (select $fact-limit
       (search-policy recursive-dependent-proof (order reverse))
       (atp:indexed:hypothesis $knowledge $index $goal))))

(= (atp:indexed:direct-premises
     $knowledge $index () $fact-limit)
   True)

(= (atp:indexed:direct-premises
     $knowledge $index $premises $fact-limit)
   (let ($premise $rest) (decons-atom $premises)
     (let (: $proof $premise)
          (atp:indexed:bounded-hypothesis
            $knowledge $index $premise $fact-limit)
       (atp:indexed:direct-premises
         $knowledge $index $rest $fact-limit))))

(= (atp:indexed:reverse-onto () $reversed) $reversed)

(= (atp:indexed:reverse-onto $items $reversed)
   (let ($head $tail) (decons-atom $items)
     (atp:indexed:reverse-onto
       $tail (cons-atom $head $reversed))))

(= (atp:indexed:ordered-premises $premises ATP.LeftToRight)
   $premises)

(= (atp:indexed:ordered-premises $premises ATP.RightToLeft)
   (atp:indexed:reverse-onto $premises ()))

(= (atp:indexed:prefix ()) ())

(= (atp:indexed:prefix $items)
   (superpose
     (()
      (let ($head $tail) (decons-atom $items)
        (let $prefix (atp:indexed:prefix $tail)
          (cons-atom $head $prefix))))))

(= (atp:indexed:direct-support
     $knowledge $index $premises $fact-limit $premise-order)
   ; Measure the longest target-hypothesis-supported prefix in the actual
   ; premise traversal order.  Each prefix uses one shared substitution.
   (fold
     (let $prefix
          (atp:indexed:prefix
            (atp:indexed:ordered-premises
              $premises $premise-order))
       (let True
            (atp:indexed:direct-premises
              $knowledge $index $prefix $fact-limit)
         (size-atom $prefix)))
     0 $best $count
     (if (> $count $best) $count $best)))

(= (atp:indexed:support-candidate
     $knowledge $index $fact-limit $premise-order
     (ATP.Candidate
       $age $head-generality $_premise-misses $_direct-support
       $rule $premises $goal))
   (ATP.Candidate
     $age
     $head-generality
     (atp:indexed:premise-misses $knowledge $index $premises)
     (atp:indexed:direct-support
       $knowledge $index $premises $fact-limit $premise-order)
     $rule $premises $goal))

(= (atp:feature:term-weight $atom)
   (if (== (get-metatype $atom) Expression)
       (+ 1 (atp:feature:items-weight $atom))
       1))

(= (atp:feature:items-weight $items)
   (if (== $items ())
       0
       (let ($head $tail) (decons-atom $items)
         (+ (atp:feature:term-weight $head)
            (atp:feature:items-weight $tail)))))

(= (atp:feature:candidate
     (ATP.Candidate
       $age $head-generality $premise-misses $direct-support
       $rule $premises $goal)
     $newest-age)
   (ATP.Features
     $head-generality
     $premise-misses
     $direct-support
     (size-atom $premises)
     (atp:feature:term-weight $premises)
     (if (> $newest-age $age) (- $newest-age $age) 0)))

; A policy is an explicit linear functional over the feature basis
; (head generality, immediate premise misses, bounded shared-substitution
; target-hypothesis support,
; premise count, structural weight, recency distance).  Other libraries may
; add new policy constructors without changing this search or the checker.
(= (atp:policy:cost
     (ATP.LinearPolicy $generality-coefficient
                       $miss-coefficient
                       $support-credit
                       $premise-coefficient
                       $weight-coefficient
                       $recency-coefficient
                       $newest-age)
     $candidate)
   (let (ATP.Features
          $generality $misses $direct-support
          $premises $weight $recency)
        (atp:feature:candidate $candidate $newest-age)
     (+ (* $generality-coefficient $generality)
        (+ (* $miss-coefficient $misses)
           (+ (- 0 (* $support-credit $direct-support))
              (+ (* $premise-coefficient $premises)
                 (+ (* $weight-coefficient $weight)
                    (* $recency-coefficient $recency))))))))

(= (atp:indexed:insert $candidate $ordered $ranker)
   (if (== $ordered ())
       (cons-atom $candidate ())
       (let ($head $tail) (decons-atom $ordered)
         ; Strict comparison preserves source order when scores tie.
         (if (< (atp:policy:cost $ranker $candidate)
                (atp:policy:cost $ranker $head))
             (cons-atom $candidate $ordered)
             (cons-atom
               $head (atp:indexed:insert $candidate $tail $ranker))))))

(= (atp:indexed:take $limit $items)
   (if (<= $limit 0)
       ()
       (if (== $items ())
           ()
           (let ($head $tail) (decons-atom $items)
             (let $taken
                  (atp:indexed:take (- $limit 1) $tail)
               (cons-atom $head $taken))))))

(= (atp:indexed:insert-bounded
     $candidate $ordered $ranker $limit)
   (atp:indexed:take
     $limit (atp:indexed:insert $candidate $ordered $ranker)))

(= (atp:indexed:top-candidates
     $knowledge $index $goal $limit $ranker)
   ; fold consumes the candidate stream online.  The accumulator never holds
   ; more than limit candidates, so ranking does not require collapse.
   (fold
     (atp:indexed:rule-candidate $knowledge $index $goal)
     () $ordered $candidate
     (atp:indexed:insert-bounded
       $candidate $ordered $ranker $limit)))

(= (atp:indexed:top-supported-candidates
     $knowledge $index $goal
     $coarse-limit $fact-limit $final-limit
     $coarse-ranker $fine-ranker $premise-order)
   ; Rank cheap static features over the complete candidate stream, then run
   ; the bounded shared-substitution support probe only on that shortlist.
   (let $coarse
        (atp:indexed:top-candidates
          $knowledge $index $goal $coarse-limit $coarse-ranker)
     (fold
       (let $candidate (superpose $coarse)
         (atp:indexed:support-candidate
           $knowledge $index $fact-limit $premise-order $candidate))
       () $ordered $candidate
       (atp:indexed:insert-bounded
         $candidate $ordered $fine-ranker $final-limit))))

(= (atp:indexed:ranked-candidate-with-selection
     $knowledge $index $goal
     (ATP.TopK $limit $ranker) $premise-order)
   (let $ordered
        (atp:indexed:top-candidates
          $knowledge $index $goal $limit $ranker)
     (superpose $ordered)))

(= (atp:indexed:ranked-candidate-with-selection
     $knowledge $index $goal
     (ATP.CoarseToFine
       $coarse-limit $fact-limit $final-limit
       $coarse-ranker $fine-ranker)
     $premise-order)
   (let $ordered
        (atp:indexed:top-supported-candidates
          $knowledge $index $goal
          $coarse-limit $fact-limit $final-limit
          $coarse-ranker $fine-ranker $premise-order)
     (superpose $ordered)))

(= (atp:indexed:ranked-candidate
     $knowledge $index $goal
     (ATP.DirectSearch $selection $premise-order))
   (atp:indexed:ranked-candidate-with-selection
     $knowledge $index $goal $selection $premise-order))

(= (atp:indexed:seen-goal $goal ()) False)

(= (atp:indexed:seen-goal $goal $ancestors)
   (let ($ancestor $rest) (decons-atom $ancestors)
     (if (=alpha $goal $ancestor)
         True
         (atp:indexed:seen-goal $goal $rest))))

(= (atp:indexed:prove-premises
     $knowledge $index () $depth $premise-order $policy $ancestors)
   (ATP.Proofs ()))

(= (atp:indexed:prove-premises
     $knowledge $index $premises $depth
     ATP.LeftToRight $policy $ancestors)
   (let ($premise $rest) (decons-atom $premises)
     (let* (((: $proof $premise)
             (atp:indexed:prove-judgment-with-ancestors
               $knowledge $index $premise $depth $policy $ancestors))
            ((ATP.Proofs $proofs)
             (atp:indexed:prove-premises
               $knowledge $index $rest $depth
               ATP.LeftToRight $policy $ancestors)))
       (ATP.Proofs (cons-atom $proof $proofs)))))

(= (atp:indexed:prove-premises
     $knowledge $index $premises $depth
     ATP.RightToLeft $policy $ancestors)
   ; Solve the tail first, but retain the authored proof-argument order.
   (let ($premise $rest) (decons-atom $premises)
     (let* (((ATP.Proofs $proofs)
             (atp:indexed:prove-premises
               $knowledge $index $rest $depth
               ATP.RightToLeft $policy $ancestors))
            ((: $proof $premise)
             (atp:indexed:prove-judgment-with-ancestors
               $knowledge $index $premise $depth $policy $ancestors)))
       (ATP.Proofs (cons-atom $proof $proofs)))))

(= (atp:indexed:use-rule
     $knowledge $index $goal $depth $policy $ancestors)
   (let (ATP.DirectSearch $selection $premise-order) $policy
     (let (ATP.Candidate
            $age $head-generality $premise-misses $direct-support
            $rule $premises $goal)
          (atp:indexed:ranked-candidate-with-selection
            $knowledge $index $goal $selection $premise-order)
       (let (ATP.Proofs $proofs)
            (atp:indexed:prove-premises
              $knowledge $index $premises (- $depth 1)
              $premise-order $policy $ancestors)
         (: (cons-atom $rule $proofs) $goal)))))

(= (atp:indexed:prove-judgment-with-ancestors
     $knowledge $index $goal $depth $policy $ancestors)
   (if (atp:indexed:seen-goal $goal $ancestors)
       (empty)
       (if (<= $depth 0)
           (atp:indexed:fact $knowledge $index $goal)
           (superpose
             ((atp:indexed:fact $knowledge $index $goal)
              (atp:indexed:use-rule
                $knowledge $index $goal $depth $policy
                (cons-atom $goal $ancestors)))))))

(= (atp:indexed:prove-judgment $knowledge $index $goal $depth $policy)
   (atp:indexed:prove-judgment-with-ancestors
     $knowledge $index $goal $depth $policy ()))

(= (atp:indexed:prove-checked
     $knowledge $index $goal $depth $fuel $policy)
   (let (: $proof $goal)
        (atp:indexed:prove-judgment
          $knowledge $index $goal $depth $policy)
     (atp:direct:certify
       $proof $goal (check-type $knowledge $proof $goal $fuel))))

(= (atp:indexed:first-checked
     $knowledge $index $goal $depth $fuel $policy)
   (select
     (atp:indexed:prove-checked
       $knowledge $index $goal $depth $fuel $policy)))
