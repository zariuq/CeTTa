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

; Matching demands only the inspected path. At the answer boundary the B
; residual's ordinary argument is observed as a value, so empty contributes
; no answer. Explicit delay instead retains that argument as code.
(= (prime-need:box (Box A) $unused) boxed-a)
!(prime-need:box (Box (superpose (A B))) (empty))
!(prime-need:box (Box (superpose (A B))) (delay (empty)))

; A code boundary never exposes a private heap reference.  A quotation is
; sealed, so a quoted parameter stays the literal variable (observed by its
; metatype) and its suspended argument is never demanded, not even for its
; effect; lifting the parameter demands the argument and quotes its value.
; The quote-as-written profile reifies the suspended source instead
; (tests/prime/quote_profiles/need_quote_preservation.metta).
(= (prime-need:quote-arg $x) (quote $x))
!(case (prime-need:quote-arg (+ 1 2)) (((quote $v) (get-metatype $v))))
!(case (prime-need:quote-arg (println! must-not-run)) (((quote $v) (get-metatype $v))))
(= (prime-need:lift-arg $x) (lift $x))
!(prime-need:lift-arg (+ 1 2))

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
   (chain (function (chain 3 $x (return inner)){$x}) $_inner
     (chain (eval (noreduce-eq $x 3)) $binding-leaked
       (return $binding-leaked)){$binding-leaked}){$_inner})

; The body's `$r` is a name of the form that its let binds, so the environment
; the caller receives keeps its value.
(= (prime-need:private-bind) (unify $w private done no))
!(collapse-bind
   (function (let $r (prime-need:private-bind) (return $r)){$r}))

; A binding the body makes for its own work stays inside the function: here
; the body refines a fresh variable that a call created, which is none of the
; body's names, and the environment its caller receives holds none of it.
(= (prime-need:fresh-variable) $v)
(= (prime-need:private-bind-of $x) (unify $x private done no))
!(collapse-bind
   (function
     (chain (prime-need:fresh-variable) $c
       (chain (prime-need:private-bind-of $c) $d (return $d)){$d}){$c}))

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

; Authored lambda application shares each supplied argument once, as an
; ordinary equation and canonical App do. Equal producer outcomes retain
; their occurrences; independent arguments still make a Cartesian product.
!(sort-atom (collapse ((lam x (Pair x x)) (superpose (A B)))))
!(sort-atom (collapse ((lam $x (Pair $x $x)) (superpose (A B)))))
!(sort-atom (collapse ((lam (x : T) (Pair x x)) (superpose (A B)))))
!(sort-atom (collapse ((lam x (Pair x x)) (superpose (A A)))))
(= (prime-need:lambda-pair) (lam x (Pair x x)))
!(sort-atom (collapse ((prime-need:lambda-pair) (superpose (A B)))))
!(sort-atom (collapse
   (((lam x (lam y (Pair x x))) (superpose (A B))) ignored)))
!(sort-atom (collapse
   ((lam (x y) (Four x x y y)) (superpose (A B)) (superpose (C D)))))
!(sort-atom (collapse
   (Two ((lam x x) (superpose (A B)))
        ((lam x x) (superpose (A B))))))

; Unused lambda arguments remain unforced, including failure, contradiction,
; divergence and observable effects. A used failing argument has no answer.
!((lam x kept-empty) (empty))
!((lam x kept-fault) (== 1 "bad"))
!((lam x kept-divergence) (prime-need:omega))
!((lam x kept-effect) (println! must-not-run))
!(collapse ((lam x (Pair x x)) (empty)))

; Returning a partial application retains the captured cell's identity. Two
; later uses of the same closure share it; two independent calls do not.
!(sort-atom (collapse
   (let $f ((lam x (lam y (Pair x x))) (superpose (A B))) ($f ignored))))
(= (prime-need:partial-lambda $x) ((lam x (lam y (Pair x x))) $x))
!(sort-atom (collapse ((prime-need:partial-lambda (superpose (A B))) ignored)))
!(sort-atom (collapse
   (let $f ((lam x (lam y (Pair x x))) (superpose (A B)))
     (Two ($f left) ($f right)))))

; Effects count actual producer executions. Sharing applies through a
; returned closure; two independent argument occurrences still run twice.
!(collapse ((lam x (Pair x x))
   (let $_ (println! need-lambda-shared-once) ran)))
!(collapse
   (let $f ((lam x (lam y (Pair x x)))
            (let $_ (println! need-closure-shared-once) ran))
     (Two ($f left) ($f right))))
!(collapse
   (Two ((lam x x) (let $_ (println! need-independent-occurrence) ran))
        ((lam x x) (let $_ (println! need-independent-occurrence) ran))))

; Capture avoidance reads the raw argument even when runtime references use
; its cell. The returned lambda leaves this compound argument unforced.
!((lam x (lam y (Pair x y))) y)
!((lam x (lam y (Pair x y))) (Box y (superpose (A B))))
!(((lam x (lam x done)) (println! must-not-run)) ignored)
(= (prime-need:produce-callable)
   (let $_ (println! need-callable-produced-once) (lam n n)))
!((lam h (Pair (h A) (h B))) (prime-need:produce-callable))

; A bare-symbol argument can itself be source work. Its callable result is
; chosen once even when the parameter is used in several call-head positions.
(= (prime-need:callable-f $x) (F $x))
(= (prime-need:callable-g $x) (G $x))
(= prime-need:symbol-callable
   (superpose (prime-need:callable-f prime-need:callable-g)))
!(sort-atom (collapse
   ((lam f (Pair (f 1) (f 2))) prime-need:symbol-callable)))
(= prime-need:duplicate-callable
   (superpose (prime-need:callable-f prime-need:callable-f)))
!(collapse ((lam f (Pair (f 1) (f 2))) prime-need:duplicate-callable))

; Cheap exclusion must admit variables on either side before later fields
; are demanded. Exact matching retains caller scope and duplicate answers.
(= (prefix:B) B)
(= (prefix:accept (Pair A B)) accepted)
(= (prefix:accept-lambda (Wrap A (lam x x))) accepted-lambda)
!(prefix:accept (Pair A (prefix:B)))
!(prefix:accept (Pair $x (prefix:B)))
!(let $r (prefix:accept (Pair $x (prefix:B))) (Result $r $x))
!(prefix:accept $p)
!(prefix:accept (Pair Z (prefix:B)))
!(prefix:accept-lambda (Wrap $x (lam x x)))
!(unify (Pair $x B) (Pair A B) (Bound $x) mismatch)
!(prefix:accept (Pair $x B))
(= (prefix:A) A)
!(prefix:accept (Pair (prefix:A) $x))
(= (prefix:duplicate-B) (superpose (B B)))
!(collapse (prefix:accept (Pair $x (prefix:duplicate-B))))
!(prefix:accept (Pair Z (prefix:duplicate-B)))

; A wildcard below a constructor retains a returned closure's live world.
; Structural inspection reads a lambda as code and never runs its body.
(= (prime-need:closure-field (Payload $f)) $f)
!(sort-atom (collapse
   (let $f (prime-need:closure-field
      (Payload ((lam x (lam y (Pair x x))) (superpose (A B)))))
      (Two ($f left) ($f right)))))
(= (prime-need:inspect-partial (lam $v $body)) inspected)
!(prime-need:inspect-partial
   ((lam x (lam y x)) (println! must-not-run)))

; Inspecting an outer lambda must keep a nested wildcard closure's cells.
(= (prime-need:lambda-closure-field (lam $v (Box $f))) $f)
!(sort-atom (collapse
   (let $f (prime-need:lambda-closure-field
       ((lam x (lam y (Box (lam z (Pair x x))))) (superpose (A B))))
     (Two ($f left) ($f right)))))

; A producer can introduce a name absent from its invocation. Its selected
; closure keeps later refinements of that name through first force, cache hits
; and another lambda's argument, without capturing a same-spelled caller.
(= (prime-need:cache-id $x) $x)
(= (prime-need:cache-fresh)
   (let $f ((lam y (lam z (Pair y y))) (prime-need:cache-id $x))
     (unify $x A $f failed)))
(= (prime-need:cache-choice)
   (let $f ((lam y (lam z (Pair y y))) (prime-need:cache-id $x))
     (let $v (superpose (A B)) (unify $x $v $f failed))))
!(let $f (prime-need:cache-fresh) ($f ignored))
!(let $s (delay (prime-need:cache-fresh))
   (let $f (force $s) ($f ignored)))
!((lam f (f ignored)) (prime-need:cache-fresh))
!(let $x B
   (let $s (delay (prime-need:cache-fresh))
     (let $f (force $s) (Out $x ($f ignored)))))
!(let $s (delay (prime-need:cache-fresh))
   (let $f (force $s) (Two ($f left) ((force $s) right))))
!(sort-atom (collapse
   ((lam f (f ignored)) (prime-need:cache-choice))))

; A read-only relational episode crosses the same closure boundary.
(prime-need:cache-seed)
(= (prime-need:cache-query)
   (match &self (prime-need:cache-seed)
     (let $f ((lam y (lam z (Pair y y))) (prime-need:cache-id $x))
       (unify $x A $f failed))))
!(let $f (prime-need:cache-query) ($f ignored))
!((lam f (f ignored)) (prime-need:cache-query))
