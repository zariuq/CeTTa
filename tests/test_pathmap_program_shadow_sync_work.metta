; The backend-primary program space is projected at most once while a result
; stream repeatedly consults derived evaluator indexes.
(shadow-left n0 k0)
(shadow-right k0 v0)
(shadow-left n1 k1)
(shadow-right k1 v1)
(shadow-left n2 k2)
(shadow-right k2 v2)
(shadow-left n3 k3)
(shadow-right k3 v3)
(shadow-left n4 k4)
(shadow-right k4 v4)
(shadow-left n5 k5)
(shadow-right k5 v5)
(shadow-left n6 k6)
(shadow-right k6 v6)
(shadow-left n7 k7)
(shadow-right k7 v7)
(shadow-left n8 k8)
(shadow-right k8 v8)
(shadow-left n9 k9)
(shadow-right k9 v9)
(shadow-left n10 k10)
(shadow-right k10 v10)
(shadow-left n11 k11)
(shadow-right k11 v11)
(shadow-left n12 k12)
(shadow-right k12 v12)
(shadow-left n13 k13)
(shadow-right k13 v13)
(shadow-left n14 k14)
(shadow-right k14 v14)
(shadow-left n15 k15)
(shadow-right k15 v15)

!(assertEqualToResult
  (let $rows
    (collapse
      (match &self (shadow-left $x $k)
        (match &self (shadow-right $k $y) ($x $y))))
    (size-atom $rows))
  (16))

; Mutating the backend-primary program space invalidates the derived shadow.
; The next evaluator lookup must refresh once and observe the new equation.
!(add-atom &self (= (shadow-f token) changed))
!(assertEqualToResult (shadow-f token) (changed))

; Removal invalidates it again; the unknown application becomes inert.
!(remove-atom &self (= (shadow-f token) changed))
!(assertEqualToResult (shadow-f token) ((shadow-f token)))
