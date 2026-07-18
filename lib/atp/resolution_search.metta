; Native MeTTa binary-resolution search.
;
; This module is an untrusted proof producer.  Clauses use ordinary MeTTa
; variables during discovery, and every complementary-literal step uses the
; runtime's ordinary occurs-checked unify.  It deliberately does not define a
; second first-order unifier.  External proof interchange remains on the
; explicit-substitution replay surface in resolution.metta.
;
; The public result is either a replayable resolution proof tree or an honest
; bounded-incomplete result.  Search order, set of support, age/weight choice,
; and deletion of tautologies or exact variants are policy only.

; Pick one item and return the remaining list.  This is the only source of
; literal-level nondeterminism in one binary-resolution expansion.
(= (atp:resolution-search:pick ()) Empty)

(= (atp:resolution-search:pick $items)
   (let ($head $tail) (decons-atom $items)
     (superpose
       ((ATP.Pick $head $tail)
        (let (ATP.Pick $picked $rest)
             (atp:resolution-search:pick $tail)
          (ATP.Pick $picked (cons-atom $head $rest)))))))

(= (atp:resolution-search:native-literal-valid $literal)
   (case $literal
     (((ATP.Pos $atom) True)
      ((ATP.Neg $atom) True)
      ($_ False))))

(= (atp:resolution-search:native-clause-valid ()) True)

(= (atp:resolution-search:native-clause-valid $clause)
   (let ($literal $rest) (decons-atom $clause)
     (if (atp:resolution-search:native-literal-valid $literal)
         (atp:resolution-search:native-clause-valid $rest)
         False)))

(= (atp:resolution-search:remove-one-alpha $item ())
   ATP.RemoveMissing)

(= (atp:resolution-search:remove-one-alpha $item $items)
   (let ($head $tail) (decons-atom $items)
     (if (=alpha $item $head)
         (ATP.RemoveOK $tail)
         (case (atp:resolution-search:remove-one-alpha $item $tail)
           (((ATP.RemoveOK $rest)
              (ATP.RemoveOK (cons-atom $head $rest)))
            ($_ ATP.RemoveMissing))))))

(= (atp:resolution-search:multiset-alpha-equal () $right)
   (== $right ()))

(= (atp:resolution-search:multiset-alpha-equal $left $right)
   (let ($head $tail) (decons-atom $left)
     (case (atp:resolution-search:remove-one-alpha $head $right)
       (((ATP.RemoveOK $remaining)
          (atp:resolution-search:multiset-alpha-equal
            $tail $remaining))
        ($_ False)))))

; A native schematic step carries instantiated parent clauses rather than an
; explicit substitution.  Re-unification is independent of the producer and
; the claimed child is compared as an alpha-invariant literal multiset.
(= (atp:resolution-search:check-polarity
     $left $right $left-pivot $right-pivot
     $left-atom $right-atom $expected)
   (unify $left-atom $right-atom
     (case (atp:resolution-search:remove-one-alpha
             $left-pivot $left)
       (((ATP.RemoveOK $left-rest)
          (case (atp:resolution-search:remove-one-alpha
                  $right-pivot $right)
            (((ATP.RemoveOK $right-rest)
               (atp:resolution-search:multiset-alpha-equal
                 (atp:resolution:append $left-rest $right-rest)
                 $expected))
             ($_ False))))
        ($_ False)))
     False))

(= (atp:resolution-search:check-native-step
     $left $right $left-pivot $right-pivot $expected)
   (if (atp:resolution-search:native-clause-valid $left)
       (if (atp:resolution-search:native-clause-valid $right)
           (if (atp:resolution-search:native-clause-valid $expected)
               (case ($left-pivot $right-pivot)
                 ((((ATP.Pos $left-atom) (ATP.Neg $right-atom))
                    (atp:resolution-search:check-polarity
                      $left $right $left-pivot $right-pivot
                      $left-atom $right-atom $expected))
                  (((ATP.Neg $left-atom) (ATP.Pos $right-atom))
                    (atp:resolution-search:check-polarity
                      $left $right $left-pivot $right-pivot
                      $left-atom $right-atom $expected))
                  ($_ False)))
               False)
           False)
       False))

(= (atp:resolution-search:conclusion
     (ATP.InputClause $name $clause))
   $clause)

(= (atp:resolution-search:conclusion
     (ATP.ResolutionProof
       $left $right $left-pivot $right-pivot $child))
   $child)

(= (atp:resolution-search:replay
     (ATP.InputClause $name $clause))
   (atp:resolution-search:native-clause-valid $clause))

(= (atp:resolution-search:replay
     (ATP.ResolutionProof
       $left $right $left-pivot $right-pivot $child))
   (if (atp:resolution-search:replay $left)
       (if (atp:resolution-search:replay $right)
           (atp:resolution-search:check-native-step
             (atp:resolution-search:conclusion $left)
             (atp:resolution-search:conclusion $right)
             $left-pivot $right-pivot $child)
           False)
       False))

; Resolve already-standardized parent copies.  unify instantiates both proof
; subtrees as well as their selected literals and residual clauses.
(= (atp:resolution-search:resolve-polarity
     $left-proof $right-proof
     $left-clause $right-clause
     $left-pivot $right-pivot
     $left-rest $right-rest
     $left-atom $right-atom)
   (unify $left-atom $right-atom
     (let $child
          (eval (atp:resolution:append $left-rest $right-rest))
       (ATP.ResolutionProof
         $left-proof $right-proof
         $left-pivot $right-pivot $child))
     Empty))

(= (atp:resolution-search:resolve-fresh
     $left-proof $right-proof)
   (let $left-clause
        (atp:resolution-search:conclusion $left-proof)
     (let $right-clause
          (atp:resolution-search:conclusion $right-proof)
       (let (ATP.Pick $left-pivot $left-rest)
            (atp:resolution-search:pick $left-clause)
         (let (ATP.Pick $right-pivot $right-rest)
              (atp:resolution-search:pick $right-clause)
           (case ($left-pivot $right-pivot)
             ((((ATP.Pos $left-atom) (ATP.Neg $right-atom))
                (atp:resolution-search:resolve-polarity
                  $left-proof $right-proof
                  $left-clause $right-clause
                  $left-pivot $right-pivot
                  $left-rest $right-rest
                  $left-atom $right-atom))
              (((ATP.Neg $left-atom) (ATP.Pos $right-atom))
                (atp:resolution-search:resolve-polarity
                  $left-proof $right-proof
                  $left-clause $right-clause
                  $left-pivot $right-pivot
                  $left-rest $right-rest
                  $left-atom $right-atom))
              ($_ Empty))))))))

(= (atp:resolution-search:resolve $left-proof $right-proof)
   ; sealed creates independent variable epochs for the two parent uses.  This
   ; is standardization apart on the ordinary MeTTa variable substrate.
   (let (quote $fresh-left)
        (sealed () (quote $left-proof))
     (let (quote $fresh-right)
          (sealed () (quote $right-proof))
       (atp:resolution-search:resolve-fresh
         $fresh-left $fresh-right))))

(= (atp:resolution-search:literal-exact-complement $left $right)
   (case ($left $right)
     ((((ATP.Pos $left-atom) (ATP.Neg $right-atom))
        (== $left-atom $right-atom))
      (((ATP.Neg $left-atom) (ATP.Pos $right-atom))
        (== $left-atom $right-atom))
      ($_ False))))

(= (atp:resolution-search:tautology-against $literal ()) False)

(= (atp:resolution-search:tautology-against $literal $rest)
   (let ($candidate $tail) (decons-atom $rest)
     (if (atp:resolution-search:literal-exact-complement
           $literal $candidate)
         True
         (atp:resolution-search:tautology-against
           $literal $tail))))

(= (atp:resolution-search:tautology ()) False)

(= (atp:resolution-search:tautology $clause)
   (let ($literal $rest) (decons-atom $clause)
     (if (atp:resolution-search:tautology-against $literal $rest)
         True
         (atp:resolution-search:tautology $rest))))

(= (atp:resolution-search:node-weight $proof)
   (atp:feature:term-weight
     (atp:resolution-search:conclusion $proof)))

(= (atp:resolution-search:item-better-weight
     (ATP.SearchItem $left-age $left-proof)
     (ATP.SearchItem $right-age $right-proof))
   (let $left-weight
        (atp:resolution-search:node-weight $left-proof)
     (let $right-weight
          (atp:resolution-search:node-weight $right-proof)
       (if (< $left-weight $right-weight)
           True
           (if (> $left-weight $right-weight)
               False
               (< $left-age $right-age))))))

(= (atp:resolution-search:item-better-age
     (ATP.SearchItem $left-age $left-proof)
     (ATP.SearchItem $right-age $right-proof))
   (< $left-age $right-age))

(= (atp:resolution-search:extract-best-by-weight $items)
   (let ($head $tail) (decons-atom $items)
     (if (== $tail ())
         (ATP.Selected $head ())
         (let (ATP.Selected $best $rest)
              (atp:resolution-search:extract-best-by-weight $tail)
           (if (atp:resolution-search:item-better-weight $head $best)
               (ATP.Selected $head (cons-atom $best $rest))
               (ATP.Selected $best (cons-atom $head $rest)))))))

(= (atp:resolution-search:extract-best-by-age $items)
   (let ($head $tail) (decons-atom $items)
     (if (== $tail ())
         (ATP.Selected $head ())
         (let (ATP.Selected $best $rest)
              (atp:resolution-search:extract-best-by-age $tail)
           (if (atp:resolution-search:item-better-age $head $best)
               (ATP.Selected $head (cons-atom $best $rest))
               (ATP.Selected $best (cons-atom $head $rest)))))))

(= (atp:resolution-search:select-given
     $passive $selection-index
     (ATP.AgeWeight $weight-turns $age-turns))
   (let $cycle (+ $weight-turns $age-turns)
     (if (< (% $selection-index $cycle) $weight-turns)
         (atp:resolution-search:extract-best-by-weight $passive)
         (atp:resolution-search:extract-best-by-age $passive))))

(= (atp:resolution-search:variant-in-items $clause ()) False)

(= (atp:resolution-search:variant-in-items $clause $items)
   (let ((ATP.SearchItem $age $proof) $rest) (decons-atom $items)
     (let $proof-clause
          (atp:resolution-search:conclusion $proof)
       (if (=alpha $clause $proof-clause)
           True
           (atp:resolution-search:variant-in-items $clause $rest)))))

(= (atp:resolution-search:variant-in-proofs $clause ()) False)

(= (atp:resolution-search:variant-in-proofs $clause $proofs)
   (let ($proof $rest) (decons-atom $proofs)
     (let $proof-clause
          (atp:resolution-search:conclusion $proof)
       (if (=alpha $clause $proof-clause)
           True
           (atp:resolution-search:variant-in-proofs $clause $rest)))))

(= (atp:resolution-search:take $limit $items)
   (if (<= $limit 0)
       ()
       (if (== $items ())
           ()
           (let ($head $tail) (decons-atom $items)
             (let $rest
                  (eval (atp:resolution-search:take
                          (- $limit 1) $tail))
               (cons-atom $head $rest))))))

(= (atp:resolution-search:resolve-with-active
     $given-proof $active $max-generated)
   ; Request one extra answer so the result records whether the local
   ; inference stream was truncated by its explicit coverage bound.
   (let $answers
        (select (+ $max-generated 1)
          (let $active-proof (superpose $active)
            (atp:resolution-search:resolve
              $given-proof $active-proof)))
     (if (> (size-atom $answers) $max-generated)
         (ATP.Generated
           (atp:resolution-search:take $max-generated $answers) True)
         (ATP.Generated $answers False))))

; Signed-literal Atomspace index.  The stored proof and selected literal share
; the clause's native variables, so match instantiates the entire parent proof
; under the same ordinary MeTTa substitution that matched the pivot.
(= (atp:resolution-search:index-literals $index $proof ()) ATP.Indexed)

(= (atp:resolution-search:index-literals $index $proof $literals)
   (let ($literal $rest) (decons-atom $literals)
     (case $literal
       (((ATP.Pos $atom)
          (let $added
               (add-atom
                 $index
                 (ATP.ActivePos $atom $proof $literal))
            (atp:resolution-search:index-literals
              $index $proof $rest)))
        ((ATP.Neg $atom)
          (let $added
               (add-atom
                 $index
                 (ATP.ActiveNeg $atom $proof $literal))
            (atp:resolution-search:index-literals
              $index $proof $rest)))
        ($_ ATP.Indexed)))))

(= (atp:resolution-search:index-proof $index $proof)
   (let $clause (atp:resolution-search:conclusion $proof)
     (atp:resolution-search:index-literals $index $proof $clause)))

(= (atp:resolution-search:index-proofs $index ()) ATP.Indexed)

(= (atp:resolution-search:index-proofs $index $proofs)
   (let ($proof $rest) (decons-atom $proofs)
     (let $indexed
          (atp:resolution-search:index-proof $index $proof)
       (atp:resolution-search:index-proofs $index $rest))))

(= (atp:resolution-search:resolve-indexed-hit
     $given-proof $given-pivot $given-rest
     $active-proof $active-pivot)
   (let $active-clause
        (atp:resolution-search:conclusion $active-proof)
     (case (atp:resolution-search:remove-one-alpha
             $active-pivot $active-clause)
       (((ATP.RemoveOK $active-rest)
          (let $child
               (eval (atp:resolution:append
                       $given-rest $active-rest))
            (ATP.ResolutionProof
              $given-proof $active-proof
              $given-pivot $active-pivot $child)))
        ($_ Empty)))))

(= (atp:resolution-search:resolve-indexed-fresh $index $given-proof)
   (let $given-clause
        (atp:resolution-search:conclusion $given-proof)
     (let (ATP.Pick $given-pivot $given-rest)
          (atp:resolution-search:pick $given-clause)
       (case $given-pivot
         (((ATP.Pos $atom)
            (match $index
              (ATP.ActiveNeg $atom $active-proof $active-pivot)
              (atp:resolution-search:resolve-indexed-hit
                $given-proof $given-pivot $given-rest
                $active-proof $active-pivot)))
          ((ATP.Neg $atom)
            (match $index
              (ATP.ActivePos $atom $active-proof $active-pivot)
              (atp:resolution-search:resolve-indexed-hit
                $given-proof $given-pivot $given-rest
                $active-proof $active-pivot)))
          ($_ Empty))))))

(= (atp:resolution-search:resolve-indexed $index $given-proof)
   ; match freshens every indexed parent.  sealed independently freshens the
   ; given clause, giving standardization apart without ATP-specific runtime
   ; machinery.
   (let (quote $fresh-given)
        (sealed () (quote $given-proof))
     (atp:resolution-search:resolve-indexed-fresh
       $index $fresh-given)))

(= (atp:resolution-search:resolve-with-index
     $index $given-proof $max-generated)
   (let $answers
        (select (+ $max-generated 1)
          (atp:resolution-search:resolve-indexed $index $given-proof))
     (if (> (size-atom $answers) $max-generated)
         (ATP.Generated
           (atp:resolution-search:take $max-generated $answers) True)
         (ATP.Generated $answers False))))

(= (atp:resolution-search:ingest
     () $passive $active $next-age
     $accepted $tautologies $variants $invalid)
   (ATP.Ingested
     $passive $next-age $accepted
     $tautologies $variants $invalid))

(= (atp:resolution-search:ingest
     $proofs $passive $active $next-age
     $accepted $tautologies $variants $invalid)
   (let ($proof $rest) (decons-atom $proofs)
     (let $clause (atp:resolution-search:conclusion $proof)
       (if (atp:resolution-search:replay $proof)
           (if (atp:resolution-search:tautology $clause)
               (atp:resolution-search:ingest
                 $rest $passive $active $next-age
                 $accepted (+ $tautologies 1) $variants $invalid)
               (if (atp:resolution-search:variant-in-items
                     $clause $passive)
                   (atp:resolution-search:ingest
                     $rest $passive $active $next-age
                     $accepted $tautologies (+ $variants 1) $invalid)
                   (if (atp:resolution-search:variant-in-proofs
                         $clause $active)
                       (atp:resolution-search:ingest
                         $rest $passive $active $next-age
                         $accepted $tautologies (+ $variants 1) $invalid)
                       (atp:resolution-search:ingest
                         $rest
                         (cons-atom
                           (ATP.SearchItem $next-age $proof) $passive)
                         $active (+ $next-age 1)
                         (+ $accepted 1) $tautologies $variants $invalid))))
           (atp:resolution-search:ingest
             $rest $passive $active $next-age
             $accepted $tautologies $variants (+ $invalid 1))))))

(= (atp:resolution-search:stats-add
     (ATP.ResolutionStats
       $selected $generated $accepted $tautologies
       $variants $invalid $truncated)
     $new-generated $new-accepted $new-tautologies
     $new-variants $new-invalid $new-truncated)
   (ATP.ResolutionStats
     (+ $selected 1)
     (+ $generated $new-generated)
     (+ $accepted $new-accepted)
     (+ $tautologies $new-tautologies)
     (+ $variants $new-variants)
     (+ $invalid $new-invalid)
     (if $new-truncated True $truncated)))

(= (atp:resolution-search:loop
     $passive $active $next-age $selection-index
     $steps $max-steps $max-generated $age-weight $stats)
   (if (== $passive ())
       (ATP.ResolutionSearchResult
         (ATP.Incomplete PassiveExhausted) $stats)
       (if (<= $steps 0)
           (ATP.ResolutionSearchResult
             (ATP.Incomplete StepBound) $stats)
           (let (ATP.Selected
                  (ATP.SearchItem $given-age $given-proof)
                  $remaining-passive)
                (atp:resolution-search:select-given
                  $passive $selection-index $age-weight)
             (let $given-clause
                  (atp:resolution-search:conclusion $given-proof)
               (if (== $given-clause ())
                   (ATP.ResolutionSearchResult
                     (ATP.Refutation $given-proof) $stats)
                   (let (ATP.Generated $generated $local-truncated)
                        (atp:resolution-search:resolve-with-active
                          $given-proof $active $max-generated)
                     (let (ATP.Ingested
                            $next-passive $after-age $accepted
                            $tautologies $variants $invalid)
                          (atp:resolution-search:ingest
                            $generated $remaining-passive
                            (cons-atom $given-proof $active)
                            $next-age 0 0 0 0)
                       (atp:resolution-search:loop
                         $next-passive
                         (cons-atom $given-proof $active)
                         $after-age (+ $selection-index 1)
                         (- $steps 1) $max-steps $max-generated
                         $age-weight
                         (atp:resolution-search:stats-add
                           $stats (size-atom $generated) $accepted
                           $tautologies $variants $invalid
                           $local-truncated))))))))))

(= (atp:resolution-search:loop-indexed
     $index $passive $active $next-age $selection-index
     $steps $max-steps $max-generated $age-weight $stats)
   (if (== $passive ())
       (ATP.ResolutionSearchResult
         (ATP.Incomplete PassiveExhausted) $stats)
       (if (<= $steps 0)
           (ATP.ResolutionSearchResult
             (ATP.Incomplete StepBound) $stats)
           (let (ATP.Selected
                  (ATP.SearchItem $given-age $given-proof)
                  $remaining-passive)
                (atp:resolution-search:select-given
                  $passive $selection-index $age-weight)
             (let $given-clause
                  (atp:resolution-search:conclusion $given-proof)
               (if (== $given-clause ())
                   (ATP.ResolutionSearchResult
                     (ATP.Refutation $given-proof) $stats)
                   (let (ATP.Generated $generated $local-truncated)
                        (atp:resolution-search:resolve-with-index
                          $index $given-proof $max-generated)
                     (let $indexed
                          (atp:resolution-search:index-proof
                            $index $given-proof)
                       (let (ATP.Ingested
                              $next-passive $after-age $accepted
                              $tautologies $variants $invalid)
                            (atp:resolution-search:ingest
                              $generated $remaining-passive
                              (cons-atom $given-proof $active)
                              $next-age 0 0 0 0)
                         (atp:resolution-search:loop-indexed
                           $index $next-passive
                           (cons-atom $given-proof $active)
                           $after-age (+ $selection-index 1)
                           (- $steps 1) $max-steps $max-generated
                           $age-weight
                           (atp:resolution-search:stats-add
                             $stats (size-atom $generated) $accepted
                             $tautologies $variants $invalid
                             $local-truncated)))))))))))

(= (atp:resolution-search:named-inputs () $age) (ATP.Inputs () $age))

(= (atp:resolution-search:named-inputs $inputs $age)
   (let ((ATP.NamedClause $name $clause) $rest) (decons-atom $inputs)
     (let (ATP.Inputs $items $after-age)
          (atp:resolution-search:named-inputs $rest (+ $age 1))
       (ATP.Inputs
         (cons-atom
           (ATP.SearchItem $age (ATP.InputClause $name $clause))
           $items)
         $after-age))))

; Set-of-support entry point.  Usable clauses begin processed; support clauses
; begin passive.  The result never interprets bound exhaustion as refutation.
(= (atp:resolution-search:prove
     $usable $support
     (ATP.GivenClauseBounds $max-steps $max-generated)
     $age-weight)
   (let (ATP.Inputs $usable-items $support-age)
        (atp:resolution-search:named-inputs $usable 0)
     (let (ATP.Inputs $support-items $next-age)
          (atp:resolution-search:named-inputs $support $support-age)
       (let $active
            (atp:resolution-search:items-to-proofs $usable-items)
         (atp:resolution-search:loop
           $support-items $active $next-age 0
           $max-steps $max-steps $max-generated $age-weight
           (ATP.ResolutionStats 0 0 0 0 0 0 False))))))

; Same calculus and policy as prove, but complementary parent generation is
; delegated to an ordinary signed-literal Atomspace index.  This is the
; practical default for nontrivial theories; prove remains as the transparent
; list-scan ablation.
(= (atp:resolution-search:prove-indexed
     $usable $support
     (ATP.GivenClauseBounds $max-steps $max-generated)
     $age-weight)
   (let (ATP.Inputs $usable-items $support-age)
        (atp:resolution-search:named-inputs $usable 0)
     (let (ATP.Inputs $support-items $next-age)
          (atp:resolution-search:named-inputs $support $support-age)
       (let $active
            (atp:resolution-search:items-to-proofs $usable-items)
         (let $index (new-space)
           (let $indexed
                (atp:resolution-search:index-proofs $index $active)
             (atp:resolution-search:loop-indexed
               $index $support-items $active $next-age 0
               $max-steps $max-steps $max-generated $age-weight
               (ATP.ResolutionStats 0 0 0 0 0 0 False))))))))

(= (atp:resolution-search:items-to-proofs ()) ())

(= (atp:resolution-search:items-to-proofs $items)
   (let ((ATP.SearchItem $age $proof) $rest) (decons-atom $items)
     (let $proofs
          (eval (atp:resolution-search:items-to-proofs $rest))
       (cons-atom $proof $proofs))))
