; Mirrored unused-candidate case: a known Number and a disjoint union remain
; separate candidates, but do not reject a body that never consumes the value.

(: disjoint-member-in-match (-> Number Bool))
(= (disjoint-member-in-match $value)
   (match &probe-space (: $value (| String Bool)) True))
