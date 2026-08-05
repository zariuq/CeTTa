; Closed deterministic constructor recursion must use the generated pure-call
; continuation rather than the native C stack.
(= (prepared-pack Z $n) (S $n))
(= (prepared-pack (S $m) Z) (prepared-pack $m (S Z)))
(= (prepared-pack (S $m) (S $n))
   (prepared-pack $m (prepared-pack (S $m) $n)))
(= (prepared-p2i Z) 0)
(= (prepared-p2i (S $x)) (+ 1 (prepared-p2i $x)))
(= (prepared-i2p $n)
   (if (== $n 0) Z (S (prepared-i2p (- $n 1)))))
!(prepared-p2i (prepared-pack (prepared-i2p 2) (prepared-i2p 300)))

; Overlap is not determinate.  The prepared machine must decline and preserve
; the active language's canonical bag/retry behavior.
(= (prepared-choice $x) left)
(= (prepared-choice $x) right)
!(prepared-choice value)

; A later alternative can itself continue through the same head.  Prime's
; explicit driver must not park that continuation on a native-stack
; OutcomeSet, and conservative decline must preserve both occurrences.
(= (prepared-deferred-choice (Cons $x $xs)) $x)
(= (prepared-deferred-choice (Cons $x (Cons $y $ys)))
   (prepared-deferred-choice (Cons $y $ys)))
!(prepared-deferred-choice (Cons 1 (Cons 2 Nil)))

; The generated bind instruction must implement the canonical structural-let
; contract: fresh fields bind positionally, repeated fields co-refer, and a
; variable already visible from the clause head is a rigid reference.
(= (prepared-unpack $pair)
   (let (Pair $x $y) $pair (Unpacked $x $y)))
!(prepared-unpack (Pair a b))

(= (prepared-same $pair)
   (let (Pair $x $x) $pair (Same $x)))
!(prepared-same (Pair same same))
; Negative: unequal repeated fields must not execute the body.
!(prepared-same (Pair left right))

(= (prepared-rigid $x $pair)
   (let (Pair $x $y) $pair (Rigid $x $y)))
!(prepared-rigid same (Pair same tail))
; Negative: a binder occurrence referring to the outer $x must not shadow it.
!(prepared-rigid same (Pair other tail))

; Eager compositions apply the canonical argument-congruence rule beneath an
; inert constructor.  This pins the distinction between constructor data and
; MeTTa quote: a nested callable must not leak into the final value.
(= (prepared-nested-leaf $x) (+ $x 1))
(= (prepared-nested-constructor $x)
   (Box (prepared-nested-leaf $x)))
!(prepared-nested-constructor 41)
