; Negative occurs-check witness in the actual-variable orientation.  The
; second field would require an infinite type after the first field binds.

(: cyclic-actual (-> ($a $b) ((List $b) (List $a))))
(= (cyclic-actual $value) $value)
