; Parenthesized PeTTa syntax is also list data.  With a List contract and no
; applicable callable signature, a symbolic first element remains data.

(: keep-list (-> (List Atom) (List Atom)))
(= (keep-list $items) $items)

!(test (keep-list (read binary)) (read binary))
