; A string in a pattern selects the rows holding that string: the index keys
; a string by its bytes, wherever and however the string was made.
!(bind! &s (new-space))
!(add-atom &s (Use "alpha" 1))
!(add-atom &s (Use "beta" 2))
!(add-atom &s (Use "beta" 2))
!(add-atom &s (Use gamma 3))
!(add-atom &s (Use 4 four))
!(collapse (match &s (Use "alpha" $v) $v))
; The same bytes made at run time select the same row.
!(let $k (repr alpha) (collapse (match &s (Use $k $v) $v)))
; Duplicate rows answer twice; a symbol of the same text is not the string.
!(collapse (match &s (Use "beta" $v) $v))
!(collapse (match &s (Use "gamma" $v) $v))
!(collapse (match &s (Use gamma $v) $v))
; An absent key answers nothing.
!(collapse (match &s (Use "delta" $v) $v))
; A variable stored at the key matches every key.
!(add-atom &s (Use $any any))
!(collapse (match &s (Use "alpha" $v) $v))
!(collapse (match &s (Use "delta" $v) $v))
; A query variable at the key reaches the string rows too.
!(let $r (collapse (match &s (Use $k $v) $v)) (size-atom $r))
; Removal and reinsertion.
!(remove-atom &s (Use "alpha" 1))
!(collapse (match &s (Use "alpha" $v) $v))
!(add-atom &s (Use "alpha" 11))
!(collapse (match &s (Use "alpha" $v) $v))
; Many keys: each lookup finds its own row.
(= (fill $i $n)
   (if (== $i $n) ok
     (let* (($k (repr $i)) ($a (add-atom &s (Key $k $i)))) (fill (+ $i 1) $n))))
!(fill 0 300)
(= (probe $i $n $acc)
   (if (== $i $n) $acc
     (let* (($k (repr $i)) ($r (collapse (match &s (Key $k $v) $v))))
       (probe (+ $i 1) $n (+ $acc (size-atom $r))))))
!(probe 0 300 0)
!(collapse (match &s (Key "299" $v) $v))
