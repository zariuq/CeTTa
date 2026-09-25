; Arithmetic off the native fast path is SWI's own: named constants evaluate,
; and a zero divisor, an undefined, infinite or subnormal float result, a
; float operand of %, and an operand that is not a number raise SWI's errors,
; unless its float flags say otherwise.  The math functions follow the same
; rule, subnormal literals read as floats, // and numeric-eq are not PeTTa
; functions and stay data, and prefer_rationals makes / exact.
(= (outcome $x) (case $x (((Error $formal $context) (error $formal)) ($value $value))))
!(println! (constants (outcome (catch (+ pi 0))) (outcome (catch (* e 1))) (outcome (catch (+ epsilon 0)))))
!(println! (unknown (outcome (catch (+ foo 1))) (outcome (catch (< foo 1)))))
!(println! (zero-divisor (outcome (catch (/ 1 0))) (outcome (catch (/ 1 0.0))) (outcome (catch (% 1 0)))))
!(println! (undefined (outcome (catch (/ 0.0 0.0)))))
!(println! (overflow (outcome (catch (* 1.0e308 10.0))) (outcome (catch (+ inf 0)))))
!(println! (float-mod (outcome (catch (% 7.5 2)))))
!(println! (data (// 7 2) (numeric-eq 1 1.0)))
!(println! (division (/ 1 2) (/ 4 2) (/ 7 22)))
!(println! (math-domain (outcome (catch (sqrt-math -1))) (outcome (catch (log-math 10 -1))) (outcome (catch (log-math 1 10))) (outcome (catch (asin-math 2))) (outcome (catch (acos-math 2))) (outcome (catch (pow-math -8 0.5))) (outcome (catch (pow-math 0 -1)))))
!(println! (math-overflow (outcome (catch (pow-math 10.0 400))) (outcome (catch (log-math 10 0)))))
!(println! (math-values (pow-math 2 -1) (pow-math 2 100) (sqrt-math 16) (sqrt-math pi) (round-math 2.5) (trunc-math 1.0e20)))
!(println! (subnormal 1.0e-310 (+ 1.0e-310 0.0) (* 1.0e-200 1.0e-110) 1.0e-400 (pow-math 10.0 -310)))
!(let $f (translatePredicate (set_prolog_flag prefer_rationals true)) rationals)
!(println! (rational-division (/ 1 2) (/ 7 22) (* (/ 7 22) 22)))
!(println! (bigint-division (/ 18446744073709551616 3) (/ 18446744073709551616 2) (pow-math 2 -1) (sqrt-math (/ 1 4))))
!(let $f (translatePredicate (set_prolog_flag float_overflow infinity)) infinity)
!(println! (overflow-allowed (* 1.0e308 10.0) (pow-math 10.0 400)))
!(let $f (translatePredicate (set_prolog_flag float_underflow error)) underflow-error)
!(println! (underflow (outcome (catch (* 1.0e-200 1.0e-110))) (outcome (catch (pow-math 10.0 -310))) (outcome (catch (/ 1.0e-300 1.0e10)))))
