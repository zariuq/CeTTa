; lib/integer: integers of any size as two's complement bit strings that
; extend without end.  Each check names one operation; the last ones are the
; refusals (a call outside the signature is an Error naming what it takes).
!(import! &self integer)

!(assertEqual (integer:and -5 3) 3)
!(assertEqual (integer:or 12 3) 15)
!(assertEqual (integer:xor 5 -1) -6)
!(assertEqual (integer:not 5) -6)
!(assertEqual (integer:not -1) 0)
!(assertEqual (integer:shift-left 1 100) 1267650600228229401496703205376)
!(assertEqual (integer:shift-right -5 1) -3)
!(assertEqual (integer:shift-right 1267650600228229401496703205376 99) 2)
!(assertEqual (integer:bit-length -255) 8)
!(assertEqual (integer:bit-length 0) 0)
!(assertEqual (integer:bit-count -255) 8)
!(assertEqual (integer:pow-mod 3 -1 7) 5)
!(assertEqual (integer:pow-mod 2 10 -7) -5)
!(assertEqual (integer:digits -255 16) "ff")
!(assertEqual (integer:digits 5 2) "101")
!(assertEqual (case (integer:shift-left 1 -1) (((Error $call $why) $why))) "expected an integer and a non-negative count")
!(assertEqual (case (integer:pow-mod 2 3 0) (((Error $call $why) $why))) "expected a nonzero modulus")
!(assertEqual (case (integer:pow-mod 2 -1 4) (((Error $call $why) $why))) "expected a base invertible for the modulus")
!(assertEqual (case (integer:digits 5 1) (((Error $call $why) $why))) "expected an integer and a base from 2 to 36")
!(assertEqual (case (integer:and 1.0 2) (((Error $call $why) $why))) "expected an integer")

!(println! (IntegerLayerV1Summary 20 20 0))
