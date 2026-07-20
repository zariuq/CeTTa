; Structural matcher identities must remain distinct in both native matcher lanes.
; The repeated-name cases are negative controls: one structural variable used
; twice imposes equality and therefore cannot match (edge A B).

!(bind! &universal-native (new-space))
!(space-set-match-backend! &universal-native native)
!(add-atom &universal-native (edge A B))
!(assertEqual
    (collapse
      (match &universal-native
             (edge $@(native-left) $@(native-right))
             (Pair $@(native-left) $@(native-right))))
    ((Pair A B)))
!(assertEqual
    (collapse
      (match &universal-native
             (edge $@(native-same) $@(native-same))
             matched))
    ())

!(bind! &universal-candidate (new-space))
!(space-set-match-backend! &universal-candidate native-candidate-exact)
!(add-atom &universal-candidate (edge A B))
!(assertEqual
    (collapse
      (match &universal-candidate
             (edge $@(candidate-left) $@(candidate-right))
             (Pair $@(candidate-left) $@(candidate-right))))
    ((Pair A B)))
!(assertEqual
    (collapse
      (match &universal-candidate
             (edge $@(candidate-same) $@(candidate-same))
             matched))
    ())

!(println! (PrimeUniversalNameMatchBackendSummary 4 4 0))
