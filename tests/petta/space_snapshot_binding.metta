; (with-space-snapshot B S BODY) binds B to a copy of the space S as it
; stands and evaluates BODY as PeTTa evaluates any body: it reads as
; (let B <the copy> BODY).  Writes to S do not reach the copy, nor writes to
; the copy S.

!(bind! &v (new-space priority))
!(add-atom &v (5 e))
!(add-atom &v (2 b))
!(w1 (with-space-snapshot $s &v (reverse (1 2))))
!(w2 (with-space-snapshot $s &v (let $a (reverse (1 2)) $a)))
!(w3 (with-space-snapshot $s &v (sort-atom (b a))))
!(w4 (with-space-snapshot $s &v (car-atom (1 2))))
!(with-space-snapshot $s &v (reverse (1 2)))

!(bind! &n (new-space))
!(add-atom &n (k 1))
(= (my-rev $l) (reverse $l))
(= (my-id $x) $x)
!(a1 (with-space-snapshot $s &n (my-id 7)))
!(a2 (with-space-snapshot $s &n (my-rev (1 2))))
!(a3 (with-space-snapshot $s &n (length (1 2))))
!(a4 (with-space-snapshot $s &n (append (1) (2))))
!(a5 (with-space-snapshot $s &n (+ 1 2)))
!(a6 (with-space-snapshot $s &n (collapse (match $s (k $x) $x))))
!(a7 (with-space-snapshot $s &n (msort (b a))))
!(a8 (with-space-snapshot $s &n (last (1 2))))

; The copy reads the space as it stood; the space reads its own writes.
!(bind! &live (new-space))
!(add-atom &live (k 1))
!(sort-atom (collapse (with-space-snapshot $s &live
                        (let $added (add-atom &live (k 2))
                          (match $s (k $x) $x)))))
!(sort-atom (collapse (match &live (k $x) $x)))
!(sort-atom (collapse (with-space-snapshot $s &live
                        (let $added (add-atom $s (k 3))
                          (match $s (k $x) $x)))))
!(sort-atom (collapse (match &live (k $x) $x)))

; A body with several answers answers each.
!(sort-atom (collapse (with-space-snapshot $s &live (superpose (a b)))))
