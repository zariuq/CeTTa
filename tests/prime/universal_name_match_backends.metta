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

; A query variable and a differently named variable stored in the space are
; genuinely related by a binding; projection must not relabel the stored
; variable as the query variable before exact removal.
!(bind! &universal-native-contextual (new-space))
!(space-set-match-backend! &universal-native-contextual native)
!(add-atom &universal-native-contextual
    (schema $@(native-stored) (wrap $@(native-stored))))
!(match &universal-native-contextual
    (schema $@(native-query) $@(native-body))
    (remove-atom &universal-native-contextual
                 (schema $@(native-query) $@(native-body))))
!(assertEqual (count-atoms &universal-native-contextual) 0)

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

!(bind! &universal-candidate-contextual (new-space))
!(space-set-match-backend! &universal-candidate-contextual native-candidate-exact)
!(add-atom &universal-candidate-contextual
    (schema $@(candidate-stored) (wrap $@(candidate-stored))))
!(match &universal-candidate-contextual
    (schema $@(candidate-query) $@(candidate-body))
    (remove-atom &universal-candidate-contextual
                 (schema $@(candidate-query) $@(candidate-body))))
!(assertEqual (count-atoms &universal-candidate-contextual) 0)

!(println! (PrimeUniversalNameMatchBackendSummary 6 6 0))
