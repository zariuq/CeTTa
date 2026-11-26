; lib/float: IEEE 754 binary64 as C computes it.  Every expected value below
; follows from IEEE 754 and exact arithmetic; the shortest digits are those
; CPython 3.13's repr gives, including powers of two whose shortest digits are
; not the correctly rounded ones of their length.

!(import! &self float)

; ------------------------------------------------- the shortest digits ----
!(assertEqual (float:digits 0.1) (FloatDigits "" "1" 0))
!(assertEqual (float:digits 0.30000000000000004) (FloatDigits "" "30000000000000004" 0))
!(assertEqual (float:digits 1.0e+16) (FloatDigits "" "1" 17))
!(assertEqual (float:digits 1.0e-05) (FloatDigits "" "1" -4))
!(assertEqual (float:digits 123456789.0) (FloatDigits "" "123456789" 9))
!(assertEqual (float:digits -0.0) (FloatDigits "-" "0" 1))
!(assertEqual (float:digits 0.0) (FloatDigits "" "0" 1))
!(assertEqual (float:digits 5.0e-324) (FloatDigits "" "5" -323))
!(assertEqual (float:digits 1.0e+23) (FloatDigits "" "1" 24))
!(assertEqual (float:digits 1.7976931348623157e+308) (FloatDigits "" "17976931348623157" 309))
!(assertEqual (float:digits 7.120236347223045e-307) (FloatDigits "" "7120236347223045" -306))
!(assertEqual (float:digits 7.291122019556398e-304) (FloatDigits "" "7291122019556398" -303))
!(assertEqual (float:digits 8.209073602596753e-289) (FloatDigits "" "8209073602596753" -288))
!(assertEqual (float:digits 9007199254740992.0) (FloatDigits "" "9007199254740992" 16))
!(assertEqual (float:digits 2.5) (FloatDigits "" "25" 1))
!(assertEqual (float:digits 1.0e+22) (FloatDigits "" "1" 23))
!(assertEqual (float:digits 0.0001) (FloatDigits "" "1" -3))
!(assertEqual (float:digits (float:div 1.0 0.0)) (FloatSpecial "inf"))
!(assertEqual (float:digits (float:div -1.0 0.0)) (FloatSpecial "-inf"))
!(assertEqual (float:digits (float:div 0.0 0.0)) (FloatSpecial "nan"))

; ------------------------------------------- arithmetic never refuses ----
!(assertEqual (float:is-inf (float:mul 1.0e308 10.0)) True)
!(assertEqual (float:is-nan (float:sub (float:mul 1.0e308 10.0) (float:mul 1.0e308 10.0))) True)
!(assertEqual (float:fmod -7.0 2.0) -1.0)
!(assertEqual (float:sign-bit (float:copysign 0.0 -1.0)) True)
!(assertEqual (float:sign-bit (float:neg 0.0)) True)
!(assertEqual (float:floor -2.5) -3.0)
!(assertEqual (float:compare 1.0 (float:div 0.0 0.0)) unordered)
!(assertEqual (float:compare 1.0 2.0) -1)
!(assertEqual (float:pow 2.0 0.5) (FloatPowed 1.4142135623730951 ok))
!(assertEqual (case (float:pow 10.0 400.0) (((FloatPowed $v $why) $why))) range)

; ------------------------------------------------ exact conversions ----
; 2^53+1 lies halfway between two doubles: ties to even.
!(assertEqual (float:from-int 9007199254740993) 9007199254740992.0)
!(assertEqual (float:from-int 9007199254740995) 9007199254740996.0)
!(assertEqual (float:from-ratio 1 3) 0.3333333333333333)
!(assertEqual (float:from-ratio 100000000000000000000000000000000000000000 10000000000000000000000000000000000000000) 10.0)
!(assertEqual (float:sign-bit (float:from-ratio 0 -5)) True)
; 2^-1075 is half the least subnormal: ties to even give zero; 3/2^1076 rounds up to it.
!(assertEqual (float:from-ratio 1 404804506614621236704990693437834614099113299528284236713802716054860679135990693783920767402874248990374155728633623822779617474771586953734026799881477019843034848553132722728933815484186432682479535356945490137124014966849385397236206711298319112681620113024717539104666829230461005064372655017292012526615415482186989568) 0.0)
!(assertEqual (float:from-ratio 3 809609013229242473409981386875669228198226599056568473427605432109721358271981387567841534805748497980748311457267247645559234949543173907468053599762954039686069697106265445457867630968372865364959070713890980274248029933698770794472413422596638225363240226049435078209333658460922010128745310034584025053230830964373979136) 5.0e-324)
!(assertEqual (float:from-int 10000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000) (FloatOverflow))
!(assertEqual (float:to-int 1.0e20) 100000000000000000000)
!(assertEqual (float:to-int -2.7) -2)
!(assertEqual (float:compare-int 9007199254740992.0 9007199254740993) -1)
!(assertEqual (float:compare-int 2.5 2) 1)
!(assertEqual (float:compare-int (float:div 0.0 0.0) 1) unordered)
!(assertEqual (case (float:to-int (float:div 1.0 0.0)) (((Error $call $why) $why))) "expected a finite float")

; ---------------------------------------------------------- decimal text ----
!(assertEqual (float:format 2.675 f 2) "2.67")
!(assertEqual (float:format 1234.5 e 3) "1.234e+03")
!(assertEqual (float:format 0.000123 g 2) "0.00012")
!(assertEqual (float:parse "1e5") 100000.0)
!(assertEqual (float:parse "-.5e-1") -0.05)
!(assertEqual (float:is-inf (float:parse "-Infinity")) True)
!(assertEqual (case (float:parse "nan(1)") (((Error $call $why) $why))) "expected a float's literal")
!(assertEqual (case (float:parse "0x10") (((Error $call $why) $why))) "expected a float's literal")
!(assertEqual (case (float:parse " 1") (((Error $call $why) $why))) "expected a float's literal")
!(assertEqual (case (float:parse "1_0") (((Error $call $why) $why))) "expected a float's literal")

!(assertEqual (float:exp 0.0) 1.0)
!(assertEqual (float:log 1.0) 0.0)
!(assertEqual (float:is-nan (float:log -1.0)) True)
!(assertEqual (float:is-inf (float:exp 1000.0)) True)
!(assertEqual (float:atan2 0.0 -1.0) 3.141592653589793)
!(assertEqual (float:hypot 3.0 4.0) 5.0)
!(assertEqual (float:cbrt -8.0) -2.0)
!(assertEqual (float:fma 2.0 3.0 1.0) 7.0)
!(assertEqual (float:frexp 8.0) (FloatFrexp 0.5 4))
!(assertEqual (float:ldexp 0.5 4) 8.0)
!(assertEqual (float:modf -3.5) (FloatModf -0.5 -3.0))
!(assertEqual (case (float:sin "x") (((Error $call $why) $why))) "expected a float")

!(println! (FloatLayerV1Summary 66 66 0))
