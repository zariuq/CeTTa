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
!(collapse (case (pick) ((1 one) (2 two) (Empty none))))
!(collapse (case (empty) ((1 one) (Empty none))))
!(collapse (case (once (pick)) ((1 one) (Empty none))))

; Dynamic applications of arithmetic and comparisons.
(= (apply-op $f $a $b) ($f $a $b))
!(apply-op + 1 2)
!(apply-op * 2.5 2)
!(apply-op - 10 4)
!(apply-op < 1 2)
!(apply-op == 3 3)
!(apply-op == 3 4)
