; Equal structural heads and arities do not hide an incompatible field.

(: need-row (-> (Row Number String) Number))
(= (need-row $row) 1)

(: invalid-row (-> (Row Number Bool) Number))
(= (invalid-row $row) (need-row $row))
