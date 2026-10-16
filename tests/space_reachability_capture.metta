;; A captured context holds its space. capture evaluated in a space made by
;; new-space (here through metta) is a closure over that space, so named state
;; holding the closure reaches the space; once the state lets go of the
;; closure, the space is unreached. The runtime-stats! gauges are those of
;; tests/petta/space_reachability.metta.
(= (gauge $name) (match (runtime-stats!) (runtime-counter $name $n) $n))
(= (spaces) ((gauge space-tracked-count) (gauge space-unreachable-count)))
!(let $_ (spaces) settled)
!(bind! &keep (new-state (nothing)))
!(let $s (new-space) (change-state! &keep (metta capture %Undefined% $s)))
!(capture-reaches (spaces))
!(change-state! &keep (nothing))
!(capture-dropped (spaces))
