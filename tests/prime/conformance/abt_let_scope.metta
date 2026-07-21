; Prime lowers let patterns to stable positional slots and the body to repeated
; unary ABT scopes.  Matching remains a metavariable operation; lexical scope
; does not depend on a surface variable's globally interned VarId.

(= (prime-let-duplicate) (pair a b))
(= (prime-let-duplicate) (pair a b))

!(assertEqual
   (let $x a (got $x))
   (got a))
!(assertEqualToResult
   (let (pair $x $y) (prime-let-duplicate) (got $x $y))
   ((got a b) (got a b)))

; The inner pattern shadows the outer one, while its source still sees the
; outer binding.
!(assertEqual
   (let $x a (let $x b (pair $x)))
   (pair b))
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

; Shadowing one component does not capture an unshadowed outer component.
!(assertEqual
   (let (pair $x $y) (pair a b)
     (let (pair $x $z) (pair c d) (triple $x $y $z)))
   (triple c b d))

; let* is sequential sugar over the same canonical let representation.
!(assertEqual
   (let* (($x a) ($x b)) (pair $x))
   (pair b))
!(assertEqual
   (let* (((pair $x $y) (pair a b))
          ((pair $x $z) (pair c d)))
     (triple $x $y $z))
   (triple c b d))

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

!(println! (PrimeABTLetSummary 14 14 0))
