; Negative mirror: the same residual obligation must reject a value for which
; the live get-type extension produces no matching nominal type.

(= (get-type $value)
   (catch (if (== (% $value 2) 0) EvenNumber)))

(: even-id (-> EvenNumber EvenNumber))
(= (even-id $value) $value)

!(even-id 3)
