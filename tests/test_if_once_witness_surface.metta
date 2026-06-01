; Two-argument if is the PeTTa profile spelling for "then or Empty".
; The True branch must preserve bindings produced by once.

(= (myf (a b)) True)
(= (myf (b a)) True)

!(assertEqual
  (if (once (myf $M)) $M)
  (a b))

!(assertEqual
  (if False ok)
  Empty)
