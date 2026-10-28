; CeTTa math helper interface.
; Canonical namespace is math:. Its HE overlay publishes the dotted math.
; aliases; other dialects retain their namespace/display policy.
; Intentionally conservative: this layer only exposes math operations that are
; already stable in CeTTa or are small, honest library combinators we expect to
; want broadly in WM-PLN, genomics, and AGI-oriented workloads.

(: math:min (-> Number Number Number))
(: math:max (-> Number Number Number))
(: math:clamp (-> Number Number Number Number))
(: math:mod (-> Number Number Number))
(: math:rem (-> Number Number Number))
(: math:floor-mod (-> Number Number Number))
(: math:pow (-> Number Number Number))
(: math:sqrt (-> Number Number))
(: math:abs (-> Number Number))
(: math:log (-> Number Number Number))
(: math:ln (-> Number Number))
(: math:exp (-> Number Number))
(: math:trunc (-> Number Number))
(: math:ceil (-> Number Number))
(: math:floor (-> Number Number))
(: math:round (-> Number Number))
(: math:isnan (-> Number Bool))
(: math:isinf (-> Number Bool))
(: __cetta_lib_math_binary64_parts (-> Number Atom))
(: math:binary64-parts (-> Number Atom))
(: __cetta_lib_math_integer_divmod (-> Number Number Atom))
(: math:integer-divmod (-> Number Number Atom))

; Exact observation of an existing binary64 value. Finite values are
; (Binary64PartsV1 sign mantissa exponent), denoting sign*mantissa*2^exponent.
; The sign of zero is retained. Nonfinite values are Binary64InfinityV1 sign
; or Binary64NaNV1 sign payload; payload is the original 52-bit fraction.
; Integer values are not silently rounded into binary64.
(= (math:binary64-parts $value)
   (function
     (chain (eval (__cetta_lib_math_binary64_parts $value)) $result (return $result))))

; Exact integer quotient/remainder: a=q*b+r, q=floor(a/b). The nonzero
; remainder has the divisor's sign. Non-integer inputs and zero divisors
; return IntegerDivmodRefusedV1; they are never coerced through binary64.
(= (math:integer-divmod $left $right)
   (function
     (chain (eval (__cetta_lib_math_integer_divmod $left $right)) $result (return $result))))

(@doc math:min
  (@desc "Binary numeric minimum in the math: namespace. This is a library-level helper rather than a new core builtin.")
  (@params (
    (@param "First number")
    (@param "Second number")))
  (@return "Smaller of the two numbers"))

(@doc math:max
  (@desc "Binary numeric maximum in the math: namespace. This is a library-level helper rather than a new core builtin.")
  (@params (
    (@param "First number")
    (@param "Second number")))
  (@return "Larger of the two numbers"))

(@doc math:clamp
  (@desc "Currying-friendly clamp helper with the bounds first and the input value last.")
  (@params (
    (@param "Lower bound")
    (@param "Upper bound")
    (@param "Input value")))
  (@return "Input clamped into the inclusive [min, max] interval"))

(@doc math:mod
  (@desc "Euclidean modulo in the math: namespace. The result is the canonical non-negative remainder modulo the absolute divisor.")
  (@params (
    (@param "Dividend")
    (@param "Divisor")))
  (@return "Non-negative remainder less than the absolute divisor"))

(@doc math:rem
  (@desc "Truncating remainder in the math: namespace. The quotient is rounded toward zero, so a nonzero result has the dividend's sign.")
  (@params (
    (@param "Dividend")
    (@param "Divisor")))
  (@return "Remainder from truncating division"))

(@doc math:floor-mod
  (@desc "Floor-division modulo in the math: namespace. A nonzero result has the divisor's sign.")
  (@params (
    (@param "Dividend")
    (@param "Divisor")))
  (@return "Remainder from floor division"))

(@doc math:pow
  (@desc "Namespaced wrapper over CeTTa's grounded pow-math interface.")
  (@params (
    (@param "Base")
    (@param "Exponent")))
  (@return "Exponentiation result as a Number"))

(@doc math:sqrt
  (@desc "Namespaced wrapper over CeTTa's grounded sqrt-math interface.")
  (@params (
    (@param "Input real number")))
  (@return "Square root as a Number when the input is within the real domain"))

(@doc math:abs
  (@desc "Namespaced wrapper over CeTTa's grounded abs-math interface.")
  (@params (
    (@param "Input number")))
  (@return "Absolute value"))

(@doc math:log
  (@desc "Namespaced wrapper over CeTTa's grounded log-math interface.")
  (@params (
    (@param "Logarithm base")
    (@param "Input value")))
  (@return "Logarithm result as a Number when both arguments are in the real domain"))

(@doc math:ln
  (@desc "Natural logarithm in the math: namespace, implemented as log base EXP.")
  (@params (
    (@param "Input value")))
  (@return "Natural logarithm as a Number when the input is positive"))

(@doc math:exp
  (@desc "Exponential function in the math: namespace, implemented as EXP raised to the given power.")
  (@params (
    (@param "Exponent")))
  (@return "e raised to the given power"))

(@doc math:trunc
  (@desc "Namespaced wrapper over trunc-math.")
  (@params (
    (@param "Input number")))
  (@return "Number truncated toward zero"))

(@doc math:ceil
  (@desc "Namespaced wrapper over ceil-math.")
  (@params (
    (@param "Input number")))
  (@return "Least integer-valued Number greater than or equal to the input"))

(@doc math:floor
  (@desc "Namespaced wrapper over floor-math.")
  (@params (
    (@param "Input number")))
  (@return "Greatest integer-valued Number less than or equal to the input"))

(@doc math:round
  (@desc "Namespaced wrapper over round-math.")
  (@params (
    (@param "Input number")))
  (@return "Number rounded to the nearest integer-valued Number"))

(@doc math:isnan
  (@desc "Namespaced wrapper over isnan-math.")
  (@params (
    (@param "Input number")))
  (@return "True when the input is NaN, otherwise False"))

(@doc math:isinf
  (@desc "Namespaced wrapper over isinf-math.")
  (@params (
    (@param "Input number")))
  (@return "True when the input is infinite, otherwise False"))

(= (math:min $a $b)
   (if (<= $a $b) $a $b))

(= (math:max $a $b)
   (if (>= $a $b) $a $b))

(= (math:clamp $min $max $v)
   (math:min $max (math:max $v $min)))

(= (math:mod $a $b)
   (- $a (* (math:abs $b) (// $a (math:abs $b)))))

(= (math:rem $a $b)
   (let $r (math:mod $a $b)
     (if (< $a 0)
         (if (== $r 0) 0 (- $r (math:abs $b)))
         $r)))

(= (math:floor-mod $a $b)
   (let $r (math:mod $a $b)
     (if (< $b 0)
         (if (== $r 0) 0 (- $r (math:abs $b)))
         $r)))

(= (math:pow $base $exponent)
   (pow-math $base $exponent))

(= (math:sqrt $x)
   (sqrt-math $x))

(= (math:abs $x)
   (abs-math $x))

(= (math:log $base $x)
   (log-math $base $x))

(= (math:ln $x)
   (log-math 2.718281828459045 $x))

(= (math:exp $x)
   (pow-math 2.718281828459045 $x))

(= (math:trunc $x)
   (trunc-math $x))

(= (math:ceil $x)
   (ceil-math $x))

(= (math:floor $x)
   (floor-math $x))

(= (math:round $x)
   (round-math $x))

(= (math:isnan $x)
   (isnan-math $x))

(= (math:isinf $x)
   (isinf-math $x))
