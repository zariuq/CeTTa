; A case with an Empty default over a key with no alternative left retires
; its default at the key's answer, so a loop over filter-atom, whose tests
; are such cases, runs in constant choice depth; the default still answers
; when the key has none, and still gives way to every answer of a key with
; several.

(= (keep-odd $xs) (filter-atom $xs $x (== (% $x 2) 1)))
(= (loop $n $acc)
   (if (< $n 1)
       $acc
       (let $k (keep-odd (1 2 3 4 5))
            (loop (- $n 1) (+ $acc (size-atom $k))))))
!(loop 2000 0)

(= (pick) 1)
(= (pick) 2)
!(sort-atom (collapse (case (pick) ((1 one) (2 two) (Empty none)))))
!(collapse (case (empty) ((1 one) (Empty none))))
; Which answer once keeps is open; either is a key's answer, so the default
; retires and the case answers once.
!(collapse (case (once (pick)) ((1 picked) (2 picked) (Empty none))))

; Dynamic applications of arithmetic and comparisons.
(= (apply-op $f $a $b) ($f $a $b))
!(apply-op + 1 2)
!(apply-op * 2.5 2)
!(apply-op - 10 4)
!(apply-op < 1 2)
!(apply-op == 3 3)
!(apply-op == 3 4)

; A translated body keeps each authored Empty arm, including a call-free
; default, across separate activations of an outer nondeterministic let.
(: eval-case (-> Atom %Undefined%))
(= (eval-case $source) (eval $source))
!(eval-case (collapse (let $y (superpose (1 2 1))
    (case (if (== $y 1) 5 (empty)) ((5 five) (Empty none))))))
!(eval-case (collapse (let $y (superpose (2 1 2))
    (case (if (== $y 1) 5 (empty)) ((5 five) (Empty none))))))
; A variable whose value is Empty still is not an authored default.
!(eval-case (collapse (let $pattern (quote Empty)
    (case (empty) (($pattern none) (5 five))))))
; The default body can hold call-shaped data without activating it.
!(eval-case (collapse (let $body (quote (+ 1 2))
    (case (empty) ((5 five) (Empty $body))))))
