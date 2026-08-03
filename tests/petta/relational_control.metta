; PeTTa evaluates both relational-control operands, unifies their results,
; and enters the continuation only for consistent branches.
!(let (+ 1 2) 3 let-positive)
!(let (+ 1 2) 4 let-negative-must-not-appear)
!(chain (+ 1 2) 3 chain-positive)
!(chain (+ 1 2) 4 chain-negative-must-not-appear)

; Surface variables and exact bag multiplicity survive the relation.
!(let $x 4 (+ $x 1))
!(let (superpose (1 1 2)) (superpose (1 2)) hit)

; Shared HE stdlib equations retain value-binding control internally.
!(map-atom (1 2 3) (+ 1))

; Relational output constraints support PeTTa's parametric type idiom.
(: apply (-> (-> $tx $ty) $tx $ty))
!(let (get-type apply) (-> (-> Bool Bool) Bool $result) $result)

; Alpha-membership follows PeTTa's relational member predicate: a free query
; variable matches only a variable item, while compound variables may unify.
!(is-alpha-member x ())
!(is-alpha-member $x (a b))
!(is-alpha-member $x (a $y))
!(is-alpha-member (f $x $x) ((f $a $b)))

; `cons` remains relational in pattern positions, including nested spines.
!(let (cons $head $tail) (1 2 3) ($head $tail))
!(let (cons $head $tail) () cons-empty-must-not-appear)
!(let (cons $first (cons $second $rest))
      (1 2 3)
      ($first $second $rest))
!(case (4 5) (((cons $head $tail) ($head $tail))))
!(case () (((cons $head $tail) cons-case-empty-must-not-appear)))

; A `cons` on the value side remains a constructor when the relation's other
; side is free, including one output per nondeterministic tail.  Once the
; other side is concrete, the same predicate is relational in reverse.
!(let $whole
      (cons head (superpose ((1 a) (2 b))))
      $whole)
!(let (1 2 3) (cons $head $tail) ($head $tail))

; A nested effect retains PeTTa's `true` success value.  A failed relation
; cannot execute its continuation.
!(let $x 9 (add-atom &self (relational-effect $x)))
!(let 0 1 (add-atom &self relational-effect-must-not-appear))
!(test (collapse (match &self (relational-effect $x) (relational-effect $x)))
       ((relational-effect 9)))
!(let $x 9 (remove-atom &self (relational-effect $x)))
!(test (collapse (match &self (relational-effect $x) (relational-effect $x)))
       ())

; Control words remain ordinary data under quotation.
!(quote (chain a b c))
