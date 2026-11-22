; User equations may extend get-type.  A statically unresolved nominal type is
; retained as a runtime obligation and accepted when the live classifier does.

(= (get-type $value)
   (catch (if (== (% $value 2) 0) EvenNumber)))

(: even-id (-> EvenNumber EvenNumber))
(= (even-id $value) $value)

!(test (even-id 2) 2)
