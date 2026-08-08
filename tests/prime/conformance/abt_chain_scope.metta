; Prime's named chain surface lowers to the same canonical unary-binder form
; used by the neutral ABT waist.  The body is instantiated once per producer
; result; multiplicity and sequential refinement are both observable.

(= (prime-chain-duplicate) a)
(= (prime-chain-duplicate) a)

!(assertEqual
   (chain a $x (got $x))
   (got a))
!(assertEqualToResult
   (chain (prime-chain-duplicate) $x (got $x))
   ((got a) (got a)))

; Reusing the same relational name refines its established value.
!(assertEqualToResult
   (chain a $x (chain b $x (pair $x)))
   ())
; A distinct name can observe the established binding in its source.
!(assertEqual
   (chain a $x (chain $x $y (pair $x $y)))
   (pair a a))
; Reusing one spelling with a compatible source succeeds once.
!(assertEqual
   (chain a $x (chain $x $x (pair $x)))
   (pair a))

; Existing canonical binders in a body keep their indices while the named
; chain binder is closed and opened around them.
!(assertEqual
   (chain a $x (Lam Type (pair $x (idx 0))))
   (Lam Type (pair a (idx 0))))
!(assertEqual
   (chain a $x (pair $x (idx 0)))
   (pair a (idx 0)))

; The internal canonical constructor is ordinary homoiconic package data and
; may be open in an explicitly supplied enclosing context.  Closedness is an
; admission judgment, not an evaluator guess at depth zero.
!(assertEqual
   (ABTChainV1 a (pair (idx 0)))
   (pair a))
!(assertEqual
   (ABTChainV1 a (pair (idx 1)))
   (pair (idx 0)))
!(assertEqualToResult
   (ABTChainV1 a (pair (idx -1)))
   ((Error (ABTChainV1 a (pair (idx -1))) ABTChainOpenFailed)))

; Prime chain admits relational patterns.  A matching pattern contributes its
; bindings; a non-matching pattern contributes no branch.
!(assertEqual
   (chain (pair a b) (pair $x $y) (got $x $y))
   (got a b))
!(assertEqualToResult
   (chain a (pair $x $y) (got $x $y))
   ())

!(println! (PrimeABTChainSummary 12 12 0))
