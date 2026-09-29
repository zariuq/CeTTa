; An `if` whose output is a value: a let's value, an argument, a field and
; a case key; conditions that compare, that are values with several answers
; or none, and that observe a match; branches with several answers, with
; none, and with bindings of their own.
;
; Each case runs from a relation that reads its inputs from the space, so
; the open tier runs it; the gate also runs the file on the machine.

(= (pick $x) (let $y (if (== $x 0) zero nonzero) (tag $y)))
(pick-input 0)
(pick-input 5)
(= (picks) (msort (collapse (match &self (pick-input $x) (pick $x)))))
!(picks)

(= (clip $x) (+ 1 (if (> $x 0) $x 0)))
(clip-input 4)
(clip-input -3)
(= (clips) (msort (collapse (match &self (clip-input $x) (clip $x)))))
!(clips)

(= (pair $x) (p (if (< $x 10) small large) $x))
(pair-input 3)
(pair-input 30)
(= (pairs) (msort (collapse (match &self (pair-input $x) (pair $x)))))
!(pairs)

(= (kase $x) (case (if (> $x 0) up down) ((up positive) (down other))))
(kase-input 2)
(kase-input -2)
(= (kases) (msort (collapse (match &self (kase-input $x) (kase $x)))))
!(kases)

; Two ifs in one expression.
(= (two $x) (+ (if (> $x 0) 1 0) (if (< $x 5) 10 0)))
(two-input 3)
(two-input 7)
(two-input -1)
(= (twos) (msort (collapse (match &self (two-input $x) (two $x)))))
!(twos)

; A condition with several answers takes a branch for each.
(= (cond) True)
(= (cond) False)
(= (both $x) (let $y (if (cond) yes no) (b $x $y)))
(both-input 1)
(= (boths) (msort (collapse (match &self (both-input $x) (both $x)))))
!(boths)

; A condition with no answers gives none.
(= (never) (empty))
(= (none-if $x) (let $y (if (never) yes no) (b $x $y)))
(none-input 1)
(= (nones) (collapse (match &self (none-input $x) (none-if $x))))
!(nones)

; A condition whose value is not a truth value takes the second branch.
(= (same $x) $x)
(= (truthy $x) (let $v (if (same $x) a b) (t $x $v)))
(truthy-input True)
(truthy-input False)
(truthy-input 5)
(= (truthies)
   (msort (collapse (match &self (truthy-input $x) (truthy $x)))))
!(truthies)

; Branches with several answers, and with none.
(= (branches $x)
   (let $y (if (== $x 1) (superpose (a b c)) (superpose (d e))) (w $x $y)))
(branches-input 1)
(branches-input 2)
(= (all-branches)
   (msort (collapse (match &self (branches-input $x) (branches $x)))))
!(all-branches)
(= (only-pos $x) (let $y (if (> $x 0) $x (empty)) (pos $y)))
(pos-input 3)
(pos-input -3)
(= (positives)
   (msort (collapse (match &self (pos-input $x) (only-pos $x)))))
!(positives)

; A branch with a binding of its own, over a variable the rest shares.
(= (shared $a $b)
   (let $r (if (== $a 1) (let $z (+ $b 5) $z) $b) (+ $r $b)))
(shared-input 1 2)
(shared-input 0 2)
(= (all-shared)
   (msort (collapse (match &self (shared-input $a $b) (shared $a $b)))))
!(all-shared)

; A condition that observes whether a match has a row.
(fact 1)
(= (known $n)
   (let $k (if (== () (collapse (once (match &self (fact $n) True))))
               unknown
               known)
     (k $n $k)))
(known-input 1)
(known-input 2)
(= (all-known)
   (msort (collapse (match &self (known-input $n) (known $n)))))
!(all-known)

; A loop that takes a value-position if on every iteration.
(= (count-even $n $acc)
   (if (< $n 1)
       $acc
       (let $v (if (== (% $n 2) 0) 1 0)
         (count-even (- $n 1) (+ $acc $v)))))
(count-input 1000)
(= (counts) (collapse (match &self (count-input $n) (count-even $n 0))))
!(counts)
