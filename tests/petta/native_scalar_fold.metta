; A fold whose step makes the next accumulator from scalars runs natively:
; the same value as stepping through the items, or the ordinary fold where
; a value is one the native fold does not read.
; The most frequent label of a sorted list, the smallest one on ties.
(= (mode-step ($c $cn $b $bn) $x)
   (if (== $x $c) ($c (+ $cn 1) $b $bn)
       (if (> $cn $bn) ($x 1 $c $cn) ($x 1 $b $bn))))
(= (mode $sorted)
   (let ($c $cn $b $bn) (foldl-atom $sorted (none 0 none 0) $acc $x (mode-step $acc $x))
     (if (> $cn $bn) $c $b)))
!(mode (1 1 2 2 2 3))
!(mode (1 1 2 2 3 3))
!(mode (4 5 5 7 7))
!(mode (9))
!(mode ())
!(foldl-atom (1 1 2 2 3) (none 0 none 0) $acc $x (mode-step $acc $x))
; Steps written in place: a sum, a maximum, a difference and orders.
!(foldl-atom (1 2 3 4) 0 $a $x (+ $a $x))
!(foldl-atom (3 9 2 7) 0 $a $x (if (> $x $a) $x $a))
!(foldl-atom (3 9 2 7) 100 $a $x (- $a $x))
!(foldl-atom (3 9 2 7) 5 $a $x (if (<= $x $a) $a (if (>= $x 8) 8 $x)))
; The item before the accumulator, and symbols compared with integers.
(= (count-eq $x ($k $n)) (if (== $x $k) ($k (+ $n 1)) ($k $n)))
!(foldl-atom (a 1 a b a) (a 0) $acc $x (count-eq $x $acc))
!(foldl-atom (1 2 1 1) (1 0) $acc $x (count-eq $x $acc))
; A tuple headed by a function is a call, not data; one headed by a symbol
; no rule defines is data.
(= (wrap $n) (tag $n))
(= (push ($h $n) $x) ($h (+ $n $x)))
!(foldl-atom (1 2) (wrap 0) $acc $x (push $acc $x))
(= (lead ($h $n) $x) ($x $n))
!(foldl-atom (wrap wrap) (k 0) $acc $x (lead $acc $x))
!(foldl-atom (j k) (h 0) $acc $x (lead $acc $x))
; A sum past 64 bits, floats, a step of two equations, and a step whose
; accumulator changes shape.
!(foldl-atom (9223372036854775807 1) 0 $a $x (+ $a $x))
!(foldl-atom (1.5 2) 0 $a $x (+ $a $x))
(= (either $a $x) (+ $a $x))
(= (either $a $x) (- $a $x))
!(collapse (foldl-atom (1 2) 0 $a $x (either $a $x)))
(= (shrink ($p $q) $x) ($p))
!(collapse (foldl-atom (1 2) (0 0) $acc $x (shrink $acc $x)))
; A comparison read as a value, not as a test: the ordinary fold answers
; true or false, a field holds them, true differs from 1, and a sum over one
; is a type error.  Read as a test it agrees.
!(foldl-atom (2) 0 $a $x (< $a $x))
!(foldl-atom (2) 0 $a $x (== $a $x))
!(foldl-atom (2) 0 $a $x (if (< $a $x) 7 9))
(= (bool-field ($tag $old) $x) ($tag (< 0 $x)))
!(foldl-atom (2) (tag 0) $a $x (bool-field $a $x))
!(catch (foldl-atom (2) 0 $a $x (+ (== $a $x) 1)))
!(foldl-atom (2) 0 $a $x (== (< $a $x) 1))
