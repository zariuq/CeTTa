; The answers of a selection are the same, lazy or eager: lazy selection
; passes each answer on in the order eager selection would deliver it, and
; keeps HE's rules for errors and Empty (an error or Empty is dropped once a
; success exists).  This file runs both ways; both outputs are its expected
; output.

(= (g) 1)
(= (g) 2)
(= (g) 3)

; Equations, a let over them, and generate and test.
!(select 2 (g))
!(select 2 (let $x (g) (+ $x 100)))
!(once (let $x (g) (if (> $x 1) $x (empty))))

; Errors before successes: the errors are dropped, as eager selection drops
; them once a success exists.
(= (h) (Error h bad))
(= (h) 5)
(= (h) 6)
!(select 1 (h))
!(select 2 (h))
!(select 1 (let $x (h) (pair $x)))

; Only errors: they are the answers.  (An error that is the result of a
; query ends the program, so these are observed inside a collection.)
(= (e) (Error e one))
(= (e) (Error e two))
!(collapse (select 1 (e)))
!(select 2 (let $x (e) (pair $x)))

; Empty between successes.
(= (m) (empty))
(= (m) 7)
(= (m) (empty))
(= (m) 8)
!(select 2 (m))

; A nondeterministic condition, a case, a match.
(= (c) True)
(= (c) False)
!(select 2 (if (c) yes no))
!(select 2 (case (g) ((1 one) ($x other))))
(fact a)
(fact b)
!(select 1 (match &self (fact $x) (wrap $x)))

; A typed function, a recursion, nested selections.
(: tf (-> Number Number))
(= (tf $x) (+ $x 1))
(= (tf $x) (+ $x 2))
!(select 1 (tf 10))
(= (upto $i $n) $i)
(= (upto $i $n) (if (< $i $n) (upto (+ $i 1) $n) (empty)))
!(select 3 (upto 0 1000))
!(select 2 (superpose ((once (g)) (select 2 (g)) last)))
