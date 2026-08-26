; Prime ordinary equations and canonical ABT application share one
; branch-local call-by-need contract.  This fixture is intentionally direct:
; its output bag and multiplicity are the normative observation.

(= (prime-need:omega) (prime-need:omega))

; Canonical ABT application.
!(App (Lam T (idx 0)) (superpose (A B)))
!(App (Lam T (Pair (idx 0) (idx 0))) (superpose (A B)))
!(App (Lam T kept-empty) (empty))
!(App (Lam T kept-fault) (== 1 "bad"))
!(App (Lam T kept-divergence) (prime-need:omega))

; Ordinary equation application.
(= (prime-need:dup $x) (Pair $x $x))
(= (prime-need:ignore-empty $x) kept-empty)
(= (prime-need:ignore-fault $x) kept-fault)
(= (prime-need:ignore-divergence $x) kept-divergence)
!(prime-need:dup (superpose (A B)))
!(prime-need:ignore-empty (empty))
!(prime-need:ignore-fault (== 1 "bad"))
!(prime-need:ignore-divergence (prime-need:omega))

; A structured higher-order callable head is usable without becoming a
; wildcard equation for unrelated named calls.
(= ((prime-need:offset $amount) $value) (+ $amount $value))
(= (prime-need:named $value) (Named $value))
!((prime-need:offset 5) 7)
!(prime-need:named intact)

; Needed matching is per equation.  A stricter alternative may demand an
; argument without making a wildcard alternative eager, and unrelated
; positions remain suspended.
(= (prime-need:classify A $unused) alpha)
(= (prime-need:classify $x $unused) other)
!(prime-need:classify
   (superpose (A B))
   (== 1 "unused"))

; Nonlinear patterns compare values rather than private thunk identities.
(= (prime-need:same $x $x) same)
(= (prime-need:same $x $y) any)
!(prime-need:same (superpose (A B)) A)

; Nested structure demands only the path inspected by the pattern.  The B
; alternative remains an inert residual and its unrelated empty argument is
; never forced.
(= (prime-need:box (Box A) $unused) boxed-a)
!(prime-need:box (Box (superpose (A B))) (empty))

; A code boundary reifies the suspended source; it never exposes a private
; heap reference and never evaluates the quoted term.
(= (prime-need:quote-arg $x) (quote $x))
!(prime-need:quote-arg (+ 1 2))

; Case selection is internal control, not an observation boundary.  The
; scrutinee carries a delayed argument through a constructor; unboxing it
; must retain the matching branch's heap until that argument is demanded.
(= (prime-need:case-unbox $value)
   (case $value
     (((Box $inner) $inner))))
!(prime-need:case-unbox
   (App (Lam T (Box (idx 0))) (superpose (A B))))

; A closure can carry a branch-local thunk through another canonical App.
!(App
   (App (Lam T (Lam U (idx 1))) (superpose (A B)))
   ignored)

; One demanded thunk occurrence may be observed twice, but its effect runs
; once in that branch.  The branch receipt is not implicitly committed to the
; ambient state cell.
!(bind! &prime-need:canonical-counter (new-state 0))
(= (prime-need:canonical-bump)
   (let $old (get-state &prime-need:canonical-counter)
     (let $_
       (change-state! &prime-need:canonical-counter (+ $old 1))
       $old)))
!(App
   (Lam T (Pair (idx 0) (idx 0)))
   (prime-need:canonical-bump))
!(get-state &prime-need:canonical-counter)

!(bind! &prime-need:ordinary-counter (new-state 0))
(= (prime-need:ordinary-bump)
   (let $old (get-state &prime-need:ordinary-counter)
     (let $_
       (change-state! &prime-need:ordinary-counter (+ $old 1))
       $old)))
(= (prime-need:effect-pair $x) (Pair $x $x))
!(prime-need:effect-pair (prime-need:ordinary-bump))
!(get-state &prime-need:ordinary-counter)

; Needed pattern matching preserves unknown alternatives instead of treating a
; known head as a total function.
(= (prime-need:partial A) partial-a)
!(prime-need:partial (superpose (A B)))

; A wildcard can return without demanding its argument while a stricter rule
; independently contributes the branch on which its pattern matches.
(= (prime-need:overlap $x) overlap-wildcard)
(= (prime-need:overlap A) overlap-a)
!(prime-need:overlap (superpose (A B)))

; Candidate rules share one call thunk.  Trying two strict patterns may split
; the thunk's result, but it must execute the producer effect only once.  The
; dedicated receipt-occurrence gate observes that event directly.
!(bind! &prime-need:decision-counter (new-state 0))
(= (prime-need:decision-bump)
   (let $old (get-state &prime-need:decision-counter)
     (let $_
       (change-state! &prime-need:decision-counter (+ $old 1))
       (superpose (A B)))))
(= (prime-need:decision A) decision-a)
(= (prime-need:decision B) decision-b)
!(prime-need:decision (prime-need:decision-bump))
!(get-state &prime-need:decision-counter)

; A registry token is data for wildcard arguments, but a constructor pattern
; demands the value named by that token.
!(bind! &prime-need:pattern-token (PrimeNeed.Policy selected))
(= (prime-need:inspect-policy (PrimeNeed.Policy $value)) $value)
!(prime-need:inspect-policy &prime-need:pattern-token)

; `function` delimits the logical variables introduced by its inner chain,
; while the Need heap remains available to returned closures.  Reusing the
; same source variable outside the nested function must therefore not observe
; the inner binding to 3.
!(function
   (chain (function (chain 3 $x (return inner))) $_inner
     (chain (eval (noreduce-eq $x 3)) $binding-leaked
       (return $binding-leaked))))

; Checking-first authority does not force a valid argument, but it rejects a
; raw argument that violates an admitted function contract even when unused.
(: prime-need:typed-ignore (-> Number Number))
(= (prime-need:typed-ignore $x) 7)

; Canonical application follows Prime's unknown-preservation boundary.  An
; untyped head stays residual, while a known Number in function position is a
; checked contradiction.
!(App Vec (idx 0))

; Recursive forwarding keeps the suspension's source type available to the
; checker without forcing it.  The numeric test demands the delayed counter
; only when the next equation body needs it.
(= (prime-need:countdown-size $value $count)
   (if (== $count 0)
       (size-atom $value)
       (prime-need:countdown-size $value (- $count 1))))
!(prime-need:countdown-size (a b) 2)

!(prime-need:typed-ignore "bad")
