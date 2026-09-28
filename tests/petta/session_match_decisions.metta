; Compiled match decisions serve later top-level queries while the
; equations they were compiled over are current: equations added and
; removed between queries, and a relation defined after earlier queries.

(= (tone red) warm)
(= (tone orange) warm)
(= (tone blue) cool)
(= (tone green) cool)
(= (tone (mix $a $b)) (blend (tone $a) (tone $b)))

!(tone red)
!(tone green)
!(tone (mix red blue))
!(add-atom &self (= (tone violet) cool))
!(tone violet)
!(tone (mix violet orange))
!(remove-atom &self (= (tone red) warm))
!(collapse (tone red))
!(collapse (tone (mix red blue)))
!(tone orange)

; A relation defined after the queries above.
!(add-atom &self (= (shade dark) (tone blue)))
!(add-atom &self (= (shade light) (tone orange)))
!(shade dark)
!(shade light)

; The same queries again, answered from what the first ones compiled.
!(tone green)
!(tone violet)
!(shade dark)
