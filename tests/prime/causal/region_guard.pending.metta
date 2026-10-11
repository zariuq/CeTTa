; PENDING.  A native region guard on contexts.
;
; Authority is a declared class of contexts: a context restricted to a region
; binds only the region's keys.  The draft has no such guard; this file states
; the refusal it should give, and `make probe-prime-causal-region-guard`
; reports it pending until the guard lands.  The equation-level prototype in
; composition_regions.metta shows the intended answers on worlds.
;
; The proposed form: (ctx:restrict context region) is the context with the
; same bindings, whose further bindings are refused outside the region; a
; restriction of a restriction keeps the smaller region.  The guarantee is
; noninterference for the region
; (GSLT.Causality.ContextOperatorsControls.OneShot.masked_noninterference) and
; a smaller region separates less
; (GSLT.Causality.ContextBindings.OverrideAction.agree_of_region_le).

; Inside the region a binding is made.
!(ctx:get (ctx:bind (ctx:restrict (ctx:bind ctx:empty open False) (open)) open True) open)
; Outside the region it is refused.
!(case (ctx:bind (ctx:restrict (ctx:bind ctx:empty open False) (open)) guard True)
   (((Error $call $reason) (Refused $reason))
    ($other Bound)))
; Restricting again cannot widen the region.
!(case (ctx:bind
         (ctx:restrict (ctx:restrict (ctx:bind ctx:empty open False) (open)) (open guard))
         guard True)
   (((Error $call $reason) (Refused $reason))
    ($other Bound)))
