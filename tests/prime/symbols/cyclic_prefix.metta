; A demanded prefix of a cyclic value.  The production of `ones` is its
; constructor with the field as written, and every demanded tail goes through
; the same cell again: the prefix is finite and the rest stays suspended.
(= ones (Cons 1 ones))
(= (head (Cons $h $t)) $h)
(= (tail (Cons $h $t)) $t)
(= (take 0 $s) Nil)
(= (take $n (Cons $h $t)) (if (> $n 0) (Cons $h (take (- $n 1) $t)) (empty)))
(= (drop 0 $s) $s)
(= (drop $n (Cons $h $t)) (if (> $n 0) (drop (- $n 1) $t) (empty)))
!(head ones)
!(take 3 ones)
; What is left after three is the stream itself.  An answer is a full value;
; a symbol met again inside its own value is written by its name, which is a
; finite presentation of the cyclic value.
!(drop 3 ones)
! ones
; Two symbols in one cycle.
(= xs (Cons 1 ys))
(= ys (Cons 2 xs))
!(take 5 xs)
! xs
; A symbol defined by a call: the call runs as far as it is demanded.
(= (from $n) (Cons $n (from (+ $n 1))))
(= nats (from 0))
!(take 3 nats)
!(head (drop 5 nats))
; The production is memoized, not re-run at each position: a cyclic symbol
; with two alternatives keeps its first choice all along the stream.  Three
; positions give two answers, not eight.
(= bits (Cons 0 bits))
(= bits (Cons 1 bits))
!(take 3 bits)
