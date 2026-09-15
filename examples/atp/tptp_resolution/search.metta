; Example: first-order resolution search over TPTP resolution-input clauses.
;
; Object variables stay explicit `fo-resolution:variable-id` data.  This module
; never turns them into evaluator variables, never calls the evaluator's unify
; operation, and has no ATP-specific native helper: complementary literals are
; joined by the authored `atp:fo-unify` unifier, whose occurs check is
; load-bearing here.
;
; The searcher is an untrusted proof producer.  Its output is a
; `fo-proof:certificate` value of the private FirstOrderResolutionExampleTrace
; language, replayed by the independent checker in
; `trace_check.metta`.  Nothing in this file is trusted by
; that checker.
;
; Agendas, indices, and host lists appearing below are internal algorithmic
; state.  Every clause, literal, term, substitution, and step that leaves this
; module is a value of the checked languages.
;
; Accumulated host lists are bound with `let` before being passed on, because
; `cons-atom` and `decons-atom` consume their arguments without evaluating
; them; an unevaluated accumulator would be spliced in as syntax.
;
; Deliberate incompleteness, named precisely: two parent clauses that share a
; variable scope are not resolved, because a sound inference between them needs
; a renaming step this round does not author.  Input clauses carry distinct
; source occurrences and are therefore always disjoint; the restriction can
; only skip inferences between a derived clause and a parent it descends from.
; No completeness claim is made for this search.

; ---------------------------------------------------------------- literals --

(= (tptp-resolution-example:search:literals-length (fo-resolution:literals-nil)) 0)

(= (tptp-resolution-example:search:literals-length
      (fo-resolution:literals-cons $first $rest))
   (+ 1 (tptp-resolution-example:search:literals-length $rest)))

(= (tptp-resolution-example:search:literal-at (fo-resolution:literals-nil) $index)
   (FOSearch.NoLiteral))

(= (tptp-resolution-example:search:literal-at
      (fo-resolution:literals-cons $first $rest) $index)
   (if (== $index 0)
       (FOSearch.Literal $first)
       (tptp-resolution-example:search:literal-at $rest (- $index 1))))

(= (tptp-resolution-example:search:literals-drop-at (fo-resolution:literals-nil) $index)
   (fo-resolution:literals-nil))

(= (tptp-resolution-example:search:literals-drop-at
      (fo-resolution:literals-cons $first $rest) $index)
   (if (== $index 0)
       $rest
       (fo-resolution:literals-cons $first
         (tptp-resolution-example:search:literals-drop-at $rest (- $index 1)))))

(= (tptp-resolution-example:search:literals-append (fo-resolution:literals-nil) $right)
   $right)

(= (tptp-resolution-example:search:literals-append
      (fo-resolution:literals-cons $first $rest) $right)
   (fo-resolution:literals-cons $first
     (tptp-resolution-example:search:literals-append $rest $right)))

(= (tptp-resolution-example:search:apply-literal $bindings $literal)
   (case $literal
     (((fo-resolution:literal-positive $atom)
        (fo-resolution:literal-positive
          (tptp-resolution-example:unify:apply-atom $bindings $atom)))
      ((fo-resolution:literal-negative $atom)
        (fo-resolution:literal-negative
          (tptp-resolution-example:unify:apply-atom $bindings $atom))))))

(= (tptp-resolution-example:search:apply-literals $bindings (fo-resolution:literals-nil))
   (fo-resolution:literals-nil))

(= (tptp-resolution-example:search:apply-literals $bindings
      (fo-resolution:literals-cons $first $rest))
   (fo-resolution:literals-cons
     (tptp-resolution-example:search:apply-literal $bindings $first)
     (tptp-resolution-example:search:apply-literals $bindings $rest)))

; --------------------------------------------------------------- host lists -

(= (tptp-resolution-example:search:member $item ()) False)

(= (tptp-resolution-example:search:member $item $items)
   (let ($first $rest) (decons-atom $items)
     (if (== $first $item)
         True
         (tptp-resolution-example:search:member $item $rest))))

(= (tptp-resolution-example:search:reverse-list () $acc) $acc)

(= (tptp-resolution-example:search:reverse-list $items $acc)
   (let ($first $rest) (decons-atom $items)
     (let $extended (cons-atom $first $acc)
       (tptp-resolution-example:search:reverse-list $rest $extended))))

(= (tptp-resolution-example:search:append-list () $right) $right)

(= (tptp-resolution-example:search:append-list $left $right)
   (let ($first $rest) (decons-atom $left)
     (let $tail (tptp-resolution-example:search:append-list $rest $right)
       (cons-atom $first $tail))))

; --------------------------------------------------------------- variables --

(= (tptp-resolution-example:search:term-variables $term $acc)
   (case $term
     (((fo-resolution:term-variable $variable)
        (if (tptp-resolution-example:search:member $variable $acc)
            $acc
            (cons-atom $variable $acc)))
      ((fo-resolution:term-function $function $arguments)
        (tptp-resolution-example:search:terms-variables $arguments $acc))
      ($_ $acc))))

(= (tptp-resolution-example:search:terms-variables (fo-resolution:terms-nil) $acc) $acc)

(= (tptp-resolution-example:search:terms-variables
      (fo-resolution:terms-cons $first $rest) $acc)
   (let $after (tptp-resolution-example:search:term-variables $first $acc)
     (tptp-resolution-example:search:terms-variables $rest $after)))

(= (tptp-resolution-example:search:atom-variables $atom $acc)
   (case $atom
     (((fo-resolution:atom-predicate $predicate $arguments)
        (tptp-resolution-example:search:terms-variables $arguments $acc))
      ((fo-resolution:atom-equality $left $right)
        (let $after (tptp-resolution-example:search:term-variables $left $acc)
          (tptp-resolution-example:search:term-variables $right $after))))))

(= (tptp-resolution-example:search:literal-variables $literal $acc)
   (case $literal
     (((fo-resolution:literal-positive $atom)
        (tptp-resolution-example:search:atom-variables $atom $acc))
      ((fo-resolution:literal-negative $atom)
        (tptp-resolution-example:search:atom-variables $atom $acc)))))

(= (tptp-resolution-example:search:literals-variables (fo-resolution:literals-nil) $acc)
   $acc)

(= (tptp-resolution-example:search:literals-variables
      (fo-resolution:literals-cons $first $rest) $acc)
   (let $after (tptp-resolution-example:search:literal-variables $first $acc)
     (tptp-resolution-example:search:literals-variables $rest $after)))

(= (tptp-resolution-example:search:variables-disjoint () $right) True)

(= (tptp-resolution-example:search:variables-disjoint $left $right)
   (let ($first $rest) (decons-atom $left)
     (if (tptp-resolution-example:search:member $first $right)
         False
         (tptp-resolution-example:search:variables-disjoint $rest $right))))

(= (tptp-resolution-example:search:clauses-variable-disjoint
      $left-literals $right-literals)
   (let $left-variables
        (tptp-resolution-example:search:literals-variables $left-literals ())
     (let $right-variables
          (tptp-resolution-example:search:literals-variables $right-literals ())
       (tptp-resolution-example:search:variables-disjoint
         $left-variables $right-variables))))

; ---------------------------------------------------------- substitutions ---
;
; `atp:fo-unify` returns a triangular binding list.  A certificate records the
; resolved form instead, so that a checker can apply it as one simultaneous
; substitution without reimplementing the unifier's walk.

(= (tptp-resolution-example:search:resolve-bindings $bindings ())
   (fo-proof:bindings-nil))

(= (tptp-resolution-example:search:resolve-bindings $bindings $pending)
   (let ((FOUnify.Binding $variable $term) $rest)
        (decons-atom $pending)
     (let $resolved (tptp-resolution-example:unify:apply-term $bindings $term)
       (let $tail (tptp-resolution-example:search:resolve-bindings $bindings $rest)
         (fo-proof:bindings-cons
           (fo-proof:binding $variable $resolved) $tail)))))

(= (tptp-resolution-example:search:substitution $bindings)
   (fo-proof:substitution
     (tptp-resolution-example:search:resolve-bindings $bindings $bindings)))

; ---------------------------------------------------------------- pivoting --

(= (tptp-resolution-example:search:complementary $left-literal $right-literal)
   (case ($left-literal $right-literal)
     ((((fo-resolution:literal-positive $left-atom)
        (fo-resolution:literal-negative $right-atom))
        (FOSearch.Pivot $left-atom $right-atom))
      (((fo-resolution:literal-negative $left-atom)
        (fo-resolution:literal-positive $right-atom))
        (FOSearch.Pivot $left-atom $right-atom))
      ($_ (FOSearch.NoPivot)))))

(= (tptp-resolution-example:search:build-step
      $left-id $left-literals $left-index
      $right-id $right-literals $right-index
      $bindings $next)
   (let $substitution (tptp-resolution-example:search:substitution $bindings)
     (let $residual
          (tptp-resolution-example:search:literals-append
            (tptp-resolution-example:search:literals-drop-at $left-literals $left-index)
            (tptp-resolution-example:search:literals-drop-at $right-literals $right-index))
       (let $conclusion (tptp-resolution-example:search:apply-literals $bindings $residual)
         (FOSearch.Step
           (fo-proof:step
             (fo-proof:derived-clause $next)
             (fo-proof:parent $left-id $left-index $substitution)
             (fo-proof:parent $right-id $right-index $substitution)
             $conclusion)
           (FOSearch.Clause
             (fo-proof:derived-clause $next) $conclusion))))))

(= (tptp-resolution-example:search:try-pivot
      $left-id $left-literals $left-index
      $right-id $right-literals $right-index
      $left-atom $right-atom
      $clauses $steps $next)
   (case (tptp-resolution-example:unify:unify-atom () $left-atom $right-atom)
     (((FOUnify.OK $bindings)
        (let (FOSearch.Step $step $clause)
             (tptp-resolution-example:search:build-step
               $left-id $left-literals $left-index
               $right-id $right-literals $right-index
               $bindings $next)
          (let $next-clauses (cons-atom $clause $clauses)
            (let $next-steps (cons-atom $step $steps)
              (let $advanced (+ $next 1)
                (FOSearch.Acc
                  $next-clauses $next-steps $advanced))))))
      ((FOUnify.Fail $reason)
        (FOSearch.Acc $clauses $steps $next)))))

(= (tptp-resolution-example:search:try-position
      $left-id $left-literals $left-index
      $right-id $right-literals $right-index
      $clauses $steps $next)
   (case ((tptp-resolution-example:search:literal-at $left-literals $left-index)
          (tptp-resolution-example:search:literal-at $right-literals $right-index))
     ((((FOSearch.Literal $left-literal)
        (FOSearch.Literal $right-literal))
        (case (tptp-resolution-example:search:complementary $left-literal $right-literal)
          (((FOSearch.Pivot $left-atom $right-atom)
             (tptp-resolution-example:search:try-pivot
               $left-id $left-literals $left-index
               $right-id $right-literals $right-index
               $left-atom $right-atom $clauses $steps $next))
           ((FOSearch.NoPivot)
             (FOSearch.Acc $clauses $steps $next)))))
      ($_ (FOSearch.Acc $clauses $steps $next)))))

(= (tptp-resolution-example:search:right-positions
      $left-id $left-literals $left-index
      $right-id $right-literals $right-index $right-limit
      $clauses $steps $next)
   (if (< $right-index $right-limit)
       (let (FOSearch.Acc $after-clauses $after-steps $after-next)
            (tptp-resolution-example:search:try-position
              $left-id $left-literals $left-index
              $right-id $right-literals $right-index
              $clauses $steps $next)
         (let $advanced (+ $right-index 1)
           (tptp-resolution-example:search:right-positions
             $left-id $left-literals $left-index
             $right-id $right-literals $advanced $right-limit
             $after-clauses $after-steps $after-next)))
       (FOSearch.Acc $clauses $steps $next)))

(= (tptp-resolution-example:search:left-positions
      $left-id $left-literals $left-index $left-limit
      $right-id $right-literals $right-limit
      $clauses $steps $next)
   (if (< $left-index $left-limit)
       (let (FOSearch.Acc $after-clauses $after-steps $after-next)
            (tptp-resolution-example:search:right-positions
              $left-id $left-literals $left-index
              $right-id $right-literals 0 $right-limit
              $clauses $steps $next)
         (let $advanced (+ $left-index 1)
           (tptp-resolution-example:search:left-positions
             $left-id $left-literals $advanced $left-limit
             $right-id $right-literals $right-limit
             $after-clauses $after-steps $after-next)))
       (FOSearch.Acc $clauses $steps $next)))

(= (tptp-resolution-example:search:resolve-pair
      (FOSearch.Clause $left-id $left-literals)
      (FOSearch.Clause $right-id $right-literals)
      $clauses $steps $next)
   (if (tptp-resolution-example:search:clauses-variable-disjoint
         $left-literals $right-literals)
       (let $left-limit (tptp-resolution-example:search:literals-length $left-literals)
         (let $right-limit (tptp-resolution-example:search:literals-length $right-literals)
           (tptp-resolution-example:search:left-positions
             $left-id $left-literals 0 $left-limit
             $right-id $right-literals $right-limit
             $clauses $steps $next)))
       (FOSearch.Acc $clauses $steps $next)))

; The given clause is always the left parent.  Only one parent order is
; explored: every pair of literal positions is already scanned, and the two
; orders differ only in the order of the residual literals, so the reverse
; order adds a permuted copy of a clause that is reachable anyway.

(= (tptp-resolution-example:search:resolve-with-active $given () $clauses $steps $next)
   (FOSearch.Acc $clauses $steps $next))

(= (tptp-resolution-example:search:resolve-with-active $given $active $clauses $steps $next)
   (let ($first $rest) (decons-atom $active)
     (let (FOSearch.Acc $after-clauses $after-steps $after-next)
          (tptp-resolution-example:search:resolve-pair $given $first $clauses $steps $next)
       (tptp-resolution-example:search:resolve-with-active $given $rest
         $after-clauses $after-steps $after-next))))

; A refutation is reported as soon as an empty clause is generated, so a
; certificate carries the round that produced it and no later search work.

(= (tptp-resolution-example:search:find-empty ()) (FOSearch.NoEmpty))

(= (tptp-resolution-example:search:find-empty $clauses)
   (let ($first $rest) (decons-atom $clauses)
     (let (FOSearch.Clause $identifier $literals) $first
       (if (== $literals (fo-resolution:literals-nil))
           (FOSearch.FoundEmpty $identifier)
           (tptp-resolution-example:search:find-empty $rest)))))

; -------------------------------------------------------------- certificate -

(= (tptp-resolution-example:search:steps-of () $tail) $tail)

(= (tptp-resolution-example:search:steps-of $steps $tail)
   (let ($first $rest) (decons-atom $steps)
     (let $extended (fo-proof:steps-cons $first $tail)
       (tptp-resolution-example:search:steps-of $rest $extended))))

; `$steps` is accumulated newest-first, so folding it onto an empty tail
; restores authoring order.

(= (tptp-resolution-example:search:certificate $source $steps $empty-id)
   (let $ordered (tptp-resolution-example:search:steps-of $steps (fo-proof:steps-nil))
     (fo-proof:certificate $source $ordered $empty-id)))

; -------------------------------------------------------------------- loop --

(= (tptp-resolution-example:search:loop
      $source $passive $active $steps $next $given-budget
      $generated-budget $given-used)
   (if (== $passive ())
       (FOSearch.Result (FOSearch.Incomplete PassiveExhausted)
         (FOSearch.Stats $given-used $next))
       (if (<= $given-budget 0)
           (FOSearch.Result (FOSearch.Incomplete GivenBound)
             (FOSearch.Stats $given-used $next))
           (let ($given $remaining) (decons-atom $passive)
             (let (FOSearch.Clause $given-id $given-literals) $given
               (if (== $given-literals (fo-resolution:literals-nil))
                   (FOSearch.Result
                     (FOSearch.Refutation
                       (tptp-resolution-example:search:certificate
                         $source $steps $given-id))
                     (FOSearch.Stats $given-used $next))
                   (tptp-resolution-example:search:expand
                     $source $remaining $active $given $given-id
                     $steps $next $given-budget $generated-budget
                     $given-used)))))))

(= (tptp-resolution-example:search:expand
      $source $remaining $active $given $given-id
      $steps $next $given-budget $generated-budget $given-used)
   (let (FOSearch.Acc $generated $next-steps $after)
        (tptp-resolution-example:search:resolve-with-active $given $active () $steps $next)
     (let $produced (- $after $next)
       (if (< $generated-budget $produced)
           (FOSearch.Result (FOSearch.Incomplete GeneratedBound)
             (FOSearch.Stats $given-used $next))
           (let $ordered (tptp-resolution-example:search:reverse-list $generated ())
             (case (tptp-resolution-example:search:find-empty $ordered)
               (((FOSearch.FoundEmpty $identifier)
                  (FOSearch.Result
                    (FOSearch.Refutation
                      (tptp-resolution-example:search:certificate
                        $source $next-steps $identifier))
                    (FOSearch.Stats $given-used $after)))
                ((FOSearch.NoEmpty)
                  (let $passive
                       (tptp-resolution-example:search:append-list $remaining $ordered)
                    (let $next-active (cons-atom $given $active)
                      (let $next-given-budget (- $given-budget 1)
                        (let $next-generated-budget
                             (- $generated-budget $produced)
                          (let $next-given-used (+ $given-used 1)
                            (tptp-resolution-example:search:loop
                              $source $passive $next-active
                              $next-steps $after $next-given-budget
                              $next-generated-budget
                              $next-given-used))))))))))))))

; ------------------------------------------------------------------ entry ---

(= (tptp-resolution-example:search:initial (fo-resolution:clauses-nil) $acc) $acc)

(= (tptp-resolution-example:search:initial
      (fo-resolution:clauses-cons
        (fo-resolution:clause $occurrence $name $role $literals)
        $rest)
      $acc)
   (let $extended
        (cons-atom
          (FOSearch.Clause
            (fo-proof:input-clause $occurrence) $literals)
          $acc)
     (tptp-resolution-example:search:initial $rest $extended)))

(= (tptp-resolution-example:search:prove $problem $bounds)
   (let (fo-resolution:problem $source $clauses) $problem
     (let (FOSearch.Bounds $given-budget $generated-budget) $bounds
       (let $reversed (tptp-resolution-example:search:initial $clauses ())
         (let $passive (tptp-resolution-example:search:reverse-list $reversed ())
           (tptp-resolution-example:search:loop $source $passive () () 0
             $given-budget $generated-budget 0))))))
