; Closed calls inside a shared-machine closure take the same finite-answer
; producer as the canonical evaluator; ground data arguments are passed whole.

(= (pick) 1)
(= (pick) 2)
(= (pick) 3)
(= (shifted) (let $x (pick) (+ $x 10)))
!(shifted)

; Inside a closure that also reads the space, a closed call of a pure
; relation is answered by the producer as one choice.
(item 100)
(item 200)
(= (mixed) (let $m (match &self (item $i) $i) (+ $m (pick))))
!(mixed)

; An answer producer whose relation leaves a call unmatched: HE returns the
; call itself.
(= (half 4) 2)
(= (half 8) 4)
(= (halves) (let $y (half 6) (quote $y)))
!(halves)

; Several answers, one of them Empty, under a let.
(= (maybe 1) one)
(= (maybe 2) (empty))
(= (maybe 3) three)
(= (spread) (let $v (maybe (superpose (1 2 3))) (wrap $v)))
!(spread)

; A relation with an effect has no answer program; its calls run as before.
(= (note $x) (let $_ (add-atom &self (noted $x)) $x))
(= (notes) (let $a (note 1) (let $b (note 2) (+ $a $b))))
!(notes)
!(match &self (noted $n) $n)

; Ground data arguments.
(= (len Nil) 0)
(= (len (Cons $h $t)) (+ 1 (len $t)))
(= (count-of $l) (let $n (len $l) (count $n)))
!(count-of (Cons a (Cons b (Cons c (Cons d Nil)))))
; A field that is a call is evaluated.
(= (inc $x) (+ $x 1))
!(count-of (Cons a (Cons (inc 1) (Cons c Nil))))
; A typed constructor checks its fields.
(: TCons (-> Number TList TList))
(= (tlen TNil) 0)
(= (tlen (TCons $h $t)) (+ 1 (tlen $t)))
(= (tcount $l) (let $n (tlen $l) (count $n)))
!(tcount (TCons 1 (TCons 2 TNil)))
!(tcount (TCons 1 (TCons two TNil)))

; The same arguments inside a closure that reads the space.
(= (sized $l) (let $m (match &self (item $i) $i) (sized $m (len $l))))
!(sized (Cons a (Cons b (Cons c Nil))))
!(sized (Cons a (Cons (inc 1) Nil)))
(= (sized-literal) (let $m (match &self (item $i) $i) (both $m (len (Cons p (Cons q Nil))))))
!(sized-literal)

; Closed data tuples at the root: each field call meets its own boundary.
!(both (len (Cons x (Cons y Nil))) (shifted))
!(pair (Cons 1 (Cons 2 Nil)) done)

; A field that is an error makes the argument the error (HE stops a run
; after a top-level error, so this comes last).
!(count-of (Cons a (Cons (Error bad field) Nil)))
