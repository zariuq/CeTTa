; A let binding answers what its source answers: the match, the let around
; it and the transform let-source forms give the same answers, both for code
; stored in a space and for a computation written in place.
!(import! &self interaction)
(let-law:claim (superpose (p q)))
!(match &self (let-law:claim $c) $c)
!(let $c (match &self (let-law:claim $x) $x) $c)
!(bind! &let-law (new-space))
!(add-atom &let-law (let-law:claim (superpose (p q))))
!(collapse (transform &let-law (read (let-law:claim $c)) (got $c)))
!(collapse (transform &let-law (, (read (let-law:claim $x)) (let $c $x)) (got $c)))
!(collapse (transform &let-law (let $c (match &self (let-law:claim $x) $x)) (got $c)))

; A computation, written in place, runs alike with and without the let.
!(superpose (p q))
!(let $c (superpose (p q)) $c)
!(collapse (transform &let-law (every (let $c (superpose (p q)))) (got $c)))
