; Negative mirror: recognizing symbolic-headed list data must still check its
; elements against the declared element type.

(: keep-list (-> (List Number) (List Number)))
(= (keep-list $items) $items)

!(keep-list (read binary))
