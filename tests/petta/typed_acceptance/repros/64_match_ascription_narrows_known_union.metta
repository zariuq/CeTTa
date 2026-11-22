; A typed match pattern adds Number as a second candidate for an already-known
; union-valued clause parameter.  The body cannot consume that non-singleton
; candidate set as a statically established Number.

(: narrow-in-match (-> (| Number String) Number))
(= (narrow-in-match $value)
   (match &probe-space (: $value Number) $value))
