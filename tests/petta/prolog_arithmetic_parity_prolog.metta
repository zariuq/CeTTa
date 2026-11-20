; SWI's flags set in the engine govern PeTTa's arithmetic: prefer_rationals
; makes / exact, float_overflow=infinity lets an infinite result stand, and
; float_underflow=error makes a subnormal result raise.
(= (outcome $x) (case $x (((Error $formal $context) (error $formal)) ($value $value))))
!(let $f (translatePredicate (set_prolog_flag prefer_rationals true)) rationals)
!(println! (rational-division (/ 1 2) (/ 7 22) (* (/ 7 22) 22)))
!(println! (bigint-division (/ 18446744073709551616 3) (/ 18446744073709551616 2) (pow-math 2 -1) (sqrt-math (/ 1 4))))
!(let $f (translatePredicate (set_prolog_flag float_overflow infinity)) infinity)
!(println! (overflow-allowed (* 1.0e308 10.0) (pow-math 10.0 400)))
!(let $f (translatePredicate (set_prolog_flag float_underflow error)) underflow-error)
!(println! (underflow (outcome (catch (* 1.0e-200 1.0e-110))) (outcome (catch (pow-math 10.0 -310))) (outcome (catch (/ 1.0e-300 1.0e10)))))
