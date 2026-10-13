; HE's rules on the compiled equation route, each with a case that applies
; and one that does not.

; An equation's head is a pattern its call's values meet: the argument is
; evaluated first, and (K 1) is one, which no head of h takes.
(= (K 1) one)
(= (h (K $a)) $a)
(= (hk $a) (h (K $a)))
!(hk 1)
; (K 2) meets no equation, so it is its own value, which h's head takes.
!(hk 2)

; A call no head takes is its own value; a head that takes the call gives
; its body's answers, none included.
(= (deduce (And $a $b)) (And (deduce $a) (deduce $b)))
(= (deduce (Fact $x)) (match &self (Known $x) T))
(= (And T T) T)
(Known sun)
!(deduce (And (Fact sun) (Fact sun)))
!(deduce (And (Fact sun) (Fact moon)))
!(deduce (Fact moon))

; The same rule for a tail call, and for the call the route enters first.
(= (g 1) one)
(= (top $x) (g $x))
!(top 5)
!(g 5)
!(top 1)

; Numbers meet by value across representations.
(= (n 1) one)
(= (n $x) other)
(= (nn $x) (n $x))
!(nn 1.0)
!(nn 2)

; A match's results are its template's instances, evaluated whole.
(item (+ 1 2))
(item done)
(= (items) (collapse (match &self (item $t) $t)))
(= (paired) (collapse (match &self (item $t) (pair $t x))))
!(items)
!(paired)

; A producer with effects completes before its consumer reads.
(p 1)
(p 2)
(= (grow) (let $r (match &self (p $x) (add-atom &self (q $x)))
             (collapse (match &self (q $y) $y))))
!(grow)
