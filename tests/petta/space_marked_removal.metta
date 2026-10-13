; Removal leaves the rows it did not remove in their order, whatever reads
; the space next: a query, a count, a listing, a match that is running.

(F 0)
(F 1)
(F 2)
(F 3)
(F 4)
(F 5)
(F 6)
(F 7)
(F 8)
(F 9)
(F 10)
(F 11)
(F 12)
(F 13)
(F 14)
(F 15)
(F 16)
(F 17)
(F 18)
(F 19)
(F 20)
(F 21)
(F 22)
(F 23)
(A 0 5)
(A 1 0)
(A 2 4)
(A 3 8)
(A 4 2)
(A 5 11)
(A 6 14)
(A 7 1)
(A 8 7)
(A 9 9)
(A 10 10)
(A 11 15)
(A 12 3)
(A 13 12)
(A 14 6)
(A 15 13)
(= (double $x) (* 2 $x))

; Removal inside a match over the same space: the match reads the rows it
; began with.
!(collapse (match &self (A $i $x) (if (> $x 10) (remove-atom &self (A $i $x)) (empty))))
!(collapse (match &self (A $i $x) (A $i $x)))
!(collapse (match &self (A 6 $x) $x))

; Remove and add inside a conjunction.
!(collapse (match &self (, (F $k) (A $k $x))
     (if (< $x 3) (let $r (remove-atom &self (A $k $x)) (add-atom &self (A $k 100))) (empty))))
!(collapse (match &self (A $i $x) (A $i $x)))

; Counts and listings read the same rows.
!(size-atom (collapse (match &self (A $i $x) $i)))
!(size-atom (collapse (match &self (F $k) $k)))

; An equation is still found after data rows before it were removed.
!(remove-atom &self (F 0))
!(remove-atom &self (F 1))
!(double 21)

; Removing an equation contracts the space at once, marks or not.
!(remove-atom &self (= (double $x) (* 2 $x)))
!(collapse (double 21))
!(add-atom &self (= (double $x) (+ $x $x)))
!(double 21)

; Remove every A row, then add rows again.
!(collapse (match &self (A $i $x) (remove-atom &self (A $i $x))))
!(collapse (match &self (A $i $x) (A $i $x)))
!(add-atom &self (A 0 zero))
!(add-atom &self (A 1 one))
!(collapse (match &self (A $i $x) (A $i $x)))

; Remove every F row: more removed rows than live ones.
!(collapse (match &self (F $k) (remove-atom &self (F $k))))
!(collapse (match &self (F $k) $k))
!(collapse (match &self (A $i $x) (A $i $x)))
