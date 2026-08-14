; A repeated head variable accumulates distinct candidate types.  The clause
; remains admissible because its body does not consume that ambiguous value.
(: repeated-head-narrow (-> (| Number String) Number Bool))
(= (repeated-head-narrow $value $value) True)

!(test (repeated-head-narrow 5 5) True)
