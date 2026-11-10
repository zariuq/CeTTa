; An application's types are the rows declared for it together with the
; type its head's function type gives.  A row on an arrow-headed application
; does not hide the arrow's own typing from an argument check.
(: OtherDemandPair (-> $t $t DemandRange))
(: (OtherDemandPair 1 2) OtherDemandRange)
(: use-demand (-> DemandRange DemandRange))
(= (use-demand $value) $value)
(: use-other (-> OtherDemandRange OtherDemandRange))
(= (use-other $value) $value)
(: use-third (-> ThirdRange ThirdRange))
(= (use-third $value) $value)
(: other-demand-result (-> DemandRange))
(= (other-demand-result) (OtherDemandPair 1 2))
; Positive: the arrow's type, the row's type, and through a typed producer.
!(collapse (use-demand (OtherDemandPair 1 2)))
!(collapse (use-other (OtherDemandPair 1 2)))
!(collapse (use-demand (other-demand-result)))
; The rows and the arrow's type, as get-type reports them.
!(get-type (OtherDemandPair 1 2))
; Negative: a parameter type neither gives; each type reports its error.
!(collapse (use-third (OtherDemandPair 1 2)))
; Negative: fields the arrow rejects keep only the row.
(: (OtherDemandPair 1 "two") OtherDemandRange)
!(collapse (use-other (OtherDemandPair 1 "two")))
