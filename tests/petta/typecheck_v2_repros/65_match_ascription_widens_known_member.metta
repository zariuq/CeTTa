; A typed match pattern adds a union as a second candidate for an already-known
; Number parameter.  Candidate evidence is variant-based, so overlap does not
; collapse the two candidates before the body consumes the value.

(: widen-in-match (-> Number Number))
(= (widen-in-match $value)
   (match &probe-space (: $value (| String Number)) $value))
