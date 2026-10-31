; A connective's absorbed operand is never evaluated, on every route: False
; absorbs `and`, True absorbs `or`.  Any other first value demands the
; second operand, and the connective refuses operands that are not truth
; values.

(= (loop) (loop))
(= (positive $x) (if (> $x 0) True (loop)))

; The effects of an absorbed operand never run; a demanded operand's run
; once.
!(and False (let $u (println! and-absorbed) True){$u})
!(or True (let $u (println! or-absorbed) False){$u})
!(and True (let $u (println! and-demanded) True){$u})
!(or False (let $u (println! or-demanded) True){$u})

; Each answer of the first operand decides for itself: the second operand
; runs once, for the answer that leaves the value open.
!(and (superpose (True False)) (let $u (println! and-per-answer) True){$u})
!(or (superpose (True False)) (let $u (println! or-per-answer) False){$u})

; Equations.  A guard whose second operand diverges below zero is absorbed
; there and demanded above.
(= (guard-and $x) (and (> $x 0) (positive $x)))
(= (guard-or $x) (or (<= $x 0) (positive $x)))
!(guard-and -1)
!(guard-and 2)
!(guard-or 0)
!(guard-or 3)
(= (both $x $y) (and $x $y))
(= (either $x $y) (or $x $y))
!(both False (loop))
!(either True (loop))
!(both True False)
!(either False True)

; Each answer of a computed first operand decides for itself: answers that
; are all absorbed never reach a diverging second operand.
(= (falses) (superpose (False False)))
(= (mixed) (superpose (False True)))
(= (all-absorbed) (and (falses) (loop)))
(= (some-open $x) (and (mixed) (positive $x)))
!(all-absorbed)
!(some-open 1)

; A connective in a constructor's field and in a fold step absorbs as
; anywhere else.
(= (pair-and $x) (Pair (and $x (loop)) done))
(= (pair-or $x) (Pair (or $x (loop)) done))
!(pair-and False)
!(pair-or True)
!(fold (superpose (1 2 3)) False $acc $x (and $acc (loop)))
!(fold (superpose (1 -2 3)) True $acc $x (and $acc (> $x 0)))

; A first value that is not a truth value demands the second operand, and
; the connective refuses it.
!(both 1 True)
!(both foo True)
!(either 1 False)

; A refused connective's value, an error or a residual form, reaches a
; branch as it is.
(= (branch-on $x) (if (and $x True) A B))
!(branch-on 1)
!(branch-on foo)
!(branch-on True)

; The absorbed answer is the first operand's own value.
!(== (both (> 0 1) (loop)) False)
!(== (either (> 1 0) (loop)) True)

; Pattern fields of computed sequences are earned data. Boolean observers
; consume those truth values while preserving their scalar checks.
(= (boolean:rows) ((True True) (True True) (True True)))
(= (boolean:mixed) ((True True) (True False) (True True)))
!(let $rows (boolean:rows)
   (foldl-atom $rows True $acc $row
     (let ($left $right) $row (and $acc (if $left $right True)))))
!(let $rows (boolean:mixed)
   (foldl-atom $rows True $acc $row
     (let ($left $right) $row (and $acc (if $left $right True)))))
!(let $rows (boolean:rows)
   (foldl-atom $rows False $acc $row
     (let ($left $right) $row (or $acc (if $left $right False)))))
!(let $xs (boolean:rows)
   (map-atom $xs $row (let ($left $right) $row (xor $left $right))))
!(let $xs (boolean:rows)
   (map-atom $xs $row (let ($left $right) $row (not $left))))
(= (boolean:bad-rows) ((True 1)))
!(let $xs (boolean:bad-rows)
   (map-atom $xs $row (let ($left $right) $row (and $left $right))))
; Reading a Boolean operand does not activate quoted code.
!(let $code (quote (println! connective-code-must-stay-data)) (not $code))
