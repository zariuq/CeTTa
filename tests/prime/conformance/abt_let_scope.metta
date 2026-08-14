; Prime lowers each let pattern to stable positional slots and its body to
; unary ABT scopes.  Across sequential relational forms, reusing one syntax
; variable refines the established binding instead of introducing a shadow.

(= (prime-let-duplicate) (pair a b))
(= (prime-let-duplicate) (pair a b))

!(assertEqual
   (let $x a (got $x))
   (got a))
!(assertEqualToResult
   (let (pair $x $y) (prime-let-duplicate) (got $x $y))
   ((got a b) (got a b)))

; Sequential reuse is single-assignment refinement: disagreement removes the
; branch, while agreement preserves the established value.
!(assertEqualToResult
   (let $x a (let $x b (pair $x)))
   ())
!(assertEqual
   (let $x a (let $x $x (pair $x)))
   (pair a))

; Slot order is structural and duplicate pattern occurrences retain ordinary
; unification semantics.
!(assertEqual
   (let (pair $x $y) (pair a b) (pair $x $y))
   (pair a b))
!(assertEqual
   (let (pair $x $x) (pair a a) (same $x))
   (same a))
; A source component may itself remain a metavariable.  The lexical pattern
; slot must point to that value, rather than orienting unification backwards.
!(assertEqual
   (let (pair $x $y) (pair $source b) (pair $x $y))
   (pair $source b))
!(assertEqualToResult
   (let (pair $x $x) (pair a b) (same $x))
   ())

; A later structured pattern refines its reused component and may introduce a
; genuinely new component.
!(assertEqualToResult
   (let (pair $x $y) (pair a b)
     (let (pair $x $z) (pair c d) (triple $x $y $z)))
   ())

; let* is sequential sugar over the same canonical let representation.
!(assertEqual
   (let* (($x a) ($x a)) (pair $x))
   (pair a))
!(assertEqualToResult
   (let* (((pair $x $y) (pair a b))
          ((pair $x $z) (pair c d)))
     (triple $x $y $z))
   ())

; Destructuring an unconstrained caller variable introduces lexical
; components whose later refinements must remain connected to that caller.
!(assertEqual
   (let (pair $x $y) $source
     (let $x a
       (let $y b
         (got $source))))
   (got (pair a b)))

; A caller-owned variable reused in a later pattern is a refinement, not a
; fresh shadow binder.  An incompatible reconstruction removes the branch.
!(assertEqualToResult
   (let (pair $x $y) $source
     (let $x a
       (let $source (pair b c)
         impossible)))
   ())

; The canonical constructor is ordinary Prime data.  Two slots require two
; explicit unary scopes, opened in left-to-right pattern order.
!(assertEqual
   (ABTLetV1
     (pair (ABTPatternVarV1 0) (ABTPatternVarV1 1))
     (pair a b)
     (ABTLetScopeV1
       (ABTLetScopeV1 (pair (idx 1) (idx 0)))))
   (pair a b))

; Sparse slots and a scope count that disagrees with the pattern fail closed.
!(assertEqualToResult
   (ABTLetV1
     (ABTPatternVarV1 1)
     a
     (ABTLetScopeV1 (idx 0)))
   ((Error
      (ABTLetV1
        (ABTPatternVarV1 1)
        a
        (ABTLetScopeV1 (idx 0)))
      ABTLetScopeError)))
!(assertEqualToResult
   (ABTLetV1
     (ABTPatternVarV1 0)
     a
     (ABTLetScopeV1 (ABTLetScopeV1 (idx 0))))
   ((Error
      (ABTLetV1
        (ABTPatternVarV1 0)
        a
        (ABTLetScopeV1 (ABTLetScopeV1 (idx 0))))
      ABTLetScopeError)))

!(println! (PrimeABTLetSummary 16 16 0))
