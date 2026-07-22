; Quick tour of Prime contexts.  The detailed executable chapters live under
; examples/prime/context_tutorial/.  This file is a compact overview, not the
; complete tutorial.
;
; Prime contexts are persistent lazy environments.  Their own API is immutable
; and their identity does not require an &reference.  A whole context may still
; be stored in a registry or space and passed to other computations.

; 1. Lookup is total.  Missing information remains matchable data.
!(ctx:get ctx:empty topic)

; 2. Extension returns a fresh sibling and leaves the parent unchanged.
!(let $base (ctx:bind ctx:empty policy baseline)
   (let $left (ctx:bind $base policy cautious)
     (let $right (ctx:bind $base policy bold)
       (Triple
         (ctx:get $base policy)
         (ctx:get $left policy)
         (ctx:get $right policy)))))

; 3. One binding means one branch-local unknown.  Every consumer agrees on
; the choice made for that binding.
!(let $shared (ctx:bind ctx:empty weather (superpose (sun rain)))
   (Pair (ctx:get $shared weather) (ctx:get $shared weather)))

; Two separately allocated bindings with identical origins are independent.
!(let $first (ctx:bind ctx:empty weather (superpose (sun rain)))
   (let $second (ctx:bind ctx:empty weather (superpose (sun rain)))
     (Pair (ctx:get $first weather) (ctx:get $second weather))))

; 4. Counterfactual policies can consume the same uncertain premise without
; accidentally sampling different worlds inside one comparison.
!(let $world (ctx:bind ctx:empty terrain (superpose (clear muddy)))
   (Pair
     (plan cautious (ctx:get $world terrain))
     (plan bold (ctx:get $world terrain))))

; 5. A context can serve as an object-level proof environment.  Structured
; names make assumptions about syntax addressable; the outer context remains
; free of the hypothetical assumption.
!(let $gamma (ctx:bind ctx:empty @(goal self-implies-self) assumed)
   (Pair
     (ctx:get $gamma @(goal self-implies-self))
     (ctx:get ctx:empty @(goal self-implies-self))))

; 6. A lazy subgoal result can be shared as a memoized binding.  This is a
; building block for tabling, not a complete table or waiter protocol.
(= (tutorial:search $goal)
   (superpose ((proof route-a $goal) (proof route-b $goal))))
!(let $memo
     (ctx:bind ctx:empty @(subgoal theorem) (tutorial:search theorem))
   (Pair
     (ctx:get $memo @(subgoal theorem))
     (ctx:get $memo @(subgoal theorem))))

; 7. Bounded inspection exposes quoted origins without running them.
!(bind! &tutorial:counter (new-state 0))
(= (tutorial:bump)
   (let $old (get-state &tutorial:counter)
     (let $_ (change-state! &tutorial:counter (+ $old 1)) computed)))
!(let $agenda (ctx:bind ctx:empty pending (tutorial:bump))
   (Pair (ctx:view $agenda 1) (get-state &tutorial:counter)))

; The same transparency means a context is not a confidentiality vault:
; ctx:view intentionally reveals the immutable origin as quoted data.
!(ctx:view (ctx:bind ctx:empty pending (private-plan alpha)) 1)

; 8. Explicit delay stays explicit.  The suspension closes over the context
; value present at allocation, even after a newer sibling shadows the key.
!(let $base (ctx:bind ctx:empty model old)
   (let $pending (delay (ctx:get $base model))
     (let $newer (ctx:bind $base model new)
       (Pair (ctx:get $newer model) (force $pending)))))
