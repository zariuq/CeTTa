; The continuation inserts a fact after each answer. The next conjunctive
; leg is a new match, so it sees that fact. The leg already in progress
; does not grow its own occurrence list.

(left a)
(left b)
(right a old)
(right b old)

!(let $w (match &self (, (left $x) (right $x $y)) (w $x $y))
   (let $_ (add-atom &self (right b new)) $w))
