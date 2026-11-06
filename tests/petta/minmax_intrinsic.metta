; PeTTa min and max over evaluated operands select one of them: on a tie min
; answers its first and max its second, -0.0 is below 0.0, and a bigint
; compares exactly.
(= (mn $a $b) (min $a $b))
(= (mx $a $b) (max $a $b))
!(mn 1 2)
!(mn 2 1)
!(mn 1 1.0)
!(mn 1.0 1)
!(mn 0.0 -0.0)
!(mn -0.0 0.0)
!(mx 1 1.0)
!(mx 3 7)
!(collapse (mn (superpose (1 5)) 3))
!(mn 9223372036854775807 9223372036854775808)
