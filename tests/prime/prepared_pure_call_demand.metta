; A variable-only pattern does not demand its unused argument under Need.
; Eagerly evaluating the comparison would produce a type fault.
(= (prepared-keep $value $unused) $value)
!(prepared-keep 7 (== 1 "unused-fault"))

; Returned fields retain their bindings and branch correlation.
(= (prepared-packet:add $x) (+ $x 1))
(= (prepared-packet:return $x) (Packet $x (prepared-packet:add $x)))
!(let (Packet $a $b) (prepared-packet:return 7) (Pair $a $b))
!(collapse (let (Packet $a $b)
               (prepared-packet:return (superpose (7 11)))
               (Pair $a $b)))

; Keeping two occurrences does not demand an unrelated faulting argument.
(= (prepared-packet:keep $x $unused) (Packet $x $x))
!(prepared-packet:keep 7 (== 1 "unused-fault"))
