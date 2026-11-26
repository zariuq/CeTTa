; Observations of a match: a first answer, presence and absence, a count.
; Rows past MATCH_TRIE_THRESHOLD are read through the space's indexes.
(e 3 (val 1))
(e 4 (val 2))
(e 3 (val 3))
(e 4 (val 4))
(e 3 (val 5))
(e 4 (val 6))
(e 3 (val 7))
(e 4 (val 8))
(e 3 (val 9))
(e 4 (val 10))
(e 3 (val 11))
(e 4 (val 12))
(e 3 (val 13))
(e 4 (val 14))
(e 3 (val 15))
(e 4 (val 16))
(e 3 (val 17))
(e 4 (val 18))

; once stops at the first answer, in the order the space's rows stand.
!(once (match &self (e 3 $v) $v))
!(once (match &self (e 9 $v) $v))

; Presence and absence, either way round, and a case on emptiness.
!(== (collapse (match &self (e 3 $v) $v)) ())
!(== () (collapse (match &self (e 3 $v) yes)))
!(== (collapse (match &self (e 9 $v) $v)) ())
!(== () (collapse (match &self (e 9 $v) yes)))
!(case (collapse (match &self (e 3 $v) yes)) ((() none) ($_ some)))
!(case (collapse (match &self (e 9 $v) yes)) ((() none) ($_ some)))
!(case (collapse (match &self (e 9 $v) yes)) (($q some) (() none)))
!(case (collapse (match &self (e 3 $v) yes)) ((() none)))

; A variable branch binds the collapse where the case keeps bindings.
!(let $r (case (collapse (match &self (e 3 $v) yes)) ((() none) ($q some))) ($r $q))
!(let $r (case (collapse (match &self (e 9 $v) yes)) (($q some) (() none))) ($r $q))

; A template with a nested call is evaluated at every row.
(= (f) (let $u (add-atom &self (seen f)) 1))
!(== (collapse (match &self (e 3 $v) (pair (f) $v))) ())
!(collapse (match &self (seen $x) $x))

; A later row's value is a call with an effect: it is evaluated.
(g 1 (val 1))
(g 1 (mark))
(= (mark) (let $u (add-atom &self (marked)) done))
!(== (collapse (match &self (g 1 $v) $v)) ())
!(collapse (match &self (marked) yes))

; A row's value is evaluated: a call with two answers counts twice, and
; Empty, alone or inside an expression, has none.
(h 1 (k))
(h 1 (val 1))
(= (k) (superpose (a b)))
!(collapse (match &self (h 1 $v) $v))
!(let $c (collapse (match &self (h 1 $v) $v)) (size-atom $c))
(z 1 Empty)
(z 1 (val Empty))
(z 1 2)
!(collapse (match &self (z 1 $v) $v))
!(let $c (collapse (match &self (z 1 $v) $v)) (size-atom $c))
(y 1 Empty)
!(== (collapse (match &self (y 1 $v) $v)) ())
!(case (collapse (match &self (y 1 $v) $v)) ((() none) ($_ some)))
!(== (collapse (match &self (h 2 $v) $v)) ())

; A stored variable binds a value of the pattern.
(sv $x $x)
!(once (match &self (sv 1 $y) $y))
