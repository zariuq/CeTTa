; CeTTa math helper surface.
; Canonical namespace is math:, and dotted math. is accepted as reader sugar
; and optional display sugar in CLI result output.
; Intentionally conservative: this layer only exposes math operations that are
; already stable in CeTTa or are small, honest library combinators we expect to
; want broadly in WM-PLN, genomics, and AGI-oriented workloads.

(: math:min (-> Number Number Number))
(: math:max (-> Number Number Number))
(: math:clamp (-> Number Number Number Number))
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

(@doc math:pow
  (@desc "Namespaced wrapper over CeTTa's grounded pow-math surface.")
  (@params (
    (@param "Base")
    (@param "Exponent")))
  (@return "Exponentiation result as a Number"))

(@doc math:sqrt
  (@desc "Namespaced wrapper over CeTTa's grounded sqrt-math surface.")
  (@params (
    (@param "Input real number")))
  (@return "Square root as a Number when the input is within the real domain"))

(@doc math:abs
  (@desc "Namespaced wrapper over CeTTa's grounded abs-math surface.")
  (@params (
    (@param "Input number")))
  (@return "Absolute value"))

(@doc math:log
  (@desc "Namespaced wrapper over CeTTa's grounded log-math surface.")
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

(= (math:pow $base $exponent)
   (pow-math $base $exponent))

(= (math:sqrt $x)
   (sqrt-math $x))

(= (math:abs $x)
   (abs-math $x))

(= (math:log $base $x)
   (log-math $base $x))

(= (math:ln $x)
   (log-math EXP $x))

(= (math:exp $x)
   (pow-math EXP $x))

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
