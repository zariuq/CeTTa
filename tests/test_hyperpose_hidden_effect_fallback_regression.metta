; Hidden effects behind user-defined heads must force a safe sequential fallback.
; Otherwise the effect would run against a cloned worker space and vanish from
; the caller's space.

!(bind! &hyperpose-hidden-effects (new-space))
(= (hyperpose-mark-hidden $x)
   (add-atom &hyperpose-hidden-effects (seen $x)))

!(pragma! hyperpose-threads 2)
!(collapse
  (hyperpose
    ((hyperpose-mark-hidden hidden-a)
     (hyperpose-mark-hidden hidden-b))))
!(assertEqual
  (collapse (match &hyperpose-hidden-effects (seen $x) $x))
  (hidden-a hidden-b))
