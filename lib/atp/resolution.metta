; Certificate replay for first-order binary resolution.
;
; Terms use ordinary MeTTa expressions.  Object variables are explicit data,
; (ATP.Var name), so certificate replay never confuses them with evaluator
; variables.  A search producer may use ordinary MeTTa unify to discover a
; substitution; replay only applies the supplied finite substitution and
; compares the instantiated pivot and resolvent.
;
; Literals and substitutions have the following forms:
;
;   (ATP.Pos atom)                    positive literal
;   (ATP.Neg atom)                    negative literal
;   (ATP.Subst ((ATP.Bind x term) ...))
;
; A clause is an ordinary expression list of literals.  Clause comparison is
; multiset comparison, so source order is irrelevant while multiplicity is
; preserved.

(= (atp:resolution:member $item ()) False)

(= (atp:resolution:member $item $items)
   (let ($head $tail) (decons-atom $items)
     (if (== $item $head)
         True
         (atp:resolution:member $item $tail))))

(= (atp:resolution:term-list-valid ()) True)

(= (atp:resolution:term-list-valid $terms)
   (let ($head $tail) (decons-atom $terms)
     (if (atp:resolution:term-valid $head)
         (atp:resolution:term-list-valid $tail)
         False)))

(= (atp:resolution:term-valid $term)
   (case $term
     (((ATP.Var $name)
        (if (== (get-metatype $name) Variable) False True))
      ($_
        (if (== (get-metatype $term) Variable)
            False
            (if (== (get-metatype $term) Expression)
                (atp:resolution:term-list-valid $term)
                True))))))

(= (atp:resolution:literal-valid $literal)
   (case $literal
     (((ATP.Pos $atom) (atp:resolution:term-valid $atom))
      ((ATP.Neg $atom) (atp:resolution:term-valid $atom))
      ($_ False))))

(= (atp:resolution:clause-valid ()) True)

(= (atp:resolution:clause-valid $clause)
   (let ($literal $rest) (decons-atom $clause)
     (if (atp:resolution:literal-valid $literal)
         (atp:resolution:clause-valid $rest)
         False)))

(= (atp:resolution:binding-name $binding)
   (case $binding
     (((ATP.Bind $name $term) (ATP.BindingName $name))
      ($_ ATP.MalformedBinding))))

(= (atp:resolution:binding-names ()) ())

(= (atp:resolution:binding-names $bindings)
   (let ($binding $rest) (decons-atom $bindings)
     (case (atp:resolution:binding-name $binding)
       (((ATP.BindingName $name)
          (let $names
               (eval (atp:resolution:binding-names $rest))
            (cons-atom $name $names)))
        ($_ (ATP.MalformedBindings))))))

(= (atp:resolution:no-duplicate-bindings ()) True)

(= (atp:resolution:no-duplicate-bindings $bindings)
   (let ($binding $rest) (decons-atom $bindings)
     (case $binding
       (((ATP.Bind $name $term)
          (case (atp:resolution:binding-names $rest)
            (((ATP.MalformedBindings) False)
             ($names
               (if (atp:resolution:member $name $names)
                   False
                   (atp:resolution:no-duplicate-bindings $rest))))))
        ($_ False)))))

(= (atp:resolution:bindings-valid ()) True)

(= (atp:resolution:bindings-valid $bindings)
   (let ($binding $rest) (decons-atom $bindings)
     (case $binding
       (((ATP.Bind $name $term)
          (if (== (get-metatype $name) Variable)
              False
              (if (atp:resolution:term-valid $term)
                  (atp:resolution:bindings-valid $rest)
                  False)))
        ($_ False)))))

; Follow bindings as a finite dependency graph.  This is deliberately part of
; certificate validation rather than an assumption about the producer's
; unifier: direct and indirect substitution cycles both fail closed.
(= (atp:resolution:term-reaches-name $bindings $term $target $seen)
   (case $term
     (((ATP.Var $name)
        (if (== $name $target)
            True
            (if (atp:resolution:member $name $seen)
                False
                (case (atp:resolution:lookup $bindings $name)
                  (((ATP.Binding $replacement)
                     (atp:resolution:term-reaches-name
                       $bindings $replacement $target
                       (cons-atom $name $seen)))
                   ($_ False))))))
      ($_
        (if (== (get-metatype $term) Expression)
            (atp:resolution:terms-reach-name
              $bindings $term $target $seen)
            False)))))

(= (atp:resolution:terms-reach-name $bindings () $target $seen) False)

(= (atp:resolution:terms-reach-name $bindings $terms $target $seen)
   (let ($head $tail) (decons-atom $terms)
     (if (atp:resolution:term-reaches-name
           $bindings $head $target $seen)
         True
         (atp:resolution:terms-reach-name
           $bindings $tail $target $seen))))

(= (atp:resolution:bindings-acyclic ()) True)

(= (atp:resolution:bindings-acyclic $bindings)
   (let ($binding $rest) (decons-atom $bindings)
     (case $binding
       (((ATP.Bind $name $term)
          (if (atp:resolution:term-reaches-name
                $bindings $term $name ())
              False
              (atp:resolution:bindings-acyclic $rest)))
        ($_ False)))))

(= (atp:resolution:substitution-valid $substitution)
   (case $substitution
     (((ATP.Subst $bindings)
        (if (atp:resolution:bindings-valid $bindings)
            (if (atp:resolution:no-duplicate-bindings $bindings)
                (atp:resolution:bindings-acyclic $bindings)
                False)
            False))
      ($_ False))))

(= (atp:resolution:lookup () $name) ATP.NoBinding)

(= (atp:resolution:lookup $bindings $name)
   (let ($binding $rest) (decons-atom $bindings)
     (case $binding
       (((ATP.Bind $candidate $term)
          (if (== $candidate $name)
              (ATP.Binding $term)
              (atp:resolution:lookup $rest $name)))
        ($_ ATP.NoBinding)))))

(= (atp:resolution:substitute-list-seen $bindings $seen ()) ())

(= (atp:resolution:substitute-list-seen $bindings $seen $items)
   (let ($head $tail) (decons-atom $items)
     (let $substituted-head
          (eval (atp:resolution:substitute-term-seen
                  $bindings $seen $head))
       (let $substituted-tail
            (eval (atp:resolution:substitute-list-seen
                    $bindings $seen $tail))
         (cons-atom $substituted-head $substituted-tail)))))

(= (atp:resolution:substitute-term-seen $bindings $seen $term)
   (case $term
     (((ATP.Var $name)
        (if (atp:resolution:member $name $seen)
            ATP.CyclicSubstitution
            (case (atp:resolution:lookup $bindings $name)
              (((ATP.Binding $replacement)
                 (atp:resolution:substitute-term-seen
                   $bindings (cons-atom $name $seen) $replacement))
               ($_ $term)))))
      ($_
        (if (== (get-metatype $term) Expression)
            (eval (atp:resolution:substitute-list-seen
                    $bindings $seen $term))
            $term)))))

(= (atp:resolution:substitute-term $bindings $term)
   (atp:resolution:substitute-term-seen $bindings () $term))

(= (atp:resolution:substitute-list $bindings $items)
   (atp:resolution:substitute-list-seen $bindings () $items))

(= (atp:resolution:substitute-clause
     (ATP.Subst $bindings) $clause)
   (atp:resolution:substitute-list $bindings $clause))

(= (atp:resolution:complementary $left $right)
   (case ($left $right)
     ((((ATP.Pos $atom) (ATP.Neg $other)) (== $atom $other))
      (((ATP.Neg $atom) (ATP.Pos $other)) (== $atom $other))
      ($_ False))))

(= (atp:resolution:remove-one $item ()) ATP.RemoveMissing)

(= (atp:resolution:remove-one $item $items)
   (let ($head $tail) (decons-atom $items)
     (if (== $item $head)
         (ATP.RemoveOK $tail)
         (case (atp:resolution:remove-one $item $tail)
           (((ATP.RemoveOK $rest)
              (ATP.RemoveOK (cons-atom $head $rest)))
            ($_ ATP.RemoveMissing))))))

(= (atp:resolution:append () $right) $right)

(= (atp:resolution:append $left $right)
   (let ($head $tail) (decons-atom $left)
     (let $rest
          (eval (atp:resolution:append $tail $right))
       (cons-atom $head $rest))))

(= (atp:resolution:multiset-equal () $right)
   (== $right ()))

(= (atp:resolution:multiset-equal $left $right)
   (let ($head $tail) (decons-atom $left)
     (case (atp:resolution:remove-one $head $right)
       (((ATP.RemoveOK $remaining)
          (atp:resolution:multiset-equal $tail $remaining))
        ($_ False)))))

(= (atp:resolution:negative-premises ()) ())

(= (atp:resolution:negative-premises $premises)
   (let ($head $tail) (decons-atom $premises)
     (let $rest
          (eval (atp:resolution:negative-premises $tail))
       (cons-atom (ATP.Neg $head) $rest))))

; Canonical Horn-clause view used by the proof-search representation ablation.
(= (atp:resolution:horn-clause $premises $conclusion)
   (let $negative-premises
        (eval (atp:resolution:negative-premises $premises))
     (cons-atom (ATP.Pos $conclusion) $negative-premises)))

(= (atp:resolution:check-step
     $left $right $substitution $left-pivot $right-pivot $expected)
   (if (atp:resolution:clause-valid $left)
     (if (atp:resolution:clause-valid $right)
       (if (atp:resolution:clause-valid $expected)
         (if (atp:resolution:literal-valid $left-pivot)
           (if (atp:resolution:literal-valid $right-pivot)
             (if (atp:resolution:substitution-valid $substitution)
               (let $instantiated-left
                    (atp:resolution:substitute-clause
                      $substitution $left)
                 (let $instantiated-right
                      (atp:resolution:substitute-clause
                        $substitution $right)
                   (let (ATP.Subst $bindings) $substitution
                     (let $instantiated-left-pivot
                          (atp:resolution:substitute-term
                            $bindings $left-pivot)
                       (let $instantiated-right-pivot
                            (atp:resolution:substitute-term
                              $bindings $right-pivot)
                         (if (atp:resolution:complementary
                               $instantiated-left-pivot
                               $instantiated-right-pivot)
                           (case (atp:resolution:remove-one
                                   $instantiated-left-pivot
                                   $instantiated-left)
                             (((ATP.RemoveOK $left-rest)
                                (case (atp:resolution:remove-one
                                        $instantiated-right-pivot
                                        $instantiated-right)
                                  (((ATP.RemoveOK $right-rest)
                                     (atp:resolution:multiset-equal
                                       (atp:resolution:append
                                         $left-rest $right-rest)
                                       $expected))
                                   ($_ False))))
                              ($_ False)))
                           False))))))
               False)
             False)
           False)
         False)
       False)
     False))
