; Integers of any size (machine or big) as two's complement bit strings that
; extend without end, as GMP and Python see them.  Arguments are integers
; except where a count or a base is named.

; X and y, bit by bit.
(= (integer:and $x $y)
   (function
     (case (__cetta_lib_integer and $x $y)
       (($result (return $result))))))

; X or y, bit by bit.
(= (integer:or $x $y)
   (function
     (case (__cetta_lib_integer or $x $y)
       (($result (return $result))))))

; X exclusive-or y, bit by bit.
(= (integer:xor $x $y)
   (function
     (case (__cetta_lib_integer xor $x $y)
       (($result (return $result))))))

; Not x, every bit: minus x minus one.
(= (integer:not $x)
   (function
     (case (__cetta_lib_integer not $x)
       (($result (return $result))))))

; X times two to the non-negative count n.
(= (integer:shift-left $x $n)
   (function
     (case (__cetta_lib_integer shift-left $x $n)
       (($result (return $result))))))

; X divided by two to the non-negative count n, rounded down.
(= (integer:shift-right $x $n)
   (function
     (case (__cetta_lib_integer shift-right $x $n)
       (($result (return $result))))))

; The number of bits of x's magnitude, 0 for 0.
(= (integer:bit-length $x)
   (function
     (case (__cetta_lib_integer bit-length $x)
       (($result (return $result))))))

; The number of one bits of x's magnitude.
(= (integer:bit-count $x)
   (function
     (case (__cetta_lib_integer bit-count $x)
       (($result (return $result))))))

; Base to the exponent modulo the nonzero m, with m's sign; a negative
; exponent takes base's inverse modulo m, refused when there is none.
(= (integer:pow-mod $base $exponent $m)
   (function
     (case (__cetta_lib_integer pow-mod $base $exponent $m)
       (($result (return $result))))))

; The digits of x's magnitude in a base from 2 to 36, lower case.
(= (integer:digits $x $base)
   (function
     (case (__cetta_lib_integer digits $x $base)
       (($result (return $result))))))
