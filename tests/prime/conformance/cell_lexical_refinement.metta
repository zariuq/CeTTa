; A Need cell evaluates in its lexical environment.  A later compatible
; binding for one of its captured variables is visible, while unrelated
; matcher-local bindings from the demanding continuation do not enter the
; cell.  The full caller world is composed back only when the value is
; published.

(= (cell-refine:valid-item $item)
   (case $item
     (((Cell.Pos $atom) True)
      ((Cell.Neg $atom) True)
      ($_ False))))

(= (cell-refine:valid-list ()) True)

(= (cell-refine:valid-list $items)
   (let ($head $tail) (decons-atom $items)
     (if (cell-refine:valid-item $head)
         (cell-refine:valid-list $tail)
         False)))

(= (cell-refine:remove-alpha $item ())
   Cell.Missing)

(= (cell-refine:remove-alpha $item $items)
   (let ($head $tail) (decons-atom $items)
     (if (=alpha $item $head)
         (Cell.Rest $tail)
         (case (cell-refine:remove-alpha $item $tail)
           (((Cell.Rest $rest)
              (Cell.Rest (cons-atom $head $rest)))
            ($_ Cell.Missing))))))

(= (cell-refine:append () $right) $right)

(= (cell-refine:append $left $right)
   (let ($head $tail) (decons-atom $left)
     (let $rest
          (eval (cell-refine:append $tail $right))
       (cons-atom $head $rest))))

(= (cell-refine:multiset-alpha-equal () $right)
   (== $right ()))

(= (cell-refine:multiset-alpha-equal $left $right)
   (let ($head $tail) (decons-atom $left)
     (case (cell-refine:remove-alpha $head $right)
       (((Cell.Rest $remaining)
          (cell-refine:multiset-alpha-equal $tail $remaining))
        ($_ False)))))

(= (cell-refine:check-after-validation
     $left $right $left-pivot $right-pivot $expected)
   (if (cell-refine:valid-list $left)
       (case ($left-pivot $right-pivot)
         ((((Cell.Pos $left-atom) (Cell.Neg $right-atom))
            (unify $left-atom $right-atom
              (case (cell-refine:remove-alpha $left-pivot $left)
                (((Cell.Rest $left-rest)
                   (case (cell-refine:remove-alpha $right-pivot $right)
                     (((Cell.Rest $right-rest)
                        (cell-refine:multiset-alpha-equal
                          (cell-refine:append $left-rest $right-rest)
                          $expected))
                      ($_ False))))
                 ($_ False)))
              False))
          ($_ False)))
       False))

; Positive: validation forces and caches a clause containing $x before the
; later unification refines $x to a.  The cached residual must be transported
; into that descendant world.
!(cell-refine:check-after-validation
   ((Cell.Pos (p $x)) (Cell.Pos (q $x)))
   ((Cell.Neg (p a)))
   (Cell.Pos (p $x)) (Cell.Neg (p a))
   ((Cell.Pos (q a))))

; Negative: lexical projection must not turn an incompatible claimed result
; into a success.
!(cell-refine:check-after-validation
   ((Cell.Pos (p $x)) (Cell.Pos (q $x)))
   ((Cell.Neg (p a)))
   (Cell.Pos (p $x)) (Cell.Neg (p a))
   ((Cell.Pos forged)))
