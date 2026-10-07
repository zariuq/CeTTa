; HE supports a compound operator. Each declaration is a native rule.
(=% (I $x) $x)
(=% ((K $x) $y) $x)
(=% (((S $x) $y) $z) (($x $z) ($y $z)))
(=% ((S K) K) I)

!(((S K) K) a)
!((K a) b)
; This call is too general: its variable must not be manufactured into K.
!((S K) $unknown)

; Ordinary relational equations can refine a caller when that is intended.
(= (relational K) selected)
!(let $answer (relational $caller) (answer $answer $caller))
