; HE checks a constructor application against the constructor's declared
; type.  A type row on the application itself is a typing HE counts, but it
; does not make the build well typed when the constructor rejects the
; arguments: the build is a type error, as HE answers it.
(: OpenShownBox (-> $t $t OpenShownRange))
(: (OpenShownBox 1 "wrong-field") AlternateRange)
(: shown-open-body (-> OpenShownRange))
(= (shown-open-body) (OpenShownBox 1 "wrong-field"))
; Positive controls: a row the constructor's type agrees with, and a
; constructor with no row at all.
(: Pair (-> Number Number PairT))
(: (Pair 1 2) PairT)
(: pair-body (-> PairT))
(= (pair-body) (Pair 1 2))
(: Box2 (-> Number Number BoxT))
(: box-body (-> BoxT))
(= (box-body) (Box2 3 4))
!(collapse (shown-open-body))
!(get-type (OpenShownBox 1 "wrong-field"))
!(collapse (pair-body))
!(collapse (box-body))
