; Example: independent replay checker for a private resolution trace.
;
; This checker is independent of the producer in `search.metta`
; is an untrusted producer.  Nothing here calls the searcher, and nothing here
; calls `atp:fo-unify`: the checker never unifies.  It applies the recorded
; substitutions as one simultaneous replacement, recomputes each resolvent from
; the two parent clauses, and compares the recomputation with the recorded
; clause.  A step is accepted only when the recomputed resolvent is reached.
;
; Substitution well-formedness is deliberately stricter than soundness needs.
; Applying any substitution to a universally quantified parent clause yields a
; logical consequence, so a repeated or cyclic binding would not by itself make
; a step unsound; the checker rejects such certificates because a duplicate key
; has no single meaning and a cyclic binding is not a resolved unifier.  A
; well-formed substitution has distinct keys and no key occurring in any of its
; own replacement terms, which rejects direct cycles (`X` bound to `f(X)`) and
; indirect ones (`X` to `f(Y)` with `Y` also bound) alike.

; ------------------------------------------------------- substitution form --

(= (tptp-resolution-example:check:lookup-binding (fo-proof:bindings-nil) $variable)
   (FOCheck.Unbound))

(= (tptp-resolution-example:check:lookup-binding
      (fo-proof:bindings-cons
        (fo-proof:binding $candidate $term) $rest)
      $variable)
   (if (== $candidate $variable)
       (FOCheck.Bound $term)
       (tptp-resolution-example:check:lookup-binding $rest $variable)))

; One simultaneous replacement: a replacement term is never substituted into
; again, so the checker does not depend on any unifier's walk order.

(= (tptp-resolution-example:check:apply-term $bindings $term)
   (case $term
     (((fo-resolution:term-variable $variable)
        (case (tptp-resolution-example:check:lookup-binding $bindings $variable)
          (((FOCheck.Bound $replacement) $replacement)
           ((FOCheck.Unbound) $term))))
      ((fo-resolution:term-function $function $arguments)
        (fo-resolution:term-function $function
          (tptp-resolution-example:check:apply-terms $bindings $arguments)))
      ($_ $term))))

(= (tptp-resolution-example:check:apply-terms $bindings (fo-resolution:terms-nil))
   (fo-resolution:terms-nil))

(= (tptp-resolution-example:check:apply-terms $bindings
      (fo-resolution:terms-cons $first $rest))
   (fo-resolution:terms-cons
     (tptp-resolution-example:check:apply-term $bindings $first)
     (tptp-resolution-example:check:apply-terms $bindings $rest)))

(= (tptp-resolution-example:check:apply-atom $bindings $atom)
   (case $atom
     (((fo-resolution:atom-predicate $predicate $arguments)
        (fo-resolution:atom-predicate $predicate
          (tptp-resolution-example:check:apply-terms $bindings $arguments)))
      ((fo-resolution:atom-equality $left $right)
        (fo-resolution:atom-equality
          (tptp-resolution-example:check:apply-term $bindings $left)
          (tptp-resolution-example:check:apply-term $bindings $right))))))

(= (tptp-resolution-example:check:apply-literal $bindings $literal)
   (case $literal
     (((fo-resolution:literal-positive $atom)
        (fo-resolution:literal-positive
          (tptp-resolution-example:check:apply-atom $bindings $atom)))
      ((fo-resolution:literal-negative $atom)
        (fo-resolution:literal-negative
          (tptp-resolution-example:check:apply-atom $bindings $atom))))))

(= (tptp-resolution-example:check:apply-literals $bindings (fo-resolution:literals-nil))
   (fo-resolution:literals-nil))

(= (tptp-resolution-example:check:apply-literals $bindings
      (fo-resolution:literals-cons $first $rest))
   (fo-resolution:literals-cons
     (tptp-resolution-example:check:apply-literal $bindings $first)
     (tptp-resolution-example:check:apply-literals $bindings $rest)))

; ------------------------------------------- substitution well-formedness ---

(= (tptp-resolution-example:check:member $item ()) False)

(= (tptp-resolution-example:check:member $item $items)
   (let ($first $rest) (decons-atom $items)
     (if (== $first $item)
         True
         (tptp-resolution-example:check:member $item $rest))))

(= (tptp-resolution-example:check:keys (fo-proof:bindings-nil) $acc) $acc)

(= (tptp-resolution-example:check:keys
      (fo-proof:bindings-cons
        (fo-proof:binding $variable $term) $rest)
      $acc)
   (let $extended (cons-atom $variable $acc)
     (tptp-resolution-example:check:keys $rest $extended)))

(= (tptp-resolution-example:check:keys-distinct (fo-proof:bindings-nil) $seen) True)

(= (tptp-resolution-example:check:keys-distinct
      (fo-proof:bindings-cons
        (fo-proof:binding $variable $term) $rest)
      $seen)
   (if (tptp-resolution-example:check:member $variable $seen)
       False
       (let $extended (cons-atom $variable $seen)
         (tptp-resolution-example:check:keys-distinct $rest $extended))))

(= (tptp-resolution-example:check:term-mentions $term $keys)
   (case $term
     (((fo-resolution:term-variable $variable)
        (tptp-resolution-example:check:member $variable $keys))
      ((fo-resolution:term-function $function $arguments)
        (tptp-resolution-example:check:terms-mention $arguments $keys))
      ($_ False))))

(= (tptp-resolution-example:check:terms-mention (fo-resolution:terms-nil) $keys) False)

(= (tptp-resolution-example:check:terms-mention
      (fo-resolution:terms-cons $first $rest) $keys)
   (if (tptp-resolution-example:check:term-mentions $first $keys)
       True
       (tptp-resolution-example:check:terms-mention $rest $keys)))

(= (tptp-resolution-example:check:ranges-avoid-keys (fo-proof:bindings-nil) $keys) True)

(= (tptp-resolution-example:check:ranges-avoid-keys
      (fo-proof:bindings-cons
        (fo-proof:binding $variable $term) $rest)
      $keys)
   (if (tptp-resolution-example:check:term-mentions $term $keys)
       False
       (tptp-resolution-example:check:ranges-avoid-keys $rest $keys)))

(= (tptp-resolution-example:check:substitution-wellformed
      (fo-proof:substitution $bindings))
   (if (tptp-resolution-example:check:keys-distinct $bindings ())
       (let $keys (tptp-resolution-example:check:keys $bindings ())
         (tptp-resolution-example:check:ranges-avoid-keys $bindings $keys))
       False))

; ------------------------------------------------------------ literal lists -

(= (tptp-resolution-example:check:literal-at (fo-resolution:literals-nil) $index)
   (FOCheck.NoLiteral))

(= (tptp-resolution-example:check:literal-at
      (fo-resolution:literals-cons $first $rest) $index)
   (if (== $index 0)
       (FOCheck.Literal $first)
       (tptp-resolution-example:check:literal-at $rest (- $index 1))))

(= (tptp-resolution-example:check:literals-drop-at (fo-resolution:literals-nil) $index)
   (fo-resolution:literals-nil))

(= (tptp-resolution-example:check:literals-drop-at
      (fo-resolution:literals-cons $first $rest) $index)
   (if (== $index 0)
       $rest
       (fo-resolution:literals-cons $first
         (tptp-resolution-example:check:literals-drop-at $rest (- $index 1)))))

(= (tptp-resolution-example:check:literals-append (fo-resolution:literals-nil) $right)
   $right)

(= (tptp-resolution-example:check:literals-append
      (fo-resolution:literals-cons $first $rest) $right)
   (fo-resolution:literals-cons $first
     (tptp-resolution-example:check:literals-append $rest $right)))

; ------------------------------------------------------------- environment --

(= (tptp-resolution-example:check:lookup () $identifier) (FOCheck.Missing))

(= (tptp-resolution-example:check:lookup $entries $identifier)
   (let ((FOCheck.Entry $candidate $literals) $rest)
        (decons-atom $entries)
     (if (== $candidate $identifier)
         (FOCheck.Found $literals)
         (tptp-resolution-example:check:lookup $rest $identifier))))

(= (tptp-resolution-example:check:inputs (fo-resolution:clauses-nil) $acc) $acc)

(= (tptp-resolution-example:check:inputs
      (fo-resolution:clauses-cons
        (fo-resolution:clause $occurrence $name $role $literals)
        $rest)
      $acc)
   (let $extended
        (cons-atom
          (FOCheck.Entry
            (fo-proof:input-clause $occurrence) $literals)
          $acc)
     (tptp-resolution-example:check:inputs $rest $extended)))

; ------------------------------------------------------------------- steps --
;
; Every parent is looked up in an environment holding exactly the problem's
; input clauses and the clauses concluded by earlier steps, so a step can never
; cite a clause that has not itself been checked.

(= (tptp-resolution-example:check:complementary $left-literal $right-literal)
   (case ($left-literal $right-literal)
     ((((fo-resolution:literal-positive $left-atom)
        (fo-resolution:literal-negative $right-atom))
        (== $left-atom $right-atom))
      (((fo-resolution:literal-negative $left-atom)
        (fo-resolution:literal-positive $right-atom))
        (== $left-atom $right-atom))
      ($_ False))))

(= (tptp-resolution-example:check:step-with-parents
      $environment $expected-identifier $recorded-identifier
      $left-literals $left-index $left-substitution
      $right-literals $right-index $right-substitution
      $recorded)
   (if (== $recorded-identifier $expected-identifier)
       (if (tptp-resolution-example:check:substitution-wellformed $left-substitution)
           (if (tptp-resolution-example:check:substitution-wellformed $right-substitution)
               (let (fo-proof:substitution $left-bindings)
                    $left-substitution
                 (let (fo-proof:substitution $right-bindings)
                      $right-substitution
                   (let $left-instance
                        (tptp-resolution-example:check:apply-literals
                          $left-bindings $left-literals)
                     (let $right-instance
                          (tptp-resolution-example:check:apply-literals
                            $right-bindings $right-literals)
                       (case ((tptp-resolution-example:check:literal-at
                                $left-instance $left-index)
                              (tptp-resolution-example:check:literal-at
                                $right-instance $right-index))
                         ((((FOCheck.Literal $left-literal)
                            (FOCheck.Literal $right-literal))
                            (if (tptp-resolution-example:check:complementary
                                  $left-literal $right-literal)
                                (let $recomputed
                                     (tptp-resolution-example:check:literals-append
                                       (tptp-resolution-example:check:literals-drop-at
                                         $left-instance $left-index)
                                       (tptp-resolution-example:check:literals-drop-at
                                         $right-instance $right-index))
                                  (if (== $recomputed $recorded)
                                      (FOCheck.StepVerified $recomputed)
                                      (FOCheck.Rejected
                                        ResolventDoesNotMatch)))
                                (FOCheck.Rejected
                                  SelectedLiteralsNotComplementary)))
                          ($_ (FOCheck.Rejected
                                SelectedPositionOutOfRange))))))))
               (FOCheck.Rejected RightSubstitutionMalformed))
           (FOCheck.Rejected LeftSubstitutionMalformed))
       (FOCheck.Rejected StepIdentifierOutOfOrder)))

(= (tptp-resolution-example:check:step
      $environment $expected-identifier
      (fo-proof:step $identifier
        (fo-proof:parent $left-id $left-index $left-substitution)
        (fo-proof:parent $right-id $right-index $right-substitution)
        $recorded))
   (case ((tptp-resolution-example:check:lookup $environment $left-id)
          (tptp-resolution-example:check:lookup $environment $right-id))
     ((((FOCheck.Found $left-literals) (FOCheck.Found $right-literals))
        (tptp-resolution-example:check:step-with-parents
          $environment $expected-identifier $identifier
          $left-literals $left-index $left-substitution
          $right-literals $right-index $right-substitution
          $recorded))
      ($_ (FOCheck.Rejected ParentClauseNotDerived)))))

(= (tptp-resolution-example:check:steps $environment (fo-proof:steps-nil) $index)
   (FOCheck.StepsVerified $environment))

(= (tptp-resolution-example:check:steps $environment
      (fo-proof:steps-cons $first $rest) $index)
   (case (tptp-resolution-example:check:step $environment
           (fo-proof:derived-clause $index) $first)
     (((FOCheck.StepVerified $literals)
        (let $extended
             (cons-atom
               (FOCheck.Entry
                 (fo-proof:derived-clause $index) $literals)
               $environment)
          (let $advanced (+ $index 1)
            (tptp-resolution-example:check:steps $extended $rest $advanced))))
      ((FOCheck.Rejected $reason) (FOCheck.Rejected $reason)))))

; -------------------------------------------------------------- entry point -

(= (tptp-resolution-example:check:certificate $problem $certificate)
   (let (fo-resolution:problem $problem-source $clauses) $problem
     (let (fo-proof:certificate
             $certificate-source $steps $empty-identifier)
          $certificate
       (tptp-resolution-example:check:certificate-parts
         $problem-source $clauses
         $certificate-source $steps $empty-identifier))))

(= (tptp-resolution-example:check:certificate-parts
      $problem-source $clauses
      $certificate-source $steps $empty-identifier)
   (if (== $problem-source $certificate-source)
       (case (tptp-resolution-example:check:steps (tptp-resolution-example:check:inputs $clauses ()) $steps 0)
         (((FOCheck.StepsVerified $environment)
            (case (tptp-resolution-example:check:lookup $environment $empty-identifier)
              (((FOCheck.Found $literals)
                 (if (== $literals (fo-resolution:literals-nil))
                     (FOCheck.Verified)
                     (FOCheck.Rejected ConcludingClauseNotEmpty)))
               ((FOCheck.Missing)
                 (FOCheck.Rejected ConcludingClauseNotDerived)))))
          ((FOCheck.Rejected $reason) (FOCheck.Rejected $reason))))
       (FOCheck.Rejected SourceDigestMismatch)))
