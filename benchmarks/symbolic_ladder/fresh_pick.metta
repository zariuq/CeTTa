; Two equations each bind a fresh $x. The shared spelling does not
; alias the two answers.

(= (pick) (let $x a $x))
(= (pick) (let $x b $x))
!(pick)
