; Example: replay for the unit-clause fragment of ordered superposition.
;
; This is intentionally a certificate checker, not a search procedure.  The
; certificate supplies the finite substitution, equality direction, target
; literal, and one-based argument position.  The library independently
; applies them, checks a genuine KBO orientation, and compares the exact child
; clause.  Source and target clauses are required to be unit clauses here, so
; their selected literals are trivially maximal.  A future non-unit extension
; must add an explicit literal-ordering check rather than weakening this gate.

(= (atp:superposition:singleton $clause)
   (if (== (size-atom $clause) 1)
       (let ($literal $rest) (decons-atom $clause)
         (ATP.Singleton $literal))
       ATP.NotSingleton))

(= (atp:superposition:index-list-valid ()) True)

(= (atp:superposition:index-list-valid $indices)
   (let ($index $rest) (decons-atom $indices)
     (if (== (get-type $index) Number)
         (if (>= $index 1)
             (atp:superposition:index-list-valid $rest)
             False)
         False)))

(= (atp:superposition:position-valid $position)
   (case $position
     (((ATP.Position $indices)
        (if (== $indices ())
            False
            (atp:superposition:index-list-valid $indices)))
      ($_ False))))

(= (atp:superposition:nth-argument $arguments $index)
   (if (== $arguments ())
       ATP.NoSubterm
       (let ($head $tail) (decons-atom $arguments)
         (if (== $index 1)
             (ATP.Subterm $head)
             (atp:superposition:nth-argument $tail (- $index 1))))))

(= (atp:superposition:subterm-in-arguments
     $arguments $index $rest-path)
   (if (== $arguments ())
       ATP.NoSubterm
       (let ($head $tail) (decons-atom $arguments)
         (if (== $index 1)
             (atp:superposition:subterm-at-path $head $rest-path)
             (atp:superposition:subterm-in-arguments
               $tail (- $index 1) $rest-path)))))

(= (atp:superposition:subterm-at-path $term $path)
   (if (== $path ())
       (ATP.Subterm $term)
       (case $term
         (((ATP.Var $name) ATP.NoSubterm)
          ($_
            (if (== (get-metatype $term) Expression)
                (if (== $term ())
                    ATP.NoSubterm
                    (let ($head $arguments) (decons-atom $term)
                      (let ($index $rest) (decons-atom $path)
                        (atp:superposition:subterm-in-arguments
                          $arguments $index $rest))))
                ATP.NoSubterm))))))

(= (atp:superposition:subterm-at $term (ATP.Position $path))
   (atp:superposition:subterm-at-path $term $path))

(= (atp:superposition:replace-nth
     $arguments $index $rest-path $replacement)
   (if (== $arguments ())
       ATP.ReplaceMissing
       (let ($head $tail) (decons-atom $arguments)
         (if (== $index 1)
             (case (atp:superposition:replace-at-path
                     $head $rest-path $replacement)
               (((ATP.ReplaceOK $new-head)
                  (ATP.ReplaceOK (cons-atom $new-head $tail)))
                ($_ ATP.ReplaceMissing)))
             (case (atp:superposition:replace-nth
                     $tail (- $index 1) $rest-path $replacement)
               (((ATP.ReplaceOK $new-tail)
                  (ATP.ReplaceOK (cons-atom $head $new-tail)))
                ($_ ATP.ReplaceMissing)))))))

(= (atp:superposition:replace-at-path $term $path $replacement)
   (if (== $path ())
       (ATP.ReplaceOK $replacement)
       (case $term
         (((ATP.Var $name) ATP.ReplaceMissing)
          ($_
            (if (== (get-metatype $term) Expression)
                (if (== $term ())
                    ATP.ReplaceMissing
                    (let ($head $arguments) (decons-atom $term)
                      (let ($index $rest-path) (decons-atom $path)
                        (case (atp:superposition:replace-nth
                                $arguments $index $rest-path $replacement)
                          (((ATP.ReplaceOK $new-arguments)
                             (ATP.ReplaceOK
                               (cons-atom $head $new-arguments)))
                           ($_ ATP.ReplaceMissing))))))
                ATP.ReplaceMissing))))))

(= (atp:superposition:replace-at
     $term (ATP.Position $path) $replacement)
   (atp:superposition:replace-at-path $term $path $replacement))

(= (atp:superposition:literal-atom $literal)
   (case $literal
     (((ATP.Pos $atom) (ATP.LiteralAtom $atom))
      ((ATP.Neg $atom) (ATP.LiteralAtom $atom))
      ($_ ATP.MalformedLiteral))))

(= (atp:superposition:literal-with-atom $literal $atom)
   (case $literal
     (((ATP.Pos $old) (ATP.Pos $atom))
      ((ATP.Neg $old) (ATP.Neg $atom))
      ($_ ATP.MalformedLiteral))))

(= (atp:superposition:literal-subterm $literal $position)
   (case (atp:superposition:literal-atom $literal)
     (((ATP.LiteralAtom $atom)
        (atp:superposition:subterm-at $atom $position))
      ($_ ATP.NoSubterm))))

(= (atp:superposition:replace-in-literal
     $literal $position $replacement)
   (case (atp:superposition:literal-atom $literal)
     (((ATP.LiteralAtom $atom)
        (case (atp:superposition:replace-at
                $atom $position $replacement)
          (((ATP.ReplaceOK $new-atom)
             (ATP.ReplaceOK
               (atp:superposition:literal-with-atom
                 $literal $new-atom)))
           ($_ ATP.ReplaceMissing))))
      ($_ ATP.ReplaceMissing))))

(= (atp:superposition:object-variable $term)
   (case $term
     (((ATP.Var $name) True)
      ($_ False))))

(= (atp:superposition:oriented-equality $literal $direction)
   (case ($literal $direction)
     ((((ATP.Pos (ATP.Eq $left $right)) ATP.LeftToRight)
        (ATP.Oriented $left $right))
      (((ATP.Pos (ATP.Eq $left $right)) ATP.RightToLeft)
        (ATP.Oriented $right $left))
      ($_ ATP.NotOrientedEquality))))

(= (atp:superposition:check-unit-rewrite
     $instantiated-target $position $from $to $ordering $expected)
   (if (atp:kbo:greater $ordering $from $to)
       (case (atp:superposition:literal-subterm
               $instantiated-target $position)
         (((ATP.Subterm $selected-subterm)
            (if (== $selected-subterm $from)
                (case (atp:superposition:replace-in-literal
                        $instantiated-target $position $to)
                  (((ATP.ReplaceOK $child-literal)
                     (atp:resolution:multiset-equal
                       (cons-atom $child-literal ()) $expected))
                   ($_ False)))
                False))
          ($_ False)))
       False))

(= (atp:superposition:check-unit-instantiated
     $instantiated-equality $direction $instantiated-target
     $position $ordering $expected)
   (case (atp:superposition:oriented-equality
           $instantiated-equality $direction)
     (((ATP.Oriented $from $to)
        (atp:superposition:check-unit-rewrite
          $instantiated-target $position $from $to $ordering $expected))
      ($_ False))))

(= (atp:superposition:check-unit-original
     $bindings $equality-literal $direction $target-literal
     $position $ordering $expected)
   (let $instantiated-equality
        (eval (atp:resolution:substitute-term
                $bindings $equality-literal))
     (let $instantiated-target
          (eval (atp:resolution:substitute-term
                  $bindings $target-literal))
       (atp:superposition:check-unit-instantiated
         $instantiated-equality $direction $instantiated-target
         $position $ordering $expected))))

(= (atp:superposition:check-unit-literals
     $bindings $source-literal $equality-literal
     $only-target-literal $target-literal $direction $position
     $ordering $expected)
   (if (== $source-literal $equality-literal)
       (if (== $only-target-literal $target-literal)
           (case (atp:superposition:literal-subterm
                   $target-literal $position)
             (((ATP.Subterm $original-subterm)
                (if (atp:superposition:object-variable $original-subterm)
                    False
                    (atp:superposition:check-unit-original
                      $bindings $equality-literal $direction
                      $target-literal $position $ordering $expected)))
              ($_ False)))
           False)
       False))

(= (atp:superposition:check-unit-core-bindings
     $source $target $bindings
     $equality-literal $direction $target-literal $position
     $ordering $expected)
   (if (== (size-atom $source) 1)
       (if (== (size-atom $target) 1)
           (let ($source-literal $source-rest) (decons-atom $source)
             (let ($only-target-literal $target-rest) (decons-atom $target)
               (atp:superposition:check-unit-literals
                 $bindings $source-literal $equality-literal
                 $only-target-literal $target-literal $direction $position
                 $ordering $expected)))
           False)
       False))

(= (atp:superposition:check-unit-core
     $source $target $substitution
     $equality-literal $direction $target-literal $position
     $ordering $expected)
   (case $substitution
     (((ATP.Subst $bindings)
        (atp:superposition:check-unit-core-bindings
          $source $target $bindings
          $equality-literal $direction $target-literal $position
          $ordering $expected))
      ($_ False))))

; Checked unit ordered-superposition inference.  Any malformed component is a
; Boolean rejection, never an exception and never an incomplete-search claim.
(= (atp:superposition:check-unit-step
     $source $target $substitution
     $equality-literal $direction $target-literal $position
     $ordering $expected)
   (if (atp:resolution:clause-valid $source)
       (if (atp:resolution:clause-valid $target)
           (if (atp:resolution:clause-valid $expected)
               (if (atp:resolution:literal-valid $equality-literal)
                   (if (atp:resolution:literal-valid $target-literal)
                       (if (atp:resolution:substitution-valid $substitution)
                           (if (atp:superposition:position-valid $position)
                               (if (atp:kbo:config-valid $ordering)
                                   (atp:superposition:check-unit-core
                                     $source $target $substitution
                                     $equality-literal $direction
                                     $target-literal $position
                                     $ordering $expected)
                                   False)
                               False)
                           False)
                       False)
                   False)
               False)
           False)
       False))
