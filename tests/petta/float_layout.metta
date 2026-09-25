; Floats print as SWI-Prolog prints them: the shortest digits that read back,
; positional unless that needs four or more zeros of padding next to the
; decimal point, and otherwise d.ddde+NN.
!(println! (positional 123456.7 1790286438.066372 99999.5 1234500.0 1000.0 3.0))
!(println! (padded 12345678901234567890.0 99990.0 0.0001 0.0001234 -1000.0))
!(println! (exponent 10000.0 100000.0 120000.0 -2.5e7 1.0e15 1.0e21 -10000.0))
!(println! (small 0.00001 0.00001234 2.0e-5 5.0e-324))
!(println! (extremes 1.7976931348623157e308 1.5e300 0.0 (* -1.0 0.0)))
!(println! (shortest 0.1 (+ 0.1 0.2) 4.35 0.5 1000.5))
