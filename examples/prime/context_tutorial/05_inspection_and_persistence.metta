; Chapter 5: bounded inspection and persistence
;
; `ctx:view` reveals at most N newest entries.  It exposes immutable origins as
; quoted data and does not force them.  A view is for reflection and debugging;
; it is not access to the hidden cells themselves.

!(let $context
     (ctx:bind
       (ctx:bind ctx:empty first 1)
       second (+ 1 1))
   (ctx:view $context 2))

; Inspection does not run an effectful origin.
!(bind! &ctx-tutorial:inspection-counter (new-state 0))
(= (ctx-tutorial:inspection-bump)
   (let $old (get-state &ctx-tutorial:inspection-counter)
     (let $_
         (change-state! &ctx-tutorial:inspection-counter (+ $old 1))
       computed)))
!(let $agenda
     (ctx:bind ctx:empty pending (ctx-tutorial:inspection-bump))
   (Pair
     (ctx:view $agenda 1)
     (get-state &ctx-tutorial:inspection-counter)))

; A context is not a confidentiality boundary.  A holder with view access can
; see a quoted private origin even though viewing cannot execute it.
!(ctx:view
   (ctx:bind ctx:empty pending (private-plan alpha))
   1)

; Persistence preserves the hidden cell graph.  Storing and reloading one
; context does not turn two reads into independent samples.
!(bind! &ctx-tutorial:store (new-space))
!(let $context
     (ctx:bind ctx:empty choice (superpose (A B)))
   (add-atom &ctx-tutorial:store (saved-context $context)))
!(match &ctx-tutorial:store (saved-context $restored)
   (Pair
     (ctx:get $restored choice)
     (ctx:get $restored choice)))
