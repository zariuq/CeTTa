; A float's printed text reads back as the same float.  The standard printer
; writes the shortest digits that round-trip, and always a point, so the text
; never reads back as an integer or as a nearby float.

(= (text-of $x) (format-args "{}" ($x)))
(= (reads-back $x) (== (parse (text-of $x)) $x))

; The text: shortest digits, with a point.
!(assertEqualToResult (text-of 1.9999999999999998) ("1.9999999999999998"))
!(assertEqualToResult (text-of (+ 0.1 0.2)) ("0.30000000000000004"))
!(assertEqualToResult (text-of 123456789.12345679) ("123456789.12345679"))
!(assertEqualToResult (text-of 0.1) ("0.1"))
!(assertEqualToResult (text-of 2.0) ("2.0"))
!(assertEqualToResult (text-of 9007199254740992.0) ("9007199254740992.0"))
!(assertEqualToResult (text-of 1.0e20) ("1.0e+20"))
!(assertEqualToResult (text-of 1.0e-7) ("1.0e-07"))
!(assertEqualToResult (text-of -2.5e-300) ("-2.5e-300"))
; Without its point an exponent word is a symbol here, so the point is kept.
!(assertEqualToResult (let $a (parse (text-of 1.0e-7)) (get-metatype $a)) (Grounded))
!(assertEqualToResult (reads-back 1.0e20) (True))
!(assertEqualToResult (reads-back 1.0e-7) (True))

; Read back: the same float, never the integer 2 or the float 0.3.
!(assertEqualToResult (reads-back 1.9999999999999998) (True))
!(assertEqualToResult (reads-back (+ 0.1 0.2)) (True))
!(assertEqualToResult (reads-back 123456789.12345679) (True))
!(assertEqualToResult (reads-back 9007199254740992.0) (True))
!(assertEqualToResult (reads-back 1.7976931348623157e308) (True))
!(assertEqualToResult (reads-back 4.9406564584124654e-324) (True))
!(assertEqualToResult (reads-back (/ 1.0 3.0)) (True))
!(assertEqualToResult (let $a (parse (text-of 1.9999999999999998)) (get-type $a)) (Number))
!(assertEqualToResult (== (parse (text-of 1.9999999999999998)) 2) (False))
