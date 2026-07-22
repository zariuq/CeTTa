; Characterize the open boundary between a private Need suspension and its
; public source-data view.  These probes are evidence for choosing a law;
; they do not make the current behavior normative.

; Top-level observation currently prints the source form.
!(delay (superpose (A B)))

; Does an internal structural observer see that same source form?
!(let $p (delay (superpose (A B)))
   (unify $p (delay $origin)
     (internal-view $origin)
     internal-opaque))

; Merely observing a suspension must never force its origin.
!(bind! &prime-need:observation-counter (new-state 0))
(= (prime-need:observation-bump)
   (let $old (get-state &prime-need:observation-counter)
     (let $_ (change-state! &prime-need:observation-counter (+ $old 1))
       $old)))
!(let $p (delay (prime-need:observation-bump))
   (unify $p (delay $origin)
     (effect-view $origin)
     effect-view-opaque))
!(get-state &prime-need:observation-counter)

; If a view exposes the immutable origin, delaying that origin again must
; allocate a fresh cell rather than aliasing the original suspension.
!(let $p (delay (superpose (A B)))
   (unify $p (delay $origin)
     (Pair (force $p) (force (delay $origin)))
     identity-opaque))

; A source-data view must not expose authority to force the original cell.
!(let $p (delay (superpose (A B)))
   (unify $p (delay $origin)
     (force $origin)
     capability-opaque))

; Reification recursively removes live capabilities while retaining shared
; source structure as data.
!(let $inner (delay (superpose (A B)))
   (delay (Pair $inner $inner)))

; A view, if selected, must expose the allocation-time closed origin.
!(let $captured Z
   (let $p (delay (Pair $captured $captured))
     (unify $p (delay $origin)
       (closed-view $origin)
       closed-opaque)))

; Persistent graph nodes preserve sharing internally.  Test both a direct
; source-shaped pattern and retrieval through a variable before observation.
!(bind! &prime-need:observation-space (new-space))
!(let $p (delay (superpose (A B)))
   (add-atom &prime-need:observation-space (Stored $p)))
!(collapse
   (match &prime-need:observation-space
          (Stored (delay $origin))
          (direct-stored-view $origin)))
!(collapse
   (match &prime-need:observation-space
          (Stored $saved)
          (unify $saved (delay $origin)
            (variable-stored-view $origin)
            variable-stored-opaque)))
