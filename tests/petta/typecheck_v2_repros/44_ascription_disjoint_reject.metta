; The base negative witness for explicit narrowing: Number and String have no
; overlap in either compatibility direction.

(: use-string (-> String))
(= (use-string) (the String 1))
