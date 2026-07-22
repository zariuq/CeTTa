; One program, five language/profile observations.
;
; The shared laws are as important as the differences:
;   - duplicated nondeterministic arguments use call-time choice;
;   - a demanded effectful argument is evaluated at most once;
;   - Prime additionally does not demand an unused argument.

(= (strategy:ignore $unused) kept)
(= (strategy:duplicate $value) (Pair $value $value))
; Empty and faulting computations are never demanded by Prime.  Divergence is
; isolated in the companion fixture so an eager lane cannot prevent the later
; examples from running.
!(strategy:ignore (empty))

; Call-time choice correlates repeated uses of one argument.
!(strategy:duplicate (superpose (A B)))

; An unused effect distinguishes eager application from call-by-need.
!(bind! &strategy:unused-counter (new-state 0))
(= (strategy:bump-unused)
   (let $old (get-state &strategy:unused-counter)
     (let $_ (change-state! &strategy:unused-counter (+ $old 1))
       $old)))
!(strategy:ignore (strategy:bump-unused))
!(get-state &strategy:unused-counter)

; A demanded argument shared by two uses performs its effect once in the
; Prime branch receipt.  Prime does not implicitly commit that branch-local
; write to the ambient state; eager HE profiles mutate their ambient cell.
!(bind! &strategy:shared-counter (new-state 0))
(= (strategy:bump-shared)
   (let $old (get-state &strategy:shared-counter)
     (let $_ (change-state! &strategy:shared-counter (+ $old 1))
       $old)))
!(strategy:duplicate (strategy:bump-shared))
!(get-state &strategy:shared-counter)

; Prime exposes deliberate per-force resampling as an explicit control form.
; HE profiles leave unknown forms uninterpreted rather than assigning them
; Prime semantics.
!(let $resampler (resample (superpose (A B)))
   (Pair (force $resampler) (force $resampler)))

; A demanded fault is evaluated once and its receipt is memoized.  Catching
; the same promise twice observes one branch-local effect, not two evaluations,
; and does not implicitly commit that effect to the ambient state.
!(bind! &strategy:fault-counter (new-state 0))
(= (strategy:bump-fault)
   (let $old (get-state &strategy:fault-counter)
     (let $_ (change-state! &strategy:fault-counter (+ $old 1))
       (== 1 "bad"))))
!(let $promise (delay (strategy:bump-fault))
   (Pair (if-error (force $promise) caught no-error)
         (if-error (force $promise) caught no-error)))
!(get-state &strategy:fault-counter)

; Keep the unused eager fault last: strict profiles report it, whereas Prime
; returns `kept` and has already produced every preceding observation.
!(strategy:ignore (== 1 "bad"))
