; Negative occurs-check witness in the required-variable orientation.

(: cyclic-required (-> ((List $b) (List $a)) ($a $b)))
(= (cyclic-required $value) $value)
