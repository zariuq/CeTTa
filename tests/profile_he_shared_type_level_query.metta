; Ordinary HE performs dependent substitution and leaves user computation
; residual. he-prime normalizes that same shared inferred type.
(: SharedTagged (-> Type Type))
(: shared-decorate-type (-> Type Type))
(= (shared-decorate-type $x) (SharedTagged $x))
(type-level-function shared-decorate-type)
(: shared-lift (-> $t (shared-decorate-type $t)))
(: SharedQueryA Type)
(: shared-query-a SharedQueryA)
!(get-type (shared-lift shared-query-a))
