; A typed call inside HE's shared execution: HE checks the call by its
; argument expressions' types, evaluates each argument at its formal, then
; dispatches the call over those values without checking them again.  The
; answers equal hyperon-experimental's as bags (collapse order is not
; observable).
(: sq (-> Number Number))
(= (sq $x) (* $x $x))
(= (rng) (superpose (1 "a" 2)))
(= (rng2) (superpose ("a" "b")))
(= (range $a $b) (if (>= $a $b) (empty) (superpose ($a (range (+ $a 1) $b)))))
(= (usq $x) (sq $x))
; Arguments evaluated by the machine; an ill-typed value fails inside the body,
; and an error alternative drops where another succeeds.
!(collapse (sq (range 0 4)))
!(collapse (sq (rng)))
!(collapse (sq (rng2)))
; The call's own check rejects an argument expression of the wrong type.
!(collapse (sq "a"))
; Arguments with variables, nested typed calls, and a typed call in an
; untyped body.
!(collapse (let $x 3 (sq $x)))
!(collapse (sq (sq 2)))
!(collapse (usq (range 0 3)))
!(let $l (collapse (sq (range 0 200))) (foldl-atom $l 0 $a $b (+ $a $b)))
; A typed call at an argument whose formal its range meets runs as at the top
; (its body at its range); one whose range the formal does not take is
; rejected by the enclosing call's check, which names the call as written.
(: g (-> Number Number))
(= (g $x) (+ $x 1))
(: h (-> Number String))
(= (h $x) "s")
(: k (-> Number Number))
(= (k $x) "notnum")
(= (wrapU $x) (g $x))
!(collapse (+ 1 (g (g 2))))
!(collapse (+ 1 (wrapU 3)))
!(collapse (+ 1 (k 2)))
!(collapse (+ 1 (g "a")))
!(collapse (+ 1 (h 2)))
