; A value has one representation in a space, whichever route produced it.
; A part taken by car-atom, cdr-atom or decons-atom straight from a written
; expression is stored as the value it is: a literal pattern finds it,
; remove-atom removes it and add-atom-nodup sees it, as for the same part
; reached through a `let`-bound expression or written directly.

!(bind! &s (new-space))
!(let $w (car-atom (the cat)) (add-atom &s (kv $w 1)))
!(let $l (the cat) (let $w (car-atom $l) (add-atom &s (kv $w 2))))
!(add-atom &s (kv the 3))
!(collapse (match &s (kv the $v) $v))
!(collapse (match &s (kv the 1) found))
!(let $w (car-atom (the cat)) (add-atom-nodup &s (kv $w 1)))
!(space-len &s)
!(remove-atom &s (kv the 1))
!(collapse (match &s (kv the $v) $v))

; Expression parts and decomposed parts are stored as the values they are.
!(let $w (cdr-atom (a b c)) (add-atom &s (tail $w)))
!(collapse (match &s (tail (b c)) found))
!(let $w (car-atom ((f 1) cat)) (add-atom &s (head $w)))
!(collapse (match &s (head (f 1)) found))
!(let ($h $t) (decons-atom (zz cat)) (add-atom &s (parts $h $t)))
!(collapse (match &s (parts zz (cat)) found))

; A space stores values: a pending computation in a stored payload runs once,
; when it is stored. `ran` prints before `after-add`, and reading the record
; gives its value without running it again.
(= (noisy) (let $u (println! ran) done))
!(bind! &d (new-space))
!((lam $x (add-atom &d (k $x))) (noisy))
!(println! after-add)
!(collapse (match &d (k $c) $c))
