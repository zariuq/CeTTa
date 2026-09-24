; Transfer of pending callers from the answer producer to equation search.
; A consumer adds an equation while a producer with non-tail calls is
; suspended; the producer hands its frontier to equation search.  Calls
; entered before the addition keep their snapshot; later calls see it.

(= (pick) a)
(= (pick) b)
(= (pick) c)

; 1. One pending caller over a pending call.
(= (gen2) (let $x (pick) (mk $x 1)))
(= (gen2) (let $x (pick) (let $y (pick) (mk $x $y))))
!(let $p (gen2) (if (== $p (mk b 1)) (let $_ (add-atom &self (= (pick) d)) $p) $p))

; 2. An operand already evaluated is a value in the continuation, not a
;    call to repeat: the first (pick2) answer is fixed when the second is
;    pending.
(= (pick2) u)
(= (pick2) v)
(= (twice) (mk2 (pick2) (pick2)))
(= (twice) done)
!(let $p (twice) (if (== $p (mk2 u v)) (let $_ (add-atom &self (= (pick2) w)) $p) $p))

; 3. Duplicate equations keep their multiplicity across the transfer.
(= (dup) p)
(= (dup) p)
(= (dup) q)
(= (dupgen) (let $x (dup) (tag $x)))
(= (dupgen) (let $x (dup) (let $y (dup) (tag $x $y))))
!(let $p (dupgen) (if (== $p (tag q)) (let $_ (add-atom &self (= (dup) r)) $p) $p))

; 4. A structured pattern binds the callee's answer after the transfer.
(= (pairs) (pr 1 one))
(= (pairs) (pr 2 two))
(= (swap) (let (pr $n $w) (pairs) (sw $w $n)))
(= (swap) (let (pr $n $w) (pairs) (let (pr $m $z) (pairs) (sw2 $w $z (+ $n $m)))))
!(let $p (swap) (if (== $p (sw two 2)) (let $_ (add-atom &self (= (pairs) (pr 3 three))) $p) $p))

; 5. Three pending callers, arithmetic and a condition after each call.
(= (num) 1)
(= (num) 2)
(= (lvl3) (let $x (num) (let $y (num) (let $z (num) (if (> (+ $x $y) $z) (big $x $y $z) (small $x $y $z))))))
(= (lvl3) end)
!(let $p (lvl3) (if (== $p (big 1 2 1)) (let $_ (add-atom &self (= (num) 3)) $p) $p))

; 6. Two additions at different points: the re-entered producer hands off a
;    second time (prepared -> search -> prepared -> search).
(= (col) red)
(= (col) green)
(= (flag2) (let $x (col) (let $y (col) (fl $x $y))))
(= (flag2) (let $x (col) (solo $x)))
!(let $p (flag2) (if (== $p (fl red green)) (let $_ (add-atom &self (= (col) blue)) $p) (if (== $p (fl green blue)) (let $_ (add-atom &self (= (col) white)) $p) $p)))
