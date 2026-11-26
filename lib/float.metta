; IEEE 754 binary64 floats as C computes them: an overflow is an infinity
; and an invalid operation a NaN, never an error; exact conversions between
; floats, integers and ratios of integers; exact comparison of a float with an
; integer; decimal text in the C locale, the shortest digits that read back
; first.  Arguments are floats except where integers or text are named.

; X plus y.
(= (float:add $x $y)
   (function
     (case (__cetta_lib_float add $x $y)
       (($result (return $result))))))

; X minus y.
(= (float:sub $x $y)
   (function
     (case (__cetta_lib_float sub $x $y)
       (($result (return $result))))))

; X times y.
(= (float:mul $x $y)
   (function
     (case (__cetta_lib_float mul $x $y)
       (($result (return $result))))))

; X divided by y (by zero, an infinity or a NaN).
(= (float:div $x $y)
   (function
     (case (__cetta_lib_float div $x $y)
       (($result (return $result))))))

; C's fmod: the remainder with x's sign.
(= (float:fmod $x $y)
   (function
     (case (__cetta_lib_float fmod $x $y)
       (($result (return $result))))))

; X's magnitude with y's sign.
(= (float:copysign $x $y)
   (function
     (case (__cetta_lib_float copysign $x $y)
       (($result (return $result))))))

; X to the y as C's pow: (FloatPowed VALUE ok), or range or domain from errno.
(= (float:pow $x $y)
   (function
     (case (__cetta_lib_float pow $x $y)
       (($result (return $result))))))

; -1, 0 or 1, or unordered when a NaN is compared.
(= (float:compare $x $y)
   (function
     (case (__cetta_lib_float compare $x $y)
       (($result (return $result))))))

; Minus x (minus zero from zero).
(= (float:neg $x)
   (function
     (case (__cetta_lib_float neg $x)
       (($result (return $result))))))

; X's magnitude.
(= (float:abs $x)
   (function
     (case (__cetta_lib_float abs $x)
       (($result (return $result))))))

; The greatest whole float not above x.
(= (float:floor $x)
   (function
     (case (__cetta_lib_float floor $x)
       (($result (return $result))))))

; The least whole float not below x.
(= (float:ceil $x)
   (function
     (case (__cetta_lib_float ceil $x)
       (($result (return $result))))))

; X's whole part, as a float.
(= (float:trunc $x)
   (function
     (case (__cetta_lib_float trunc $x)
       (($result (return $result))))))

; X's square root.
(= (float:sqrt $x)
   (function
     (case (__cetta_lib_float sqrt $x)
       (($result (return $result))))))

; C's libm, as it computes them: a NaN where undefined, an infinity where it
; overflows; a language builds its own errors on the results.

; E to the x.
(= (float:exp $x)
   (function
     (case (__cetta_lib_float exp $x)
       (($result (return $result))))))

; Two to the x.
(= (float:exp2 $x)
   (function
     (case (__cetta_lib_float exp2 $x)
       (($result (return $result))))))

; E to the x, minus one (exact near zero).
(= (float:expm1 $x)
   (function
     (case (__cetta_lib_float expm1 $x)
       (($result (return $result))))))

; X's natural logarithm.
(= (float:log $x)
   (function
     (case (__cetta_lib_float log $x)
       (($result (return $result))))))

; X's base-two logarithm.
(= (float:log2 $x)
   (function
     (case (__cetta_lib_float log2 $x)
       (($result (return $result))))))

; X's base-ten logarithm.
(= (float:log10 $x)
   (function
     (case (__cetta_lib_float log10 $x)
       (($result (return $result))))))

; The natural logarithm of one plus x (exact near zero).
(= (float:log1p $x)
   (function
     (case (__cetta_lib_float log1p $x)
       (($result (return $result))))))

; X's sine.
(= (float:sin $x)
   (function
     (case (__cetta_lib_float sin $x)
       (($result (return $result))))))

; X's cosine.
(= (float:cos $x)
   (function
     (case (__cetta_lib_float cos $x)
       (($result (return $result))))))

; X's tangent.
(= (float:tan $x)
   (function
     (case (__cetta_lib_float tan $x)
       (($result (return $result))))))

; X's arc sine.
(= (float:asin $x)
   (function
     (case (__cetta_lib_float asin $x)
       (($result (return $result))))))

; X's arc cosine.
(= (float:acos $x)
   (function
     (case (__cetta_lib_float acos $x)
       (($result (return $result))))))

; X's arc tangent.
(= (float:atan $x)
   (function
     (case (__cetta_lib_float atan $x)
       (($result (return $result))))))

; X's hyperbolic sine.
(= (float:sinh $x)
   (function
     (case (__cetta_lib_float sinh $x)
       (($result (return $result))))))

; X's hyperbolic cosine.
(= (float:cosh $x)
   (function
     (case (__cetta_lib_float cosh $x)
       (($result (return $result))))))

; X's hyperbolic tangent.
(= (float:tanh $x)
   (function
     (case (__cetta_lib_float tanh $x)
       (($result (return $result))))))

; X's inverse hyperbolic sine.
(= (float:asinh $x)
   (function
     (case (__cetta_lib_float asinh $x)
       (($result (return $result))))))

; X's inverse hyperbolic cosine.
(= (float:acosh $x)
   (function
     (case (__cetta_lib_float acosh $x)
       (($result (return $result))))))

; X's inverse hyperbolic tangent.
(= (float:atanh $x)
   (function
     (case (__cetta_lib_float atanh $x)
       (($result (return $result))))))

; The error function at x.
(= (float:erf $x)
   (function
     (case (__cetta_lib_float erf $x)
       (($result (return $result))))))

; One minus the error function at x.
(= (float:erfc $x)
   (function
     (case (__cetta_lib_float erfc $x)
       (($result (return $result))))))

; X's cube root.
(= (float:cbrt $x)
   (function
     (case (__cetta_lib_float cbrt $x)
       (($result (return $result))))))

; The arc tangent of y over x, in the right quadrant: (float:atan2 Y X).
(= (float:atan2 $x $y)
   (function
     (case (__cetta_lib_float atan2 $x $y)
       (($result (return $result))))))

; The length of the vector (x, y).
(= (float:hypot $x $y)
   (function
     (case (__cetta_lib_float hypot $x $y)
       (($result (return $result))))))

; The float after x in the direction of y.
(= (float:nextafter $x $y)
   (function
     (case (__cetta_lib_float nextafter $x $y)
       (($result (return $result))))))

; X minus the nearest multiple of y (IEEE remainder).
(= (float:remainder $x $y)
   (function
     (case (__cetta_lib_float remainder $x $y)
       (($result (return $result))))))

; X times y plus z, rounded once.
(= (float:fma $x $y $z)
   (function
     (case (__cetta_lib_float fma $x $y $z)
       (($result (return $result))))))

; X as m times two to the e, m in [0.5, 1) or zero: (FloatFrexp M E).
(= (float:frexp $x)
   (function
     (case (__cetta_lib_float frexp $x)
       (($result (return $result))))))

; X times two to the integer n.
(= (float:ldexp $x $n)
   (function
     (case (__cetta_lib_float ldexp $x $n)
       (($result (return $result))))))

; X's fractional and whole parts, each with x's sign: (FloatModf FRAC WHOLE).
(= (float:modf $x)
   (function
     (case (__cetta_lib_float modf $x)
       (($result (return $result))))))

(= (float:is-nan $x)
   (function
     (case (__cetta_lib_float is-nan $x)
       (($result (return $result))))))

(= (float:is-inf $x)
   (function
     (case (__cetta_lib_float is-inf $x)
       (($result (return $result))))))

(= (float:is-finite $x)
   (function
     (case (__cetta_lib_float is-finite $x)
       (($result (return $result))))))

; Whether x's sign bit is set (minus zero too).
(= (float:sign-bit $x)
   (function
     (case (__cetta_lib_float sign-bit $x)
       (($result (return $result))))))

; The float nearest an integer, ties to even, or (FloatOverflow).
(= (float:from-int $n)
   (function
     (case (__cetta_lib_float from-int $n)
       (($result (return $result))))))

; The float nearest num/den, ties to even, or (FloatOverflow).
(= (float:from-ratio $num $den)
   (function
     (case (__cetta_lib_float from-ratio $num $den)
       (($result (return $result))))))

; The integer a finite float truncates to, exactly.
(= (float:to-int $x)
   (function
     (case (__cetta_lib_float to-int $x)
       (($result (return $result))))))

; A float compared with an integer, exactly: -1, 0, 1 or unordered.
(= (float:compare-int $x $n)
   (function
     (case (__cetta_lib_float compare-int $x $n)
       (($result (return $result))))))

; The shortest digits that read back to x, nearest x: (FloatDigits SIGN DIGITS DECPT) for 0.DIGITS times ten to DECPT, or (FloatSpecial NAME) for inf, -inf and nan.
(= (float:digits $x)
   (function
     (case (__cetta_lib_float digits $x)
       (($result (return $result))))))

; X written as C's %.Pf, %.Pe or %.Pg: conversion f, e or g.
(= (float:format $x $conversion $precision)
   (function
     (case (__cetta_lib_float format $x $conversion $precision)
       (($result (return $result))))))

; A float's literal read whole, nearest: digits with a point or an exponent, inf, infinity or nan in any case, with a sign.
(= (float:parse $text)
   (function
     (case (__cetta_lib_float parse $text)
       (($result (return $result))))))
