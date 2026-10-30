; Ordinary in-memory PeTTa queries over the same records used by the DB.
!(import! &self "claims_data.metta")
!(import! &self "dependencies_data.metta")

; Equal payloads remain two evidence occurrences.
!(size-atom (collapse (match &self (KbFact $id evidence (judgement1 source1)) $id)))
; The copied record and the original share one declared source origin.
!(size-atom (collapse (match &self (KbFact $id source (origin1 $label)) $id)))
; An annotation names one occurrence, not an entire class of equal payloads.
!(match &self (KbFact annotation1 provenance ($copy $original $why)) ($copy $original))
; Two distinct dependency occurrences join through the same intermediate node.
!(size-atom (collapse (match &self
    (, (KbFact $first dependency (emit $middle))
       (KbFact $second dependency ($middle $target)))
    ($first $second $target))))
