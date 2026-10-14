; `and` demands its second operand only when the first is True, and `or`
; only when the first is False; the first operand is always demanded.

(= (loop) (loop))
(= (choice) (superpose (True False)))
(= (first) (superpose (False True)))

; The second operand is not evaluated: it neither loops nor runs effects.
!(and False (loop))
!(or True (loop))
!(and False (let $x (println! never) True){$x})
(= (guarded) (and False (loop)))
!(guarded)

; An undemanded operand contributes no alternatives.
!(and False (choice))
!(and (first) (choice))
!(or (first) (choice))

; When the first operand leaves the value open, the second is demanded, and
; operands that are not truth values are refused as before.
!(and True (let $x (println! demanded) True){$x})
!(and True False)
!(or False True)
!(and 1 True)
!(and True 1)
