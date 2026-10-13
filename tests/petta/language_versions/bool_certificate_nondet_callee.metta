; either-flag yields bound Bools, but two of them: and/2 over its call
; answers twice, so a det caller is refuted.
(: either-flag (-[nondet]-> Bool))
(= (either-flag) (superpose (true false)))

(: both-flags (-[det]-> Bool))
(= (both-flags) (and (either-flag) true))
