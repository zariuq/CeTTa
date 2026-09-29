; A relation's compiled program serves later queries of the session only
; while the program it was compiled from is unchanged: equations added or
; removed between queries, and relations defined after an earlier query.

(= (f $x) (g $x))
(= (g a) one)

!(collapse (f a))
!(collapse (f a))
!(add-atom &self (= (g a) two))
!(collapse (f a))
!(remove-atom &self (= (g a) one))
!(collapse (f a))
(= (h $x) (f $x))
(= (g b) three)
!(collapse (h b))
!(collapse (f a))
