; Example: first-order unification over TPTP resolution-input data.
;
; Object variables are explicit scoped constructors.  This module does not
; call the evaluator's unify operation and has no ATP-specific native helper.
; Its finite binding list is internal algorithmic state; source and result
; terms remain values of the checked resolution-input language.

(= (tptp-resolution-example:unify:lookup () $variable) FOUnify.Unbound)

(= (tptp-resolution-example:unify:lookup $bindings $variable)
   (let ((FOUnify.Binding $candidate $term) $rest)
        (decons-atom $bindings)
     (if (== $candidate $variable)
         (FOUnify.Bound $term)
         (tptp-resolution-example:unify:lookup $rest $variable))))

(= (tptp-resolution-example:unify:walk $bindings $term)
   (case $term
     (((fo-resolution:term-variable $variable)
        (case (tptp-resolution-example:unify:lookup $bindings $variable)
          (((FOUnify.Bound $replacement)
             (tptp-resolution-example:unify:walk $bindings $replacement))
           ($_ $term))))
      ($_ $term))))

(= (tptp-resolution-example:unify:occurs-terms $bindings $variable
      (fo-resolution:terms-nil))
   False)

(= (tptp-resolution-example:unify:occurs-terms $bindings $variable
      (fo-resolution:terms-cons $first $rest))
   (if (tptp-resolution-example:unify:occurs $bindings $variable $first)
       True
       (tptp-resolution-example:unify:occurs-terms $bindings $variable $rest)))

(= (tptp-resolution-example:unify:occurs $bindings $variable $term)
   (let $walked (tptp-resolution-example:unify:walk $bindings $term)
     (case $walked
       (((fo-resolution:term-variable $candidate)
          (== $variable $candidate))
        ((fo-resolution:term-function $function $arguments)
          (tptp-resolution-example:unify:occurs-terms
            $bindings $variable $arguments))
        ($_ False)))))

(= (tptp-resolution-example:unify:bind $bindings $variable $term)
   (if (tptp-resolution-example:unify:occurs $bindings $variable $term)
       (FOUnify.Fail OccursCheck)
       (FOUnify.OK
         (cons-atom (FOUnify.Binding $variable $term) $bindings))))

(= (tptp-resolution-example:unify:unify-term $bindings $left $right)
   (let $walked-left (tptp-resolution-example:unify:walk $bindings $left)
     (let $walked-right (tptp-resolution-example:unify:walk $bindings $right)
       (if (== $walked-left $walked-right)
           (FOUnify.OK $bindings)
           (case ($walked-left $walked-right)
             ((((fo-resolution:term-variable $variable) $term)
                (tptp-resolution-example:unify:bind $bindings $variable $term))
              (($term (fo-resolution:term-variable $variable))
                (tptp-resolution-example:unify:bind $bindings $variable $term))
              (((fo-resolution:term-function $left-function $left-arguments)
                (fo-resolution:term-function
                  $right-function $right-arguments))
                (if (== $left-function $right-function)
                    (tptp-resolution-example:unify:unify-terms
                      $bindings $left-arguments $right-arguments)
                    (FOUnify.Fail FunctionMismatch)))
              (((fo-resolution:term-number $left-kind $left-lexeme)
                (fo-resolution:term-number $right-kind $right-lexeme))
                (FOUnify.Fail NumberMismatch))
              (((fo-resolution:term-distinct $left-lexeme)
                (fo-resolution:term-distinct $right-lexeme))
                (FOUnify.Fail DistinctObjectMismatch))
              ($_ (FOUnify.Fail TermConstructorMismatch))))))))

(= (tptp-resolution-example:unify:unify-terms $bindings $left $right)
   (case ($left $right)
     ((((fo-resolution:terms-nil) (fo-resolution:terms-nil))
        (FOUnify.OK $bindings))
      (((fo-resolution:terms-cons $left-first $left-rest)
        (fo-resolution:terms-cons $right-first $right-rest))
        (case (tptp-resolution-example:unify:unify-term
                $bindings $left-first $right-first)
          (((FOUnify.OK $after-first)
             (tptp-resolution-example:unify:unify-terms
               $after-first $left-rest $right-rest))
           ((FOUnify.Fail $reason) (FOUnify.Fail $reason)))))
      ($_ (FOUnify.Fail TermArityMismatch)))))

(= (tptp-resolution-example:unify:unify-atom $bindings $left $right)
   (case ($left $right)
     ((((fo-resolution:atom-predicate
          $left-predicate $left-arguments)
        (fo-resolution:atom-predicate
          $right-predicate $right-arguments))
        (if (== $left-predicate $right-predicate)
            (tptp-resolution-example:unify:unify-terms
              $bindings $left-arguments $right-arguments)
            (FOUnify.Fail PredicateMismatch)))
      (((fo-resolution:atom-equality $left-left $left-right)
        (fo-resolution:atom-equality $right-left $right-right))
        (case (tptp-resolution-example:unify:unify-term
                $bindings $left-left $right-left)
          (((FOUnify.OK $after-left)
             (tptp-resolution-example:unify:unify-term
               $after-left $left-right $right-right))
           ((FOUnify.Fail $reason) (FOUnify.Fail $reason)))))
      ($_ (FOUnify.Fail AtomConstructorMismatch)))))

(= (tptp-resolution-example:unify:apply-terms $bindings
      (fo-resolution:terms-nil))
   (fo-resolution:terms-nil))

(= (tptp-resolution-example:unify:apply-terms $bindings
      (fo-resolution:terms-cons $first $rest))
   (fo-resolution:terms-cons
     (tptp-resolution-example:unify:apply-term $bindings $first)
     (tptp-resolution-example:unify:apply-terms $bindings $rest)))

(= (tptp-resolution-example:unify:apply-term $bindings $term)
   (let $walked (tptp-resolution-example:unify:walk $bindings $term)
     (case $walked
       (((fo-resolution:term-function $function $arguments)
          (fo-resolution:term-function $function
            (tptp-resolution-example:unify:apply-terms $bindings $arguments)))
        ($_ $walked)))))

(= (tptp-resolution-example:unify:apply-atom $bindings $atom)
   (case $atom
     (((fo-resolution:atom-predicate $predicate $arguments)
        (fo-resolution:atom-predicate $predicate
          (tptp-resolution-example:unify:apply-terms $bindings $arguments)))
      ((fo-resolution:atom-equality $left $right)
        (fo-resolution:atom-equality
          (tptp-resolution-example:unify:apply-term $bindings $left)
          (tptp-resolution-example:unify:apply-term $bindings $right))))))

(= (tptp-resolution-example:unify:equalized $result $left $right)
   (case $result
     (((FOUnify.OK $bindings)
        (== (tptp-resolution-example:unify:apply-atom $bindings $left)
            (tptp-resolution-example:unify:apply-atom $bindings $right)))
      ((FOUnify.Fail $reason) False))))
