; An untyped recursive callable has no static output proof.  Default mode may
; retain the declared result contract as a runtime residual; strict mode must
; reject the same unresolved equation instead of treating it as proved.

(: guarded (-> Number))
(= (guarded) (opaque-loop))

(= (opaque-loop) (opaque-loop))
