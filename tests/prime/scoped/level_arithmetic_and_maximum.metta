; Among natural numbers the sum, product and power of universe levels are
; those of the numbers: inside (u ...) the expressions (+ 1 2), (* 2 3) and
; (^ 2 3) are the levels 3, 6 and 8.  (max a b) is the larger of two closed
; levels.  Outside a universe these are the ordinary operations.

; The universe at a computed level is the universe at that numeral.
!(type:of (u (+ 1 2)))
!(type:eq (u (+ 1 2)) (u 3))
!(type:eq (u (* 2 3)) (u 6))
!(type:eq (u (^ 2 3)) (u 8))
!(type:eq (u (+ 1 2 3)) (u 6))
!(type:eq (u (+ (* 2 2) (^ 0 0))) (u 5))
; Comparisons that are false after the arithmetic: 1 + 2 is 3, and the
; universe at 3 is a term of the universe at 4 and not of itself; 2 * 3 is
; not 5, and 2^3 is not 9.
!(type:check (u (+ 1 2)) (u 4))
!(type:check (u (+ 1 2)) (u 3))
!(type:eq (u (* 2 3)) (u 5))
!(type:eq (u (^ 2 3)) (u 9))
; A computed number stands where a numeral stands in a notation.
!(type:eq (u (+ omega (+ 1 2))) (u (+ omega 3)))
!(type:eq (u (* omega (* 2 3))) (u (* omega 6)))
!(type:eq (u (^ omega (+ 1 1))) (u (^ omega 2)))
!(type:of (u (+ omega (* 2 2))))

; A declaration at a computed level is a declaration at that numeral.
!(bind! &s (new-space))
!(add-atom &s (: Foo (u (+ 1 2))))
!(type:of &s Foo)
!(type:check &s Foo (u 3))
!(type:check &s Foo (u (* 3 1)))
!(type:check &s Foo (u 2))

; A number has any number of digits, and a number beyond the machine
; integers is the number it is: 2^64, the number after 2^63 - 1, and
; 2^32 * 2^32, which is 2^64 and not 0.
!(type:formed (u (^ 2 64)))
!(try (type:formed (u (^ 2 64))))
!(try (type:formed (u (+ 9223372036854775807 1))))
!(try (type:eq (u (* 4294967296 4294967296)) (u 0)))
!(type:eq (u (* 4294967296 4294967296)) (u (^ 2 64)))
!(type:eq (u (^ 2 64)) (u 18446744073709551616))
!(type:of (u (^ 2 64)))
; A power and a product of many digits, against their numerals.
!(type:eq (u (^ 7 100))
   (u 3234476509624757991344647769100216810857203198904625400933895331391691459636928060001))
!(type:eq
   (u (* 1234567890123456789012345678901234567890
         1234567890123456789012345678901234567890))
   (u 1524157875323883675049535156256668194500533455762536198787501905199875019052100))
; Negative: 2^64 is not the number before it and is no member of its own
; universe, and 7^100 is not the numeral that differs in the last digit.
!(type:eq (u (^ 2 64)) (u 18446744073709551615))
!(type:check (u (^ 2 64)) (u 18446744073709551616))
!(type:eq (u (^ 7 100))
   (u 3234476509624757991344647769100216810857203198904625400933895331391691459636928060002))
; A long number stands where a numeral stands in a notation.
!(type:eq (u (+ omega (^ 2 64))) (u (+ omega 18446744073709551616)))
!(type:of (u (* omega (^ 10 40))))
!(type:eq (u (* omega (^ 10 40))) (u (* omega (^ 10 39))))

; The maximum of two closed levels is the larger one.
!(type:eq (u (max omega 1)) (u omega))
!(type:eq (u (max 2 3)) (u 3))
!(type:eq (u (max 3 2)) (u 3))
!(type:of (u (max 2 3)))
!(type:of (u (max (+ omega 1) (* omega 2))))
!(type:check (u 7) (u (max omega 1)))
; Comparisons that fail: omega is not below itself, the larger of 2 and 3 is
; not 2, and the universe at 3 is no term of the universe at max(2, 3).
!(type:check (u (max omega 1)) (u omega))
!(type:eq (u (max 2 3)) (u 2))
!(type:check (u 3) (u (max 2 3)))
; The maximum of two long numerals is the larger one, and not the smaller.
!(type:eq (u (max 99999999999999999999999999 100000000000000000000000000))
          (u 100000000000000000000000000))
!(type:eq (u (max 99999999999999999999999999 100000000000000000000000000))
          (u 99999999999999999999999999))
; A maximum compares the values of its sides, however they are written:
; 1 + omega is omega, so (max omega (+ 1 omega)) is omega, and
; (max (+ omega 1) (+ 1 omega)) is omega + 1, which is not omega.
!(try (type:formed (u (max omega (+ 1 omega)))))
!(type:eq (u (max omega (+ 1 omega))) (u omega))
!(type:eq (u (max (+ omega 1) (+ 1 omega))) (u (+ omega 1)))
!(type:eq (u (max (+ omega 1) (+ 1 omega))) (u omega))
